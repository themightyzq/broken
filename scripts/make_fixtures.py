#!/usr/bin/env python3
"""Generate the deterministic audio/MIDI fixture set for the TurboSynth test harness.

Every fixture is pure function of its seed/params below -> re-running this script
must produce byte-identical files (verified by the self-check at the bottom).
"""
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

SCRIPT_DIR = Path(__file__).resolve().parent
FIXTURES_DIR = SCRIPT_DIR.parent / "tests" / "fixtures"

# default 48000; `make_fixtures.py --sr 44100` writes a parity set into
# tests/fixtures/sr44100/ with its own manifest (suite-14 sample-rate parity)
SR = 48000
if "--sr" in sys.argv:
    SR = int(sys.argv[sys.argv.index("--sr") + 1])
    if SR != 48000:
        FIXTURES_DIR = FIXTURES_DIR / f"sr{SR}"
PEAK_M6_AMP = 0.5011872336272722  # spec-given exact value for -6 dBFS, not recomputed
RMS_M18_AMP = 10 ** (-18.0 / 20.0)

# Sweep parameters shared conceptually with analyze.py's Farina inverse filter;
# duplicated there rather than imported since scripts must run standalone.
SWEEP_F0 = 20.0
SWEEP_F1 = 20000.0
SWEEP_DURATION = 10.0

EPS = 1e-15  # dBFS floor so log10 never sees an exact zero (true silence -> -300 dB, not -inf)


def amp_to_db(x):
    return 20.0 * np.log10(max(abs(float(x)), EPS))


def rms_of(x):
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64))))


def peak_of(x):
    return float(np.max(np.abs(x))) if x.size else 0.0


def n_samples(duration_s):
    return int(round(duration_s * SR))


def raised_cosine_fade(sig, fade_s):
    n = int(round(fade_s * SR))
    if n <= 0 or n * 2 > sig.size:
        return sig
    ramp = 0.5 * (1.0 - np.cos(np.pi * np.arange(n) / n))
    sig = sig.copy()
    sig[:n] *= ramp
    sig[-n:] *= ramp[::-1]
    return sig


def scale_to_peak(sig, target_amp):
    p = peak_of(sig)
    return sig if p == 0 else sig * (target_amp / p)


def scale_to_rms(sig, target_amp):
    r = rms_of(sig)
    return sig if r == 0 else sig * (target_amp / r)


def sawtooth_naive(t, freq):
    # naive (aliased) sawtooth: intentional per spec, not band-limited
    phase = t * freq
    return 2.0 * (phase - np.floor(phase + 0.5))


def log_sweep_phase(t, f0, f1, duration):
    # closed-form integral of instantaneous exponential frequency f0*(f1/f0)^(t/T)
    k = np.log(f1 / f0)
    return 2.0 * np.pi * f0 * duration / k * (np.exp(k * t / duration) - 1.0)


# ---------------------------------------------------------------------------
# Fixture builders. Each returns a mono float64 array in [-1, 1].
# ---------------------------------------------------------------------------

def build_sweep():
    n = n_samples(SWEEP_DURATION)
    t = np.arange(n) / SR
    sig = np.sin(log_sweep_phase(t, SWEEP_F0, SWEEP_F1, SWEEP_DURATION))
    sig = raised_cosine_fade(sig, 0.050)
    return scale_to_peak(sig, PEAK_M6_AMP)


def build_impulse():
    n = n_samples(2.0)
    sig = np.zeros(n)
    sig[n_samples(0.5)] = 0.5
    return sig


def build_white():
    n = n_samples(5.0)
    rng = np.random.default_rng(4801)
    sig = rng.uniform(-1.0, 1.0, n)
    return scale_to_rms(sig, RMS_M18_AMP)


def build_pink():
    n = n_samples(5.0)
    rng = np.random.default_rng(4802)
    white = rng.standard_normal(n)
    spec = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    scale = np.ones_like(freqs)
    scale[1:] = 1.0 / np.sqrt(freqs[1:])
    scale[0] = 0.0  # kill DC so pink noise has no bias
    pink = np.fft.irfft(spec * scale, n=n)
    return scale_to_rms(pink, RMS_M18_AMP)


