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

// Titled rack panel: gradient face, hard border, silk title with a hairline rule.
// Kept local instead of aliasing zqsfx::ui::Panel: this one draws its title in the platform
// bold face, the shared Panel draws it in Barlow Condensed (what the handoff specifies).
// Switching is a visible change to the panel, so it waits for the owner's sign-off.
// (Restored after the v0.27 Theme rewrite accidentally dropped it — Block and Light have
// always lived here so every view can use them without extra includes.)
class Block : public juce::Component
{
public:
    explicit Block (juce::String titleIn) : title (std::move (titleIn))
    {
        setInterceptsMouseClicks (false, true); // the panel is chrome; children interact
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (gradients::panel (r));
        g.fillRect (r);
        g.setColour (juce::Colours::white.withAlpha (0.04f)); // inner top highlight
        g.fillRect (r.withHeight (1.0f).translated (0.0f, 1.0f));
        g.setColour (colour::panelBorder);
        g.drawRect (r, 1.0f);

        auto head = getLocalBounds().reduced (8, 0).removeFromTop (20);
        g.setColour (colour::silkTitle);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)).withExtraKerningFactor (0.27f));
        g.drawText (title.toUpperCase(), head.translated (0, 4), juce::Justification::centredLeft);
        g.setColour (colour::ruleTitle);
        g.fillRect (juce::Rectangle<int> (8, 24, getWidth() - 16, 1));
    }

private:
    juce::String title;
};

// Non-interactive status LED (the TAPE lamp). The shared Led with Broken's caption.
struct Light : public zqsfx::ui::Led
{
    Light() { text = "TAPE"; }
};
} // namespace ts::ui
