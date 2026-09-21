#pragma once
// The "degraded 90s rackmount sampler" LookAndFeel (v0.27).
// Visual spec of record: ClaudeDesign/design_handoff_broken_ui/README.md.
//
// - Knobs are FILMSTRIPS (Analog Knob Kit 01 by Julian Behrens / Noisehead, 128 frames
//   each, vertical strips): frame = round(pos * 127), blitted into the dial bounds.
//   Which strip a slider uses is chosen by dial size (60 -> scalloped, 42/38 -> stripe,
//   34 -> metal cap); a slider can override via the "brokenStrip" component property.
// - Every value field is an LCD: VT323 green on near-black with a soft glow.
// - Buttons: vertical gradient faces; the LEGEND turns accent on hover (the handoff's
//   ~100 ms ease is approximated by an instant swap — JUCE repaints on enter/exit).
// - Silkscreen text is Barlow Condensed; serial stamps IBM Plex Mono. All embedded from
//   BrokenAssets (OFL; licences ship in plugin/assets/fonts/).

#include <juce_gui_basics/juce_gui_basics.h>
#include "BrokenAssets.h" // fonts + knob strips (name avoids the presets' BinaryData.h)
#include "Theme.h"

namespace ts::ui
{
class TsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TsLookAndFeel()
    {
        silk       = load (BrokenAssets::BarlowCondensedMedium_ttf,   BrokenAssets::BarlowCondensedMedium_ttfSize);
        silkBold   = load (BrokenAssets::BarlowCondensedSemiBold_ttf, BrokenAssets::BarlowCondensedSemiBold_ttfSize);
        lcd        = load (BrokenAssets::VT323Regular_ttf,            BrokenAssets::VT323Regular_ttfSize);
        stamp      = load (BrokenAssets::IBMPlexMonoRegular_ttf,      BrokenAssets::IBMPlexMonoRegular_ttfSize);

        stripXL = juce::ImageCache::getFromMemory (BrokenAssets::strip_a_png, BrokenAssets::strip_a_pngSize);
        stripM  = juce::ImageCache::getFromMemory (BrokenAssets::strip_b_png, BrokenAssets::strip_b_pngSize);
        stripS  = juce::ImageCache::getFromMemory (BrokenAssets::strip_c_png, BrokenAssets::strip_c_pngSize);

        setColour (juce::ResizableWindow::backgroundColourId, colour::chassisMid);
        setColour (juce::Label::textColourId, colour::silkLabel);

        // combos are LCD fields
        setColour (juce::ComboBox::backgroundColourId, colour::lcdBg);
        setColour (juce::ComboBox::outlineColourId, colour::lcdBorder);
        setColour (juce::ComboBox::textColourId, colour::lcdText);
        setColour (juce::ComboBox::arrowColourId, colour::lcdDim);
        setColour (juce::PopupMenu::backgroundColourId, colour::lcdBg);
        setColour (juce::PopupMenu::textColourId, colour::lcdDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::lcdBorder);
        setColour (juce::PopupMenu::highlightedTextColourId, colour::lcdText);

        setColour (juce::TextButton::buttonColourId, colour::btnBot);      // gradient drawn below
        setColour (juce::TextButton::buttonOnColourId, colour::accent);
        setColour (juce::TextButton::textColourOffId, colour::btnText);
        setColour (juce::TextButton::textColourOnId, colour::accentInk);

        // slider text boxes are LCD readouts
        setColour (juce::Slider::textBoxTextColourId, colour::lcdText);
        setColour (juce::Slider::textBoxBackgroundColourId, colour::lcdBg);
        setColour (juce::Slider::textBoxOutlineColourId, colour::lcdBorder);

        setColour (juce::TooltipWindow::backgroundColourId, colour::panelBot);
        setColour (juce::TooltipWindow::textColourId, colour::btnText);
        setColour (juce::TooltipWindow::outlineColourId, colour::panelBorder);

        setColour (juce::AlertWindow::backgroundColourId, colour::panelBot);
        setColour (juce::AlertWindow::textColourId, colour::silkLabel);
        setColour (juce::TextEditor::backgroundColourId, colour::lcdBg);
        setColour (juce::TextEditor::textColourId, colour::lcdText);
        setColour (juce::TextEditor::outlineColourId, colour::lcdBorder);
    }

