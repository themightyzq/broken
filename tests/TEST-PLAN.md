# TEST-PLAN — Broken (JUCE)

> The loop (v0.2, autonomous): `scripts/make_fixtures.py` generates fixtures → `broken_cli`
> (plugin/build) renders them through the actual plugin engine at named snapshots
> (`plugin/snapshots/test/`) into `tests/renders/<fixture>__<snapshot>__vNN.wav` →
> `scripts/analyze.py render <file> --expect <profile>` gives numeric pass/fail + plot.
> Claude runs the whole loop; the user confirms the perceptual items (panel, feel, sound
> in Reaper). 44.1 k parity fixtures: `make_fixtures.py --sr 44100` → `tests/fixtures/sr44100/`.
>
> M8 gate semantics (adopted after v0.2 investigation): tuning/attack/release/retrigger
> run against the POLY render; checks auto-skip notes whose measurement physics is
> invalid (notes < 100 ms for tuning; chord/low-pitch/short notes for attack; notes with
> overlapping release tails for release) — the skipped cases' correctness is covered by
> the Catch2 envelope/voice unit tests. The `d_retrigger` fixture is staccato (50% gate)
> because clickless retrigger is amplitude-invisible at zero gap. Mono collapse and
> unison level-compensation are verified as separate numeric behavior checks on the
> mono/uni renders. TAPE (M9) is scripted headless via `broken_cli --at` timed actions.

## Fixture set (generated, deterministic — never hand-made)

All WAV: 48 kHz, 24-bit, stereo with identical L/R, peak −6 dBFS unless noted.
`fixtures/manifest.json` records duration/peak/hash per file; analyze.py reads it.

| Fixture | Content | Primarily tests |
|---|---|---|
| `sweep.wav` | 20 Hz–20 kHz exponential sine, 10 s, 50 ms fades | frequency response, filter poles |
| `impulse.wav` | unit impulse (amp 0.5) at exactly 0.5 s, 2 s total | latency, resonator ring, delay time |
| `white.wav` | seeded uniform white, 5 s, RMS −18 dBFS | noise-floor shifts, filter slope |
| `pink.wav` | seeded pink (Voss/filtered), 5 s | broadband response sanity |
| `dcstep.wav` | 0 → +0.25 (1 s) → 0, 2 s total | DC-blocker behavior |
| `silence.wav` | true zeros, 5 s | noise floor, denormal CPU (manual) |
| `multitone.wav` | 10 non-harmonically-related tones, log-spaced 50 Hz–12 kHz, equal-level, file peak −6 dBFS, 5 s | THD, aliasing signature |
| `tone1k.wav` | 1 kHz sine, 3 s | quantization noise, SpecInv line, sideband math |
| `drone110.wav` | 110 Hz sawtooth, 4 s | CYCLE/modulator musical checks |
| `drum.wav` | synthesized kick (80→40 Hz sweep, 300 ms) + snare burst at 1 s | Drum Crush technique renders |
| `guitar.wav` | detuned-saw power-chord-ish stack, 2 s | Power Chunk technique renders |
| `notes.mid` | @120 bpm: C-major scale ♩; held C2 (4 beats); Cmaj chord; 16th-note retrigs on C3; legato pair. Exact tick layout in `manifest.json` | tuning, envelopes, mono/poly/unison |

## Render matrix — milestone by milestone

Expectations are numeric; exact thresholds live in `analyze.py --expect <profile>` profiles.
"Null" = residual RMS below −60 dBFS after latency alignment (thresholds start pragmatic,
tighten once measured).

### M1 — Unity pass-through (calibrates the whole loop; no DSP yet)
Snapshot `00 Init`, FX mode, all modules bypassed.
| Render | Expectation |
|---|---|
| `sweep__00-init__v01` | null vs. fixture; flat response ±0.1 dB |
| `silence__00-init__v01` | ≤ −90 dBFS RMS |
| `multitone__00-init__v01` | null; no added spectral lines > −80 dBFS |
| `impulse__00-init__v01` | reported latency = plugin latency, consistent across renders |
Manual: CPU meter at idle noted (baseline); panel loads; Reaper automation sees controls.

### M2 — Waveshaper
| Render | Expectation |
|---|---|
| `sweep__curve1-drive0` | null (Linear curve is the verification curve) |
| `tone1k__curve2-drive12` | odd-harmonic series; levels recorded as reference signature |
| `tone1k__curve5-drive12` | even harmonics present; post-blocker DC < −60 dBFS |
| `multitone__curve4-hi` | aliasing signature recorded (baseline, not pass/fail) |
| `dcstep__curve5-drive12` | DC removed within 200 ms |

