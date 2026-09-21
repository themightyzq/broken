#pragma once
// Small reusable bound-control wrappers: every widget owns its APVTS attachment so the
// views (MangleView / EditView) stay a flat list of "new Widget(apvts, id, ...)" calls.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "TsLookAndFeel.h"

namespace ts::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

// Rotary knob with an optional title label above it. Big knobs (the five "moves") get a
// TextBoxBelow value readout per spec; small knobs get no textbox at all (tooltip only).
class Knob : public juce::Component
{
public:
    // readout/units are additive: default (false, {}) preserves prior behaviour for every
    // existing call site, including the big-knob path (which never consults them).
    Knob (APVTS& apvts, const juce::String& paramId, const juce::String& labelText,
          const juce::String& tooltip, bool big, bool readout = false,
          const juce::String& units = {},
          std::function<juce::String (double)> fmt = nullptr)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        if (big)
        {
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
            slider.setNumDecimalPlacesToDisplay (1); // "20000.0", not "20000.0..." or 7-decimal noise
        }
        else
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);

        if (labelText.isNotEmpty())
        {
            title.setText (labelText, juce::dontSendNotification);
            styleControlLabel (title);
            addAndMakeVisible (title);
        }

        // screen readers get the silkscreen name, not an anonymous slider
        slider.setTitle (labelText.isNotEmpty() ? labelText : paramId);
        attachment = std::make_unique<APVTS::SliderAttachment> (apvts, paramId, slider);
        if (big)
        {
            // the attachment installs the parameter's own text conversion (7-decimal
            // noise); replace it AFTER attaching so the readout stays one decimal
            slider.textFromValueFunction = [] (double v)
            { if (std::abs (v) < 0.05) v = 0.0; return juce::String (v, 1); }; // no "-0.0"
            slider.updateText();
        }
        else if (readout)
        {
            // small-knob numeric readout (e.g. XFADE "25 ms") — installed after the
            // attachment for the same reason as the big-knob path above.
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
            if (fmt != nullptr) // custom formatter, e.g. 0..1 params shown as %
                slider.textFromValueFunction = std::move (fmt);
            else
                slider.textFromValueFunction = [units] (double v)
                { if (std::abs (v) < 0.5) v = 0.0; return juce::String (v, 0) + " " + units; }; // no "-0"
            slider.updateText();
        }
    }

    // screen-reader name override for panels where two knobs share a silk label
    void setAccessibleTitle (const juce::String& t) { slider.setTitle (t); }

    void setActive (bool active)
    {
        setEnabled (active);
        // dim the CONTROL to spec, but keep the label half-readable: a new user must be
        // able to learn that POS/LEN/IN TRIM exist even when the mode disables them
        slider.setAlpha (active ? 1.0f : geom::dimAlpha);
        title.setAlpha (active ? 1.0f : 0.55f);
        setAlpha (1.0f);
        repaint(); // the tick ring in paint() dims with the slider
    }

    // The silkscreen tick ring is printed on the PANEL around the knob, not on the knob
    // image itself (the filmstrip already carries its own pointer) — so it lives here in
    // the component's paint, using the slider's real rotary bounds from the LookAndFeel.
    void paint (juce::Graphics& g) override
    {
        auto layout = slider.getLookAndFeel().getSliderLayout (slider);
        auto r = layout.sliderBounds.toFloat();
        const float dial = juce::jmin (r.getWidth(), r.getHeight());
        auto square = r.withSizeKeepingCentre (dial, dial).translated ((float) slider.getX(), (float) slider.getY());
        juce::Graphics::ScopedSaveState ss (g);
        g.setOpacity (slider.getAlpha());
        TsLookAndFeel::drawTickRing (g, square);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (title.getText().isNotEmpty())
            title.setBounds (b.removeFromTop (13));
        slider.setBounds (b);
    }

    juce::Slider slider;

