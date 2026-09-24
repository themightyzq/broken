#pragma once
// Mono thumbnail of processor.getDisplayBuffer(): drag-and-drop or click-to-browse loader,
// with a CYCLE-window overlay when source.mode == Cycle. ~300x120 per spec.

#include <array>
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include "OscCurve.h"
#include "Theme.h"
#include "BrokenLookAndFeel.h"
#include "../PluginProcessor.h"

namespace broken::ui
{
class WaveformDisplay : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    explicit WaveformDisplay (BrokenProcessor& proc) : processor (proc)
    {
        setTooltip ("Shows the loaded sample, the region (tinted band), and the cycle window. "
                    "Double-click or hit EDIT to open the region editor.");
        oscTaps.attach (processor.apvts);
        startTimerHz (10); // cheap poll for source.mode / winpos / winlen — avoids a listener

        editButton.setButtonText ("EDIT");
        editButton.setTooltip ("Zoom, pan, and drag-select the part of the file you want "
                                "\xe2\x80\x94 no destructive editing. Play a MIDI key to hear it live "
                                "while you drag.");
        editButton.onClick = [this] { if (onOpenEditor) onOpenEditor(); };
        addAndMakeVisible (editButton);
    }

    ~WaveformDisplay() override { stopTimer(); }

    // Wired by MangleView to show + front the SampleEditor overlay.
    std::function<void()> onOpenEditor;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        auto* lnf = dynamic_cast<BrokenLookAndFeel*> (&getLookAndFeel()); // may be null in the pop-out window briefly

        // shared 90s phosphor glass; its own scanlines replace the old stripe helper
        BrokenLookAndFeel::drawScreen (g, b.toFloat());
        if (dragHover) { g.setColour (colour::lcdText); g.drawRect (b, 1); }

