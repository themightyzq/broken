# Project TurboSynth

A JUCE audio plugin (VST3 / AU / Standalone, macOS) recreating Digidesign TurboSynth (1988) as the band used it — sample mangling through waveshaping, modulation, and generational resampling. Product name: **Broken**, by ZQ SFX. "TurboSynth" here refers to the original Digidesign hardware, an a third party trademark; this is an independent recreation, not affiliated with a third party or the band.

## Status

v0.2 — design docs, verified test harness, JUCE build in progress. See [CHANGELOG.md](CHANGELOG.md). (The project began as a the prototyping environment ensemble; [docs/builds/M1.md](docs/builds/M1.md) is that retired path's record.)

## Layout

```
plugin/              JUCE/CMake project: plugin targets, headless render CLI, unit tests
  src/dsp/           pure C++ DSP (JUCE-free, unit-tested)
  src/plugin/        processor + editor (the MANGLE panel)
  src/cli/           ts_cli — renders fixtures through the real engine, headless
  snapshots/         JSON presets, named by technique
docs/                DESIGN.md DSP-NOTES.md PANEL.md PITFALLS.md RESEARCH.md
tests/
  TEST-PLAN.md       render matrix + manual checklist
  fixtures/          script-generated test signals (never hand-made)
  renders/           ts_cli renders of fixtures through the engine
  results/           analyze.py output (JSON + plots)
scripts/             make_fixtures.py  analyze.py
```

## The verification loop

Fixtures are generated deterministically by `scripts/make_fixtures.py`; `ts_cli` renders each fixture through the actual plugin DSP at named snapshots into `tests/renders/` (naming: `<fixture>__<snapshot>__vNN.wav`); `scripts/analyze.py` compares render to fixture and reports numeric pass/fail plus a plot per render. Every DSP claim is a hypothesis until a render confirms it.

## Running the scripts

```bash
python3 scripts/make_fixtures.py
```

```bash
python3 scripts/analyze.py selftest
```

If a `.venv/` exists at the project root, use `.venv/bin/python` instead of `python3`.

## Ground rules

- Claude authors code, docs, tests, and scripts; the human is design authority and final judge of sound and panel. See CLAUDE.md for the full contract.
- Versioning: Diversion for Desktop, continuously; milestones noted in CHANGELOG.
- Design decisions trace to primary sources collected in [docs/RESEARCH.md](docs/RESEARCH.md).

## Sources

Sound On Sound Dec 1988; Music Technology Sep 1988; Keyboard Magazine March 1994.


## License

Broken is free software released under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE). Copyright © 2026 ZQ SFX LLC. Built with [JUCE](https://juce.com), used
under its GPLv3 option. "TurboSynth" is a trademark of a third party Technology; the band is
referenced as historical context only. No affiliation with either.

Third-party assets, each under its own licence (not GPL):
- **Knob artwork:** "Analog Knob Kit 01" by **Julian Behrens** (Noisehead /
  [vst-design.com](https://www.vst-design.com)) — used and modified with permission of its
  licence, which requires this credit for open-source use and forbids reselling or
  redistributing the images as standalone design resources. Licence text:
  `plugin/assets/knobs/LICENSE-Noisehead-KnobKit.txt`.
- **Fonts:** Barlow Condensed, VT323, IBM Plex Mono — SIL Open Font License, texts in
  `plugin/assets/fonts/`.
