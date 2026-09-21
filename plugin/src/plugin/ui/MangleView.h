#pragma once
// "A view" — the MANGLE face. Left-to-right = signal flow: SOURCE -> MANGLE -> PLAY -> TAPE -> OUTPUT.
// Layout matches ClaudeDesign/design_handoff_broken_ui/README.md's "Layout" section: 1520
// design width, 18px side padding, 10px gaps, top-row grid 300 | 620 | 1fr (PLAY+TAPE over
// OUTPUT in the last column).

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "Controls.h"
#include "WaveformDisplay.h"
#include "SourceEditorPanel.h"
#include "PeakMeter.h"
#include "TunerDisplay.h"
#include "../PluginProcessor.h"
#include "../Params.h"

namespace ts::ui
{
class MangleView : public juce::Component, private juce::Timer
{
public:
    explicit MangleView (TurboSynthProcessor& p) : processor (p)
    {
        auto& av = processor.apvts;

        addAndMakeVisible (sourceBlock);
        addAndMakeVisible (mangleBlock);
        addAndMakeVisible (playBlock);
        addAndMakeVisible (tapeBlock);
        addAndMakeVisible (outputBlock);
        mangleBlock.addAndMakeVisible (mangleHairline);

        // ---------------- SOURCE ----------------
        sourceMode = std::make_unique<Combo> (av, "source.mode", params::sourceModes, juce::String(),
            "What gets mangled. CYCLE loops a tiny slice of your sample as a raw oscillator "
            "\xe2\x80\x94 try a real low note.");
        waveform = std::make_unique<WaveformDisplay> (processor);
        sourceEditor = std::make_unique<SourceEditorPanel> (processor);
        pitchKnob = std::make_unique<Knob> (av, "source.pitch", "PITCH",
            "Transpose. Pitching down also slows \xe2\x80\x94 that's the point.", true);
        winPosKnob = std::make_unique<Knob> (av, "source.winpos", "POS",
            "CYCLE only: where in the sample the oscillator window starts.", false);
        winLenKnob = std::make_unique<Knob> (av, "source.winlen", "LEN",
            "CYCLE only: how much of the sample becomes the oscillator.", false);
        inTrimKnob = std::make_unique<Knob> (av, "source.intrim", "IN TRIM",
            "Input mode: level into the chain.", false);
        playToggle = std::make_unique<TextToggle> (av, "play.hold", "PLAY",
            "Plays the sound without a MIDI keyboard, and leaves both hands free for the "
            "knobs. Click again to stop. Pitch comes from PITCH/FINE.", false, "STOP");
        oscWaveCombo = std::make_unique<Combo> (av, "source.oscwave", params::oscWaves, "OSC WAVE",
            "Waveform for the OSC source. The 64-partial Harmonic mode lives in the EDIT view.");
        oscModeCombo = std::make_unique<Combo> (av, "osc.mode", params::oscModes, "OSC MODE",
            "Wave = a preset shape. Harmonic = 64 partials. Draw = draw it by hand \xe2\x80\x94 "
            "open the source window and just start drawing.");
        noiseAmpKnob = std::make_unique<Knob> (av, "noise.amp", "AMP NZ",
            "Noise source: amplitude randomness. 0 with PH NZ 0 = a plain tunable sine.", false);
        noisePhaseKnob = std::make_unique<Knob> (av, "noise.phase", "PH NZ",
            "Noise source: phase randomness. Both at 100 = white noise.", false);
        fineKnob = std::make_unique<Knob> (av, "source.finecents", "FINE",
            "Fine tune, same range as the original's Pitch Shifter fine. TUNE sets it for you.",
            false, true, "c");
        tuneButton.setButtonText ("TUNE");
        tuneButton.setTooltip ("One press: locks the source to the nearest note using the "
                               "IN tuner. Dimmed when no confident pitch.");
        tuneButton.onClick = [this] { processor.applyTuneLock(); };
        sourceBlock.addAndMakeVisible (tuneButton);
        tunerIn = std::make_unique<TunerDisplay> (processor, false, "IN",
            "What's coming in - sample, osc, or live input - before the mangle.");
        tunerOut = std::make_unique<TunerDisplay> (processor, true, "OUT",
            "What's coming out after everything. Chase it with FINE if you want the "
            "wreckage in tune.");
        sourceBlock.addAndMakeVisible (tunerIn.get());
        outputBlock.addAndMakeVisible (tunerOut.get());

        juce::Component* const sourceComps[] = { sourceMode.get(), waveform.get(), pitchKnob.get(),
                                                  winPosKnob.get(), winLenKnob.get(), inTrimKnob.get(),
                                                  oscWaveCombo.get(), oscModeCombo.get(),
                                                  noiseAmpKnob.get(), noisePhaseKnob.get(),
                                                  fineKnob.get(), playToggle.get() };
        for (auto* c : sourceComps)
            sourceBlock.addAndMakeVisible (c);

        // ---------------- MANGLE ----------------
        wsLight  = std::make_unique<LitToggle> (av, "ws.on",  "Waveshaper on/off (hard bypass).");
        modLight = std::make_unique<LitToggle> (av, "mod.on", "Modulator on/off (hard bypass).");
        fltLight = std::make_unique<LitToggle> (av, "flt.on", "Filter on/off (hard bypass).");
        resLight = std::make_unique<LitToggle> (av, "res.on", "Resonator on/off (hard bypass).");
        invLight = std::make_unique<LitToggle> (av, "inv.on", "Spectral inverter on/off (hard bypass).");
        dlyLight = std::make_unique<LitToggle> (av, "dly.on", "Delay on/off (hard bypass).");

        driveKnob = std::make_unique<Knob> (av, "ws.drive", "DRIVE",
            "How hard the sound hits the curve. The main damage control.", true);
        curveCombo = std::make_unique<Combo> (av, "ws.curve", params::wsCurves, "CURVE",
            "Click through the shaper curves like the artist did. 1 is clean.");
        morphKnob = std::make_unique<Knob> (av, "ws.morph", "MORPH",
            "Blend between clean and the selected curve.", false, true, "%",
            [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; });

        modKnob = std::make_unique<Knob> (av, "mod.amount", "MOD",
            "Death-vocal machine: multiplies the sound with a low bell tone. "
            "Turn up, then tune MOD FREQ.", true);
        modFreqKnob = std::make_unique<Knob> (av, "mod.freq", "FREQ",
            "Frequency of the modulating tone. Low = growl, high = metallic.", false, true, "Hz",
            [] (double v) { return (v < 100.0 ? juce::String (v, 1) : juce::String (v, 0)) + " Hz"; });
        modModeCombo = std::make_unique<Combo> (av, "mod.mode", params::modModes, "MODE",
            "How the sound is modulated (AM / RM / FM / PM).");
        modWaveCombo = std::make_unique<Combo> (av, "mod.wave", params::modWaves, "WAVE",
            "What waveform modulates the sound (Osc source).");
        modSourceCombo = std::make_unique<Combo> (av, "mod.source", params::modSources, "SRC",
            "What modulates: the internal osc, the sound itself, the sample, or the tape "
            "- any module as modulator, by design.");

        filterKnob = std::make_unique<Knob> (av, "flt.cutoff", "FILTER",
            "Low-pass only, by design. POLES sets steepness.", true);
        // readout shows the *effective* cutoff: the DSP floors it at 500 Hz unless
        // FLOOR EXT (EDIT view) is on (docs/DSP-NOTES.md §4, PANEL.md Mangle note)
        filterKnob->slider.textFromValueFunction = [this] (double v)
        {
            auto* ext = processor.apvts.getRawParameterValue ("flt.ext");
            if (ext == nullptr || ext->load() < 0.5f)
                v = juce::jmax (500.0, v);
            return juce::String (v, 1);
        };
        filterKnob->slider.updateText();
        if (auto* ext = av.getRawParameterValue ("flt.ext"))
            applyFilterFloorToKnob (ext->load() > 0.5f);
        polesCombo = std::make_unique<Combo> (av, "flt.poles", juce::StringArray { "1", "2", "3", "4" },
            "POLES", "6/12/18/24 dB per octave.");

        resKnob = std::make_unique<Knob> (av, "res.freq", "RES",
            "Ringing comb resonator pitch.", true);
        resFbKnob = std::make_unique<Knob> (av, "res.fb", "FB",
            "Ring length. Negative = hollower.", false, true, "%",
            [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; });

        invertKnob = std::make_unique<Knob> (av, "inv.mix", "INVERT",
            "Spectral flip mix \xe2\x80\x94 mirrors the spectrum. Weird by design.", false, true, "%",
            [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; });

        delayTimeKnob = std::make_unique<Knob> (av, "dly.time", "DELAY",
            "Simple delay with polarity flip (INV button).", false, true, "ms",
            [] (double v) { return (v < 100.0 ? juce::String (v, 1) : juce::String (v, 0)) + " ms"; });
        delayMixKnob = std::make_unique<Knob> (av, "dly.mix", "MIX",
            "Wet level of the delay.", false, true, "%",
            [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; });
        delayInvToggle = std::make_unique<TextToggle> (av, "dly.inv", "INV",
            "Flip delay-tap polarity.");

        juce::Component* const mangleComps[] = {
            wsLight.get(), modLight.get(), fltLight.get(), resLight.get(),
            invLight.get(), dlyLight.get(), driveKnob.get(), curveCombo.get(), morphKnob.get(),
            modKnob.get(), modFreqKnob.get(), modModeCombo.get(), modWaveCombo.get(),
            modSourceCombo.get(),
            filterKnob.get(), polesCombo.get(), resKnob.get(), resFbKnob.get(),
            invertKnob.get(), delayTimeKnob.get(), delayMixKnob.get(), delayInvToggle.get()
        };
        for (auto* c : mangleComps)
            mangleBlock.addAndMakeVisible (c);

        // ---------------- PLAY ----------------
        // MONO | POLY is a SEGMENTED pair per the reference render, not one swap-label
        // button (a lone full-width "MONO" read as a giant orange bar in the screenshot).
        // One parameter, two buttons: each click writes the value, the timer lights the
        // segment that matches — the same pattern as the OscEditor mode strip.
        monoBtn.setButtonText ("MONO");  polyBtn.setButtonText ("POLY");
        monoBtn.setTooltip ("One brutal voice, last-note priority (the workflow).");
        polyBtn.setTooltip ("Six playable voices.");
        auto setVoiceMode = [&av] (float v)
        {
            if (auto* prm = av.getParameter ("voice.mode"))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                prm->endChangeGesture();
            }
        };
        monoBtn.onClick = [setVoiceMode] { setVoiceMode (0.0f); };
        polyBtn.onClick = [setVoiceMode] { setVoiceMode (1.0f); };
        unisonToggle = std::make_unique<TextToggle> (av, "voice.unison", "UNISON",
            "Stacks all 6 voices detuned on one note.");
        spreadKnob = std::make_unique<Knob> (av, "voice.spread", "SPREAD",
            "Unison detune spread.", false);
        ampA = std::make_unique<Knob> (av, "amp.a", "A", "Amp envelope attack.", false);
        ampD = std::make_unique<Knob> (av, "amp.d", "D", "Amp envelope decay.", false);
        ampS = std::make_unique<Knob> (av, "amp.s", "S", "Amp envelope sustain.", false);
        ampR = std::make_unique<Knob> (av, "amp.r", "R", "Amp envelope release.", false);