        // OSC has no sample: draw the cycle you are actually hearing. Without this the
        // display showed the loaded sample in every mode, so in Osc mode you looked at
        // one thing and heard another (docs/PANEL.md).
        if (isOscMode)
        {
            std::array<float, 256> cyc {};
            osccurve::build (oscTaps, cyc.data(), (int) cyc.size());
            auto area = b.toFloat().reduced (4.0f);
            const float midY = area.getCentreY();
            g.setColour (colour::lcdBorder); // centre guide, a touch brighter than the screen fill
            g.drawHorizontalLine ((int) midY, area.getX(), area.getRight());
            juce::Path path;
            for (size_t i = 0; i < cyc.size(); ++i)
            {
                const float x = area.getX() + area.getWidth() * (float) i / (float) (cyc.size() - 1);
                const float y = midY - cyc[i] * area.getHeight() * 0.45f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (colour::lcdDim); // waveform trace colour per the LCD spec
            g.strokePath (path, juce::PathStrokeType (1.4f));
            g.setColour (colour::lcdFaint);
            g.setFont (lnf != nullptr ? lnf->lcdFont (11.0f) : juce::Font (juce::FontOptions (10.0f)));
            g.drawText (oscLabel, b.reduced (4, 2), juce::Justification::topLeft);
            return;
        }

        const auto& buf = processor.getDisplayBuffer();

        if (buf.empty())
        {
            g.setColour (colour::lcdFaint);
            g.setFont (lnf != nullptr ? lnf->lcdFont (14.0f) : juce::Font (juce::FontOptions (12.0f)));
            g.drawText (emptyStateText, b, juce::Justification::centred);
            return;
        }

        drawThumbnail (g, b.reduced (2), buf);

        // region band: regstart..regend across the FULL width (whole file)
        {
            auto area = b.toFloat();
            float w = area.getWidth();
            float x0 = area.getX() + juce::jlimit (0.0f, 1.0f, regStart) * w;
            float x1 = area.getX() + juce::jlimit (0.0f, 1.0f, regEnd) * w;
            if (x1 > x0)
            {
                g.setColour (colour::lcdText.withAlpha (0.15f));
                g.fillRect (juce::Rectangle<float> (x0, area.getY(), x1 - x0, area.getHeight()));
            }
        }

        g.setColour (colour::lcdFaint);
        g.setFont (lnf != nullptr ? lnf->lcdFont (11.0f) : juce::Font (juce::FontOptions (10.0f)));
        g.drawText (processor.getDisplayName(), b.reduced (4, 2), juce::Justification::topLeft);

        if (isCycleMode)
        {
            auto area = b.toFloat();
            float w = area.getWidth();
            float x0 = area.getX() + cyclePos * w;
            float winW = juce::jmax (2.0f, (cycleLenSamples / juce::jmax (1.0f, (float) buf.size())) * w);
            auto rect = juce::Rectangle<float> (x0, area.getY(), winW, area.getHeight()).getIntersection (area);
            g.setColour (colour::lcdText);
            g.drawRect (rect, 1.5f);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        editButton.setBounds (b.removeFromBottom (16).removeFromRight (36).reduced (1));
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files)
            if (isAudioFile (juce::File (f)))
                return true;
        return false;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragHover = false;
        for (auto& f : files)
        {
            juce::File file (f);
            if (isAudioFile (file))
            {
                juce::String err;
                processor.loadSampleFile (file, err);
                repaint();
                break;
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        // click-to-load only applies when nothing is loaded yet; once a sample is in,
        // a single click does nothing (double-click or EDIT opens the region editor,
        // drag-drop still replaces the sample).
        if (! processor.getSampleBuffer().empty())   // the loader means the SAMPLE
            return;
        // ...and only in the modes that actually consume a sample. Clicking an oscillator
        // shape or a noise readout must never pop a "Load sample..." dialog (v0.19).
        if (! (sourceMode == 0 || sourceMode == 1))
            return;

        chooser = std::make_unique<juce::FileChooser> ("Load sample...", juce::File(),
                                                        "*.wav;*.aif;*.aiff");
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        // SafePointer: the native dialog can outlive this component (host closes the
        // editor while the panel is open), and the callback would then touch freed
        // memory — same guard the TAPE FLIP timer already uses. (v0.12 review)
        juce::Component::SafePointer<WaveformDisplay> safeThis (this);
        chooser->launchAsync (flags, [safeThis] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String err;
                safeThis->processor.loadSampleFile (file, err);
                safeThis->repaint();
            }
        });
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onOpenEditor) onOpenEditor();
    }

