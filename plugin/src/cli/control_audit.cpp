// broken_control_audit / broken_fx_control_audit — headless control-wiring audit.
//
// The owner's question: "are we sure every control is wired up properly?" Until now the
// only guard was --param-check (broken_cli), which proves every parameter ID *exists* in
// the APVTS -- not that moving it changes the sound. This tool renders the real processor
// (same processBlock every plugin instance runs) with a rich base state, then for every
// parameter and every source-mode context, moves ONLY that parameter and measures whether
// the tail of the render actually changed. Output: a CSV (param_id,name,context,wired,diff_db)
// plus a stdout summary, and three PASS/FAIL checks the owner named by name.
//
// Built twice, exactly like broken_fx_check: broken_control_audit (BROKEN_FX=0) and
// broken_fx_control_audit (BROKEN_FX=1). Neither is registered with ctest -- the owner
// decides what to do with the findings first.
//
// Usage: broken_control_audit --csv <path.csv>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/PluginProcessor.h"

using broken::BrokenProcessor;
namespace params = broken::params;
using broken::dsp::SourceEngine;

namespace
{
// ---- render constants -----------------------------------------------------------
constexpr double kSr            = 48000.0;
constexpr int    kBlock         = 512;
constexpr double kTotalSeconds  = 1.5;   // render length: lets smoothing settle
constexpr double kMeasureSeconds = 0.75; // only the tail is measured
constexpr double kWiredThresholdDb = -60.0;

// ---- small param helpers ---------------------------------------------------------
juce::RangedAudioParameter* rangedOf (BrokenProcessor& proc, const juce::String& id)
{
    return dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id));
}

void setNorm (juce::RangedAudioParameter* rp, float norm)
{
    if (rp != nullptr) rp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
}

void setReal (juce::RangedAudioParameter* rp, float real)
{
    if (rp == nullptr) return;
    const auto& range = rp->getNormalisableRange();
    const float v = juce::jlimit (range.start, range.end, real);
    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

void setChoiceReal (BrokenProcessor& proc, const char* id, int idx)
{
    setReal (rangedOf (proc, id), (float) idx);
}

void setBoolReal (BrokenProcessor& proc, const char* id, bool v)
{
    setReal (rangedOf (proc, id), v ? 1.0f : 0.0f);
}

bool isBankId (const juce::String& id)
{
    // curvePointId = "ws.c%03d", harmonicId = "osc.h%02d", drawPointId = "osc.d%03d".
    // "ws.curve" is handled by its own explicit branch before this is ever consulted.
    return id.startsWith ("osc.h") || id.startsWith ("osc.d") || id.startsWith ("ws.c");
}

// ---- base state (documented in the report; printed here too) ---------------------
// - every "*.on" module switch on (mod/ws/flt/res/inv/dly/stretch/flat)
// - sample.loopon on, so loopstyle/xfade/xfadeshape have a dependent to matter for
// - ws.curve = Custom (the 128 curve points are live); points keep their Params.h
//   diagonal defaults (moving any ONE of them still perturbs the local curve shape)
// - every other plain float parameter (including the harmonic and draw banks) at
//   40% of its normalised range, so gain/amount-style dependents are non-trivial
// - chain.mix = 1, bypass = 0, out.level = 0 dB, ws.randseed left at its Params.h
//   default (fixed across every render -- nothing here is time-seeded)
// - every other bool/choice stays at its Params.h default (source.mode and osc.mode
//   are set per-context/per-test on top of this)
void applyBaseState (BrokenProcessor& proc)
{
    for (auto* param : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (rp == nullptr) continue;
        const auto id = rp->paramID;

        if (id.endsWith (".on"))            { setReal (rp, 1.0f); continue; }
        if (id == "ws.curve")
        {
            const int idx = params::wsCurves.indexOf ("Custom");
            setReal (rp, (float) idx);
            continue;
        }
        if (id == "chain.mix")              { setReal (rp, 1.0f); continue; }
        if (id == "bypass")                 { setReal (rp, 0.0f); continue; }
        if (id == "out.level")              { setReal (rp, 0.0f); continue; }
        if (id == "ws.randseed")            { continue; } // fixed: never time-seeded, left at default
        if (id == "sample.loopon")          { setReal (rp, 1.0f); continue; }
        if (isBankId (id))                  { continue; } // keep Params.h recipe/sine/diagonal defaults

        if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr)   continue; // other bools: Params.h default
        if (dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr) continue; // other choices: Params.h default

        setNorm (rp, 0.4f); // plain float "amount-type" parameter: 40% of range
    }
}

// ---- context definition -----------------------------------------------------------
struct CtxDef
{
    const char* name;
    int  sourceModeIdx;
    bool feedInput;
    bool holdNote;
    bool isTape;
};

// ---- test material: samples + FX input buffer -------------------------------------
juce::File tempFile (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
}

void writeMonoWav (const juce::File& f, const std::vector<float>& mono, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (new juce::FileOutputStream (f), sr, 1, 24, {}, 0));
    juce::AudioBuffer<float> buf (1, (int) mono.size());
    std::memcpy (buf.getWritePointer (0), mono.data(), mono.size() * sizeof (float));
    writer->writeFromAudioSampleBuffer (buf, 0, (int) mono.size());
}

// 1.5 s of a 110 Hz saw with a slow amplitude wobble, per the task spec.
juce::File makePrimarySample()
{
    const int n = (int) std::llround (kSr * 1.5);
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double phase = std::fmod (110.0 * t, 1.0);
        const double saw = 2.0 * phase - 1.0;
        const double wobble = 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.6 * t);
        m[(size_t) i] = (float) (0.6 * saw * (0.25 + 0.75 * wobble));
    }
    auto f = tempFile ("control_audit_sample_primary.wav");
    writeMonoWav (f, m, kSr);
    return f;
}

// A second, clearly different sample -- used only by the "Sample as a shape" test
// (mod.source = Sample must sound different when the loaded content differs).
juce::File makeAltSample()
{
    const int n = (int) std::llround (kSr * 1.5);
    std::vector<float> m ((size_t) n);
    std::mt19937 rng (777);
    std::uniform_real_distribution<float> noiseDist (-1.0f, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double tone = std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t);
        const double envel = std::exp (-1.2 * std::fmod (t, 0.5));
        m[(size_t) i] = (float) (0.5 * tone * envel) + 0.1f * noiseDist (rng);
    }
    auto f = tempFile ("control_audit_sample_alt.wav");
    writeMonoWav (f, m, kSr);
    return f;
}

// 220 Hz tone + light noise at -14 dBFS RMS; distinct phase offsets per channel.
void makeStereoToneNoise (juce::AudioBuffer<float>& buf, int n, double phaseL, double phaseR)
{
    buf.setSize (2, n, false, false, true);
    std::mt19937 rng (2468);
    std::uniform_real_distribution<float> noiseDist (-1.0f, 1.0f);
    std::vector<float> L ((size_t) n), R ((size_t) n);
    const double inc = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSr;
    double phL = phaseL, phR = phaseR;
    for (int i = 0; i < n; ++i)
    {
        L[(size_t) i] = (float) std::sin (phL) + 0.05f * noiseDist (rng);
        R[(size_t) i] = (float) std::sin (phR) + 0.05f * noiseDist (rng);
        phL += inc; phR += inc;
    }
    auto normalize = [] (std::vector<float>& v)
    {
        double sumSq = 0.0;
        for (auto x : v) sumSq += (double) x * (double) x;
        const double rms = std::sqrt (sumSq / (double) std::max<size_t> (1, v.size()));
        const double targetLin = std::pow (10.0, -14.0 / 20.0);
        const float scale = rms > 1.0e-9 ? (float) (targetLin / rms) : 1.0f;
        for (auto& x : v) x *= scale;
    };
    normalize (L); normalize (R);
    for (int i = 0; i < n; ++i) { buf.setSample (0, i, L[(size_t) i]); buf.setSample (1, i, R[(size_t) i]); }
}

// ---- extra test material for the targeted-context pass ------------------------------

// A short (0.3 s) sample, for testing sample.loopon against a note held much longer than
// the loaded content -- the loop point is guaranteed to be crossed many times over.
juce::File makeShortSample()
{
    const int n = (int) std::llround (kSr * 0.3);
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double phase = std::fmod (110.0 * t, 1.0);
        m[(size_t) i] = (float) (0.6 * (2.0 * phase - 1.0));
    }
    auto f = tempFile ("control_audit_sample_short.wav");
    writeMonoWav (f, m, kSr);
    return f;
}

// A plain 1 s sample for the stretch targeted test (predelay 0, so the effect should be
// audible almost immediately if it engages at all). Instrument-only: the FX build's
// stretch group is confirmed dead analytically (source forced Input), no render needed.
#if !BROKEN_FX
juce::File makeOneSecondSample()
{
    const int n = (int) std::llround (kSr * 1.0);
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double phase = std::fmod (110.0 * t, 1.0);
        m[(size_t) i] = (float) (0.6 * (2.0 * phase - 1.0));
    }
    auto f = tempFile ("control_audit_sample_1s.wav");
    writeMonoWav (f, m, kSr);
    return f;
}
#endif

// A steady tone at an exact dBFS level, both channels identical -- for level-tracing
// tests (source.intrim) where we need to read the OUTPUT level cleanly, not just detect
// "some" difference.
void makeConstantTone (juce::AudioBuffer<float>& buf, int n, double dbfs)
{
    buf.setSize (2, n, false, false, true);
    const double inc = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSr;
    std::vector<float> mono ((size_t) n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i) { mono[(size_t) i] = (float) std::sin (ph); ph += inc; }
    double sumSq = 0.0;
    for (auto v : mono) sumSq += (double) v * (double) v;
    const double rms = std::sqrt (sumSq / (double) std::max (1, n));
    const double targetLin = std::pow (10.0, dbfs / 20.0);
    const float scale = rms > 1.0e-9 ? (float) (targetLin / rms) : 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float v = mono[(size_t) i] * scale;
        buf.setSample (0, i, v);
        buf.setSample (1, i, v);
    }
}

// A deep, slow tremolo (220 Hz tone x a 1.2 Hz amplitude envelope swinging between ~13%
// and 100% of peak) at -14 dBFS RMS -- deliberately gives Flatten's envelope-follower a
// large, slow amplitude swing to level out, so its effect (or lack of one) is unambiguous.
void makeTremoloTone (juce::AudioBuffer<float>& buf, int n)
{
    buf.setSize (2, n, false, false, true);
    const double inc = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSr;
    const double envInc = 2.0 * juce::MathConstants<double>::pi * 1.2 / kSr;
    std::vector<float> mono ((size_t) n);
    double ph = 0.0, envPh = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double env = 0.15 + 0.85 * (0.5 + 0.5 * std::sin (envPh));
        mono[(size_t) i] = (float) (std::sin (ph) * env);
        ph += inc; envPh += envInc;
    }
    double sumSq = 0.0;
    for (auto v : mono) sumSq += (double) v * (double) v;
    const double rms = std::sqrt (sumSq / (double) std::max (1, n));
    const double targetLin = std::pow (10.0, -14.0 / 20.0);
    const float scale = rms > 1.0e-9 ? (float) (targetLin / rms) : 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float v = mono[(size_t) i] * scale;
        buf.setSample (0, i, v);
        buf.setSample (1, i, v);
    }
}

