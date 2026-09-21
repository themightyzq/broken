#pragma once
// Live waveshaper transfer-curve display + editor. The drawn curve is sampled straight
// from a copy of dsp/Waveshaper.h's shape()/breakpointEval() math (kept byte-for-byte
// identical here — DO NOT "improve" it, the picture must match what you hear) so the
// display always tracks ws.curve / ws.drive / ws.randseed / ws.c001..128, including preset
// loads and host automation, via a 15 Hz poll (same Timer idiom as SampleEditor.h).
//
// A PENCIL over 128 points (v0.20, raised from 16 draggable handles - 128 is far too dense
// to grab one at a time). A drag paints every point it sweeps and interpolates across fast
// moves so no point is skipped. Each write goes LIVE to the matching ws.cNN parameter with
// the same begin/setValueNotifyingHost/end gesture idiom SampleEditor.h uses.
//
// DRAWING ENTERS CUSTOM, mirroring the oscillator's draw-to-enter (OscEditor.h): a stroke
// on any other curve first seeds the 128 points from the curve currently displayed, then
// switches ws.curve to Custom, then applies the stroke - so you deform the shape you were
// looking at. ws.drive/ws.morph and the old curve index are untouched, so re-selecting the
// previous curve puts it back.
//
// FROM SAMPLE fills the curve from the CYCLE window of the loaded sample: "custom wavetable
// distortion" (docs/DSP-NOTES.md §3.2). An audio slice is a NON-MONOTONIC transfer function,
// so this is deliberately violent - a clean sine comes out as broadband scream.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Theme.h"
#include "TsLookAndFeel.h"

namespace ts::ui
{
class CurveEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    CurveEditor (TurboSynthProcessor& p)
        : processor (p), apvts (p.apvts)
    {
        fromSampleButton.setTooltip ("Use the CYCLE window of the loaded sample AS the "
                                     "distortion curve. Non-monotonic on purpose \xe2\x80\x94 "
                                     "expect scream, not saturation.");
        fromSampleButton.onClick = [this] { grabCurveFromSample(); };
        addAndMakeVisible (fromSampleButton);
        setWantsKeyboardFocus (false);

        curveRaw = apvts.getRawParameterValue ("ws.curve");
        driveRaw = apvts.getRawParameterValue ("ws.drive");
        seedRaw  = apvts.getRawParameterValue ("ws.randseed");
        for (int i = 0; i < kCust; ++i)
        {
            auto id = params::curvePointId (i + 1);
            custRaw[(size_t) i]    = apvts.getRawParameterValue (id);
            custParams[(size_t) i] = apvts.getParameter (id);
        }

        refreshFromParams();
        startTimerHz (15);
    }

    ~CurveEditor() override { stopTimer(); }

    // Re-samples curveY[]/randPts_/custPts_/curCurve_/curDriveDb_ from the live params.
    // Public so EditView's COPY->CUSTOM action can force a fresh read before calling
    // evaluateCurrent() (the 15 Hz timer alone could be up to ~66 ms stale).
    void refreshFromParams()
    {
        curCurve_   = curveRaw != nullptr ? (int) std::lround (curveRaw->load()) : 0;
        curDriveDb_ = driveRaw != nullptr ? driveRaw->load() : 0.0f;

        for (int i = 0; i < kCust; ++i)
            custPts_[(size_t) i] = custRaw[(size_t) i] != nullptr ? custRaw[(size_t) i]->load() : 0.0f;

        int seed = seedRaw != nullptr ? (int) std::lround (seedRaw->load()) : 1;
        if (seed != lastSeed_)
            rebuildRandom (seed);

        for (int i = 0; i < kPoints; ++i)
        {
            float u = -1.0f + 2.0f * (float) i / (float) (kPoints - 1);
            curveY[(size_t) i] = shape (curCurve_, u, curDriveDb_);
        }
    }

