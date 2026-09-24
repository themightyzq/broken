// broken_cli — headless render tool: plays a fixture through the real BrokenProcessor
// and writes the result, so scripts/analyze.py can gate every milestone without a DAW.
//
//   broken_cli --in <fixture.wav> --out <render.wav>            (FX / sample-through mode)
//   broken_cli --midi <notes.mid> --out <render.wav>            (instrument mode)
//   common: [--snapshot <file.json>] [--set id=value ...] [--sample <wav>]
//           [--block 512] [--tail seconds]
//
// The render sample rate is the input WAV's rate, or 48000 for MIDI-only (override
// with --sr). Every render exercises the identical processBlock the plugin runs.

#include <chrono>
#include <string>
#include <juce_audio_utils/juce_audio_utils.h>
#include "plugin/PluginProcessor.h"
#include "dsp/PitchDetector.h"

namespace
{
struct Args
{
    juce::String inPath, outPath, snapshotPath, samplePath, midiPath, saveTapePath, presetName;
    juce::StringArray sets;
    juce::StringArray ats; // timed actions: "<seconds>:<id>=<value>" or "<seconds>:noteon:<n>" / ":noteoff:<n>"
    int blockSize = 512;
    double tailSeconds = 0.0;
    double srOverride = 0.0;
};

bool parseArgs (const juce::StringArray& raw, Args& a, juce::String& err)
{
    for (int i = 0; i < raw.size(); ++i)
    {
        const auto& s = raw[i];
        auto next = [&] () -> juce::String {
            return ++i < raw.size() ? raw[i] : juce::String();
        };
        if      (s == "--in")       a.inPath = next();
        else if (s == "--out")      a.outPath = next();
        else if (s == "--snapshot") a.snapshotPath = next();
        else if (s == "--preset")   a.presetName = next();
        else if (s == "--sample")   a.samplePath = next();
        else if (s == "--midi")     a.midiPath = next();
        else if (s == "--set")      a.sets.add (next());
        else if (s == "--at")       a.ats.add (next());
        else if (s == "--save-tape") a.saveTapePath = next();
        else if (s == "--block")    a.blockSize = next().getIntValue();
        else if (s == "--tail")     a.tailSeconds = next().getDoubleValue();
        else if (s == "--sr")       a.srOverride = next().getDoubleValue();
        else { err = "unknown argument: " + s; return false; }
    }
    if (a.outPath.isEmpty())                          { err = "--out is required"; return false; }
    if (a.inPath.isEmpty() && a.midiPath.isEmpty())   { err = "need --in or --midi"; return false; }
    if (a.blockSize < 16 || a.blockSize > 8192)       { err = "--block out of range [16,8192]"; return false; }
    return true;
}

bool applySet (broken::BrokenProcessor& proc, const juce::String& spec, juce::String& err)
{
    auto eq = spec.indexOfChar ('=');
    if (eq <= 0) { err = "--set expects id=value, got: " + spec; return false; }
    auto id = spec.substring (0, eq);
    auto* p = proc.apvts.getParameter (id);
    if (p == nullptr) { err = "unknown parameter id: " + id; return false; }
    // clamp: convertTo0to1 doesn't, so `--set flt.poles=99` would store an out-of-range
    // normalized value that the DSP then reads as garbage (v0.12 review)
    const auto& range = p->getNormalisableRange();
    const float v = juce::jlimit (range.start, range.end, spec.substring (eq + 1).getFloatValue());
    p->setValueNotifyingHost (p->convertTo0to1 (v));
    return true;
}

juce::File resolve (const juce::String& p)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile (p);
}

// Carries the MIDI message ITSELF, not a flattened note. The old TimedNote kept only
// note-on/off and silently dropped everything else, so pitch bend could not be tested at
// all (v0.22 found this the hard way). Anything the plugin learns to read is now testable
// without touching the CLI again.
struct TimedMsg { double timeSec; juce::MidiMessage msg; };

bool loadMidi (const juce::File& f, std::vector<TimedMsg>& out, double& lengthSec, juce::String& err)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) { err = "cannot read MIDI " + f.getFullPathName(); return false; }
    mf.convertTimestampTicksToSeconds();
    lengthSec = 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* track = mf.getTrack (t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& msg = track->getEventPointer (i)->message;
            // everything the plugin might act on; meta events (tempo, track end) are not
            // channel messages and would only confuse processBlock
            if (msg.isNoteOnOrOff() || msg.isPitchWheel() || msg.isController()
                || msg.isChannelPressure() || msg.isAftertouch())
                out.push_back ({ msg.getTimeStamp(), msg });
            lengthSec = std::max (lengthSec, msg.getTimeStamp());
        }
    }
    std::stable_sort (out.begin(), out.end(),
                      [] (const TimedMsg& a, const TimedMsg& b) { return a.timeSec < b.timeSec; });
    return true;
}
} // namespace

