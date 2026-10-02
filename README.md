# Broken / Broken FX

Two plugins built from one codebase, inspired by the 1988 Digidesign
TurboSynth. **Broken** is the instrument: it mangles a loaded sample, an
internal oscillator/noise source, or live audio input into degraded,
industrial textures, with MIDI-triggered 6-voice mono/poly/unison
playback and a "tape" loop that bounces the current output back through
the chain for generational resampling inside the plugin. **Broken FX** is
the same mangling engine (waveshaping, AM/RM/FM modulation, a filter
stack, a resonator, a spectral inverter, delay, bit-depth reduction)
built as a plain stereo insert effect for DAWs and Soundminer's DSP rack:
no MIDI and no playable source selector, always processing the live
input in true stereo (independent left/right channels, not a mono chain
duplicated to both sides). It still keeps a sample slot for modulation
and curve material (drop a file to feed the Sample modulator source and
the waveshaper's FROM SAMPLE), its own oscillator for the Table modulator
source, and TAPE for capturing and reprocessing its own output.

Broken ships 17 factory presets and Broken FX ships 15; both let you
save, rename, and delete your own. AU, VST3, and Standalone, macOS.

## Install

Binary releases are available for download from the
[v0.38.0 release](https://github.com/themightyzq/broken/releases/tag/v0.38.0)
(Broken-macOS-VST3.zip, Broken-macOS-AU-Standalone.zip, BrokenFX-macOS-VST3.zip,
BrokenFX-macOS-AU-Standalone.zip, plus Linux and Windows VST3). The built plugins are
unsigned, so a standalone app's first launch needs right-click, Open, and some hosts may
refuse the plugins until they are signed locally. Install both bundles: `Broken.vst3` /
`Broken.component` and `Broken FX.vst3` / `Broken FX.component`.

Alternatively, build from source (below).

Requires macOS 11.0 or later.

## Use

A new instance of Broken opens on white noise (the Noise source with Amp
and Phase noise at 100 %), so a note plays without loading anything.
Saved sessions and presets keep their own source.

Load a sample, or feed Broken live audio, and shape it with the source,
modulator, waveshaper, filter, resonator, spectral inverter, delay, and
envelope sections in the signal chain. One-press tune lock and a
RANDOMIZE button with undo are on the panel for quick exploration. TAPE
records the current output and feeds it back through the chain for
further mangling. Presets save, rename, delete, and overwrite from the
preset bar. User presets are stored in ~/Library/Audio/Presets/ZQ SFX/Broken
on macOS, %APPDATA%/ZQ SFX/Broken on Windows and ~/.config/ZQ SFX/Broken on
Linux (Broken FX: a "Broken FX" folder in the same place).

Broken FX keeps its source fixed to the live input, so it drops the
source selector and PLAY, but insert it on a track or in Soundminer's DSP
rack and it mangles whatever comes in, independently on the left and
right channels. Drop a sample to feed the Sample modulator source and
FROM SAMPLE; TAPE records the effect's own stereo output and feeds it
back in for further mangling.

Broken FX reports a fixed latency to the host (about 50 ms) for its input
stage. PITCH on the live input adds a further delay that the host does not
compensate: it varies continuously as the pitch shifter runs, averaging about
13 ms at +/-1 semitone, 35-60 ms at an octave and up to about 760 ms at the
+48 semitone extreme. At PITCH 0 it adds nothing.

## Building

```bash
cd plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
```

JUCE and the shared house UI module are fetched automatically by CMake.
This builds both the `Broken` and `BrokenFX` CMake targets, AU, VST3,
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

GPL-3.0-or-later. See LICENSE. Built with JUCE.

Fonts: Barlow Condensed, VT323 and IBM Plex Mono, embedded through the zqsfx_ui
module, are licensed under the SIL Open Font License 1.1. The licence texts are in
licenses/fonts/. Knob artwork: CC0 filmstrips from the g200kg KnobGallery, shipped
with zqsfx_ui.

ZQ SFX, https://www.zq-sfx.com, connect@zq-sfx.com.