    // Evaluates whatever curve refreshFromParams() last cached, at input u in [-1, 1].
    // Used by EditView's COPY->CUSTOM to sample the CURRENTLY SELECTED curve (which may
    // not be Custom) at the kCust fixed breakpoint x positions.
    float evaluateCurrent (float u) const { return shape (curCurve_, u, curDriveDb_); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        // glass covers only the plot; the FROM SAMPLE strip below stays panel, so the
        // button no longer sits ON the screen (v0.29 screenshot review)
        TsLookAndFeel::drawScreen (g, b.withTrimmedBottom (20.0f));

        // MUST be the same rectangle paintAt() edits, or the curve you see sits offset
        // from the curve you can touch
        auto area = plotArea();
        if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
            return;

        // guides sit a touch brighter than the screen fill (lcdBorder), not a chassis hairline
        g.setColour (colour::lcdBorder);
        g.drawVerticalLine ((int) area.getCentreX(), area.getY(), area.getBottom());
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        // faint 45-degree identity diagonal for reference
        g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

        // the active curve, sampled at kPoints positions across input -1..+1
        curvePath.clear();
        for (int i = 0; i < kPoints; ++i)
        {
            float px = area.getX() + area.getWidth() * ((float) i / (float) (kPoints - 1));
            float py = area.getBottom() - (curveY[(size_t) i] * 0.5f + 0.5f) * area.getHeight();
            if (i == 0)
                curvePath.startNewSubPath (px, py);
            else
                curvePath.lineTo (px, py);
        }

        // phosphor ghost: same path, offset 1px down-right, drawn BEFORE the live stroke
        // (design handoff's "faint ghost stroke") — a transform at draw time, no path copy
        g.setColour (colour::lcdFaint2);
        g.strokePath (curvePath, juce::PathStrokeType (1.6f), juce::AffineTransform::translation (1.0f, 1.0f));

        g.setColour (colour::lcdText);
        g.strokePath (curvePath, juce::PathStrokeType (1.6f));

        // draggable breakpoint handles, Custom curve only
        if (isCustom())
        {
            g.setColour (colour::lcdFaint);
            for (int i = 0; i < kCust; ++i)
            {
                auto p = handlePos (area, i);
                g.fillEllipse (p.x - 1.2f, p.y - 1.2f, 2.4f, 2.4f);
            }
        }
        else if (curCurve_ == 8) // Random curve: show its 12 breakpoints too, dimmer since they aren't draggable
        {
            g.setColour (colour::lcdFaint2);
            for (int i = 0; i < (int) randPts_.size(); ++i)
            {
                float x01 = (float) i / (float) (randPts_.size() - 1);
                float px = area.getX() + x01 * area.getWidth();
                float py = area.getBottom() - (randPts_[(size_t) i] * 0.5f + 0.5f) * area.getHeight();
                g.fillEllipse (px - 1.2f, py - 1.2f, 2.4f, 2.4f);
            }
        }
    }

    void resized() override
    {
        fromSampleButton.setBounds (buttonStrip().removeFromRight (96).reduced (1));
    }

