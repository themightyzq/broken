#pragma once
// "B view" — EDIT. The deep parameter set PANEL.md doesn't put on the MANGLE face, laid
// out per ClaudeDesign/design_handoff_broken_ui/README.md's bottom-row grid: three equal
// columns (ENVELOPES / OSCILLATOR / WAVESHAPER), then MODULE TRIMS spanning two columns
// with TIME in the third. RETRIGGER/BEND moved to MangleView's PLAY panel; the old 12
// blocks (filterEnv/auxEnv/cycleOsc/mod/delay/res/curve/voice/filter/harmonics/time/
// colourNoise) are consolidated into these 5.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "Controls.h"
#include "CurveEditor.h"
#include "HarmonicEditor.h"
#include "../PluginProcessor.h"
#include "../Params.h"

namespace broken::ui
{
class EditView : public juce::Component
{
public:
    explicit EditView (BrokenProcessor& p) : processor (p)
    {
        auto& av = processor.apvts;

        for (auto* b : { &envelopesBlock, &oscillatorBlock, &waveshaperBlock,
                          &trimsBlock, &timeBlock })
            addAndMakeVisible (b);
        envelopesBlock.addAndMakeVisible (envHairline);
        for (auto* d : { &trimDiv1, &trimDiv2, &trimDiv3, &trimDiv4 })
            trimsBlock.addAndMakeVisible (d);
        for (auto* l : { &trimCaptionFm, &trimCaptionDly, &trimCaptionRes,
                          &trimCaptionInv, &trimCaptionNz })
        {
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            l->setColour (juce::Label::textColourId, colour::silkCaption);
            trimsBlock.addAndMakeVisible (l);
        }
        trimCaptionFm.setText ("FM", juce::dontSendNotification);
        trimCaptionDly.setText ("DELAY", juce::dontSendNotification);
        trimCaptionRes.setText ("RESONATOR", juce::dontSendNotification);
        trimCaptionInv.setText ("INVERT", juce::dontSendNotification);
        trimCaptionNz.setText ("NOISE", juce::dontSendNotification);
        for (auto* l : { &envCaptionFlt, &envCaptionAux })
        {
            l->setJustificationType (juce::Justification::centredLeft);
            l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            l->setColour (juce::Label::textColourId, colour::silkCaption);
            envelopesBlock.addAndMakeVisible (l);
        }
        envCaptionFlt.setText ("FLT", juce::dontSendNotification);
        envCaptionAux.setText ("AUX", juce::dontSendNotification);

        // ---------------- ENVELOPES: FLT row + AUX row, shared knob grid ----------------
        fenvA = std::make_unique<Knob> (av, "fenv.a", "A", "Filter envelope attack.", false);
        fenvD = std::make_unique<Knob> (av, "fenv.d", "D", "Filter envelope decay.", false);
        fenvS = std::make_unique<Knob> (av, "fenv.s", "S", "Filter envelope sustain.", false);
        fenvR = std::make_unique<Knob> (av, "fenv.r", "R", "Filter envelope release.", false);
        fltEnvAmt = std::make_unique<Knob> (av, "flt.envamt", "AMT",
            "Filter envelope amount, \xc2\xb1" "60 semitones.", false);
        // moved here from the old FILTER block (README: ENVELOPES row FLT)
        fltExt = std::make_unique<TextToggle> (av, "flt.ext", "FLOOR EXT",
            "Extends the filter's low-end floor below the era-correct 500 Hz.");
        for (auto* c : { (juce::Component*) fenvA.get(), (juce::Component*) fenvD.get(),
                          (juce::Component*) fenvS.get(), (juce::Component*) fenvR.get(),
                          (juce::Component*) fltEnvAmt.get(), (juce::Component*) fltExt.get() })
            envelopesBlock.addAndMakeVisible (c);

        auxA = std::make_unique<Knob> (av, "aux.a", "A", "Aux envelope attack.", false);
        auxD = std::make_unique<Knob> (av, "aux.d", "D", "Aux envelope decay.", false);
        auxS = std::make_unique<Knob> (av, "aux.s", "S", "Aux envelope sustain.", false);
        auxR = std::make_unique<Knob> (av, "aux.r", "R", "Aux envelope release.", false);
        auxAmount = std::make_unique<Knob> (av, "aux.amount", "AMT", "Aux envelope amount.", false);
        auxDest = std::make_unique<Combo> (av, "aux.dest", params::auxDests, "DEST",
            "Patch the aux envelope onto FilterCut / ResFreq / InvMix / ModAmt / Pitch.");
        juce::Component* const auxComps[] = { auxA.get(), auxD.get(), auxS.get(), auxR.get(),
                                               auxAmount.get(), auxDest.get() };
        for (auto* c : auxComps)
            envelopesBlock.addAndMakeVisible (c);

        // ---------------- OSCILLATOR: HARMONICS + CYCLE/OSC consolidated ----------------
        // In FX the OSCILLATOR block no longer feeds the SOURCE (FX source is always
        // Input) -- it exists so the Table mod source (item 2) has a shape to read.
        // Tooltips say so; the controls and the harmonic-table math underneath are
        // otherwise identical to the instrument.
        harmonicEditor = std::make_unique<HarmonicEditor> (av);
#if BROKEN_FX
        harmonicEditor->setTooltip ("The 64 partials that feed the Table mod source when "
            "MOD SRC is TABLE (MOD MODE/SRC in MANGLE). Drag bars to draw; hold Shift to "
            "rake a straight line across them.");
#endif
        oscillatorBlock.addAndMakeVisible (harmonicEditor.get());

#if BROKEN_FX
        oscMode = std::make_unique<Combo> (av, "osc.mode", params::oscModes, "OSC MODE",
            "Which shape the Table mod source reads: Wave (a preset waveform), Harmonic "
            "(the 64 partials below) or Draw (the hand-drawn shape). Only matters when "
            "MOD SRC is TABLE.");
        oscWave = std::make_unique<Combo> (av, "source.oscwave", params::oscWaves, "OSC WAVE",
            "Waveform the Table mod source reads when OSC MODE is Wave.");
#else
        oscMode = std::make_unique<Combo> (av, "osc.mode", params::oscModes, "OSC MODE",
            "Wave picks a preset waveform; Harmonic builds the sound from 64 partials; "
            "Draw lets you draw the shape by hand in the source window.");
        oscWave = std::make_unique<Combo> (av, "source.oscwave", params::oscWaves, "OSC WAVE",
            "Waveform used by the OSC source mode.");
#endif
        oscillatorBlock.addAndMakeVisible (oscMode.get());
        oscillatorBlock.addAndMakeVisible (oscWave.get());

        sawButton.setTooltip ("Set all 64 partials to the classic 1/k sawtooth recipe.");
        sawButton.onClick = [&av]
        {
            for (int k = 1; k <= params::harmonicCount; ++k)
            {
                if (auto* param = av.getParameter (params::harmonicId (k)))
                {
                    float v = 100.0f / (float) k;
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 (v));
                    param->endChangeGesture();
                }
            }
        };
        oscillatorBlock.addAndMakeVisible (sawButton);

