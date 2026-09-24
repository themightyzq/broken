Universal ZQ SFX rules (identity, real-time safety, VCS policy, signing, shared agents, shared docs) live in ../CLAUDE.md and apply here. This file only adds what is specific to Broken.

# CLAUDE.md — Broken (JUCE)

## What this is
A JUCE/C++ audio plugin (VST3 + AU + Standalone, macOS arm64); see README.md for the
one-line attribution. It does sample mangling via waveshaper, FM/AM modulator, LP stack,
resonator, spectral inverter, plus an in-plugin TAPE resample workflow. v1 scope = the
fixed chain in docs/DESIGN.md §2 plus the Stretcher (shipped v0.10 as STRETCH/FLATTEN);
Diffuser and breakpoint envelopes are out. Two plugins ship from this one codebase: the
instrument (this section, unchanged) and **Broken FX**, the same engine as a stereo
insert effect — see "Two targets" below.
Product name: **Broken** (ZQ SFX); the effect build is **Broken FX**.

## Ownership
- Claude authors and builds everything: `plugin/` (C++/CMake), `docs/`, `tests/`,
  `scripts/`, `CHANGELOG.md`, `README.md`.
- The user is the design authority and the ears: panel review, Reaper integration checks,
  and the final sound judgment against the reference material.
- Historical note: the project began as a the prototyping environment ensemble; `docs/builds/M1.md` is the
  retired path's record. The old "the prototyping environment files are user-only" clause is retired;
  DSP-NOTES.md §0's the prototyping environment conventions remain as documentation of the design's origins.

## The spec of record
`docs/DSP-NOTES.md` (math, ranges, coefficients) and `docs/PANEL.md` (controls, defaults,
info text) define the implementation. Code follows docs; if implementation forces a
deviation, the doc is updated FIRST, with the reason, then the code. `docs/RESEARCH.md`
is the evidence base for era claims. For the panel's visual design, the spec of record
remains `ClaudeDesign/design_handoff_broken_ui/README.md` (with its `BEHAVIOR.md`); that
does not change with the note below.

Broken is the baseline for the house visual style (`../docs/ZQSFX_UI_STYLE_GUIDE.md`), and
its UI core now lives in the shared `zqsfx_ui` module (`../zqsfx_ui/`, fetched by tag in
`plugin/CMakeLists.txt`). `plugin/src/plugin/ui/Theme.h`, `BrokenLookAndFeel.h`, and `Controls.h`
are thin adapters. To change a token or a shared control: change it in `zqsfx_ui`, keep its
`TokenTests` green, tag a release, bump `GIT_TAG` here, then prove the panel with
`broken_ui_snapshot` before and after. Broken-only decoration (grime, screws, mirrored K, the
licensed knob strips) stays in this project. The design spec of record above still governs.

## Conventions
- Build: CMake out-of-source in `plugin/build/`; JUCE 8 pinned via FetchContent.
- Pure DSP in `plugin/src/dsp/` stays JUCE-free (unit-testable); JUCE types only in
  `src/plugin/` and `src/cli/`.
- Presets: JSON APVTS state in `plugin/snapshots/`, named `NN-technique-name` — never
  song names.
- Renders: `tests/renders/<fixture>__<snapshot>__vNN.wav`, produced by `broken_cli` at
  48 kHz/24-bit (parity runs at 44.1 k).
- Versioning: git, local only, no remote (first repository created 2026-09-21, branch
  `main`) — see ../CLAUDE.md section 3. A **milestone** = a
  P-step passing its gates; tag/note milestones in CHANGELOG.
- No oversampling in the signal chain by default — aliasing is era-correct and deliberate.
- Python scripts (`scripts/make_fixtures.py`, `scripts/analyze.py`) run in a `.venv` that is
  not kept in the tree. Recreate it with `python3 -m venv .venv && .venv/bin/pip install -r
  requirements.lock.txt` before using `.venv/bin/python` per README.md.