// --state-roundtrip <sample.wav>: proves getState/setState restores the sample path and
// region params through the exact host save/restore path. Prints PASS/FAIL, exits 0/1.
static int stateRoundtrip (const juce::String& samplePath)
{
    juce::String err;
    broken::BrokenProcessor a;
    if (! a.loadSampleFile (juce::File::getCurrentWorkingDirectory().getChildFile (samplePath), err))
    { std::cerr << "roundtrip: load failed: " << err << "\n"; return 1; }
    for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
             { "sample.regstart", 0.2f }, { "sample.regend", 0.3f },
             { "sample.loopon", 1.0f }, { "sample.loopstyle", 1.0f },
             { "sample.rev", 1.0f }, { "sample.xfade", 42.0f } })
    {
        auto* p = a.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    juce::MemoryBlock state;
    a.getStateInformation (state);

    broken::BrokenProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    bool ok = b.getSampleName() == a.getSampleName() && ! b.isSampleMissing()
           && b.getSampleBuffer().size() == a.getSampleBuffer().size();
    for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
             { "sample.regstart", 0.2f }, { "sample.regend", 0.3f },
             { "sample.loopon", 1.0f }, { "sample.loopstyle", 1.0f },
             { "sample.rev", 1.0f }, { "sample.xfade", 42.0f } })
    {
        auto* p = b.apvts.getParameter (id);
        const float got = p->convertFrom0to1 (p->getValue());
        if (std::abs (got - v) > 0.05f) { std::cerr << "roundtrip mismatch " << id << ": " << got << "\n"; ok = false; }
    }
    std::cout << "state roundtrip: sample \"" << b.getSampleName() << "\", "
              << b.getSampleBuffer().size() << " samples, " << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------------
// Hardening pass (v0.12): adversarial fuzz + CPU bench. Parameters are enumerated from
// the processor itself, so the fuzzer can never drift out of sync with Params.h.
// ---------------------------------------------------------------------------------
namespace fuzz
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed ? seed : 1) {}
    float next01() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((double) s / 4294967295.0); }
    int nextInt (int n) { return n <= 0 ? 0 : (int) (next01() * (float) n) % n; }
};

// A musically plausible sample written to a temp WAV, loaded through the REAL
// loadSampleFile path (so the fuzz exercises the same code a user's drag-drop does).
static juce::File writeMaterialFile()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("broken_fuzz_material.wav");
    if (f.existsAsFile()) return f;

    const int n = 48000 * 2;
    juce::AudioBuffer<float> buf (1, n);
    Rng r (99);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / 48000.0;
        buf.getWritePointer (0)[i] = (float) (0.6 * std::exp (-t * 0.8)
                                              * (std::sin (2.0 * 3.14159265 * 110.0 * t)
                                                 + 0.3 * ((double) r.next01() * 2.0 - 1.0)));
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto* w = wav.createWriterFor (new juce::FileOutputStream (f), 48000.0, 1, 24, {}, 0))
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (buf, 0, n);
    }
    return f;
}

static void randomiseAll (broken::BrokenProcessor& proc, Rng& rng)
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rng.next01());
}

struct RenderStats
{
    float peak = 0.0f;        // whole render
    float peakEarly = 0.0f;   // first half
    float peakLate = 0.0f;    // second half
    float maxStep = 0.0f;
    bool nonFinite = false;

    // The invariant that separates a DEFECT from a loud patch: stacked gain controls
    // (TRIM +24 dB, resonator/delay feedback, unison) legitimately produce output far
    // above 0 dBFS, but they PLATEAU. A runaway keeps climbing. Both windows sit in the
    // LATE half so a slow-filling delay line isn't mistaken for divergence.
    bool diverging() const { return peakLate > 8.0f * std::max (peakEarly, 1.0e-6f) && peakLate > 4.0f; }
};

static RenderStats renderBlocks (broken::BrokenProcessor& proc, int blocks, int blockSize,
                          bool feedInput, bool notes, Rng& rng)
{
    RenderStats st;
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    float prev = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        if (feedInput)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buf.getWritePointer (ch)[i] = 0.4f * (rng.next01() * 2.0f - 1.0f);

        midi.clear();
        if (notes && b % 8 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + rng.nextInt (48), 0.9f), 0);
        if (notes && b % 8 == 5)
            midi.addEvent (juce::MidiMessage::noteOff (1, 36 + rng.nextInt (48)), 0);

        proc.processBlock (buf, midi);

        const bool measure = b >= blocks / 2;      // ignore the initial fill
        const bool secondHalf = b >= (3 * blocks) / 4;
        for (int i = 0; i < blockSize; ++i)
        {
            const float y = buf.getReadPointer (0)[i];
            if (! std::isfinite (y)) st.nonFinite = true;
            const float a = std::abs (y);
            st.peak = std::max (st.peak, a);
            if (measure)
                (secondHalf ? st.peakLate : st.peakEarly) = std::max (secondHalf ? st.peakLate : st.peakEarly, a);
            st.maxStep = std::max (st.maxStep, std::abs (y - prev));
            prev = y;
        }
    }
    return st;
}
} // namespace fuzz