// ---- rendering ---------------------------------------------------------------------
void pumpSilentBlock (BrokenProcessor& proc, int n = 8)
{
    juce::AudioBuffer<float> b (2, n); b.clear();
    juce::MidiBuffer m;
    proc.processBlock (b, m);
}

// Records ~1.5 s through Sample mode with the held note, flips it into the active tape
// slot, then retriggers the same note against source.mode = Tape so the read head starts
// from a known position. Runs on an already-prepared processor.
void primeTapeTake (BrokenProcessor& proc)
{
    setChoiceReal (proc, "source.mode", SourceEngine::Sample);
    setBoolReal (proc, "tape.rec", true);

    const int total = (int) std::llround (kSr * kTotalSeconds);
    juce::AudioBuffer<float> block (2, kBlock);
    bool noteSent = false;
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (kBlock, total - pos);
        block.setSize (2, n, false, false, true);
        block.clear();
        juce::MidiBuffer midi;
        if (! noteSent) { midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); noteSent = true; }
        proc.processBlock (block, midi);
        pos += n;
    }

    setBoolReal (proc, "tape.rec", false);
    pumpSilentBlock (proc);
    setBoolReal (proc, "tape.flip", true);
    pumpSilentBlock (proc); // rising edge is caught here
    setBoolReal (proc, "tape.flip", false);
    pumpSilentBlock (proc);

    setChoiceReal (proc, "source.mode", SourceEngine::Tape);
    {
        juce::AudioBuffer<float> b (2, 8); b.clear();
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        proc.processBlock (b, m);
    }
    {
        juce::AudioBuffer<float> b (2, 8); b.clear();
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        proc.processBlock (b, m);
    }
}

std::vector<float> measureRender (BrokenProcessor& proc, const CtxDef& ctx,
                                  const juce::AudioBuffer<float>* fxInput)
{
    const int totalFrames   = (int) std::llround (kSr * kTotalSeconds);
    const int measureFrames = (int) std::llround (kSr * kMeasureSeconds);
    const int measureStart  = totalFrames - measureFrames;

    juce::AudioBuffer<float> block (2, kBlock);
    std::vector<float> out; out.reserve ((size_t) measureFrames);
    bool noteSent = false;
    int pos = 0;
    while (pos < totalFrames)
    {
        const int n = std::min (kBlock, totalFrames - pos);
        block.setSize (2, n, false, false, true);
        block.clear();
        if (ctx.feedInput && fxInput != nullptr)
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, *fxInput, ch, pos, n);

        juce::MidiBuffer midi;
        if (ctx.holdNote && ! noteSent)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            noteSent = true;
        }
        proc.processBlock (block, midi);

        for (int i = 0; i < n; ++i)
        {
            const int g = pos + i;
            if (g >= measureStart)
            {
                const float v = block.getSample (0, i);
                out.push_back (std::isfinite (v) ? v : 0.0f);
            }
        }
        pos += n;
    }
    return out;
}

// Fresh processor every call -- no state leaks between parameters or renders.
std::vector<float> renderCase (const CtxDef& ctx, const juce::File& sampleFile,
                               const juce::AudioBuffer<float>* fxInput,
                               const std::function<void (BrokenProcessor&)>& setup,
                               const std::function<void (BrokenProcessor&)>& perturb)
{
    BrokenProcessor proc;
    juce::String err;
    proc.loadSampleFile (sampleFile, err);
    applyBaseState (proc);
    setChoiceReal (proc, "source.mode", ctx.sourceModeIdx);
    if (setup) setup (proc);
    proc.setPlayConfigDetails (2, 2, kSr, kBlock);
    proc.prepareToPlay (kSr, kBlock);
    if (ctx.isTape) primeTapeTake (proc);
    if (perturb) perturb (proc);
    return measureRender (proc, ctx, fxInput);
}

// Same as measureRender, but (a) can schedule a note-off partway through so release
// stages actually run, and (b) returns the WHOLE render, not just the tail -- needed for
// anything whose effect lives in the attack/decay/release rather than the steady state.
std::vector<float> measureFullRender (BrokenProcessor& proc, const CtxDef& ctx,
                                      const juce::AudioBuffer<float>* fxInput,
                                      double totalSeconds, bool releaseAt0_5)
{
    const int totalFrames = (int) std::llround (kSr * totalSeconds);
    const int releaseFrame = (int) std::llround (kSr * 0.5);
    juce::AudioBuffer<float> block (2, kBlock);
    std::vector<float> out; out.reserve ((size_t) totalFrames);
    bool noteOnSent = false, noteOffSent = false;
    int pos = 0;
    while (pos < totalFrames)
    {
        const int n = std::min (kBlock, totalFrames - pos);
        block.setSize (2, n, false, false, true);
        block.clear();
        if (ctx.feedInput && fxInput != nullptr)
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, *fxInput, ch, pos, n);

        juce::MidiBuffer midi;
        if (ctx.holdNote && ! noteOnSent)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            noteOnSent = true;
        }
        if (releaseAt0_5 && ! noteOffSent && pos + n > releaseFrame)
        {
            const int off = std::max (0, releaseFrame - pos);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), off);
            noteOffSent = true;
        }
        proc.processBlock (block, midi);
        for (int i = 0; i < n; ++i)
        {
            const float v = block.getSample (0, i);
            out.push_back (std::isfinite (v) ? v : 0.0f);
        }
        pos += n;
    }
    return out;
}

// Like renderCase, but drives measureFullRender instead of the tail-only measureRender.
std::vector<float> renderCaseFull (const CtxDef& ctx, const juce::File& sampleFile,
                                   const juce::AudioBuffer<float>* fxInput,
                                   double totalSeconds, bool releaseAt0_5,
                                   const std::function<void (BrokenProcessor&)>& setup,
                                   const std::function<void (BrokenProcessor&)>& perturb)
{
    BrokenProcessor proc;
    juce::String err;
    proc.loadSampleFile (sampleFile, err);
    applyBaseState (proc);
    setChoiceReal (proc, "source.mode", ctx.sourceModeIdx);
    if (setup) setup (proc);
    proc.setPlayConfigDetails (2, 2, kSr, kBlock);
    proc.prepareToPlay (kSr, kBlock);
    if (ctx.isTape) primeTapeTake (proc);
    if (perturb) perturb (proc);
    return measureFullRender (proc, ctx, fxInput, totalSeconds, releaseAt0_5);
}

double rmsOf (const std::vector<float>& v)
{
    double s = 0.0;
    for (auto x : v) s += (double) x * (double) x;
    return std::sqrt (s / (double) std::max<size_t> (1, v.size()));
}

double linToDbFloor (double lin) { return lin > 1.0e-12 ? 20.0 * std::log10 (lin) : -240.0; }

double diffDbBetween (const std::vector<float>& ref, const std::vector<float>& moved)
{
    const size_t n = std::min (ref.size(), moved.size());
    if (n == 0) return -300.0;
    double sumSqRef = 0.0, sumSqDiff = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double d = (double) moved[i] - (double) ref[i];
        sumSqDiff += d * d;
        sumSqRef  += (double) ref[i] * (double) ref[i];
    }
    const double rmsRef  = std::sqrt (sumSqRef  / (double) n);
    const double rmsDiff = std::sqrt (sumSqDiff / (double) n);
    const double floorLin = 1.0e-9; // -180 dBFS reference floor
    const double ratio = rmsDiff / std::max (rmsRef, floorLin);
    return ratio > 1.0e-15 ? 20.0 * std::log10 (ratio) : -300.0;
}

// ---- parameter classification for the sweep ----------------------------------------
enum class PKind { Bool, Choice, Float };

PKind classify (juce::AudioProcessorParameter* p)
{
    if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)   return PKind::Bool;
    if (dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr) return PKind::Choice;
    return PKind::Float;
}

std::vector<float> candidatesFor (juce::RangedAudioParameter* rp, PKind kind)
{
    std::vector<float> out;
    if (kind == PKind::Bool)
    {
        out.push_back (rp->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    else if (kind == PKind::Choice)
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (rp);
        const int n = choice->choices.size();
        // getValue()/convertFrom0to1/convertTo0to1 are called through the RangedAudioParameter
        // base pointer: AudioParameterChoice hides getValue() as private on itself.
        const int cur = (int) std::round (rp->convertFrom0to1 (rp->getValue()));
        for (int i = 0; i < n; ++i)
            if (i != cur) out.push_back (rp->convertTo0to1 ((float) i));
    }
    else
    {
        out.push_back (0.1f);
        out.push_back (0.9f);
    }
    return out;
}

struct ParamInfo { juce::String id; juce::String name; PKind kind; };

// ---- the three named checks ---------------------------------------------------------
struct TestOutcome { bool pass; juce::String message; };

TestOutcome testCustomWaveshaperCurve (const juce::File& sampleFile, const CtxDef& ctx,
                                       const juce::AudioBuffer<float>* fxInput)
{
    auto renderWithPoints = [&] (const std::function<void (BrokenProcessor&)>& setPoints)
    {
        return renderCase (ctx, sampleFile, fxInput,
            [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "ws.morph"), 1.0f); },
            setPoints);
    };

    auto straight = renderWithPoints ([] (BrokenProcessor& proc)
    {
        for (int k = 0; k < params::curvePointCount; ++k)
        {
            const float v = -1.0f + 2.0f * (float) k / (float) (params::curvePointCount - 1);
            setReal (rangedOf (proc, params::curvePointId (k + 1)), v);
        }
    });
    auto folded = renderWithPoints ([] (BrokenProcessor& proc)
    {
        for (int k = 0; k < params::curvePointCount; ++k)
        {
            const float u = -1.0f + 2.0f * (float) k / (float) (params::curvePointCount - 1);
            // dsp::Waveshaper's own Fold curve (case 3): a strongly different shape.
            const float v2 = 2.0f * u;
            const float t = v2 + 1.0f;
            const float fmod4 = t - 4.0f * std::floor (t / 4.0f);
            const float v = 1.0f - std::abs (fmod4 - 2.0f);
            setReal (rangedOf (proc, params::curvePointId (k + 1)), v);
        }
    });
    const double d = diffDbBetween (straight, folded);
    const bool pass = d > kWiredThresholdDb;
    return { pass, "straight-line vs fold: diff " + juce::String (d, 1) + " dB" };
}