        // RETRIG + BEND moved here from EDIT view (README Layout: PLAY panel) — both are
        // note/MIDI-trigger behaviour, so they now live beside the rest of the voice
        // controls instead of across the window in EDIT. Same parameter ids, same tooltips.
        voiceRetrig = std::make_unique<TextToggle> (av, "voice.retrig", "RETRIG",
            "Retrigger the amp envelope on legato notes.");
        bendRange = std::make_unique<Knob> (av, "midi.bendrange", "BEND",
            "How far the pitch wheel bends, in semitones. 2 is the usual, 12 is an octave "
            "dive. Set it to 0 to switch the wheel off entirely.", false, true, "st");

        juce::Component* const playComps[] = { &monoBtn, &polyBtn, unisonToggle.get(), spreadKnob.get(),
                                                ampA.get(), ampD.get(), ampS.get(), ampR.get(),
                                                voiceRetrig.get(), bendRange.get() };
        for (auto* c : playComps)
            playBlock.addAndMakeVisible (c);
        playBlock.addAndMakeVisible (playHairline);

        // ---------------- TAPE ----------------
        recButton = std::make_unique<TextToggle> (av, "tape.rec", "REC",
            "Records the output (max 60 s) into the tape.");
        tapeBlock.addAndMakeVisible (recButton.get());

