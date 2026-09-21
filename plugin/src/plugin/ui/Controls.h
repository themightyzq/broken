#pragma once
// Small reusable bound-control wrappers: every widget owns its APVTS attachment so the
// views (MangleView / EditView) stay a flat list of "new Widget(apvts, id, ...)" calls.
//
// These were written for Broken and are now the shared zqsfx_ui controls, unchanged in
// signature and behaviour (they additionally publish their tooltip as the accessible
// description and help text). This header keeps the ts::ui names the views use.

#include <juce_audio_processors/juce_audio_processors.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "Theme.h"
#include "TsLookAndFeel.h"

namespace ts::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

using zqsfx::ui::Knob;
using zqsfx::ui::Combo;
using zqsfx::ui::LitToggle;
using zqsfx::ui::TextToggle;
} // namespace ts::ui