#if !BROKEN_FX
TestOutcome testDrawnOscillator (const juce::File& sampleFile)
{
    auto renderDraw = [&] (const std::function<void (BrokenProcessor&)>& setDraw)
    {
        CtxDef ctx { "Osc", SourceEngine::Osc, false, true, false };
        return renderCase (ctx, sampleFile, nullptr,
            [] (BrokenProcessor& proc) { setChoiceReal (proc, "osc.mode", 2); }, // Draw
            setDraw);
    };
    auto refDraw = renderDraw ([] (BrokenProcessor&) {}); // Params.h default: a sine
    auto altDraw = renderDraw ([] (BrokenProcessor& proc)
    {
        for (int k = 0; k < params::drawPointCount; ++k)
            setReal (rangedOf (proc, params::drawPointId (k + 1)), (k % 2 == 0) ? 1.0f : -1.0f);
    });
    const double dDraw = diffDbBetween (refDraw, altDraw);

    auto renderHarm = [&] (const std::function<void (BrokenProcessor&)>& setHarm)
    {
        CtxDef ctx { "Osc", SourceEngine::Osc, false, true, false };
        return renderCase (ctx, sampleFile, nullptr,
            [] (BrokenProcessor& proc) { setChoiceReal (proc, "osc.mode", 1); }, // Harmonic
            setHarm);
    };
    auto refHarm = renderHarm ([] (BrokenProcessor&) {}); // Params.h default: 1/k recipe
    auto altHarm = renderHarm ([] (BrokenProcessor& proc)
    {
        for (int k = 1; k <= params::harmonicCount; ++k)
            setReal (rangedOf (proc, params::harmonicId (k)), (k == 3) ? 100.0f : 0.0f);
    });
    const double dHarm = diffDbBetween (refHarm, altHarm);

    const bool pass = dDraw > kWiredThresholdDb && dHarm > kWiredThresholdDb;
    return { pass, "draw-bank diff " + juce::String (dDraw, 1)
                  + " dB, harmonic-bank diff " + juce::String (dHarm, 1) + " dB" };
}
#else
// item 2 (Table mod source): unlike the old expectation ("no path makes the drawn/harmonic
// table affect the FX build's output" -- true before Table existed, since SourceEngine's
// Input case never reads drawPts/harmonics), the Table mod source now reads exactly this
// content via Engine::rebuildModTableIfNeeded regardless of source.mode, so both banks ARE
// wired in FX once mod.source=Table -- verified the same way the instrument verifies them
// (drawn points into osc.mode=Draw, harmonic amplitudes into osc.mode=Harmonic), just with
// mod.source=Table + mod.on=1 standing in for the instrument's source.mode=Osc.
TestOutcome testDrawnOscillator (const juce::File& sampleFile, const juce::AudioBuffer<float>& fxInput)
{
    auto renderWithOscMode = [&] (int oscModeIdx, const std::function<void (BrokenProcessor&)>& setBank)
    {
        CtxDef ctx { "FX", SourceEngine::Input, true, false, false };
        return renderCase (ctx, sampleFile, &fxInput,
            [oscModeIdx] (BrokenProcessor& proc)
            {
                setChoiceReal (proc, "osc.mode", oscModeIdx);
                setChoiceReal (proc, "mod.source", 4); // Table
                setReal (rangedOf (proc, "mod.on"), 1.0f);
                setChoiceReal (proc, "mod.mode", params::modModes.indexOf ("AM"));
                setReal (rangedOf (proc, "mod.amount"), 0.8f);
            },
            setBank);
    };
    auto refDraw = renderWithOscMode (2, [] (BrokenProcessor&) {}); // Draw, Params.h default: a sine
    auto altDraw = renderWithOscMode (2, [] (BrokenProcessor& proc)
    {
        for (int k = 0; k < params::drawPointCount; ++k)
            setReal (rangedOf (proc, params::drawPointId (k + 1)), (k % 2 == 0) ? 1.0f : -1.0f);
    });
    const double dDraw = diffDbBetween (refDraw, altDraw);

    auto refHarm = renderWithOscMode (1, [] (BrokenProcessor&) {}); // Harmonic, Params.h default: 1/k recipe
    auto altHarm = renderWithOscMode (1, [] (BrokenProcessor& proc)
    {
        for (int k = 1; k <= params::harmonicCount; ++k)
            setReal (rangedOf (proc, params::harmonicId (k)), (k == 3) ? 100.0f : 0.0f);
    });
    const double dHarm = diffDbBetween (refHarm, altHarm);

    const bool pass = dDraw > kWiredThresholdDb && dHarm > kWiredThresholdDb;
    return { pass, "FX mod.source=Table: draw-bank diff " + juce::String (dDraw, 1)
                  + " dB, harmonic-bank diff " + juce::String (dHarm, 1) + " dB" };
}
#endif

TestOutcome testModSourceSample (const juce::File& sampleA, const juce::File& sampleB,
                                 const CtxDef& ctx, const juce::AudioBuffer<float>* fxInput)
{
    auto renderWithSample = [&] (const juce::File& sf)
    {
        return renderCase (ctx, sf, fxInput,
            [] (BrokenProcessor& proc)
            {
                setChoiceReal (proc, "mod.source", 2); // Sample
            },
            nullptr);
    };
    auto a = renderWithSample (sampleA);
    auto b = renderWithSample (sampleB);
    const double d = diffDbBetween (a, b);
    return { d > kWiredThresholdDb, "mod.source=Sample, sample A vs B: diff " + juce::String (d, 1) + " dB" };
}

// Reproduces CurveEditor::grabCurveFromSample() headlessly: the CYCLE window
// (source.winpos/source.winlen) of the loaded sample becomes the 128 custom curve
// points (centred on its mean, peak-normalized).
TestOutcome testFromSampleCurve (const juce::File& sampleFile, const CtxDef& ctx,
                                 const juce::AudioBuffer<float>* fxInput)
{
    BrokenProcessor tmp;
    juce::String err;
    tmp.loadSampleFile (sampleFile, err);
    const auto& buf = tmp.getSampleBuffer();

    const double last = (double) (buf.size() - 1);
    const double winpos = 0.1, winlen = 512.0; // Params.h defaults for source.winpos/winlen
    const double start = juce::jlimit (0.0, last, winpos * last);
    const double avail = last - start > 2.0 ? last - start : 2.0;
    const double len = juce::jlimit (2.0, avail, winlen);

    std::array<float, (size_t) params::curvePointCount> pts {};
    double mean = 0.0;
    for (int k = 0; k < params::curvePointCount; ++k)
    {
        const double idx = start + len * (double) k / (double) params::curvePointCount;
        const auto i0 = (size_t) juce::jlimit (0.0, last, std::floor (idx));
        const auto i1 = (size_t) juce::jlimit (0.0, last, (double) i0 + 1.0);
        const float fr = (float) (idx - std::floor (idx));
        pts[(size_t) k] = buf[i0] + fr * (buf[i1] - buf[i0]);
        mean += pts[(size_t) k];
    }
    mean /= (double) params::curvePointCount;
    float peak = 0.0f;
    for (auto& v : pts) { v -= (float) mean; peak = std::max (peak, std::abs (v)); }
    const float g = peak > 1.0e-6f ? 1.0f / peak : 1.0f;
    for (auto& v : pts) v *= g;

    auto renderWithCurve = [&] (bool fromSample)
    {
        return renderCase (ctx, sampleFile, fxInput,
            [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "ws.morph"), 1.0f); },
            [&] (BrokenProcessor& proc)
            {
                if (fromSample)
                    for (int k = 0; k < params::curvePointCount; ++k)
                        setReal (rangedOf (proc, params::curvePointId (k + 1)), pts[(size_t) k]);
            });
    };
    auto base = renderWithCurve (false);
    auto fromSample = renderWithCurve (true);
    const double d = diffDbBetween (base, fromSample);
    return { d > kWiredThresholdDb, "FROM SAMPLE curve vs default Custom diagonal: diff " + juce::String (d, 1) + " dB" };
}
// ======================================================================================
// Phase 2: targeted-context pass. The general sweep above uses ONE rich base state for
// every parameter; that is fair for "does moving this in a plausible mix change the
// sound", but it also means some parameters sit dead only because the base state never
// satisfies the ONE precondition they need (an aux route that isn't floor-clamped, a
// released note, a non-identity curve, ...). This pass gives each such parameter the
// specific condition it needs, and reports whether it moves the needle THERE. Anything
// that still doesn't move under its own best-case condition is a real DEFECT candidate.
// ======================================================================================
enum class Verdict { Wired, DeadByDesign, Defect };
struct ParamVerdict { Verdict v; juce::String reason; };

