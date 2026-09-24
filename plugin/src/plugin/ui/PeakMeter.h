#pragma once
// 10px-wide vertical output peak meter. Polls processor.outPeak (atomic, linear) at 30 Hz.
// Range -60..+6 dB, accent-coloured above -6 dB.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace broken::ui
{
class PeakMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit PeakMeter (BrokenProcessor& proc) : processor (proc)
    {
        setTooltip ("Output level.");
        startTimerHz (30);
    }

    ~PeakMeter() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // Reserve a thin strip on the left for the printed tick scale (design handoff:
        // "tick scale printed left"); the well itself is whatever's left of that.
        auto tickArea = b.removeFromLeft (juce::jmin (3.0f, b.getWidth() * 0.35f));
        auto well = b;

        g.setColour (colour::lcdScreenDark);
        g.fillRect (well);
        g.setColour (colour::lcdBorder);
        g.drawRect (well, 1.0f);

        auto inner = well.reduced (1.0f);
        const float loDb = -60.0f, hiDb = 6.0f, threshDb = -6.0f;

        // small marks at 0/-6/-12/-24/-48 dB, same loDb..hiDb mapping as the fill below
        g.setColour (colour::tick);
        for (float markDb : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            float markFrac = (markDb - loDb) / (hiDb - loDb);
            float y = well.getBottom() - well.getHeight() * markFrac;
            g.drawHorizontalLine ((int) y, tickArea.getX(), tickArea.getRight());
        }

        float db = juce::Decibels::gainToDecibels (lastPeak, loDb);
        db = juce::jlimit (loDb, hiDb, db);
        float frac = (db - loDb) / (hiDb - loDb);
        float fillH = inner.getHeight() * frac;
        auto fillRect = juce::Rectangle<float> (inner.getX(), inner.getBottom() - fillH,
                                                 inner.getWidth(), fillH);

        float threshFrac = (threshDb - loDb) / (hiDb - loDb);
        float threshY = inner.getBottom() - inner.getHeight() * threshFrac;

        // Gradient stops anchored to the WELL's full range (not the current fill height)
        // so the colour at a given dB position never shifts as the level moves — like a
        // printed gradient behind glass, revealed by the fill.
        juce::ColourGradient grad (colour::meterLo, inner.getBottomLeft(), colour::meterHi, inner.getTopLeft(), false);
        grad.addColour (0.70, colour::meterMid);
        g.setGradientFill (grad);
        g.fillRect (fillRect);

        if (fillRect.getY() < threshY)
        {
            // colour shifts to amber above -6 dB, per the handoff
            g.setColour (colour::meterHot);
            g.fillRect (fillRect.withBottom (threshY));
        }
    }

    void resized() override {}

private:
    void timerCallback() override
    {
        float peak = processor.outPeak.load();
        if (! juce::approximatelyEqual (peak, lastPeak))
        {
            lastPeak = peak;
            repaint();
        }
    }

    BrokenProcessor& processor;
    float lastPeak = 0.0f;
};
} // namespace broken::ui
