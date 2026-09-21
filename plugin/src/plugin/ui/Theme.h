#pragma once
// Broken's theme is now the ZQ SFX house theme, and lives in the shared zqsfx_ui module
// (github.com/themightyzq/zqsfx_ui, pinned by tag in CMakeLists.txt). This header is the
// thin adapter that keeps Broken's views compiling against their original ts::ui names.
//
// The visual spec of record is unchanged: ClaudeDesign/design_handoff_broken_ui/README.md.
// To change a token or a shared control, change it in zqsfx_ui, tag a release, bump the tag
// here. Broken-only decoration (grime, screws, mirrored K, the licensed knob strips) stays
// in this project.

#include <juce_gui_basics/juce_gui_basics.h>
#include <zqsfx_ui/zqsfx_ui.h>

namespace ts::ui
{
namespace colour
{
    using namespace zqsfx::ui::colour;

    // legacy aliases from before the v0.27 rewrite; a few call sites still use them
    inline const juce::Colour bg      = chassisMid;
    inline const juce::Colour panel   = panelBot;
    inline const juce::Colour border  = ruleTitle;
    inline const juce::Colour text    = silkLabel;
    inline const juce::Colour textDim = silkCaption;
}

namespace gradients { using namespace zqsfx::ui::gradients; }
namespace geom      { using namespace zqsfx::ui::geom; }

using zqsfx::ui::styleSectionLabel;
using zqsfx::ui::styleControlLabel;

// Titled rack panel. The shared Panel: gradient face, hard border, platform-bold tracked title
// with a hairline rule (the owner confirmed the platform bold titles as the house standard).
using Block = zqsfx::ui::Panel;

// Non-interactive status LED (the TAPE lamp). The shared Led with Broken's caption.
struct Light : public zqsfx::ui::Led
{
    Light() { text = "TAPE"; }
};
} // namespace ts::ui