// Reasons for parameters the targeted pass does not touch directly (mostly the two big
// banks, plus a handful of context-forcing facts that don't need their own render).
juce::String curatedReason (const juce::String& id, bool fx)
{
    // item 2 (Table mod source): osc.h*/osc.d* are read by TWO independent mechanisms now
    // -- SourceEngine's Harmonic/Draw osc.mode branches (Sample source's own OSC mode,
    // instrument only) AND Engine::rebuildModTableIfNeeded (the Table mod source, both
    // builds, keyed on the SAME osc.mode/osc.h*/osc.d* parameters but a separate 4096-entry
    // table). The general sweep's base state leaves BOTH mod.source and osc.mode at their
    // Wave/Osc defaults, so neither mechanism is exercised there; both are verified live by
    // the named drawn-oscillator/harmonic test instead (mod.source=Table in FX, osc.mode
    // directly in the instrument).
    if (id.startsWith ("osc.h"))
        return juce::String ("osc.mode is left at its default (Wave) and mod.source at its "
               "default (Osc) in every general-sweep context, so neither SourceEngine's "
               "Harmonic-mode partial table (playOsc(), oscMode==1) nor the Table mod source's "
               "own copy (Engine::rebuildModTableIfNeeded) is ever built -- verified live by the "
               "named drawn-oscillator/harmonic test") + (fx ? " (mod.source=Table)." : ".");
    if (id.startsWith ("osc.d"))
        return juce::String ("osc.mode is left at its default (Wave) and mod.source at its "
               "default (Osc) in every general-sweep context, so neither SourceEngine's "
               "Draw-mode table (playOsc(), oscMode==2) nor the Table mod source's own copy "
               "(Engine::rebuildModTableIfNeeded) is ever built -- verified live by the named "
               "drawn-oscillator test") + (fx ? " (mod.source=Table)." : ".");
    if (id == "source.ext")
        return "the general sweep's base source.pitch (40% of [-48,48] = -19.2st) already sits "
               "inside the +-24st unclamped range, so PluginProcessor.cpp:142-143's "
               "ext ? pitch : clamp(pitch,-24,24) never actually clips -- toggling ext changes "
               "nothing at THIS base pitch. Not exercised by a dedicated targeted test.";
    if (fx)
    {
        // osc.mode/source.oscwave: item 2 gives these a real path in FX (the Table mod
        // source), so they are NOT lumped in with the source-forced-Input group below --
        // they get their own targeted-pass test (mirrors testDrawnOscillator's mod.source=
        // Table logic) instead of a blanket DeadByDesign.
        if (id == "source.mode" || id == "source.pitchmix" || id == "source.xfade")
            return "gatherParams forces v.sourceMode = SourceEngine::Input regardless of the "
                   "source.mode parameter (PluginProcessor.cpp:136); this id only matters for "
                   "Sample/Cycle/Tape playback, none of which ever run in the FX build. (Hidden "
                   "in the FX editor's OSCILLATOR block, item 2.)";
        // item 4: winpos/winlen no longer route through SourceEngine::playCycle() in the FX
        // build (that never runs, source is forced Input) but they DO gate CurveEditor::
        // grabCurveFromSample()'s CYCLE window -- verified by the "sample as a shape (b)"
        // named check (FROM SAMPLE vs default diagonal uses the Params.h winpos/winlen
        // defaults already); not swept here because the general sweep never invokes FROM
        // SAMPLE (it is a one-shot UI action, not a live audio-path read).
        if (id == "source.winpos" || id == "source.winlen")
            return "not read anywhere in the live audio path (SourceEngine::processSample's "
                   "Input case never calls playCycle()); the only consumer is CurveEditor::"
                   "grabCurveFromSample() (FROM SAMPLE), a one-shot UI action the render sweep "
                   "never triggers -- covered instead by the \"sample as a shape (b)\" named "
                   "check, which reproduces grabCurveFromSample() headlessly and IS wired.";
        if (id == "sample.regstart" || id == "sample.regend")
            return "not read by SourceEngine's Input case (returns the raw input directly, "
                   "never calling playRegioned()), but IS read by tickModSource()'s "
                   "regionBounds() call for mod.source=Sample -- the general sweep's default "
                   "mod.source is Osc, so this needs mod.source=Sample to move the needle; "
                   "covered by the \"sample as a shape (a)\" named check (two different loaded "
                   "samples, mod.source=Sample) rather than a per-parameter targeted test.";
        if (id == "sample.loopon" || id == "sample.loopstyle" || id == "sample.rev"
            || id == "sample.xfade" || id == "sample.xfadeshape")
            return "SourceEngine::processSample's Input case (SourceEngine.h:196) returns the raw "
                   "input directly; playRegioned() (which reads all of these) is never called "
                   "because source is forced to Input (PluginProcessor.cpp:136). tickModSource() "
                   "(mod.source=Sample/Tape) reads only the region bounds, not loop style/reverse/"
                   "crossfade -- confirmed by reading SourceEngine::tickModSource. Hidden in the "
                   "FX pop-out SampleEditor (item 4).";
        if (id == "voice.mode" || id == "voice.retrig" || id == "play.hold")
            return "the FX build never sends a note event: acceptsMidi() is false, no host feeds "
                   "it MIDI, and this audit's FX context sends none either (matching real usage); "
                   "not visible in the FX editor (the PLAY panel is hidden under BROKEN_FX).";
        if (id == "noise.amp" || id == "noise.phase")
            return "SourceEngine::nextNoise() (the only reader of these) is reached only when "
                   "source.mode==Noise; the FX build forces Input (PluginProcessor.cpp:136), so "
                   "Noise mode is unreachable. Hidden in the FX editor's MODULE TRIMS panel "
                   "(item 5: Noise-source-only controls).";
        if (id == "amp.a" || id == "amp.d" || id == "amp.s")
            return "freeRun (source forced to Input) hard-codes the amp-envelope gain to a constant "
                   "1.0 instead of reading aenv.processSample() (Voice.h:254), so none of the amp "
                   "envelope's own shape parameters can ever reach the output. Same mechanism "
                   "confirmed for amp.r by the targeted pass below. Not visible in the FX editor "
                   "(the PLAY panel that hosts amp A/D/S/R is hidden under BROKEN_FX).";
    }
    return "dead in every context; not covered by a targeted test in this pass -- needs a "
           "closer look before calling it a defect.";
}

// ---------------------------------------------------------------------------------
// Each block below: set up the ONE condition the group needs, move the parameter(s),
// measure, and write the result into `verdict` (overriding whatever the general sweep
// concluded). Printed as it runs so the numbers are auditable, not just the verdict.
// ---------------------------------------------------------------------------------
void runTargetedPass (std::map<juce::String, ParamVerdict>& verdict,
                      const juce::File& sampleA,
                      const juce::AudioBuffer<float>& fxInputBuf)
{
    juce::ignoreUnused (fxInputBuf); // only read on the BROKEN_FX branches below
    std::cout << "\n=== TARGETED-CONTEXT PASS ===\n";

#if BROKEN_FX
    const CtxDef genCtx { "FX", SourceEngine::Input, true, false, false };
    const juce::AudioBuffer<float>* genInput = &fxInputBuf;
#else
    const CtxDef genCtx { "Sample", SourceEngine::Sample, false, true, false };
    const juce::AudioBuffer<float>* genInput = nullptr;
#endif

    auto record = [&] (const char* id, double diffDb, const char* conditionDesc)
    {
        const bool wired = diffDb > kWiredThresholdDb;
        verdict[id] = wired
            ? ParamVerdict { Verdict::Wired, juce::String ("targeted: ") + conditionDesc }
            : ParamVerdict { Verdict::Defect,
                juce::String ("still dead under its own best-case condition (") + conditionDesc
                    + "); diff " + juce::String (diffDb, 1) + " dB -- needs code review, not just a base-state fix." };
        std::cout << "  " << id << ": diff " << juce::String (diffDb, 2) << " dB [" << conditionDesc
                  << "] -> " << (wired ? "WIRED" : "DEFECT?") << "\n";
    };

    // ---- ws.morph: needs a non-identity curve so "shaped" differs from "dry" ----------
    {
        auto setup = [] (BrokenProcessor& proc)
        {
            setChoiceReal (proc, "ws.curve", params::wsCurves.indexOf ("HardClip"));
            setReal (rangedOf (proc, "ws.drive"), 18.0f);
        };
        auto lo = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "ws.morph"), 0.1f); });
        auto hi = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "ws.morph"), 0.9f); });
        record ("ws.morph", diffDbBetween (lo, hi), "ws.curve=HardClip, drive=18dB");
    }

    // ---- mod.fmindex: needs mod.mode=FM ------------------------------------------------
    {
        auto setup = [] (BrokenProcessor& proc)
        {
            setChoiceReal (proc, "mod.mode", params::modModes.indexOf ("FM"));
            setReal (rangedOf (proc, "mod.on"), 1.0f);
            setReal (rangedOf (proc, "mod.amount"), 0.8f);
        };
        auto lo = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "mod.fmindex"), 0.1f); });
        auto hi = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "mod.fmindex"), 0.9f); });
        const double d = diffDbBetween (lo, hi);
        // item 3 (Input FM): FM used to be structurally dead on the Input source (it only
        // ever called src.setRateMod(), read by Sample/Cycle/Osc/Tape playback in
        // SourceEngine.h, never by the Input case). InputVarispeed.h now gives Input its
        // own rate-modulated read head fed the identical r term, applied in Voice::render
        // right after liveShift -- so this is genuinely wired in BOTH builds now (the FX
        // build's ONLY source is Input, so this is exactly the path FX always takes).
        record ("mod.fmindex", d, "mod.mode=FM, mod.on=1, mod.amount=0.8");
    }

    // ---- ws.randseed (bonus, cheap): needs ws.curve=Random -----------------------------
    {
        auto setup = [] (BrokenProcessor& proc) { setChoiceReal (proc, "ws.curve", params::wsCurves.indexOf ("Random")); };
        auto lo = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "ws.randseed"), 0.1f); });
        auto hi = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "ws.randseed"), 0.9f); });
        record ("ws.randseed", diffDbBetween (lo, hi), "ws.curve=Random");
    }

    // ---- fenv/aux group: needs an escapable filter cutoff, a positive envamt, and a
    // released note (so attack/decay/sustain/release all get exercised). Instrument only;
    // FX is annotated analytically per the coordinator's own prediction (no note ever
    // gates fenv/auxenv there -- confirmed true regardless of cutoff/envamt/aux state).
#if !BROKEN_FX
    {
        auto setup = [] (BrokenProcessor& proc)
        {
            setReal (rangedOf (proc, "flt.cutoff"), 1500.0f);   // well above the 500 Hz floor
            setReal (rangedOf (proc, "flt.envamt"), 30.0f);     // +50% of the +-60st range, POSITIVE
            setReal (rangedOf (proc, "aux.amount"), 0.6f);      // positive
            setChoiceReal (proc, "aux.dest", 0);                // FilterCut (explicit, matches default)
        };
        const CtxDef relCtx { "Sample", SourceEngine::Sample, false, true, false };
        const double totalSec = 1.2; // 0.5 s held, then release -- covers A/D/S/R fully
        auto reference = renderCaseFull (relCtx, sampleA, nullptr, totalSec, true, setup, nullptr);
        const char* cond = "flt.cutoff=1500Hz, flt.envamt=+30st, aux.amount=0.6, note released at 0.5s";

        for (const char* id : { "fenv.a", "fenv.d", "fenv.s", "fenv.r",
                                "flt.envamt", "aux.a", "aux.d", "aux.s", "aux.r", "aux.amount" })
        {
            auto lo = renderCaseFull (relCtx, sampleA, nullptr, totalSec, true, setup,
                [id] (BrokenProcessor& proc) { setNorm (rangedOf (proc, id), 0.1f); });
            auto hi = renderCaseFull (relCtx, sampleA, nullptr, totalSec, true, setup,
                [id] (BrokenProcessor& proc) { setNorm (rangedOf (proc, id), 0.9f); });
            record (id, std::max (diffDbBetween (reference, lo), diffDbBetween (reference, hi)), cond);
        }
        // aux.dest: choice, try the 4 non-default routes
        {
            double best = -300.0;
            for (int i = 1; i < params::auxDests.size(); ++i)
            {
                auto moved = renderCaseFull (relCtx, sampleA, nullptr, totalSec, true, setup,
                    [i] (BrokenProcessor& proc) { setChoiceReal (proc, "aux.dest", i); });
                best = std::max (best, diffDbBetween (reference, moved));
            }
            record ("aux.dest", best, cond);
        }
    }