private:
    static bool isAudioFile (const juce::File& f)
    {
        return f.hasFileExtension ("wav;aif;aiff");
    }

    void timerCallback() override
    {
        // Tape mode draws the tape take, not the loaded sample (PluginProcessor.h).
        // Message thread only.
        processor.refreshDisplaySource();

        bool changed = false;

        int mode = 0;
        if (auto* modeParam = processor.apvts.getRawParameterValue ("source.mode"))
        {
            mode = (int) modeParam->load();
            if (mode != sourceMode)
            {
                sourceMode = mode;
                changed = true;
                editButton.setTooltip (mode == 2
                    ? "Opens the source window on the oscillator's own shape. Drag on it to "
                      "draw your own wave."
                    : "Zoom, pan, and drag-select the part of the file you want "
                      "\xe2\x80\x94 no destructive editing. Play a MIDI key to hear it live "
                      "while you drag.");
                setTooltip (mode == 2
                    ? "One cycle of the oscillator. Double-click or hit EDIT to open it big "
                      "and draw on it."
                    : "Shows the loaded sample, the region (tinted band), and the cycle "
                      "window. Double-click or hit EDIT to open the region editor.");
            }
            bool cyc = mode == 1; // "Cycle" is index 1 in sourceModes
            if (cyc != isCycleMode) { isCycleMode = cyc; changed = true; }
            bool osc = mode == 2;
            if (osc != isOscMode) { isOscMode = osc; changed = true; }
            if (osc)
            {
                const int om = oscTaps.mode != nullptr ? (int) oscTaps.mode->load() : 0;
                const int ow = oscTaps.wave != nullptr ? (int) oscTaps.wave->load() : 0;
                auto label = om == 2 ? juce::String ("OSC - DRAW")
                           : om == 1 ? juce::String ("OSC - HARMONIC")
                                     : "OSC - " + params::oscWaves[ow].toUpperCase();
                if (label != oscLabel) { oscLabel = label; changed = true; }
                changed = true; // the cycle itself can move with any of 64 points
            }
        }

        // empty/won't-sound explainer, only consulted by paint() when nothing to draw
        {
            const char* newText = "DROP SAMPLE or CLICK";
            switch (mode)
            {
                case 5: // Tape
                    newText = processor.getTapeLength() == 0 ? "TAPE IS EMPTY - REC THEN FLIP"
                                                              : "TAPE TAKE READY";
                    break;
                case 4: newText = "LIVE INPUT - NO MIDI NEEDED"; break; // Input
                case 2: // Osc
                case 3: newText = "PLAY A NOTE"; break;                // Noise
                default: break;                                        // Sample / Cycle
            }
            if (emptyStateText != newText) { emptyStateText = newText; changed = true; }
        }
        if (auto* posParam = processor.apvts.getRawParameterValue ("source.winpos"))
        {
            float pos = posParam->load();
            if (! juce::approximatelyEqual (pos, cyclePos)) { cyclePos = pos; changed = true; }
        }
        if (auto* lenParam = processor.apvts.getRawParameterValue ("source.winlen"))
        {
            float len = lenParam->load();
            if (! juce::approximatelyEqual (len, cycleLenSamples)) { cycleLenSamples = len; changed = true; }
        }

        auto name = processor.getDisplayName();
        if (name != lastName) { lastName = name; changed = true; }

        if (auto* rsParam = processor.apvts.getRawParameterValue ("sample.regstart"))
        {
            float v = rsParam->load();
            if (! juce::approximatelyEqual (v, regStart)) { regStart = v; changed = true; }
        }
        if (auto* reParam = processor.apvts.getRawParameterValue ("sample.regend"))
        {
            float v = reParam->load();
            if (! juce::approximatelyEqual (v, regEnd)) { regEnd = v; changed = true; }
        }

        if (changed)
            repaint();
    }

    // Stripe texture drawn as a loop of hairlines (no cached Image) so paint() makes no
    // new allocations, matching the reference's dark-green-on-darker-green VFD screen.
    static void drawScreenStripes (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (colour::lcdScreenDark);
        for (int y = area.getY(); y < area.getBottom(); y += 3)
            g.drawHorizontalLine (y, (float) area.getX(), (float) area.getRight());
    }

    static void drawThumbnail (juce::Graphics& g, juce::Rectangle<int> area, const std::vector<float>& buf)
    {
        if (buf.empty() || area.getWidth() <= 0)
            return;

        g.setColour (colour::lcdDim); // waveform trace colour per the LCD spec
        const int w = area.getWidth();
        const int h = area.getHeight();
        const int midY = area.getCentreY();
        const int n = (int) buf.size();

        for (int x = 0; x < w; ++x)
        {
            int i0 = (int) ((juce::int64) x * n / w);
            int i1 = (int) ((juce::int64) (x + 1) * n / w);
            i1 = juce::jlimit (i0 + 1, n, i1);
            float peak = 0.0f;
            for (int i = i0; i < i1; ++i)
                peak = juce::jmax (peak, std::abs (buf[(size_t) i]));
            int barH = (int) (peak * (h * 0.5f));
            g.drawVerticalLine (area.getX() + x, (float) (midY - barH), (float) (midY + barH));
        }
    }

    BrokenProcessor& processor;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton editButton;
    int sourceMode = 0;
    bool isCycleMode = false;
    bool isOscMode = false;
    osccurve::Taps oscTaps;
    juce::String oscLabel;
    bool dragHover = false;
    float cyclePos = 0.0f;
    float cycleLenSamples = 0.0f;
    float regStart = 0.0f;
    float regEnd = 1.0f;
    juce::String lastName;
    juce::String emptyStateText { "DROP SAMPLE or CLICK" };
};
} // namespace broken::ui