        squareButton.setTooltip ("Set odd partials to 1/k, mute the even partials.");
        squareButton.onClick = [&av]
        {
            for (int k = 1; k <= params::harmonicCount; ++k)
            {
                if (auto* param = av.getParameter (params::harmonicId (k)))
                {
                    float v = (k % 2 != 0) ? 100.0f / (float) k : 0.0f;
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 (v));
                    param->endChangeGesture();
                }
            }
        };
        oscillatorBlock.addAndMakeVisible (squareButton);

        flatShapeButton.setTooltip ("Set all 64 partials to full amplitude.");
        flatShapeButton.onClick = [&av]
        {
            for (int k = 1; k <= params::harmonicCount; ++k)
            {
                if (auto* param = av.getParameter (params::harmonicId (k)))
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 (100.0f));
                    param->endChangeGesture();
                }
            }
        };
        oscillatorBlock.addAndMakeVisible (flatShapeButton);

        cycleXfade = std::make_unique<Knob> (av, "source.xfade", "XFADE",
            "CYCLE seam crossfade, 0\xe2\x80\x93" "16 samples.", false);
        pitchMix = std::make_unique<Knob> (av, "source.pitchmix", "PITCH MIX",
            "Blend of pitched vs unpitched playback - the original Pitch Shifter's Mix. "
            "~50% with FINE detune = the spec's chorus recipe.", false);
        sourceExt = std::make_unique<TextToggle> (av, "source.ext", "PITCH EXT",
            "Extends PITCH range to \xc2\xb1" "48 semitones.");
        // moved here from the old COLOUR/NOISE block, relabelled "LOOP XFADE" (README:
        // OSCILLATOR panel) — same id (sample.xfadeshape)
        xfadeShape = std::make_unique<Combo> (av, "sample.xfadeshape", params::xfadeShapes, "LOOP XFADE",
            "Loop crossfade shape. Equal-power holds the level through the seam.");
        // item 2 FX check: XFADE (CYCLE-only), PITCH MIX (Sample/Tape-only) and LOOP XFADE
        // (Sample/Tape loop-seam shape) all read parameters SourceEngine only consults for
        // Sample/Cycle/Tape source modes, and the FX source is forced to Input -- they do
        // nothing there, so they are hidden under BROKEN_FX. PITCH EXT stays: it gates
        // PITCH's range, and PITCH is live on Input via TapeShift (varispeed on the live
        // signal, DSP-NOTES §14) in both builds.
        juce::Component* const cycleOscComps[] = { cycleXfade.get(), pitchMix.get(),
                                                    sourceExt.get(), xfadeShape.get() };
        for (auto* c : cycleOscComps)
            oscillatorBlock.addAndMakeVisible (c);