#else
    for (const char* id : { "fenv.a", "fenv.d", "fenv.s", "fenv.r", "flt.envamt",
                            "aux.a", "aux.d", "aux.s", "aux.r", "aux.amount", "aux.dest" })
    {
        verdict[id] = { Verdict::DeadByDesign,
            "Instrument-only targeted test skipped for FX per spec: the FX build never sends a "
            "note-on (acceptsMidi()==false, no host MIDI, no PLAY latch here), so Voice::noteOn's "
            "fenv.gateOn/auxenv.gateOn (Voice.h:172-173) never fire -- both envelopes sit in "
            "EnvelopeADSR::Stage::Idle at level 0 all render regardless of cutoff/envamt/aux state "
            "(Voice.h:234-236, 205-207)." };
        std::cout << "  " << id << ": SKIPPED in FX (no note ever gates it) -> DEAD-BY-DESIGN\n";
    }
#endif

    // ---- amp.r: needs a released note ---------------------------------------------------
    {
        auto lo = renderCaseFull (genCtx, sampleA, genInput, 1.2, true, nullptr,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "amp.r"), 0.1f); });
        auto hi = renderCaseFull (genCtx, sampleA, genInput, 1.2, true, nullptr,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "amp.r"), 0.9f); });
        const double d = diffDbBetween (lo, hi);
#if BROKEN_FX
        // Expected dead regardless of release: freeRun hard-codes the gain to 1.0.
        verdict["amp.r"] = { Verdict::DeadByDesign,
            "freeRun (source forced to Input) hard-codes the amp-envelope gain to 1.0 instead of "
            "reading aenv.processSample() (Voice.h:254), so amp.r's release time never has "
            "anything to affect, released note or not; diff " + juce::String (d, 1) + " dB." };
        std::cout << "  amp.r: diff " << juce::String (d, 2) << " dB [released note] -> DEAD-BY-DESIGN (freeRun bypass)\n";
#else
        record ("amp.r", d, "released note (held 0.5s, release measured over the full render)");
#endif
    }

    // ---- sample.loopon: 0.3s sample + 1.5s note, so the loop point is crossed for sure --
    {
        auto shortSample = makeShortSample();
        const CtxDef ctx { "Sample", SourceEngine::Sample, false, true, false };
        auto off = renderCaseFull (ctx, shortSample, nullptr, 1.5, false, nullptr,
            [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "sample.loopon"), 0.0f); });
        auto on  = renderCaseFull (ctx, shortSample, nullptr, 1.5, false, nullptr,
            [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "sample.loopon"), 1.0f); });
        const double d = diffDbBetween (off, on);
#if BROKEN_FX
        verdict["sample.loopon"] = { Verdict::DeadByDesign,
            "SourceEngine::processSample's Input case (SourceEngine.h:196) returns the raw input "
            "directly, never calling playRegioned() (the only reader of loopOn); source is forced "
            "to Input in the FX build (PluginProcessor.cpp:136). diff " + juce::String (d, 1) + " dB." };
        std::cout << "  sample.loopon: diff " << juce::String (d, 2) << " dB [0.3s sample, 1.5s note] -> DEAD-BY-DESIGN (source forced Input)\n";
#else
        record ("sample.loopon", d, "0.3s sample, 1.5s held note (loop point crossed repeatedly)");
#endif
    }

    // ---- voice.spread: needs unison on --------------------------------------------------
    {
        auto setup = [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "voice.unison"), 1.0f); };
        auto lo = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "voice.spread"), 0.1f); });
        auto hi = renderCase (genCtx, sampleA, genInput, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "voice.spread"), 0.9f); });
        const double d = diffDbBetween (lo, hi);
#if BROKEN_FX
        // Structural, not a missing precondition: voice.spread feeds ONLY detuneSemis, which
        // Voice::applyParams folds into src.setTranspose(sourcePitch + detuneSemis) -- read by
        // SourceEngine's Sample/Cycle/Osc/Tape rate math, never by the Input case. Input mode's
        // OWN pitch path is liveShift.setSemitones(vp.sourcePitch) (Voice.h, right after
        // src.setOscWave/etc in applyParams), which reads sourcePitch directly and does NOT
        // include detuneSemis. So even with unison on, the FX build's one rendered voice can
        // never be detuned by spread. (Also moot in practice: the PLAY panel that hosts
        // Unison/Spread is hidden entirely in the FX editor.)
        verdict["voice.spread"] = { Verdict::DeadByDesign,
            "structural: feeds only detuneSemis via src.setTranspose(), read by Sample/Cycle/Osc/"
            "Tape rate math in SourceEngine.h, never by the Input case (SourceEngine.h:196-197); "
            "Input's own pitch path is liveShift.setSemitones(vp.sourcePitch) (Voice.h), which "
            "does not include detuneSemis. diff with unison=1: " + juce::String (d, 1) + " dB. "
            "Also not reachable from the UI -- the PLAY panel (Unison/Spread) is hidden in the "
            "FX editor (MangleView.h under BROKEN_FX)." };
        std::cout << "  voice.spread: diff " << juce::String (d, 2)
                  << " dB [voice.unison=1] -> DEAD-BY-DESIGN (structural, not a precondition gap; also UI-hidden)\n";
#else
        record ("voice.spread", d, "voice.unison=1");
#endif
    }

    // ---- midi.bendrange: needs an actual pitch-bend message ----------------------------
    {
        auto renderBend = [&] (float norm)
        {
            BrokenProcessor proc;
            juce::String err;
            proc.loadSampleFile (sampleA, err);
            applyBaseState (proc);
            setChoiceReal (proc, "source.mode", genCtx.sourceModeIdx);
            setNorm (rangedOf (proc, "midi.bendrange"), norm);
            proc.setPlayConfigDetails (2, 2, kSr, kBlock);
            proc.prepareToPlay (kSr, kBlock);

            const int totalFrames = (int) std::llround (kSr * kTotalSeconds);
            const int measureFrames = (int) std::llround (kSr * kMeasureSeconds);
            const int measureStart = totalFrames - measureFrames;
            juce::AudioBuffer<float> block (2, kBlock);
            std::vector<float> out; out.reserve ((size_t) measureFrames);
            bool sent = false;
            int pos = 0;
            while (pos < totalFrames)
            {
                const int n = std::min (kBlock, totalFrames - pos);
                block.setSize (2, n, false, false, true);
                block.clear();
                if (genInput != nullptr) for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, *genInput, ch, pos, n);
                juce::MidiBuffer midi;
                if (! sent)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                    midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0); // full up
                    sent = true;
                }
                proc.processBlock (block, midi);
                for (int i = 0; i < n; ++i)
                {
                    const int g = pos + i;
                    if (g >= measureStart)
                    {
                        const float v = block.getSample (0, i);
                        out.push_back (std::isfinite (v) ? v : 0.0f);
                    }
                }
                pos += n;
            }
            return out;
        };
        auto lo = renderBend (0.1f), hi = renderBend (0.9f);
        record ("midi.bendrange", diffDbBetween (lo, hi), "pitch-wheel message at full up (16383)");
    }

    // ---- tape.rec: record then play the take in Tape mode -------------------------------
#if !BROKEN_FX
    {
        auto renderNoRecord = [&] ()
        {
            BrokenProcessor proc;
            juce::String err;
            proc.loadSampleFile (sampleA, err);
            applyBaseState (proc);
            setChoiceReal (proc, "source.mode", SourceEngine::Tape);
            proc.setPlayConfigDetails (2, 2, kSr, kBlock);
            proc.prepareToPlay (kSr, kBlock);
            const CtxDef ctx { "Tape", SourceEngine::Tape, false, true, false };
            return measureRender (proc, ctx, nullptr);
        };
        auto renderWithRecord = [&] ()
        {
            BrokenProcessor proc;
            juce::String err;
            proc.loadSampleFile (sampleA, err);
            applyBaseState (proc);
            setChoiceReal (proc, "source.mode", SourceEngine::Tape);
            proc.setPlayConfigDetails (2, 2, kSr, kBlock);
            proc.prepareToPlay (kSr, kBlock);
            primeTapeTake (proc); // records ~1.5s, flips, retriggers in Tape mode
            const CtxDef ctx { "Tape", SourceEngine::Tape, false, true, false };
            return measureRender (proc, ctx, nullptr);
        };
        const double d = diffDbBetween (renderNoRecord(), renderWithRecord());
        record ("tape.rec", d, "record+flip (API round trip) vs an empty tape, both played back in Tape mode");
    }
#else
    {
        // item 5: FX's TAPE block is back. gatherParams still forces the SOURCE to Input
        // (PluginProcessor.cpp:136), so tape.rec/tape.flip can never make the SOURCE
        // become Tape -- but mod.source=Tape reads whichever tape buffer is ACTIVE
        // (Engine::applyParams -> v.setTapeData(tape.activeData()...), consumed by
        // SourceEngine::tickModSource(fromTape=true,...)) independent of source.mode, so
        // both ARE wired via the modulator. Records ~0.75s of the FX input buffer.
        auto recordTake = [&] (BrokenProcessor& proc)
        {
            setBoolReal (proc, "tape.rec", true);
            const int recFrames = (int) std::llround (kSr * 0.75);
            juce::AudioBuffer<float> block (2, kBlock);
            int pos = 0;
            while (pos < recFrames)
            {
                const int n = std::min (kBlock, recFrames - pos);
                block.setSize (2, n, false, false, true);
                for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, fxInputBuf, ch, pos, n);
                juce::MidiBuffer midi;
                proc.processBlock (block, midi);
                pos += n;
            }
            setBoolReal (proc, "tape.rec", false);
            pumpSilentBlock (proc);
        };
        auto makeTapeModProc = [&] ()
        {
            auto proc = std::make_unique<BrokenProcessor>();
            juce::String err;
            proc->loadSampleFile (sampleA, err);
            applyBaseState (*proc);
            setChoiceReal (*proc, "mod.source", 3); // Tape
            setReal (rangedOf (*proc, "mod.on"), 1.0f);
            setChoiceReal (*proc, "mod.mode", params::modModes.indexOf ("AM"));
            setReal (rangedOf (*proc, "mod.amount"), 0.8f);
            proc->setPlayConfigDetails (2, 2, kSr, kBlock);
            proc->prepareToPlay (kSr, kBlock);
            return proc;
        };
        const CtxDef fxTapeCtx { "FX", SourceEngine::Input, true, false, false };

        auto empty = makeTapeModProc();
        auto noRecordOut = measureRender (*empty, fxTapeCtx, &fxInputBuf);

        auto recordedNoFlip = makeTapeModProc();
        recordTake (*recordedNoFlip); // deliberately no FLIP: ACTIVE stays the empty side
        auto recordedNoFlipOut = measureRender (*recordedNoFlip, fxTapeCtx, &fxInputBuf);

        auto recordedFlipped = makeTapeModProc();
        recordTake (*recordedFlipped);
        setBoolReal (*recordedFlipped, "tape.flip", true);
        pumpSilentBlock (*recordedFlipped); // rising edge caught here
        setBoolReal (*recordedFlipped, "tape.flip", false);
        pumpSilentBlock (*recordedFlipped);
        auto recordedFlippedOut = measureRender (*recordedFlipped, fxTapeCtx, &fxInputBuf);

        const double dRec = diffDbBetween (noRecordOut, recordedFlippedOut);
        record ("tape.rec", dRec,
            "FX: mod.source=Tape, mod.on=1, mod.amount=0.8; record 0.75s then FLIP vs an empty tape");

        // tape.flip isolated from tape.rec: recording without flipping leaves ACTIVE
        // (and therefore the Tape mod source's output) unchanged from the empty case;
        // only the FLIP rising edge moves it.
        const double dFlip = diffDbBetween (recordedNoFlipOut, recordedFlippedOut);
        record ("tape.flip", dFlip, "FX: same 0.75s recording, FLIP vs no FLIP (isolates the rising edge)");
    }
