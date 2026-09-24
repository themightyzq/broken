#pragma once
// The "degraded 90s rackmount sampler" LookAndFeel.
// Visual spec of record: ClaudeDesign/design_handoff_broken_ui/README.md.
//
// Since the ZQ SFX house look was lifted from Broken, all of it now lives in the shared
// zqsfx_ui module: tokens, typography (Barlow Condensed / VT323 / IBM Plex Mono, OFL), the
// phosphor screen, LCD combos and readouts, gradient buttons with the accent hover legend, tick
// rings, the accent keyboard-focus outline, and the knobs (the module's CC0 house filmstrips:
// dials >= 56 px silver cap in a lobed skirt, >= 42 px black with a white pointer, smaller a
// brushed silver cap; a slider can override with the "zqsfxStrip" property "xl" / "m" / "s").
//
// Broken used its own licensed knob art until 2026-09-21; the owner moved it to the house
// knobs so every ZQ SFX product matches. What remains Broken's alone (grime, screws, the
// mirrored K) lives in PluginEditor.

#include <juce_gui_basics/juce_gui_basics.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "Theme.h"

namespace broken::ui
{
using BrokenLookAndFeel = zqsfx::ui::LookAndFeel;
} // namespace broken::ui