#if BROKEN_FX
        cycleXfade->setVisible (false);
        pitchMix->setVisible (false);
        xfadeShape->setVisible (false);
#endif

        // ---------------- WAVESHAPER (was CURVE) ----------------
        wsTrim = std::make_unique<Knob> (av, "ws.trim", "TRIM", "Waveshaper output trim.", false);
        waveshaperBlock.addAndMakeVisible (wsTrim.get());

        curveEditor = std::make_unique<CurveEditor> (processor);
        // item 4: FROM SAMPLE now works in both builds (Broken FX has a sample slot too),
        // so the FX-specific shorter tooltip that omitted it is gone -- both builds use
        // the same tooltip.
        curveEditor->setTooltip ("The waveshaper's transfer curve. Just drag on it \xe2\x80\x94 that "
                                 "switches to CUSTOM and keeps the shape you were looking at. "
                                 "FROM SAMPLE turns the CYCLE window into the curve itself.");
        waveshaperBlock.addAndMakeVisible (curveEditor.get());

        rndButton.setTooltip ("Roll a new random transfer curve. The seed is saved with the preset.");
        rndButton.setTitle ("RANDOM CURVE"); // header RND is "RANDOMIZE"; disambiguate for screen readers
        rndButton.onClick = [&av]
        {
            if (auto* seedParam = av.getParameter ("ws.randseed"))
            {
                int newSeed = 1 + juce::Random::getSystemRandom().nextInt (9999); // 1..9999
                seedParam->beginChangeGesture();
                seedParam->setValueNotifyingHost (seedParam->convertTo0to1 ((float) newSeed));
                seedParam->endChangeGesture();
            }
        };
        waveshaperBlock.addAndMakeVisible (rndButton);

        copyButton.setTooltip ("Copy the selected curve into the editable points as a starting shape.");
        copyButton.onClick = [this, &av]
        {
            curveEditor->refreshFromParams(); // avoid up to ~66 ms of timer staleness
            for (int i = 0; i < params::curvePointCount; ++i)
            {
                float u = -1.0f + 2.0f * (float) i / (float) (params::curvePointCount - 1);
                float y = curveEditor->evaluateCurrent (u);
                if (auto* cp = av.getParameter (params::curvePointId (i + 1)))
                {
                    cp->beginChangeGesture();
                    cp->setValueNotifyingHost (cp->convertTo0to1 (y));
                    cp->endChangeGesture();
                }
            }
        };
        waveshaperBlock.addAndMakeVisible (copyButton);

        // ---------------- MODULE TRIMS: FM / DELAY / RESONATOR / INVERT / NOISE ----------------
        fmIndex = std::make_unique<Knob> (av, "mod.fmindex", "FM INDEX", "FM modulation index.", false);
        trimsBlock.addAndMakeVisible (fmIndex.get());

        dlyFine = std::make_unique<Knob> (av, "dly.fine", "FINE",
            "Delay fine time trim (not on the original unit).", false);
        dlyFb = std::make_unique<Knob> (av, "dly.fb", "FEEDBACK",
            "Delay feedback (not on the original unit).", false);
        for (auto* c : { dlyFine.get(), dlyFb.get() })
            trimsBlock.addAndMakeVisible (c);

        resDamp = std::make_unique<Knob> (av, "res.damp", "DAMP", "Resonator high-frequency damping.", false);
        trimsBlock.addAndMakeVisible (resDamp.get());

        invType = std::make_unique<Combo> (av, "inv.type", params::invTypes, "TYPE",
            "Spectral inverter flavour: A mirrors around Nyquist, B makes two quarter-rate images.");
        trimsBlock.addAndMakeVisible (invType.get());

        noiseAmp = std::make_unique<Knob> (av, "noise.amp", "AMP NZ",
            "Noise source: amplitude randomness. 0 with PH NZ 0 = a plain sine.", false);
        noisePhase = std::make_unique<Knob> (av, "noise.phase", "PH NZ",
            "Noise source: phase randomness. Both at 100 = white noise.", false);
        for (auto* c : { noiseAmp.get(), noisePhase.get() })
            trimsBlock.addAndMakeVisible (c);