#endif

    // ---- source.intrim: -30 dBFS in, -12 vs +12 dB trim, both builds, RMS printed -------
    {
#if BROKEN_FX
        const CtxDef inputCtx { "FX", SourceEngine::Input, true, false, false };
#else
        const CtxDef inputCtx { "Input", SourceEngine::Input, true, true, false };
#endif
        juce::AudioBuffer<float> minus30Buf;
        makeConstantTone (minus30Buf, (int) std::llround (kSr * kTotalSeconds), -30.0);

        auto renderIntrim = [&] (float dbVal)
        {
            return renderCase (inputCtx, sampleA, &minus30Buf, nullptr,
                [dbVal] (BrokenProcessor& proc) { setReal (rangedOf (proc, "source.intrim"), dbVal); });
        };
        auto lo = renderIntrim (-12.0f), hi = renderIntrim (12.0f);
        const double rmsLo = rmsOf (lo), rmsHi = rmsOf (hi);
        const double dbLo = linToDbFloor (rmsLo), dbHi = linToDbFloor (rmsHi);
        const double d = diffDbBetween (lo, hi);
        std::cout << "  source.intrim (" << inputCtx.name << ", -30dBFS in, rich base state): out RMS "
                  << juce::String (dbLo, 1) << " dBFS (trim -12dB) vs " << juce::String (dbHi, 1)
                  << " dBFS (trim +12dB), diff " << juce::String (d, 2) << " dB\n";

        // Isolated variant: every module off, so nothing downstream can mask the trim.
        auto renderIsolated = [&] (float dbVal)
        {
            return renderCase (inputCtx, sampleA, &minus30Buf,
                [] (BrokenProcessor& proc)
                {
                    for (auto id : { "mod.on", "ws.on", "flt.on", "res.on", "inv.on", "dly.on", "stretch.on", "flat.on" })
                        setReal (rangedOf (proc, id), 0.0f);
                },
                [dbVal] (BrokenProcessor& proc) { setReal (rangedOf (proc, "source.intrim"), dbVal); });
        };
        auto loIso = renderIsolated (-12.0f), hiIso = renderIsolated (12.0f);
        const double dIso = diffDbBetween (loIso, hiIso);
        const double dbLoIso = linToDbFloor (rmsOf (loIso)), dbHiIso = linToDbFloor (rmsOf (hiIso));
        std::cout << "    isolated (every module off): out RMS " << juce::String (dbLoIso, 1)
                  << " dBFS vs " << juce::String (dbHiIso, 1) << " dBFS (diff "
                  << juce::String (dbHiIso - dbLoIso, 2) << " dB -- the ~24dB the parameter itself should move it)\n";

        // The AUTHORITATIVE wiring signal is the isolated test: it removes every downstream
        // module that could legitimately mask a gain change, so it answers "does inGain
        // reach the output at all". The rich-state diff only tells us whether it SURVIVES
        // the default mix -- that's a second, separate fact, not the wiring verdict.
        const bool wiredRaw = dIso > kWiredThresholdDb;
        const bool movesInRichState = std::abs (dbHi - dbLo) > 3.0;
        if (wiredRaw && movesInRichState)
            verdict["source.intrim"] = { Verdict::Wired, "targeted: -30dBFS in, +-12dB trim" };
        else if (wiredRaw && ! movesInRichState)
            verdict["source.intrim"] = { Verdict::Wired,
                "wired (Engine.h:116/137 apply inGain before Voice::render -- isolated diff "
                + juce::String (dIso, 1) + " dB, ~" + juce::String (dbHiIso - dbLoIso, 0)
                + " dB RMS swing), but that swing is almost entirely cancelled by flat.on's leveler "
                "in the rich base state (rich-state diff only " + juce::String (d, 1) + " dB) -- "
                "Flatten.h processSample() divides by a tracked envelope toward a fixed target "
                "level (Flatten.h ~34-56), applied AFTER source.intrim's gain (Voice.h, right after "
                "src.processSample()) and BEFORE the modulator, so any upstream gain change is "
                "mostly re-leveled away by design, not lost to a defect." };
        else
            verdict["source.intrim"] = { Verdict::Defect,
                "diff " + juce::String (dIso, 1) + " dB even isolated (every other module off) -- "
                "trace inGain (Engine.h:116) through Engine::process to the output: possible defect." };
    }

    // ---- flat.on / flat.response: Sample, Osc, Input/FX ---------------------------------
    {
        struct FlatCtx { const char* label; CtxDef ctx; const juce::AudioBuffer<float>* input; juce::File sample; };
        auto tremolo = std::make_shared<juce::AudioBuffer<float>> ();
        makeTremoloTone (*tremolo, (int) std::llround (kSr * kTotalSeconds));

#if BROKEN_FX
        std::vector<FlatCtx> flatCtxs = {
            { "FX (tremolo tone)", { "FX", SourceEngine::Input, true, false, false }, tremolo.get(), sampleA }
        };
#else
        std::vector<FlatCtx> flatCtxs = {
            { "Sample (wobble sample)", { "Sample", SourceEngine::Sample, false, true, false }, nullptr, sampleA },
            { "Osc (constant osc tone)", { "Osc", SourceEngine::Osc, false, true, false }, nullptr, sampleA },
            { "Input (tremolo tone)",    { "Input", SourceEngine::Input, true, true, false }, tremolo.get(), sampleA },
        };
#endif
        double bestOnDiff = -300.0, bestRespDiff = -300.0;
        juce::String bestOnCtx, bestRespCtx;
        for (auto& fc : flatCtxs)
        {
            auto full = fc.ctx; // measure the WHOLE render (attack included), 1.2s, no release needed
            auto onOff = [&] (float boolVal)
            {
                return renderCaseFull (full, fc.sample, fc.input, 1.2, false, nullptr,
                    [boolVal] (BrokenProcessor& proc) { setReal (rangedOf (proc, "flat.on"), boolVal); });
            };
            auto respLoHi = [&] (float norm)
            {
                return renderCaseFull (full, fc.sample, fc.input, 1.2, false, nullptr,
                    [norm] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "flat.response"), norm); });
            };
            const double dOn = diffDbBetween (onOff (0.0f), onOff (1.0f));
            const double dResp = std::max (diffDbBetween (respLoHi (0.1f), respLoHi (0.5f)),
                                           diffDbBetween (respLoHi (0.5f), respLoHi (0.9f)));
            std::cout << "  flat.on/[" << fc.label << "]: on-vs-off diff " << juce::String (dOn, 2)
                      << " dB; flat.response spread " << juce::String (dResp, 2) << " dB\n";
            if (dOn > bestOnDiff)     { bestOnDiff = dOn;     bestOnCtx = fc.label; }
            if (dResp > bestRespDiff) { bestRespDiff = dResp; bestRespCtx = fc.label; }
        }
        auto verdictFor = [&] (double d, const juce::String& bestCtx, const char* what)
        {
            if (d > kWiredThresholdDb)
                return ParamVerdict { Verdict::Wired, juce::String ("targeted: best in ") + bestCtx };
            return ParamVerdict { Verdict::Defect,
                juce::String (what) + " never crosses threshold in any tried context (best " + juce::String (d, 1)
                    + " dB in " + bestCtx + ") -- Flatten.h's leveler should be audible on a signal with this much "
                    "amplitude variation; needs code review." };
        };
        verdict["flat.on"] = verdictFor (bestOnDiff, bestOnCtx, "flat.on");
        verdict["flat.response"] = verdictFor (bestRespDiff, bestRespCtx, "flat.response");
    }

    // ---- stretch group: 1s sample, 1.5s note, predelay=0 (instrument); FX confirmed dead
#if !BROKEN_FX
    {
        auto oneSec = makeOneSecondSample();
        auto setup = [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "stretch.predelay"), 0.0f); };
        const CtxDef ctx { "Sample", SourceEngine::Sample, false, true, false };
        auto reference = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup, nullptr);
        const char* cond = "1s sample, 1.5s held note, predelay=0";

        auto onOff = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup,
            [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "stretch.on"), 0.0f); });
        const double dOn = diffDbBetween (reference, onOff);

        auto amtLo = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "stretch.amount"), 0.1f); });
        auto amtHi = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "stretch.amount"), 0.9f); });
        const double dAmt = std::max (diffDbBetween (reference, amtLo), diffDbBetween (reference, amtHi));

        auto freqLo = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "stretch.freq"), 0.1f); });
        auto freqHi = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "stretch.freq"), 0.9f); });
        const double dFreq = std::max (diffDbBetween (reference, freqLo), diffDbBetween (reference, freqHi));

        auto preLo = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "stretch.predelay"), 0.1f); });
        auto preHi = renderCaseFull (ctx, oneSec, nullptr, 1.5, false, setup,
            [] (BrokenProcessor& proc) { setNorm (rangedOf (proc, "stretch.predelay"), 0.9f); });
        const double dPre = std::max (diffDbBetween (reference, preLo), diffDbBetween (reference, preHi));

        std::cout << "  stretch.on: diff " << juce::String (dOn, 2) << " dB [" << cond << "]\n";
        std::cout << "  stretch.amount: diff " << juce::String (dAmt, 2) << " dB [" << cond << "]\n";
        std::cout << "  stretch.freq: diff " << juce::String (dFreq, 2) << " dB [" << cond << "]\n";
        std::cout << "  stretch.predelay: diff " << juce::String (dPre, 2) << " dB [" << cond << "]\n";
        std::cout << "  (stretch.on toggles stretchActive's boolean gate; stretch.amount/freq change what "
                     "the ALREADY-ACTIVE segment-repeat does every pass -- if the on/off switch and the "
                     "amount knob disagree on wired-ness, that split is reported verbatim below, not "
                     "papered over.)\n";

        verdict["stretch.on"]       = { dOn > kWiredThresholdDb ? Verdict::Wired : Verdict::Defect,
            dOn > kWiredThresholdDb ? juce::String ("targeted: ") + cond
                                    : juce::String ("diff ") + juce::String (dOn, 1) + " dB even isolated to Sample mode, 1s sample, predelay 0 -- see stretch.amount for comparison." };
        verdict["stretch.amount"]   = { dAmt > kWiredThresholdDb ? Verdict::Wired : Verdict::Defect, juce::String ("targeted: ") + cond };
        verdict["stretch.freq"]     = { dFreq > kWiredThresholdDb ? Verdict::Wired : Verdict::Defect, juce::String ("targeted: ") + cond };
        verdict["stretch.predelay"] = { dPre > kWiredThresholdDb ? Verdict::Wired : Verdict::Defect, juce::String ("targeted: ") + cond };
    }
