#pragma once
// Single source of truth for every parameter: IDs, ranges, defaults.
// IDs are stable forever — snapshots (JSON) and host automation both key on them.
// Ranges/defaults trace to docs/PANEL.md and docs/DSP-NOTES.md; change those docs first.

#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SourceEngine.h"
#include "dsp/Waveshaper.h"

namespace broken::params
{
using APVTS = juce::AudioProcessorValueTreeState;

// --- indexed parameter banks -------------------------------------------------------
// These ids used to be formatted with a literal "ws.c%02d"-style string in every file
// that touched them. Renumbering a bank (v0.20: ws.c01..c16 -> ws.c001..c128) then
// silently orphaned callers the compiler could not catch: getParameter() returns nullptr,
// a null guard swallows it, and the control just stops working. It shipped that way.
//
// So: ONE place builds these ids and ONE place states the counts. Every caller goes
// through here. `id-banks resolve to real parameters` in the test suite proves no caller
// can drift again.
inline constexpr int harmonicCount   = 64;
inline constexpr int drawPointCount  = dsp::SourceEngine::drawPointCount;
inline constexpr int curvePointCount = dsp::Waveshaper::customPointCount;

// the DSP owns the real array sizes; a mismatch here would write past them
static_assert (drawPointCount  == dsp::SourceEngine::drawPointCount,  "draw bank size drift");
static_assert (curvePointCount == dsp::Waveshaper::customPointCount,  "curve bank size drift");

inline juce::String harmonicId  (int i) { return juce::String::formatted ("osc.h%02d", i); }
inline juce::String drawPointId (int i) { return juce::String::formatted ("osc.d%03d", i); }
inline juce::String curvePointId (int i) { return juce::String::formatted ("ws.c%03d", i); }

// --- choice-parameter orderings (index order is part of the persistent contract) ---
inline const juce::StringArray sourceModes  { "Sample", "Cycle", "Osc", "Noise", "Input", "Tape" };
inline const juce::StringArray loopStyles   { "Loop", "PingPong" };
inline const juce::StringArray oscWaves     { "Sine", "Tri", "Saw", "Square", "Bell", "Odd" };
inline const juce::StringArray oscModes     { "Wave", "Harmonic", "Draw" };
inline const juce::StringArray modModes     { "AM", "RM", "FM", "PM" };
inline const juce::StringArray modWaves     { "Sine", "Bell", "Odd" };
inline const juce::StringArray modSources   { "Osc", "Self", "Sample", "Tape" };
inline const juce::StringArray wsCurves     { "Linear", "HardClip", "SoftSat", "Fold",
                                              "Asym", "Stair", "Sine", "InvertS",
                                              "Random", "Custom" };
inline const juce::StringArray auxDests     { "FilterCut", "ResFreq", "InvMix", "ModAmt", "Pitch" };
inline const juce::StringArray voiceModes   { "Mono", "Poly" };
inline const juce::StringArray invTypes     { "A", "B" };
inline const juce::StringArray xfadeShapes  { "Linear", "EqPower" };
inline const juce::StringArray colourModes  { "Off", "12bit", "8bit" };

inline APVTS::ParameterLayout createLayout()
{
    using P  = juce::AudioParameterFloat;
    using Pc = juce::AudioParameterChoice;
    using Pb = juce::AudioParameterBool;
    using Rng = juce::NormalisableRange<float>;

    auto logRange = [] (float lo, float hi) {
        Rng r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi)); // log-feel: centre at the geometric mean
        return r;
    };

    // v0.34: hosts show real values in automation lanes ("2400 Hz", "-12.0 dB").
    // Display only — ids, ranges, defaults and skews are the frozen contract. No
    // scaling (a scaled display would break host type-in parsing), so 0..1 blend
    // params stay plain floats and are not routed through this helper.
    auto Pf = [] (const juce::String& id, const juce::String& name, Rng rng, float def,
                  const juce::String& unit, int decimals)
    {
        return std::make_unique<P> (id, name, rng, def,
            juce::AudioParameterFloatAttributes()
                .withLabel (unit)
                .withStringFromValueFunction ([decimals] (float v, int)
                {
                    const float eps = 0.5f * std::pow (10.0f, (float) -decimals);
                    if (std::abs (v) < eps) v = 0.0f; // no "-0.0" in automation lanes
                    return juce::String (v, decimals);
                }));
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Source
    // FX build default = Input (index 4): the source is always the live input there
    // (BrokenProcessor::gatherParams forces it regardless of this value too, but the
    // parameter's own default should agree — a generic host editor reads this).
    layout.add (std::make_unique<Pc> ("source.mode",  "Source",       sourceModes,
#if BROKEN_FX
        4
#else
        0
#endif
        ));
    layout.add (Pf ("source.pitch", "Pitch",        Rng (-48.f, 48.f, 0.01f), 0.f, "st", 1));
    layout.add (std::make_unique<Pb> ("source.ext",   "Pitch Ext",    false)); // UI gate: +-24 st unless EXT
    layout.add (Pf ("source.finecents", "Fine",     Rng (-50.f, 50.f, 1.f), 0.f, "c", 0)); // era: +-1/2 st, 1-cent steps
    layout.add (std::make_unique<P>  ("source.pitchmix",  "Pitch Mix", Rng (0.f, 1.f), 1.f)); // 1 = fully pitched; ~0.5 = era detune/chorus
    layout.add (std::make_unique<P>  ("source.winpos","Cycle Pos",    Rng (0.f, 1.f), 0.1f));
    layout.add (Pf ("source.winlen","Cycle Length", logRange (32.f, 4096.f), 512.f, "smp", 0));
    layout.add (Pf ("source.xfade", "Cycle Xfade",  Rng (0.f, 16.f, 1.f), 8.f, "smp", 0));
    layout.add (std::make_unique<Pc> ("source.oscwave","Osc Wave",    oscWaves, 2));
    layout.add (std::make_unique<Pc> ("osc.mode",      "Osc Mode",    oscModes, 0));
    for (int k = 1; k <= harmonicCount; ++k)
    {
        // Harmonic Mode (manual: 64 partials as % amplitude); defaults = saw recipe 1/k
        layout.add (std::make_unique<P> (harmonicId (k),
                                         juce::String::formatted ("Harm %02d", k),
                                         Rng (0.f, 100.f, 1.f), 100.0f / (float) k));
    }
    for (int k = 1; k <= drawPointCount; ++k)
    {
        // DRAW mode (docs/DSP-NOTES.md §1.3b): one point per cycle slice.
        // Default = a sine, so switching to Draw gives a shape to deform, not a blank page.
        layout.add (std::make_unique<P> (drawPointId (k),
                                         juce::String::formatted ("Draw %03d", k),
                                         Rng (-1.f, 1.f, 0.001f),
                                         (float) std::sin (6.283185307179586 * (k - 1) / (double) drawPointCount)));
    }
    layout.add (Pf ("source.intrim","In Trim",      Rng (-12.f, 12.f, 0.1f), 0.f, "dB", 1));

    // non-destructive sample region + play modes (docs/DSP-NOTES.md §1.1a)
    layout.add (std::make_unique<P>  ("sample.regstart", "Region Start", Rng (0.f, 1.f), 0.f));
    layout.add (std::make_unique<P>  ("sample.regend",   "Region End",   Rng (0.f, 1.f), 1.f));
    layout.add (std::make_unique<Pb> ("sample.loopon",   "Loop On",      false));
    layout.add (std::make_unique<Pc> ("sample.loopstyle","Loop Style",   loopStyles, 0));
    layout.add (std::make_unique<Pb> ("sample.rev",      "Reverse",      false));
    Rng xfadeRange (0.f, 250.f);
    xfadeRange.setSkewForCentre (25.f); // true 0 stays reachable (0 = raw era click)
    layout.add (Pf ("sample.xfade",    "Loop Xfade",   xfadeRange, 10.f, "ms", 1));
    layout.add (std::make_unique<Pc> ("sample.xfadeshape", "Xfade Shape", xfadeShapes, 0));

    // Stretcher / Time Compressor (docs/DSP-NOTES.md §16)
    layout.add (std::make_unique<Pb> ("stretch.on",       "Stretch On",   false));
    layout.add (Pf ("stretch.freq",     "Stretch Freq", logRange (20.f, 2000.f), 130.81f, "Hz", 1)); // C3 = source root (DSP-NOTES §0)
    layout.add (Pf ("stretch.amount",   "Stretch Amt",  Rng (-100.f, 100.f, 1.f), 0.f, "%", 0));
    layout.add (Pf ("stretch.predelay", "Predelay",     Rng (0.f, 1000.f, 1.f), 0.f, "ms", 0));

    // Envelope Removal, realtime (docs/DSP-NOTES.md §17)
    layout.add (std::make_unique<Pb> ("flat.on",       "Flatten On",  false));
    layout.add (Pf ("flat.response", "Flat Resp",   logRange (1.f, 500.f), 50.f, "Hz", 1));

    // Modulator
    layout.add (std::make_unique<Pb> ("mod.on",     "Mod On",      true));
    layout.add (std::make_unique<P>  ("mod.amount", "Mod Amount",  Rng (0.f, 1.f), 0.f));
    layout.add (Pf ("mod.freq",   "Mod Freq",    logRange (0.1f, 2000.f), 65.41f, "Hz", 1)); // C2 (DSP-NOTES §0)
    layout.add (std::make_unique<Pc> ("mod.mode",   "Mod Mode",    modModes, 1));
    layout.add (std::make_unique<Pc> ("mod.wave",   "Mod Wave",    modWaves, 1));
    layout.add (std::make_unique<Pc> ("mod.source", "Mod Source",  modSources, 0));
    layout.add (std::make_unique<P>  ("mod.fmindex","FM Index",    Rng (0.f, 8.f, 0.01f), 2.f));

    // Waveshaper
    layout.add (std::make_unique<Pb> ("ws.on",    "Shaper On",   true));
    layout.add (std::make_unique<Pc> ("ws.curve", "Curve",       wsCurves, 2));
    // FX default 0 dB (was 12 dB, the instrument's value): 12 dB of drive into SoftSat
    // is the known cause of a fresh instance being too loud as a plug-in effect (see
    // CHANGELOG); the instrument keeps 12 dB unchanged.
    layout.add (Pf ("ws.drive", "Drive",       Rng (0.f, 40.f, 0.1f),
#if BROKEN_FX
        0.f,
#else
        12.f,
#endif
        "dB", 1));
    layout.add (std::make_unique<P>  ("ws.morph", "Morph",       Rng (0.f, 1.f), 1.f));
    // FX default -3.5 dB (was 0 dB): the default SoftSat curve (ws.curve index 2,
    // 1.5u - 0.5u^3) has slope 1.5 at u=0, i.e. +3.52 dB of small-signal gain even at
    // ws.drive 0 dB (measured: a -14 dBFS default render came out +3.34 dB hot with
    // trim at 0 -- see CHANGELOG). -3.5 dB of trim cancels that gain so the curve's
    // character survives at an honest default level; the instrument keeps 0 dB.
    layout.add (Pf ("ws.trim",  "Trim",        Rng (-24.f, 24.f, 0.1f),
#if BROKEN_FX
        -3.5f,
#else
        0.f,
#endif
        "dB", 1));
    layout.add (std::make_unique<P>  ("ws.randseed", "Curve Seed", Rng (1.f, 9999.f, 1.f), 1.f));
    for (int i = 1; i <= curvePointCount; ++i)
    {
        // Custom curve breakpoints: y at 16 fixed x positions across [-1,1];
        // defaults trace the identity diagonal (= no effect, per the spec's editor)
        const float defY = -1.0f + 2.0f * (float) (i - 1) / (float) (curvePointCount - 1);
        layout.add (std::make_unique<P> (curvePointId (i),
                                         juce::String::formatted ("Curve P%03d", i),
                                         Rng (-1.f, 1.f, 0.001f), defY));
    }

    // Filter (LP only, era-correct); cutoff in Hz, authentic 500 Hz floor unless flt.ext
    layout.add (std::make_unique<Pb> ("flt.on",     "Filter On",   true));
    layout.add (Pf ("flt.cutoff", "Cutoff",      logRange (20.f, 20000.f), 20000.f, "Hz", 0));
    layout.add (std::make_unique<Pb> ("flt.ext",    "Filter Ext",  false));
    layout.add (std::make_unique<P>  ("flt.poles",  "Poles",       Rng (1.f, 4.f, 1.f), 2.f));
    layout.add (Pf ("flt.envamt", "Filter Env",  Rng (-60.f, 60.f, 0.1f), 0.f, "st", 1));
    layout.add (Pf ("fenv.a", "FEnv A", logRange (0.001f, 10.f), 0.005f, "s", 3));
    layout.add (Pf ("fenv.d", "FEnv D", logRange (0.001f, 10.f), 0.2f, "s", 3));
    layout.add (std::make_unique<P>  ("fenv.s", "FEnv S", Rng (0.f, 1.f), 0.8f));
    layout.add (Pf ("fenv.r", "FEnv R", logRange (0.001f, 10.f), 0.15f, "s", 3));

    // Resonator
    layout.add (std::make_unique<Pb> ("res.on",   "Resonator On", true));
    layout.add (Pf ("res.freq", "Res Freq",     logRange (20.f, 2000.f), 130.81f, "Hz", 1)); // C3, unison with source root
    layout.add (std::make_unique<P>  ("res.fb",   "Res FB",       Rng (-0.995f, 0.995f, 0.001f), 0.f));
    layout.add (Pf ("res.damp", "Res Damp",     logRange (500.f, 15000.f), 8000.f, "Hz", 0));

    // Spectral inverter (manual: Types A and B)
    layout.add (std::make_unique<Pb> ("inv.on",   "Invert On",   true));
    layout.add (std::make_unique<P>  ("inv.mix",  "Invert Mix",  Rng (0.f, 1.f), 0.f));
    layout.add (std::make_unique<Pc> ("inv.type", "Invert Type", invTypes, 0));

    // era Noise model (manual: randomized sine; 25/25 defaults per the spec screenshot)
    layout.add (Pf ("noise.amp",   "Amp Noise",   Rng (0.f, 100.f, 1.f), 25.f, "%", 0));
    layout.add (Pf ("noise.phase", "Phase Noise", Rng (0.f, 100.f, 1.f), 25.f, "%", 0));

    // Delay
    layout.add (std::make_unique<Pb> ("dly.on",   "Delay On",     true));
    layout.add (Pf ("dly.time", "Delay Time",   logRange (0.1f, 2000.f), 80.f, "ms", 1));
    layout.add (Pf ("dly.fine", "Delay Fine",   Rng (-10.f, 10.f, 0.01f), 0.f, "ms", 2));
    layout.add (std::make_unique<P>  ("dly.mix",  "Delay Mix",    Rng (0.f, 1.f), 0.f));
    layout.add (std::make_unique<Pb> ("dly.inv",  "Delay Invert", false));
    layout.add (std::make_unique<P>  ("dly.fb",   "Delay FB",     Rng (0.f, 0.9f, 0.001f), 0.f));

    // Amp envelope
    layout.add (Pf ("amp.a", "Amp A", logRange (0.001f, 10.f), 0.005f, "s", 3));
    layout.add (Pf ("amp.d", "Amp D", logRange (0.001f, 10.f), 0.2f, "s", 3));
    layout.add (std::make_unique<P> ("amp.s", "Amp S", Rng (0.f, 1.f), 0.8f));
    layout.add (Pf ("amp.r", "Amp R", logRange (0.001f, 10.f), 0.15f, "s", 3));

    // Aux envelope ("patch an envelope anywhere", productized)
    layout.add (Pf ("aux.a", "Aux A", logRange (0.001f, 10.f), 0.005f, "s", 3));
    layout.add (Pf ("aux.d", "Aux D", logRange (0.001f, 10.f), 0.3f, "s", 3));
    layout.add (std::make_unique<P>  ("aux.s", "Aux S", Rng (0.f, 1.f), 0.f));
    layout.add (Pf ("aux.r", "Aux R", logRange (0.001f, 10.f), 0.2f, "s", 3));
    layout.add (std::make_unique<Pc> ("aux.dest",   "Aux Dest",   auxDests, 0));
    layout.add (std::make_unique<P>  ("aux.amount", "Aux Amount", Rng (-1.f, 1.f, 0.001f), 0.f));

    // Voices
    layout.add (std::make_unique<Pc> ("voice.mode",   "Voice Mode", voiceModes, 0));
    // 0 disables the wheel outright (DSP-NOTES §9.1) - a drifting wheel on a cheap
    // controller would otherwise detune everything with no obvious cause
    layout.add (Pf ("midi.bendrange", "Bend Range", Rng (0.f, 24.f, 1.f), 2.f, "st", 0));
    layout.add (std::make_unique<Pb> ("voice.retrig", "Retrigger",  true));
    layout.add (std::make_unique<Pb> ("voice.unison", "Unison",     false));
    layout.add (Pf ("voice.spread", "Spread",     Rng (0.f, 50.f, 0.1f), 12.f, "c", 1));

    // PLAY: a latching note trigger so the instrument works without a MIDI keyboard and
    // both hands stay on the panel. A parameter (not just UI state) so it automates.
    layout.add (std::make_unique<Pb> ("play.hold", "Play", false));

    // Tape (REC latch / FLIP trigger are parameters so host automation can drive them;
    // the processor edge-detects FLIP)
    layout.add (std::make_unique<Pb> ("tape.rec",  "Tape Rec",  false));
    layout.add (std::make_unique<Pb> ("tape.flip", "Tape Flip", false));

    // Sampler colour + output
    layout.add (std::make_unique<Pc> ("col.mode", "Colour",      colourModes, 1));
    layout.add (Pf ("col.rate", "Colour Rate", logRange (8000.f, 48000.f), 44100.f, "Hz", 0));
    // Global wet/dry: dry = the un-mangled source, blended before COLOUR/OUT
    // (DSP-NOTES §12b). Default 1 = full wet keeps the unity null bit-exact.
    layout.add (std::make_unique<P>  ("chain.mix","Mix",         Rng (0.f, 1.f), 1.f));
    // True bypass; returned from getBypassParameter() so hosts bind their own control.
    // A parameter (not UI state) for the same reason as play.hold: host automation.
    layout.add (std::make_unique<Pb> ("bypass",   "Bypass",      false));
    layout.add (Pf ("out.level","Output",      Rng (-60.f, 6.f, 0.1f), 0.f, "dB", 1));

    return layout;
}
} // namespace broken::params