        flipButton.setButtonText ("FLIP");
        flipButton.setTooltip ("Makes the recording the new source. Mangle it again. Generations.");
        flipButton.onClick = [this]
        {
            if (auto* param = processor.apvts.getParameter ("tape.flip"))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (1.0f);
                param->endChangeGesture();
            }
            juce::Component::SafePointer<MangleView> safeThis (this);
            juce::Timer::callAfterDelay (100, [safeThis]
            {
                if (safeThis == nullptr)
                    return;
                if (auto* flipParam = safeThis->processor.apvts.getParameter ("tape.flip"))
                {
                    flipParam->beginChangeGesture();
                    flipParam->setValueNotifyingHost (0.0f);
                    flipParam->endChangeGesture();
                }
            });
        };
        tapeBlock.addAndMakeVisible (flipButton);

        saveButton.setButtonText ("SAVE");
        saveButton.setTooltip ("Saves the latest take as a 24-bit WAV \xe2\x80\x94 no FLIP "
                               "needed. Design, resample, export - like it's 1994. "
                               "(Or just drag the TAKE display out.)");
        saveButton.setEnabled (false);
        saveButton.onClick = [this]
        {
            saveChooser = std::make_unique<juce::FileChooser> (
                "Save tape as WAV",
                juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                    .getChildFile ("broken-take.wav"),
                "*.wav");
            saveChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                          | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::warnAboutOverwriting,
                // SafePointer: the native save panel can outlive this view if the host
                // closes the editor while it's open (v0.12 review)
                [safeThis = juce::Component::SafePointer<MangleView> (this)]
                (const juce::FileChooser& fc)
                {
                    if (safeThis == nullptr) return;
                    auto file = fc.getResult();
                    if (file == juce::File()) return;
                    juce::String err;
                    if (! safeThis->processor.saveTapeToFile (file.withFileExtension ("wav"), err))
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon, "Tape save failed", err);
                });
        };
        tapeBlock.addAndMakeVisible (saveButton);

        // screen-reader disambiguation: two knobs/buttons share silk labels (v0.33)
        mixKnob->setAccessibleTitle ("OUTPUT MIX");
        delayMixKnob->setAccessibleTitle ("DELAY MIX");
        saveButton.setTitle ("SAVE TAPE WAV");
        takeReadout.proc = &processor;
        takeReadout.setTooltip ("The latest take. Drag it straight into your DAW or "
                                "Finder \xe2\x80\x94 it rides out as a 24-bit WAV.");
        tapeBlock.addAndMakeVisible (takeReadout);

        tapeLight.setTooltip ("Lit when TAPE is the active source.");
        tapeLight.dia = 14;        // a lamp, not a stray checkbox (v0.32)
        tapeLight.centred = true;
        tapeBlock.addAndMakeVisible (tapeLight);

        // ---------------- OUTPUT ----------------
        mixKnob = std::make_unique<Knob> (av, "chain.mix", "MIX",
            "Wet/dry. Dry = the un-mangled source, blended in before COLOUR and OUT \xe2\x80\x94 "
            "parallel mangling in every mode.", false, true, "%",
            [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; });
        colourCombo = std::make_unique<Combo> (av, "col.mode", params::colourModes, "COLOUR",
            "The sound of the sampler this would have been rendered to.");
        rateKnob = std::make_unique<Knob> (av, "col.rate", "RATE",
            "Sample-rate crush. No smoothing filter, on purpose.", false, true, "kHz",
            [] (double v) { return juce::String (v / 1000.0, 1) + " kHz"; });
        outKnob = std::make_unique<Knob> (av, "out.level", "OUT", "Master level.", false, true, "dB",
            [] (double v) { if (std::abs (v) < 0.05) v = 0.0; return juce::String (v, 1) + " dB"; });
        bypassToggle = std::make_unique<TextToggle> (av, "bypass", "BYPASS",
            "True bypass: the track passes through untouched (~25 ms fade, no clicks). "
            "Your host's bypass control drives this too.", false, "BYPASSED");
        meter = std::make_unique<PeakMeter> (processor);

        juce::Component* const outputComps[] = { mixKnob.get(), colourCombo.get(), rateKnob.get(),
                                                 outKnob.get(), bypassToggle.get(), meter.get() };
        for (auto* c : outputComps)
            outputBlock.addAndMakeVisible (c);

        // ---------------- SAMPLE EDITOR (POP-OUT WINDOW) ----------------
        // Owned here, but shown inside its own resizable window rather than as a
        // full-panel overlay, so the MANGLE knobs stay visible while you audition a
        // region (docs/PANEL.md). EDIT / double-click on the small waveform opens it.
        sourceEditor->onCloseRequest = [this]
        {
            if (editorWindow != nullptr) editorWindow->setVisible (false);
        };
        sourceEditor->onTitle = [this] (const juce::String& t)
        {
            if (editorWindow != nullptr) editorWindow->setName (t);
        };
        waveform->onOpenEditor = [this] { showSampleEditorWindow(); };

        // match the first timer tick so the row never flashes both sets on open
        {
            const int m0 = (int) av.getRawParameterValue ("source.mode")->load();
            oscWaveCombo->setVisible (m0 == 2);
            oscModeCombo->setVisible (m0 == 2);
            noiseAmpKnob->setVisible (m0 == 3);
            noisePhaseKnob->setVisible (m0 == 3);
        }

        startTimerHz (10);
    }

    // The window must die before the component it points at, and the editor's
    // LookAndFeel (borrowed from the plugin editor below) must be released while that
    // LookAndFeel is still alive.
    ~MangleView() override
    {
        stopTimer();
        editorWindow.reset();
        if (sourceEditor != nullptr) sourceEditor->setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics&) override {}

    // Outer grid per README Layout: 18px side padding, 10px gaps, top row 300 | 620 | 1fr.
    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft (outerPad); area.removeFromRight (outerPad);
        area.removeFromTop (outerGap);  area.removeFromBottom (outerGap);

        auto sourceArea = area.removeFromLeft (300); area.removeFromLeft (outerGap);
        auto mangleArea = area.removeFromLeft (620); area.removeFromLeft (outerGap);
        auto rightArea  = area; // remaining ~544, per README's "1fr"

        // Right column: PLAY + TAPE side by side on top, OUTPUT spanning the full
        // column width below them (README Layout).
        auto rightTop = rightArea.removeFromTop (playTapeRowH);
        rightArea.removeFromTop (outerGap);
        auto outputArea = rightArea;

        auto playArea = rightTop.removeFromLeft (playW);
        rightTop.removeFromLeft (outerGap);
        auto tapeArea = rightTop;

        sourceBlock.setBounds (sourceArea);
        mangleBlock.setBounds (mangleArea);
        playBlock.setBounds (playArea);
        tapeBlock.setBounds (tapeArea);
        outputBlock.setBounds (outputArea);

        // Children live inside each Block, so layout in that Block's *local* coordinate space
        // (origin 0,0), not MangleView's — setBounds() above already positioned the Block itself.
        auto local = [] (juce::Rectangle<int> r) { return r.withPosition (0, 0); };
        layoutSource (local (sourceArea));
        layoutMangle (local (mangleArea));
        layoutPlay (local (playArea));
        layoutTape (local (tapeArea));
        layoutOutput (local (outputArea));
    }