#else
    {
        // Confirm analytically AND empirically: source is forced Input, and SourceEngine's
        // Input case never calls advanceHead()/playRegioned() (where stretch lives) at all.
        auto setup = [] (BrokenProcessor& proc) { setReal (rangedOf (proc, "stretch.predelay"), 0.0f); };
        const CtxDef ctx { "FX", SourceEngine::Input, true, false, false };
        auto reference = renderCase (ctx, sampleA, &fxInputBuf, setup, nullptr);
        auto moved = renderCase (ctx, sampleA, &fxInputBuf, setup, [] (BrokenProcessor& proc)
        {
            setReal (rangedOf (proc, "stretch.on"), 1.0f);
            setReal (rangedOf (proc, "stretch.amount"), 90.0f);
            setReal (rangedOf (proc, "stretch.freq"), 500.0f);
        });
        const double d = diffDbBetween (reference, moved);
        const juce::String reason = "confirmed: source is forced to Input (PluginProcessor.cpp:136); "
            "SourceEngine::processSample's Input case (SourceEngine.h:196) returns the raw input directly "
            "and never calls advanceHead() (SourceEngine.h), which is the only place stretch.on/amount/"
            "freq/predelay are read. diff with everything cranked at once: " + juce::String (d, 1) + " dB.";
        for (const char* id : { "stretch.on", "stretch.amount", "stretch.freq", "stretch.predelay" })
            verdict[id] = { Verdict::DeadByDesign, reason };
        std::cout << "  stretch group (all 4, cranked together): diff " << juce::String (d, 2)
                  << " dB [FX, source forced Input] -> DEAD-BY-DESIGN (confirmed)\n";
    }
#endif

    // ---- noise.amp/noise.phase: instrument Noise mode wired (already proven by the
    // general sweep); FX dead-by-design (source-side only, forced Input) -- annotate
    // directly, no new render needed since the general sweep already covers both cases
    // correctly with a properly-fed context.
#if BROKEN_FX
    for (const char* id : { "noise.amp", "noise.phase" })
        verdict[id] = { Verdict::DeadByDesign, curatedReason (id, true) };
#endif

    // ---- item 2 (Table mod source), FX only: osc.mode and source.oscwave (OSC WAVE) now
    // have a real path via Engine::rebuildModTableIfNeeded, independent of source.mode --
    // needs mod.source=Table to move the needle, which the general sweep's Osc-default
    // mod.source never reaches. osc.h*/osc.d* are covered by the named drawn-oscillator
    // test instead (see curatedReason); these two scalar ids get their own targeted test.
#if BROKEN_FX
    {
        auto renderTable = [&] (const std::function<void (BrokenProcessor&)>& perturb)
        {
            CtxDef ctx { "FX", SourceEngine::Input, true, false, false };
            return renderCase (ctx, sampleA, &fxInputBuf,
                [] (BrokenProcessor& proc)
                {
                    setChoiceReal (proc, "mod.source", 4); // Table
                    setReal (rangedOf (proc, "mod.on"), 1.0f);
                    setChoiceReal (proc, "mod.mode", params::modModes.indexOf ("AM"));
                    setReal (rangedOf (proc, "mod.amount"), 0.8f);
                },
                perturb);
        };
        // osc.mode: Wave (Params.h default source.oscwave=Saw) vs Harmonic (Params.h
        // default 1/k recipe) -- two very differently-shaped tables.
        auto wave = renderTable ([] (BrokenProcessor& proc) { setChoiceReal (proc, "osc.mode", 0); });
        auto harm = renderTable ([] (BrokenProcessor& proc) { setChoiceReal (proc, "osc.mode", 1); });
        record ("osc.mode", diffDbBetween (wave, harm), "mod.source=Table: Wave vs Harmonic");

        // source.oscwave: two very different analytic waves under osc.mode=Wave.
        auto sine = renderTable ([] (BrokenProcessor& proc)
        {
            setChoiceReal (proc, "osc.mode", 0);
            setChoiceReal (proc, "source.oscwave", params::oscWaves.indexOf ("Sine"));
        });
        auto square = renderTable ([] (BrokenProcessor& proc)
        {
            setChoiceReal (proc, "osc.mode", 0);
            setChoiceReal (proc, "source.oscwave", params::oscWaves.indexOf ("Square"));
        });
        record ("source.oscwave", diffDbBetween (sine, square), "mod.source=Table, osc.mode=Wave: Sine vs Square");
    }

    // ---- sample.regstart/regend: needs mod.source=Sample (tickModSource's regionBounds()
    // call is the only reader of these in the FX build; the "sample as a shape (a)" named
    // check exercises this same mechanism with two different SAMPLES rather than two
    // different REGIONS of the same sample, so it does not feed this verdict table).
    {
        auto renderRegion = [&] (float rs, float re)
        {
            CtxDef ctx { "FX", SourceEngine::Input, true, false, false };
            return renderCase (ctx, sampleA, &fxInputBuf,
                [] (BrokenProcessor& proc)
                {
                    setChoiceReal (proc, "mod.source", 2); // Sample
                    setReal (rangedOf (proc, "mod.on"), 1.0f);
                    setChoiceReal (proc, "mod.mode", params::modModes.indexOf ("AM"));
                    setReal (rangedOf (proc, "mod.amount"), 0.8f);
                },
                [rs, re] (BrokenProcessor& proc)
                {
                    setReal (rangedOf (proc, "sample.regstart"), rs);
                    setReal (rangedOf (proc, "sample.regend"), re);
                });
        };
        auto firstHalf = renderRegion (0.0f, 0.5f);
        auto secondHalf = renderRegion (0.5f, 1.0f);
        const double d = diffDbBetween (firstHalf, secondHalf);
        record ("sample.regstart", d, "mod.source=Sample: first half of the file vs second half");
        record ("sample.regend", d, "mod.source=Sample: first half of the file vs second half");
    }
#endif

    std::cout << "=== end targeted-context pass ===\n\n";
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String csvPath;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a { juce::CharPointer_UTF8 (argv[i]) };
        if (a == "--csv" && i + 1 < argc) csvPath = juce::String (juce::CharPointer_UTF8 (argv[++i]));
    }
    if (csvPath.isEmpty())
    {
        std::cerr << "usage: " << argv[0] << " --csv <path.csv>\n";
        return 2;
    }

    std::cout << "=== BASE STATE ===\n"
                 "  every *.on switch (mod/ws/flt/res/inv/dly/stretch/flat) = ON\n"
                 "  sample.loopon = ON\n"
                 "  ws.curve = Custom (128 curve points at their Params.h diagonal defaults)\n"
                 "  every other plain float parameter (incl. the 64 harmonic and 128 draw\n"
                 "    bank points) = 40% of its normalised range\n"
                 "  chain.mix = 1, bypass = 0, out.level = 0 dB, ws.randseed = Params.h default (fixed)\n"
                 "  every other bool/choice = Params.h default; source.mode/osc.mode set per context/test\n"
                 "  note: C3 (60), velocity 100, held for the whole render except where noted\n"
                 "  render: 1.5 s @ 48 kHz, block 512; measured window = last 0.75 s\n"
                 "  wired threshold: diff > " << kWiredThresholdDb << " dB relative to reference RMS\n\n";

    auto sampleA = makePrimarySample();
    auto sampleB = makeAltSample();

    juce::AudioBuffer<float> fxInputBuf;
    makeStereoToneNoise (fxInputBuf, (int) std::llround (kSr * kTotalSeconds), 0.0, juce::MathConstants<double>::pi / 3.0);

#if BROKEN_FX
    std::vector<CtxDef> contexts = {
        { "FX", SourceEngine::Input, true, false, false }
    };
#else
    std::vector<CtxDef> contexts = {
        { "Sample", SourceEngine::Sample, false, true, false },
        { "Cycle",  SourceEngine::Cycle,  false, true, false },
        { "Osc",    SourceEngine::Osc,    false, true, false },
        { "Noise",  SourceEngine::Noise,  false, true, false },
        { "Input",  SourceEngine::Input,  true,  true, false },
        { "Tape",   SourceEngine::Tape,   false, true, true  },
    };
#endif

    std::vector<ParamInfo> paramList;
    {
        BrokenProcessor tmp;
        for (auto* p : tmp.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr) continue;
            paramList.push_back ({ rp->paramID, rp->getName (100), classify (p) });
        }
    }
    std::cout << "enumerated " << paramList.size() << " parameters from getParameters()\n\n";

    std::ofstream csv (csvPath.toStdString());
    csv << "param_id,name,context,wired,diff_db\n";

    std::map<juce::String, std::vector<juce::String>> wiredIn;

    for (auto& ctx : contexts)
    {
        std::cout << "--- context " << ctx.name << (ctx.isTape ? " (tape take primed each render)" : "") << " ---\n";
        const juce::AudioBuffer<float>* inputPtr = ctx.feedInput ? &fxInputBuf : nullptr;

        auto reference = renderCase (ctx, sampleA, inputPtr, nullptr, nullptr);

        for (auto& pi : paramList)
        {
            std::vector<float> candidates;
            {
                BrokenProcessor tmp;
                candidates = candidatesFor (rangedOf (tmp, pi.id), pi.kind);
            }

            double best = -300.0;
            for (float cand : candidates)
            {
                auto moved = renderCase (ctx, sampleA, inputPtr, nullptr,
                    [&] (BrokenProcessor& proc) { setNorm (rangedOf (proc, pi.id), cand); });
                const double d = diffDbBetween (reference, moved);
                if (d > best) best = d;
            }

            const bool wired = best > kWiredThresholdDb;
            csv << pi.id << ",\"" << pi.name << "\"," << ctx.name << ","
                << (wired ? 1 : 0) << "," << juce::String (best, 2) << "\n";
            if (wired) wiredIn[pi.id].push_back (ctx.name);
        }
    }
    csv.close();
    std::cout << "\nwrote " << csvPath << "\n\n";

    // ---- summary ----------------------------------------------------------------
    std::vector<juce::String> deadEverywhere;
    int wiredCount = 0;
    for (auto& pi : paramList)
    {
        auto it = wiredIn.find (pi.id);
        std::cout << "  " << pi.id;
        if (it != wiredIn.end() && ! it->second.empty())
        {
            ++wiredCount;
            std::cout << " wired in:";
            for (auto& c : it->second) std::cout << " " << c;
        }
        else
        {
            deadEverywhere.push_back (pi.id);
            std::cout << " DEAD in every context";
        }
        std::cout << "\n";
    }
    std::cout << "\ncontrol_audit: " << paramList.size() << " parameters, " << wiredCount
              << " wired in at least one context, " << deadEverywhere.size() << " dead in every context\n";
    std::cout << "dead-in-every-context (" << deadEverywhere.size() << "):\n";
    for (auto& id : deadEverywhere) std::cout << "  " << id << "\n";
    std::cout << "\n";

    // ---- the three named checks ---------------------------------------------------
    int failures = 0;
    auto report = [&] (const char* label, const TestOutcome& r)
    {
        std::cout << (r.pass ? "PASS " : "FAIL ") << label << " -- " << r.message << "\n";
        if (! r.pass) ++failures;
    };

    // A context that actually carries a signal through the waveshaper in BOTH builds:
    // the instrument's Sample context (source IS the loaded sample), or the FX context
    // (source is forced to Input, so it needs the fed tone -- feedInput/holdNote must
    // match how the FX build actually runs: no MIDI, fed input).
    CtxDef curveCtx = contexts.front();