    // The button lives BELOW the plot, never on top of it: it used to sit inside the
    // drawable area, so the bottom-right corner of the curve could not be drawn.
    juce::Rectangle<int> buttonStrip() const { return getLocalBounds().removeFromBottom (20); }
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().withTrimmedBottom (20).toFloat().reduced (3.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! plotArea().contains (e.position)) return;
        beginGestures();
        enterCustom();          // no-op when Custom is already selected
        lastIndex = -1;
        paintAt (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! gesturesOpen) return;
        paintAt (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override { endGestures(); }

private:
    static constexpr int kPoints = 512; // display resolution
    static constexpr int kCust   = params::curvePointCount; // DSP-NOTES §3.1

    bool isCustom() const { return curCurve_ == 9; }

    juce::Point<float> handlePos (juce::Rectangle<float> area, int index) const
    {
        float x01 = (float) index / (float) (kCust - 1);
        float px = area.getX() + x01 * area.getWidth();
        float py = area.getBottom() - (custPts_[(size_t) index] * 0.5f + 0.5f) * area.getHeight();
        return { px, py };
    }

    void beginGestures()
    {
        if (gesturesOpen) return;
        for (auto* p : custParams) if (p != nullptr) p->beginChangeGesture();
        gesturesOpen = true;
    }

    void endGestures()
    {
        if (! gesturesOpen) return;
        for (auto* p : custParams) if (p != nullptr) p->endChangeGesture();
        gesturesOpen = false;
        lastIndex = -1;
    }

    void writePoint (int k, float v)
    {
        if (k < 0 || k >= kCust) return;
        const float y = juce::jlimit (-1.0f, 1.0f, v);
        custPts_[(size_t) k] = y;   // local cache so the drawing tracks instantly
        if (auto* p = custParams[(size_t) k])
            p->setValueNotifyingHost (p->convertTo0to1 (y));
    }

    // Seeds the 128 points from the curve currently ON SCREEN, then selects Custom, so a
    // stroke deforms the shape you were looking at rather than jumping to a diagonal.
    // Assumes the caller already opened the gestures. ws.drive/ws.morph are untouched.
    void enterCustom()
    {
        if (isCustom()) return;
        // MUST refresh first: curCurve_/curDriveDb_ come from a 15 Hz timer, so changing
        // the CURVE combo and dragging within ~66 ms would otherwise seed the PREVIOUS
        // curve and silently break the "keeps the shape you were looking at" promise.
        refreshFromParams();
        for (int k = 0; k < kCust; ++k)
        {
            const float u = -1.0f + 2.0f * (float) k / (float) (kCust - 1);
            writePoint (k, evaluateCurrent (u));
        }
        if (auto* p = apvts.getParameter ("ws.curve"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (9.0f)); // Custom
            p->endChangeGesture();
        }
        curCurve_ = 9;
    }

    // Paints the point under the cursor plus every point between here and the previous
    // one, so a fast drag cannot leave untouched points behind as spikes.
    void paintAt (juce::Point<float> pos)
    {
        auto area = plotArea();
        if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f) return;

        const float x01 = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float y01 = (area.getBottom() - pos.y) / area.getHeight();
        const float y   = juce::jlimit (-1.0f, 1.0f, y01 * 2.0f - 1.0f);
        const int   k   = juce::jlimit (0, kCust - 1, (int) std::round (x01 * (float) (kCust - 1)));

        if (lastIndex >= 0 && lastIndex != k)
        {
            const int step = k > lastIndex ? 1 : -1;
            const int span = std::abs (k - lastIndex);
            for (int i = 1; i < span; ++i)
            {
                const float t = (float) i / (float) span;
                writePoint (lastIndex + i * step, lastValue + t * (y - lastValue));
            }
        }
        writePoint (k, y);
        lastIndex = k;
        lastValue = y;
        refreshFromParams();
        repaint();
    }

    // "Custom wavetable distortion" (docs/DSP-NOTES.md §3.2): the CYCLE window of the
    // loaded sample BECOMES the transfer curve. Centred first so an off-centre slice does
    // not waste half the output range, then peak-normalized.
    void grabCurveFromSample()
    {
        const auto& buf = processor.getSampleBuffer();
        if (buf.size() < 2) return;

        auto* posRaw = apvts.getRawParameterValue ("source.winpos");
        auto* lenRaw = apvts.getRawParameterValue ("source.winlen");
        if (posRaw == nullptr || lenRaw == nullptr) return;

        const double last  = (double) (buf.size() - 1);
        const double start = juce::jlimit (0.0, last, (double) posRaw->load() * last);
        const double avail = last - start > 2.0 ? last - start : 2.0;
        const double len   = juce::jlimit (2.0, avail, (double) lenRaw->load());

        std::array<float, (size_t) kCust> pts {};
        double mean = 0.0;
        for (int k = 0; k < kCust; ++k)
        {
            const double idx = start + len * (double) k / (double) kCust;
            const auto   i0  = (size_t) juce::jlimit (0.0, last, std::floor (idx));
            const auto   i1  = (size_t) juce::jlimit (0.0, last, (double) i0 + 1.0);
            const float  fr  = (float) (idx - std::floor (idx));
            pts[(size_t) k]  = buf[i0] + fr * (buf[i1] - buf[i0]);
            mean += pts[(size_t) k];
        }
        mean /= (double) kCust;

        float peak = 0.0f;
        for (auto& v : pts) { v -= (float) mean; peak = std::max (peak, std::abs (v)); }
        const float g = peak > 1.0e-6f ? 1.0f / peak : 1.0f;

        beginGestures();
        enterCustom();
        for (int k = 0; k < kCust; ++k) writePoint (k, pts[(size_t) k] * g);
        endGestures();
        refreshFromParams();
        repaint();
    }

