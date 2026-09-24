#pragma once
// Contents of the source pop-out window. The window used to host the SampleEditor
// directly, so in Osc mode it showed the loaded sample while you heard an oscillator —
// looking at one thing, hearing another. This panel swaps in the view that matches
// source.mode, and retitles the window to say which one you are looking at.
//
//   Sample / Cycle / Tape -> SampleEditor  (region, loop, crossfade)
//   Osc                   -> OscEditor     (the one-cycle shape; drawable in Draw mode)
//   Noise / Input         -> a short explanation; neither has an editable waveform
//
// docs/PANEL.md.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "OscEditor.h"
#include "SampleEditor.h"
#include "Theme.h"

namespace broken::ui
{
class SourceEditorPanel : public juce::Component, private juce::Timer
{
public:
    explicit SourceEditorPanel (BrokenProcessor& p)
        : sampleEditor (p), oscEditor (p)
    {
        modeRaw    = p.apvts.getRawParameterValue ("source.mode");
        oscModeRaw = p.apvts.getRawParameterValue ("osc.mode");

        addChildComponent (sampleEditor);
        addChildComponent (oscEditor);

        message.setJustificationType (juce::Justification::centred);
        message.setColour (juce::Label::textColourId, colour::silkCaption);
        addChildComponent (message);

        sampleEditor.onCloseRequest = [this] { if (onCloseRequest) onCloseRequest(); };
        setWantsKeyboardFocus (true);
        applyMode();
        startTimerHz (8);
    }

    ~SourceEditorPanel() override { stopTimer(); }

    std::function<void()> onCloseRequest;                 // window asks to hide
    std::function<void (const juce::String&)> onTitle;    // window title follows the mode

    // Called each time the window is shown.
    void show()
    {
        applyMode();
        if (sampleEditor.isVisible())
            sampleEditor.openEditor();
        grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        // the pop-out is a rack panel, not flat chassis: same face + border as every
        // other Block on the main panel (ClaudeDesign/design_handoff_broken_ui/README.md).
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (gradients::panel (r));
        g.fillRect (r);
        g.setColour (colour::panelBorder);
        g.drawRect (r, 1.0f);
    }

    void resized() override
    {
        sampleEditor.setBounds (getLocalBounds());
        oscEditor.setBounds (getLocalBounds());
        message.setBounds (getLocalBounds());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && onCloseRequest) { onCloseRequest(); return true; }
        return false;
    }

private:
    void timerCallback() override
    {
        const int m  = modeRaw    != nullptr ? (int) modeRaw->load()    : 0;
        const int om = oscModeRaw != nullptr ? (int) oscModeRaw->load() : 0;
        if (m == shownMode && om == shownOscMode) return;
        applyMode();
    }

    void applyMode()
    {
        shownMode    = modeRaw    != nullptr ? (int) modeRaw->load()    : 0;
        shownOscMode = oscModeRaw != nullptr ? (int) oscModeRaw->load() : 0;

        const bool isSampleish = (shownMode == 0 || shownMode == 1 || shownMode == 5);
        const bool isOsc       = (shownMode == 2);

        sampleEditor.setVisible (isSampleish);
        oscEditor.setVisible (isOsc);
        message.setVisible (! isSampleish && ! isOsc);

        if (shownMode == 3)
            message.setText ("NOISE has no waveform to edit.\nShape it with AMP NZ and PH NZ "
                             "in the SOURCE block.", juce::dontSendNotification);
        else if (shownMode == 4)
            message.setText ("LIVE INPUT has no stored waveform.\nUse IN TRIM and the "
                             "IN tuner in the SOURCE block.", juce::dontSendNotification);

        if (onTitle)
        {
            const char* t = "Sample Editor";
            if (shownMode == 1)      t = "Cycle Window";
            else if (shownMode == 5) t = "Tape Take";
            else if (isOsc)          t = shownOscMode == 2 ? "Oscillator - Draw"
                                       : (shownOscMode == 1 ? "Oscillator - Harmonic"
                                                            : "Oscillator - Wave");
            else if (shownMode == 3) t = "Noise";
            else if (shownMode == 4) t = "Live Input";
            onTitle (t);
        }

        if (isSampleish && isShowing())
            sampleEditor.openEditor();
    }

    SampleEditor sampleEditor;
    OscEditor    oscEditor;
    juce::Label  message;

    std::atomic<float>* modeRaw    = nullptr;
    std::atomic<float>* oscModeRaw = nullptr;
    int shownMode = -1, shownOscMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourceEditorPanel)
};
} // namespace broken::ui