// --fuzz [iterations] [seed]: random full-parameter states must never produce NaN/Inf
// or runaway output, and voices must always drain.
static int runFuzz (int iterations, int seed)
{
    using namespace fuzz;
    auto materialFile = writeMaterialFile();
    int failures = 0;
    float worstPeak = 0.0f;
    int worstSeedPeak = 0;

    for (int it = 0; it < iterations; ++it)
    {
        const int caseSeed = seed * 100000 + it;
        Rng rng ((uint32_t) caseSeed);

        broken::BrokenProcessor proc;
        juce::String loadErr;
        proc.loadSampleFile (materialFile, loadErr);
        randomiseAll (proc, rng);

        const int blockSize = 1 << (5 + rng.nextInt (5)); // 32..512
        proc.setPlayConfigDetails (2, 2, 48000.0, blockSize);
        proc.prepareToPlay (48000.0, blockSize);

        const bool feedInput = rng.next01() > 0.5f;
        // 2 s, not 1: a long delay line at high feedback is still FILLING after 1 s,
        // which the divergence heuristic read as a runaway (verified convergent by hand)
        auto st = renderBlocks (proc, 2 * 48000 / blockSize, blockSize, feedInput, true, rng);

        if (st.nonFinite)
        { std::cout << "FUZZ FAIL (non-finite) seed=" << caseSeed << "\n"; ++failures; }
        if (st.diverging())
        { std::cout << "FUZZ FAIL (diverging: early " << st.peakEarly << " -> late "
                    << st.peakLate << ") seed=" << caseSeed << "\n"; ++failures; }
        if (st.peak > worstPeak) { worstPeak = st.peak; worstSeedPeak = caseSeed; }
    }

    std::cout << "fuzz: " << iterations << " random states, " << failures << " failures"
              << " (worst peak " << worstPeak << " at seed " << worstSeedPeak << ")\n";
    return failures == 0 ? 0 : 1;
}

// --fuzz-diagnose <seed>: reproduce one failing fuzz case, print the offending
// parameter values, then re-run with each module bypassed to find who is responsible.
static int fuzzDiagnose (int caseSeed)
{
    using namespace fuzz;
    auto materialFile = writeMaterialFile();

    auto runCase = [&] (const char* bypassId) -> RenderStats
    {
        Rng rng ((uint32_t) caseSeed);
        broken::BrokenProcessor proc;
        juce::String loadErr;
        proc.loadSampleFile (materialFile, loadErr);
        randomiseAll (proc, rng);
        if (bypassId != nullptr)
            if (auto* p = proc.apvts.getParameter (bypassId))
                p->setValueNotifyingHost (0.0f);

        const int blockSize = 1 << (5 + rng.nextInt (5));
        proc.setPlayConfigDetails (2, 2, 48000.0, blockSize);
        proc.prepareToPlay (48000.0, blockSize);
        const bool feedInput = rng.next01() > 0.5f;
        return renderBlocks (proc, 48000 / blockSize, blockSize, feedInput, true, rng);
    };

    // print the state that triggered it
    {
        Rng rng ((uint32_t) caseSeed);
        broken::BrokenProcessor proc;
        juce::String loadErr;
        proc.loadSampleFile (materialFile, loadErr);
        randomiseAll (proc, rng);
        std::cout << "--- seed " << caseSeed << " parameters of interest:\n";
        for (const char* id : { "source.mode", "mod.on", "mod.mode", "mod.source", "mod.amount",
                                "ws.on", "ws.curve", "ws.drive", "ws.trim", "flt.on", "flt.poles",
                                "res.on", "res.fb", "res.freq", "res.damp", "inv.on", "inv.mix",
                                "dly.on", "dly.fb", "dly.mix", "stretch.on", "flat.on",
                                "col.mode", "out.level", "voice.unison" })
            if (auto* p = proc.apvts.getParameter (id))
                std::cout << "    " << id << " = " << p->convertFrom0to1 (p->getValue()) << "\n";
    }

    auto base = runCase (nullptr);
    std::cout << "baseline: peak " << base.peak << (base.nonFinite ? " NON-FINITE" : "") << "\n";
    for (const char* id : { "mod.on", "ws.on", "flt.on", "res.on", "inv.on", "dly.on",
                            "stretch.on", "flat.on" })
    {
        auto st = runCase (id);
        std::cout << "  bypass " << id << ": peak " << st.peak
                  << (st.nonFinite ? " NON-FINITE" : "") << "\n";
    }
    return 0;
}

