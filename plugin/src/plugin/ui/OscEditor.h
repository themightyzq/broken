#pragma once
// One-cycle oscillator view for the source pop-out window. Shows what OSC actually
// sounds like: the analytic wave (Wave mode), the additive sum (Harmonic mode), or the
// hand-drawn table (Draw mode). Before this existed the window drew the loaded SAMPLE in
// every mode, so in Osc mode you were looking at one thing and hearing another.
//
// It is a PENCIL, not draggable handles: 128 points is far too dense to grab one at a
// time, so a drag paints every point it sweeps over and interpolates across fast mouse
// moves so no point is skipped.
//
// DRAWING IS HOW YOU ENTER DRAW MODE (v0.19). The mode selector used to be a combo
// labelled "MODE" buried in the EDIT view's HARMONICS block, which nobody could find. Now
// a stroke in any osc mode seeds the 128 points from the cycle currently on screen, flips
// osc.mode to Draw, and then applies the stroke — so you deform the shape you were looking
// at. source.oscwave and the harmonic amplitudes are untouched, so the WAVE button puts
// the previous sound back exactly.
//
// Wave/Harmonic rendering mirrors dsp/SourceEngine.h + dsp/Waves.h; keep them in lockstep.
// docs/DSP-NOTES.md §1.3b, docs/PANEL.md.

#include <algorithm>
#include <array>
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "OscCurve.h"
#include "Theme.h"

namespace broken::ui
{
class OscEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit OscEditor (BrokenProcessor& p) : processor (p), apvts (p.apvts)
    {
        // the whole plot is the tooltip target; the mode/FROM SAMPLE/SINE buttons
        // below set their own, more specific tooltips and take priority over this one.
        setTooltip ("One cycle of the oscillator. Drag to draw - sharp corners buzz, "
                    "that is the era.");
        modeRaw = apvts.getRawParameterValue ("osc.mode");
        waveRaw = apvts.getRawParameterValue ("source.oscwave");
        posRaw  = apvts.getRawParameterValue ("source.winpos");
        lenRaw  = apvts.getRawParameterValue ("source.winlen");
        for (int i = 0; i < kDraw; ++i)
        {
            auto d = params::drawPointId (i + 1);
            drawRaw[(size_t) i]    = apvts.getRawParameterValue (d);
            drawParams[(size_t) i] = apvts.getParameter (d);
        }
        for (int i = 0; i < params::harmonicCount; ++i)
            harmRaw[(size_t) i] = apvts.getRawParameterValue (params::harmonicId (i + 1));

        // The mode is now visible where the shape is, not only in the EDIT view.
        static const char* modeTips[3] = {
            "Preset waveform mode.",
            "Build the wave from 64 partials.",
            "Hand-drawn wave. Or just drag on the shape - it switches for you."
        };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = modeButtons[(size_t) i];
            b->setClickingTogglesState (false);
            b->setTooltip (modeTips[i]);
            b->onClick = [this, i] { setOscMode (i); };
            addAndMakeVisible (b);
        }

        fromSampleButton.setTooltip ("Grabs the CYCLE window of the loaded sample as the drawn shape.");
        resetButton.setTooltip ("Resets the drawn shape to a sine.");
        fromSampleButton.onClick = [this] { grabCycleFromSample(); };
        resetButton.onClick      = [this] { resetToSine(); };
        addAndMakeVisible (fromSampleButton);
        addAndMakeVisible (resetButton);

        hint.setJustificationType (juce::Justification::centredLeft);
        hint.setColour (juce::Label::textColourId, colour::silkCaption);
        addAndMakeVisible (hint);