def build_dcstep():
    n = n_samples(2.0)
    sig = np.zeros(n)
    sig[n_samples(0.5) : n_samples(1.5)] = 0.25
    return sig


def build_silence():
    return np.zeros(n_samples(5.0))


def _snap_to_bin(freq, bin_res):
    return round(freq / bin_res) * bin_res


def build_multitone():
    n = n_samples(5.0)
    t = np.arange(n) / SR
    bin_res = SR / 65536.0

    candidates = np.geomspace(50.0, 12000.0, 10)
    freqs = []
    for f in candidates:
        snapped = _snap_to_bin(f, bin_res)
        # nudge by whole bins until no integer-multiple relationship with any prior tone
        bump = 0
        while any(
            abs(round(snapped / g) * g - snapped) < bin_res * 0.5 or
            abs(round(g / snapped) * snapped - g) < bin_res * 0.5
            for g in freqs if g > 0
        ):
            bump += 1
            snapped = _snap_to_bin(f, bin_res) + bump * bin_res
        freqs.append(snapped)

    rng = np.random.default_rng(4803)
    phases = rng.uniform(0, 2 * np.pi, len(freqs))
    amp = RMS_M18_AMP
    sig = np.zeros(n)
    for f, ph in zip(freqs, phases):
        sig += amp * np.sin(2 * np.pi * f * t + ph)
    sig = raised_cosine_fade(sig, 0.010)
    # 10 random-phase tones summed past full scale and clipped at PCM write; a clipped
    # THD reference is useless, so normalize the file to the standard -6 dBFS fixture peak
    # (tones stay equal-level; the actual per-tone level is recorded in the manifest)
    scale = PEAK_M6_AMP / peak_of(sig)
    return sig * scale, freqs, amp * scale


def build_tone1k():
    n = n_samples(3.0)
    t = np.arange(n) / SR
    sig = np.sin(2 * np.pi * 1000.0 * t)
    sig = raised_cosine_fade(sig, 0.010)
    return scale_to_peak(sig, PEAK_M6_AMP)


def build_drone110():
    n = n_samples(4.0)
    t = np.arange(n) / SR
    sig = sawtooth_naive(t, 110.0)
    sig = raised_cosine_fade(sig, 0.010)
    return scale_to_peak(sig, PEAK_M6_AMP)


def build_drum():
    n = n_samples(2.0)
    sig = np.zeros(n)

    # kick: 300 ms exponential freq sweep 80->40 Hz, exponential amplitude decay from t=0
    kick_n = n_samples(0.300)
    t_k = np.arange(kick_n) / SR
    kick_phase = log_sweep_phase(t_k, 80.0, 40.0, 0.300)
    tau_k = 0.300 / np.log(100.0)  # decays ~-40 dB by end of the 300 ms window
    kick_env = np.exp(-t_k / tau_k)
    sig[:kick_n] += np.sin(kick_phase) * kick_env

    # snare: 250 ms noise+tone burst starting at t=1.0s, same decay shape
    snare_start = n_samples(1.0)
    snare_n = n_samples(0.250)
    t_s = np.arange(snare_n) / SR
    tau_s = 0.250 / np.log(100.0)
    snare_env = np.exp(-t_s / tau_s)
    rng = np.random.default_rng(4804)
    noise = rng.uniform(-1.0, 1.0, snare_n)
    tone = np.sin(2 * np.pi * 200.0 * t_s)
    snare = (0.7 * noise + 0.3 * tone) * snare_env
    sig[snare_start:snare_start + snare_n] += snare

    return scale_to_peak(sig, PEAK_M6_AMP)


def build_guitar():
    n = n_samples(2.0)
    t = np.arange(n) / SR
    detune = 2.0 ** (7.0 / 1200.0)
    fundamentals = [82.41, 123.47, 164.81]  # E2, B2, E3
    sig = np.zeros(n)
    for f in fundamentals:
        sig += sawtooth_naive(t, f)
        sig += sawtooth_naive(t, f * detune)
    env = 0.7 ** (t / 2.0)  # decays to exactly 70% amplitude by t=2.0s
    sig *= env
    return scale_to_peak(sig, PEAK_M6_AMP)