// --bench: realtime factor for the worst-case configurations
static int runBench()
{
    using namespace fuzz;
    auto materialFile = writeMaterialFile();
    struct Cfg { const char* name; double sr; int block; bool unison; bool everything; };
    const Cfg cfgs[] = {
        { "idle (no notes)",        48000.0, 512, false, false },
        { "mono, default chain",    48000.0, 512, false, false },
        { "poly 6, all modules",    48000.0, 512, false, true  },
        { "poly+unison, all",       48000.0, 512, true,  true  },
        { "poly+unison, all",       48000.0,  64, true,  true  },
        { "poly+unison, all @96k",  96000.0, 512, true,  true  },
    };

    for (const auto& c : cfgs)
    {
        broken::BrokenProcessor proc;
        juce::String loadErr;
        proc.loadSampleFile (materialFile, loadErr);
        auto set = [&proc] (const char* id, float v)
        {
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        if (c.everything)
        {
            for (const char* on : { "mod.on", "ws.on", "flt.on", "res.on", "inv.on", "dly.on",
                                    "stretch.on", "flat.on" })
                set (on, 1.0f);
            set ("mod.amount", 0.7f); set ("res.fb", 0.8f); set ("inv.mix", 0.4f);
            set ("dly.mix", 0.4f); set ("dly.fb", 0.5f); set ("flt.poles", 4.0f);
            set ("stretch.amount", 60.0f); set ("col.mode", 2.0f);
            set ("source.mode", 2.0f); set ("osc.mode", 1.0f); // harmonic osc
            set ("voice.mode", 1.0f);
        }
        set ("voice.unison", c.unison ? 1.0f : 0.0f);
        set ("amp.s", 1.0f);

        proc.setPlayConfigDetails (2, 2, c.sr, c.block);
        proc.prepareToPlay (c.sr, c.block);

        const bool notes = std::string (c.name).find ("idle") == std::string::npos;
        const int blocks = (int) (c.sr * 4.0 / c.block); // 4 s of audio
        Rng rng (7);
        const auto t0 = std::chrono::steady_clock::now();
        auto st = renderBlocks (proc, blocks, c.block, false, notes, rng);
        const auto t1 = std::chrono::steady_clock::now();

        const double elapsed = std::chrono::duration<double> (t1 - t0).count();
        const double audioSeconds = (double) blocks * c.block / c.sr;
        std::cout << "bench " << c.name << " sr=" << (int) c.sr << " block=" << c.block
                  << ": " << (elapsed / audioSeconds) << "x realtime"
                  << " (peak " << st.peak << (st.nonFinite ? ", NON-FINITE" : "") << ")\n";
    }
    return 0;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for APVTS internals, headless-safe

    Args args;
    juce::StringArray raw;
    for (int i = 1; i < argc; ++i) raw.add (juce::String (juce::CharPointer_UTF8 (argv[i])));
    // --list-presets / --preset load from the SAME embedded data the plugin ships,
    // so this verifies what users get, not what happens to be on disk
    if (raw.size() == 1 && raw[0] == "--list-presets")
    {
        broken::BrokenProcessor proc;
        for (const auto& e : proc.presets.getEntries())
            std::cout << (e.isUser ? "user   " : "factory") << "  " << e.name << "\n";
        std::cout << proc.presets.getEntries().size() << " presets\n";
        return 0;
    }

    if (raw.size() == 2 && raw[0] == "--preset-check")
    {
        broken::BrokenProcessor proc;
        juce::String err;
        if (! proc.loadSampleFile (resolve (raw[1]), err))
        { std::cerr << "preset-check: " << err << "\n"; return 2; }

        // the contract: loading a factory preset must not disturb the sample or region
        auto* rs = proc.apvts.getParameter ("sample.regstart");
        auto* re = proc.apvts.getParameter ("sample.regend");
        rs->setValueNotifyingHost (rs->convertTo0to1 (0.25f));
        re->setValueNotifyingHost (re->convertTo0to1 (0.75f));
        const auto sampleBefore = proc.getSampleName();
        int failures = 0;

        for (size_t i = 0; i < proc.presets.getEntries().size(); ++i)
        {
            const auto name = proc.presets.getEntries()[i].name;
            if (! proc.presets.load ((int) i, err))
            { std::cout << "FAIL load " << name << ": " << err << "\n"; ++failures; continue; }

            const float gotRs = rs->convertFrom0to1 (rs->getValue());
            const float gotRe = re->convertFrom0to1 (re->getValue());
            if (std::abs (gotRs - 0.25f) > 1.0e-4f || std::abs (gotRe - 0.75f) > 1.0e-4f)
            { std::cout << "FAIL " << name << " moved the region\n"; ++failures; }
            if (proc.getSampleName() != sampleBefore || proc.getSampleBuffer().empty())
            { std::cout << "FAIL " << name << " disturbed the loaded sample\n"; ++failures; }
        }
        std::cout << "preset-check: " << proc.presets.getEntries().size() << " presets, "
                  << failures << " failures\n";
        return failures == 0 ? 0 : 1;
    }

    if (raw.size() == 2 && raw[0] == "--state-roundtrip")
        return stateRoundtrip (raw[1]);

    if (raw.size() >= 1 && raw[0] == "--fuzz")
        return runFuzz (raw.size() > 1 ? raw[1].getIntValue() : 500,
                        raw.size() > 2 ? raw[2].getIntValue() : 1);

    if (raw.size() >= 1 && raw[0] == "--bench")
        return runBench();

    if (raw.size() == 2 && raw[0] == "--fuzz-diagnose")
        return fuzzDiagnose (raw[1].getIntValue());

    // --bend-test: proves the pitch wheel actually bends, headless. Plays the root note on
    // a sine osc, walks the wheel centre -> full up -> full down, and checks the sounding
    // frequency each time. Also proves range 0 disables the wheel outright.
    if (raw.size() == 1 && raw[0] == "--bend-test")
    {
        auto soundingHz = [] (int wheel, float range)
        {
            broken::BrokenProcessor proc;
            for (const char* spec : { "mod.on=0", "ws.on=0", "flt.on=0", "res.on=0",
                                      "inv.on=0", "dly.on=0", "col.mode=0",
                                      "source.mode=2", "source.oscwave=0", "amp.s=1" })
            { juce::String e; applySet (proc, spec, e); }
            juce::String e;
            applySet (proc, "midi.bendrange=" + juce::String (range), e);

            proc.setPlayConfigDetails (2, 2, 48000.0, 512);
            proc.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, broken::dsp::SourceEngine::rootNote, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, wheel), 0);

            // let the tap ring fill past the detector window, then read the IN tap
            for (int i = 0; i < 120; ++i)
            { block.clear(); proc.processBlock (block, midi); midi.clear(); }

            constexpr int N = 4096;
            std::vector<float> tap ((size_t) N);
            proc.copyTunerTap (false, tap.data(), N);
            broken::dsp::PitchDetector det;
            det.prepare (48000.0);
            const auto r = det.detect (tap.data());
            return r.clarity >= broken::dsp::PitchDetector::clarityGate ? (double) r.hz : 0.0;
        };

        const double c3 = 130.813;
        struct Case { const char* name; int wheel; float range; double expect; };
        const Case cases[] = {
            { "centre, range 2",  8192,  2.0f, c3 },
            { "full up, range 2", 16383, 2.0f, c3 * std::pow (2.0, 2.0 / 12.0) },
            { "full down, range 2", 0,   2.0f, c3 * std::pow (2.0, -2.0 / 12.0) },
            { "full up, range 0", 16383, 0.0f, c3 },
        };
        int fails = 0;
        for (const auto& c : cases)
        {
            const double got = soundingHz (c.wheel, c.range);
            const double cents = got > 0.0 ? 1200.0 * std::log2 (got / c.expect) : 9999.0;
            const bool ok = std::abs (cents) < 5.0;
            if (! ok) ++fails;
            std::cout << "  " << c.name << ": " << got << " Hz, expect " << c.expect
                      << " (" << cents << " cents) " << (ok ? "PASS" : "FAIL") << "\n";
        }
        std::cout << "bend-test: " << fails << " failures\n";
        return fails == 0 ? 0 : 1;
    }

    // --state-migrate-check: v0.24 renamed the product; state saved under the working
    // title ("TurboSynth" tag) must still load, or every earlier session/preset is lost.
    if (raw.size() == 1 && raw[0] == "--state-migrate-check")
    {
        broken::BrokenProcessor src;
        juce::String e;
        applySet (src, "ws.drive=33", e);
        auto xml = src.apvts.copyState().createXml();
        xml->setTagName ("TurboSynth");                  // pretend it was saved pre-rename

        broken::BrokenProcessor dst;
        dst.restoreFromXml (*xml);
        const float got = dst.apvts.getRawParameterValue ("ws.drive")->load();
        const bool ok = std::abs (got - 33.0f) < 1.0e-3f;
        std::cout << "state-migrate-check: legacy tag -> ws.drive " << got
                  << (ok ? " PASS" : " FAIL") << "\n";
        return ok ? 0 : 1;
    }

    // --param-check: the guard for the bug class that shipped in v0.20. Parameter ids for
    // the indexed banks used to be formatted with a literal "ws.c%02d" in four different
    // files; renumbering a bank orphaned callers the compiler could not see, because
    // getParameter() just returns nullptr and a null guard swallows it (COPY->CUSTOM went
    // dead and nothing complained). Params.h now owns the ids; this proves every one of
    // them, and every id gatherParams reads, resolves to a real parameter.
    if (raw.size() == 1 && raw[0] == "--param-check")
    {
        broken::BrokenProcessor proc;
        int missing = 0;
        auto check = [&proc, &missing] (const juce::String& id)
        {
            if (proc.apvts.getParameter (id) == nullptr)
            { std::cout << "MISSING " << id << "\n"; ++missing; }
        };
        for (int i = 1; i <= broken::params::harmonicCount;   ++i) check (broken::params::harmonicId (i));
        for (int i = 1; i <= broken::params::drawPointCount;  ++i) check (broken::params::drawPointId (i));
        for (int i = 1; i <= broken::params::curvePointCount; ++i) check (broken::params::curvePointId (i));

        const int total = broken::params::harmonicCount + broken::params::drawPointCount
                        + broken::params::curvePointCount;
        std::cout << "param-check: " << total << " banked ids, " << missing << " missing\n";
        return missing == 0 ? 0 : 1;
    }

    // --tune-test: proves the tap -> detector -> TUNE lock chain headless. Plays a sine
    // osc detuned +30 cents, reads the IN tap, applies the lock, checks FINE == -30.
    // --rnd-check: RANDOMIZE must respect its exclusions and safety caps every roll,
    // and UNDO must restore the exact prior state (DSP-NOTES 12b).
    if (raw.size() == 1 && raw[0] == "--rnd-check")
    {
        broken::BrokenProcessor proc;
        auto real = [&] (const char* id)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id));
            return rp->convertFrom0to1 (rp->getValue());
        };
        const float mode0 = real ("source.mode"), out0 = real ("out.level");
        const float draw0  = real (broken::params::drawPointId (64).toRawUTF8());
        const float curve0 = real (broken::params::curvePointId (64).toRawUTF8());
        auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };

        int failures = 0;
        for (int roll = 0; roll < 50; ++roll)
        {
            proc.randomizeParams();
            auto expect = [&] (bool ok, const char* what)
            { if (! ok) { std::cout << "FAIL roll " << roll << ": " << what << "\n"; ++failures; } };
            expect (same (real ("source.mode"), mode0),   "source.mode changed");
            expect (same (real ("out.level"), out0),      "out.level changed");
            expect (real ("bypass")      < 0.5f,          "bypass engaged");
            expect (real ("play.hold")   < 0.5f,          "play latched");
            expect (real ("tape.rec")    < 0.5f,          "tape rec started");
            expect (std::abs (real ("res.fb")) <= 0.851f, "res.fb over cap");
            expect (real ("dly.fb")   <= 0.631f,          "dly.fb over cap");
            expect (real ("ws.drive") <= 30.01f,          "ws.drive over cap");
            expect (real ("amp.a")    <= 2.01f,           "amp.a over cap");
            expect (real ("flt.cutoff") >= 199.9f,        "flt.cutoff under floor");
            expect (same (real (broken::params::drawPointId (64).toRawUTF8()), draw0),   "draw bank touched");
            expect (same (real (broken::params::curvePointId (64).toRawUTF8()), curve0), "curve bank touched");
        }
        // UNDO restores the state before the LAST roll (that is its contract), so
        // snapshot immediately before one final roll and compare against that.
        const auto before = proc.apvts.copyState().createXml()->toString();
        proc.randomizeParams();
        proc.undoRandomize();
        const auto after = proc.apvts.copyState().createXml()->toString();
        if (after != before)
        {
            std::cout << "FAIL: UNDO did not restore the exact pre-roll state\n"; ++failures;
            const int len = juce::jmin (before.length(), after.length());
            int d = 0; while (d < len && before[d] == after[d]) ++d;
            std::cout << "  first diff at char " << d << "\n  before: ..."
                      << before.substring (juce::jmax (0, d - 60), d + 120).toStdString()
                      << "\n  after:  ..."
                      << after.substring (juce::jmax (0, d - 60), d + 120).toStdString() << "\n";
        }

        std::cout << "rnd-check: 50 rolls, " << failures << " failures -> "
                  << (failures == 0 ? "PASS" : "FAIL") << "\n";
        return failures == 0 ? 0 : 1;
    }

    // --bypass-check: BYPASS must pass the host input through BIT-exactly once the
    // 25 ms crossfade has completed, on both channels of a stereo pair (DSP-NOTES 12b).
    if (raw.size() == 1 && raw[0] == "--bypass-check")
    {
        broken::BrokenProcessor proc;
        {
            juce::String e;
            applySet (proc, "bypass=1", e);
            applySet (proc, "source.mode=4", e); // Input mode: chain would be audible if bypass leaked
        }
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512); // prepare snaps the fade: no fade-in from a restored bypass

        juce::Random rng (0x5EED);
        juce::AudioBuffer<float> block (2, 512), input (2, 512);
        juce::MidiBuffer midi;
        int mismatches = 0;
        float worst = 0.0f;
        for (int blk = 0; blk < 20; ++blk)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    // distinct per-channel noise so a mono-summed leak can't hide
                    const float v = rng.nextFloat() * 2.0f - 1.0f;
                    input.setSample (ch, i, ch == 0 ? v : -0.5f * v);
                }
            block.makeCopyOf (input);
            proc.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float diff = std::abs (block.getSample (ch, i) - input.getSample (ch, i));
                    if (diff != 0.0f) { ++mismatches; worst = std::max (worst, diff); }
                }
        }
        const bool ok = mismatches == 0;
        std::cout << "bypass-check: 20 blocks stereo noise, " << mismatches
                  << " non-identical samples (worst " << worst << ") -> "
                  << (ok ? "PASS" : "FAIL") << "\n";
        return ok ? 0 : 1;
    }

    if (raw.size() == 1 && raw[0] == "--tune-test")
    {
        broken::BrokenProcessor proc;
        for (const char* offSpec : { "mod.on=0", "ws.on=0", "flt.on=0", "res.on=0",
                                     "inv.on=0", "dly.on=0", "col.mode=0",
                                     "source.mode=2", "source.oscwave=0", "amp.s=1" })
        {
            juce::String e;
            applySet (proc, offSpec, e);
        }
        auto* fineP = proc.apvts.getParameter ("source.finecents");
        fineP->setValueNotifyingHost (fineP->convertTo0to1 (30.0f)); // 30 cents sharp

        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0); // A3 = 220 Hz
        for (int i = 0; i < 100; ++i) // ~1 s: fills the tap ring past the detector window
        {
            block.clear();
            proc.processBlock (block, midi);
            midi.clear();
        }

        const bool locked = proc.applyTuneLock();
        const float fineAfter = fineP->convertFrom0to1 (fineP->getValue());
        const bool ok = locked && std::abs (fineAfter - 0.0f) < 3.0f;
        std::cout << "tune-test: locked=" << (locked ? "yes" : "NO")
                  << " fine after lock = " << fineAfter
                  << " cents (started +30, expect ~0) -> " << (ok ? "PASS" : "FAIL") << "\n";
        return ok ? 0 : 1;
    }

    // emits a plugin-state blob (JUCE base64) with a sample + demo region loaded —
    // used to seed the standalone's saved state for GUI review screenshots
    // --dump-state-b64 <sample|-> [id=value ...] : emit a plugin-state blob, used to seed
    // the standalone for GUI verification (pass "-" for no sample)
    if (raw.size() >= 2 && raw[0] == "--dump-state-b64")
    {
        juce::String err;
        broken::BrokenProcessor proc;
        if (raw[1] != "-"
            && ! proc.loadSampleFile (juce::File::getCurrentWorkingDirectory().getChildFile (raw[1]), err))
        { std::cerr << "broken_cli error: " << err << "\n"; return 2; }
        for (int i = 2; i < raw.size(); ++i)
            if (! applySet (proc, raw[i], err))
            { std::cerr << "broken_cli error: " << err << "\n"; return 2; }
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                 { "sample.regstart", 0.32f }, { "sample.regend", 0.58f },
                 { "sample.loopon", 1.0f }, { "sample.loopstyle", 1.0f },
                 { "sample.xfade", 25.0f } })
        {
            auto* p = proc.apvts.getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        }
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        std::cout << state.toBase64Encoding() << "\n";
        return 0;
    }
    juce::String err;
    if (! parseArgs (raw, args, err)) { std::cerr << "broken_cli error: " << err << "\n"; return 2; }

    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    if (args.inPath.isNotEmpty())
    {
        reader.reset (fm.createReaderFor (resolve (args.inPath)));
        if (reader == nullptr) { std::cerr << "broken_cli error: cannot read " << args.inPath << "\n"; return 2; }
    }

    std::vector<TimedMsg> notes;
    double midiLen = 0.0;
    if (args.midiPath.isNotEmpty())
        if (! loadMidi (resolve (args.midiPath), notes, midiLen, err))
        { std::cerr << "broken_cli error: " << err << "\n"; return 2; }

    // timed actions let one render script the TAPE workflow (rec -> flip -> play back)
    struct TimedParam { double t; juce::String spec; };
    std::vector<TimedParam> timedParams;
    for (const auto& a : args.ats)
    {
        const auto colon = a.indexOfChar (':');
        if (colon <= 0) { std::cerr << "broken_cli error: --at expects <sec>:<action>, got " << a << "\n"; return 2; }
        const double t = a.substring (0, colon).getDoubleValue();
        const auto action = a.substring (colon + 1);
        if (action.startsWith ("noteon:"))
            notes.push_back ({ t, juce::MidiMessage::noteOn (1,
                action.fromFirstOccurrenceOf ("noteon:", false, false).getIntValue(), 1.0f) });
        else if (action.startsWith ("noteoff:"))
            notes.push_back ({ t, juce::MidiMessage::noteOff (1,
                action.fromFirstOccurrenceOf ("noteoff:", false, false).getIntValue()) });
        else
            timedParams.push_back ({ t, action });
    }
    std::stable_sort (notes.begin(), notes.end(),
                      [] (const TimedMsg& a, const TimedMsg& b) { return a.timeSec < b.timeSec; });
    std::stable_sort (timedParams.begin(), timedParams.end(),
                      [] (const TimedParam& a, const TimedParam& b) { return a.t < b.t; });

    const double sr = args.srOverride > 0.0 ? args.srOverride
                    : reader != nullptr ? reader->sampleRate : 48000.0;
    const juce::int64 numInFrames = reader != nullptr ? (juce::int64) reader->lengthInSamples
                                  : (juce::int64) std::llround ((midiLen + 0.5) * sr);
    const juce::int64 totalFrames = numInFrames + (juce::int64) std::llround (args.tailSeconds * sr);

    broken::BrokenProcessor proc;

    if (args.snapshotPath.isNotEmpty())
    {
        auto parsed = juce::JSON::parse (resolve (args.snapshotPath).loadFileAsString());
        if (! proc.loadSnapshotJson (parsed, err))
        { std::cerr << "broken_cli error: snapshot: " << err << "\n"; return 2; }
    }
    for (const auto& s : args.sets)
        if (! applySet (proc, s, err)) { std::cerr << "broken_cli error: " << err << "\n"; return 2; }

    if (args.presetName.isNotEmpty())
    {
        const int idx = proc.presets.indexOf (args.presetName);
        if (idx < 0 || ! proc.presets.load (idx, err))
        { std::cerr << "broken_cli error: preset '" << args.presetName << "': " << err << "\n"; return 2; }
    }

    if (args.samplePath.isNotEmpty())
        if (! proc.loadSampleFile (resolve (args.samplePath), err))
        { std::cerr << "broken_cli error: " << err << "\n"; return 2; }

    proc.setPlayConfigDetails (2, 2, sr, args.blockSize);
    proc.prepareToPlay (sr, args.blockSize);

    auto outFile = resolve (args.outPath);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFmt.createWriterFor (new juce::FileOutputStream (outFile), sr, 2, 24, {}, 0));
    if (writer == nullptr) { std::cerr << "broken_cli error: cannot write " << args.outPath << "\n"; return 2; }

    juce::AudioBuffer<float> block (2, args.blockSize);
    juce::MidiBuffer midi;
    size_t nextNote = 0;
    size_t nextTimed = 0;
    juce::int64 pos = 0;
    while (pos < totalFrames)
    {
        const int n = (int) std::min<juce::int64> (args.blockSize, totalFrames - pos);
        block.setSize (2, n, false, false, true);
        block.clear();

        if (reader != nullptr)
        {
            const juce::int64 available = std::max<juce::int64> (0, numInFrames - pos);
            if (available > 0)
            {
                const int toRead = (int) std::min<juce::int64> (n, available);
                reader->read (&block, 0, toRead, pos, true, true);
                if (reader->numChannels == 1)
                    block.copyFrom (1, 0, block, 0, 0, toRead);
            }
        }

        while (nextTimed < timedParams.size()
               && (juce::int64) std::llround (timedParams[nextTimed].t * sr) < pos + n)
        {
            if (! applySet (proc, timedParams[nextTimed].spec, err))
            { std::cerr << "broken_cli error: --at: " << err << "\n"; return 2; }
            ++nextTimed;
        }

        midi.clear();
        while (nextNote < notes.size())
        {
            const auto frame = (juce::int64) std::llround (notes[nextNote].timeSec * sr);
            if (frame >= pos + n) break;
            const int off = (int) std::max<juce::int64> (0, frame - pos);
            midi.addEvent (notes[nextNote].msg, off);
            ++nextNote;
        }

        proc.processBlock (block, midi);
        writer->writeFromAudioSampleBuffer (block, 0, n);
        pos += n;
    }

    writer->flush();
    std::cout << "broken_cli: wrote " << outFile.getFullPathName()
              << " (" << totalFrames << " frames @ " << sr << " Hz)\n";

    if (args.saveTapePath.isNotEmpty())
    {
        // same code path the panel's SAVE button runs — that's the point
        if (! proc.saveTapeToFile (resolve (args.saveTapePath), err))
        { std::cerr << "broken_cli error: save-tape: " << err << "\n"; return 3; }
        std::cout << "broken_cli: saved tape to " << resolve (args.saveTapePath).getFullPathName() << "\n";
    }
    return 0;
}