#if BROKEN_FX
        // item 5: Noise-source-only controls (FX source is always Input); hidden rather
        // than left dead. layoutTrims() drops the whole NOISE segment + its divider in FX
        // and stretches FM/DELAY/RESONATOR/INVERT to fill the freed width.
        noiseAmp->setVisible (false);
        noisePhase->setVisible (false);
        trimCaptionNz.setVisible (false);
        trimDiv4.setVisible (false);
#endif

        // ---------------- TIME: Stretcher + Envelope Removal ----------------
        // item 5: STRETCH is sample-playback only (SourceEngine's stretch segment-repeat
        // logic runs only inside playRegioned, called for Sample/Tape modes; it never runs
        // on Input), so it and its three knobs are hidden in FX and the block is retitled
        // "FLATTEN" there -- FLATTEN and RESP are the only two that still do anything.
        stretchOn = std::make_unique<TextToggle> (av, "stretch.on", "STRETCH",
            "Segment-repeat time stretch, by design Stretcher. Tune FREQ to the "
            "material or enjoy the artifacts.");
        stretchAmount = std::make_unique<Knob> (av, "stretch.amount", "AMOUNT",
            "Positive stretches, negative compresses.", false);
        stretchFreq = std::make_unique<Knob> (av, "stretch.freq", "FREQ",
            "Segment frequency - match the sound's fundamental.", false);
        stretchPredelay = std::make_unique<Knob> (av, "stretch.predelay", "PREDELAY",
            "Leaves the attack untouched before stretching starts.", false);
        flattenOn = std::make_unique<TextToggle> (av, "flat.on", "FLATTEN",
            "Envelope Removal: levels out the sound's own dynamics, like heavy compression.");
        flatResp = std::make_unique<Knob> (av, "flat.response", "RESP",
            "How fast FLATTEN tracks the level.", false);
        juce::Component* const timeComps[] = { stretchOn.get(), stretchAmount.get(), stretchFreq.get(),
                                                stretchPredelay.get(), flattenOn.get(), flatResp.get() };
        for (auto* c : timeComps)
            timeBlock.addAndMakeVisible (c);
#if BROKEN_FX
        stretchOn->setVisible (false);
        stretchAmount->setVisible (false);
        stretchFreq->setVisible (false);
        stretchPredelay->setVisible (false);
#endif
    }

    void paint (juce::Graphics&) override {}

    // Bottom-row grid per README Layout: 18px side padding, 10px gaps, 3 equal columns on
    // row 1, MODULE TRIMS spanning columns 1-2 with TIME in column 3 on row 2.
    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft (outerPad); area.removeFromRight (outerPad);
        area.removeFromTop (outerGap);  area.removeFromBottom (outerGap);

        const int colW = (area.getWidth() - 2 * outerGap) / 3;
        auto local = [] (juce::Rectangle<int> r) { return r.withPosition (0, 0); };

        auto row1 = area.removeFromTop (row1H);
