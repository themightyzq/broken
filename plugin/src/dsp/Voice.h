#pragma once
// One voice = the full per-voice chain (docs/DESIGN.md §2):
// Source → Modulator → Waveshaper → Filter(+env) → Resonator → SpectralInv → Delay → AmpEnv.
// Everything downstream of the voice sum (colour, tape, output) lives in Engine.

#include <array>
#include "SourceEngine.h"
#include "TapeShift.h"
#include "InputVarispeed.h"
#include "Flatten.h"
#include "Modulator.h"
#include "Waveshaper.h"
#include "FilterStack.h"
#include "EnvelopeADSR.h"
#include "Resonator.h"
#include "SpectralInverter.h"
#include "DelayLine.h"

namespace broken::dsp
{
// Block-rate parameter snapshot, filled from APVTS once per block. Field names mirror
// parameter IDs (Params.h) so drift is visible at a glance.
struct VoiceParams
{
    int   sourceMode = 0;       // SourceEngine::Mode
    float sourcePitch = 0.0f;
    float winPos = 0.1f, winLen = 512.0f, winXfade = 8.0f;
    int   oscWave = 2;
    int   oscMode = 0;                      // 0 Wave, 1 Harmonic
    std::array<float, 64> harmonics {};
    std::array<float, 128> drawPts {};  // DRAW mode table (DSP-NOTES §1.3b)
    float inTrimDb = 0.0f;
    bool  stretchOn = false;
    float stretchFreq = 110.0f, stretchAmount = 0.0f, stretchPredelayMs = 0.0f;
    bool  flatOn = false;
    float flatResponseMs = 50.0f;
    float regStart = 0.0f, regEnd = 1.0f;   // non-destructive sample region
    bool  loopOn = false;
    int   loopStyle = 0;                    // SourceEngine::LoopStyle
    bool  rev = false;
    float loopXfadeMs = 10.0f;

    bool  modOn = true;
    float modAmount = 0.0f, modFreq = 55.0f, fmIndex = 2.0f;
    int   modMode = 1;          // 0 AM, 1 RM, 2 FM, 3 PM
    int   modWave = 1;          // Modulator wave index: 0 sine, 1 bell, 2 odd
    int   modSource = 0;        // 0 Osc, 1 Self, 2 Sample, 3 Tape, 4 Table (OSCILLATOR shape)
    float pitchMix = 1.0f;

    bool  wsOn = true;
    int   wsCurve = 2;
    float wsDriveDb = 12.0f, wsMorph = 1.0f, wsTrimDb = 0.0f;
    int   wsSeed = 1;
    std::array<float, 128> wsCustom {};   // Custom transfer curve (DSP-NOTES §3.1)

    bool  fltOn = true;
    float fltCutoffHz = 20000.0f, fltEnvAmt = 0.0f;
    float fltFloorHz = 500.0f;
    int   fltPoles = 2;
    float fenvA = 0.005f, fenvD = 0.2f, fenvS = 0.8f, fenvR = 0.15f;

    bool  resOn = true;
    float resFreq = 220.0f, resFb = 0.0f, resDamp = 8000.0f;

    bool  invOn = true;
    float invMix = 0.0f;
    int   invType = 0;
    float noiseAmpPct = 25.0f, noisePhasePct = 25.0f;
    int   xfadeShape = 0;

    bool  dlyOn = true;
    float dlyTimeMs = 80.0f, dlyMix = 0.0f, dlyFb = 0.0f;
    bool  dlyInvert = false;

