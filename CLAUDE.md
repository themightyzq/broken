Universal ZQ SFX rules (identity, real-time safety, VCS policy, signing, shared agents, shared docs) live in ../CLAUDE.md and apply here. This file only adds what is specific to Broken.

# CLAUDE.md — Project TurboSynth (JUCE)

## What this is
A JUCE/C++ audio plugin (VST3 + AU + Standalone, macOS arm64) replicating Digidesign
TurboSynth as the band used it: sample mangling via waveshaper, FM/AM modulator, LP stack,
resonator, spectral inverter, plus an in-plugin TAPE resample workflow. v1 scope = the
fixed chain in docs/DESIGN.md §2 plus the Stretcher (shipped v0.10 as STRETCH/FLATTEN);
Diffuser and breakpoint envelopes are out.
Product name: **Broken** (ZQ SFX). "TurboSynth" refers to the original a third party hardware only; internal identifiers keep the working name.

## Ownership
- Claude authors and builds everything: `plugin/` (C++/CMake), `docs/`, `tests/`,
  `scripts/`, `CHANGELOG.md`, `README.md`.
- The user is the design authority and the ears: panel review, Reaper integration checks,
  and the final sound judgment against reference the band material.
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

Broken is now the baseline for the house visual style
(`../docs/ZQSFX_UI_STYLE_GUIDE.md`, draft): changes to `plugin/src/plugin/ui/Theme.h`,
`TsLookAndFeel.h`, or `Controls.h` will feed the future shared design system other
projects migrate onto. Keep making those changes against the design spec of record above.

## Conventions
- Build: CMake out-of-source in `plugin/build/`; JUCE 8 pinned via FetchContent.
- Pure DSP in `plugin/src/dsp/` stays JUCE-free (unit-testable); JUCE types only in
  `src/plugin/` and `src/cli/`.
- Presets: JSON APVTS state in `plugin/snapshots/`, named `NN-technique-name` — never
  song names.
- Renders: `tests/renders/<fixture>__<snapshot>__vNN.wav`, produced by `ts_cli` at
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
3. `ts_cli` renders of the milestone's TEST-PLAN fixtures analyzed by
   `scripts/analyze.py` with the matching expectations profile — numeric PASS, run by
   Claude directly (never trusted from a subagent's report).
4. CHANGELOG entry with the numbers.

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

## Tiered-model note (from user-global CLAUDE.md)
Architecture, class contracts, Chain/Voice/Tape integration, CMake, and gate reviews stay
top-tier. Sonnet subagents implement well-specified DSP classes/tests, analyzer
extensions, and GUI from PANEL.md — specs must be self-contained with file whitelists and
verification commands.