#if BROKEN_FX
        // ENVELOPES stays hidden (gated by MIDI notes, which never fire in an effect).
        // OSCILLATOR is shown again (item 2): the Table mod source reads the OSCILLATOR
        // panel's current shape, so it needs to be visible and editable even though it no
        // longer drives the SOURCE. Row 1 becomes two columns instead of three.
        envelopesBlock.setVisible (false);
        oscillatorBlock.setVisible (true);
        const int colW2 = (row1.getWidth() - outerGap) / 2;
        auto oscArea = row1.removeFromLeft (colW2); row1.removeFromLeft (outerGap);
        auto wsArea  = row1;
#else
        auto envArea = row1.removeFromLeft (colW); row1.removeFromLeft (outerGap);
        auto oscArea = row1.removeFromLeft (colW); row1.removeFromLeft (outerGap);
        auto wsArea  = row1;
#endif

        area.removeFromTop (outerGap);
        auto row2 = area.removeFromTop (row2H);
        auto trimsArea = row2.removeFromLeft (colW * 2 + outerGap); row2.removeFromLeft (outerGap);
        auto timeArea = row2;

#if !BROKEN_FX
        envelopesBlock.setBounds (envArea);
        layoutEnvelopes (local (envArea));
#endif
        oscillatorBlock.setBounds (oscArea);
        waveshaperBlock.setBounds (wsArea);
        trimsBlock.setBounds (trimsArea);
        timeBlock.setBounds (timeArea);

        layoutOscillator (local (oscArea));
        layoutWaveshaper (local (wsArea));
        layoutTrims (local (trimsArea));
        layoutTime (local (timeArea));
    }

