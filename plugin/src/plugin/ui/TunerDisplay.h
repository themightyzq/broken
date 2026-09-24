#pragma once
// Live tuner readout (docs/PANEL.md): note name, ±50-cent needle, Hz. Polls one of the
// processor's tuner taps at 20 Hz and runs the display-grade PitchDetector on the GUI
// thread. Dims when clarity is below the gate (no confident pitch).

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "BrokenLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../../dsp/PitchDetector.h"

namespace broken::ui
{
class TunerDisplay : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    TunerDisplay (BrokenProcessor& p, bool postChain, const juce::String& label,
                  const juce::String& tooltip)
        : processor (p), post (postChain), caption (label)
    {
        setTooltip (tooltip);
        window.resize ((size_t) dsp::PitchDetector::windowSize);
        startTimerHz (20);
    }

    // latest confident reading, for anything (TUNE) that wants to reuse it
    bool hasPitch() const { return clarity >= dsp::PitchDetector::clarityGate; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        BrokenLookAndFeel::drawScreen (g, b.toFloat()); // shared 90s phosphor glass

        auto area = b.reduced (4);
        const bool live = hasPitch();
        auto* lnf = dynamic_cast<BrokenLookAndFeel*> (&getLookAndFeel()); // may be null briefly in the pop-out window

        // caption sits OUTSIDE the LCD screen feel: small silkscreen caption, not glowing green.
        g.setColour (colour::silkCaption);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (caption, area.removeFromTop (10), juce::Justification::centredLeft);

        auto row = area.removeFromTop (16);
        auto noteArea = row.removeFromLeft (46);
        auto hzFont = lnf != nullptr ? lnf->lcdFont (12.0f) : juce::Font (juce::FontOptions (10.0f));
        if (live)
        {
            const auto name = juce::String (dsp::PitchDetector::noteName (note))
                            + juce::String (dsp::PitchDetector::octaveOf (note));
            // note name is the primary LCD value: bright lcdText, with the LnF's soft glow
            if (lnf != nullptr)
                lnf->drawLcdText (g, name, noteArea, 16.0f, juce::Justification::centredLeft, colour::lcdText);
            else
            {
                g.setColour (colour::lcdText);
                g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
                g.drawText (name, noteArea, juce::Justification::centredLeft);
            }
            g.setColour (colour::lcdDim); // Hz is the secondary LCD value
            g.setFont (hzFont);
            g.drawText (juce::String (hz, 1) + " Hz", row, juce::Justification::centredRight);
        }
        else
        {
            // clarity gate not met: whole readout drops to lcdFaint, including "-- Hz"
            // (previously the Hz line was omitted entirely when unpitched)
            if (lnf != nullptr)
                lnf->drawLcdText (g, "--", noteArea, 16.0f, juce::Justification::centredLeft, colour::lcdFaint);
            else
            {
                g.setColour (colour::lcdFaint);
                g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
                g.drawText ("--", noteArea, juce::Justification::centredLeft);
            }
            g.setColour (colour::lcdFaint);
            g.setFont (hzFont);
            g.drawText ("-- Hz", row, juce::Justification::centredRight);
        }

        // cents needle: +-50 across the bar well, centre tick, needle brightness by lock
        auto bar = area.removeFromTop (10).reduced (2, 1);
        g.setColour (colour::lcdScreenDark);
        g.fillRect (bar);
        g.setColour (colour::lcdBorder);
        g.drawRect (bar, 1);
        g.setColour (colour::tick); // centre mark, printed-scale colour
        g.drawVerticalLine (bar.getCentreX(), (float) bar.getY(), (float) bar.getBottom());
        if (live)
        {
            const float t = juce::jlimit (-50.0f, 50.0f, cents) / 100.0f + 0.5f;
            const int x = bar.getX() + (int) (t * (float) bar.getWidth());
            g.setColour (std::abs (cents) < 3.0f ? colour::lcdText : colour::lcdFaint2);
            g.fillRect (x - 1, bar.getY() + 1, 3, bar.getHeight() - 2);
        }
    }

private:
    void timerCallback() override
    {
        processor.copyTunerTap (post, window.data(), (int) window.size());
        detector.prepare (processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0);
        const auto r = detector.detect (window.data());
        clarity = r.clarity;
        if (hasPitch())
        {
            hz = r.hz;
            dsp::PitchDetector::centsFromHz (hz, note, cents);
        }
        repaint();
    }

    BrokenProcessor& processor;
    dsp::PitchDetector detector;
    std::vector<float> window;
    bool post = false;
    juce::String caption;
    float hz = 0.0f, cents = 0.0f, clarity = 0.0f;
    int note = 69;
};
} // namespace broken::ui
