#pragma once
// 64-bar harmonic editor for osc.mode == Harmonic: the spec's Harmonic Mode, where the
// oscillator is built from 64 partials (osc.h01..osc.h64, each 0..100%) instead of a
// preset waveform. Same shape as CurveEditor.h: live param writes on drag, a 15 Hz Timer
// poll for the display cache (so host automation / preset loads stay reflected), and
// everything used by paint()/timerCallback() preallocated up front.
//
// Two drag modes:
//  - plain drag: paints the bar(s) under the cursor as it moves, using the on-screen
//    y value at each bar's x (interpolated between successive mouse events so a fast
//    drag doesn't skip bars).
//  - SHIFT+drag: RAKE, the spec's tool. A straight line is drawn from the drag's start
//    point to the current point, and every bar between those two x positions is set to
//    that line's y at the bar's x - recomputed on every mouseDrag call as the line pivots.
//
// Each newly-touched osc.hNN parameter gets its own beginChangeGesture()/endChangeGesture()
// pair (several bars can be "open" at once mid-drag); mouseUp closes whatever is still open.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Params.h"
#include "Theme.h"
#include "BrokenLookAndFeel.h"

namespace broken::ui
{
class HarmonicEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit HarmonicEditor (juce::AudioProcessorValueTreeState& apvtsIn) : apvts (apvtsIn)
    {
        setWantsKeyboardFocus (false);
        setTooltip ("The 64 partials, by design's Harmonic Mode. Drag bars to draw; "
                    "hold Shift to rake a straight line across them.");

        for (int i = 0; i < kPartials; ++i)
        {
            auto id = params::harmonicId (i + 1);
            rawValues[(size_t) i] = apvts.getRawParameterValue (id);
            params[(size_t) i]    = apvts.getParameter (id);
        }

        refreshFromParams();
        startTimerHz (15);
    }

    ~HarmonicEditor() override { stopTimer(); }

    // Re-samples values[] from the live params. Public so callers (e.g. EditView's preset
    // buttons) can force a fresh read; also called by the 15 Hz timer.
    void refreshFromParams()
    {
        for (int i = 0; i < kPartials; ++i)
            values[(size_t) i] = rawValues[(size_t) i] != nullptr ? rawValues[(size_t) i]->load() : 0.0f;
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        BrokenLookAndFeel::drawScreen (g, b); // shared 90s phosphor glass (bezel, wash, scanlines)

        auto area = barsArea();
        if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
            return;

        // faint 50% gridline: a guide a touch brighter than the screen fill, not a chassis hairline
        g.setColour (colour::lcdBorder);
        float midY = area.getBottom() - 0.5f * area.getHeight();
        g.drawHorizontalLine ((int) midY, area.getX(), area.getRight());

        // the 64 bars: lcdDim body, 1px lcdText cap so each bar reads like a lit VFD segment
        const float barW = area.getWidth() / (float) kPartials;
        for (int i = 0; i < kPartials; ++i)
        {
            float pct = juce::jlimit (0.0f, 100.0f, values[(size_t) i]) / 100.0f;
            float h = pct * area.getHeight();
            juce::Rectangle<float> bar (area.getX() + (float) i * barW, area.getBottom() - h,
                                         juce::jmax (1.0f, barW - 1.0f), h);
            g.setColour (colour::lcdDim);
            g.fillRect (bar);
            if (h > 1.0f)
            {
                g.setColour (colour::lcdText);
                g.fillRect (bar.withHeight (1.0f)); // brighter cap line on top of the bar
            }
        }

        // partial-number labels, only if there is room for them
        if (showLabels())
        {
            auto* lnf = dynamic_cast<BrokenLookAndFeel*> (&getLookAndFeel()); // null briefly in the pop-out window
            g.setColour (colour::lcdFaint);
            g.setFont (lnf != nullptr ? lnf->lcdFont (11.0f) : juce::Font (juce::FontOptions (9.0f)));
            auto labelStrip = juce::Rectangle<float> (area.getX(), area.getBottom(),
                                                        area.getWidth(), labelH);
            g.drawText ("1", labelStrip.removeFromLeft (16.0f), juce::Justification::centredLeft);
            g.drawText ("64", labelStrip.removeFromRight (20.0f), juce::Justification::centredRight);
        }
    }