private:
    static constexpr int outerPad = 18; // README: 18px side padding
    static constexpr int outerGap = 10; // README: 10px gaps
    static constexpr int headerPad = 20;
    static constexpr int row1H = 245; // ENVELOPES / OSCILLATOR / WAVESHAPER
    static constexpr int row2H = 140; // MODULE TRIMS / TIME

    // Knob box heights, same convention as MangleView: label(11) + dial + textbox(0 here —
    // none of EditView's knobs carry a readout).
    static constexpr int mBoxH = 13 + geom::knobM; // 57: ENVELOPES/OSCILLATOR/WAVESHAPER/TRIMS knobs
    static constexpr int sBoxH = mBoxH; // TIME uses the same stripe knobs as MODULE TRIMS beside it — two adjacent panels with different knob hardware read as an accident (v0.30)

    void layoutEnvelopes (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        r.removeFromTop (headerPad);

        // centre the content vertically: the two rows used to hug the title and pool
        // their slack at the panel bottom (v0.30 screenshot review)
        {
            const int content = mBoxH * 2 + 12;
            r.removeFromTop (juce::jmax (0, (r.getHeight() - content) / 2));
        }
        // FLT row: A D S R AMT + FLOOR EXT. The 26px gutter captions are what
        // tell two otherwise identical ADSR rows apart (v0.29 screenshot review).
        auto fltRow = r.removeFromTop (mBoxH);
        envCaptionFlt.setBounds (fltRow.removeFromLeft (26));
        auto floorExtCol = fltRow.removeFromRight (100);
        int w = fltRow.getWidth() / 5;
        for (auto* k : { fenvA.get(), fenvD.get(), fenvS.get(), fenvR.get(), fltEnvAmt.get() })
            k->setBounds (fltRow.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH));
        fltExt->setBounds (floorExtCol.withSizeKeepingCentre (100, 24));

        r.removeFromTop (10);
        envHairline.setBounds (r.removeFromTop (2));
        r.removeFromTop (10);

        // AUX row: A D S R AMT (38px) + DEST combo
        auto auxRow = r.removeFromTop (mBoxH);
        envCaptionAux.setBounds (auxRow.removeFromLeft (26));
        auto destCol = auxRow.removeFromRight (100);
        w = auxRow.getWidth() / 5;
        for (auto* k : { auxA.get(), auxD.get(), auxS.get(), auxR.get(), auxAmount.get() })
            k->setBounds (auxRow.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH));
        auxDest->setBounds (destCol.withSizeKeepingCentre (100, 33));
    }

    // Consolidates the old HARMONICS + CYCLE/OSC blocks: the 64-bar editor and its mode
    // controls on top, XFADE/PITCH MIX/PITCH EXT/LOOP XFADE below.
    void layoutOscillator (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        r.removeFromTop (headerPad);

        auto topRow = r.removeFromTop (96);
        auto right = topRow.removeFromRight (132);
        topRow.removeFromRight (12);
        harmonicEditor->setBounds (topRow);

        oscMode->setBounds (right.removeFromTop (33));
        right.removeFromTop (6);
        oscWave->setBounds (right.removeFromTop (33));
        right.removeFromTop (6);
        auto btnRow = right.removeFromTop (22);
        const int bw = (btnRow.getWidth() - 8) / 3;
        sawButton.setBounds (btnRow.removeFromLeft (bw));
        btnRow.removeFromLeft (4);
        squareButton.setBounds (btnRow.removeFromLeft (bw));
        btnRow.removeFromLeft (4);
        flatShapeButton.setBounds (btnRow);

        r.removeFromTop (10);
        auto bottomRow = r.removeFromTop (mBoxH);
#if BROKEN_FX
        // item 2: CYCLE XFADE / PITCH MIX / LOOP XFADE are hidden (Sample/Cycle/Tape-only,
        // FX source is always Input) -- only PITCH EXT remains, centred in the full row
        // rather than left stranded in a quarter-width cell sized for four controls.
        sourceExt->setBounds (bottomRow.withSizeKeepingCentre (juce::jmin (bottomRow.getWidth() - 8, 120), 26));
#else
        // four even cells across the width — the old row crowded left and left a hole
        // under SAW/SQR/FLAT (v0.30 screenshot review)
        const int cell = bottomRow.getWidth() / 4;
        cycleXfade->setBounds (bottomRow.removeFromLeft (cell).withSizeKeepingCentre (70, mBoxH));
        pitchMix->setBounds (bottomRow.removeFromLeft (cell).withSizeKeepingCentre (70, mBoxH));
        sourceExt->setBounds (bottomRow.removeFromLeft (cell).withSizeKeepingCentre (juce::jmin (cell - 8, 96), 26));
        xfadeShape->setBounds (bottomRow.withSizeKeepingCentre (juce::jmin (bottomRow.getWidth() - 8, 150), 46));
#endif
    }

    // CURVE cell (now WAVESHAPER): transfer-curve editor left (FROM SAMPLE lives inside
    // it already), TRIM + RND + COPY TO CUSTOM stacked on the right.
    void layoutWaveshaper (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        r.removeFromTop (headerPad);

        // the display is the panel's subject: give it the left half at full height, and
        // distribute TRIM / RND / COPY evenly down the right half instead of crowding the
        // top row and leaving an empty quarter below (v0.30 screenshot review)
        auto disp = r.removeFromLeft (r.getWidth() / 2);
        curveEditor->setBounds (disp.reduced (0, 2));
        r.removeFromLeft (16);

        const int rowGap = 12;
        const int rowH = (r.getHeight() - 2 * rowGap) / 3;
        wsTrim->setBounds (r.removeFromTop (rowH).withSizeKeepingCentre (70, juce::jmin (rowH, mBoxH)));
        r.removeFromTop (rowGap);
        rndButton.setBounds (r.removeFromTop (rowH).withSizeKeepingCentre (r.getWidth(), 26));
        r.removeFromTop (rowGap);
        copyButton.setBounds (r.removeFromTop (rowH).withSizeKeepingCentre (r.getWidth(), 26));
    }

    // MODULE TRIMS: five segments separated by 1px dividers (README).
    void layoutTrims (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        r.removeFromTop (headerPad);

#if BROKEN_FX
        // item 5: NOISE (noise.amp/noise.phase) is Noise-source-only and hidden in FX
        // (source is always Input), so its segment and divider are dropped entirely and
        // FM/DELAY/RESONATOR/INVERT stretch to fill the row -- 3 dividers instead of 4,
        // same 10:16:10:12 flex ratios so the three that remain keep their relative width.
        const int unit = (r.getWidth() - 3 * (2 * 12 + 1)) / 48;
        auto take = [&] (int units) { return r.removeFromLeft (unit * units); };

        auto fmSeg = take (10);
        r.removeFromLeft (12); trimDiv1.setBounds (r.removeFromLeft (1)); r.removeFromLeft (12);
        auto dlySeg = take (16);
        r.removeFromLeft (12); trimDiv2.setBounds (r.removeFromLeft (1)); r.removeFromLeft (12);
        auto resSeg = take (10);
        r.removeFromLeft (12); trimDiv3.setBounds (r.removeFromLeft (1)); r.removeFromLeft (12);
        auto invSeg = r; // remainder
#else
        // flex ratios 1 : 1.6 : 1 : 1.2 : 1.6 (FM : DELAY : RESONATOR : INVERT : NOISE),
        // matching the approved design's proportions
        const int unit = (r.getWidth() - 4 * (2 * 12 + 1)) / 64; // 4 dividers, 12px gap each side
        auto take = [&] (int units)
        {
            auto seg = r.removeFromLeft (unit * units);
            return seg;
        };

        auto fmSeg = take (10);
        r.removeFromLeft (12); trimDiv1.setBounds (r.removeFromLeft (1)); r.removeFromLeft (12);
        auto dlySeg = take (16);
        r.removeFromLeft (12); trimDiv2.setBounds (r.removeFromLeft (1)); r.removeFromLeft (12);
        auto resSeg = take (10);
        r.removeFromLeft (12); trimDiv3.setBounds (r.removeFromLeft (1)); r.removeFromLeft (12);
        auto invSeg = take (12);
        r.removeFromLeft (12); trimDiv4.setBounds (r.removeFromLeft (1)); r.removeFromLeft (12);
        auto nzSeg = r; // remainder
#endif

        layoutTrimSegment (fmSeg, trimCaptionFm, [this] (juce::Rectangle<int> c)
        { fmIndex->setBounds (c.withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH)); });

        layoutTrimSegment (dlySeg, trimCaptionDly, [this] (juce::Rectangle<int> c)
        {
            int w = c.getWidth() / 2;
            dlyFine->setBounds (c.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH));
            dlyFb->setBounds (c.withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH));
        });

        layoutTrimSegment (resSeg, trimCaptionRes, [this] (juce::Rectangle<int> c)
        { resDamp->setBounds (c.withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH)); });

        layoutTrimSegment (invSeg, trimCaptionInv, [this] (juce::Rectangle<int> c)
        { invType->setBounds (c.withSizeKeepingCentre (juce::jmin (c.getWidth(), 90), 33)); });

