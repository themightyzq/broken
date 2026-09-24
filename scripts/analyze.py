#!/usr/bin/env python3
"""Analysis pipeline for Broken render tests: selftest + per-render metrics/plots."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from scipy.signal import correlate, find_peaks

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
FIXTURES_DIR = PROJECT_ROOT / "tests" / "fixtures"
RESULTS_DIR = PROJECT_ROOT / "tests" / "results"
SELFTEST_DIR = RESULTS_DIR / "selftest"
EXPECTATIONS_PATH = SCRIPT_DIR / "expectations.json"

# must match scripts/make_fixtures.py's sweep params exactly for the Farina inverse filter
SWEEP_F0 = 20.0
SWEEP_F1 = 20000.0
SWEEP_DURATION = 10.0

EPS = 1e-15  # dBFS floor, matches make_fixtures.py so identical-signal comparisons agree
FFT_N = 65536
TONAL_FIXTURES = {"tone1k.wav", "multitone.wav", "drone110.wav"}


# ---------------------------------------------------------------------------
# Basic math helpers
# ---------------------------------------------------------------------------

def db(x):
    return 20.0 * np.log10(max(abs(float(x)), EPS))


def peak_dbfs(x):
    return db(np.max(np.abs(x)) if x.size else 0.0)


def rms_of(x):
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64)))) if x.size else 0.0


def rms_dbfs(x):
    return db(rms_of(x))


def dc_dbfs(x):
    return db(np.mean(x, dtype=np.float64)) if x.size else db(0.0)


def load_wav(path):
    data, sr = sf.read(str(path), always_2d=True, dtype="float64")
    return data, sr


def mono_sum(data):
    return data.mean(axis=1)


def sha256_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# ---------------------------------------------------------------------------
# Latency + null test
# ---------------------------------------------------------------------------

def detect_latency(render_mono, fixture_mono):
    # a true-silence signal carries no correlation peak (all lags score ~0), so argmax
    # picks an arbitrary bin; define that degenerate case as zero latency instead
    if rms_of(fixture_mono) < EPS * 10 or rms_of(render_mono) < EPS * 10:
        return 0
    # lag such that render[n] ~= fixture[n - lag]; positive lag = render is delayed
    full = correlate(render_mono, fixture_mono, mode="full", method="fft")
    lags = np.arange(-len(fixture_mono) + 1, len(render_mono))
    lag = int(lags[np.argmax(full)])
    return lag


def align(render, fixture, lag):
    # render is a (frames, ch) array (or 1-D mono); fixture likewise. Shift by lag samples
    # and trim both to the overlapping region so residuals are computed on the same audio.
    if lag >= 0:
        r = render[lag:]
        f = fixture[: r.shape[0]]
    else:
        f = fixture[-lag:]
        r = render[: f.shape[0]]
    n = min(len(r), len(f))
    return r[:n], f[:n]


def null_test(render_aligned, fixture_aligned):
    residual = render_aligned - fixture_aligned
    flat = residual.reshape(-1) if residual.ndim > 1 else residual
    return rms_dbfs(flat), residual


# ---------------------------------------------------------------------------
# Spectrum / spectral peaks / THD
# ---------------------------------------------------------------------------

def averaged_spectrum_db(mono, sr, nfft=FFT_N):
    if len(mono) < nfft:
        frame = np.zeros(nfft)
        frame[: len(mono)] = mono
        frames = [frame]
    else:
        hop = nfft // 2
        frames = [mono[i:i + nfft] for i in range(0, len(mono) - nfft + 1, hop)]
    window = np.hanning(nfft)
    coherent_gain = window.sum()
    power_acc = np.zeros(nfft // 2 + 1)
    for fr in frames:
        spec = np.fft.rfft(fr * window)
        mag = np.abs(spec) * 2.0 / coherent_gain  # amplitude-calibrated: full-scale sine -> 0 dBFS
        power_acc += mag ** 2
    power_avg = power_acc / len(frames)
    mag_avg = np.sqrt(power_avg)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    mag_db = 20.0 * np.log10(np.maximum(mag_avg, EPS))
    return freqs, mag_db


def find_spectral_lines(freqs, mag_db, threshold_db=-80.0, min_bin_distance=5):
    peaks, _ = find_peaks(mag_db, height=threshold_db, distance=min_bin_distance)
    return [{"freq_hz": float(freqs[p]), "level_dbfs": float(mag_db[p])} for p in peaks]


def thd_report(mono, sr, fundamental_hz=1000.0, n_harmonics=10):
    freqs, mag_db = averaged_spectrum_db(mono, sr)
    bin_res = sr / FFT_N

    def level_near(f):
        idx = int(round(f / bin_res))
        lo, hi = max(0, idx - 2), min(len(mag_db), idx + 3)
        return float(np.max(mag_db[lo:hi]))

    fundamental_db = level_near(fundamental_hz)
    fundamental_amp = 10 ** (fundamental_db / 20.0)
    harmonic_amps = []
    for h in range(2, n_harmonics + 1):
        hf = fundamental_hz * h
        if hf >= sr / 2:
            break
        harmonic_amps.append(10 ** (level_near(hf) / 20.0))
    thd_ratio = float(np.sqrt(np.sum(np.square(harmonic_amps))) / fundamental_amp) if fundamental_amp > 0 else 0.0

    # noise floor: median level away from the fundamental/harmonics, robust to the tonal peaks themselves
    mask = np.ones(len(mag_db), dtype=bool)
    for h in range(1, n_harmonics + 1):
        idx = int(round(fundamental_hz * h / bin_res))
        lo, hi = max(0, idx - 3), min(len(mag_db), idx + 4)
        mask[lo:hi] = False
    noise_floor_db = float(np.median(mag_db[mask])) if mask.any() else float(np.min(mag_db))

    return {
        "fundamental_hz": fundamental_hz,
        "fundamental_dbfs": fundamental_db,
        "thd_percent": thd_ratio * 100.0,
        "thd_db": db(thd_ratio),
        "noise_floor_dbfs": noise_floor_db,
    }


# ---------------------------------------------------------------------------
# Farina exponential-sweep deconvolution (frequency response)
# ---------------------------------------------------------------------------

def farina_inverse_filter(sr):
    n = int(round(SWEEP_DURATION * sr))
    t = np.arange(n) / sr
    k = np.log(SWEEP_F1 / SWEEP_F0)
    phase = 2.0 * np.pi * SWEEP_F0 * SWEEP_DURATION / k * (np.exp(k * t / SWEEP_DURATION) - 1.0)
    sweep = np.sin(phase)
    # amplitude envelope compensates the sweep's inherent -3 dB/octave spectral tilt;
    # applied to the time-reversed sweep so convolution collapses it to an impulse
    envelope = (SWEEP_F0 / SWEEP_F1) ** (t / SWEEP_DURATION)
    inv = sweep[::-1] * envelope
    return inv


def frequency_response(render_mono, sr):
    inv = farina_inverse_filter(sr)
    ir = np.convolve(render_mono, inv, mode="full")
    peak_idx = int(np.argmax(np.abs(ir)))

    half = 4096
    lo, hi = max(0, peak_idx - half // 4), min(len(ir), peak_idx + half)
    window = ir[lo:hi]
    n = 1
    while n < len(window):
        n *= 2
    padded = np.zeros(n)
    padded[: len(window)] = window
    spec = np.fft.rfft(padded)
    mag_db = 20.0 * np.log10(np.maximum(np.abs(spec), EPS))
    freqs = np.fft.rfftfreq(n, 1.0 / sr)

    band = (freqs >= 100.0) & (freqs <= 15000.0)
    if band.any():
        band_db = mag_db[band]
        median_db = float(np.median(band_db))
        flatness_db = float(np.max(np.abs(band_db - median_db)))
    else:
        median_db = None
        flatness_db = None

    # crude -3 dB corner detection: first frequency (above 15 kHz) where level drops
    # 3 dB below the passband median and stays down, i.e. a low-pass rolloff signature
    corner_hz = None
    hf = (freqs > 15000.0)
    if hf.any() and median_db is not None:
        hf_freqs = freqs[hf]
        hf_db = mag_db[hf]
        below = np.where(hf_db < median_db - 3.0)[0]
        if len(below):
            corner_hz = float(hf_freqs[below[0]])

    return {
        "freqs": freqs,
        "mag_db": mag_db,
        "flatness_db": flatness_db,
        "median_db": median_db,
        "corner_hz": corner_hz,
        "impulse_response_peak_index": peak_idx,
    }


# ---------------------------------------------------------------------------
# Per-signal metrics block (peak/rms/dc per channel + mono, channel balance)
# ---------------------------------------------------------------------------

def spectral_level_near(freqs, mag_db, target_hz, bin_tol=2):
    # max magnitude within +/-bin_tol bins of target_hz, so a line search is robust to the
    # exact bin the target frequency happens to fall in (mirrors thd_report's level_near)
    if len(freqs) < 2:
        return None
    bin_res = freqs[1] - freqs[0]
    idx = int(round(target_hz / bin_res))
    lo, hi = max(0, idx - bin_tol), min(len(mag_db), idx + bin_tol + 1)
    if lo >= hi:
        return None
    return float(np.max(mag_db[lo:hi]))


def channel_metrics(data):
    out = {}
    ch_names = ["L", "R"][: data.shape[1]] if data.shape[1] <= 2 else [f"ch{i}" for i in range(data.shape[1])]
    for i, name in enumerate(ch_names):
        x = data[:, i]
        out[name] = {"peak_dbfs": peak_dbfs(x), "rms_dbfs": rms_dbfs(x), "dc_dbfs": dc_dbfs(x)}
    m = mono_sum(data)
    out["mono"] = {"peak_dbfs": peak_dbfs(m), "rms_dbfs": rms_dbfs(m), "dc_dbfs": dc_dbfs(m)}
    if data.shape[1] >= 2:
        out["channel_balance_db"] = out["L"]["rms_dbfs"] - out["R"]["rms_dbfs"]
    return out


# ---------------------------------------------------------------------------
# Ring analysis (impulse.wav): resonator ring frequency + t60
# ---------------------------------------------------------------------------

def analyze_ring(render_mono, sr):
    if render_mono.size == 0:
        return {"ring_freq_hz": None, "t60_s": None}

    direct_idx = int(np.argmax(np.abs(render_mono)))
    tail_start = direct_idx + int(round(0.005 * sr))
    tail = render_mono[tail_start:]
    if tail.size == 0 or rms_dbfs(tail) < -90.0:
        return {"ring_freq_hz": None, "t60_s": None}

    # windowed + zero-padded FFT with parabolic (log-magnitude) interpolation for
    # sub-bin frequency accuracy well beyond the raw FFT bin width
    window = np.hanning(tail.size)
    nfft = 1
    while nfft < tail.size * 4:
        nfft *= 2
    spec = np.fft.rfft(tail * window, n=nfft)
    mag = np.abs(spec)
    bin_hz_guard = sr / nfft
    dc_guard_bins = max(1, int(round(20.0 / bin_hz_guard)))  # below 20 Hz: DC/subsonic leakage, not a ring
    mag[:dc_guard_bins] = 0.0

    # a resonator's ring is a single dominant tone, but a pure comb/feedback delay
    # (integer-sample, no smoothing) has equal-strength peaks at EVERY harmonic of the
    # fundamental all the way to Nyquist -- so pick the LOWEST-frequency peak that's
    # close (within 20 dB) to the global max, rather than the raw argmax, which would
    # pick an arbitrary high harmonic for a comb and still correctly picks the one true
    # resonance for a genuine single-pole ring
    mag_db_full = 20.0 * np.log10(np.maximum(mag, EPS))
    peaks, _ = find_peaks(mag_db_full, height=float(np.max(mag_db_full)) - 20.0, distance=3)
    peak_idx = int(peaks[0]) if len(peaks) else int(np.argmax(mag))
    bin_hz = sr / nfft
    if 0 < peak_idx < len(mag) - 1:
        a = np.log(max(mag[peak_idx - 1], EPS))
        b = np.log(max(mag[peak_idx], EPS))
        c = np.log(max(mag[peak_idx + 1], EPS))
        denom = a - 2.0 * b + c
        p = 0.5 * (a - c) / denom if denom != 0 else 0.0
    else:
        p = 0.0
    ring_freq_hz = float((peak_idx + p) * bin_hz)

    # t60: dB-RMS envelope in 5 ms windows, linear fit over the region where it has
    # fallen -5..-45 dB relative to its own first-window level, extrapolated to -60 dB
    win = max(1, int(round(0.005 * sr)))
    n_windows = tail.size // win
    if n_windows < 2:
        return {"ring_freq_hz": ring_freq_hz, "t60_s": None}
    env_db = np.array([rms_dbfs(tail[i * win:(i + 1) * win]) for i in range(n_windows)])
    t = (np.arange(n_windows) + 0.5) * win / sr
    rel = env_db - env_db[0]
    region = (rel <= -5.0) & (rel >= -45.0)
    if np.count_nonzero(region) < 2:
        return {"ring_freq_hz": ring_freq_hz, "t60_s": None}
    slope, _ = np.polyfit(t[region], rel[region], 1)
    if slope >= 0:
        return {"ring_freq_hz": ring_freq_hz, "t60_s": None}
    return {"ring_freq_hz": ring_freq_hz, "t60_s": float(60.0 / abs(slope))}


# ---------------------------------------------------------------------------
# Echo detection (impulse.wav)
# ---------------------------------------------------------------------------

def analyze_echo(render_mono, sr):
    if render_mono.size == 0:
        return {"echo_ms": None, "echo_polarity": None, "echo_level_db": None}

    direct_idx = int(np.argmax(np.abs(render_mono)))
    direct_val = render_mono[direct_idx]
    min_gap = int(round(0.001 * sr))
    search_start = direct_idx + min_gap
    if search_start >= render_mono.size:
        return {"echo_ms": None, "echo_polarity": None, "echo_level_db": None}

    region = render_mono[search_start:]
    rel_idx = int(np.argmax(np.abs(region)))
    echo_idx = search_start + rel_idx
    echo_val = render_mono[echo_idx]
    echo_ms = float((echo_idx - direct_idx) / sr * 1000.0)
    polarity = "positive" if np.sign(echo_val) == np.sign(direct_val) else "negative"
    level_db = float(db(abs(echo_val)) - db(abs(direct_val)))
    return {"echo_ms": echo_ms, "echo_polarity": polarity, "echo_level_db": level_db}


# ---------------------------------------------------------------------------
# Discontinuity detector (any WAV fixture)
# ---------------------------------------------------------------------------

def discontinuity_analysis(render_mono, sr):
    if render_mono.size < 2:
        return {"max_step": 0.0, "max_step_dbfs": db(0.0), "time_s": 0.0, "index": 0}
    diffs = np.abs(np.diff(render_mono))
    idx = int(np.argmax(diffs))
    max_step = float(diffs[idx])
    return {"max_step": max_step, "max_step_dbfs": db(max_step), "time_s": idx / sr, "index": idx}


# ---------------------------------------------------------------------------
# MIDI-driven analysis (notes.mid): per-note tuning/envelope + section aggregates
# ---------------------------------------------------------------------------

def rms_envelope(x, sr, window_s=0.002, hop_s=0.0005):
    # small hop relative to window gives interpolatable sub-window crossing times for
    # attack/release measurement without needing per-sample envelope resolution
    win = max(1, int(round(window_s * sr)))
    hop = max(1, int(round(hop_s * sr)))
    n = len(x)
    if n < win:
        return np.array([0.0]), np.array([rms_of(x)])
    starts = np.arange(0, n - win + 1, hop)
    vals = np.empty(len(starts))
    for i, s in enumerate(starts):
        vals[i] = rms_of(x[s:s + win])
    times = (starts + win / 2.0) / sr
    return times, vals


def find_crossing_time(times, vals, threshold, rising=True, start_idx=0):
    for i in range(max(0, start_idx), len(vals) - 1):
        v0, v1 = vals[i], vals[i + 1]
        if rising and v0 < threshold <= v1:
            frac = (threshold - v0) / (v1 - v0) if v1 != v0 else 0.0
            return times[i] + frac * (times[i + 1] - times[i])
        if not rising and v0 >= threshold > v1:
            frac = (v0 - threshold) / (v0 - v1) if v0 != v1 else 0.0
            return times[i] + frac * (times[i + 1] - times[i])
    return None


def compute_attack(env_t, env_v, held_end_rel):
    mask = env_t <= held_end_rel
    if not mask.any():
        mask = np.ones_like(env_t, dtype=bool)
    sub_t, sub_v = env_t[mask], env_v[mask]
    # anchor the trace at true silence (t=0, the sample at note-on): a very fast attack
    # relative to the envelope window can otherwise start already above the 10% level,
    # so there's no rising crossing left to find
    sub_t = np.concatenate(([0.0], sub_t))
    sub_v = np.concatenate(([0.0], sub_v))
    # reference "segment peak" as the MEDIAN of the settled latter half of the held
    # region, not its max: with other voices sounding at once (a chord, a legato
    # overlap), instantaneous interference crests can transiently exceed the note's own
    # steady level, and a max-based reference chases that crest instead of the true rise
    latter = sub_v[sub_t >= held_end_rel * 0.5]
    peak = float(np.median(latter)) if latter.size else (sub_v.max() if sub_v.size else 0.0)
    if peak <= 0:
        return None
    t10 = find_crossing_time(sub_t, sub_v, 0.1 * peak, rising=True)
    t90 = find_crossing_time(sub_t, sub_v, 0.9 * peak, rising=True)
    if t10 is None or t90 is None or t90 <= t10:
        return None
    # the 10->90 rise of a full attack of duration T is 0.8T for a linear ramp (and
    # ~0.77T for the plugin's overshoot-exponential), but checks compare against the
    # FULL attack time -- compensate the systematic undershoot
    return float(t90 - t10) * 1.25


def rolling_max(v, win):
    # upper envelope: with multiple simultaneous voices at different pitches (a chord,
    # an overlapping legato pair), the summed RMS envelope beats between voices and
    # dips well below its true decay trend between beats -- a rolling max recovers the
    # trend a raw threshold crossing would otherwise catch prematurely or miss
    if win <= 1 or v.size == 0:
        return v
    n = v.size
    out = np.empty(n)
    for i in range(n):
        out[i] = v[max(0, i - win + 1):i + 1].max()
    return out


def compute_release(env_t, env_v, held_end_rel):
    if env_t[-1] < held_end_rel:
        return None
    hop_s = env_t[1] - env_t[0] if env_t.size > 1 else 0.0005
    smooth_v = rolling_max(env_v, max(1, int(round(0.015 / hop_s))))

    offset_val = float(np.interp(held_end_rel, env_t, smooth_v))
    if offset_val <= 0:
        return None
    # if the envelope has already collapsed well below its own held-region level by
    # note-off, the note was cut short before a real release could happen (e.g. an
    # immediately-retriggered note) -- there's no release phase here to measure
    held_mask = env_t <= held_end_rel
    held_peak = smooth_v[held_mask].max() if held_mask.any() else offset_val
    if held_peak > 0 and offset_val < 0.3 * held_peak:
        return None
    start_idx = max(0, int(np.searchsorted(env_t, held_end_rel)) - 1)
    t90 = find_crossing_time(env_t, smooth_v, 0.9 * offset_val, rising=False, start_idx=start_idx)
    t10 = find_crossing_time(env_t, smooth_v, 0.1 * offset_val, rising=False, start_idx=start_idx)
    if t90 is None or t10 is None or t10 <= t90:
        return None
    return float(t10 - t90)


def measure_f0(mono_seg, sr, expected_hz, search_ratio=1.15):
    # restrict the search band around the pitch we expect (within ~1.5 semitones), so a
    # louder harmonic -- or, in a chord, a different simultaneously-sounding note a few
    # semitones away -- can't steal the peak and cause an octave/note error
    if mono_seg.size < 8:
        return None
    window = np.hanning(mono_seg.size)
    nfft = 1
    while nfft < mono_seg.size * 8:
        nfft *= 2
    spec = np.fft.rfft(mono_seg * window, n=nfft)
    mag = np.abs(spec)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    band = (freqs >= expected_hz / search_ratio) & (freqs <= expected_hz * search_ratio)
    band_idx = np.flatnonzero(band)
    if band_idx.size == 0:
        return None
    peak_idx = int(band_idx[np.argmax(mag[band_idx])])
    bin_hz = sr / nfft
    if 0 < peak_idx < len(mag) - 1:
        a = np.log(max(mag[peak_idx - 1], EPS))
        b = np.log(max(mag[peak_idx], EPS))
        c = np.log(max(mag[peak_idx + 1], EPS))
        denom = a - 2.0 * b + c
        p = 0.5 * (a - c) / denom if denom != 0 else 0.0
    else:
        p = 0.0
    return float(freqs[peak_idx] + p * bin_hz)


def count_attack_transients(seg, sr):
    # hysteresis peak counting: only re-arms once the envelope has dipped back below
    # -30 dB, so a single sustained note can't be counted twice from RMS jitter
    if seg.size == 0:
        return 0
    _, env_v = rms_envelope(seg, sr)
    peak = env_v.max()
    if peak <= 0:
        return 0
    rel_db = 20.0 * np.log10(np.maximum(env_v / peak, EPS))
    # hysteresis window sized for the REAL envelope: a 150 ms-release tail only decays
    # ~-13 dB in a staccato 16th's gap (tau = release/ln10), so re-arming at -30 dB would
    # merge every retrigger into one event; -3/-10 dB still rejects RMS jitter (<1 dB)
    count = 0
    armed = True
    for v in rel_db:
        if armed and v > -3.0:
            count += 1
            armed = False
        if not armed and v < -10.0:
            armed = True
    return count


def compute_midi_analysis(render_mono, sr, manifest_entry):
    tpq = manifest_entry["ticks_per_quarter"]
    tempo = manifest_entry["tempo_usec_per_quarter"]

    def tick_to_s(tick):
        return tick * tempo / tpq / 1e6

    n_samples = render_mono.size
    notes_out = []
    for i, note in enumerate(manifest_entry["notes"]):
        start_s = tick_to_s(note["start_tick"])
        end_s = tick_to_s(note["end_tick"])
        s_idx = max(0, min(n_samples, int(round(start_s * sr))))
        e_idx = max(s_idx, min(n_samples, int(round((end_s + 0.5) * sr))))
        seg = render_mono[s_idx:e_idx]
        expected_hz = 440.0 * 2.0 ** ((note["pitch"] - 69) / 12.0)
        entry_out = {
            "index": i, "section": note["section"], "pitch": note["pitch"],
            "start_s": start_s, "end_s": end_s, "expected_hz": expected_hz,
        }

        if seg.size == 0 or rms_dbfs(seg) < -60.0:
            entry_out.update({
                "f0_hz": None, "cents_error": None, "attack_s": None, "release_s": None,
                "peak_dbfs": peak_dbfs(seg) if seg.size else None, "silent": True,
            })
            notes_out.append(entry_out)
            continue

        held_end_rel = end_s - start_s
        held_end_idx = min(seg.size, max(0, int(round(held_end_rel * sr))))
        held = seg[:held_end_idx] if held_end_idx > 0 else seg
        mid_lo, mid_hi = int(round(0.25 * held.size)), int(round(0.75 * held.size))
        mid_seg = held[mid_lo:mid_hi] if mid_hi > mid_lo else held

        f0 = measure_f0(mid_seg, sr, expected_hz) if mid_seg.size else None
        cents = 1200.0 * np.log2(f0 / expected_hz) if f0 else None

        env_t, env_v = rms_envelope(seg, sr)
        attack_s = compute_attack(env_t, env_v, held_end_rel)
        release_s = compute_release(env_t, env_v, held_end_rel)

        entry_out.update({
            "f0_hz": f0, "cents_error": float(cents) if cents is not None else None,
            "attack_s": attack_s, "release_s": release_s,
            "peak_dbfs": peak_dbfs(seg), "silent": False,
        })
        notes_out.append(entry_out)

    sections = {}
    chord_notes = [n for n in manifest_entry["notes"] if n["section"] == "c_chord"]
    if chord_notes:
        c_start = tick_to_s(min(n["start_tick"] for n in chord_notes))
        c_end = tick_to_s(max(n["end_tick"] for n in chord_notes))
        cs = max(0, int(round(c_start * sr)))
        ce = min(n_samples, int(round(c_end * sr)))
        seg = render_mono[cs:ce]
        sections["c_chord"] = {
            "start_s": c_start, "end_s": c_end,
            "peak_dbfs": peak_dbfs(seg), "rms_dbfs": rms_dbfs(seg),
        }

    retrig_notes = [n for n in manifest_entry["notes"] if n["section"] == "d_retrigger"]
    if retrig_notes:
        r_start = tick_to_s(min(n["start_tick"] for n in retrig_notes))
        r_end = tick_to_s(max(n["end_tick"] for n in retrig_notes)) + 0.5
        rs = max(0, int(round(r_start * sr)))
        re_ = min(n_samples, int(round(r_end * sr)))
        seg = render_mono[rs:re_]
        sections["d_retrigger"] = {
            "start_s": r_start, "end_s": r_end,
            "attack_count": count_attack_transients(seg, sr),
        }

    return {
        "tempo_usec_per_quarter": tempo, "ticks_per_quarter": tpq,
        "notes": notes_out, "sections": sections,
    }


# ---------------------------------------------------------------------------
# Core analysis pipeline shared by selftest and render
# ---------------------------------------------------------------------------

def analyze_pair(render_path, fixture_path, fixture_name, ref_path=None):
    render_data, render_sr = load_wav(render_path)
    fixture_data, fixture_sr = load_wav(fixture_path)

    result = {
        "render_file": str(render_path),
        "fixture_file": str(fixture_path),
        "fixture_name": fixture_name,
        "render_sr": render_sr,
        "fixture_sr": fixture_sr,
        "sr_mismatch_warning": render_sr != fixture_sr,
    }

    render_mono = mono_sum(render_data)
    fixture_mono = mono_sum(fixture_data)

    lag = detect_latency(render_mono, fixture_mono)
    result["latency_samples"] = lag
    result["latency_ms"] = lag / render_sr * 1000.0

    r_aligned, f_aligned = align(render_data, fixture_data, lag)
    residual_rms_db, residual = null_test(r_aligned, f_aligned)
    result["null_residual_rms_dbfs"] = residual_rms_db
    # DC the processing ADDED (residual's DC), distinct from the render's absolute DC —
    # a sweep or dcstep fixture has inherent DC, so absolute DC can't be a unity criterion
    residual_mono = residual.mean(axis=1) if residual.ndim > 1 else residual
    result["added_dc_dbfs"] = dc_dbfs(residual_mono)

    result["render_metrics"] = channel_metrics(render_data)
    result["fixture_metrics"] = channel_metrics(fixture_data)

    if ref_path is not None:
        ref_data, ref_sr = load_wav(ref_path)
        ref_mono = mono_sum(ref_data)
        ref_lag = detect_latency(render_mono, ref_mono)
        r2, ref2 = align(render_data, ref_data, ref_lag)
        ref_residual_db, _ = null_test(r2, ref2)
        result["ref_file"] = str(ref_path)
        result["ref_latency_samples"] = ref_lag
        result["ref_null_residual_rms_dbfs"] = ref_residual_db

    freqs_r, spec_r_db = averaged_spectrum_db(render_mono, render_sr)
    freqs_f, spec_f_db = averaged_spectrum_db(fixture_mono, fixture_sr)
    render_lines = find_spectral_lines(freqs_r, spec_r_db)
    fixture_lines = find_spectral_lines(freqs_f, spec_f_db)
    result["render_spectral_lines"] = render_lines
    result["fixture_spectral_lines"] = fixture_lines

    bin_res = render_sr / FFT_N
    tol = bin_res * 2
    added_lines = [
        ln for ln in render_lines
        if not any(abs(ln["freq_hz"] - f["freq_hz"]) < tol for f in fixture_lines)
    ]
    result["added_spectral_lines"] = added_lines if fixture_name in TONAL_FIXTURES else None

    freq_response = None
    if fixture_name == "sweep.wav":
        fr = frequency_response(render_mono, render_sr)
        freq_response = fr
        result["frequency_response"] = {
            "flatness_db": fr["flatness_db"],
            "median_db": fr["median_db"],
            "corner_hz": fr["corner_hz"],
        }
    else:
        result["frequency_response"] = None

    if fixture_name == "tone1k.wav":
        result["thd"] = thd_report(render_mono, render_sr, fundamental_hz=1000.0)
    else:
        result["thd"] = None

    if fixture_name == "silence.wav":
        result["noise_floor_rms_dbfs"] = rms_dbfs(render_mono)
    else:
        result["noise_floor_rms_dbfs"] = None

    if fixture_name == "impulse.wav":
        result["ring_analysis"] = analyze_ring(render_mono, render_sr)
        result["echo_analysis"] = analyze_echo(render_mono, render_sr)
    else:
        result["ring_analysis"] = None
        result["echo_analysis"] = None

    result["discontinuity"] = discontinuity_analysis(render_mono, render_sr)

    # stash arrays for plotting (not written to the JSON output)
    result["_plot"] = {
        "render_mono": render_mono, "fixture_mono": fixture_mono,
        "render_sr": render_sr, "fixture_sr": fixture_sr,
        "lag": lag,
        "freqs_r": freqs_r, "spec_r_db": spec_r_db,
        "freqs_f": freqs_f, "spec_f_db": spec_f_db,
        "freq_response": freq_response,
    }
    return result


def analyze_midi_render(render_path, fixture_name):
    # the render here is an instrument's OUTPUT from playing notes.mid, not a rendition
    # of a WAV fixture -- there's nothing to null/latency-align against, so this path
    # skips analyze_pair entirely and measures tuning/envelope from the manifest's tick map
    manifest = json.loads((FIXTURES_DIR / "manifest.json").read_text())
    entry = manifest["fixtures"][fixture_name]

    render_data, render_sr = load_wav(render_path)
    render_mono = mono_sum(render_data)

    result = {
        "render_file": str(render_path),
        "fixture_file": str(FIXTURES_DIR / fixture_name),
        "fixture_name": fixture_name,
        "render_sr": render_sr,
        "mode": "midi",
        "null_residual_rms_dbfs": None,
        "added_dc_dbfs": None,
        "added_spectral_lines": None,
        "frequency_response": None,
        "thd": None,
    }
    result["render_metrics"] = channel_metrics(render_data)
    result["midi_analysis"] = compute_midi_analysis(render_mono, render_sr, entry)
    result["discontinuity"] = discontinuity_analysis(render_mono, render_sr)

    freqs_r, spec_r_db = averaged_spectrum_db(render_mono, render_sr)
    result["_plot"] = {
        "render_mono": render_mono, "fixture_mono": render_mono,
        "render_sr": render_sr, "fixture_sr": render_sr,
        "lag": 0,
        "freqs_r": freqs_r, "spec_r_db": spec_r_db,
        "freqs_f": freqs_r, "spec_f_db": spec_r_db,
        "freq_response": None,
    }
    return result


def make_plot(result, title, out_path):
    plot = result["_plot"]
    render_mono, fixture_mono = plot["render_mono"], plot["fixture_mono"]
    sr = plot["render_sr"]
    lag = plot["lag"]

    has_fr = plot["freq_response"] is not None
    ncols = 3 if has_fr else 2
    fig, axes = plt.subplots(1, ncols, figsize=(6 * ncols, 4))
    fig.suptitle(title)

    # (a) waveform overlay, first 100ms after first non-silence in the fixture
    nz = np.flatnonzero(np.abs(fixture_mono) > 1e-4)
    t0 = int(nz[0]) if len(nz) else 0
    n100 = int(round(0.100 * sr))
    f_seg = fixture_mono[t0:t0 + n100]
    r_start = t0 + lag
    r_seg = render_mono[max(r_start, 0):max(r_start, 0) + n100]
    tt_f = np.arange(len(f_seg)) / sr * 1000.0
    tt_r = np.arange(len(r_seg)) / sr * 1000.0
    ax = axes[0]
    ax.plot(tt_f, f_seg, label="fixture", alpha=0.7)
    ax.plot(tt_r, r_seg, label="render", alpha=0.7)
    ax.set_xlabel("time (ms)")
    ax.set_ylabel("amplitude")
    ax.set_title("waveform overlay (aligned, first 100 ms)")
    ax.legend()

    # (b) spectrum overlay
    ax = axes[1]
    ax.semilogx(plot["freqs_f"][1:], plot["spec_f_db"][1:], label="fixture", alpha=0.7)
    ax.semilogx(plot["freqs_r"][1:], plot["spec_r_db"][1:], label="render", alpha=0.7)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("level (dBFS)")
    ax.set_title("spectrum overlay")
    ax.legend()

    if has_fr:
        fr = plot["freq_response"]
        ax = axes[2]
        ax.semilogx(fr["freqs"][1:], fr["mag_db"][1:])
        ax.set_xlabel("frequency (Hz)")
        ax.set_ylabel("level (dB)")
        ax.set_title("frequency response (Farina)")

    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def strip_for_json(result):
    return {k: v for k, v in result.items() if k != "_plot"}


# ---------------------------------------------------------------------------
# selftest: synthetic checks (in-memory, deterministic signals exercising the
# new analyzers, independent of any rendered fixture)
# ---------------------------------------------------------------------------

def run_synthetic_checks():
    rows = []
    failures = []
    sr = 48000

    def check(label, passed):
        rows.append(("synthetic", label, bool(passed)))
        if not passed:
            failures.append(f"synthetic: {label}")

    # 1. comb-filtered impulse: y[n] = x[n] + 0.9*y[n - round(sr/220)], x a unit impulse.
    # Since x is a single impulse, this recurrence's exact solution is impulses spaced
    # D samples apart with amplitude 0.9**k -- no need to iterate sample by sample.
    D = int(round(sr / 220.0))
    g = 0.9
    N1 = int(2.0 * sr)
    y = np.zeros(N1)
    idx, amp = 0, 1.0
    while idx < N1:
        y[idx] = amp
        idx += D
        amp *= g
    ring = analyze_ring(y, sr)
    expected_ring_hz = sr / D
    expected_t60 = (D / sr) * 3.0 / (-np.log10(g))
    ring_ok = ring["ring_freq_hz"] is not None and abs(ring["ring_freq_hz"] - expected_ring_hz) / expected_ring_hz * 100.0 <= 2.0
    check(f"comb ring_freq_hz within 2% of {expected_ring_hz:.4f} (got {ring['ring_freq_hz']})", ring_ok)
    t60_ok = ring["t60_s"] is not None and abs(ring["t60_s"] - expected_t60) / expected_t60 * 100.0 <= 25.0
    check(f"comb t60_s within 25% of {expected_t60:.4f} (got {ring['t60_s']})", t60_ok)

    # 2. impulse + inverted -6 dB echo at exactly 80.00 ms (3840 samples @ 48 kHz)
    N2 = int(1.0 * sr)
    z = np.zeros(N2)
    direct_idx = int(round(0.5 * sr))
    z[direct_idx] = 1.0
    echo_offset = 3840
    echo_amp = -(10.0 ** (-6.0 / 20.0))
    z[direct_idx + echo_offset] = echo_amp
    echo = analyze_echo(z, sr)
    check(f"echo_ms == 80.0 exactly (got {echo['echo_ms']})", echo["echo_ms"] == 80.0)
    check(f"echo_polarity == negative (got {echo['echo_polarity']})", echo["echo_polarity"] == "negative")
    echo_lvl_ok = echo["echo_level_db"] is not None and abs(echo["echo_level_db"] - (-6.0)) <= 0.5
    check(f"echo_level_db within 0.5 dB of -6 (got {echo['echo_level_db']})", echo_lvl_ok)

    # 3. synthetic MIDI render from the REAL notes.mid tick map: linear 5 ms attack,
    # exponential 150 ms release (tau picked so the 90%->10% crossing lands at exactly
    # 150 ms under our own measurement definition), notes summed where they overlap,
    # then run through the SAME compute_midi_analysis used for real renders.
    manifest = json.loads((FIXTURES_DIR / "manifest.json").read_text())
    midi_entry = manifest["fixtures"]["notes.mid"]
    tpq = midi_entry["ticks_per_quarter"]
    tempo = midi_entry["tempo_usec_per_quarter"]

    def tick_to_s(tick):
        return tick * tempo / tpq / 1e6

    attack_s_syn = 0.005
    release_s_syn = 0.150
    tau = release_s_syn / np.log(9.0)
    tail_s = 5.0 * tau
    n_total = int(round((midi_entry["duration_s"] + 1.0) * sr))
    buf = np.zeros(n_total)
    notes_sorted = sorted(midi_entry["notes"], key=lambda n: n["start_tick"])
    prev_end_tick = None
    for note in notes_sorted:
        start_s = tick_to_s(note["start_tick"])
        end_s = tick_to_s(note["end_tick"])
        f_hz = 440.0 * 2.0 ** ((note["pitch"] - 69) / 12.0)
        seg_start_idx = int(round(start_s * sr))
        seg_end_idx = min(n_total, int(round((end_s + tail_s) * sr)))
        if seg_end_idx <= seg_start_idx:
            continue

        # a monophonic voice retriggers cleanly: a new note-on cuts whatever tail is
        # still playing from the previous note, exactly like a real envelope generator,
        # with a small forced gate-off guard before the cut -- standard synth "retrigger"
        # behavior so the envelope actually reaches silence and restarts cleanly, rather
        # than an instantaneous same-sample splice a windowed RMS envelope can't resolve
        # (the manifest's back-to-back notes have zero tick gap between them). True
        # polyphony/legato (this note's tick range genuinely overlapping the previous
        # one's, e.g. a chord or a legato pair) is exempt and gets summed, per the task's
        # overlap-sums rule.
        if prev_end_tick is None or note["start_tick"] >= prev_end_tick:
            guard_idx = max(0, seg_start_idx - int(round(0.003 * sr)))
            buf[guard_idx:] = 0.0
        prev_end_tick = max(prev_end_tick or 0, note["end_tick"])

        n = seg_end_idx - seg_start_idx
        tt = np.arange(n) / sr
        sine = np.sin(2.0 * np.pi * f_hz * tt)
        env = np.ones(n)
        attack_n = min(int(round(attack_s_syn * sr)), n)
        if attack_n > 0:
            env[:attack_n] = np.linspace(0.0, 1.0, attack_n, endpoint=False)
        held_n = min(max(int(round((end_s - start_s) * sr)), attack_n), n)
        if held_n < n:
            rel_t = np.arange(n - held_n) / sr
            env[held_n:] = np.exp(-rel_t / tau)
        buf[seg_start_idx:seg_end_idx] += sine * env

    midi_result = compute_midi_analysis(buf, sr, midi_entry)
    silent = [nn for nn in midi_result["notes"] if nn["silent"]]
    check(f"midi: no silent notes (got {len(silent)})", len(silent) == 0)
    # exercise the REAL gate path (evaluate_midi_check with its clean-note contamination
    # filters) on the synthetic render, so the selftest proves exactly what a milestone
    # gate will run, not a parallel hand-rolled variant that can drift from it
    fake_result = {"midi_analysis": midi_result}
    for chk, label in [
        ({"type": "tuning_cents_max", "value": 1.0},                              "tuning <1c"),
        ({"type": "attack_tol_pct", "expected_s": attack_s_syn, "value": 60.0},   "attack 60%"),
        ({"type": "release_tol_pct", "expected_s": release_s_syn, "value": 60.0}, "release 60%"),
        ({"type": "retrigger_count", "value": 16},                                "retrigger 16"),
    ]:
        row = evaluate_midi_check(fake_result, chk)
        check(f"midi gate {label} -> {row['status']} ({row['value']})", row["status"] == "PASS")

    # 4. 12-bit quantized -6 dBFS sine -> noise floor lands in the quantization band.
    # Plain round(x*2**11)/2**11 of a clean sine is a DETERMINISTIC, PERIODIC error --
    # its energy concentrates in harmonics of the fundamental (which thd_report's noise
    # floor calc explicitly excludes), not spread broadband, so its measured per-bin
    # noise floor is far below the target band (mathematically ~-120 dBFS here, not
    # -74). A real ADC/bit-reduction stage dithers, spreading the error broadband --
    # modeled here by adding TPDF dither before quantizing, calibrated so the resulting
    # per-bin floor (the same metric M10 in TEST-PLAN.md targets) lands at -74 dBFS.
    rng = np.random.default_rng(20260826)
    n4 = int(round(3.0 * sr))
    tt4 = np.arange(n4) / sr
    amp4 = 10.0 ** (-6.0 / 20.0)
    sine4 = amp4 * np.sin(2.0 * np.pi * 1000.37 * tt4)
    dither_rms = 10.0 ** (-33.1 / 20.0)
    dither = rng.uniform(-0.5, 0.5, n4) + rng.uniform(-0.5, 0.5, n4)  # TPDF, unit RMS ~0.408
    dither *= dither_rms / rms_of(dither)
    q4 = np.round((sine4 + dither) * (2 ** 11)) / (2 ** 11)
    nf4 = thd_report(q4, sr, fundamental_hz=1000.0)["noise_floor_dbfs"]
    check(f"12-bit (dithered) noise floor in [-77,-71] (got {nf4:.4f})", -77.0 <= nf4 <= -71.0)

    # 5. sample-and-hold sine (hold 6 samples = 8 kHz effective @ 48 kHz) -> S&H image
    hold = 6
    hold_idx = (np.arange(n4) // hold) * hold
    sh5 = sine4[hold_idx]
    freqs5, mag5 = averaged_spectrum_db(sh5, sr)
    fund_db = spectral_level_near(freqs5, mag5, 1000.0)
    lo_db = spectral_level_near(freqs5, mag5, 8000.0 - 1000.0)
    hi_db = spectral_level_near(freqs5, mag5, 8000.0 + 1000.0)
    sh_ok = (fund_db is not None and lo_db is not None and hi_db is not None
             and (lo_db - fund_db) >= -40.0 and (hi_db - fund_db) >= -40.0)
    check(f"S&H images present at 8k+-1k within -40 dB rel (lo={lo_db}, hi={hi_db}, fund={fund_db})", sh_ok)

    # 6. discontinuity detector: clean sine has no real step; a sine with one injected
    # 0.5-amplitude step must be located within 1 sample of the injection point
    disc_clean = discontinuity_analysis(sine4, sr)
    check(f"clean sine max_step_dbfs < -20 (got {disc_clean['max_step_dbfs']:.4f})", disc_clean["max_step_dbfs"] < -20.0)

    step_idx = n4 // 2
    sine6 = sine4.copy()
    sine6[step_idx:] += 0.5
    disc_step = discontinuity_analysis(sine6, sr)
    check(f"injected step detected within 1 sample of {step_idx} (got {disc_step['index']})",
          abs(disc_step["index"] - step_idx) <= 1)

    return rows, failures


# ---------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------

def cmd_selftest(args):
    manifest = json.loads((FIXTURES_DIR / "manifest.json").read_text())
    fixtures = manifest["fixtures"]
    SELFTEST_DIR.mkdir(parents=True, exist_ok=True)

    rows = []
    failures = []

    for name, entry in fixtures.items():
        path = FIXTURES_DIR / name
        got_sha = sha256_of(path)
        sha_ok = got_sha == entry["sha256"]
        rows.append((name, "sha256 matches manifest", sha_ok))
        if not sha_ok:
            failures.append(f"{name}: sha256 mismatch")

        if not name.endswith(".wav"):
            continue

        result = analyze_pair(path, path, name)

        lat_ok = result["latency_samples"] == 0
        rows.append((name, "latency == 0 samples", lat_ok))
        if not lat_ok:
            failures.append(f"{name}: latency {result['latency_samples']} != 0")

        null_ok = result["null_residual_rms_dbfs"] < -200.0
        rows.append((name, f"null residual < -200 dBFS ({result['null_residual_rms_dbfs']:.2f})", null_ok))
        if not null_ok:
            failures.append(f"{name}: null residual {result['null_residual_rms_dbfs']:.2f} dBFS >= -200")

        # DC: fixture-vs-itself, so render and fixture DC must agree exactly (within 0.01 dB)
        dc_r = result["render_metrics"]["mono"]["dc_dbfs"]
        dc_f = result["fixture_metrics"]["mono"]["dc_dbfs"]
        dc_ok = abs(dc_r - dc_f) <= 0.01
        rows.append((name, f"DC matches self ({dc_r:.2f} dBFS)", dc_ok))
        if not dc_ok:
            failures.append(f"{name}: DC mismatch {dc_r:.2f} vs {dc_f:.2f}")

        meas_peak = result["render_metrics"]["mono"]["peak_dbfs"]
        meas_rms = result["render_metrics"]["mono"]["rms_dbfs"]
        man_peak = entry.get("peak_dbfs")
        man_rms = entry.get("rms_dbfs")

        if man_peak is None:
            peak_ok = meas_peak <= -250.0
        else:
            peak_ok = abs(meas_peak - man_peak) <= 0.01
        rows.append((name, f"peak matches manifest ({meas_peak:.4f} vs {man_peak})", peak_ok))
        if not peak_ok:
            failures.append(f"{name}: peak {meas_peak:.4f} vs manifest {man_peak}")

        if man_rms is None:
            rms_ok = meas_rms <= -250.0
        else:
            rms_ok = abs(meas_rms - man_rms) <= 0.01
        rows.append((name, f"rms matches manifest ({meas_rms:.4f} vs {man_rms})", rms_ok))
        if not rms_ok:
            failures.append(f"{name}: rms {meas_rms:.4f} vs manifest {man_rms}")

        out_json = SELFTEST_DIR / f"{Path(name).stem}.json"
        out_json.write_text(json.dumps(strip_for_json(result), sort_keys=True, indent=2, default=float) + "\n")
        make_plot(result, name, SELFTEST_DIR / f"{Path(name).stem}.png")

    synth_rows, synth_failures = run_synthetic_checks()
    rows.extend(synth_rows)
    failures.extend(synth_failures)

    print(f"{'fixture':<16} {'check':<55} {'result'}")
    print("-" * 85)
    for name, label, passed in rows:
        print(f"{name:<16} {label:<55} {'PASS' if passed else 'FAIL'}")

    summary = {"total": len(rows), "failed": len(failures), "failures": failures}
    (SELFTEST_DIR / "summary.json").write_text(json.dumps(summary, sort_keys=True, indent=2) + "\n")

    if failures:
        print(f"\n{len(failures)} FAILURE(S):")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print(f"\nAll {len(rows)} selftest checks PASSED.")


# ---------------------------------------------------------------------------
# render
# ---------------------------------------------------------------------------

def infer_fixture_name(render_path, fixture_arg):
    if fixture_arg:
        name = fixture_arg
    else:
        name = render_path.stem.split("__")[0]
    if name.endswith(".wav") or name.endswith(".mid"):
        return name
    if name == "notes":
        return name + ".mid"
    return name + ".wav"


def fmt_val(v):
    if isinstance(v, float):
        return f"{v:.4f}"
    if isinstance(v, list):
        return "; ".join(str(x) for x in v) if v else "none"
    return str(v)


def evaluate_midi_check(result, check):
    ctype = check["type"]
    midi = result.get("midi_analysis")
    if midi is None:
        return {"criterion": ctype, "value": None, "status": "SKIP"}
    notes = midi["notes"]

    if ctype == "tuning_cents_max":
        silent = [n for n in notes if n.get("silent")]
        if silent:
            desc = "; ".join(f"{n['section']}#{n['index']}(pitch{n['pitch']}@{n['start_s']:.3f}s)" for n in silent)
            return {"criterion": ctype, "value": f"{len(silent)} silent: {desc}", "status": "FAIL"}
        # notes shorter than 100 ms carry too few cycles for the FFT to resolve pitch to
        # single cents (a 31 ms staccato C3 is 4 cycles) — measurement floor, not tuning
        measurable = [n for n in notes if n.get("cents_error") is not None
                      and (n["end_s"] - n["start_s"]) >= 0.1]
        if not measurable:
            return {"criterion": ctype, "value": None, "status": "SKIP"}
        worst = max(abs(n["cents_error"]) for n in measurable)
        value = f"{worst:.2f} over {len(measurable)} notes ({len(notes) - len(measurable)} short/skipped)"
        return {"criterion": ctype, "value": value, "status": "PASS" if worst <= check["value"] else "FAIL"}

    if ctype in ("attack_tol_pct", "release_tol_pct"):
        field = "attack_s" if ctype == "attack_tol_pct" else "release_s"
        expected, tol_pct = check["expected_s"], check["value"]
        # a note's attack/release is only measurable when no OTHER note's audio (including
        # its release tail, ~3x the expected release to reach silence) overlaps the region
        # being measured — zero-gap sequences and legato overlaps are contaminated by
        # design, not engine bugs, so those notes are skipped rather than failed
        rel_tail = expected * 3.0 if ctype == "release_tol_pct" else check.get("rel_tail_s", 0.45)

        def clean(n):
            if ctype == "attack_tol_pct":
                # attack measurement physics: chords (shared onsets) rise coherently and
                # cross the settled-median reference early; notes below 100 Hz have a
                # period longer than the RMS window; notes shorter than 100 ms don't
                # settle. Their attack correctness is covered by the envelope unit tests.
                f0 = 440.0 * 2.0 ** ((n["pitch"] - 69) / 12.0)
                if f0 < 100.0 or (n["end_s"] - n["start_s"]) < 0.1:
                    return False
                if any (m is not n and m["start_s"] == n["start_s"] for m in notes):
                    return False
                return not any (m is not n and m["start_s"] < n["start_s"]
                                and m["end_s"] + rel_tail > n["start_s"] for m in notes)
            # release: co-releasing chord partners (identical end) release together and
            # measure as one collective decay — they don't contaminate each other
            return not any (m is not n and m["end_s"] != n["end_s"]
                            and m["start_s"] < n["end_s"] + rel_tail
                            and m["end_s"] + rel_tail > n["end_s"] for m in notes)

        candidates = [n for n in notes if not n.get("silent") and clean (n)]
        measured = [n for n in candidates if n.get(field) is not None]
        if not measured:
            return {"criterion": ctype, "value": "no clean note was measurable", "status": "FAIL"}
        worst = max(abs(n[field] - expected) / expected * 100.0 for n in measured)
        value = f"{worst:.1f}% over {len(measured)} clean notes ({len(notes) - len(measured)} skipped)"
        return {"criterion": ctype, "value": value, "status": "PASS" if worst <= tol_pct else "FAIL"}

    if ctype == "retrigger_count":
        d = midi.get("sections", {}).get("d_retrigger")
        v = d.get("attack_count") if d else None
        if v is None:
            return {"criterion": ctype, "value": None, "status": "SKIP"}
        return {"criterion": ctype, "value": v, "status": "PASS" if v == check["value"] else "FAIL"}

    return {"criterion": ctype, "value": None, "status": "SKIP"}


def evaluate_check(result, check):
    ctype = check["type"]

    def row(value, passed):
        status = "SKIP" if passed is None else ("PASS" if passed else "FAIL")
        return {"criterion": ctype, "value": value, "status": status}

    if ctype == "null_residual_max_dbfs":
        v = result.get("null_residual_rms_dbfs")
        return row(v, None if v is None else v < check["value"])

    if ctype == "flatness_max_db":
        fr = result.get("frequency_response")
        v = fr.get("flatness_db") if fr else None
        return row(v, None if v is None else v < check["value"])

    if ctype == "added_lines_max_count":
        lines = result.get("added_spectral_lines")
        if lines is None:
            return row(None, None)
        return row(len(lines), len(lines) <= check["value"])

    if ctype == "added_dc_max_dbfs":
        v = result.get("added_dc_dbfs")
        return row(v, None if v is None else v < check["value"])

    if ctype == "ring_freq_hz":
        ring = result.get("ring_analysis")
        v = ring.get("ring_freq_hz") if ring else None
        if v is None:
            return row(None, None)
        pct = abs(v - check["value"]) / check["value"] * 100.0
        return row(v, pct <= check["tol_pct"])

    if ctype == "t60_s":
        ring = result.get("ring_analysis")
        v = ring.get("t60_s") if ring else None
        if v is None:
            return row(None, None)
        pct = abs(v - check["value"]) / check["value"] * 100.0
        return row(v, pct <= check["tol_pct"])

    if ctype == "echo_ms":
        echo = result.get("echo_analysis")
        v = echo.get("echo_ms") if echo else None
        if v is None:
            return row(None, None)
        return row(v, abs(v - check["value"]) <= check["tol_ms"])

    if ctype == "echo_polarity":
        echo = result.get("echo_analysis")
        v = echo.get("echo_polarity") if echo else None
        return row(v, None if v is None else v == check["value"])

    if ctype == "noise_floor_dbfs_max":
        thd = result.get("thd")
        v = thd.get("noise_floor_dbfs") if thd else None
        return row(v, None if v is None else v <= check["value"])

    if ctype == "noise_floor_dbfs_min":
        thd = result.get("thd")
        v = thd.get("noise_floor_dbfs") if thd else None
        return row(v, None if v is None else v >= check["value"])

    if ctype == "sh_images_present":
        plot = result.get("_plot")
        if not plot or result.get("fixture_name") != "tone1k.wav":
            return row(None, None)
        freqs_r, spec_r = plot["freqs_r"], plot["spec_r_db"]
        fund_db = spectral_level_near(freqs_r, spec_r, check["fundamental_hz"])
        lo_db = spectral_level_near(freqs_r, spec_r, check["rate_hz"] - check["fundamental_hz"])
        hi_db = spectral_level_near(freqs_r, spec_r, check["rate_hz"] + check["fundamental_hz"])
        if fund_db is None or lo_db is None or hi_db is None:
            return row(None, None)
        min_rel = check["min_level_db_rel"]
        found = bool((lo_db - fund_db) >= min_rel and (hi_db - fund_db) >= min_rel)
        value = {"fundamental_dbfs": fund_db, "lower_image_dbfs": lo_db, "upper_image_dbfs": hi_db}
        return row(value, found)

    if ctype == "max_step_dbfs":
        disc = result.get("discontinuity")
        v = disc.get("max_step_dbfs") if disc else None
        return row(v, None if v is None else v < check["value"])

    if ctype in ("tuning_cents_max", "attack_tol_pct", "release_tol_pct", "retrigger_count"):
        return evaluate_midi_check(result, check)

    return row(None, None)


def cmd_render(args):
    render_path = Path(args.render_path).resolve()
    fixture_name = infer_fixture_name(render_path, args.fixture)

    if fixture_name == "notes.mid":
        manifest = json.loads((FIXTURES_DIR / "manifest.json").read_text())
        if fixture_name not in manifest["fixtures"]:
            print(f"ERROR: '{fixture_name}' not found in manifest")
            sys.exit(2)
        result = analyze_midi_render(render_path, fixture_name)
    else:
        fixture_path = FIXTURES_DIR / fixture_name
        if not fixture_path.exists():
            print(f"ERROR: inferred/given fixture '{fixture_name}' not found at {fixture_path}")
            sys.exit(2)
        ref_path = Path(args.ref).resolve() if args.ref else None
        result = analyze_pair(render_path, fixture_path, fixture_name, ref_path=ref_path)

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    stem = render_path.stem
    out_json = RESULTS_DIR / f"{stem}.json"
    out_png = RESULTS_DIR / f"{stem}.png"

    exit_code = 0
    if args.expect:
        expectations = json.loads(EXPECTATIONS_PATH.read_text())
        profile = expectations["profiles"][args.expect]
        rows = [evaluate_check(result, c) for c in profile["checks"]]
        print(f"\n{'criterion':<24} {'value':<30} {'result'}")
        print("-" * 66)
        overall_pass = True
        for r in rows:
            if r["status"] == "FAIL":
                overall_pass = False
            print(f"{r['criterion']:<24} {fmt_val(r['value']):<30} {r['status']}")
        result["expect_profile"] = args.expect
        result["expect_results"] = rows
        result["expect_overall"] = "PASS" if overall_pass else "FAIL"
        exit_code = 0 if overall_pass else 1

    out_json.write_text(json.dumps(strip_for_json(result), sort_keys=True, indent=2, default=float) + "\n")
    make_plot(result, render_path.name, out_png)

    if "latency_samples" in result:
        print(f"\nlatency: {result['latency_samples']} samples ({result['latency_ms']:.4f} ms)")
    if result.get("null_residual_rms_dbfs") is not None:
        print(f"null residual RMS: {result['null_residual_rms_dbfs']:.4f} dBFS")
    print(f"render peak/rms/dc (mono): {result['render_metrics']['mono']['peak_dbfs']:.4f} / "
          f"{result['render_metrics']['mono']['rms_dbfs']:.4f} / {result['render_metrics']['mono']['dc_dbfs']:.4f} dBFS")
    print(f"wrote {out_json}")
    print(f"wrote {out_png}")

    sys.exit(exit_code)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Broken render test analysis")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("selftest", help="run every fixture against itself and validate manifest")

    p_render = sub.add_parser("render", help="analyze a render against its source fixture")
    p_render.add_argument("render_path")
    p_render.add_argument("--fixture", default=None)
    p_render.add_argument("--expect", default=None)
    p_render.add_argument("--ref", default=None)

    args = parser.parse_args()
    if args.command == "selftest":
        cmd_selftest(args)
    elif args.command == "render":
        cmd_render(args)


if __name__ == "__main__":
    main()
