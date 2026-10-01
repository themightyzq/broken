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
//
// Text-size floor (2026-10-01): the editor scales its whole design-size panel with one
// transform (0.65x to 2x), so a 10.6 pt caption rendered at 6.9 px on screen at the 0.65x
// floor. This subclass keeps every font it hands out at least minScreenPt points ON SCREEN:
// the size requested in design coordinates is raised to minScreenPt / scale whenever the
// component's real on-screen scale would draw it smaller. At 1x nothing changes for any
// font of 9 pt or more. The scale is read per component (getApproximateScaleFactorForComponent),
// so the unscaled pop-out sample editor, which shares this LookAndFeel, is unaffected.
// The LCD face (VT323) has a cap height of 0.56 em against Barlow Condensed's 0.70, so it
// gets its own floor, minLcdScreenPt = 11 pt: the same ~6.2 px capitals as a 9 pt label.

#include <juce_gui_basics/juce_gui_basics.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "Theme.h"

namespace broken::ui
{
class BrokenLookAndFeel : public zqsfx::ui::LookAndFeel
{
public:
    static constexpr float minScreenPt = 9.0f;
    static constexpr float minLcdScreenPt = 11.0f;

    // on-screen scale of a component (1 for a component that is not transformed)
    static float screenScale (const juce::Component* c)
    {
        if (c == nullptr) return 1.0f;
        const float s = juce::Component::getApproximateScaleFactorForComponent (c);
        return s > 0.01f ? s : 1.0f;
    }

    // design-coordinate point size that renders at >= minPt on screen
    static float floorPt (const juce::Component* c, float pt, float minPt = minScreenPt)
    {
        const float s = screenScale (c);
        return pt * s < minPt ? minPt / s : pt;
    }
    static float floorLcdPt (const juce::Component* c, float pt) { return floorPt (c, pt, minLcdScreenPt); }

    // the same floor for a font built some other way (FontOptions heights, fallbacks)
    static juce::Font floored (const juce::Component* c, const juce::Font& f)
    {
        const float pt = f.getHeightInPoints();
        const float want = floorPt (c, pt);
        return want > pt ? f.withPointHeight (want) : f;
    }

    // scale-aware versions of the base typography helpers (component = where it is drawn)
    juce::Font silkFont (const juce::Component* c, float pt, bool bold = false) const
    {
        return zqsfx::ui::LookAndFeel::silkFont (floorPt (c, pt), bold);
    }
    juce::Font lcdFont (const juce::Component* c, float pt) const
    {
        return zqsfx::ui::LookAndFeel::lcdFont (floorLcdPt (c, pt));
    }
    using zqsfx::ui::LookAndFeel::silkFont;
    using zqsfx::ui::LookAndFeel::lcdFont;

    juce::Font getLabelFont (juce::Label& l) override
    {
        // the base keeps the component's chosen size in the silkscreen face; same, floored
        return silkFont (&l, l.getFont().getHeight() * 0.92f, true).withExtraKerningFactor (0.10f);
    }

    // A label whose font the floor had to raise gets its side padding back (5 px a side by
    // default) so the larger text fits the same box instead of being cut to "SPRE...".
    juce::BorderSize<int> getLabelBorderSize (juce::Label& l) override
    {
        const float pt = l.getFont().getHeight() * 0.92f;
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) == nullptr && floorPt (&l, pt) > pt)
            return { 1, 1, 1, 1 };
        return zqsfx::ui::LookAndFeel::getLabelBorderSize (l);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override { return lcdFont (&box, 16.0f); }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        return silkFont (&b, juce::jmin (14.0f, (float) h * 0.6f), true).withExtraKerningFactor (0.12f);
    }

    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        // slider value readouts: the base's phosphor glass + LCD text, with the floored size
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
        {
            drawScreen (g, l.getLocalBounds().toFloat(), false);
            if (! l.isBeingEdited())
            {
                // the base's drawLcdText (four-pass halo, then the text), but fitted: at the
                // raised size a long readout ("65.4 Hz") is squeezed slightly instead of
                // being cut to "65.4 ..."
                g.setFont (lcdFont (&l, (float) l.getHeight() * 0.92f));
                const auto text = l.getText();
                const auto area = l.getLocalBounds();
                const auto col = zqsfx::ui::colour::lcdText;
                g.setColour (col.withAlpha (0.16f));
                for (auto d : { juce::Point<int> (1, 0), { -1, 0 }, { 0, 1 }, { 0, -1 } })
                    g.drawFittedText (text, area.translated (d.x, d.y), juce::Justification::centred, 1, 0.8f);
                g.setColour (col);
                g.drawFittedText (text, area, juce::Justification::centred, 1, 0.8f);
            }
            return;
        }
        zqsfx::ui::LookAndFeel::drawLabel (g, l);
    }
};
} // namespace broken::ui
