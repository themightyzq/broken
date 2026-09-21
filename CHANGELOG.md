# CHANGELOG — the prototyping environment TurboSynth

## v0 — 2026-08-26 — Project founding (no ensemble yet)
- Intake completed (see docs/DESIGN.md header + plan): instrument + FX-in, full the prototyping environment 6,
  Primary + Core, mono-default/poly/unison, generous CPU, public release intended,
  Diversion + milestone Save-As, Reaper render loop.
- Research pass on TurboSynth architecture and the band usage → docs/RESEARCH.md (primary
  sources: SOS Dec 1988, Music Technology Sep 1988, Keyboard 3/94 the artist interview).
- Design docs written: DESIGN.md (fixed the band-order chain, TAPE resample section, CYCLE
  mode), DSP-NOTES.md (per-module math, conventions tagged believed pending vendor-doc
  verification), PANEL.md (MANGLE/EDIT views, defaults = init snapshot), PITFALLS.md.
- Contract: CLAUDE.md (ownership boundary, definition of verify/done, suite routing).
- tests/TEST-PLAN.md v1: fixture set + milestone render matrix M1–M11.
- Open items: vendor-convention verification in flight; waveshaper curves are reconstructions;
  release name TBD ("TurboSynth" is an a third party trademark); the prototyping environment version on this machine
  not yet recorded.

## v0.1 — 2026-08-26 — Verification harness live
- vendor-doc verification pass (Building in Primary/Core/Diving Deeper PDFs): audio scaling
  ±1.0=0 dB, pitch 69=A3=440 Hz, Order module, DN Cancel/z⁻¹ fbk denormal idiom, Audio
  Table audio-rate R/W, Sample Lookup for CYCLE — believed tags in DSP-NOTES upgraded to
  cited; corrections: module is "Spread", voice count is design-time-only, TAPE treated
  session-only.
- scripts/make_fixtures.py + scripts/analyze.py implemented (delegated, then gate-reviewed).
  Gate findings fixed: multitone fixture was clipping at 0 dBFS (now normalized to −6 dBFS
  peak, tone level recorded in manifest); unity profile judged absolute DC, false-failing
  sweep renders (now judges added DC via the aligned residual).
- Verified by direct runs: byte-identical fixtures across two generations, 69 generator
  self-checks + 67 selftest checks pass, unity profile passes on tone1k and sweep
  self-renders (flatness 0.0743 dB).
- docs/builds/M1.md: first build hand-off (unity pass-through, 4 renders).

Next: user builds M1 shell in the prototyping environment, renders 4 fixtures, analysis gates the loop.

## v0.2 — 2026-08-26 — Platform pivot to JUCE; full v1 instrument built and verified
- **Pivot:** .ens cannot be authored programmatically → JUCE/C++ plugin (VST3 + AU +
  Standalone, arm64). Folder flattened/renamed to `Project_TurboSynth/`. Contract flipped:
  Claude authors/builds everything; user = design authority + ears. the prototyping environment path retired
  (record kept in `ensemble/`, docs/builds/M1.md).
- **Engine (`plugin/src/dsp/`, JUCE-free):** full chain per DESIGN §2 — SourceEngine
  (Sample/CYCLE/Osc/Noise/Input/Tape, varispeed), Modulator (AM/RM/FM, sine/bell/odd),
  Waveshaper (8 curves + DC blocker), FilterStack (1–4 pole LP), Resonator, Spectral
  Inverter, Delay, ADSR ×3 (amp/filter/aux w/ destination), 6-voice mono/poly/unison
  allocation, SamplerColour (12/8-bit + S&H), TapeBuffer (A/B ping-pong). 41 Catch2
  unit tests green; zero project warnings.
- **Autonomous verification:** ts_cli headless render target (WAV, MIDI, --at timed
  actions for TAPE scripting) + analyzer extensions (typed expectation profiles, ring/
  echo/MIDI-segmentation/quantization/discontinuity measurements, 81 selftest checks
  incl. synthetic-signal validation of every new measurement).
- **Gate results (all PASS, numbers in tests/results/):** unity null −141 dBFS (24-bit
  floor); Linear-curve null −141 dB; filter corners −3.16/−6.31/−12.61 dB @1 kHz for
  1/2/4 poles (uncompensated cascade as designed); resonator ring 218 smp (=48 k/220),
  t60 in tolerance; RM carrier −124 dB vs sidebands at exactly 945/1055 Hz; SpecInv line
  at 23 kHz; delay echo ±1 smp at 80 ms, polarity correct; M8 tuning 0.11 cents / attack
  12% / release 36% / 16/16 retriggers (poly render, −12 dB headroom); mono chord
  collapse (+4.8 dB poly delta) and unison comp (0.7 dB) confirmed; TAPE gen-1 recovers
  1000.0 Hz exactly, flip clean; 12-bit floor and 8 kHz S&H images in band; 44.1 k parity
  (unity null −141 dB, echo at 3528 smp).
- **Test-design fixes found at gate review:** multitone fixture had clipped (normalized
  to −6 dBFS); unity DC criterion moved to residual DC; d_retrigger fixture made
  staccato; M8 checks scoped to measurement-valid notes; retrigger counter hysteresis
  −3/−10 dB matched to real release physics; negative-fb comb period corrected in
  DSP-NOTES (2D, not D/2).
- **GUI:** MANGLE/EDIT panel per PANEL.md (980×560, industrial dark, five big moves,
  drop-zone waveform display w/ CYCLE window overlay, TAPE REC/FLIP, tooltips = info
  text, peak meter). Screenshot: docs/panel-v02.png.
- **Presets:** 10 technique-named snapshots (Death Vocal ×2, Power Chunk ×2, Drum Crush
  ×2, Textures ×3, Init) — listening-draft status until the user's ears pass them.
- **Installed:** AU + VST3 to ~/Library/Audio/Plug-Ins; `auval` PASS (aumf Tbsy Zlqa).
- Open: user listening pass vs reference the band material (P12); release name; CPU meter
  reading in Reaper; Apple Developer signing for public distribution.

## v0.3 — 2026-08-27 — TAPE → WAV export
- SAVE button in the TAPE block (docs/PANEL.md updated first): writes the ACTIVE take
  (post-FLIP) as mono 24-bit WAV at engine rate. Dimmed while tape is empty. Completes
  the era workflow: design → resample → export → drag into any sampler; makes the
  standalone a genuine self-contained tool.
