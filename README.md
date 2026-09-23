# Broken

This project is called TurboSynth internally because it recreates the
Digidesign TurboSynth (1988) the way the band used it; the shipped
plugin is named Broken. It mangles a sample or live audio input into
degraded, industrial textures: the signal runs through waveshaping,
AM/RM/FM modulation, a filter stack, a resonator, a spectral inverter,
delay, three envelopes, and 6-voice mono/poly/unison playback, with
bit-depth reduction along the way. A "tape" loop lets you bounce the
current output back through the chain for generational resampling inside
the plugin. "TurboSynth" is a trademark of a third party Technology (originally
Digidesign); the band is referenced as historical context only.
Broken is an independent recreation with no affiliation to either.

17 factory presets ship with it, and you can save, rename, and delete your
own. AU, VST3, and Standalone, macOS.

## Install

There are no packaged releases yet; build from source (below). The built
plugin is unsigned, so first launch needs right-click, Open, and hosts
such as Soundminer will refuse to load it until it is signed locally.

## Use

Load a sample, or feed Broken live audio, and shape it with the source,
modulator, waveshaper, filter, resonator, spectral inverter, delay, and
envelope sections in the signal chain. One-press tune lock and a
RANDOMIZE button with undo are on the panel for quick exploration. TAPE
records the current output and feeds it back through the chain for
further mangling. Presets save, rename, delete, and overwrite from the
preset bar.

## Building

```bash
cd plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
```

JUCE and the shared house UI module are fetched automatically by CMake.
This builds the AU, VST3, and Standalone targets as a macOS universal
binary (Apple Silicon and Intel).
Built plugins land under `plugin/build/TurboSynth_artefacts/`; the build
does not install them automatically, so copy the AU and VST3 bundles to
`~/Library/Audio/Plug-Ins/` yourself to load them in a DAW.

## Testing

```bash
ctest --test-dir plugin/build --output-on-failure
```

For plugin-level checks, run pluginval against the built VST3 and auval
against the AU.

## Licence

GPL-3.0-or-later. See LICENSE. Built with JUCE. Third-party assets (knob
artwork, embedded fonts) keep their own licences; see LICENSE for details.

ZQ SFX, https://www.zq-sfx.com, connect@zq-sfx.com.