# ---------------------------------------------------------------------------
# MIDI (Standard MIDI File, format 0) — minimal stdlib writer, no external libs.
# ---------------------------------------------------------------------------

TICKS_PER_QUARTER = 480
TEMPO_USEC_PER_QUARTER = 500000  # 120 bpm


def vlq(value):
    bytes_out = [value & 0x7F]
    value >>= 7
    while value:
        bytes_out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(bytes_out))


def build_bend_mid():
    """One held root note (C3 = 48) with the pitch wheel parked at four positions, so a
    render can be windowed per position and the sounding frequency measured exactly.
    Layout at 120 bpm, 480 ticks/quarter (1 beat = 0.5 s):
        0.0-1.0 s  wheel centred (8192)
        1.0-2.0 s  wheel full up (16383)
        2.0-3.0 s  wheel centred again  -> proves it returns, not just moves
        3.0-4.0 s  wheel full down (0)
    """
    beat = TICKS_PER_QUARTER
    def wheel(value):                       # 14-bit, LSB first
        return bytes([0xE0, value & 0x7F, (value >> 7) & 0x7F])

    events = [
        (0,        1, wheel(8192)),
        (0,        1, bytes([0x90, 48, 100])),   # note on C3
        (2 * beat, 1, wheel(16383)),
        (4 * beat, 1, wheel(8192)),
        (6 * beat, 1, wheel(0)),
        (8 * beat, 0, bytes([0x80, 48, 0])),     # note off
    ]
    end_tick = 8 * beat + beat // 4
    events.append((end_tick, 2, bytes([0xFF, 0x2F, 0x00])))
    events.sort(key=lambda e: (e[0], e[1]))

    track_data = bytearray()
    prev = 0
    for tick, _prio, raw in events:
        track_data += vlq(tick - prev)
        track_data += raw
        prev = tick

    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + TICKS_PER_QUARTER.to_bytes(2, "big")
    track = b"MTrk" + len(track_data).to_bytes(4, "big") + bytes(track_data)
    duration_s = end_tick / TICKS_PER_QUARTER * (TEMPO_USEC_PER_QUARTER / 1_000_000.0)
    return header + track, duration_s


