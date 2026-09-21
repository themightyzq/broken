#pragma once
// Design tokens for the Broken "degraded 90s rackmount sampler" GUI (v0.27).
// SPEC OF RECORD: ClaudeDesign/design_handoff_broken_ui/README.md — values here are
// transcribed from its token table and must not drift from it. One accent only
// (#E8622A), reserved for state: LEDs, active toggles, hover text, REC. LCD green is
// a DISPLAY colour, not an accent.
//
// Legacy note: the pre-v0.27 flat palette (bg #111213 / accent #ff5a1f) is gone; token
// NAMES are role-based so call sites read as intent, not as hex.

#include <juce_graphics/juce_graphics.h>

namespace ts::ui
{
namespace colour
{
    // chassis + panels (gradients built via helpers below; these are the stops)
    inline const juce::Colour chassisTop    { 0xff0e1011 };
    inline const juce::Colour chassisMid    { 0xff0a0b0c };   // at 60 %
    inline const juce::Colour chassisBot    { 0xff0d0f10 };
    inline const juce::Colour panelTop      { 0xff1d2022 };
    inline const juce::Colour panelBot      { 0xff121416 };
    inline const juce::Colour panelBorder   { 0xff060707 };

    // silkscreen text
    inline const juce::Colour silkTitle     { 0xff66b7ae };   // section titles
    inline const juce::Colour silkLabel     { 0xff8fb3ae };   // knob/control labels
    inline const juce::Colour silkCaption   { 0xff7a9a94 };   // sub-captions, disabled text (raised from #62807C: 10px captions sat under WCAG 4.5:1)

    // hairlines
    inline const juce::Colour ruleTitle     { 0xff22272a };   // under panel titles
    inline const juce::Colour ruleInner     { 0xff1b1f21 };   // inner dividers

    // LCD / VFD displays (readouts, dropdowns, waveform screens, tuners, meter well)
    inline const juce::Colour lcdBg         { 0xff0c150e };
    inline const juce::Colour lcdBorder     { 0xff1f2b21 };
    inline const juce::Colour lcdText       { 0xff8fe89a };   // primary values (glow drawn by LnF)
    inline const juce::Colour lcdDim        { 0xff63b871 };   // secondary (PRESET, carets, TAKE)
    inline const juce::Colour lcdFaint      { 0xff4e8a5a };   // hints, scale numerals
    inline const juce::Colour lcdFaint2     { 0xff3f7a4a };   // needles, ghost strokes
    inline const juce::Colour lcdScreenDark { 0xff0a120c };   // waveform stripes, meter well
    inline const juce::Colour lcdGlow       { 0x805adc6e };   // 0 0 7px glow tint (drawn soft)

    // buttons
    inline const juce::Colour btnTop        { 0xff212326 };
    inline const juce::Colour btnBot        { 0xff141618 };
    inline const juce::Colour btnBorder     { 0xff0a0b0c };
    inline const juce::Colour btnText       { 0xffc9d4d2 };
    inline const juce::Colour btnText2      { 0xffb9c7c4 };
    inline const juce::Colour btnDisTop     { 0xff17191b };
    inline const juce::Colour btnDisBot     { 0xff101214 };

    // the ONE accent + friends
    inline const juce::Colour accent        { 0xffe8622a };   // LEDs, active toggles, hover text
    inline const juce::Colour accentInk     { 0xff140d07 };   // text ON an accent fill
    inline const juce::Colour ledOffFill    { 0xff151719 };
    inline const juce::Colour ledOffRim     { 0xff3d4448 }; // bright enough that an OFF lamp is still findable
    inline const juce::Colour recDot        { 0xff5a2018 };   // REC's dark-red dot, unlit

    // knob furniture (knobs themselves are filmstrip images)
    inline const juce::Colour tick          { 0x737abab2 };   // rgba(122,186,178,.45) tick rings + meter scale
    inline const juce::Colour pointer       { 0xffded6c2 };   // kept for any drawn pointer fallback

    // meter fill stops (bottom -> top)
    inline const juce::Colour meterLo       { 0xff2f8f45 };
    inline const juce::Colour meterMid      { 0xff6fd57e };   // at 70 %
    inline const juce::Colour meterHi       { 0xff8fe89a };
    inline const juce::Colour meterHot      { 0xffd9a441 };   // above -6 dB shifts amber
    inline const juce::Colour meterClip     { 0xffdd4433 };