### M3 — Filter
`sweep` at poles 1/2/3/4, cutoff 1 kHz: measured −3 dB point per N recorded and slope
6·N dB/oct ±1 dB in the stopband octave; EXT off ⇒ cutoff floor honors 500 Hz.

### M4 — Resonator
`impulse` at f_res = 220 Hz, fb 0.9: ring frequency = 220 Hz ±1 cent-equivalent bin;
t₆₀ within ±20% of DSP-NOTES §5 formula; fb −0.9 shows odd-harmonic comb.

### M5 — Modulator
`tone1k` × mod 55 Hz: AM ⇒ lines at 945/1000/1055 Hz; RM d=1 ⇒ 945/1055 only, carrier
< −40 dB vs. sidebands. Sideband frequencies exact to the FFT bin.

### M6 — Spectral Inverter
`tone1k` mix=1 ⇒ line at 23 kHz (48 k sr), level ±0.5 dB vs. input; mix=0.5 ⇒ both lines.

### M7 — Delay
`impulse`, 80 ms, mix 0.5: echo at exactly 80 ms ± 1 sample; polarity flip inverts echo.

### M8 — Envelopes & voices (instrument mode, `notes.mid`)
Held C2: A/D/R measured within ±10% of panel; scale: each note f = equal temperament
±3 cents; chord in poly = 3 voices sounding (level ≈ +9.5 dB vs. single at equal settings
— record actual); mono mode: chord collapses to last note; retrig pattern: no stuck notes;
unison: level comp (1/√6, a POWER compensation) holds output within ±1 dB of a single-voice render **over a sustained note**, where the ±12-cent voices decorrelate (v0.2 measured 0.7 dB). At the onset the six voices are still coherent, so the first ~100 ms sit up to **+7.8 dB** (= 20·log10 √6) above a single voice — by design, not a defect (test_engine.cpp asserts it, v0.23).

### M9 — TAPE
Record `tone1k` playback → FLIP → render TAPE source at root pitch ⇒ recovered 1 kHz ±1
bin, level within ±1 dB (one generation loss recorded); REC during TAPE playback + FLIP ⇒
no discontinuity > −40 dBFS at the flip point.

### M10 — Sampler Colour
`tone1k` 12-bit ⇒ noise floor −74 dBFS ±3 dB; RATE 8 kHz ⇒ images at 8 k ± 1 k present.

### M11 — Parity (suite 14)
Reaper project at 44.1 k: repeat M1, M3 (one config), M7 — time/frequency behavior scales
with sr as designed. Open user item: Reaper spot-check by ear.

### M12 — Sample region + play modes (v0.4; controls remapped v0.5: REV toggle +
### LOOP on/off + style Loop|PingPong; xfade now internal for Loop and blended for
### PingPong turnarounds — re-measured numbers below)
Loaded sample = `sweep.wav` (a known frequency trajectory makes direction audible in
analysis); region 0.2–0.3 (sweep content 2.0–3.0 s), note C3 held 0.1–4.5 s via
`broken_cli --sample ... --at noteon`. Measured results 2026-08-27, all within expectation:
| Check | Expectation | Measured (v0.5, 2026-08-27) |
|---|---|---|
| One-shot (LOOP off) | region plays once (~1 s) then silence | −9.0 dB during, −300 dB after |
| Loop | sustains at source level through the held note | −9.0 dB at 3.5–4.4 s |
| REV + one-shot | spectral centroid descends (sweep backward) | 149 Hz → 86 Hz |
| PingPong | centroid slope alternates ≥ 2× | 4 sign changes |
| Loop seam, mid-file | 50 ms internal fade vs raw ≥ 20 dB step drop | −8.3 → −39.9 dBFS |
| Loop seam, FILE-HEAD region | internal fade works where v0.4 silently didn't | −52.0 dBFS step at xf 50 |
| PingPong turnaround | cosine blend smooths the slope corner ≥ 20 dB | second-diff −36.6 → −73.8 dBFS |
| Unit layer | exact position sequences per control combo | 12 Catch2 cases, 53/53 green |
| State | sample path + loop controls survive getState/setState | `broken_cli --state-roundtrip` PASS |