    void resized() override {}

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStart = e.position;
        lastDragPos = e.position;
        paintAt (xToIndex (e.position.x), yToValue (e.position.y));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isShiftDown())
        {
            // RAKE: straight line from the drag's start point to here, applied to every
            // bar between those two x positions.
            int i0 = xToIndex (dragStart.x);
            int i1 = xToIndex (e.position.x);
            if (i0 > i1)
                std::swap (i0, i1);

            const float dx = e.position.x - dragStart.x;
            for (int i = i0; i <= i1; ++i)
            {
                float t = std::abs (dx) > 0.0001f ? (indexCentreX (i) - dragStart.x) / dx : 0.0f;
                float y = dragStart.y + t * (e.position.y - dragStart.y);
                paintAt (i, yToValue (y));
            }
        }
        else
        {
            // Plain paint: interpolate between the last event and this one so a fast
            // drag still touches every bar in between, rather than skipping some.
            int i0 = xToIndex (lastDragPos.x);
            int i1 = xToIndex (e.position.x);
            int step = i1 >= i0 ? 1 : -1;
            int span = std::abs (i1 - i0);
            for (int i = i0; ; i += step)
            {
                float t = span > 0 ? (float) std::abs (i - i0) / (float) span : 0.0f;
                float y = lastDragPos.y + t * (e.position.y - lastDragPos.y);
                paintAt (i, yToValue (y));
                if (i == i1)
                    break;
            }
        }

        lastDragPos = e.position;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        for (int i = 0; i < kPartials; ++i)
        {
            if (gestureOpen[(size_t) i])
            {
                if (auto* p = params[(size_t) i])
                    p->endChangeGesture();
                gestureOpen[(size_t) i] = false;
            }
        }
        repaint();
    }

private:
    static constexpr int kPartials = params::harmonicCount;
    static constexpr float labelH = 10.0f;

    juce::Rectangle<float> barsArea() const
    {
        auto area = getLocalBounds().toFloat().reduced (3.0f);
        if (showLabels())
            area.removeFromBottom (labelH);
        return area;
    }

    bool showLabels() const { return getLocalBounds().toFloat().reduced (3.0f).getHeight() > 40.0f; }

    int xToIndex (float x) const
    {
        auto area = barsArea();
        if (area.getWidth() <= 0.0f)
            return 0;
        float rel = (x - area.getX()) / area.getWidth();
        return juce::jlimit (0, kPartials - 1, (int) std::floor (rel * (float) kPartials));
    }

    float indexCentreX (int index) const
    {
        auto area = barsArea();
        float barW = area.getWidth() / (float) kPartials;
        return area.getX() + barW * ((float) index + 0.5f);
    }

    float yToValue (float y) const
    {
        auto area = barsArea();
        if (area.getHeight() <= 0.0f)
            return 0.0f;
        float rel = (area.getBottom() - y) / area.getHeight();
        return juce::jlimit (0.0f, 100.0f, rel * 100.0f);
    }

    // Sets bar `index` to `value` live: updates the display cache immediately, opens a
    // change gesture on first touch this drag, and writes the host-notifying value.
    void paintAt (int index, float value)
    {
        if (index < 0 || index >= kPartials)
            return;
        values[(size_t) index] = value;
        if (auto* p = params[(size_t) index])
        {
            if (! gestureOpen[(size_t) index])
            {
                p->beginChangeGesture();
                gestureOpen[(size_t) index] = true;
            }
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        }
    }

    void timerCallback() override
    {
        refreshFromParams();
        repaint();
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::array<std::atomic<float>*, kPartials> rawValues {};
    std::array<juce::RangedAudioParameter*, kPartials> params {};
    std::array<float, kPartials> values {};
    std::array<bool, kPartials> gestureOpen {};

    juce::Point<float> dragStart, lastDragPos;
};
} // namespace broken::ui