def build_notes_mid():
    events = []  # (tick, priority, raw_bytes) ; priority 0=meta/off before 1=on at same tick
    note_log = []  # for manifest: exact tick start/end of every note

    def note_on(tick, pitch, vel, section):
        events.append((tick, 1, bytes([0x90, pitch, vel])))

    def note_off(tick, pitch, section):
        events.append((tick, 0, bytes([0x80, pitch, 0])))

    def add_note(start, dur, pitch, vel, section):
        end = start + dur
        note_on(start, pitch, vel, section)
        note_off(end, pitch, section)
        note_log.append({"section": section, "pitch": pitch, "start_tick": start, "end_tick": end, "velocity": vel})
        return end

    beat = TICKS_PER_QUARTER
    cursor = 0

    # (a) C major scale, quarter notes
    scale = [48, 50, 52, 53, 55, 57, 59, 60]
    for pitch in scale:
        cursor = add_note(cursor, beat, pitch, 100, "a_scale")
    cursor += beat  # 1 beat rest

    # (b) held C2, 4 beats
    cursor = add_note(cursor, 4 * beat, 36, 100, "b_held_c2")
    cursor += beat

    # (c) C major chord, 2 beats
    chord_start = cursor
    chord_end = chord_start + 2 * beat
    for pitch in (48, 52, 55):
        note_on(chord_start, pitch, 100, "c_chord")
        note_off(chord_end, pitch, "c_chord")
        note_log.append({"section": "c_chord", "pitch": pitch, "start_tick": chord_start, "end_tick": chord_end, "velocity": 100})
    cursor = chord_end + beat

    # (d) sixteen 16th notes retriggering C3 — staccato (50% gate): back-to-back gates
    # retrigger clicklessly from the current envelope level, which is amplitude-invisible;
    # the gap is what makes retriggers countable in a render
    sixteenth = beat // 4
    for _ in range(16):
        add_note(cursor, sixteenth // 2, 48, 100, "d_retrigger")
        cursor += sixteenth
    cursor += beat

    # (e) legato pair: C3 for 2 beats, E3 starting 1 beat in, overlapping 1 beat
    e_start = cursor
    add_note(e_start, 2 * beat, 48, 100, "e_legato")
    add_note(e_start + beat, 2 * beat, 52, 100, "e_legato")
    track_end_tick = e_start + 3 * beat  # E3 end = last event

    # tempo meta at tick 0, then all note events, then end-of-track
    meta = [(0, -1, bytes([0xFF, 0x51, 0x03]) + TEMPO_USEC_PER_QUARTER.to_bytes(3, "big"))]
    all_events = meta + events
    all_events.append((track_end_tick, 2, bytes([0xFF, 0x2F, 0x00])))
    all_events.sort(key=lambda e: (e[0], e[1]))

    track_data = bytearray()
    prev_tick = 0
    for tick, _prio, raw in all_events:
        delta = tick - prev_tick
        track_data += vlq(delta)
        track_data += raw
        prev_tick = tick

    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + TICKS_PER_QUARTER.to_bytes(2, "big")
    track = b"MTrk" + len(track_data).to_bytes(4, "big") + bytes(track_data)
    midi_bytes = header + track

    duration_s = track_end_tick / TICKS_PER_QUARTER * (TEMPO_USEC_PER_QUARTER / 1_000_000.0)
    return midi_bytes, note_log, duration_s


# ---------------------------------------------------------------------------
# Writing + manifest
# ---------------------------------------------------------------------------

def write_wav_fixture(name, mono, manifest, description, expected_duration_s):
    path = FIXTURES_DIR / name
    stereo = np.column_stack([mono, mono]).astype(np.float64)
    sf.write(str(path), stereo, SR, subtype="PCM_24")

    # measure from the re-read file, not the float buffer, so manifest reflects the actual quantized bytes
    data, sr_check = sf.read(str(path), always_2d=True, dtype="float64")
    flat = data.reshape(-1)
    peak = peak_of(flat)
    rms = rms_of(flat)
    sha = hashlib.sha256(path.read_bytes()).hexdigest()

    manifest[name] = {
        "filename": name,
        "sr": sr_check,
        "bit_depth": 24,
        "channels": data.shape[1],
        "duration_s": data.shape[0] / sr_check,
        "expected_duration_s": expected_duration_s,
        "peak_dbfs": amp_to_db(peak) if peak > 0 else None,
        "rms_dbfs": amp_to_db(rms) if rms > 0 else None,
        "sha256": sha,
        "description": description,
    }


def write_bend_fixture(manifest):
    path = FIXTURES_DIR / "bend.mid"
    midi_bytes, duration_s = build_bend_mid()
    path.write_bytes(midi_bytes)
    manifest["bend.mid"] = {
        "filename": "bend.mid",
        "sha256": hashlib.sha256(midi_bytes).hexdigest(),
        "duration_s": duration_s,
        "note": "held C3 (48); wheel centred / full up / centred / full down, 1 s each",
        "windows_s": {"centre": [0.0, 1.0], "up": [1.0, 2.0],
                      "recentre": [2.0, 3.0], "down": [3.0, 4.0]},
    }


def write_midi_fixture(manifest):
    path = FIXTURES_DIR / "notes.mid"
    midi_bytes, note_log, duration_s = build_notes_mid()
    path.write_bytes(midi_bytes)
    sha = hashlib.sha256(midi_bytes).hexdigest()
    manifest["notes.mid"] = {
        "filename": "notes.mid",
        "sr": None,
        "bit_depth": None,
        "channels": None,
        "duration_s": duration_s,
        "expected_duration_s": duration_s,
        "peak_dbfs": None,
        "rms_dbfs": None,
        "sha256": sha,
        "description": "SMF format 0, 480 tpq, 120 bpm: scale / held note / chord / retrigger / legato pair",
        "ticks_per_quarter": TICKS_PER_QUARTER,
        "tempo_usec_per_quarter": TEMPO_USEC_PER_QUARTER,
        "notes": note_log,
    }


def main():
    FIXTURES_DIR.mkdir(parents=True, exist_ok=True)
    manifest = {}

    write_wav_fixture("sweep.wav", build_sweep(), manifest,
                       "20 Hz-20 kHz exponential sine sweep, 10 s, 50 ms fades", 10.0)
    write_wav_fixture("impulse.wav", build_impulse(), manifest,
                       "single-sample impulse (amp 0.5) at sample 24000 in 2 s of silence", 2.0)
    write_wav_fixture("white.wav", build_white(), manifest,
                       "seeded uniform white noise, RMS -18 dBFS, 5 s", 5.0)
    write_wav_fixture("pink.wav", build_pink(), manifest,
                       "FFT 1/f pink noise, RMS -18 dBFS, 5 s", 5.0)
    write_wav_fixture("dcstep.wav", build_dcstep(), manifest,
                       "2 s: silence / +0.25 DC step for 1 s / silence", 2.0)
    write_wav_fixture("silence.wav", build_silence(), manifest,
                       "5 s true digital zero", 5.0)

    multitone, mt_freqs, mt_tone_amp = build_multitone()
    write_wav_fixture("multitone.wav", multitone, manifest,
                       "10 log-spaced non-harmonic sines, bin-exact for 65536-pt FFT, equal-level, file peak -6 dBFS, 10 ms fades", 5.0)
    manifest["multitone.wav"]["tone_frequencies_hz"] = mt_freqs
    manifest["multitone.wav"]["tone_amplitude_dbfs"] = amp_to_db(mt_tone_amp)

    write_wav_fixture("tone1k.wav", build_tone1k(), manifest,
                       "1000.0 Hz sine, peak -6 dBFS, 3 s, 10 ms fades", 3.0)
    write_wav_fixture("drone110.wav", build_drone110(), manifest,
                       "110 Hz naive sawtooth, peak -6 dBFS, 4 s, 10 ms fades", 4.0)
    write_wav_fixture("drum.wav", build_drum(), manifest,
                       "synthesized kick (80->40 Hz sweep) + snare (noise+200 Hz) at t=1s, peak -6 dBFS, 2 s", 2.0)
    write_wav_fixture("guitar.wav", build_guitar(), manifest,
                       "power-chunk stand-in: E2/B2/E3 sawtooths doubled +7c detuned, decay to 70%, peak -6 dBFS, 2 s", 2.0)

    write_midi_fixture(manifest)
    write_bend_fixture(manifest)

    out = {"spec_version": 1, "fixtures": manifest}
    manifest_path = FIXTURES_DIR / "manifest.json"
    manifest_path.write_text(json.dumps(out, sort_keys=True, indent=2) + "\n")

    ok = run_selfcheck(out)
    if not ok:
        sys.exit(1)


# ---------------------------------------------------------------------------
# Self-check
# ---------------------------------------------------------------------------

# expected peak amplitude (linear) where the spec pins one down; None = not pinned
EXPECTED_PEAK_AMP = {
    "sweep.wav": PEAK_M6_AMP,
    "multitone.wav": PEAK_M6_AMP,
    "impulse.wav": 0.5,
    "dcstep.wav": 0.25,
    "tone1k.wav": PEAK_M6_AMP,
    "drone110.wav": PEAK_M6_AMP,
    "drum.wav": PEAK_M6_AMP,
    "guitar.wav": PEAK_M6_AMP,
}
EXPECTED_RMS_AMP = {
    "white.wav": RMS_M18_AMP,
    "pink.wav": RMS_M18_AMP,
}


def spectral_centroid(x, sr):
    mag = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1.0 / sr)
    total = mag.sum()
    return float((freqs * mag).sum() / total) if total > 0 else 0.0