private:
    juce::Label title;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Labelled ComboBox. Item order MUST match the StringArray order in Params.h (ids 1..N).
class Combo : public juce::Component
{
public:
    Combo (APVTS& apvts, const juce::String& paramId, const juce::StringArray& items,
           const juce::String& labelText, const juce::String& tooltip)
    {
        for (int i = 0; i < items.size(); ++i)
            box.addItem (items[i], i + 1);
        box.setTooltip (tooltip);
        addAndMakeVisible (box);

        if (labelText.isNotEmpty())
        {
            title.setText (labelText, juce::dontSendNotification);
            styleControlLabel (title);
            addAndMakeVisible (title);
        }

        box.setTitle (labelText.isNotEmpty() ? labelText : paramId);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, paramId, box);
    }

    void setActive (bool active)
    {
        setEnabled (active);
        box.setAlpha (active ? 1.0f : geom::dimAlpha);
        title.setAlpha (active ? 1.0f : 0.55f);   // label stays learnable (see Knob)
        setAlpha (1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (title.getText().isNotEmpty())
            title.setBounds (b.removeFromTop (13));
        box.setBounds (b);
    }

    juce::ComboBox box;

private:
    juce::Label title;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

// Small square light: filled accent when the bound bool param is on, dark outline when off.
// Used for the six module on/off (= hard-bypass) switches.
class LitToggle : public juce::Component
{
public:
    LitToggle (APVTS& apvts, const juce::String& paramId, const juce::String& tooltip)
    {
        button.setClickingTogglesState (true);
        button.setTooltip (tooltip);
        button.setTitle (tooltip.upToFirstOccurrenceOf (".", false, false)); // "Waveshaper on/off (hard bypass)"
        addAndMakeVisible (button);
        attachment = std::make_unique<APVTS::ButtonAttachment> (apvts, paramId, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

private:
    struct LedButton : public juce::Button
    {
        LedButton() : juce::Button (juce::String()) {}
        void paintButton (juce::Graphics& g, bool, bool) override
        {
            // round rack LED per the handoff: accent + glow when the module is active,
            // dark well with a faint rim when hard-bypassed
            auto b = getLocalBounds().toFloat();
            const float d = juce::jmin ((float) geom::lightSize, juce::jmin (b.getWidth(), b.getHeight()) - 2.0f);
            auto led = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
            if (getToggleState())
            {
                g.setColour (colour::accent.withAlpha (0.35f));
                g.fillEllipse (led.expanded (3.5f));
                g.setColour (colour::accent);
            }
            else
                g.setColour (colour::ledOffFill);
            g.fillEllipse (led);
            g.setColour (getToggleState() ? juce::Colours::black.withAlpha (0.7f) : colour::ledOffRim);
            g.drawEllipse (led, 1.0f);
        }
    } button;

    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// Text toggle button (MONO/POLY, UNISON, EXT, retrigger, delay polarity, ...).
// invertLit=true means "lit" corresponds to toggle-state OFF (value 0) rather than ON —
// used for MONO/POLY where PANEL.md calls for "0=Mono shown as MONO lit".
class TextToggle : public juce::Component
{
public:
    TextToggle (APVTS& apvts, const juce::String& paramId, const juce::String& text,
                const juce::String& tooltip, bool invertLit = false,
                const juce::String& onText = {})
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        button.setTooltip (tooltip);
        if (onText.isNotEmpty()) // state-labeled toggle: shows what it currently IS
        {
            button.onStateChange = [this, text, onText]
            { button.setButtonText (button.getToggleState() ? onText : text); };
            button.onStateChange();
        }

        if (invertLit)
            button.getProperties().set ("invertLit", true); // LnF flips lit/unlit (MONO=0 shows lit)

        addAndMakeVisible (button);
        attachment = std::make_unique<APVTS::ButtonAttachment> (apvts, paramId, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::TextButton button;

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};
} // namespace ts::ui