    // ---- typography ---------------------------------------------------------------
    juce::Font silkFont (float px, bool bold = false) const
    {
        auto t = bold ? silkBold : silk;
        return t != nullptr ? juce::Font (juce::FontOptions (t).withPointHeight (px))
                            : juce::Font (juce::FontOptions (px, juce::Font::bold));
    }
    juce::Font lcdFont (float px) const
    {
        return lcd != nullptr ? juce::Font (juce::FontOptions (lcd).withPointHeight (px))
                              : juce::Font (juce::FontOptions (px, juce::Font::plain));
    }
    juce::Font stampFont (float px) const
    {
        return stamp != nullptr ? juce::Font (juce::FontOptions (stamp).withPointHeight (px))
                                : juce::Font (juce::FontOptions (px, juce::Font::plain));
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        // keep whatever size the component chose, but in the silkscreen face
        return silkFont (l.getFont().getHeight() * 0.92f, true).withExtraKerningFactor (0.10f);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override      { return lcdFont (16.0f); }
    juce::Font getPopupMenuFont() override                     { return lcdFont (16.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return silkFont (juce::jmin (14.0f, (float) h * 0.6f), true).withExtraKerningFactor (0.12f);
    }

    // ---- filmstrip rotary ----------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float dial = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto square = bounds.withSizeKeepingCentre (dial, dial);

        if (slider.hasKeyboardFocus (true))  // focus ring hugs the dial
        {
            g.setColour (colour::tick);
            g.drawEllipse (square.reduced (0.5f), 1.0f);
        }
        const juce::Image& strip = stripFor (slider, dial);
        if (strip.isValid())
        {
            const int fw     = strip.getWidth();
            const int frames = juce::jmax (1, strip.getHeight() / fw);
            const int frame  = juce::jlimit (0, frames - 1,
                                             juce::roundToInt (sliderPos * (float) (frames - 1)));
            g.drawImage (strip, square.toNearestInt().getX(), square.toNearestInt().getY(),
                         (int) dial, (int) dial, 0, frame * fw, fw, fw);
        }
        else
        {
            // fallback if an asset ever fails to load: the old drawn pointer knob,
            // so the plugin degrades to ugly rather than to invisible
            auto r = dial * 0.5f - 2.0f;
            auto c = square.getCentre();
            g.setColour (colour::panelBot);
            g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
            const float a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * sliderPos);
            g.setColour (colour::pointer);
            g.drawLine ({ c, { c.x + (r - 3) * std::sin (a), c.y - (r - 3) * std::cos (a) } }, 2.0f);
        }
    }