def run_selfcheck(manifest_out):
    rows = []
    failures = []
    fixtures = manifest_out["fixtures"]

    for name, entry in fixtures.items():
        if not name.endswith(".wav"):
            continue
        path = FIXTURES_DIR / name
        info = sf.info(str(path))
        data, sr = sf.read(str(path), always_2d=True, dtype="float64")

        checks = []
        checks.append(("sr==48000", sr == SR))
        checks.append(("subtype==PCM_24", info.subtype == "PCM_24"))
        checks.append(("channels==2", data.shape[1] == 2))
        checks.append(("L==R exact", np.array_equal(data[:, 0], data[:, 1])))

        expected_frames = n_samples(entry["expected_duration_s"])
        checks.append(("duration within 1 sample", abs(data.shape[0] - expected_frames) <= 1))

        peak = peak_of(data.reshape(-1))
        if name in EXPECTED_PEAK_AMP:
            exp_db = amp_to_db(EXPECTED_PEAK_AMP[name])
            got_db = amp_to_db(peak)
            checks.append((f"peak within 0.01 dB (exp {exp_db:.4f})", abs(got_db - exp_db) <= 0.01))
        elif name == "silence.wav":
            checks.append(("peak == 0 exact", peak == 0.0))

        if name in EXPECTED_RMS_AMP:
            exp_db = amp_to_db(EXPECTED_RMS_AMP[name])
            got_db = amp_to_db(rms_of(data.reshape(-1)))
            checks.append((f"rms within 0.01 dB (exp {exp_db:.4f})", abs(got_db - exp_db) <= 0.01))

        for label, passed in checks:
            rows.append((name, label, passed))
            if not passed:
                failures.append(f"{name}: {label}")

    # sweep-specific frequency sweep verification
    sweep_path = FIXTURES_DIR / "sweep.wav"
    sdata, ssr = sf.read(str(sweep_path), always_2d=True, dtype="float64")
    mono = sdata[:, 0]
    win = 4096

    def centroid_at(t_s):
        c = int(t_s * ssr)
        lo, hi = max(0, c - win // 2), min(len(mono), c + win // 2)
        return spectral_centroid(mono[lo:hi], ssr)

    c_05 = centroid_at(0.5)
    c_1 = centroid_at(1.0)
    c_2 = centroid_at(2.0)
    c_8 = centroid_at(8.0)

    f_expected_1s = SWEEP_F0 * (SWEEP_F1 / SWEEP_F0) ** (1.0 / SWEEP_DURATION)
    octave_ok = 0.5 * f_expected_1s <= c_1 <= 2.0 * f_expected_1s
    monotonic_ok = c_8 > c_2 > c_05

    rows.append(("sweep.wav", f"centroid(1s)={c_1:.1f}Hz within octave of {f_expected_1s:.1f}Hz", octave_ok))
    rows.append(("sweep.wav", f"centroid(8s)={c_8:.1f} > centroid(2s)={c_2:.1f} > centroid(0.5s)={c_05:.1f}", monotonic_ok))
    if not octave_ok:
        failures.append("sweep.wav: centroid(1s) not within an octave of analytic expectation")
    if not monotonic_ok:
        failures.append("sweep.wav: centroid not monotonically increasing at 0.5/2/8 s")

    # light MIDI sanity check
    mid_entry = fixtures["notes.mid"]
    expected_note_events = 8 + 1 + 3 + 16 + 2
    got_note_events = len(mid_entry["notes"])
    midi_ok = got_note_events == expected_note_events
    rows.append(("notes.mid", f"note event count == {expected_note_events}", midi_ok))
    if not midi_ok:
        failures.append(f"notes.mid: expected {expected_note_events} note events, got {got_note_events}")

    print(f"{'fixture':<16} {'check':<60} {'result'}")
    print("-" * 90)
    for name, label, passed in rows:
        print(f"{name:<16} {label:<60} {'PASS' if passed else 'FAIL'}")

    if failures:
        print(f"\n{len(failures)} FAILURE(S):")
        for f in failures:
            print(f"  - {f}")
        return False
    print(f"\nAll {len(rows)} self-checks PASSED.")
    return True


if __name__ == "__main__":
    main()