#if !BROKEN_FX
        layoutTrimSegment (nzSeg, trimCaptionNz, [this] (juce::Rectangle<int> c)
        {
            int w = c.getWidth() / 2;
            noiseAmp->setBounds (c.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH));
            noisePhase->setBounds (c.withSizeKeepingCentre (juce::jmin (70, mBoxH + 40), mBoxH));
        });
#endif
    }

    // Segment caption on top, then hands the remaining rect to `layoutFn`.
    template <typename LayoutFn>
    static void layoutTrimSegment (juce::Rectangle<int> seg, juce::Label& caption, LayoutFn&& layoutFn)
    {
        caption.setBounds (seg.removeFromTop (16));
        seg.removeFromTop (8);   // air so knob titles never kiss the segment caption
        // every segment's content sits on ONE baseline row — the TYPE combo used to
        // float higher than its neighbour knobs (v0.30 screenshot review)
        layoutFn (seg.withHeight (mBoxH).withY (seg.getY() + juce::jmax (0, (seg.getHeight() - mBoxH) / 2)));
    }

    // TIME cell: STRETCH + FLATTEN stacked left; AMOUNT / FREQ / PREDELAY / RESP (34px) right.
    void layoutTime (juce::Rectangle<int> r)
    {
        r = r.reduced (6);
        r.removeFromRight (6); // RESP was hugging the panel border (v0.32)
        r.removeFromTop (headerPad);

        auto row = r.withSizeKeepingCentre (r.getWidth(), juce::jmax (sBoxH, 56));

#if BROKEN_FX
        // item 5: STRETCH + AMOUNT/FREQ/PREDELAY are hidden (sample-playback only); only
        // FLATTEN + RESP remain, so FLATTEN centres alone in the left column and RESP
        // takes the whole knob row instead of one quarter of it -- no gap left.
        auto left = row.removeFromLeft (110);
        flattenOn->setBounds (left.withSizeKeepingCentre (110, 24));
        row.removeFromLeft (14);
        flatResp->setBounds (row.withSizeKeepingCentre (juce::jmin (70, sBoxH + 40), sBoxH));
#else
        auto left = row.removeFromLeft (110);
        stretchOn->setBounds (left.removeFromTop (24).withSizeKeepingCentre (110, 24));
        left.removeFromTop (8);
        flattenOn->setBounds (left.removeFromTop (24).withSizeKeepingCentre (110, 24));

        row.removeFromLeft (14);
        int w = row.getWidth() / 4;
        stretchAmount->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (70, sBoxH + 40), sBoxH));
        stretchFreq->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (70, sBoxH + 40), sBoxH));
        stretchPredelay->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (70, sBoxH + 40), sBoxH));
        flatResp->setBounds (row.withSizeKeepingCentre (juce::jmin (70, sBoxH + 40), sBoxH));