    // Printed tick ring AROUND a knob (silkscreen on the panel, not on the knob).
    // Called by Knob::paint with the dial's square; kept here so ring and knob agree.
    static void drawTickRing (juce::Graphics& g, juce::Rectangle<float> dialSquare)
    {
        const auto c = dialSquare.getCentre();
        const float r0 = dialSquare.getWidth() * 0.5f + 3.0f;
        const float r1 = r0 + 4.0f;
        g.setColour (colour::tick);
        for (int i = 0; i <= 10; ++i)
        {
            // 11 ticks across the -135..+135 sweep, matching the reference render
            const float a = juce::degreesToRadians (-135.0f + 27.0f * (float) i);
            g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a),
                        c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), 1.0f);
        }
    }

    // ---- the 90s-synth phosphor screen -----------------------------------------------
    // One treatment for EVERYTHING that should read as a screen rather than hardware:
    // dropdowns, value readouts, the sample display, tuners, oscilloscope panes, meter
    // well. Recessed bezel, near-black green-lit glass, faint scanlines, a phosphor
    // wash that is brightest in the middle — per the user's brief, "the green LED screen
    // from a synth from the 1990s".
    static void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, bool scanlines = true)
    {
        // bezel: 1px hard dark rim + 1px inner shadow so the glass sits BELOW the panel
        g.setColour (juce::Colour (0xff040605));
        g.drawRect (r, 1.0f);
        auto glass = r.reduced (1.0f);
        g.setColour (colour::lcdBg);
        g.fillRect (glass);
        // phosphor wash: the tube is faintly alive even where nothing is drawn
        juce::ColourGradient wash (colour::lcdText.withAlpha (0.05f), glass.getCentreX(), glass.getCentreY(),
                                   juce::Colours::transparentBlack, glass.getX(), glass.getY(), true);
        g.setGradientFill (wash);
        g.fillRect (glass);
        if (scanlines)
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            for (float y = glass.getY() + 2.0f; y < glass.getBottom(); y += 3.0f)
                g.drawHorizontalLine ((int) y, glass.getX(), glass.getRight());
        }
        // top inner shadow (recessed) + border glow line
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillRect (glass.withHeight (2.0f));
        g.setColour (colour::lcdBorder);
        g.drawRect (glass, 1.0f);
    }

    // ---- LCD combo -----------------------------------------------------------------
    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height);
        drawScreen (g, r, height > 30); // scanlines only where the field is tall enough to carry them
        if (box.hasKeyboardFocus (true))
        {
            g.setColour (colour::tick);
            g.drawRect (r.reduced (1.0f), 1.0f);
        }
        // caret
        g.setColour (box.isEnabled() ? colour::lcdDim : colour::lcdFaint2);
        g.setFont (lcdFont (12.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"),
                    juce::Rectangle<int> (width - 18, 0, 14, height), juce::Justification::centred);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (7, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, colour::lcdText);
    }

    // Slider value boxes are Labels parented to their Slider; give them the screen glass
    // and glowing text instead of a flat fill.
    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
        {
            auto r = l.getLocalBounds().toFloat();
            drawScreen (g, r, false); // too small for scanlines
            if (! l.isBeingEdited())
                drawLcdText (g, l.getText(), l.getLocalBounds(), (float) l.getHeight() * 0.92f,
                             juce::Justification::centred);
            return;
        }
        LookAndFeel_V4::drawLabel (g, l);
    }

    // ---- gradient buttons, hover -> accent legend ------------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool, bool isDown) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool invert = (bool) b.getProperties()["invertLit"];
        const bool on = b.getToggleState() != invert; // MONO/POLY: value 0 shows lit
        if (on)
        {
            g.setColour (colour::accent);
            g.fillRect (r);
            // inset bottom shadow on active toggles, per the handoff
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (r.withTop (r.getBottom() - 2.0f));
        }
        else
        {
            g.setGradientFill (gradients::button (r, b.isEnabled()));
            g.fillRect (r);
            g.setColour (juce::Colours::white.withAlpha (b.isEnabled() ? 0.07f : 0.0f));
            g.fillRect (r.removeFromTop (1.0f));  // inset top highlight
        }
        g.setColour (colour::btnBorder);
        g.drawRect (b.getLocalBounds().toFloat(), 1.0f);
        if (b.hasKeyboardFocus (true))   // visible focus: same silk as the tick rings
        {
            g.setColour (colour::tick);
            g.drawRect (b.getLocalBounds().toFloat().reduced (1.0f), 1.0f);
        }
        if (isDown)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRect (b.getLocalBounds().toFloat());
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool isOver, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        const bool invert = (bool) b.getProperties()["invertLit"];
        const bool lit = b.getToggleState() != invert;
        const auto col = ! b.isEnabled() ? colour::silkCaption
                       : lit ? colour::accentInk
                       : isOver ? colour::accent
                                : colour::btnText;
        g.setColour (col);
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2, 0),
                    juce::Justification::centred);
    }

    // ---- LCD glow helper (tuners/displays call this for their value text) ------------
    void drawLcdText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                      float px, juce::Justification just = juce::Justification::centred,
                      juce::Colour col = colour::lcdText) const
    {
        g.setFont (lcdFont (px));
        // four-pass halo reads as phosphor bloom at these sizes without a GlowEffect's cost
        g.setColour (col.withAlpha (0.16f));
        for (auto d : { juce::Point<int> (1, 0), { -1, 0 }, { 0, 1 }, { 0, -1 } })
            g.drawText (text, area.translated (d.x, d.y), just);
        g.setColour (col);
        g.drawText (text, area, just);
    }

private:
    static juce::Typeface::Ptr load (const void* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    const juce::Image& stripFor (juce::Slider& s, float dial) const
    {
        const auto prop = s.getProperties()["brokenStrip"].toString();
        if (prop == "xl") return stripXL;
        if (prop == "m")  return stripM;
        if (prop == "s")  return stripS;
        if (dial >= 56.0f) return stripXL;   // 66 px primaries -> scalloped
        if (dial >= 42.0f) return stripM;    // 46/44 -> stripe knob
        return stripS;                       // 40 -> metal cap (PLAY/SOURCE trims)
    }

    juce::Typeface::Ptr silk, silkBold, lcd, stamp;
    juce::Image stripXL, stripM, stripS;
};
} // namespace ts::ui