private:
    static constexpr int outerPad = 18; // README: 18px side padding
    static constexpr int outerGap = 10; // README: 10px gaps
    static constexpr int headerPad = 20; // Block title reserves this much at the top

    // Right column split: PLAY (wider — MONO/POLY, ADSR row, RETRIG+BEND) sits left of
    // the narrower TAPE column; OUTPUT spans the full 544-ish width below both.
    static constexpr int playW = 310;
    // PLAY/TAPE hold ~330 of content; OUTPUT was clipping mid-knob because this row
    // hoarded the column height while OUTPUT got the scraps (v0.29 screenshot review)
    static constexpr int playTapeRowH = 350;

    // Knob box heights = label(11, if titled) + dial + textbox(14, if it has a readout).
    // Passing a WIDTH equal to the dial size (not the wider column) is what pins the
    // rendered dial to that exact geom:: size — see Knob/TsLookAndFeel::drawRotarySlider.
    static constexpr int xlBoxH = 13 + geom::knobXL + 16; // 95: DRIVE/MOD/FILTER/RES (+readout)
    static constexpr int lBoxH  = 13 + geom::knobL + 16;  // 75: MORPH/FREQ/FB, INVERT/DELAY/MIX (+LCD readout, v0.32)
    static constexpr int pitchDial = 50, pitchBoxH = 13 + 50 + 16; // 79: PITCH (upsized v0.30)
    static constexpr int fineBoxH  = 13 + geom::knobS + 16;        // 69: FINE ("c" readout)
    static constexpr int sBoxH     = 13 + geom::knobS;             // 53: POS/LEN/IN TRIM, SPREAD, ADSR, BEND

    // The FILTER knob's travel stops at the floor the DSP enforces (500 Hz unless FLOOR
    // EXT), so there is no dead zone where the knob keeps turning while the readout sits
    // at 500.0 and nothing changes. The parameter keeps its full 20..20 kHz range for
    // presets/automation; the slider is just not allowed to be dragged below the floor.
    // Same log feel as Params.h's logRange (skew centred on the geometric mean).
    void applyFilterFloorToKnob (bool extOn)
    {
        const double lo = extOn ? 20.0 : 500.0, hi = 20000.0;
        juce::NormalisableRange<double> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        filterKnob->slider.setNormalisableRange (r);

        // EXT switched off with the cutoff already below the floor: pull the parameter up
        // so knob, readout and DSP agree instead of the knob clamping against a value the
        // host still holds lower
        if (! extOn)
            if (auto* p = processor.apvts.getParameter ("flt.cutoff"))
                if (p->convertFrom0to1 (p->getValue()) < 500.0f)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (500.0f));
                    p->endChangeGesture();
                }
    }

    void layoutSource (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        r.removeFromTop (headerPad);

        sourceMode->setBounds (r.removeFromTop (26));
        r.removeFromTop (8);
        waveform->setBounds (r.removeFromTop (150)); // taller than the README's 118: it is SOURCE's centrepiece and the panel had the void to spend (v0.30)
        r.removeFromTop (8);
        playToggle->setBounds (r.removeFromTop (30)); // README: PLAY button 30
        r.removeFromTop (10);

        // PITCH + FINE + TUNE in three even columns on ONE baseline. The old cluster had
        // three different vertical centres (PITCH low, FINE high, TUNE floating) and
        // ragged horizontal gaps — the "sloppy" the user called out (v0.31).
        auto pitchRow = r.removeFromTop (pitchBoxH);
        {
            const int col = pitchRow.getWidth() / 3;
            pitchKnob->setBounds (pitchRow.removeFromLeft (col).withSizeKeepingCentre (juce::jmin (col, 66), pitchBoxH));
            fineKnob->setBounds (pitchRow.removeFromLeft (col).withSizeKeepingCentre (juce::jmin (col, 64), pitchBoxH));
            tuneButton.setBounds (pitchRow.withSizeKeepingCentre (juce::jmin (pitchRow.getWidth() - 8, 90), 30));
        }
        r.removeFromTop (14);

        // POS / LEN / IN TRIM in the same three columns, so the two rows grid-align
        auto smallRow = r.removeFromTop (sBoxH);
        int colW = smallRow.getWidth() / 3;
        winPosKnob->setBounds (smallRow.removeFromLeft (colW).withSizeKeepingCentre (juce::jmin (colW, 60), sBoxH));
        winLenKnob->setBounds (smallRow.removeFromLeft (colW).withSizeKeepingCentre (juce::jmin (colW, 60), sBoxH));
        inTrimKnob->setBounds (smallRow.withSizeKeepingCentre (juce::jmin (colW, 60), sBoxH));

        // OSC WAVE / AMP NZ / PH NZ: mutually exclusive with the row above by mode.
        // Tucked right under POS/LEN/IN TRIM (v0.32): in Sample/Input/Tape modes this
        // row is INVISIBLE, and the v0.31 half/half slack split left a hole mid-panel.
        // One void above the pinned tuner reads as breathing room; two read as holes.
        r.removeFromTop (10);
        auto modeRow = r.removeFromTop (sBoxH);
        {   // OSC: WAVE | MODE
            auto row = modeRow;
            auto a = row.removeFromLeft (row.getWidth() / 2);
            oscWaveCombo->setBounds (a.withSizeKeepingCentre (a.getWidth() - 6, 33));
            oscModeCombo->setBounds (row.withSizeKeepingCentre (row.getWidth() - 6, 33));
        }
        {   // NOISE: AMP NZ | PH NZ — same rectangle; only one set is ever visible
            auto row = modeRow;
            noiseAmpKnob->setBounds (row.removeFromLeft (row.getWidth() / 2).withSizeKeepingCentre (geom::knobS, sBoxH));
            noisePhaseKnob->setBounds (row.withSizeKeepingCentre (geom::knobS, sBoxH));
        }

        // IN tuner pinned to the panel bottom (README) — whatever's left of `r` above this
        // point is the flexible gap, exactly like the design's own flex:1 spacer.
        tunerIn->setBounds (r.removeFromBottom (46));
    }

    void layoutMangle (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        // v0.31: the rows used to top-pack and pool ~150px of void under row 4. Spread
        // the spare height into the three inter-row gaps (capped so rows stay grouped).
        const int mangleFixedH = headerPad + (headerPad + 6 + xlBoxH) + 16 + 33
                               + (14 + 2 + 14) + lBoxH + 16 + (headerPad + 6 + lBoxH);
        const int extra = juce::jlimit (0, 40, (r.getHeight() - mangleFixedH) / 3);
        r.removeFromTop (headerPad);

        // Row 1: DRIVE / MOD / FILTER / RES — 60px dials (geom::knobXL), LED above,
        // LCD readout below (the big-knob Knob already draws it). One 4-column grid that
        // rows 3 and 4 reuse below, so every row's knobs line up vertically.
        auto topRow = r.removeFromTop (headerPad + 6 + xlBoxH);
        int colW = topRow.getWidth() / 4;
        auto driveCol = topRow.removeFromLeft (colW);
        auto modCol   = topRow.removeFromLeft (colW);
        auto fltCol   = topRow.removeFromLeft (colW);
        auto resCol   = topRow;

        layoutModuleColumn (driveCol, *wsLight, [this] (juce::Rectangle<int> c)
        { driveKnob->setBounds (c.withSizeKeepingCentre (geom::knobXL, xlBoxH)); });
        layoutModuleColumn (modCol, *modLight, [this] (juce::Rectangle<int> c)
        { modKnob->setBounds (c.withSizeKeepingCentre (geom::knobXL, xlBoxH)); });
        layoutModuleColumn (fltCol, *fltLight, [this] (juce::Rectangle<int> c)
        { filterKnob->setBounds (c.withSizeKeepingCentre (geom::knobXL, xlBoxH)); });
        layoutModuleColumn (resCol, *resLight, [this] (juce::Rectangle<int> c)
        { resKnob->setBounds (c.withSizeKeepingCentre (geom::knobXL, xlBoxH)); });

        r.removeFromTop (16 + extra);

        // Row 2: CURVE (150) / MODE / WAVE / SRC (72 each) / POLES (104) — all on one row,
        // left-aligned, per the screenshot (they no longer nest under row 1's columns).
        // combos anchored to the same 4-column grid as the knobs so each reads as
        // belonging to its module: CURVE under DRIVE, the MODE/WAVE/SRC trio starting
        // under MOD, POLES right-aligned under FILTER. RES has no dropdown; its column
        // staying empty is honest. (v0.32 \xe2\x80\x94 the row used to left-pack.)
        auto ddRow = r.removeFromTop (33);
        const int ddColW = ddRow.getWidth() / 4;
        curveCombo->setBounds (ddRow.getX(), ddRow.getY(), juce::jmin (ddColW - 8, 144), 33);
        int tx = ddRow.getX() + ddColW;
        for (auto* c : { modModeCombo.get(), modWaveCombo.get(), modSourceCombo.get() })
        {
            c->setBounds (tx, ddRow.getY(), 64, 33);
            tx += 64 + 6;
        }
        polesCombo->setBounds (ddRow.getX() + ddColW * 3 - 100, ddRow.getY(), 96, 33);

        r.removeFromTop (14 + extra / 2);
        mangleHairline.setBounds (r.removeFromTop (2));
        r.removeFromTop (14 + extra / 2);

        // Row 3: MORPH / FREQ / FB — 42px dials (geom::knobL) in columns 1-3.
        auto rowB = r.removeFromTop (lBoxH);
        colW = rowB.getWidth() / 4;
        morphKnob->setBounds (rowB.removeFromLeft (colW).withSizeKeepingCentre (geom::knobL, lBoxH));
        modFreqKnob->setBounds (rowB.removeFromLeft (colW).withSizeKeepingCentre (geom::knobL, lBoxH));
        resFbKnob->setBounds (rowB.removeFromLeft (colW).withSizeKeepingCentre (geom::knobL, lBoxH));

        r.removeFromTop (16 + extra);

        // Row 4: INVERT / DELAY (each with its LED) / MIX — 42px dials in columns 1-3,
        // the INV (polarity) toggle in column 4. Same 4-column grid as row 1.
        auto rowC = r.removeFromTop (headerPad + 6 + lBoxH);
        colW = rowC.getWidth() / 4;
        auto invCol = rowC.removeFromLeft (colW);
        auto dlyCol = rowC.removeFromLeft (colW);
        auto mixCol = rowC.removeFromLeft (colW);
        auto invBtnCol = rowC;

        layoutModuleColumn (invCol, *invLight, [this] (juce::Rectangle<int> c)
        { invertKnob->setBounds (c.withSizeKeepingCentre (geom::knobL, lBoxH)); });
        layoutModuleColumn (dlyCol, *dlyLight, [this] (juce::Rectangle<int> c)
        { delayTimeKnob->setBounds (c.withSizeKeepingCentre (geom::knobL, lBoxH)); });
        mixCol.removeFromTop (headerPad + 6); // blank spacer matching the LED row above, for alignment
        delayMixKnob->setBounds (mixCol.withSizeKeepingCentre (geom::knobL, lBoxH));
        invBtnCol.removeFromTop (headerPad + 6);
        delayInvToggle->setBounds (invBtnCol.withSizeKeepingCentre (juce::jmin (invBtnCol.getWidth(), 60), 24));
    }

    // Draws the on/off light above a module's controls, then hands the remaining rect to `layoutFn`.
    template <typename LayoutFn>
    static void layoutModuleColumn (juce::Rectangle<int> col, LitToggle& light, LayoutFn&& layoutFn)
    {
        auto lightRow = col.removeFromTop (headerPad + 6);
        // centred over the knob so the lamp reads as THAT module's switch, not corner
        // decoration; bounds are ~2x the lamp so it is clickable by mortals (the LED
        // paints small and centred inside whatever bounds it gets)
        light.setBounds (lightRow.withSizeKeepingCentre (22, 22));
        col.removeFromTop (2);
        layoutFn (col);
    }

    void layoutPlay (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        // v0.31: distribute the spare height into the inter-row gaps (was pooling under
        // RETRIG/BEND), same treatment as MANGLE.
        const int playFixedH = headerPad + 26 + 10 + sBoxH + 14 + sBoxH
                             + (8 + 2 + 8) + sBoxH + 16; // +16: BEND readout (v0.32)
        const int extra = juce::jlimit (0, 28, (r.getHeight() - playFixedH) / 3);
        r.removeFromTop (headerPad);

        {   // segmented MONO | POLY: two equal halves, 2 px apart
            auto seg = r.removeFromTop (26);
            monoBtn.setBounds (seg.removeFromLeft (seg.getWidth() / 2 - 1));
            seg.removeFromLeft (2);
            polyBtn.setBounds (seg);
        }
        r.removeFromTop (10 + extra);

        auto unisonRow = r.removeFromTop (sBoxH);
        unisonToggle->setBounds (unisonRow.removeFromLeft (70).withSizeKeepingCentre (70, 24));
        unisonRow.removeFromLeft (14);
        spreadKnob->setBounds (unisonRow.removeFromLeft (50).withSizeKeepingCentre (geom::knobS, sBoxH));
        r.removeFromTop (14 + extra);

        // A D S R — 34px dials, one row (README: "A D S R row (34-px dials)")
        auto adsrRow = r.removeFromTop (sBoxH);
        int colW = adsrRow.getWidth() / 4;
        ampA->setBounds (adsrRow.removeFromLeft (colW).withSizeKeepingCentre (geom::knobS, sBoxH));
        ampD->setBounds (adsrRow.removeFromLeft (colW).withSizeKeepingCentre (geom::knobS, sBoxH));
        ampS->setBounds (adsrRow.removeFromLeft (colW).withSizeKeepingCentre (geom::knobS, sBoxH));
        ampR->setBounds (adsrRow.withSizeKeepingCentre (geom::knobS, sBoxH));

        r.removeFromTop (8 + extra / 2);
        playHairline.setBounds (r.removeFromTop (2));
        r.removeFromTop (8 + extra / 2);

        // RETRIG + BEND (moved from EDIT view)
        auto retrigRow = r.removeFromTop (sBoxH + 16); // taller: BEND has an LCD readout
        voiceRetrig->setBounds (retrigRow.removeFromLeft (110).withSizeKeepingCentre (110, 24));
        retrigRow.removeFromLeft (14);
        bendRange->setBounds (retrigRow.removeFromLeft (56).withSizeKeepingCentre (56, sBoxH + 16));
    }

    void layoutTape (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        // v0.31: transport buttons up 32 -> 36 (they are the panel's whole job) and the
        // spare height spread across the four gaps instead of pooling under the lamp.
        const int tapeFixedH = headerPad + 36 * 3 + 10 + 10 + 14 + 24 + 14 + 24;
        const int extra = juce::jlimit (0, 26, (r.getHeight() - tapeFixedH) / 4);
        r.removeFromTop (headerPad);

        recButton->setBounds (r.removeFromTop (36));
        r.removeFromTop (10 + extra);
        flipButton.setBounds (r.removeFromTop (36));
        r.removeFromTop (10 + extra);
        saveButton.setBounds (r.removeFromTop (36));
        r.removeFromTop (14 + extra);
        takeReadout.setBounds (r.removeFromTop (24));
        r.removeFromTop (14 + extra);
        // the lamp belongs WITH the transport it reports on, not haunting the panel's
        // far corner like a stray checkbox (v0.30 screenshot review)
        tapeLight.setBounds (r.removeFromTop (24));
    }

    void layoutOutput (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        r.removeFromTop (headerPad);

        // Meter spans the FULL remaining panel height at the far right (README); the rest
        // of the row (combo/knobs/tuner) is a fixed-height strip vertically centred in it.
        auto meterCol = r.removeFromRight (16); // was a sliver at 13
        meter->setBounds (meterCol);
        r.removeFromRight (8);

        auto row = r.withSizeKeepingCentre (r.getWidth(), 69);
        // left->right mirrors signal flow: blend, colour, crush rate, level, bypass
        mixKnob->setBounds (row.removeFromLeft (56).withSizeKeepingCentre (52, 69));
        row.removeFromLeft (10);
        colourCombo->setBounds (row.removeFromLeft (130).withSizeKeepingCentre (130, 53));
        row.removeFromLeft (12);
        rateKnob->setBounds (row.removeFromLeft (56).withSizeKeepingCentre (56, 69));
        row.removeFromLeft (8);
        outKnob->setBounds (row.removeFromLeft (56).withSizeKeepingCentre (56, 69));
        row.removeFromLeft (12);
        bypassToggle->setBounds (row.removeFromLeft (86).withSizeKeepingCentre (86, 26));
        row.removeFromLeft (12);
        tunerOut->setBounds (row.withSizeKeepingCentre (row.getWidth(), 53));
    }

    void timerCallback() override
    {
        // conditional enable: WINDOW POS/LEN only for Cycle, IN TRIM only for Input
        if (auto* modeParam = processor.apvts.getRawParameterValue ("source.mode"))
        {
            int mode = (int) modeParam->load();
            bool cycle = mode == 1; // Cycle
            bool osc   = mode == 2; // Osc
            bool noise = mode == 3; // Noise
            bool input = mode == 4; // Input
            bool tape  = mode == 5; // Tape

            winPosKnob->setActive (cycle);
            winLenKnob->setActive (cycle);
            inTrimKnob->setActive (input);
            playToggle->setEnabled (! input);
            playToggle->setAlpha (input ? 0.4f : 1.0f);
            // segmented MONO|POLY follows the parameter (host automation included)
        if (auto* vm = processor.apvts.getRawParameterValue ("voice.mode"))
        {
            const bool poly = vm->load() > 0.5f;
            if (monoBtn.getToggleState() == poly) // i.e. wrong segment lit
            {
                monoBtn.setToggleState (! poly, juce::dontSendNotification);
                polyBtn.setToggleState (poly, juce::dontSendNotification);
            }
        }

        // This row is the ONE place the panel swaps content instead of greying:
            // OSC WAVE/OSC MODE and AMP NZ/PH NZ share the rectangle and never apply at
            // the same time, so greying would leave two dead controls on top of two live
            // ones (docs/PANEL.md).
            oscWaveCombo->setVisible (osc);
            oscModeCombo->setVisible (osc);
            noiseAmpKnob->setVisible (noise);
            noisePhaseKnob->setVisible (noise);

            if (tape != tapeLight.on)
            {
                tapeLight.on = tape;
                tapeLight.repaint();
            }
        }

        // FILTER readout depends on flt.ext, whose toggle lives in the EDIT view — the
        // slider never sees that change, so refresh the text ourselves when it flips
        if (auto* ext = processor.apvts.getRawParameterValue ("flt.ext"))
        {
            bool extOn = ext->load() > 0.5f;
            if (extOn != lastFltExt)
            {
                lastFltExt = extOn;
                applyFilterFloorToKnob (extOn);
                filterKnob->slider.updateText();
            }
        }

        // SAVE only makes sense once a take exists (post-FLIP active tape)
        saveButton.setEnabled (processor.getLatestTakeLength() > 0);
        tuneButton.setEnabled (tunerIn != nullptr && tunerIn->hasPitch());
        tuneButton.setAlpha (tuneButton.isEnabled() ? 1.0f : 0.4f);

        // TAKE readout: "TAKE --" with nothing recorded, else "TAKE <secs>s" — length is in
        // samples (TapeBuffer::activeLength), so it needs the tape's OWN rate, not a
        // hardcoded 48000. There's no dedicated processor accessor for that, so this falls
        // back to the processor's current sample rate (AudioProcessor::getSampleRate()),
        // which is what the tape was recorded at.
        {
            const auto len = processor.getLatestTakeLength();
            if (len != lastTapeLen)
            {
                lastTapeLen = len;
                if (len == 0)
                    takeReadout.text = "TAKE --";
                else
                {
                    const double sr = processor.getSampleRate();
                    const double seconds = sr > 0.0 ? (double) len / sr : 0.0;
                    takeReadout.text = "TAKE " + juce::String (seconds, 1) + "s";
                }
                takeReadout.repaint();
            }
        }

        // REC blink while actually recording (steady lit = tape.rec on; blink = engine capturing)
        bool recording = processor.isTapeRecording();
        if (recording)
        {
            if (++blinkCounter >= 5) { blinkCounter = 0; blinkOn = ! blinkOn; }
            recButton->button.setAlpha (blinkOn ? 1.0f : 0.45f);
        }
        else
        {
            blinkCounter = 0;
            blinkOn = true;
            recButton->button.setAlpha (1.0f);
        }
    }

    // Small filled/outline indicator, not user-clickable (state driven purely by source.mode).
    // (the old nested Light here drew a SQUARE outline — the "stray checkbox" look from
    // the screenshot reviews. Deleted v0.32; the tape lamp now uses ui::Light from
    // Theme.h, the same round LED the module columns use.)

    // 1px inner divider (colour::ruleInner) — MANGLE (row2/row3 split) and PLAY
    // (ADSR/RETRIG split) per the README layout.
    struct Rule : public juce::Component
    {
        void paint (juce::Graphics& g) override { g.setColour (colour::ruleInner); g.fillRect (getLocalBounds()); }
    };

    // "TAKE --" / "TAKE <n>s" LCD label (README: TAPE panel) — and a drag source:
    // drag it into your DAW/Finder and the latest take rides out as a WAV (v0.33).
    struct TakeReadout : public juce::Component, public juce::SettableTooltipClient
    {
        juce::String text = "TAKE --";
        TurboSynthProcessor* proc = nullptr;
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds();
            g.setColour (colour::lcdBg);
            g.fillRect (b);
            g.setColour (colour::lcdBorder);
            g.drawRect (b, 1);
            g.setColour (colour::lcdDim);
            g.setFont (juce::Font (juce::FontOptions (14.0f)));
            g.drawText (text, b.reduced (8, 2), juce::Justification::centredLeft);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging || proc == nullptr || proc->getLatestTakeLength() == 0) return;
            if (e.getDistanceFromDragStart() < 8) return; // don't fire on a twitchy click
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("Broken Takes");
            dir.createDirectory();
            auto file = dir.getNonexistentChildFile ("broken-take", ".wav");
            juce::String err;
            if (! proc->saveTapeToFile (file, err)) return;
            dragging = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { file.getFullPathName() }, false, this,
                [this] (auto&&...) { dragging = false; });
        }
        bool dragging = false; // one drag per gesture; macOS reenters mouseDrag during it
    };

    TurboSynthProcessor& processor;

    Block sourceBlock { "SOURCE" }, mangleBlock { "MANGLE" }, playBlock { "PLAY" },
          tapeBlock { "TAPE" }, outputBlock { "OUTPUT" };
    Rule mangleHairline, playHairline;

    // Source
    std::unique_ptr<Combo> sourceMode;
    std::unique_ptr<WaveformDisplay> waveform;
    // Creates the window lazily on first open, then just re-shows and fronts it, so the
    // user's zoom/scroll/selection survive closing it. A detached window does not
    // inherit the plugin editor's LookAndFeel through the parent chain, so it is set
    // explicitly here (getLookAndFeel() still resolves via this view's parent).
    void showSampleEditorWindow()
    {
        if (editorWindow == nullptr)
        {
            sourceEditor->setLookAndFeel (&getLookAndFeel());
            editorWindow = std::make_unique<SampleEditorWindow> (*sourceEditor, this);
        }
        editorWindow->setVisible (true);
        editorWindow->toFront (true);
        sourceEditor->show();
    }

    std::unique_ptr<SourceEditorPanel> sourceEditor;
    // declared AFTER sourceEditor so it is destroyed FIRST (it holds a non-owning
    // pointer to that component)
    std::unique_ptr<SampleEditorWindow> editorWindow;


    std::unique_ptr<Knob> pitchKnob, winPosKnob, winLenKnob, inTrimKnob, fineKnob;
    std::unique_ptr<Knob> mixKnob;
    std::unique_ptr<TextToggle> bypassToggle;
    std::unique_ptr<Combo> oscWaveCombo;
    std::unique_ptr<Combo> oscModeCombo;
    std::unique_ptr<Knob> noiseAmpKnob, noisePhaseKnob;
    juce::TextButton tuneButton;
    std::unique_ptr<TunerDisplay> tunerIn, tunerOut;

    // Mangle
    std::unique_ptr<LitToggle> wsLight, modLight, fltLight, resLight, invLight, dlyLight;
    std::unique_ptr<Knob> driveKnob, morphKnob, modKnob, modFreqKnob, filterKnob, resKnob, resFbKnob,
                           invertKnob, delayTimeKnob, delayMixKnob;
    std::unique_ptr<Combo> curveCombo, modModeCombo, modWaveCombo, modSourceCombo, polesCombo;
    std::unique_ptr<TextToggle> delayInvToggle;

    // Play
    std::unique_ptr<TextToggle> playToggle;
    juce::TextButton monoBtn, polyBtn;
    std::unique_ptr<TextToggle> unisonToggle;
    std::unique_ptr<Knob> spreadKnob, ampA, ampD, ampS, ampR;
    // moved from EditView (was: VOICE block) — same ids, same tooltips
    std::unique_ptr<TextToggle> voiceRetrig;
    std::unique_ptr<Knob> bendRange;

    // Tape
    std::unique_ptr<TextToggle> recButton;
    juce::TextButton flipButton;
    juce::TextButton saveButton;
    std::unique_ptr<juce::FileChooser> saveChooser;
    TakeReadout takeReadout;
    size_t lastTapeLen = (size_t) -1; // force the first timer tick to set the text
    Light tapeLight;
    int blinkCounter = 0;
    bool blinkOn = true;
    bool lastFltExt = false;

    // Output
    std::unique_ptr<Combo> colourCombo;
    std::unique_ptr<Knob> rateKnob, outKnob;
    std::unique_ptr<PeakMeter> meter;
};
} // namespace ts::ui
