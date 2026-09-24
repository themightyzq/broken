# Broken / Broken FX

Two plugins built from one codebase, inspired by the 1988 Digidesign
TurboSynth. **Broken** is the instrument: it mangles a loaded sample, an
internal oscillator/noise source, or live audio input into degraded,
industrial textures, with MIDI-triggered 6-voice mono/poly/unison
playback and a "tape" loop that bounces the current output back through
the chain for generational resampling inside the plugin. **Broken FX** is
the same mangling engine (waveshaping, AM/RM/FM modulation, a filter
stack, a resonator, a spectral inverter, delay, bit-depth reduction)
built as a plain stereo insert effect for DAWs and Soundminer's DSP rack
— no MIDI, no sample/tape workflow, always processing the live input in
true stereo (independent left/right channels, not a mono chain duplicated
to both sides).

Broken ships 17 factory presets and you can save, rename, and delete your
own; Broken FX ships one ("Init") and the rest is yours to build. AU,
VST3, and Standalone, macOS.

## Install

There are no packaged releases yet; build from source (below). The built
plugins are unsigned, so first launch needs right-click, Open, and hosts
such as Soundminer will refuse to load them until they are signed
locally. Install both bundles: `Broken.vst3` / `Broken.component` and
`Broken FX.vst3` / `Broken FX.component`.

Requires macOS 11.0 or later.

## Use

Load a sample, or feed Broken live audio, and shape it with the source,
modulator, waveshaper, filter, resonator, spectral inverter, delay, and
envelope sections in the signal chain. One-press tune lock and a
RANDOMIZE button with undo are on the panel for quick exploration. TAPE
records the current output and feeds it back through the chain for
further mangling. Presets save, rename, delete, and overwrite from the
preset bar.

Broken FX drops the source selector, PLAY, and TAPE — insert it on a
track or in Soundminer's DSP rack and it mangles whatever comes in,
independently on the left and right channels.

## Building

```bash
cd plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
```

JUCE and the shared house UI module are fetched automatically by CMake.
This builds both the `Broken` and `BrokenFX` CMake targets — AU, VST3,
and Standalone, as macOS universal binaries (Apple Silicon and Intel).
Built plugins land under `plugin/build/Broken_artefacts/` (the
instrument) and `plugin/build/BrokenFX_artefacts/` (the effect); the
build does not install them automatically, so copy the AU and VST3
bundles to `~/Library/Audio/Plug-Ins/` yourself to load them in a DAW.

## Testing

```bash
ctest --test-dir plugin/build --output-on-failure
```

For plugin-level checks, run pluginval against the built VST3s (`Broken.vst3`
and `Broken FX.vst3`) and auval against the AUs (`aumu Brkn ZQSF` and
`aufx BrFx ZQSF`).

## Licence

GPL-3.0-or-later. See LICENSE. Built with JUCE. Third-party assets (knob
artwork, embedded fonts) keep their own licences; see LICENSE for details.

ZQ SFX, https://www.zq-sfx.com, connect@zq-sfx.com.
