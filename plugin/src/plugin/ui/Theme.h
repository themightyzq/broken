#pragma once
// Broken's theme is now the ZQ SFX house theme, and lives in the shared zqsfx_ui module
// (github.com/themightyzq/zqsfx_ui, pinned by tag in CMakeLists.txt). This header is the
// thin adapter that keeps Broken's views compiling against their original broken::ui names.
//
// The visual spec of record is unchanged: ClaudeDesign/design_handoff_broken_ui/README.md.
// To change a token or a shared control, change it in zqsfx_ui, tag a release, bump the tag
// here. Broken-only decoration (grime, screws, mirrored K, the licensed knob strips) stays
// in this project.

#include <juce_gui_basics/juce_gui_basics.h>
#include <zqsfx_ui/zqsfx_ui.h>

namespace broken::ui
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
// The caption is drawn here rather than by zqsfx::ui::Led so it keeps the 9 pt on-screen
// text floor at small window sizes (BrokenLookAndFeel.h); geometry and colours are Led's.
struct Light : public zqsfx::ui::Led
{
    Light() { text = "TAPE"; }

    void paint (juce::Graphics& g) override
    {
        const auto caption = text;
        text.clear();          // the lamp only, from the shared Led
        zqsfx::ui::Led::paint (g);
        text = caption;
        if (caption.isEmpty()) return;

        auto b = getLocalBounds().toFloat();
        const float d = (float) dia;
        const float unitW = d * 2.0f + 6.0f + 34.0f;
        const float x0 = centred ? b.getCentreX() - unitW * 0.5f : b.getX();
        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.16f);
        const float s = juce::Component::getApproximateScaleFactorForComponent (this);
        const float minPt = 9.0f; // BrokenLookAndFeel::minScreenPt (Theme.h cannot include it)
        if (s > 0.01f && f.getHeightInPoints() * s < minPt)
            f = f.withPointHeight (minPt / s);
        g.setColour (on ? colour::silkLabel : colour::silkCaption);
        g.setFont (f);
        g.drawText (caption, getLocalBounds().withTrimmedLeft ((int) (x0 - b.getX()) + (int) d * 2 + 6),
                    juce::Justification::centredLeft);
    }
};
} // namespace broken::ui