#endif
    }

    // 1px inner divider (colour::ruleInner).
    struct Rule : public juce::Component
    {
        void paint (juce::Graphics& g) override { g.setColour (colour::ruleInner); g.fillRect (getLocalBounds()); }
    };

    BrokenProcessor& processor;

    Block envelopesBlock { "ENVELOPES" }, oscillatorBlock { "OSCILLATOR" },
          waveshaperBlock { "WAVESHAPER" }, trimsBlock { "MODULE TRIMS" },
#if BROKEN_FX
          timeBlock { "FLATTEN" }; // item 5: STRETCH is hidden in FX, only FLATTEN/RESP work
#else
          timeBlock { "TIME" };
#endif
    Rule envHairline, trimDiv1, trimDiv2, trimDiv3, trimDiv4;
    juce::Label trimCaptionFm, trimCaptionDly, trimCaptionRes, trimCaptionInv, trimCaptionNz;
    juce::Label envCaptionFlt, envCaptionAux;

    // Envelopes
    std::unique_ptr<Knob> fenvA, fenvD, fenvS, fenvR, fltEnvAmt;
    std::unique_ptr<TextToggle> fltExt; // moved here from the old FILTER block
    std::unique_ptr<Knob> auxA, auxD, auxS, auxR, auxAmount;
    std::unique_ptr<Combo> auxDest;

    // Oscillator (HARMONICS + CYCLE/OSC consolidated)
    std::unique_ptr<HarmonicEditor> harmonicEditor;
    std::unique_ptr<Combo> oscMode;
    // "SQR" not "SQUARE": three buttons share one ~47 px-wide row (tooltip spells it out)
    juce::TextButton sawButton { "SAW" }, squareButton { "SQR" }, flatShapeButton { "FLAT" };
    std::unique_ptr<Knob> cycleXfade, pitchMix;
    std::unique_ptr<Combo> oscWave;
    std::unique_ptr<TextToggle> sourceExt;
    std::unique_ptr<Combo> xfadeShape; // moved here from the old COLOUR/NOISE block, "LOOP XFADE"

    // Waveshaper (was CURVE)
    std::unique_ptr<Knob> wsTrim;
    std::unique_ptr<CurveEditor> curveEditor;
    juce::TextButton rndButton { "RND" };
    // plain ASCII: the raw UTF-8 arrow escape rendered as mojibake in the button
    juce::TextButton copyButton { "COPY TO CUSTOM" };

    // Module trims
    std::unique_ptr<Knob> fmIndex;
    std::unique_ptr<Knob> dlyFine, dlyFb;
    std::unique_ptr<Knob> resDamp;
    std::unique_ptr<Combo> invType;
    std::unique_ptr<Knob> noiseAmp, noisePhase;

    // Time
    std::unique_ptr<TextToggle> stretchOn, flattenOn;
    std::unique_ptr<Knob> stretchAmount, stretchFreq, stretchPredelay, flatResp;
};
} // namespace broken::ui