    float ampA = 0.005f, ampD = 0.2f, ampS = 0.8f, ampR = 0.15f;
    float auxA = 0.005f, auxD = 0.3f, auxS = 0.0f, auxR = 0.2f;
    int   auxDest = 0;          // 0 FilterCut, 1 ResFreq, 2 InvMix, 3 ModAmt, 4 Pitch
    float auxAmount = 0.0f;
};

class Voice
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        src.prepare (sampleRate);
        liveShift.prepare (sampleRate);
        inputFM.prepare (sampleRate);
        flat.prepare (sampleRate);
        mod.prepare (sampleRate);
        ws.prepare (sampleRate);
        flt.prepare (sampleRate);
        fenv.prepare (sampleRate);
        aenv.prepare (sampleRate);
        auxenv.prepare (sampleRate);
        // A note in flight must not survive a prepare(): the host calls this on a
        // sample-rate or buffer-size change and expects silence, but EnvelopeADSR::prepare
        // only re-derives coefficients. Without this a held note kept sounding at full
        // level through the change (found by test_engine.cpp, v0.23).
        aenv.reset();
        fenv.reset();
        auxenv.reset();
        res.prepare (sampleRate);
        inv.prepare (sampleRate);
        dly.prepare (sampleRate);
    }

    void applyParams (const VoiceParams& p)
    {
        vp = p;
        src.setMode (p.sourceMode);
        src.setTranspose (p.sourcePitch + detuneSemis); // aux->pitch is applied per sample in render()
        src.setOscWave (p.oscWave);
        src.setOscMode (p.oscMode);
        src.setHarmonics (p.harmonics.data());
        src.setDrawPoints (p.drawPts.data());
        src.setWindow (p.winPos, p.winLen, p.winXfade);
        src.setRegion (p.regStart, p.regEnd);
        src.setLoopOn (p.loopOn);
        src.setLoopStyle (p.loopStyle);
        src.setReverse (p.rev);
        src.setLoopXfadeMs (p.loopXfadeMs);
        src.setPitchMix (p.pitchMix);
        src.setNoiseModel (p.noiseAmpPct, p.noisePhasePct);
        src.setXfadeShape (p.xfadeShape);
        src.setStretch (p.stretchOn, p.stretchFreq, p.stretchAmount, p.stretchPredelayMs);
        flat.setResponseMs (p.flatResponseMs);
        inv.setType (p.invType);
        // PITCH is live on the Input source: varispeed on a tape loop (DSP-NOTES §14)
        liveShift.setSemitones (p.sourcePitch < -48.f ? -48.f
                                : (p.sourcePitch > 48.f ? 48.f : p.sourcePitch));
        mod.setFreqHz (p.modFreq);
        mod.setWave (p.modWave);
        mod.setAmount (p.modAmount);
        ws.setCurve (p.wsCurve);
        ws.setDriveDb (p.wsDriveDb);
        ws.setMorph (p.wsMorph);
        ws.setTrimDb (p.wsTrimDb);
        ws.setRandomSeed (p.wsSeed);
        ws.setCustomPoints (p.wsCustom.data());
        flt.setCutoffHz (p.fltCutoffHz);
        flt.setFloorHz (p.fltFloorHz);
        flt.setPoles (p.fltPoles);
        fenv.setTimes (p.fenvA, p.fenvD, p.fenvS, p.fenvR);
        aenv.setTimes (p.ampA, p.ampD, p.ampS, p.ampR);
        auxenv.setTimes (p.auxA, p.auxD, p.auxS, p.auxR);
        res.setDampHz (p.resDamp);
        res.setFeedback (p.resFb);
        dly.setTimeMs (p.dlyTimeMs);
        dly.setMix (p.dlyOn ? p.dlyMix : 0.0f);
        dly.setInvert (p.dlyInvert);
        dly.setFeedback (p.dlyFb);

        // A live sample-editor edit must be audible without retriggering: rewind a
        // one-shot that already ran out, but only while the key (or PLAY) is still down.
        // docs/DSP-NOTES.md §1.1a "Re-arm on edit".
        if (aenv.isGateOn())
            src.rearmIfEdited();
    }

    void setSampleData (const float* d, size_t n, double dsr) { src.setSampleData (d, n, dsr); }
    void setTapeData (const float* d, size_t n, double dsr)   { src.setTapeData (d, n, dsr); }
    // Table mod source (DSP-NOTES §2a "Table"): the table itself is owned and rebuilt by
    // Engine (one per Engine, not per voice -- rebuilding 4096 entries per voice per block
    // would be 6x the work for an identical result); the Voice only holds a const pointer
    // and its own read phase, exactly like the Sample/Tape mod sources' modPhase.
    void setModTable (const float* data, size_t len) { modTableData = data; modTableLen = len; }
    void setDetuneSemis (float st) { detuneSemis = st; }
    void setGainComp (float g)     { gainComp = g; }

    void noteOn (int midiNote, float vel, uint32_t seed, bool retrigger)
    {
        note = midiNote;
        velocity = vel;
        if (retrigger || ! aenv.isActive())
        {
            src.noteOn (midiNote, seed);
            aenv.gateOn (vel);
            fenv.gateOn (1.0f);
            auxenv.gateOn (1.0f);
        }
        // legato (no retrigger, env already running): pitch changes, envelopes ride on
    }

    void noteOff() { aenv.gateOff(); fenv.gateOff(); auxenv.gateOff(); }
    bool isActive() const { return aenv.isActive(); }
    int currentNote() const { return note; }
    float getPlayhead01() const { return src.getPlayhead01(); }
    float getLastSourceSample() const { return lastSourceSample; }
    float getLastDrySample()    const { return lastDrySample; }

    // FX mode: chain runs without MIDI, amp env bypassed
    void setFreeRun (bool fr) { freeRun = fr; }

    float render (float input)
    {
        // modulator signal by source (DSP-NOTES §2a): Self uses the previous sample's
        // post-source value (one-sample-delayed self-modulation — avoids circularity)
        float m;
        switch (vp.modSource)
        {
            case 1:  m = lastSourceSample; break;
            case 2:  m = src.tickModSource (false, vp.modFreq); break;
            case 3:  m = src.tickModSource (true,  vp.modFreq); break;
            case 4:  m = tickTableMod(); break;
            default: m = mod.tick(); break;
        }
        // every mode's math is defined on a +-1 modulator (DSP-NOTES §2). The Self /
        // Sample / Tape sources can run hotter than that; unclamped it made RM a
        // runaway amplifier and drove PM's delay index out of its buffer (v0.12 fuzz).
        if (! (m > -1.0f)) m = -1.0f;   // also catches NaN
        else if (m > 1.0f) m = 1.0f;
        const float auxRaw = auxenv.processSample() * vp.auxAmount;

        float modAmt = vp.modAmount + (vp.auxDest == 3 ? auxRaw : 0.0f);
        modAmt = modAmt < 0.0f ? 0.0f : (modAmt > 1.0f ? 1.0f : modAmt);
        mod.setAmount (modAmt);

        // FM modulates source rate; the source runs at note pitch + detune (+ aux pitch)
        const bool fmActive = vp.modOn && vp.modMode == 2;
        const float rFM = fmActive ? vp.fmIndex * modAmt * m : 0.0f;
        src.setRateMod (rFM); // SourceEngine::processSample ignores this for Input (see below)
        if (vp.auxDest == 4)
            src.setTranspose (vp.sourcePitch + detuneSemis + auxRaw * 12.0f);

        float x = src.processSample (input);
        if (vp.sourceMode == SourceEngine::Input)
        {
            const float liveDry = x;
            x = liveShift.processSample (x);
            // Pitch MIX on Input (DSP-NOTES §2a): blend the shifted signal with the live
            // one, the Input counterpart of SourceEngine's root-rate dry head. The dry leg
            // is not delayed to match TapeShift's read taps, so ~50% with FINE detune
            // gives a chorus with some comb colour. Skipped while TapeShift is in its
            // exact-bypass range (|PITCH| < 0.01 st) so the unity null stays bit-true.
            if (vp.pitchMix < 0.999f && std::abs (vp.sourcePitch) >= 0.01f)
                x = vp.pitchMix * x + (1.0f - vp.pitchMix) * liveDry;
            // FM on Input (docs/DSP-NOTES.md §14a): the Input source ignores setRateMod
            // above (it has no "rate" to modulate -- it is the live signal itself), so FM
            // was silently dead on Input. InputVarispeed applies the SAME r as every other
            // source via a rate-modulated read head on a short delay line -- true varispeed
            // of the live signal. It stays after liveShift so the dry/MIX leg sees the same
            // source signal the instrument's FM affects (lastSourceSample below is set from
            // this x, matching the source-then-mangle ordering used everywhere else).
            x = inputFM.processSample (x, rFM, fmActive);
        }
        if (vp.flatOn)
            x = flat.processSample (x);
        lastSourceSample = x; // TAP A for the IN tuner: what enters the mangle

        if (vp.modOn)
        {
            if (vp.modMode == 0)      x = mod.applyAM (x, m);
            else if (vp.modMode == 1) x = mod.applyRM (x, m);
            else if (vp.modMode == 3) x = mod.applyPM (x, m);
            // mode 2 (FM) acts through src.setRateMod above, nothing to apply here
        }

        if (vp.wsOn)
            x = ws.processSample (x);

        const float fe = fenv.processSample();
        flt.setCutoffModSemitones (vp.fltEnvAmt * fe
                                   + (vp.auxDest == 0 ? auxRaw * 60.0f : 0.0f));
        if (vp.fltOn)
            x = flt.processSample (x);

        res.setFreqHz (vp.resFreq * std::pow (2.0f, (vp.auxDest == 1 ? auxRaw * 24.0f : 0.0f) / 12.0f));
        if (vp.resOn)
            x = res.processSample (x);

        if (vp.invOn)
        {
            float im = vp.invMix + (vp.auxDest == 2 ? auxRaw : 0.0f);
            inv.setMix (im < 0.0f ? 0.0f : (im > 1.0f ? 1.0f : im));
            x = inv.processSample (x);
        }

        if (vp.dlyOn)
            x = dly.processSample (x);

        const float g = freeRun ? 1.0f : aenv.processSample();
        // dry leg for the global MIX: the un-mangled source through the SAME envelope
        // and comp gain, so the blend never changes level or gating (DSP-NOTES §12b)
        lastDrySample = lastSourceSample * g * gainComp;
        return x * g * gainComp;
    }