    // ---- everything below mirrors dsp/Waveshaper.h shape()/breakpointEval()/
    // setRandomSeed() exactly. Do not re-derive; keep in lockstep with the DSP. ----

    static float breakpointEval (const float* pts, int n, float u)
    {
        const float t = (u + 1.0f) * 0.5f * (float) (n - 1);
        const int i0 = std::clamp ((int) t, 0, n - 2);
        const float frac = t - (float) i0;
        const float w = 0.5f * (1.0f - std::cos (3.14159265f * frac));
        return pts[i0] + w * (pts[i0 + 1] - pts[i0]);
    }

    float shape (int curve, float u, float driveDb) const
    {
        switch (curve)
        {
            case 0: return u;
            case 1: return std::clamp (1.5f * u, -1.0f, 1.0f);
            case 2: return 1.5f * u - 0.5f * u * u * u;
            case 3:
            {
                const float v = 2.0f * u;
                const float t = v + 1.0f;
                const float fmod4 = t - 4.0f * std::floor (t / 4.0f);
                return 1.0f - std::abs (fmod4 - 2.0f);
            }
            case 4: return u >= 0.0f ? 1.0f - (1.0f - u) * (1.0f - u) : 0.6f * u;
            case 5: return std::round (u * 8.0f) / 8.0f;
            case 6:
                return std::sin ((3.14159265358979323846f / 2.0f) * u
                                  * (1.0f + 2.0f * (driveDb / 40.0f)));
            case 7: return -u * (2.0f - std::abs (u));
            case 8: return breakpointEval (randPts_.data(), 12, u);
            case 9: return breakpointEval (custPts_.data(), kCust, u);
            default: return u;
        }
    }

    void rebuildRandom (int seed)
    {
        lastSeed_ = seed;
        uint32_t s = (uint32_t) seed * 2654435761u + 0x9E3779B9u;
        auto next = [&s]
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) ((double) s / 4294967295.0) * 2.0f - 1.0f;
        };
        for (auto& pt : randPts_) pt = next();
    }

    void timerCallback() override
    {
        refreshFromParams();
        repaint();
    }

    TurboSynthProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    std::atomic<float>* curveRaw = nullptr;
    std::atomic<float>* driveRaw = nullptr;
    std::atomic<float>* seedRaw  = nullptr;
    std::array<std::atomic<float>*, (size_t) kCust> custRaw {};
    std::array<juce::RangedAudioParameter*, (size_t) kCust> custParams {};

    std::array<float, kPoints> curveY {};
    std::array<float, 12> randPts_ {};
    std::array<float, (size_t) kCust> custPts_ {};
    int curCurve_ = 0;
    float curDriveDb_ = 0.0f;
    int lastSeed_ = -1;
    bool gesturesOpen = false;
    int   lastIndex = -1;
    float lastValue = 0.0f;
    juce::TextButton fromSampleButton { "FROM SAMPLE" };
    juce::Path curvePath; // preallocated; cleared + rebuilt each paint, never reallocated per frame
};
} // namespace ts::ui