## Definition of verify (per milestone)
1. `cmake --build` clean (no new warnings in project code).
2. `ctest` green (Catch2 unit tests for the touched DSP classes).
3. `broken_cli` renders of the milestone's TEST-PLAN fixtures analyzed by
   `scripts/analyze.py` with the matching expectations profile — numeric PASS, run by
   Claude directly (never trusted from a subagent's report).
4. For anything touching the editor or look-and-feel: `broken_ui_snapshot` renders before and
   after (pixel diff reported), and pluginval on the VST3 (it opens the editor; the unit and
   render gates do not, which is how a crash-on-open once shipped green).
5. CHANGELOG entry with the numbers.

## Definition of done (per feature)
Design entry current → implemented → verified as above → user-facing behavior confirmed
by the user where it's perceptual (panel, feel, sound) → CHANGELOG written.

## Standing principles (adopted)
Verify your own work — don't trust that it worked. Investigate before answering.
Root-cause fixes only. Numbers, not adjectives. Mark conventions verified vs. believed.
Stay in scope; note adjacent issues for follow-up. Delegated diffs get top-tier review;
twice-failed delegations pull up to the top tier.

## Suite routing
- 03 (feature): code + unit tests + CLI render gates. 04 (bug/RCA): red test = failing
  render analysis or unit test. 05/06 (QA/remediation): fixture edge cases + user manual
  attack list. 10/11 (end-user/design review): panel screenshots + Reaper session — the
  panel is the product. 12 (code review): normal code review of `plugin/`. 13
  (dependencies): JUCE version currency + pin policy. 14 (platform parity): 48 k vs
  44.1 k renders, VST3 vs AU vs Standalone, Reaper vs standalone. 16/17/18 as written.

## Two targets: Broken and Broken FX
One codebase (`${BROKEN_PLUGIN_SOURCES}` in `plugin/CMakeLists.txt`) builds two CMake
plugin targets: `Broken` (the instrument, `PLUGIN_CODE Brkn`, `IS_SYNTH FALSE` but
`NEEDS_MIDI_INPUT TRUE` — MIDI-triggered) and `BrokenFX` (the insert effect,
`PLUGIN_CODE BrFx`, no MIDI, `com.zqsfx.brokenfx`). `BrokenFX` is compiled with
`BROKEN_FX=1`; every other target defines it `0` so `#if BROKEN_FX` is never an
undefined-macro `-Wundef` risk. Everything specific to the FX build lives behind that
macro in `src/plugin/PluginProcessor.{h,cpp}`, `Params.h`, `PresetManager.h`,
`PluginEditor.{h,cpp}`, `MangleView.h`, and `EditView.h` — the instrument path (macro
undefined/0) is byte-identical to before the split.

What `BROKEN_FX` changes: `acceptsMidi()` false; the APVTS state type is `"BrokenFX"`
(not `"Broken"`, so the two products' saved states can never cross-load, and the
instrument's legacy `"TurboSynth"` tag migration is instrument-only); `gatherParams()`
forces the engine's source to Input every block regardless of what the (hidden, still
technically automatable) `source.mode` parameter holds; `Params.h` gives the FX build
different DEFAULT VALUES only (never different ids/ranges) — `source.mode` defaults to
Input, `ws.drive` defaults to 0 dB (was 12 dB, the instrument's value and the known
cause of a fresh FX instance being too loud); `PresetManager` points at
`~/Library/Audio/Presets/ZQ SFX/Broken FX` and its own `BrokenFXPresets` binary-data
target (`snapshots/fx/`, currently just "00 Init"); the editor hides PLAY/TAPE
(MangleView) and ENVELOPES/OSCILLATOR (EditView) and uses its own, narrower
`designW`/`designH`.

**True stereo, two engines.** The instrument is mono end to end: one `dsp::Engine`
averages every input channel into `monoIn` and duplicates `monoOut` to every output
channel. The FX build instead holds a second `dsp::Engine` (`engineR`, alongside the
existing `engine` which stays the L/primary channel for the tape/tuner/playhead
accessors), prepared and given the identical `EngineParams` every block. Each channel's
raw input feeds its own engine (channel 1 falls back to channel 0 for a mono input bus);
their outputs write independently to output channels 0 and 1 (a mono output bus
averages the two). Because both engines are constructed identically, parameterised
identically, and never see a note event, they stay sample-coherent: a mono input
produces bit-identical L/R output, and silence on one channel's input stays silence on
that channel's output regardless of what the other channel is doing. Verify with
`broken_fx_check` (console-app gate, `ctest` target) and `broken_fx_ui_snapshot`
(look-and-feel regression gate, same pattern as `broken_ui_snapshot`).

## Tiered-model note (from user-global CLAUDE.md)
Architecture, class contracts, Chain/Voice/Tape integration, CMake, and gate reviews stay
top-tier. Sonnet subagents implement well-specified DSP classes/tests, analyzer
extensions, and GUI from PANEL.md — specs must be self-contained with file whitelists and
verification commands.