private:
    // Table mod source (DSP-NOTES §2a "Table"): own phase over the Engine-owned table,
    // exactly like SourceEngine::tickModSource's modPhase for Sample/Tape.
    float tickTableMod()
    {
        if (modTableData == nullptr || modTableLen < 2) return 0.0f;
        modTablePhase += (double) vp.modFreq / sr;
        modTablePhase -= std::floor (modTablePhase);
        const double t = modTablePhase * (double) modTableLen;
        const auto i0 = (size_t) t % modTableLen;
        const auto i1 = (i0 + 1) % modTableLen;
        const float frac = (float) (t - std::floor (t));
        return modTableData[i0] + frac * (modTableData[i1] - modTableData[i0]);
    }

    SourceEngine src;
    TapeShift liveShift;
    InputVarispeed inputFM;
    Flatten flat;
    Modulator mod;
    Waveshaper ws;
    FilterStack flt;
    EnvelopeADSR fenv, aenv, auxenv;
    Resonator res;
    SpectralInverter inv;
    DelayLine dly;

    VoiceParams vp;
    double sr = 48000.0;
    int note = 48;
    float velocity = 1.0f;
    float detuneSemis = 0.0f;
    float gainComp = 1.0f;
    float lastSourceSample = 0.0f;
    float lastDrySample = 0.0f;
    bool freeRun = false;
    const float* modTableData = nullptr;
    size_t modTableLen = 0;
    double modTablePhase = 0.0;
};
} // namespace broken::dsp