## Manual-only checklist (user confirms per milestone)
- [ ] Feel: knob ranges musical, big-knob "three moves" gets an industrial sound fast
- [x] CPU meter: idle / mono / poly+unison worst case, numbers recorded in CHANGELOG —
      see CHANGELOG v0.12: idle 0.0008×, mono 0.004×, poly-6 all modules 0.025×,
      poly+unison everything 0.040× at block 512 (0.150× at block 64, 0.086× at 96 kHz)
- [x] Silence tail: no CPU spike after audio stops (denormals) — see CHANGELOG v0.12:
      9 s silent tail with resonator+delay feedback runs faster than a held note
      (0.05 s vs 0.06 s), no denormal penalty
- [ ] Snapshots recall consistently, incl. source mode + CYCLE window combos
- [ ] Reaper automation: DRIVE, FILTER, MOD writable/readable
- [ ] Panel fits a 13" laptop screen at the 55 % minimum scale (v0.27 design 1520 px wide → ~836 px at 55 %)

### M13–M17 — era-gap phases (v0.7–v0.11), measured 2026-08-27
| Feature | Check | Measured |
|---|---|---|
| PM mode (A) | vibrato sidebands around the carrier | 105/115 Hz present, carrier −10 dB |
| Self-modulation (A) | sine self-RM squares to 2f | 2f dominates f by >10x (unit) |
| Sample-as-modulator (A) | region read as wavetable at mod freq | 55 Hz line >10x its octave |
| Pitch MIX (A) | dry + pitched lines coexist | 110 Hz and 220 Hz both strong |
| Pitch MIX = 1 (A) | pitched path bit-identical to single head | exact match (unit) |
| Era Noise 0/0 (B) | randomized sine is tunable | dominant 220.0 Hz at A3 |
| Era Noise 100/100 (B) | broadband | detector clarity below gate |
| SpecInv Type B (B) | two quarter-rate images | 11 k + 13 k, original −68 dB |
| EqPower xfade (B) | no −3 dB seam dip on uncorrelated material | min/median power ratio up >1.2x |
| Random curve (C) | deterministic per seed, different across seeds | identical / >5 of 21 points differ |
| Custom curve (C) | honours its 16 breakpoints, clamped | within 2e-3 at each point |
| Stretch (D) | duration grows | guitar 2.00 s → 3.42 s |
| Compress (D) | duration shrinks | guitar 2.00 s → 0.50 s |
| Predelay (D) | attack passes through untouched | bit-identical over predelay window |
| FLATTEN (D) | removes the source's own dynamics | drum decay 27.6 → 3.8 dB; guitar 2.7 → −0.2 dB |
| Harmonic Mode (E) | 1/k saw recipe | partials 2–5 within 1.5 dB of ideal |
| Harmonic band-limit (E) | partial above 0.45·sr dropped | silent at 987 Hz, audible at 110 Hz |
| Standing regressions | unity / tune-lock / state roundtrip | PASS at every phase |

## Standing gates (not fixture milestones)

These run alongside the render matrix at every milestone from the version noted onward,
but check invariants rather than a single fixture's numbers.

### M18 — Tune-lock (`broken_cli --tune-test`, v0.6)
Detune the oscillator (+30¢), trigger TUNE, re-measure: locked pitch must land at 0¢.

### M19 — Fuzz hardening (`broken_cli --fuzz N`, v0.12)
N seeded random states over all APVTS parameters; pass = finite output and no divergence
on every seed (divergence heuristic: output must not grow unbounded across the render).

### M20 — Param-check (`broken_cli --param-check`, v0.21)
Every id the GUI's indexed-bank builders generate (curve points, draw points, harmonics)
must resolve to a real parameter; pass = 0 missing out of the full banked-id count.

### M21 — Bend test (`broken_cli --bend-test`, v0.22)
Render `bend.mid` (held root note, wheel centred / full-up / centred / full-down) at a
fixed bend range; pass = all 4 wheel positions measure the expected frequency.

### M22 — Parameter-id static check (`scripts/check_ids.py`, v0.23)
Static scan of the source tree; pass = no hand-formatted bank-id strings (e.g.
`"ws.c%02d"`-style) exist outside `Params.h`.

### M23 — State migration (v0.24)
`broken_cli --state-migrate-check`: a state tree saved under the pre-rename tag ("TurboSynth")
must restore into the renamed product ("Broken") with its values intact — pass = the
probe parameter survives. Guards the rename landmine: hosts and presets key on the tag.