        taps.attach (apvts);
        rebuildCurve();
        updateHint();
        startTimerHz (15);
    }

    ~OscEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        // rack panel chrome, matching every other Block on the main panel — the
        // pop-out reuses the same face rather than a flat fill.
        auto b = getLocalBounds().toFloat();
        g.setGradientFill (gradients::panel (b));
        g.fillRect (b);
        g.setColour (colour::panelBorder);
        g.drawRect (b, 1.0f);

        // the plot is an LCD screen set into that panel
        auto area = plotArea().toFloat();
        BrokenLookAndFeel::drawScreen (g, area);

        // centre line + quarter-cycle guides
        g.setColour (colour::ruleInner);
        const float midY = area.getCentreY();
        g.drawHorizontalLine ((int) midY, area.getX(), area.getRight());
        for (int q = 1; q < 4; ++q)
        {
            const float x = area.getX() + area.getWidth() * (float) q / 4.0f;
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }
        g.setColour (colour::lcdBorder);
        g.drawRect (area, 1.0f);

        // the cycle itself
        juce::Path path;
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kPoints - 1);
            const float y = midY - curve[(size_t) i] * area.getHeight() * 0.48f;
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        g.setColour (isDrawMode() ? colour::lcdText : colour::lcdDim);
        g.strokePath (path, juce::PathStrokeType (1.6f));

        // in Draw mode show where the 64 editable points sit, so the pencil's resolution
        // is visible rather than a surprise
        if (isDrawMode())
        {
            g.setColour (colour::lcdFaint2);
            for (int k = 0; k < kDraw; ++k)
            {
                const float x = area.getX() + area.getWidth() * (float) k / (float) kDraw;
                const float y = midY - drawRaw[(size_t) k]->load() * area.getHeight() * 0.48f;
                g.fillEllipse (x - 1.5f, y - 1.5f, 3.0f, 3.0f);
            }
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8);
        auto top = b.removeFromTop (24);
        const int mw = juce::jmin (96, top.getWidth() / 4);
        for (auto* mb : modeButtons) { mb->setBounds (top.removeFromLeft (mw)); top.removeFromLeft (4); }

        auto bar = b.removeFromBottom (26);
        fromSampleButton.setBounds (bar.removeFromLeft (110));
        bar.removeFromLeft (8);
        resetButton.setBounds (bar.removeFromLeft (80));
        bar.removeFromLeft (12);
        hint.setBounds (bar);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! plotArea().contains (e.getPosition())) return;
        beginGestures();
        enterDrawMode();          // no-op when already drawing
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
    static constexpr int kDraw   = params::drawPointCount; // DSP-NOTES §1.3b

    bool isDrawMode() const { return modeRaw != nullptr && (int) modeRaw->load() == 2; }

    juce::Rectangle<int> plotArea() const
    {
        return getLocalBounds().reduced (8).withTrimmedTop (28).withTrimmedBottom (34);
    }

    // ---- editing -----------------------------------------------------------------

    void beginGestures()
    {
        if (gesturesOpen) return;
        for (auto* p : drawParams) if (p != nullptr) p->beginChangeGesture();
        gesturesOpen = true;
    }

    void endGestures()
    {
        if (! gesturesOpen) return;
        for (auto* p : drawParams) if (p != nullptr) p->endChangeGesture();
        gesturesOpen = false;
        lastIndex = -1;
    }

    void setOscMode (int m)
    {
        if (auto* p = apvts.getParameter ("osc.mode"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) m));
            p->endChangeGesture();
        }
        rebuildCurve();
        updateHint();
        repaint();
    }

    // Seeds the 128 points from the cycle currently ON SCREEN, then switches to Draw. The
    // shape you were looking at is what you start deforming; source.oscwave and the 64
    // harmonic amplitudes are left alone, so WAVE restores the previous sound exactly.
    // Assumes the caller already opened the point gestures.
    void enterDrawMode()
    {
        if (isDrawMode()) return;

        std::array<float, (size_t) kDraw> seed {};
        osccurve::build (taps, seed.data(), kDraw);
        for (int k = 0; k < kDraw; ++k) writePoint (k, seed[(size_t) k]);

        if (auto* p = apvts.getParameter ("osc.mode"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
            p->endChangeGesture();
        }
        updateHint();
    }

    void writePoint (int k, float v)
    {
        if (k < 0 || k >= kDraw) return;
        if (auto* p = drawParams[(size_t) k])
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-1.0f, 1.0f, v)));
    }

    // Paints the point under the cursor, and every point between here and the previous
    // position — a fast drag would otherwise leave untouched points behind as spikes.
    void paintAt (juce::Point<float> pos)
    {
        auto area = plotArea().toFloat();
        if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f) return;

        const float x01 = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        const float y   = juce::jlimit (-1.0f, 1.0f,
                                        (area.getCentreY() - pos.y) / (area.getHeight() * 0.48f));
        const int k = juce::jlimit (0, kDraw - 1, (int) std::round (x01 * (float) (kDraw - 1)));

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
        rebuildCurve();
        repaint();
    }

    void resetToSine()
    {
        beginGestures();
        enterDrawMode();
        for (int k = 0; k < kDraw; ++k)
            writePoint (k, (float) std::sin (6.283185307179586 * (double) k / (double) kDraw));
        endGestures();
        rebuildCurve();
        repaint();
    }

    // "Convert sample to oscillator" (RESEARCH.md quote #2 — the artist's own phrase): fill
    // the 64 points from the current CYCLE window of the loaded sample, peak-normalized.
    void grabCycleFromSample()
    {
        const auto& buf = processor.getSampleBuffer();
        if (buf.size() < 2 || posRaw == nullptr || lenRaw == nullptr)
        {
            hint.setText ("Load a sample first.", juce::dontSendNotification);
            return;
        }

        const double last  = (double) (buf.size() - 1);
        const double start = juce::jlimit (0.0, last, (double) posRaw->load() * last);
        const double len   = juce::jlimit (2.0, last - start > 2.0 ? last - start : 2.0,
                                           (double) lenRaw->load());

        std::array<float, (size_t) kDraw> pts {};
        float peak = 0.0f;
        for (int k = 0; k < kDraw; ++k)
        {
            const double idx = start + len * (double) k / (double) kDraw;
            const auto   i0  = (size_t) juce::jlimit (0.0, last, std::floor (idx));
            const auto   i1  = (size_t) juce::jlimit (0.0, last, (double) i0 + 1.0);
            const float  fr  = (float) (idx - std::floor (idx));
            pts[(size_t) k] = buf[i0] + fr * (buf[i1] - buf[i0]);
            peak = std::max (peak, std::abs (pts[(size_t) k]));
        }
        const float g = peak > 1.0e-6f ? 1.0f / peak : 1.0f;

        beginGestures();
        enterDrawMode();   // grabbing a cycle IS drawing; do not require the mode first
        for (int k = 0; k < kDraw; ++k) writePoint (k, pts[(size_t) k] * g);
        endGestures();

        hint.setText ("Grabbed the CYCLE window. Draw on it.", juce::dontSendNotification);
        rebuildCurve();
        repaint();
    }

    // ---- display -----------------------------------------------------------------

    void timerCallback() override
    {
        const int m = modeRaw != nullptr ? (int) modeRaw->load() : 0;
        const int w = waveRaw != nullptr ? (int) waveRaw->load() : 0;
        bool changed = (m != lastMode) || (w != lastWave);
        for (int k = 0; k < kDraw && ! changed; ++k)
            if (std::abs (drawRaw[(size_t) k]->load() - lastDraw[(size_t) k]) > 1.0e-5f) changed = true;
        for (int k = 0; k < 64 && ! changed; ++k)
            if (std::abs (harmRaw[(size_t) k]->load() - lastHarm[(size_t) k]) > 1.0e-3f) changed = true;
        if (! changed) return;

        lastMode = m;
        lastWave = w;
        rebuildCurve();
        updateHint();
        repaint();
    }

    void updateHint()
    {
        const int m = modeRaw != nullptr ? (int) modeRaw->load() : 0;
        // toggle state, not a per-button colour override: the global LnF paints a
        // toggled TextButton accent-filled with dark text, matching the reference's
        // segmented control (drawButtonBackground/drawButtonText in BrokenLookAndFeel.h).
        for (int i = 0; i < 3; ++i)
            modeButtons[(size_t) i]->setToggleState (i == m, juce::dontSendNotification);
        hint.setText (m == 2 ? "Drag to draw. Sharp corners buzz - that is the era."
                             : "Just start drawing - it switches to DRAW and keeps this shape.",
                      juce::dontSendNotification);
        repaint();
    }

    void rebuildCurve()
    {
        for (int k = 0; k < kDraw; ++k) lastDraw[(size_t) k] = drawRaw[(size_t) k]->load();
        for (int k = 0; k < 64; ++k)     lastHarm[(size_t) k] = harmRaw[(size_t) k]->load();
        osccurve::build (taps, curve.data(), kPoints);
    }

    BrokenProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* modeRaw = nullptr;
    std::atomic<float>* waveRaw = nullptr;
    std::atomic<float>* posRaw  = nullptr;
    std::atomic<float>* lenRaw  = nullptr;
    std::array<std::atomic<float>*, (size_t) kDraw> drawRaw {};
    std::array<std::atomic<float>*, 64> harmRaw {};
    std::array<juce::RangedAudioParameter*, (size_t) kDraw> drawParams {};
    osccurve::Taps taps;

    std::array<float, kPoints> curve {};
    std::array<float, (size_t) kDraw> lastDraw {};
    std::array<float, 64> lastHarm {};
    int lastMode = -1, lastWave = -1;

    juce::TextButton waveBtn { "WAVE" }, harmBtn { "HARMONIC" }, drawBtn { "DRAW" };
    std::array<juce::TextButton*, 3> modeButtons { &waveBtn, &harmBtn, &drawBtn };
    juce::TextButton fromSampleButton { "FROM SAMPLE" };
    juce::TextButton resetButton { "SINE" };
    juce::Label hint;

    bool gesturesOpen = false;
    int   lastIndex = -1;
    float lastValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscEditor)
};
} // namespace broken::ui