#if !BROKEN_FX
    curveCtx = { "Sample", SourceEngine::Sample, false, true, false };
#endif
    const juce::AudioBuffer<float>* curveInputPtr = curveCtx.feedInput ? &fxInputBuf : nullptr;

    report ("custom waveshaper curve (straight line vs fold)",
            testCustomWaveshaperCurve (sampleA, curveCtx, curveInputPtr));

#if !BROKEN_FX
    report ("drawn oscillator wavetable (draw bank + harmonic bank, Osc context)",
            testDrawnOscillator (sampleA));
#else
    report ("drawn oscillator wavetable (FX build: mod.source=Table)",
            testDrawnOscillator (sampleA, fxInputBuf));
#endif

    {
        CtxDef modCtx = contexts.front();
#if !BROKEN_FX
        modCtx = { "Osc", SourceEngine::Osc, false, true, false }; // isolates mod.source from the main signal
#endif
        const juce::AudioBuffer<float>* inputPtr = modCtx.feedInput ? &fxInputBuf : nullptr;
        report ("sample as a shape (a): mod.source=Sample, two different samples",
                testModSourceSample (sampleA, sampleB, modCtx, inputPtr));
    }
    report ("sample as a shape (b): FROM SAMPLE curve vs default Custom diagonal",
            testFromSampleCurve (sampleA, curveCtx, curveInputPtr));

    std::cout << "\ncontrol_audit: " << failures << " named-check failures"
                 " (dead-parameter counts above are audit findings, not test failures)\n";

    // ==================================================================================
    // Phase 2: targeted-context pass + final verdict table.
    // Seed from the general sweep (wired -> Wired; dead -> DeadByDesign/curatedReason),
    // then let runTargetedPass override entries it has a specific, better-conditioned
    // answer for. Anything still dead after its OWN best-case condition is a DEFECT.
    // ==================================================================================
    std::map<juce::String, ParamVerdict> verdict;
    for (auto& pi : paramList)
    {
        auto it = wiredIn.find (pi.id);
        if (it != wiredIn.end() && ! it->second.empty())
            verdict[pi.id] = { Verdict::Wired, "general sweep" };
        else
#if BROKEN_FX
            verdict[pi.id] = { Verdict::DeadByDesign, curatedReason (pi.id, true) };
#else
            verdict[pi.id] = { Verdict::DeadByDesign, curatedReason (pi.id, false) };
#endif
    }

    runTargetedPass (verdict, sampleA, fxInputBuf);

    // ---- print the final table -------------------------------------------------------
    std::cout << "\n=== FINAL VERDICT TABLE ("
#if BROKEN_FX
              << "Broken FX"
#else
              << "Broken (instrument)"
#endif
              << ") ===\n";

    auto moduleOf = [] (const juce::String& id)
    {
        const int dot = id.indexOfChar ('.');
        return dot > 0 ? id.substring (0, dot) : id;
    };

    std::map<juce::String, int> wiredByModule;
    std::vector<juce::String> deadByDesignIds, defectIds;
    for (auto& pi : paramList)
    {
        const auto& v = verdict.at (pi.id);
        if (v.v == Verdict::Wired) ++wiredByModule[moduleOf (pi.id)];
        else if (v.v == Verdict::DeadByDesign) deadByDesignIds.push_back (pi.id);
        else defectIds.push_back (pi.id);
    }

    int totalWired = 0;
    std::cout << "WIRED, by module:\n";
    for (auto& kv : wiredByModule)
    {
        std::cout << "  " << kv.first << ": " << kv.second << "\n";
        totalWired += kv.second;
    }
    std::cout << "  TOTAL WIRED: " << totalWired << " / " << paramList.size() << "\n\n";

    // Collapse the two big banks into one summary line each; list everything else
    // individually with its reason.
    int bankHarmCount = 0, bankDrawCount = 0;
    juce::String bankHarmReason, bankDrawReason;
    std::cout << "DEAD-BY-DESIGN (" << deadByDesignIds.size() << "):\n";
    for (auto& id : deadByDesignIds)
    {
        if (id.startsWith ("osc.h")) { ++bankHarmCount; bankHarmReason = verdict.at (id).reason; continue; }
        if (id.startsWith ("osc.d")) { ++bankDrawCount; bankDrawReason = verdict.at (id).reason; continue; }
        std::cout << "  " << id << " -- " << verdict.at (id).reason << "\n";
    }
    if (bankHarmCount > 0) std::cout << "  osc.h01..osc.h" << bankHarmCount
                                     << " (" << bankHarmCount << " params) -- " << bankHarmReason << "\n";
    if (bankDrawCount > 0) std::cout << "  osc.d001..osc.d" << bankDrawCount
                                     << " (" << bankDrawCount << " params) -- " << bankDrawReason << "\n";

    std::cout << "\nDEFECT (" << defectIds.size() << "):\n";
    if (defectIds.empty()) std::cout << "  (none)\n";
    for (auto& id : defectIds)
        std::cout << "  " << id << " -- " << verdict.at (id).reason << "\n";

#if BROKEN_FX
    // ---- FX-visible-yet-dead cross-reference -------------------------------------------
    // Parameter IDs bound to a control that is actually PRESENT in the FX editor
    // (MangleView.h / EditView.h under BROKEN_FX), cross-referenced against the verdict
    // table above. These are the controls a Broken FX user can see and turn and hear
    // nothing from.
    // Updated for items 2/4/5: OSCILLATOR (osc.mode/source.oscwave, item 2), the sample
    // slot (source.winpos/winlen/ext, item 4) and TAPE (tape.rec/tape.flip, item 5) are
    // newly visible in the FX editor; noise.amp/noise.phase and the STRETCH group are now
    // hidden there (items 5) so they are removed -- this list must track MangleView.h/
    // EditView.h's actual `#if BROKEN_FX` control set, not the pre-item-2/4/5 one.
    static const char* const fxVisibleIds[] = {
        "source.pitch", "source.finecents", "source.intrim", "source.winpos", "source.winlen",
        "source.ext", "osc.mode", "source.oscwave", "sample.regstart", "sample.regend",
        "ws.on", "mod.on", "flt.on", "res.on", "inv.on", "dly.on",
        "ws.drive", "ws.curve", "ws.morph", "mod.amount", "mod.freq", "mod.mode", "mod.wave",
        "mod.source", "flt.cutoff", "flt.poles", "res.freq", "res.fb", "inv.mix",
        "dly.time", "dly.mix", "dly.inv", "chain.mix", "col.mode", "col.rate", "out.level", "bypass",
        "ws.trim", "ws.randseed", "mod.fmindex", "dly.fine", "dly.fb", "res.damp", "inv.type",
        "flat.on", "flat.response", "tape.rec", "tape.flip",
    };
    std::cout << "\n=== FX-EDITOR-VISIBLE CONTROLS THAT ARE DEAD IN THE FX BUILD ===\n"
                 "(from reading MangleView.h/EditView.h under BROKEN_FX -- excludes the "
                 "128 ws.c### curve points, which the CurveEditor writes directly and which "
                 "ARE live whenever ws.curve=Custom; the 64 osc.h### / 128 osc.d### bank "
                 "points, also visible in FX's OSCILLATOR block since item 2, are checked "
                 "separately just below since they collapse to one line each)\n";
    int fxVisibleDeadCount = 0;
    for (auto id : fxVisibleIds)
    {
        auto it = verdict.find (id);
        if (it != verdict.end() && it->second.v != Verdict::Wired)
        {
            ++fxVisibleDeadCount;
            std::cout << "  " << id << " [" << (it->second.v == Verdict::Defect ? "DEFECT" : "DEAD-BY-DESIGN")
                      << "] -- " << it->second.reason << "\n";
        }
    }
    // osc.h*/osc.d* banks: visible in FX's OSCILLATOR block (item 2) but, like the
    // instrument, DeadByDesign in the verdict TABLE (the general sweep never sets
    // mod.source=Table) with an explicit pointer to the named test that DOES prove them
    // live -- so they are explained, not unexplained, entries.
    int fxHarmDead = 0, fxDrawDead = 0;
    for (int k = 1; k <= params::harmonicCount; ++k)
        if (auto it = verdict.find (params::harmonicId (k)); it != verdict.end() && it->second.v != Verdict::Wired)
            ++fxHarmDead;
    for (int k = 1; k <= params::drawPointCount; ++k)
        if (auto it = verdict.find (params::drawPointId (k)); it != verdict.end() && it->second.v != Verdict::Wired)
            ++fxDrawDead;
    if (fxHarmDead > 0)
    {
        std::cout << "  osc.h01..osc.h" << fxHarmDead << " (" << fxHarmDead << " params) -- "
                  << curatedReason ("osc.h01", true) << "\n";
        fxVisibleDeadCount += fxHarmDead;
    }
    if (fxDrawDead > 0)
    {
        std::cout << "  osc.d001..osc.d" << fxDrawDead << " (" << fxDrawDead << " params) -- "
                  << curatedReason ("osc.d001", true) << "\n";
        fxVisibleDeadCount += fxDrawDead;
    }
    std::cout << fxVisibleDeadCount << " of "
              << ((sizeof (fxVisibleIds) / sizeof (fxVisibleIds[0])) + params::harmonicCount + params::drawPointCount)
              << " FX-editor-visible controls are dead in the FX build (every one explained above).\n";
#endif

    int defectCount = (int) defectIds.size();
    return (failures == 0 && defectCount == 0) ? 0 : 1;
}