    inline const juce::Colour logoBright    { 0xffe2e5e8 };
    inline const juce::Colour footer        { 0xff45524f };
    inline const juce::Colour warn          { 0xffdd4433 };   // missing-file name (kept from v0.26)

    // ---- compatibility aliases -------------------------------------------------------
    // The whole component tree was written against these names; they now map onto the
    // rack palette so every file keeps compiling while call sites migrate to the precise
    // tokens above. New code should use the specific tokens.
    inline const juce::Colour bg        = chassisMid;
    inline const juce::Colour panel     = panelBot;
    inline const juce::Colour border    = ruleTitle;
    inline const juce::Colour text      = silkLabel;
    inline const juce::Colour textDim   = silkCaption;
    inline const juce::Colour accentDim { 0xff8a4a28 };       // dimmed accent (blink-off)
}

namespace gradients
{
    inline juce::ColourGradient chassis (juce::Rectangle<float> r)
    {
        juce::ColourGradient g (colour::chassisTop, r.getTopLeft(), colour::chassisBot, r.getBottomLeft(), false);
        g.addColour (0.6, colour::chassisMid);
        return g;
    }
    inline juce::ColourGradient panel (juce::Rectangle<float> r)
    {
        return { colour::panelTop, r.getTopLeft(), colour::panelBot, r.getBottomLeft(), false };
    }
    inline juce::ColourGradient button (juce::Rectangle<float> r, bool enabled)
    {
        return { enabled ? colour::btnTop : colour::btnDisTop, r.getTopLeft(),
                 enabled ? colour::btnBot : colour::btnDisBot, r.getBottomLeft(), false };
    }
}

namespace geom
{
    // knob dial sizes per the handoff (filmstrips scale to these)
    constexpr int knobXL    = 66;   // MANGLE primary
    constexpr int knobL     = 46;
    constexpr int knobM     = 44;
    constexpr int knobS     = 40;
    constexpr int lightSize = 10;   // round LEDs, 9-10 px
    constexpr int headerH   = 16;   // Block title strip (legacy layout metric)
    constexpr float dimAlpha = 0.30f; // inapplicable controls per the handoff
}

// Section title: Barlow Condensed 600 13px ls 3.5, uppercase — the LnF supplies the
// typeface; these helpers only set colour/justification so they stay usable before the
// font loads (fallback face).
inline void styleSectionLabel (juce::Label& l)
{
    l.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    l.setColour (juce::Label::textColourId, colour::silkTitle);
    l.setJustificationType (juce::Justification::centredLeft);
}

// Titled rack panel: gradient face, hard border, silk title with a hairline rule.
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

// Non-interactive status LED (the TAPE lamp): round, accent when on, dark when off.
struct Light : public juce::Component, public juce::SettableTooltipClient
{
    bool on = false;
    int  dia = geom::lightSize; // TAPE lamp uses a bigger LED (v0.32)
    bool centred = false;       // centre the LED+text unit instead of left-hugging
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float d = (float) dia;
        const float unitW = d * 2.0f + 6.0f + 34.0f; // LED offset + label estimate
        const float x0 = centred ? b.getCentreX() - unitW * 0.5f : b.getX();
        auto led = juce::Rectangle<float> (d, d).withCentre ({ x0 + d, b.getCentreY() });
        if (on)
        {
            g.setColour (colour::accent.withAlpha (0.35f));   // soft glow halo
            g.fillEllipse (led.expanded (3.0f));
            g.setColour (colour::accent);
        }
        else
            g.setColour (colour::ledOffFill);
        g.fillEllipse (led);
        g.setColour (on ? juce::Colours::black.withAlpha (0.7f) : colour::ledOffRim);
        g.drawEllipse (led, 1.0f);

        g.setColour (on ? colour::silkLabel : colour::silkCaption);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.16f));
        g.drawText ("TAPE", getLocalBounds().withTrimmedLeft ((int) (x0 - b.getX()) + (int) d * 2 + 6),
                    juce::Justification::centredLeft);
    }
};

inline void styleControlLabel (juce::Label& l)
{
    l.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    l.setColour (juce::Label::textColourId, colour::silkLabel);
    l.setJustificationType (juce::Justification::centred);
}
} // namespace ts::ui