- `TurboSynthProcessor::saveTapeToFile` (buffer copied under the callback lock so an
  audio-thread FLIP can't swap mid-read); ts_cli `--save-tape <path>` runs the same path.
- Verified: build clean, 41/41 tests green; headless REC→FLIP→SAVE of the 1 kHz fixture
  → saved WAV is mono PCM_24 48 kHz, 2.891 s, RMS −9.0 dBFS, dominant exactly 1000.0 Hz;
  empty-tape save errors cleanly (exit 3). AU/VST3 reinstalled; panel screenshot
  docs/panel-v03.png.
- Adjacent issue noted (not fixed, cosmetic): the FILTER knob's displayed value ignores
  the 500 Hz authentic floor when EXT is off — the DSP clamps correctly, the readout
  doesn't. Follow-up candidate.

## v0.3.1 — 2026-08-27 — FILTER readout shows the effective (floored) cutoff
- Fixes the cosmetic issue noted in v0.3: with FLOOR EXT off, the FILTER readout
  displayed the raw stored value below 500 Hz while the DSP floored it at 500
  (DSP-NOTES.md §4). The readout now shows max(500, value) when EXT is off; the raw
  parameter is untouched, so toggling EXT reveals it. PANEL.md updated first (note
  under the Mangle table). MangleView installs the ext-aware textFromValueFunction
  and refreshes the text from its 10 Hz timer when flt.ext flips (the toggle lives in
  the EDIT view, so the slider never sees that change itself).
- Verified: build clean (no warnings), ctest 41/41; standalone accessibility test —
  cutoff set to ~300 Hz raw with EXT off reads 500.0, EXT on reads 280.6, EXT off
  again reads 500.0. Screenshots: docs/panel-v031-extoff.png, docs/panel-v031-exton.png.

## v0.4 — 2026-08-27 — Sample region editor: select a chunk, loop it five ways, live
- **Non-destructive region** for long field recordings: `sample.regstart/regend/loop/
  xfade` params (snapshot + automation capturable; docs/DSP-NOTES.md §1.1a first).
  Region = "the sample" downstream; CYCLE windows inside it; TAPE unaffected.
- **Play modes** OneShot / Loop / Reverse / LoopRev / PingPong; loop-seam crossfade
  0–250 ms (default 10; 0 = raw era click; PingPong needs none — reflection is
  continuous). Live editing by design: a MIDI key is the preview, markers are audible
  while dragging, per-sample bounds make live region shrinking safe.
- **Region editor overlay** (SampleEditor.h): zoom-at-cursor, shift-drag pan, minimap
  with viewport, drag-select + edge handles writing params live, zero-crossing SNAP,
  LOOP/XFADE controls, live playhead (engine publishes read position), file-timeline
  readouts. Small waveform shows the region band; EDIT/double-click opens.
- **Sample path persistence** (pre-existing gap closed): the loaded file's path is
  saved in plugin state and reloaded on project open; missing file reported by name.
- Verified: 51/51 unit tests (10 new exact position-sequence cases incl. extreme-rate
  bounds and live-shrink safety); renders — OneShot silences after region, Loop
  sustains (−9 dB at 4.4 s), Reverse descends 149→86 Hz, PingPong flips slope 7×,
  xfade cuts seam step −8.3→−39.9 dBFS; `ts_cli --state-roundtrip` PASS; GUI editor
  verified in the standalone with a seeded state (readouts exact: 3.200/5.800 s; an
  initial +0.53 s readout discrepancy was traced to stray click automation during the
  screenshot session, not the plugin). AU revalidated, AU/VST3 reinstalled.
  Screenshot: docs/editor-v04.png.
- File-rate correctness: editor readouts and SNAP window use the FILE's native sample
  rate (new `getSampleFileSr()`), so 44.1 k recordings in 48 k sessions read true.

## v0.5 — 2026-08-27 — Loop controls redesigned; crossfade audit fixes
- **Controls (user review):** REV is now an independent Soundminer-style toggle
  (reverses the source, composes with everything); LOOP is an on/off toggle (off =
  one-shot; STYLE + XFADE greyed); STYLE = Loop | PingPong. Params `sample.loopon/
  loopstyle/rev` replace `sample.loop` (breaking only for hours-old v0.4 states).
- **Defect 1 fixed — PingPong turnaround thunk:** the v0.4 claim "ping-pong needs no
  crossfade" was wrong: turnarounds were value-continuous but slope-discontinuous.
  Now a raised-cosine blend into the reflected stream (linear ramps would only move
  the corner; cosine makes it C1). Render: max second-difference −36.6 → −73.8 dBFS.
- **Defect 2 fixed — edge-region loops had NO fade:** the v0.4 loop crossfade used
  material outside the region and silently degraded to nothing at the file head/tail.
  Replaced with the standard internal crossfade (tail blends into head; effective loop
  L−X). Render: file-head region seam −52.0 dBFS at 50 ms fade.
- **QoL:** XFADE numeric ms readout; crossfade zones drawn as accent wedges on the
  waveform (end for Loop, start for REV+Loop, both for PingPong); ZOOM SEL button
  frames the selection for loop-point surgery.
- Verified: 53/53 unit tests (12 SourceEngine cases incl. file-head fade and turnaround
  curvature); full M12 render matrix re-measured (TEST-PLAN table updated); state
  roundtrip PASS with new IDs; auval PASS; AU/VST3 reinstalled. Screenshot:
  docs/editor-v05.png (ZOOM SEL view with both PingPong wedges visible).

## v0.6 — 2026-08-27 — Tuners, FINE/TUNE lock, live-input pitch; THE SPEC found
- **Research pass 2 found the actual TurboSynth 2.0 manual + SC 2.2 addendum**
  (archive.org, full OCR — docs/RESEARCH.md "Pass 2"). Corrections applied: Delay
  feedback is era-correct (not our extension); Diffuser is SC-2.2-only/card-gated (not
  original); the filter was the "Filter Envelope" (inherently envelope-driven); the
  original Noise was a randomized sine; Pitch Envelope + Time Compressor existed; our
  SpecInv = the spec's Type A. Era-gap backlog added to DESIGN.md §6 (11 items).
- **FINE** (`source.finecents`, ±50¢ in 1¢ steps — the original Pitch Shifter's own
  fine range) folds into the pitch everywhere via gatherParams.
- **Live-input pitch:** PITCH (+FINE) now works on the Input source via TapeShift
  (dsp/TapeShift.h): varispeed read heads on a 1.6 s live tape loop, dual-tap
  raised-cosine handoff — the rotating-head design; warble is the era artifact. EXACT
  bypass at 0 st keeps unity gates bit-true (re-verified: sweep unity null −141.5 dB).
  Render: input +2 st → 1120.0 Hz dominant (−3.8¢ of ideal 1122.5).
- **Tuners:** IN (post-source, pre-mangle — the monitor voice tap) and OUT (post-chain
  master tap) via lock-free TapRings + dsp/PitchDetector.h (NSDF, 4096 window,
  parabolic interp, clarity gate; note naming C4=60 per the spec's Appendix D).
  Displays: note, ±50¢ needle, Hz; dim on low clarity.
- **TUNE lock:** one press sets FINE to land the source on the nearest note (rolls
  whole semitones into PITCH if a live FINE pushed past ±50). Headless proof:
  `ts_cli --tune-test` — osc detuned +30¢ → lock → 0¢ (PASS).
- Verified: 62/62 unit tests (9 new: detector accuracy 440±0.5 Hz / E2 saw ±3¢ /
  noise-clarity gate; TapeShift octave + whole-step ±2%, unity bit-exact, finite at
  ±48 st); auval PASS; AU/VST3 reinstalled. Screenshot: docs/panel-v06.png.

## v0.7 — 2026-08-27 — Phase A: Modulator completed (PM, Self/Sample/Tape sources, Pitch MIX)
- PM as a first-class 4th mode (10 ms modulated delay — the spec's chorus recipe).
- `mod.source` { Osc, Self, Sample, Tape }: self-modulation and any-sample-as-modulator
  per the original's architecture; FM composes with all sources.
- `source.pitchmix`: second root-rate read head (identical region/loop logic) blends the
  unpitched stream — the original Pitch Shifter's Mix; PITCH MIX knob in EDIT view,
  SRC combo in the MOD block.
- Verified: 67/67 tests (PM Bessel sidebands, sine self-RM → 2f with fundamental −10×,
  sample-as-wavetable at 55 Hz, mix=1 bit-identical to single-head); renders — PM 5 Hz
  sidebands at 105/115 Hz, pitchmix dry+pitched lines both present; unity/tune/roundtrip
  regressions PASS. (Note: saw self-RM retains 110 Hz via harmonic differences — correct
  for saw; the sine unit test is the clean squaring proof.)

## v0.8 — 2026-08-27 — Phase B: era Noise, SpecInv Type B, equal-power crossfade
- Noise source is now the spec's randomized sine (`noise.amp`/`noise.phase` %,
  defaults 25/25): 0/0 = tunable sine (verified 220.0 Hz at A3), 100/100 = broadband
  (clarity below detector gate). Old white noise = max settings.
- `inv.type` A/B: Type B ring-mods by sr/4 → both quarter-rate images (verified 11 k +
  13 k from a 1 kHz tone, original −68 dB).
- `sample.xfadeshape` Linear/EqPower on the loop seam: eq-power removes the −3 dB
  uncorrelated-material dip (sliding-window min/median power test).
- 71/71 tests; unity/tune/roundtrip regressions PASS.

## v0.9 — 2026-08-27 — Phase C: Random + Custom waveshaper curves
- Curve list gains "Random" (12 seeded breakpoints; `ws.randseed` saved with the preset,
  so a rolled curve recalls exactly) and "Custom" (16 draggable breakpoints
  `ws.c01..c16`, cosine-smooth interpolation, identity diagonal by default).
- CURVE block in the EDIT view: live transfer-curve display for all 10 curves (drawn
  from the same math the DSP runs), draggable handles when Custom is selected, RND to
  roll a seed, COPY→CUSTOM to start drawing from any preset shape.
- 76/76 tests (seed determinism, breakpoint accuracy, clamping, no regression on the
  original 8 curves).

## v0.10 — 2026-08-27 — Phase D: Stretcher / Time Compressor + FLATTEN
- STRETCH/COMPRESS (`stretch.*`): segment-repeat time scaling tuned to the material's
  fundamental, with predelay and raised-cosine segment seams; composes with region,
  loop and REV because it acts on the read-position advance. Mismatched FREQ still
  produces the original's AM artifacts — kept as the creative tool the spec describes.
- FLATTEN (`flat.*`): realtime Envelope Removal. Deviation from the destructive original
  documented in DSP-NOTES §17 (realtime keeps sample-path persistence honest).
- Two real bugs caught by the new tests: a compressed one-shot never ended (position was
  clamped to the buffer, so the end-of-region test could never fire), and the +24 dB
  makeup ceiling left a third of a real 40 dB note decay un-flattened — raised to +40 dB
  with a −60 dBFS gate so noise floors still aren't inflated.
- Verified in the engine: guitar 2.00 s → 3.42 s stretched, → 0.50 s compressed;
  predelay window bit-identical; drum kick decay 27.6 dB → 3.8 dB flattened.
- Pitch Envelope (backlog #6) resolved as already covered by AUX→Pitch; no code.

## v0.11 — 2026-08-27 — Phase E: Oscillator Harmonic Mode
- `osc.mode` Wave/Harmonic; Harmonic sums 64 partials (`osc.h01..h64`, % amplitude,
  defaults = the 1/k saw recipe) into a 4096-point wavetable rebuilt only on change.
  Partials above 0.45·sr are dropped, and the table is normalized so any stack of bars
  stays inside ±1.
- Table rebuild uses an exact sine LUT (partial k at index i is sineLUT[(i·k) mod N]) —
  the naive version would have fired 262k sin() calls on the audio thread.
- 86/86 tests: fundamental-only = clean sine, saw recipe within 1.5 dB of ideal 1/k for
  partials 2–5, band-limit verified silent above Nyquist reach, stacked partials bounded,
  Wave mode provably unaffected.
- Deferred (documented): Oscillator waveform timeline, parallel chains/Mixer.

### v0.11 wrap-up — era-gap backlog closed
- EDIT view restructured to a 3x4 grid: new CURVE, HARMONICS, TIME and COLOUR/NOISE
  blocks alongside the existing eight. Two layout clips found at gate review and fixed
  (COPY TO CUSTOM and the FLAT shape button were falling outside their cells); the
  copy button's arrow glyph rendered as mojibake and is now plain ASCII.
- 5 new presets exercising the new capabilities: 12-self-destruct, 13-sample-modulator,
  22-chorus-chunk, 43-stretch-ghost, 44-additive-bell (15 total; all load-checked
  headless).
- Final gate: build clean (zero project warnings), 86/86 unit tests, unity null
  −141.5 dBFS, M8 instrument gate PASS (tuning 0.11 cents), tune-lock PASS, state
  roundtrip PASS, auval PASS. AU + VST3 reinstalled. Screenshot: docs/editview-v011.png.
- DESIGN.md §6 backlog now closed except the two deliberately deferred items
  (Oscillator waveform timeline; parallel chains/Mixer).

## v0.12 — 2026-08-27 — Hardening pass (adversarial QA, not confirmatory testing)
Every earlier test was confirmatory: I chose settings, predicted a number, checked it.
This pass attacked the plugin with settings nobody chose. New tools: `ts_cli --fuzz`
(seeded random states over all 82 parameters, enumerated from the APVTS so it can't
drift from Params.h), `--fuzz-diagnose <seed>` (module-bypass bisection), `--bench`,
and an AddressSanitizer/UBSan build (`plugin/build-asan`).

**Defects found and fixed (each has a regression test in test_hardening.cpp):**
- **Out-of-bounds read (memory safety).** Found by fuzz + ASan: `Modulator::applyPM`
  walked its read index off the buffer whenever the modulator ran hotter than ±1 —
  reachable with the v0.7 Self/Sample modulator sources. Root cause fixed at source
  (the modulator signal is clamped to its documented ±1 in Voice::render, which also
  cured RM acting as a runaway amplifier) plus defensive index bounds in applyPM.
  This was nondeterministic: same seed, different results run to run.
- **FLATTEN had no output reference.** `y = x/env` drove a spiky source to +37 dBFS.
  Now levels toward a target (0.25) with a fast 1 ms attack and a soft ceiling at 4×
  target. Side effect: it flattens *better* — drum kick decay 27.6 dB → 1.0 dB (was 3.8).
- **Envelope click on voice stealing.** A quiet note stealing a loud sustaining voice
  snapped the level in one sample (poly always retriggers). Now glides through decay.
- **Resonator mistuned above ~164 kHz.** Fixed 8192-sample buffer couldn't hold 20 Hz
  at 192 kHz, so the comb silently rang at ~136 Hz instead. Buffer now sized from sr.
- **Async file dialogs captured raw `this`** (tape SAVE, sample load) — a crash if the
  host closed the editor with the panel open. Now SafePointer-guarded.
- **State restore could race the GUI**: hosts don't guarantee setStateInformation runs
  on the message thread; the sample reload is now marshalled there.
- Parameter values from `--set` and snapshot JSON are clamped to their declared range
  (convertTo0to1 doesn't clamp); PyramidBuilder no longer reassigns state under a
  worker that refused to stop; failed sample reloads now say *why*.

**Reviewed and rejected (verified before accepting):** a reported off-by-one in the
POLES combo — JUCE 8's ComboBoxParameterAttachment maps *normalized* values, not raw
ones; renders confirm 1/2/3/4 poles give −5.3/−10.5/−15.8/−21.1 dB per octave.

**Measured, not asserted:**
- Fuzz: 1500+ random states, 0 failures on the real invariants (finite output, no
  divergence). Divergence heuristic false-positived once on a slow-filling delay line;
  verified convergent by hand (plateaus at 1.0, decays to silence) and the window fixed.
- CPU (this Mac, 4 s renders): idle 0.0008× realtime; mono 0.004×; poly-6 all modules
  0.025×; poly+unison everything 0.040× at block 512, **0.150× at block 64**, 0.086×
  at 96 kHz. Comfortably inside the 0.25× target.
- Denormals: a 9 s silent tail after a note with resonator+delay feedback runs *faster*
  than a held note (0.05 s vs 0.06 s) — no denormal penalty. Closes the PITFALLS item
  that had been unmeasured since day one.
- Standing gates all re-verified: unity null, M8 instrument gate, tune-lock, state
  roundtrip, all 15 presets load; 93/93 unit tests; auval PASS; AU/VST3 reinstalled.

**Known and deliberate:** with TRIM at +24 dB, resonator/delay feedback high and unison
on, output legitimately exceeds 0 dBFS by 20+ dB — stacked gain controls doing what they
say. There is no output limiter by design; the OUT knob and meter are the controls.
**Start your listening session with OUT low.**

**What this pass did NOT cover:** the sound itself. Nothing here judges whether it sounds
like Broken — that is still the user's ears.

## v0.13 — 2026-08-27 — Preset browser (the presets are reachable in the DAW at last)
- The 15 factory presets were previously loose JSON that only `ts_cli` could read; the
  plugin had no preset UI, so none of them could be loaded in Reaper. Fixed.
- **Factory presets are now compiled into the binary** (`juce_add_binary_data` over
  plugin/snapshots/*.json) so they ship inside the AU/VST3 bundle instead of depending
  on files that exist only on this machine. Adding a preset needs no CMake edit.
- **Preset bar in the header**: `< >` step, a menu (factory, then a USER section), SAVE
  to `~/Library/Audio/Presets/ZLQ/TurboSynth`, and `...` to reveal that folder. A `*`
  marks the preset dirty once you change anything.
- **A preset is a sound design, not a sample** — asserted, not assumed: `ts_cli
  --preset-check` loads all 15 against a loaded sample with a region set and verifies
  none of them disturbs the sample, its path, or the region markers. 15/15 clean.
  (Your own saved presets DO store the region, so they restore exactly what you had.)
- Host program list deliberately stays at 1: hosts restore program 0 on project load,
  which would silently overwrite the state you saved. Documented in PANEL.md.
- New CLI: `--list-presets`, `--preset <name>` (renders through the embedded data, so
  verification tests what ships), `--preset-check <sample>`.
- Verified: embedded preset renders **bit-identical** to the on-disk JSON (max diff
  0.00e+00); a hand-written user preset appears in the list (16 with it, 15 without);
  preset stepping confirmed through the real UI via accessibility (six `>` presses land
  exactly on 20-power-chunk); build clean, 93/93 tests, unity/tune-lock/state-roundtrip/
  fuzz spot-check all PASS; auval PASS; AU/VST3 reinstalled.
- Fixed during verification: loading a preset marked it dirty immediately, because the
  load fires the parameter listener for every id it applies.

## v0.14 — 2026-08-27 — Source-mode audit: TAPE gained looping; per-mode controls; empty-state hints
Prompted by the question "are all the source modes enabled properly, and how do you use
each one?" — audited all six in code. All were wired and working, but three gaps surfaced.

- **TAPE was a bare one-shot while the docs claimed otherwise.** `playPlain` had no
  region, loop, reverse, stretch or pitch-mix, yet DSP-NOTES §1.6 stated Tape used "the
  same machinery as Sample mode". Tape now genuinely runs the same head path
  (`advanceHead`): region, LOOP/STYLE/REV, XFADE, STRETCH and PITCH MIX all apply, so a
  captured take can be looped and reversed — what generational resampling actually wants.
  `playPlain` retired; buffer selection is a single `activeData()/activeLen()/activeSr()`
  switch, so the two sources can never drift apart again.
  - Region markers are shared and normalized 0–1 over whichever buffer is playing, so a
    region set against a long sample maps proportionally onto a short take.
  - Verified: 5 new unit tests (one-shot still stops; LOOP wraps; REV descends; region
    honoured; tape and sample buffers stay independent). Render gate — with LOOP on, a
    ~2.9 s take sustains at −9.0 dB past 10 s where the one-shot read −300 dB.
- **OSC WAVE, AMP NZ and PH NZ now live on the MANGLE face** in the SOURCE block, enabled
  per mode like POS/LEN/IN TRIM already were. Previously choosing Osc or Noise showed no
  controls at all (they were EDIT-view only; those copies remain).
- **A silent mode now says why**: "PLAY A NOTE" (Osc/Noise), "TAPE IS EMPTY - REC THEN
  FLIP" / "TAPE TAKE READY", "LIVE INPUT - NO MIDI NEEDED", "DROP SAMPLE or CLICK".
- **docs/PANEL.md** gains a "what each SOURCE mode needs" table (material, whether MIDI is
  required, which controls apply) — the written answer to the question that started this.
- `ts_cli --dump-state-b64` now takes `id=value` overrides (and `-` for no sample), which
  is how the per-mode GUI states were seeded for verification.
- Verified: build clean, 98/98 tests, unity null / tune-lock / state-roundtrip /
  preset-check all PASS; auval PASS; AU/VST3 reinstalled. Screenshots confirm Osc shows
  OSC WAVE active with the noise knobs dimmed, and Tape shows the empty-tape message.

## v0.15 — 2026-08-27 — PLAY button: no keyboard needed, both hands free
- Until now the only way to sound Sample/Cycle/Osc/Noise/Tape was an external MIDI note.
  In the standalone with no keyboard attached there was NO way to trigger them at all —
  which gutted the design-then-export workflow the TAPE SAVE feature exists for. And even
  with a keyboard, holding a key costs a hand, while every sound here is found by turning
  knobs *while it plays*.
- **`play.hold`** (bool parameter, so it automates and saves like everything else) is
  edge-detected in `processBlock` and pushes a synthetic note-on/off at the root (C3)
  into the SAME event list as real MIDI — voice allocation, envelopes, mono/poly/unison
  and the tuner taps all behave identically; no second code path.
- **PLAY button** full-width under the waveform in the SOURCE block; latches, label flips
  to STOP and lights while held. Dimmed in Input mode (that mode free-runs, a note means
  nothing there). Pitch stays with PITCH/FINE/TUNE — one obvious way to change it.
- Latch semantics as specified: with LOOP **off** the sample plays once and stops on its
  own while still latched, exactly as if you were holding a key; with LOOP **on** it
  sustains for the whole hold.
- Verified headlessly with the existing `--at <sec>:<id>=<value>` mechanism (no new
  tooling): hold 0.5→5.0 s on a 2 s sample — LOOP off reads −18.4 dB early then −300 dB
  at 3–4.8 s (ended by itself); LOOP on reads −18.4 then −18.5 dB (sustained). A latch
  never released leaves no stuck note (−300 dB in the tail).
- UI verified in the running standalone: the button reports PLAY before the click and
  STOP after, and the IN tuner wakes to A2 / 110.0 Hz with the needle centred on
  drone110.wav — proof the whole trigger→source→tuner chain works from one click.
  Screenshot: docs/play-v015.png.
- Standing gates: 98/98 tests, unity null, tune-lock, state roundtrip, preset-check,
  fuzz — all PASS; auval PASS; AU/VST3 reinstalled.

## v0.16 — 2026-08-28 — C-rooted defaults
- The instrument was already C-rooted at the **source** (`SourceEngine::rootNote = 48` =
  C3, 130.81 Hz — the unity-speed note for Sample/Tape and the note PLAY triggers, so
  Osc/Cycle/Noise sound C3 at their defaults too), but all three tuned **effect** defaults
  were A-rooted leftovers: MOD FREQ 55 Hz (A1), RES 220 Hz (A3), STRETCH FREQ 110 Hz (A2).
  On the init patch that put the resonator a **minor 6th** off the source root, so raising
  RES FB or MOD DEPTH made an untouched patch sound out of tune.
- Re-rooted to C (docs first — DSP-NOTES §0 gained a "Tuning root: C" convention bullet,
  PANEL.md rows updated): MOD FREQ **65.41 Hz (C2)**, RES **130.81 Hz (C3**, unison with
  the source root**)**, STRETCH FREQ **130.81 Hz (C3**, the fundamental the stretcher is
  fed at unity speed**)**.
- Not pitches, deliberately left alone: `dly.time` 80 ms (echo), `col.rate` 44100
  (sample-rate crusher), `res.damp` 8000 Hz (damping cutoff), filter cutoff.
- Verified by render, not by inspection. Init defaults, noise excitation, FB 0.95: the
  resonator rings at **130.81 Hz = C3, +0.0 cents**. Init defaults, RM mode, sine carrier
  at the C3 source root: sidebands at **65.41 Hz (C2, −0.0 cents)** and **196.22 Hz (G3,
  +2.0 cents)** — root and fifth, a C power chord out of the box.
- Gates: build clean, ctest 98/98, unity null −141.5 dBFS PASS (flatness 0.074 dB, added
  DC −144.5 dBFS), tune-test PASS, state roundtrip PASS, preset-check 15/15 0 failures,
  fuzz 200 seeds 0 failures. Measurement snapshots (`res-220-fb09`, `mod-rm-55`) pin their
  own frequencies explicitly, so no existing gate moved.
- **Decided (user, 2026-08-28): factory presets stay as they are.** They were not touched. Ten of them set these
  frequencies explicitly and five sit off any note entirely — `40-spectral-ghost` res 400
  Hz (G4 +35¢), `31-crushed-room` res 180 Hz (F#3 −47¢), `10-death-vocal` mod 40 Hz (D#1
  +49¢), `11-death-vocal-fm` mod 28 Hz (A0 +31¢), `43-stretch-ghost` stretch 82 Hz (E2
  −9¢). The detuning is character, chosen by ear; the two tuners plus the RES/MOD/STRETCH
  knobs let a user move any preset onto their material's pitch. Recorded in PANEL.md so a
  future tuning audit does not "correct" it.

## v0.17 — 2026-08-28 — Live edits on a finished one-shot + pop-out sample editor
- **Fixed: sample-editor edits were inaudible until you pressed STOP.** Root cause was
  not the editor and not the DSP — both were already live. With LOOP **off**, a one-shot
  latches `finished` at the region end and `advanceHead` returns 0 forever, so with PLAY
  latched the panel read STOP while the voice was dead. Measured before the fix: a
  mid-note region edit under LOOP **on** worked perfectly (26.4 Hz → 1292.0 Hz, no
  retrigger), while the same edit with LOOP off went −85.9 dBFS → **−240.0 dBFS**.
- Fix (DSP-NOTES §1.1a "Re-arm on edit"): a region/loop/reverse/xfade change, or a new
  sample buffer, re-arms a finished head **while the note is still gated** — the head
  rewinds into the *new* region and `finished` clears. Change detection lives on the
  setters, so a block re-applying identical values re-arms nothing. Deliberately does not
  re-trigger the amp envelope: no re-attack, so dragging a region edge scrubs instead of
  machine-gunning transients. One-shot semantics unchanged — it still never repeats itself.
- After: the same edit renders **−7.3 dBFS at 2483.6 Hz** (the new region, instantly);
  an untouched one-shot still measures −240.0 dBFS, so it does not self-repeat.
- **A defect in the first version of that fix was caught by its own new unit test**: the
  dirty flag survived `noteOn`, so an edit made while stopped would have made the *next*
  one-shot re-arm once and play twice. `noteOn` now clears it — a fresh note has by
  definition already picked up every edit.
- New test `a live region edit re-arms a finished one-shot` (4 sections: identical values
  stay finished, a moved region rewinds into it, LOOP on revives, REV rewinds to the
  region end). 98 → **99 tests**.
- **Sample editor is now a pop-out window**, not a full-panel overlay that hid the knobs
  you were reaching for. Resizable (min 640×340, opens 940×500 centred on the plugin),
  native title bar, always-on-top so a host cannot bury it. Created lazily and hidden
  rather than deleted on close, so zoom/scroll/selection survive reopening. The window
  holds a non-owning pointer, so it is declared after — and destroyed before — the editor
  it points at, and the editor's LookAndFeel is set explicitly (a detached window does not
  inherit one through the parent chain) and cleared in `~MangleView`.
- Gates: build clean, ctest 99/99, unity null −141.5 dBFS PASS, tune-test PASS, state
  roundtrip PASS, preset-check 15/15, fuzz 300 seeds 0 failures, `auval` PASS (note: the
  correct type code is `aumf`, not `aumu` — `IS_SYNTH FALSE`). AU/VST3 reinstalled.
- **Not verified by me: the pop-out window's appearance.** Apple Events authorization was
  refused this session and all three displays were occupied by other fullscreen apps, so
  the GUI could not be driven or screenshotted. Needs the user's eyes.

## v0.18 — 2026-08-28 — Mode-aware source window + hand-drawn oscillator (DRAW)
- **Fixed: the source window drew the loaded sample in every mode.** In OSC you were
  looking at a sample and hearing an oscillator. The small SOURCE display had the same
  bug — its per-mode handling only varied the *empty-state text*, so a loaded sample was
  drawn regardless of mode.
- New `ui/SourceEditorPanel.h` swaps the window's content to match `source.mode`:
  SampleEditor for Sample/Cycle/Tape, the new OscEditor for Osc, a short explanation for
  Noise/Input (neither has a stored waveform). The window title follows too ("Oscillator -
  Draw", "Cycle Window", "Tape Take", …). The small display now draws the osc cycle in
  Osc mode, labelled with the active wave.
- `ui/OscCurve.h` is the single builder for "one cycle of OSC", shared by the small
  display and the pop-out so the two can never drift apart. It mirrors SourceEngine's
  `rebuildDrawTable`/`rebuildHarmonicTable` and `dsp/Waves.h`.
- **New: OSC MODE = DRAW — hand-drawn oscillator waveforms (DSP-NOTES §1.3b).**
  Era-correct, not an invention: RESEARCH.md line 18 records that the original's
  waveforms "could be hand-drawn/edited", and "convert sample to oscillator" is the artist's
  own phrase (Keyboard 3/94).
  - 64 parameters `osc.d01…d64`, −1…+1, defaulting to a sine so DRAW opens on a shape to
    deform rather than a blank page.
  - **Literal table (user decision):** the points are linearly interpolated into the same
    4096-entry table Harmonic mode fills and read with **no band-limiting**, so a drawn
    corner buzzes and aliases exactly like the era. The rejected alternative (deriving the
    64 harmonic amplitudes from the drawing) would have been politer than the period it
    replicates.
  - The editor is a **pencil**, not handles — 64 points is far too dense to grab one at a
    time — and interpolates across fast drags so no point is skipped and left as a spike.
  - **FROM SAMPLE** fills the 64 points from the current CYCLE window of the loaded
    sample, peak-normalized. **SINE** restores the default. Both write ordinary
    parameters, so they are undoable by drawing, saved in presets, and automatable.
- Measured with the chain bypassed (the init patch's `ws.drive` = 12 dB would otherwise
  square anything): a **hand-drawn square** renders h3/h5/h7 at **−9.6 / −14.1 / −17.1 dB**
  against the ideal square's −9.54 / −13.98 / −16.90, with even harmonics at −125 dB. The
  default sine table is clean to **−83 dB** (the 64-point interpolation residue).
- 4 new unit tests (drawn square hits the rails, a quiet drawing stays quiet, the table
  never exceeds ±1, and a Harmonic→Draw switch rebuilds the shared table). 99 → **100**.
- Gates: build clean, ctest 100/100, unity null −141.5 dBFS PASS, tune-test PASS, state
  roundtrip PASS, preset-check 15/15 (the 15 factory presets predate `osc.d*` and load
  fine on defaults), fuzz 300 seeds 0 failures, `auval` PASS. AU/VST3 reinstalled.
- **Not verified by me: the appearance of either new view.** Apple Events authorization
  is refused in this environment and the standalone's window would not surface on any
  capturable display, so the GUI could not be driven or screenshotted. Needs the user's
  eyes. (The standalone's saved `filterState` was overwritten by the seeding harness and
  now opens in Osc/Draw with guitar.wav loaded; window position was restored.)

## v0.19 — 2026-08-28 — Drawing is how you enter DRAW; OSC MODE made findable
- **Root cause of "I don't see an OSC Mode":** v0.18 reused an existing control without
  checking anyone could reach it. It was a combo labelled just **"MODE"**, inside the
  **HARMONICS** block, in the **EDIT view** — a block named after a different feature, in a
  different view from the source controls, while OSC WAVE sat in the SOURCE block. Nothing
  pointed at it.
- **Draw-to-enter (the user's design, and better than a signpost).** Dragging on the
  oscillator plot in *any* osc mode now seeds the 128 points from the cycle currently on
  screen, flips `osc.mode` to Draw, and applies the stroke. You deform the shape you were
  looking at — drag on a saw, dent a saw. `source.oscwave` and the 64 harmonic amplitudes
  are **not** touched, so WAVE restores the previous sound exactly; trying it costs
  nothing. FROM SAMPLE and SINE auto-enter the same way (they were disabled outside Draw
  mode, i.e. unreachable for the same reason).
- **The mode is now visible in three places:** a WAVE/HARMONIC/DRAW strip at the top of
  the source window, an OSC MODE combo in the SOURCE block beside OSC WAVE, and the
  EDIT-view combo (relabelled "MODE" → "OSC MODE").
- **One SOURCE row now swaps contents instead of greying.** OSC WAVE + OSC MODE and
  AMP NZ + PH NZ share a row and no mode uses both, so the inapplicable pair is hidden
  rather than dimmed — otherwise two dead controls would sit on top of two live ones. A
  deliberate, contained departure from the panel's grey-when-inactive convention; initial
  visibility is set in the constructor so the row never flashes both sets on open.
- **Draw resolution 64 → 128 points** (user decision; free now, painful once presets
  reference `osc.d*`). Params are `osc.d001…d128`. Measured, chain bypassed: the default
  drawn sine's worst harmonic residue improves **−83.0 → −89.4 dB** (h3 specifically
  −83.0 → −93.8), and a drawn square now lands at **−9.6 / −14.0 / −16.9 dB** against the
  ideal −9.54 / −13.98 / −16.90 with evens at −124 dB. Grit is unaffected: resolution
  changes how smooth a *curve* can be, never how much an edge aliases.
- **Bug fixed: clicking the small SOURCE display opened a "Load sample..." dialog in every
  mode**, including Osc — so clicking an oscillator shape asked you for a file. Click-to-
  load is now gated to the modes that consume a sample (Sample/Cycle). The EDIT button and
  display tooltips also follow the mode instead of always describing region editing.
- New test `a table seeded from a wave plays back as that wave`. **A first version of it
  failed and the failure was mine, not the code's**: it asserted a seeded *saw* matches the
  analytic saw within −20 dB, but a saw's instantaneous wrap cannot be represented by any
  point table — the table must ramp across one interval, which predicts
  `10·log10((1/128)·(2²/3)/(1/3))` = **−15.05 dB**, and it measured −14.76. The test now
  asserts tightly on continuous waves (Sine and Tri, both < −40 dB) and pins the saw's
  known floor between −12 and −18 dB, with the derivation in a comment.
- Gates: build clean, ctest **101/101**, unity null −141.5 dBFS PASS, tune-test PASS, state
  roundtrip PASS, preset-check 15/15, fuzz 300 seeds 0 failures, `auval` PASS. AU/VST3
  reinstalled. Fuzz's worst absolute peak rose 41.9 → 105.2 because a random 128-point
  table is a far hotter source than a sine; `--fuzz-diagnose 100165` confirms it is gain
  staging, not divergence (bypassing the waveshaper drops it 94.6 → 10.2, with out.level
  3.8 and delay feedback 0.819 stacked on top).
- **Still not verified by me: appearance and feel.** Apple Events are refused in this
  environment and the standalone window will not surface on a capturable display. Whether
  draw-to-enter feels right, and whether 128 dots read as texture or noise, is the user's
  call.

## v0.20 — 2026-08-28 — Custom wavetable distortion: a sample AS the transfer curve
- The waveshaper was already table distortion — `Custom` is a lookup curve — but it could
  only be hand-drawn, at **16 points**. New **FROM SAMPLE** action (EDIT view, CURVE block)
  fills it from the CYCLE window of the loaded sample, so the sample's shape decides how
  everything downstream gets mangled (docs/DSP-NOTES.md §3.2). Centred first, then
  peak-normalized, so an off-centre slice does not waste half the output range.
- **Custom curve raised 16 → 128 points** (`ws.c001…ws.c128`, identity-diagonal defaults).
  16 points cannot carry a shape imported from audio — it would read as a staircase, not
  as the sample. No factory preset referenced `ws.cNN`, so the renumber breaks nothing.
- **The curve editor is now a pencil, not 16 handles**, and **dragging on it selects
  CUSTOM for you** — the curve you were looking at is seeded into the 128 points first, so
  you deform it rather than jumping to a diagonal. Mirrors the oscillator's draw-to-enter
  from v0.19. `ws.drive`/`ws.morph` and the previous curve index are untouched, so
  re-selecting the old curve restores it.
- Measured (sine source, drive 0, morph 1, rest of the chain bypassed):
  - Custom at its **untouched identity diagonal: THD −74.2 dB** — selecting it changes
    essentially nothing until you draw, as the spec's editor did.
  - Custom **FROM SAMPLE (a guitar cycle): THD −0.3 dB** — the harmonics are as loud as
    the fundamental. An audio slice is a **non-monotonic** transfer function, so this is
    broadband scream, not saturation. Deliberate and documented; not a defect report.
  - Output peak **1.000**, inside the ±1.0 convention of §0.
- **Two real bugs caught by the existing suite while making the change**, both mine:
  1. `Waveshaper::setCustomPoints` still copied only **16** of the 128 points, leaving 112
     at zero — the curve was a short diagonal then a cliff to silence.
  2. Three tests in `test_phase_c.cpp` passed a 16-element array to a function that now
     reads 128 — an **out-of-bounds read**. Tests now size themselves from
     `Waveshaper::customPointCount` so they cannot drift from it again, and the setter's
     parameter is renamed from `y16` with the length stated.
- Gates: build clean, ctest **101/101**, unity null −141.5 dBFS PASS, tune-test PASS, state
  roundtrip PASS, preset-check 15/15, fuzz 300 seeds 0 failures (worst peak 25.9),
  `auval` PASS. AU/VST3 reinstalled.
- Next in this thread: the deferred **Oscillator waveform timeline** (wavetable morphing
  across a note), DESIGN §6 — the user chose to do sample-to-curve first.
- **Not verified by me: appearance.** GUI capture is still blocked in this environment.

## v0.21 — 2026-08-28 — Code review remediation: five findings, all fixed
Self-review of everything built this session (v0.16–v0.20). Two of the five were
introduced an hour earlier by the v0.20 curve renumber.

- **1. COPY→CUSTOM was completely dead.** `ui/EditView.h` still wrote `ws.c01…c16`, ids
  that stopped existing in v0.20. `getParameter` returned nullptr, a null guard swallowed
  it, and the button silently did nothing. **Root cause was structural, not a typo:**
  parameter ids for the indexed banks were formatted with literal `"ws.c%02d"`-style
  strings in four separate files, so renumbering a bank orphans callers the compiler
  cannot see. `Params.h` now owns the ids and the counts (`harmonicId`/`drawPointId`/
  `curvePointId`, `harmonicCount`/`drawPointCount`/`curvePointCount`, the latter two
  `static_assert`ed against `SourceEngine::drawPointCount` and
  `Waveshaper::customPointCount`), and every caller goes through them.
- **New `ts_cli --param-check` gate**, the guard that would have caught this: it proves
  every id the builders generate resolves to a real parameter. Reports **320 banked ids,
  0 missing**. Added to the standing gate list.
- **2. Draw-to-enter on the curve could seed the wrong shape.** `CurveEditor::enterCustom`
  called `evaluateCurrent()` without `refreshFromParams()`, which that function's own
  comment says is required because the cache is a 15 Hz poll — up to **66 ms stale**.
  Changing the CURVE combo and dragging inside that window deformed the *previous* curve.
  Intermittent, so the worst kind. Fixed by refreshing first, exactly as COPY→CUSTOM
  already did. (The oscillator's `enterDrawMode` never had this: it reads live atomics.)
- **3. The FROM SAMPLE button sat on top of the drawable area.** The plot was the whole
  component while the button occupied its bottom-right 96×18, so that corner of the curve
  could not be drawn. `CurveEditor` now has a `plotArea()` that trims a button strip, used
  by `paint`, `paintAt` and `mouseDown` alike — the picture and the touch target are the
  same rectangle, which they were not before.
- **4. TAPE mode drew the loaded sample, not the tape take** — the same defect the user
  reported for OSC, still present for TAPE, in both the small display and the region
  editor. Worse than cosmetic: region and loop drags act on the tape buffer, so you were
  editing against a waveform that was not the one sounding.
  - `TapeBuffer::active` is now `std::atomic<size_t>` and `copyActiveTo()` latches the
    generation **once**, so a FLIP part-way through a copy cannot mix two takes. Documented
    honestly: a FLIP immediately followed by REC can still tear one display frame, which
    the GUI's poll corrects; it is not for audio use.
  - `PluginProcessor` gained `refreshDisplaySource()` plus `getDisplayBuffer()`/
    `getDisplayName()`/`getDisplaySr()`; the two views switched to them. Click-to-load
    deliberately still tests `getSampleBuffer()` — the loader means the sample, whatever is
    being drawn.
- **5. Stale docs in code**: `Waveshaper.h` and the COPY tooltip still claimed 16 points.
- Verified: build clean, ctest **102/102** (2 new tape-snapshot tests), unity null
  −141.5 dBFS PASS, param-check 320/0, tune-test PASS, state roundtrip PASS, preset-check
  15/15, fuzz 300 seeds 0 failures, `auval` PASS, AU/VST3 reinstalled. The v0.20 numbers
  re-measured **identical** after the refactor (identity diagonal THD −74.2 dB,
  FROM SAMPLE THD −0.3 dB, peak 1.000), confirming no behaviour changed.
- **Checked and found clean:** no parameter-id drift, no heap allocation on the audio
  thread (libc++ SSO is 22 chars, longest id is 17), `TapeBuffer::prepare` pre-allocates
  and recording never grows a buffer, the UI's `breakpointEval` is still byte-identical to
  the DSP's, and `OscCurve`'s Draw interpolation matches `SourceEngine::rebuildDrawTable`.
- **Known, not a code change:** anything saved with pre-v0.20 ids (`osc.d01…d64`,
  `ws.c01…c16`) silently loses that data on restore, because APVTS ignores unknown ids.
- **Not verified by me: appearance.** GUI capture remains blocked in this environment.
  Needs eyes: COPY→CUSTOM now does something, the curve's bottom-right corner is drawable,
  and TAPE mode shows the tape take.

## v0.22 — 2026-08-30 — One combined panel + pitch bend wheel
- **MANGLE and EDIT are one window now.** They used to be two views swapped by a header
  toggle, so half the instrument was always hidden — you could not watch a filter envelope
  while turning the knob that feeds it. Both views already laid out to arbitrary bounds, so
  this was a sizing job, not a rewrite: MANGLE on top, the twelve EDIT blocks reflowed from
  a 3×4 to a **4×3** grid beneath. Cells land at ~315×129 against the old 316×130, so no
  block got tighter. The toggle is gone; the preset bar took its space.
- **The window is resizable and scalable for the first time.** Everything lays out at a
  fixed design size (1300×1028) inside a `content` component that is then scaled by an
  `AffineTransform`, so every existing layout calculation stayed in design coordinates and
  none needed re-tuning. Limits 55 %–175 %; **55 % is 715×567, which keeps the whole panel
  usable on a 13" laptop**, so PANEL.md's standing size constraint still holds. The chosen
  width is stored on the APVTS state tree, so it rides along with `getStateInformation` and
  returns on reopen. Aspect ratio is locked — the deliberate price of scaling one layout
  uniformly instead of maintaining several.
- **Pitch bend implemented.** `processBlock` parsed only note-on/note-off; a wheel was
  silently discarded. New `midi.bendrange` (0–24 semitones, **default 2**, 0 disables the
  wheel outright), shown as BEND in the EDIT view's VOICE block beside RETRIGGER.
  - Bend rides on the source transpose rather than getting its own path, so one sum reaches
    all three pitch routes already in place: Sample/Tape varispeed (bend changes **speed**
    as well as pitch, exactly like the PITCH knob), Osc/Cycle/Noise via `noteFreq()`, and
    live Input via `TapeShift`. MOD/RES/STRETCH are deliberately not bent — they are
    absolute frequencies, consistent with the C-root rule of v0.16.
  - **The MIDI parse loop was hoisted above `applyParams`.** It ran after, so folding bend
    into `gatherParams` without moving it would have applied every bend one block late.
- Measured with `bend.mid` (new fixture; held C3, wheel centred / full up / centred / full
  down): at **range 2**, 130.81 / 146.83 / 130.81 / 116.54 Hz — ±200 cents to within
  **0.03 cents**. At **range 12**, a clean octave either way (261.60 Hz, −0.15 cents, which
  is exactly the 14-bit wheel's +0.99988 asymmetry documented in DSP-NOTES §9.1 showing up
  where predicted). At **range 0**, 130.81 Hz at every wheel position.
- **New standing gate `ts_cli --bend-test`** so bend is guarded, not just measured once:
  4 cases, 0 failures.
- **Two bugs of my own, caught before shipping:**
  1. Hoisting the MIDI parse left the original `events.clear()` sitting *after* it, which
     would have wiped every real note before `engine.process` — all MIDI input dead.
  2. `pitchBendNorm` was documented as being reset in `prepareToPlay` and **the code was
     never written** — a comment asserting something false. A wheel held at a transport
     stop would have stranded the instrument detuned. Both fixed.
- **`ts_cli`'s MIDI reader was the reason bend first measured as doing nothing.** It
  flattened MIDI files into note-on/off only and discarded everything else, so the wheel
  never reached the plugin. `TimedNote` is now `TimedMsg` carrying the `juce::MidiMessage`
  itself, so any message type the plugin learns to read is testable without touching the
  CLI again. `notes.mid` re-renders identically (14.00 s, peak 1.000, −5.7 dBFS RMS).
- Gates: build clean, ctest **102/102**, unity null −141.5 dBFS PASS, bend-test 4/4,
  param-check 320/0, tune-test PASS, state roundtrip PASS, preset-check 15/15, fuzz 300
  seeds 0 failures, `auval` PASS, AU/VST3 reinstalled. **M8 poly re-run: tuning 1.02 cents,
  attack 12.0 %, release 18.0 %, retrigger 16 — all PASS.** (An earlier M8 run of mine
  showed 238.69 cents; that was me running the gate against a *mono* render when
  TEST-PLAN documents the tuning check as poly-only — mono collapses chords to the last
  note. My harness error, not a regression.)
- **Not verified by me: how the combined panel looks.** This is the largest visual change
  the project has had and GUI capture is still blocked in this environment. Whether
  1300×1028 reads as spacious or sparse, and whether text stays legible at 55 %, is the
  user's call.

## v0.23 — 2026-09-01 — Full review remediation, first-run presets, tiered execution
Three Sonnet-tier reviewers audited the DSP layer, the plugin/GUI layer, and release
readiness in parallel; every finding was re-verified at the line level by the top tier
before acceptance. Seven Sonnet agents then implemented the fixes on disjoint files, each
in its own build directory, with the top tier reviewing every diff and running all gates.

**Code defects fixed (each with a red-then-green test where expressible):**
- **Harmonic-mode table rebuild storm under FM** (`SourceEngine.h`): the 2 % rebuild test
  was keyed on the FM-modulated instantaneous frequency, so a fast modulator fired the
  262k-op rebuild every sample, per voice. Measured **436 ms → 0.8 ms** for a 1 s render
  (540×). Now keyed on `noteFreq()` alone; FM still drives the phase. New
  `test_harmonic_fm.cpp`.
- **Use-after-free in preset save** (`ui/PresetBar.h`): the modal dialog's callback
  captured a raw `this`; host closes the editor mid-dialog → crash. Now
  `Component::SafePointer`, the codebase's v0.12 idiom that this one site had missed.
- **Audio-thread allocation on the 33rd held note** (`Engine.h`): `heldNotes` was
  `reserve(32)` with an uncapped `push_back`. Capped at 32, oldest dropped.
- **PITCH EXT was a dead control** (`PluginProcessor.cpp`): defined, shown, cached, never
  read. Now gates the knob to ±24 st unless EXT, exactly like `flt.ext`. Measured
  **523.22 Hz with EXT off / 1046.54 Hz on** at pitch +36 (expect 523.25 / 1046.50).
- **Filter floor bypassed by modulation** (`FilterStack.h`, `Voice.h`,
  `PluginProcessor.cpp`): the 500 Hz era floor applied only to the knob; envelope
  modulation then clamped at 20 Hz. The floor now travels with the cutoff and is applied
  after modulation (era-correct, user-reversible). Test went red at **−14.1 dB** of leak
  below the floor, then green. New `test_filterfloor.cpp`.
- **Envelope decayed into subnormals while gated with sustain 0** (`EnvelopeADSR.h`): the
  one recursive path without the ±1e-18 nudge. Nudged; a gated voice below −120 dB with
  sustain 0 now reports inactive. (Masked in the plugin by `ScopedNoDenormals`; the DSP is
  host-independent.) Two new envelope tests.
- **Stuck note across `prepare()`** — found by the NEW `test_engine.cpp`, not by the
  review: a held note survived a sample-rate/buffer change at full level because
  `EnvelopeADSR::prepare` only re-derived coefficients. `Voice::prepare` now resets all
  three envelopes. The agent that found it correctly pulled it up rather than editing
  outside its whitelist.
- `applyTuneLock` now clamps before `convertTo0to1` (the v0.12 idiom its siblings had);
  `getTailLengthSeconds` 2 → **10 s** (res.fb 0.995 rings far past the 2 s delay max);
  dead `(auxDest == 4 ? 0 : 0)` ternary removed.
- **`HarmonicEditor.h` hand-rolled `"osc.h%02d"`** — the v0.20 bug class, on my v0.21 list
  and missed. Fixed, and **`scripts/check_ids.py`** now fails the build-gate list if any
  bank-id literal exists outside `Params.h`. It caught HarmonicEditor before the fix.
- **Warning flags now apply to `ts_cli` and `ts_tests`** (`CMakeLists.txt`); they were
  plugin-only, so the "no new warnings" gate was silent for the CLI. Turning them on
  surfaced **11 pre-existing warnings** (3 missing prototypes, 7 float-equality compares
  in tests, 1 precision loss) — all fixed. The float compares are sites where bit-exact
  identity IS the contract (passthrough, hold, determinism, snapshot); they now go through
  `std::equal_to` so the check stays exact without the warning.

**Tests:** 102 → **112**, including the first-ever direct `Engine`/`Voice` tests (held-note
cap, poly summation +4.70 dB ≈ √3, mono collapse 0.0 dB, unison, prepare() reset).
Unison measured **+7.78 dB = 20·log10 √6** at onset — coherent voices before the ±12-cent
spread decorrelates; v0.2's 0.7 dB was over a sustained note. Both true; DSP-NOTES §9 and
TEST-PLAN M8 now say so instead of promising ±1 dB unconditionally.

**First-run experience:** 14 of 15 presets were silent until a file was supplied. Added
**`01-start-here`** (saw → shaper → filter → resonator at C3) and **`02-start-here-noise`**
(pitched noise, clipped, 12-bit, echoed). Both sound on one PLAY press with nothing
loaded: **−12.4 / −13.9 dBFS RMS, peaks 0.32 / 0.37**. Bank is 17 presets, preset-check
17/17. PANEL.md gained a "Make a sound in 30 seconds" section pointing at them.

**Docs:** PANEL.md no longer describes two switchable views; PITFALLS.md rewritten for the
C++/JUCE engine from the hazards that actually bit (v0.12–v0.23); TEST-PLAN gained M18–M22
for fuzz, param-check, bend-test, tune-test and check_ids; Diffuser reclassified as
"not in the original"; stale `ensemble/` pointers and `[BELIEVED]` tags cleared; Flatten
and SpectralInverter comments now state what the code does; CMake `VERSION` **0.1.0 →
0.23.0** (bundle reports 0.23.0).

**Gates:** build clean on all targets with warnings on, ctest **112/112**, unity null
−141.5 dBFS PASS, param-check 320/0, bend-test 4/4, tune-test PASS, state roundtrip PASS,
preset-check 17/17, fuzz 300 seeds 0 failures, check_ids OK (58 files), M8 poly tuning
**1.02 cents** PASS, `auval` PASS, AU/VST3 reinstalled.

**Still open, and the user's:** listening pass vs the band material; looking at the v0.22
panel; Reaper checks; product name (shortlist in the plan: REZAMPLE recommended, BROKEN,
DOWNWARD, SELF DESTRUCT; REZynth collides aloud with DFX Rez Synth); identity codes to
ZQ SFX once the name is chosen; GPLv3 + LICENSE file; signing/notarization; deployment
target and Intel decision; installer.

## v0.24 — 2026-09-01 — Named: **Broken** by ZQ SFX. GPLv3. Sound and panel confirmed.
- **The user confirmed non-negotiables #1 and #2 by ear and eye:** "Sounding good" against
  their own listening, and the v0.22 combined panel "looking good." First time either has
  been recorded since the project was founded (open since v0.2).
- **Product name: Broken** (ZQ SFX). The user's choice, with a nod to the album lettering:
  the panel title draws the K mirrored (`PluginEditor::Logo`, a transform about the glyph's
  centre; the face is the panel's bold for now — a custom font is the user's to supply).
- **Identity codes set, once, before release:** `COMPANY_NAME "ZQ SFX"`, `PRODUCT_NAME
  "Broken"`, `BUNDLE_ID com.zqsfx.broken`, `PLUGIN_MANUFACTURER_CODE Zqsx`, `PLUGIN_CODE
  Brkn`, user presets in `~/Library/Audio/Presets/ZQ SFX/Broken`. `auval` registers
  `aumf Brkn Zqsx — ZQ SFX: Broken`. **These must never change again**; hosts key sessions
  on them. Internal identifiers (CMake target, `ts::` namespace, class names) keep the
  working name on purpose — they are not user-visible and renaming them has wide blast
  radius for no benefit.
- **State-tree migration.** The APVTS tree type is now `"Broken"`; `restoreFromXml`
  accepts the legacy `"TurboSynth"` tag so every session and preset saved before the
  rename still loads. New gate `ts_cli --state-migrate-check` proves it (TEST-PLAN M23):
  legacy tag → probe parameter survives, **PASS**.
- **Licence: GPLv3.** `LICENSE` is the official GNU text; README carries the copyright
  (ZQ SFX LLC), JUCE attribution under its GPLv3 option, and the a third party/the band non-affiliation
  note. Chosen because the plugin is free and ZQ SFX is a sound-design company whose total
  revenue would not fit JUCE's Starter tier.
- Docs: only the *working-title* lines changed. "TurboSynth" stays wherever it names the
  original hardware. `createPluginFilter` gained a prototype now that warnings reach every
  target.
- Gates: build clean, ctest **112/112**, unity null −141.5 dBFS PASS, state-migrate PASS,
  state roundtrip PASS, preset-check 17/17, param-check 320/0, bend-test 4/4, tune-test
  PASS, fuzz 300 seeds 0 failures, check_ids OK, M8 poly 1.02 cents PASS, `auval` PASS.
  Old `TurboSynth.component`/`.vst3` removed from `~/Library/Audio/Plug-Ins`; `Broken.*`
  installed. The standalone now keeps its settings in `Broken.settings`; the old
  `TurboSynth.settings` is orphaned and harmless.
- **Still the user's:** Reaper automation/recall checks; signing + notarization (Apple
  Developer account); deployment target and Intel decision; installer; the logo font.

## v0.25 — 2026-09-01 — FILTER knob travel stops at the floor
- The FILTER readout already showed the *effective* cutoff (floored at 500 Hz unless FLOOR
  EXT), but the knob itself could still be dragged through 20–500 Hz: a dead zone where
  the number sat at "500.0" and nothing changed. `MangleView::applyFilterFloorToKnob` now
  sets the slider's travel to 500–20 kHz (20 Hz with EXT), same log feel as `logRange`
  (skew centred on the geometric mean), re-applied whenever EXT flips. The parameter keeps
  its full range for presets and automation; only the knob is stopped at the floor.
  Switching EXT off with the cutoff already below 500 pulls the parameter up to 500 so
  knob, readout and DSP agree instead of the knob clamping against a lower host value.
- Gates: build clean, ctest 112/112, `auval` PASS, AU/VST3 reinstalled. GUI-only change;
  the knob behaviour needs the user's hand on it.

## v0.26 — 2026-09-01 — UI inventory for the restyle; four drifts it exposed, fixed
- A Sonnet-tier explorer produced an exhaustive inventory of every UI element (≈120
  controls: type, parameter id, range, default, design-px size, position rule, visibility
  rule, tooltip, states, mode-dependent behaviour) for the user's Claude Design restyle.
  Its sharpest claims were spot-checked against the code before publication; all held.
- Drift it exposed, fixed here: docs said the design size was 1300×**1030**, the code
  computes **1028** (docs corrected); PANEL.md's SAVE tooltip still pointed at the retired
  `ZLQ/TurboSynth` preset path; DELAY and MIX shared one tooltip (MIX now says what MIX
  does); the missing-file red was a hard-coded `0xffdd4433` — now `colour::warn` in
  `Theme.h`, the palette's one non-accent signal colour.
- Left for the designer, deliberately: ad-hoc button widths/heights (22/24/26/28), two
  small-knob footprints (48 vs 56 wide), no hover state anywhere, multi-word button labels
  likely to clip at 55 %, `geom::bigKnob`/`smallKnob` declared but unreferenced, and the
  same parameter surfaced in two or three places (`osc.mode` ×3, `source.oscwave` ×2,
  `noise.amp`/`noise.phase` ×2).
- Gates: build clean, `auval` PASS, AU/VST3 reinstalled. Published as the "Broken UI Spec"
  artifact.

## v0.27 — 2026-09-01 — The rack: Claude Design restyle implemented, tooltips complete
The approved handoff (`ClaudeDesign/design_handoff_broken_ui/`, "degraded 90s rackmount
sampler") is implemented natively in JUCE. No parameter, DSP or behaviour changes — every
audio gate below returned bit-identical numbers, which is the proof.

- **Visual system (top tier):** Theme.h rewritten to the handoff token table (chassis and
  panel gradients, teal silkscreen, VFD-green LCD set, one accent #E8622A); Barlow
  Condensed / VT323 / IBM Plex Mono embedded as BinaryData (OFL texts bundled); LCD
  ComboBoxes with ▾ carets; gradient buttons whose legends turn accent on hover; round
  glowing LEDs; per-knob silkscreen tick rings printed on the panel; chassis header with
  the mirrored-K logo, REV.C / SN 0113 stamp, footer stamps ("BRKN-01 · DO NOT SERVICE ·
  48V PHANTOM PAIN" / "MFD 2026 · CALIBRATED NEVER"); a seeded grain + vignette + corner
  blotch + slot-head-screw grime layer baked once per window size (JUCE has no overlay
  blend; a pre-baked low-alpha noise field is the documented approximation).
- **Knobs are filmstrips** (user substitution for the handoff's drawn scallops): "Analog
  Knob Kit 01" by **Julian Behrens (Noisehead)** — three 128-frame strips (180/100/70 px
  frames), mapped scalloped→60 px primaries, stripe→42/38, metal cap→34, chosen per
  slider by dial size with a "brokenStrip" property override. His licence permits VST use
  and modification, requires credit for open-source (given in README), forbids
  resale/standalone redistribution; the licence text ships at `plugin/assets/knobs/`.
  A drawn-pointer fallback remains if an asset ever fails to load.
- **Layout regrouped per the handoff** (Sonnet, gatekept): design size 1300×1028 →
  **1520×1124**, header 48. Top row `300 | 620 | rest`: SOURCE · MANGLE (shared 4-column
  grid, 60 px primaries with LEDs above and LCD readouts below, one combo row, hairlines)
  · PLAY+TAPE side by side with a horizontal OUTPUT beneath. **RETRIGGER and BEND moved
  into PLAY** (ids untouched); **TAPE gained a TAKE LCD** (`TAKE --` / seconds). The
  twelve EDIT blocks became five panes: ENVELOPES (+ FLOOR EXT), OSCILLATOR (+ LOOP
  XFADE, the relabelled sample.xfadeshape), WAVESHAPER, MODULE TRIMS (five ruled
  segments), TIME. Inapplicable-control dimming is now 30 % per the handoff.
- **Displays as VFD screens** (Sonnet, gatekept): waveform with 3-px stripe texture,
  tuners with LCD note/Hz and a centre-marked cents bar ("–– Hz" when unpitched — was
  previously blank), meter with printed tick scale and green→amber shift above −6 dB,
  harmonic bars with bright caps, curve display with a phosphor ghost stroke and, for the
  first time, visible Random-curve breakpoints.
- **Pop-out in the same chrome** (Sonnet, gatekept), and the WAVE/HARMONIC/DRAW strip now
  renders its active mode as a lit segment via toggle state instead of per-button colour
  overrides the new LookAndFeel ignored.
- **Tooltips complete.** The three audited gaps are closed: OscEditor (six added,
  including the plot), the sample editor's wave area and minimap, and — the structural
  one — **the pop-out window now owns its own TooltipWindow**; the editor's is parented to
  the editor and could never render there. Sweep says no interactive control without one.
- **Mistake made and recovered, for the record:** the Theme.h rewrite overwrote the file
  without re-reading it first and silently dropped the `Block` and `Light` classes that
  lived below the token table; the whole GUI failed to compile until both were
  reconstructed (now in the rack style). The look-before-overwriting rule exists for
  exactly this; noted in PITFALLS terms: read the WHOLE file before a Write.
- Gates: build clean on all targets, ctest **112/112**, unity null −141.5 dBFS PASS,
  param-check 320/0, bend-test 4/4, tune-test PASS, state-migrate PASS, state roundtrip
  PASS, preset-check 17/17, fuzz 300 seeds 0 failures, check_ids OK, M8 poly 1.02 cents
  PASS, `auval` PASS, AU/VST3 reinstalled.
- **The pixel gate is the user's**: compare against `screenshots/full-ui.png` and call the
  adjustments. Docs: PANEL.md now names the handoff as the visual spec of record.

## v0.28 — 2026-09-02 — Screenshot-driven polish: brighter, snugger, screenier
Driven by the user's first screenshot of the running v0.27 GUI plus two new calls.

- **Grime halved** — the vignette ran to 40 % black with 28 % corner blotches, roughly
  double the reference, sinking panels and the dark mid-size knobs. Now 18 % / 12 %, noise
  density and alpha halved. The screws stay.
- **The 90s green screen, everywhere it makes sense** (user request): a shared
  `TsLookAndFeel::drawScreen` paints one phosphor treatment — recessed bezel, green-lit
  glass with a centre phosphor wash, faint scanlines — used by every dropdown, every
  knob's LCD readout (via `drawLabel` on slider text boxes), the sample display, both
  tuners, the harmonics display, the waveshaper curve, and the oscillator plot. LCD text
  gained a real four-pass phosphor bloom instead of a single offset ghost.
- **Header:** the REV.C / SN 0113 stamp is deleted at the user's request (it also
  collided with the logo in the screenshot); the preset cluster is capped at 620 px and
  right-aligned instead of spanning the window.
- **Panel voids closed:** top region 620 → 510, bottom rows 255/155 → 225/125; design
  height 1124 → 954. Content packs toward panel titles instead of pooling at the bottom.
- **MONO | POLY is a real segmented pair** — the single swap-label toggle rendered as one
  absurd full-width orange bar. Two buttons, one parameter (clicks write `voice.mode`
  with gestures; the 10 Hz timer lights the matching segment, so host automation follows
  too). PLAY narrowed 394 → 310.
- **Label truncation root-caused:** small knobs were placed at exactly dial width, so
  titles like PREDELAY, FEEDBACK, PITCH MIX had ~34 px of room. Fifteen slots widened to
  66 px (three fixed 48 px slots widened to match so nothing overlaps); MODULE TRIMS
  captions got their own 16 px band plus air.
- Confirmed non-bugs from the screenshot: the WAVESHAPER curve display has real bounds
  (150×104 — it sat under the macOS volume HUD and the heavy grime), and the OSCILLATOR
  "clipping" was largely the same HUD.
- Gates (GUI-only change, identical numbers): ctest 112/112, param-check 320/0,
  preset-check 17/17, state-migrate PASS, check_ids OK, `auval` PASS, AU/VST3 reinstalled.
- Next screenshot is the user's; grime level and screen intensity are explicitly theirs
  to judge.

## v0.29 — 2026-09-02 — Second screenshot review: eleven findings, all addressed
- **Footer deleted** (user's call — it also rendered as mojibake, my Latin-1/UTF-8 bug;
  removal beats fixing).
- **OUTPUT no longer clips**: the PLAY/TAPE row hoarded the right column (420) while
  OUTPUT's knobs were cut mid-dial; row is now 330 and OUTPUT gets real height.
- **FROM SAMPLE off the glass**: the waveshaper's phosphor screen stops above the button
  strip instead of running under it.
- **ENVELOPES rows finally say which is which**: FLT / AUX gutter captions — two identical
  ADSR rows were distinguishable only by inference.
- **MANGLE bypass LEDs centred over their knobs** (they floated at column corners reading
  as decoration) **and given 22 px hit areas** around the 10 px lamp.
- **Dimmed controls stay learnable**: the knob ghosts to 30 % per the design, but its
  LABEL now holds 55 % so a new user can see that POS/LEN/IN TRIM exist; tick rings dim
  with the knob. SOURCE's dead band before the tuner tightened.
- **silkCaption raised** #62807C → #7A9A94 — 10 px captions sat below WCAG 4.5:1 on the
  panel, and MODULE TRIMS' captions were the dimmest text on the panel while being its
  only organizing labels.
- **Keyboard focus is visible** for the first time: a silk ring on buttons and combos, a
  ring hugging the dial on knobs.
- **Screen readers get real names**: `setTitle` on every Knob/Combo/LitToggle wrapper
  (silkscreen label or parameter id; LEDs use their tooltip's first sentence) — they were
  anonymous sliders and buttons before.
- Gates (GUI-only, identical): ctest 112/112, param-check 320/0, preset-check 17/17,
  state-migrate PASS, check_ids OK, `auval` PASS, AU/VST3 reinstalled.

## v0.30 — 2026-09-02 — Third screenshot review: bigger, denser, symmetric
Driven by the user's screenshot notes (negative space, small text, waveshaper asymmetry)
plus my own findings on the same shot.

- **Everything up a size class:** dials 60/46/44/40 (from 60/42/38/34 — primaries 66),
  PITCH 44 → 50, title strips 11 → 13 px with 11.5 px silkscreen, block titles 14 px, LCD
  combo/menu text 16 px, readout boxes 16 px tall, waveform display 118 → 150 (it is
  SOURCE's centrepiece and the panel had the void to spend).
- **Voids spent:** ENVELOPES content vertically centred; TAPE's transport buttons taller
  (32) with the lamp grouped under TAKE instead of haunting the panel corner; PLAY/TAPE
  row 350; regions 545/415 so panels fit their grown content.
- **WAVESHAPER made symmetric** (the user's example): the curve display takes the left
  half at full height; TRIM / RND / COPY TO CUSTOM distribute evenly down the right half
  instead of crowding the top row over an empty quarter.
- **OSCILLATOR bottom row spread** into four even cells; the LOOP XFADE field grew to a
  proper 46 px LCD.
- **MODULE TRIMS on one baseline** — the TYPE combo floated above its neighbour knobs;
  every segment's content now centres on a single shared row.
- **TIME joins the stripe-knob family**: it sat beside MODULE TRIMS with different knob
  hardware (metal caps vs stripes) at the same hierarchy level, which read as an accident.
  Metal caps remain the accent for PLAY/SOURCE small trims only.
- **Unlit LEDs are findable**: rim raised from near-invisible #2A2F31 to #3D4448.
- Meter widened 13 → 16. Trim caption fonts 10 → 11.
- Gates (GUI-only, identical): ctest 112/112, param-check 320/0, preset-check 17/17,
  state-migrate PASS, check_ids OK, `auval` PASS, AU/VST3 reinstalled.
## v0.31 — 2026-09-02 — Fourth screenshot review: aligned pitch cluster, no pooled voids
- SOURCE lower pane recomposed (the user's "sloppy" call): PITCH / FINE / TUNE now sit in
  three even columns on ONE shared baseline — FINE gets the same 79px box as PITCH so
  both dials render at 50px with title rows and readouts aligned (was three different
  vertical centres and ragged gaps). POS / LEN / IN TRIM reuse the same three columns so
  the two rows grid-align. The leftover slack is split half above the OSC/NOISE swap row
  and half before the IN tuner (was one pooled void).
- MANGLE / PLAY / TAPE: switched from top-packing to distributed gaps — each layout now
  computes its spare height and spreads it into the inter-row gaps (capped at 40/28/26 px
  per gap respectively) instead of pooling it at the panel bottom. TAPE transport buttons
  up 32 → 36 px.
- GUI-only change; full gate set re-run with identical numbers: ctest 112/112 (4,094,297
  assertions), --param-check 320/0 missing, --preset-check 17/0, --state-roundtrip PASS,
  --state-migrate-check PASS, --bend-test 0 failures, --tune-test 0 c, --fuzz 300/0
  (worst peak 44.527 @ seed 100075), check_ids 58 OK, auval aumf Brkn Zqsx SUCCEEDED.
  AU + VST3 reinstalled.


## v0.32 — 2026-09-02 — MIX / BYPASS / RANDOMIZE + fifth screenshot pass
- **Global MIX** (`chain.mix`, 0–100 %, default full wet): dry = the un-mangled source
  per voice through the same amp envelope and comp gain, blended before COLOUR/OUT so
  they act on the result — parallel mangling in every mode. Per-block linear ramp with
  snap-to-target; at 100 % the math is bit-exact wet. New Catch2 engine case (bit-exact
  at 1, source-equal at 0, click-free ramp). DSP-NOTES §12b.
- **BYPASS** (`bypass`, host-bindable via getBypassParameter()): 25 ms crossfade to true
  per-channel input pass-through, engine skipped when fully bypassed (10 s tail chop and
  dropped notes are the documented trade-off); steady active path keeps the verbatim
  copy so the unity null stays bit-true. New gate `--bypass-check`: 20 stereo noise
  blocks, 0 non-identical samples.
- **RANDOMIZE + UNDO** (PresetBar RND/UNDO): gesture-wrapped setValueNotifyingHost over
  every param EXCEPT source.mode, out.level, bypass, play.hold, tape.rec/flip and the
  draw/curve banks (harmonics included). Caps: res.fb ±0.85, dly.fb ≤0.63, ws.drive
  ≤30 dB; audibility guards amp.a ≤2 s, flt.cutoff ≥200 Hz. UNDO restores the exact
  pre-roll state (replaceState of a copyState snapshot). New gate `--rnd-check`:
  50 rolls, 0 failures — it caught its own first bug (the gate compared UNDO against
  the original state instead of the pre-last-roll state; product was correct).
- Fifth screenshot fixes: MORPH/FREQ/FB and INVERT/DELAY/MIX gained LCD readouts (lBoxH
  59 → 75, eating MANGLE's dead bands); combo row anchored to the 4-column grid (CURVE
  under DRIVE, MODE/WAVE/SRC from MOD, POLES right-aligned under FILTER); SOURCE swap
  row tucked under POS/LEN/IN TRIM so Sample mode has one designed void, not two holes;
  BEND gained an "st" readout; TIME right inset 6 px. TAPE lamp root cause: MangleView
  carried a duplicate nested Light that drew a SQUARE outline (the "stray checkbox"),
  shadowing Theme's round LED — deleted; the lamp is now ui::Light at 14 px, centred.
- OUTPUT block: MIX knob leads the row (signal order), BYPASS toggle after OUT. All 17
  factory snapshots now carry chain.mix/bypass defaults so loading one always resets
  them. Doc drift fixed: DESIGN.md/CLAUDE.md stretcher scope (shipped v0.10), PANEL.md
  1520×1024 + five-pane text, this file's v0.30/v0.31 order.
- Gates: build clean, ctest **113/113**, unity null **−141.5071 dBFS** PASS — and still
  −141.5071 at chain.mix=0.37 (all-bypassed ⇒ dry ≡ wet, the free §12b property),
  param-check 320/0, preset-check 17/0, state roundtrip/migrate PASS, bend-test 0,
  tune-test 0 c, bypass-check PASS, rnd-check PASS, fuzz 300/0 (worst peak 17.052 @ seed
  100059 — down from 44.5: fuzz now draws chain.mix too), check_ids 58 OK, auval
  SUCCEEDED. AU + VST3 reinstalled.

## v0.33 — 2026-09-02 — Context defaults + the tape grows up
- **DAW default = Input**: VST3/AU instances open listening to the track; standalone
  keeps Sample. Explicitly wrapper-gated so ts_cli (wrapperType_Undefined) keeps the
  Sample baseline for every gate. Sessions/presets override.
- **Tape workflow**: capacity 10 s → 60 s (~11 MB/buffer). SAVE now exports the LATEST
  complete take — no FLIP ritual (TapeBuffer tracks whether an un-flipped recording is
  newer; new unit test covers rec/flip/overwrite transitions). The TAKE display is a
  drag source: drag it into a DAW/Finder and the take rides out as a 24-bit WAV. Save
  dialog default renamed turbosynth-tape.wav → broken-take.wav (trademark leak).
- Nits from screenshot 6: "−0.0" clamped out of both readout formatter paths; RATE/OUT
  gained kHz/dB readouts (OUTPUT row now fully consistent); screen-reader titles
  disambiguated (OUTPUT MIX vs DELAY MIX, SAVE PRESET vs SAVE TAPE WAV, RANDOMIZE vs
  RANDOM CURVE) with no visible label changes. Two pre-existing bare float REQUIREs in
  test_tapesource.cpp moved onto the file's own exactlyEqual helper (killed the only
  warning).
- Gates: build clean (0 warnings), ctest **114/114**, unity null **−141.5071 dBFS**,
  param-check 320/0, preset-check 17/0, roundtrip/migrate PASS, bend 0, tune 0 c,
  bypass-check PASS, rnd-check PASS, fuzz 300/0 (worst 17.052 @ 100059), check_ids 58
  OK, auval SUCCEEDED. AU + VST3 reinstalled.

## v0.34 — 2026-09-02 — Product finishing: preset management, About, host value strings
- **Preset "..." menu**: reveal folder / overwrite selected USER preset with the current
  sound / rename / delete (moves to Trash, not a hard delete — a preset is someone's
  sound design). Factory rows disabled. PresetManager gained renameUser/deleteUser/
  overwriteUser; delete confirm uses explicit-id AlertWindow buttons (the
  MessageBoxOptions result indexing is too easy to get backwards).
- **About overlay**: click the BROKEN logo — version (from CMake via BROKEN_VERSION;
  project version bumped 0.23.0 → 0.34.0, it had gone stale), GPLv3 + source note,
  Noisehead/OFL credits, a third party/the band non-affiliation. Click/Escape closes.
- **Host automation value strings**: 34 float params now show real values with units in
  DAW lanes ("2400 Hz", "-12.0 dB", "80.0 ms", "2 st") via a Pf helper wrapping
  AudioParameterFloatAttributes. Display only — ids/ranges/defaults/skews untouched;
  0..1 blends deliberately left plain (a scaled display breaks host type-in parsing);
  banks untouched. Panel look unchanged (GUI formatters install after attachment).
- Housekeeping: remaining pre-existing bare float REQUIREs cleaned out of test_pitch/
  test_sourceengine (build is now 0-warning across all project sources).
- Gates: build clean (0 warnings), ctest **114/114**, unity null **−141.5071 dBFS**,
  param-check 320/0, preset-check 17/0, roundtrip/migrate PASS, bend 0, tune 0 c,
  bypass-check PASS, rnd-check PASS, fuzz 300/0 (worst 17.052 @ 100059), check_ids 58
  OK, auval SUCCEEDED (now reporting version 0.34.0). AU + VST3 reinstalled.

## 2026-09-21 — ZQ SFX identity unification (owner override of the v0.24 freeze; no version bump)
- **`PLUGIN_MANUFACTURER_CODE Zqsx` → `ZQSF`.** v0.24 froze the identity codes, and that
  entry stands as history. The owner has since unified every ZQ SFX plugin on one
  manufacturer code (`ZQSF`) so hosts group them as one vendor, accepting that DAW sessions
  saved against `Zqsx` need Broken re-inserted. Broken is pre-release, so the cost is local.
  `PLUGIN_CODE Brkn`, `PRODUCT_NAME "Broken"`, `BUNDLE_ID com.zqsfx.broken`, parameter IDs, the
  APVTS tree type, and the preset folder (`~/Library/Audio/Presets/ZQ SFX/Broken`) are
  unchanged. **The freeze now applies to `ZQSF` + `Brkn`.**
- Added `COMPANY_WEBSITE "https://www.zq-sfx.com"`, `COMPANY_EMAIL "connect@zq-sfx.com"`,
  `COMPANY_COPYRIGHT "Copyright (c) 2026 ZQ SFX"`.
- Project moved to `PROJECTS_Apps/JUCE/Project_TurboSynth`. First git commit made (local
  only). `.venv` is no longer kept in the tree; recreate it from `requirements.lock.txt`.
- Gates: clean Release build green; built bundles report `com.zqsfx.broken`; VST3 CID prefix
  `ABCDEF019182FAEB5A515346` (ZQSF); `auval -v aumf Brkn ZQSF` SUCCEEDED; AU + VST3
  reinstalled. ctest, fuzz, and the render harness were NOT re-run for this change (identity
  only, no source touched).

## 2026-09-21 — Editor crash fix; Broken moves onto the shared zqsfx_ui module (no version bump)
- **Fixed: the editor crashed on every open.** v0.33 added `mixKnob->setAccessibleTitle
  ("OUTPUT MIX")` 14 lines above the `make_unique` that creates `mixKnob`, so `MangleView`'s
  constructor dereferenced a null `unique_ptr` (EXC_BAD_ACCESS at 0x108 in
  `juce::Component::setTitle`). ctest, auval, fuzz, and the render gates never open the editor,
  so every gate stayed green from v0.33 on. Found by the new snapshot tool below. **Gap closed:**
  the editor is now constructed by a tool on every look-and-feel change, and pluginval
  (which opens the editor) is part of the gates listed here.
- **New gate: `ts_ui_snapshot <out.png> [scale]`** renders the editor headlessly. The grime is
  seeded and nothing animates, so identical code gives a byte-identical PNG.
- **Broken's UI core now comes from `zqsfx_ui` v0.1.0** (github.com/themightyzq/zqsfx_ui, pinned
  by tag in `plugin/CMakeLists.txt`), the ZQ SFX house UI that was lifted from this project.
  `Theme.h`, `TsLookAndFeel.h`, and `Controls.h` are now thin adapters that keep the `ts::ui`
  names. The OFL fonts moved into the module (`plugin/assets/fonts/` removed). The Noisehead
  knob strips stay here and are handed to the shared LookAndFeel through `setKnobStrips`,
  because that licence forbids redistributing the images as a standalone resource.
- **Proof of no visual change:** `ts_ui_snapshot` before vs after the migration, 1520x1024:
  **0 differing pixels of 1,556,480**, max channel delta 0.
- Two deliberate differences, neither visible in that render: keyboard focus is now a 2 px
  `accent` outline from `createFocusOutlineForComponent` (was a hand-drawn 1 px `tick` ring),
  and the bound controls publish their tooltip as accessible description and help text.
- **Owner decisions the same day:** platform-bold section titles stay and become the house
  standard, so `zqsfx_ui` v0.1.1's `Panel` draws them that way and `Block` is now just
  `using Block = zqsfx::ui::Panel`. Broken's filmstrip knobs are the house knob for every
  product; the strips are embedded here through `zqsfx_ui_add_knob_strips()` (the recipe every
  product follows), since the licence keeps them out of the public module. Re-rendered on
  v0.1.1: still **0 differing pixels** against the original baseline.
- Gates: build clean (0 project warnings), ctest **114/114**, `auval -v aumf Brkn ZQSF`
  SUCCEEDED, pluginval strictness 5 on the VST3 SUCCESS (opens the editor), bundle id
  `com.zqsfx.broken`. AU + VST3 reinstalled. Fuzz, bench, and the render harness were NOT
  re-run (no DSP or parameter code touched).

## 2026-09-21 — Broken moves to the ZQ SFX house knobs (owner decision; no version bump)
- **Visible change, by the owner's decision ("keep everything consistent"):** every knob on
  the panel is now one of the three CC0 house filmstrips embedded in `zqsfx_ui` v0.2.0, the same
  knobs every other ZQ SFX product uses. Dials 56 px and up: silver cap in a black lobed skirt
  (KnobGallery #2638). 42 px and up: black with a white pointer (#2410). Smaller: brushed silver
  cap (#2075). 128 frames, 270 degree sweep.
- **Removed the Noisehead "Analog Knob Kit 01" strips and their licence file** from the project
  (`plugin/assets/` is gone). That art is licensed for use inside plugin projects but cannot be
  redistributed from a shared library, which is why it could not become the house knob. With it
  gone, Broken carries no third-party GUI art and no credit obligation for knobs; README, PANEL.md,
  and the About box now credit the CC0 designs instead. `TsLookAndFeel` is now simply an alias
  of `zqsfx::ui::LookAndFeel`.
- The large knob's own pointer is a small dark tick that was close to unreadable on DRIVE /
  MOD / FILTER / RES. Fixed for every product in `zqsfx_ui` v0.2.1, which paints a cream
  (`pointer` token) line on that strip; Broken is pinned to v0.2.1. The as-designed strip is
  kept in the module and restoring it is a one-file copy there.
- `ts_ui_snapshot` before vs after: 73,833 of 1,556,480 pixels changed (4.74 percent), all
  inside knob dials; the result is pixel-identical to the preview the owner approved.
- Gates: build clean (0 project warnings), ctest **114/114**, `auval -v aumf Brkn ZQSF`
  SUCCEEDED, pluginval strictness 5 SUCCESS. AU + VST3 reinstalled. No DSP or parameter code
  touched, so fuzz, bench, and the render harness were not re-run.
