#pragma once
// The "degraded 90s rackmount sampler" LookAndFeel.
// Visual spec of record: ClaudeDesign/design_handoff_broken_ui/README.md.
//
// Since the ZQ SFX house look was lifted from Broken, everything generic lives in the shared
// zqsfx_ui module: tokens, typography (Barlow Condensed / VT323 / IBM Plex Mono, OFL, embedded
// by the module), the phosphor screen, LCD combos and readouts, gradient buttons with the
// accent hover legend, tick rings, and the accent keyboard-focus outline.
//
// What stays here is the one thing that is Broken's alone: the FILMSTRIP knobs (Analog Knob
// Kit 01 by Julian Behrens / Noisehead, 128 frames each, vertical strips; licence ships in
// plugin/assets/knobs/). That art is licensed for Broken, so it is handed to the shared
// LookAndFeel here rather than shipped in the module, whose default knob is vector.
// Strip choice by dial size: >= 56 px scalloped, >= 42 px stripe, else metal cap. A slider can
// override with the "zqsfxStrip" component property ("xl" / "m" / "s").

#include <juce_gui_basics/juce_gui_basics.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "BrokenAssets.h" // knob strips only (name avoids the presets' BinaryData.h)
#include "Theme.h"

namespace ts::ui
{
class TsLookAndFeel : public zqsfx::ui::LookAndFeel
{
public:
    TsLookAndFeel()
    {
        setKnobStrips (juce::ImageCache::getFromMemory (BrokenAssets::strip_a_png, BrokenAssets::strip_a_pngSize),
                       juce::ImageCache::getFromMemory (BrokenAssets::strip_b_png, BrokenAssets::strip_b_pngSize),
                       juce::ImageCache::getFromMemory (BrokenAssets::strip_c_png, BrokenAssets::strip_c_pngSize));
    }
};
} // namespace ts::ui
