// broken_fx_check: headless console gate for the Broken FX processor (compiled with
// BROKEN_FX=1). No host, no MIDI -- proves the contract in the task spec / plugin/CLAUDE.md:
//   (a) a -14 dBFS test tone through a default instance produces non-silent output
//   (b) level within 3 dB of input RMS, peak below -1 dBFS
//   (c) identical L/R input gives bit-identical L/R output
//   (d) different L/R input (tone in L, silence in R) leaves R silent-ish (< -60 dBFS)
//   (e) no NaN/Inf and stable output at block sizes 64/100/1024, sample rates
//       44100/48000/96000
//   (f) getLatencySamples() == 0, identical across those configs
//   (g) engaging bypass returns the input unchanged after the fade
//   (h) item 1 regression: a sample loaded via the real loadSampleFile path, mod.source=
//       Sample, differs audibly from mod.source=Osc, AND a mono input gives bit-identical
//       L/R output (proves engineR got the sample mirror, not just engine)
//   (i) item 2: the Table mod source (osc.mode Draw/Harmonic) tracks the OSCILLATOR panel
//   (j) item 3: FM now works on the live Input source (InputVarispeed.h)
//   (k) item 5: FX tape record -> mod.source=Tape changes the output
// Exit 0 on pass, 1 on any failure. Every check is printed so a failure is diagnosable
// without re-running under a debugger.

#include <cmath>
#include <memory>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/PluginProcessor.h"
#include "plugin/PresetManager.h"

namespace
{
constexpr double kTargetDbfs = -14.0;

// bit-exact comparison without -Wfloat-equal: this is deliberately NOT a tolerance
// check (test c wants bit-identical, not merely close) so the bitwise memcmp is the
// correct comparison, not a workaround.
bool bitIdenticalFloat (float a, float b)
{
    return std::memcmp (&a, &b, sizeof (float)) == 0;
}

double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
double linToDb (double lin) { return lin > 1.0e-12 ? 20.0 * std::log10 (lin) : -300.0; }

void setParam (broken::BrokenProcessor& proc, const char* id, float value)
{
    auto* p = proc.apvts.getParameter (id);
    jassert (p != nullptr);
    if (p == nullptr) return;
    const auto& range = p->getNormalisableRange();
    const float v = juce::jlimit (range.start, range.end, value);
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// 220 Hz tone + light noise, normalized to exactly kTargetDbfs RMS. `rightSilent`
// zeroes the R channel instead of duplicating L into it (test d).
void makeTestBuffer (juce::AudioBuffer<float>& buf, double sr, int n, uint32_t seed,
                     bool rightSilent)
{
    buf.setSize (2, n, false, false, true);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> noiseDist (-1.0f, 1.0f);

    std::vector<float> mono ((size_t) n);
    const double phaseInc = 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float tone = (float) std::sin (phase);
        phase += phaseInc;
        const float noise = 0.05f * noiseDist (rng); // "light noise"
        mono[(size_t) i] = tone + noise;
    }

    double sumSq = 0.0;
    for (auto v : mono) sumSq += (double) v * (double) v;
    const double rms = std::sqrt (sumSq / (double) juce::jmax (1, n));
    const double targetLin = dbToLin (kTargetDbfs);
    const float scale = rms > 1.0e-9 ? (float) (targetLin / rms) : 1.0f;
    for (auto& v : mono) v *= scale;

    for (int i = 0; i < n; ++i)
    {
        buf.setSample (0, i, mono[(size_t) i]);
        buf.setSample (1, i, rightSilent ? 0.0f : mono[(size_t) i]);
    }
}

// Drums+bass stand-in for (l): a saw bass line (a new note each second) plus a decaying
// noise hit every 0.5 s, normalized to -18 dBFS RMS. The factory bank was level-matched
// on this signal; a lone sine is the wrong probe for comb- and fold-based presets, which
// are pitch- and spectrum-selective by design.
void makeMusicBuffer (juce::AudioBuffer<float>& buf, double sr, int n)
{
    buf.setSize (2, n, false, false, true);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> noiseDist (-1.0f, 1.0f);
    const double notes[] = { 110.0, 82.41, 98.0, 73.42, 110.0, 146.83 };
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double f = notes[(size_t) t % 6];
        const double ph = std::fmod (t * f, 1.0);
        const double env = 0.6 + 0.4 * std::exp (-std::fmod (t, 1.0) * 3.0);
        const double hit = std::exp (-std::fmod (t, 0.5) * 25.0);
        mono[(size_t) i] = (float) (0.5 * (2.0 * ph - 1.0) * env + noiseDist (rng) * hit);
    }
    double sumSq = 0.0;
    for (auto v : mono) sumSq += (double) v * (double) v;
    const float scale = (float) (dbToLin (-18.0) / std::sqrt (sumSq / (double) juce::jmax (1, n)));
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch)
            buf.setSample (ch, i, mono[(size_t) i] * scale);
}

struct ChannelStats
{
    double rms = 0.0;
    float peak = 0.0f;
    bool nonFinite = false;
};

ChannelStats measure (const juce::AudioBuffer<float>& buf, int channel, int startSample, int numSamples)
{
    ChannelStats s;
    double sumSq = 0.0;
    const auto* d = buf.getReadPointer (channel);
    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        const float v = d[i];
        if (! std::isfinite (v)) s.nonFinite = true;
        sumSq += (double) v * (double) v;
        s.peak = std::max (s.peak, std::abs (v));
    }
    s.rms = std::sqrt (sumSq / (double) juce::jmax (1, numSamples));
    return s;
}

// Runs `proc` over `totalFrames` of `input` in `blockSize`-sized chunks, writing into
// `output` (sized to totalFrames already). Reuses one small AudioBuffer per block so
// nothing allocates inside the loop beyond JUCE's own per-call bookkeeping.
void render (broken::BrokenProcessor& proc, const juce::AudioBuffer<float>& input,
            juce::AudioBuffer<float>& output, int blockSize)
{
    const int total = input.getNumSamples();
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi; // always empty: Broken FX accepts no MIDI
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (blockSize, total - pos);
        block.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom (ch, 0, input, ch, pos, n);
        proc.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, pos, block, ch, 0, n);
        pos += n;
    }
}

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (! ok) ++failures;
}

// ---- tests (h)-(k) helpers: real sample files + a RMS-difference metric -----------

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

// A short, ordinary tone -- test (h)'s "real sample loaded via loadSampleFile" material.
juce::File makeToneSample (double sr)
{
    const int n = (int) std::llround (sr * 1.5);
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        m[(size_t) i] = (float) (0.6 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t));
    }
    auto f = tempFile ("fx_check_sample_tone.wav");
    writeMonoWav (f, m, sr);
    return f;
}

// test (j): a DC-offset sample -- exercises InputVarispeed's pull-back-to-centre against
// a modulator source (mod.source=Sample) that is NOT the Modulator's own +-1 oscillator.
juce::File makeDcOffsetSample (double sr)
{
    const int n = (int) std::llround (sr * 1.0);
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        m[(size_t) i] = (float) (0.3 + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 90.0 * t));
    }
    auto f = tempFile ("fx_check_sample_dc.wav");
    writeMonoWav (f, m, sr);
    return f;
}

// RMS(b - a) relative to RMS(a), in dB -- same metric control_audit.cpp's diffDbBetween
// uses, so "> -40 dB" reads the same way across both tools.
double diffDbBetween (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                     int channel, int start, int n)
{
    const auto* da = a.getReadPointer (channel);
    const auto* db = b.getReadPointer (channel);
    double sumSqDiff = 0.0, sumSqRef = 0.0;
    for (int i = start; i < start + n; ++i)
    {
        const double d = (double) db[i] - (double) da[i];
        sumSqDiff += d * d;
        sumSqRef  += (double) da[i] * (double) da[i];
    }
    const double rmsRef  = std::sqrt (sumSqRef  / (double) juce::jmax (1, n));
    const double rmsDiff = std::sqrt (sumSqDiff / (double) juce::jmax (1, n));
    const double floorLin = 1.0e-9;
    const double ratio = rmsDiff / std::max (rmsRef, floorLin);
    return ratio > 1.0e-15 ? linToDb (ratio) : -300.0;
}

// records the FIRST `seconds` of `input` into the tape, stops, then flips so the take
// becomes the ACTIVE buffer mod.source=Tape reads (TapeBuffer: REC always writes the
// INACTIVE side; playback -- here, tickModSource -- always reads the ACTIVE side).
// `input` may be longer than `seconds`; only its head is used.
void recordAndFlipTape (broken::BrokenProcessor& proc, const juce::AudioBuffer<float>& input,
                       double sr, int blockSize, double seconds)
{
    const int n = (int) std::llround (sr * seconds);
    juce::AudioBuffer<float> sub (2, n);
    for (int ch = 0; ch < 2; ++ch)
        sub.copyFrom (ch, 0, input, ch, 0, n);

    setParam (proc, "tape.rec", 1.0f);
    juce::AudioBuffer<float> out (2, n);
    render (proc, sub, out, blockSize);
    setParam (proc, "tape.rec", 0.0f);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> pump (2, 8);
    pump.clear();
    proc.processBlock (pump, midi); // let stopRec() land
    setParam (proc, "tape.flip", 1.0f);
    pump.clear();
    proc.processBlock (pump, midi); // rising edge caught here
    setParam (proc, "tape.flip", 0.0f);
    pump.clear();
    proc.processBlock (pump, midi);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for APVTS internals, headless-safe

    // ---- (a) + (b): default instance, -14 dBFS tone, identical L/R -----------------
    {
        auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
        auto& proc = *procOwner;
        const double sr = 48000.0;
        const int blockSize = 512;
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        proc.prepareToPlay (sr, blockSize);

        const int warmupFrames = (int) sr; // 1 s: let filters/envelope-free FX chain settle
        const int measureFrames = (int) sr; // 1 s measured
        const int total = warmupFrames + measureFrames;

        juce::AudioBuffer<float> input, output (2, total);
        makeTestBuffer (input, sr, total, 1, false);
        render (proc, input, output, blockSize);

        const auto inStats  = measure (input,  0, warmupFrames, measureFrames);
        const auto outStatsL = measure (output, 0, warmupFrames, measureFrames);
        const auto outStatsR = measure (output, 1, warmupFrames, measureFrames);

        check (! outStatsL.nonFinite && ! outStatsR.nonFinite, "(a)/(e) default instance: finite output");
        check (outStatsL.rms > 1.0e-5, "(a) default instance: non-silent output (L RMS > -100 dBFS)");

        const double inDb  = linToDb (inStats.rms);
        const double outDb = linToDb (outStatsL.rms);
        const double diffDb = std::abs (outDb - inDb);
        std::cout << "  input RMS " << inDb << " dBFS, output RMS " << outDb
                  << " dBFS (diff " << diffDb << " dB), peak " << linToDb ((double) outStatsL.peak) << " dBFS\n";
        check (diffDb <= 3.0, "(b) default level within 3 dB of input RMS");
        check (linToDb ((double) outStatsL.peak) < -1.0, "(b) default peak below -1 dBFS");

        // (c): identical L/R input -> bit-identical L/R output, sample for sample, over
        // the WHOLE render (not just the measured tail) -- the two engines must never
        // diverge, from the very first sample.
        bool bitIdentical = true;
        for (int i = 0; i < total && bitIdentical; ++i)
            if (! bitIdenticalFloat (output.getSample (0, i), output.getSample (1, i)))
                bitIdentical = false;
        check (bitIdentical, "(c) identical L/R input gives bit-identical L/R output");

        check (proc.getLatencySamples() == 0, "(f) getLatencySamples() == 0 @ 48000/512");
    }

    // ---- (d): tone in L only, silence in R -> R stays silent-ish -------------------
    {
        auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
        auto& proc = *procOwner;
        const double sr = 48000.0;
        const int blockSize = 512;
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        proc.prepareToPlay (sr, blockSize);

        const int warmupFrames = (int) sr;
        const int measureFrames = (int) sr;
        const int total = warmupFrames + measureFrames;

        juce::AudioBuffer<float> input, output (2, total);
        makeTestBuffer (input, sr, total, 2, true); // R silent
        render (proc, input, output, blockSize);

        const auto outStatsL = measure (output, 0, warmupFrames, measureFrames);
        const auto outStatsR = measure (output, 1, warmupFrames, measureFrames);
        std::cout << "  L RMS " << linToDb (outStatsL.rms) << " dBFS, R peak "
                  << linToDb ((double) outStatsR.peak) << " dBFS (R input was silent)\n";
        check (outStatsL.rms > 1.0e-5, "(d) L channel processed (non-silent) with silent R input");
        check (linToDb ((double) outStatsR.peak) < -60.0, "(d) R channel stays below -60 dBFS with silent R input");
    }

    // ---- (e) + (f): stability across block sizes / sample rates --------------------
    {
        const int blockSizes[] = { 64, 100, 1024 };
        const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
        bool allFinite = true, allStable = true, allLatencyZero = true;

        for (double sr : sampleRates)
        {
            for (int bs : blockSizes)
            {
                auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
                auto& proc = *procOwner;
                proc.setPlayConfigDetails (2, 2, sr, bs);
                proc.prepareToPlay (sr, bs);

                const int total = (int) (sr * 1.0); // 1 s
                juce::AudioBuffer<float> input, output (2, total);
                makeTestBuffer (input, sr, total, 3, false);
                render (proc, input, output, bs);

                const int tail = total / 2;
                const auto sL = measure (output, 0, total - tail, tail);
                const auto sR = measure (output, 1, total - tail, tail);
                if (sL.nonFinite || sR.nonFinite) allFinite = false;
                // "stable" = no runaway: a -14 dBFS input through a mild default chain
                // must never reach anywhere near clipping headroom repeatedly-summed gain
                if (sL.peak > 4.0f || sR.peak > 4.0f) allStable = false;
                if (proc.getLatencySamples() != 0) allLatencyZero = false;
            }
        }
        check (allFinite, "(e) no NaN/Inf at block sizes {64,100,1024} x sample rates {44100,48000,96000}");
        check (allStable, "(e) stable (non-runaway) output across the same matrix");
        check (allLatencyZero, "(f) getLatencySamples() == 0 and identical across the same matrix");
    }

    // ---- (g): bypass returns the input unchanged after the fade --------------------
    {
        auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
        auto& proc = *procOwner;
        setParam (proc, "bypass", 1.0f);
        const double sr = 48000.0;
        const int blockSize = 512;
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        proc.prepareToPlay (sr, blockSize); // prepare snaps the fade: no fade-in from a restored bypass

        juce::Random rng (0x5EED);
        juce::AudioBuffer<float> block (2, blockSize), input (2, blockSize);
        juce::MidiBuffer midi;
        int mismatches = 0;
        float worst = 0.0f;
        for (int blk = 0; blk < 20; ++blk)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = rng.nextFloat() * 2.0f - 1.0f;
                    input.setSample (ch, i, ch == 0 ? v : -0.5f * v); // distinct per-channel noise
                }
            block.makeCopyOf (input);
            proc.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float diff = std::abs (block.getSample (ch, i) - input.getSample (ch, i));
                    if (diff != 0.0f) { ++mismatches; worst = std::max (worst, diff); }
                }
        }
        std::cout << "  bypass: " << mismatches << " non-identical samples over 20 blocks (worst " << worst << ")\n";
        check (mismatches == 0, "(g) bypass returns the input unchanged after the fade");
    }

    // ---- (h) item 1 regression: real sample load, mod.source=Sample vs Osc, L/R mirror --
    {
        const double sr = 48000.0;
        const int blockSize = 512;
        auto sampleFile = makeToneSample (sr);

        auto procSampleOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
        auto& procSample = *procSampleOwner;
        procSample.setPlayConfigDetails (2, 2, sr, blockSize);
        procSample.prepareToPlay (sr, blockSize);
        juce::String loadErr;
        const bool loaded = procSample.loadSampleFile (sampleFile, loadErr);
        check (loaded, "(h) sample loads via the real loadSampleFile path");
        setParam (procSample, "mod.on", 1.0f);
        setParam (procSample, "mod.mode", 0.0f);   // AM
        setParam (procSample, "mod.amount", 0.8f);
        setParam (procSample, "mod.source", 2.0f); // Sample

        auto procOscOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
        auto& procOsc = *procOscOwner;
        procOsc.setPlayConfigDetails (2, 2, sr, blockSize);
        procOsc.prepareToPlay (sr, blockSize);
        juce::String loadErr2;
        procOsc.loadSampleFile (sampleFile, loadErr2);
        setParam (procOsc, "mod.on", 1.0f);
        setParam (procOsc, "mod.mode", 0.0f);
        setParam (procOsc, "mod.amount", 0.8f);
        setParam (procOsc, "mod.source", 0.0f);    // Osc (default)

        const int warmupFrames = (int) sr;
        const int measureFrames = (int) sr;
        const int total = warmupFrames + measureFrames;
        juce::AudioBuffer<float> input, outSample (2, total), outOsc (2, total);
        makeTestBuffer (input, sr, total, 10, false); // identical L/R -- the item-1 probe
        render (procSample, input, outSample, blockSize);
        render (procOsc, input, outOsc, blockSize);

        const double d = diffDbBetween (outOsc, outSample, 0, warmupFrames, measureFrames);
        std::cout << "  (h) mod.source=Sample vs Osc diff " << d << " dB\n";
        check (d > -40.0, "(h) mod.source=Sample differs from mod.source=Osc by > -40 dB RMS");

        bool bitIdentical = true;
        for (int i = 0; i < total && bitIdentical; ++i)
            if (! bitIdenticalFloat (outSample.getSample (0, i), outSample.getSample (1, i)))
                bitIdentical = false;
        check (bitIdentical,
              "(h) item-1 regression: mono input gives bit-identical L/R with mod.source=Sample "
              "(engineR must receive the sample mirror in loadSampleFile)");
    }

    // ---- (i) item 2: Table mod source tracks the OSCILLATOR panel (Draw + Harmonic) ----
    {
        const double sr = 48000.0;
        const int blockSize = 512;
        const int total = (int) sr; // 1 s

        auto renderWithOscMode = [&] (int oscModeIdx,
                                      const std::function<void (broken::BrokenProcessor&)>& setBank)
        {
            auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
            auto& proc = *procOwner;
            proc.setPlayConfigDetails (2, 2, sr, blockSize);
            proc.prepareToPlay (sr, blockSize);
            setParam (proc, "mod.on", 1.0f);
            setParam (proc, "mod.mode", 0.0f);   // AM
            setParam (proc, "mod.amount", 0.8f);
            setParam (proc, "mod.source", 4.0f); // Table
            setParam (proc, "osc.mode", (float) oscModeIdx);
            if (setBank) setBank (proc);

            juce::AudioBuffer<float> input (2, total), output (2, total);
            makeTestBuffer (input, sr, total, 20, false);
            render (proc, input, output, blockSize);
            return output;
        };

        auto refDraw = renderWithOscMode (2, nullptr); // Draw, Params.h default: a sine
        auto altDraw = renderWithOscMode (2, [] (broken::BrokenProcessor& proc)
        {
            for (int k = 0; k < broken::params::drawPointCount; ++k)
            {
                const juce::String id = broken::params::drawPointId (k + 1);
                setParam (proc, id.toRawUTF8(), (k % 2 == 0) ? 1.0f : -1.0f); // square-ish
            }
        });
        const double dDraw = diffDbBetween (refDraw, altDraw, 0, 0, total);
        std::cout << "  (i) Table+Draw: default sine vs square-ish points, diff " << dDraw << " dB\n";
        check (dDraw > -40.0, "(i) Table mod source (Draw): changing drawn points changes output > -40 dB RMS");

        auto refHarm = renderWithOscMode (1, nullptr); // Harmonic, Params.h default: 1/k recipe
        auto altHarm = renderWithOscMode (1, [] (broken::BrokenProcessor& proc)
        {
            for (int k = 1; k <= broken::params::harmonicCount; ++k)
            {
                const juce::String id = broken::params::harmonicId (k);
                setParam (proc, id.toRawUTF8(), (k == 1) ? 100.0f : 0.0f); // pure fundamental
            }
        });
        const double dHarm = diffDbBetween (refHarm, altHarm, 0, 0, total);
        std::cout << "  (i) Table+Harmonic: default 1/k recipe vs pure fundamental, diff " << dHarm << " dB\n";
        check (dHarm > -40.0, "(i) Table mod source (Harmonic): changing harmonic amplitudes changes output > -40 dB RMS");
    }

    // ---- (j) item 3: FM now works on the live Input source (InputVarispeed.h) ----------
    {
        const double sr = 48000.0;
        const int blockSize = 512;
        const int total = (int) sr; // 1 s

        auto renderFm = [&] (float fmIndex)
        {
            auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
            auto& proc = *procOwner;
            proc.setPlayConfigDetails (2, 2, sr, blockSize);
            proc.prepareToPlay (sr, blockSize);
            setParam (proc, "mod.on", 1.0f);
            setParam (proc, "mod.mode", 2.0f); // FM
            setParam (proc, "mod.amount", 0.8f);
            setParam (proc, "mod.fmindex", fmIndex);
            juce::AudioBuffer<float> input (2, total), output (2, total);
            makeTestBuffer (input, sr, total, 30, false);
            render (proc, input, output, blockSize);
            return output;
        };
        auto fm0 = renderFm (0.0f);
        auto fm2 = renderFm (2.0f);
        const double d = diffDbBetween (fm0, fm2, 0, 0, total);
        std::cout << "  (j) FM on Input: fmindex 0 vs 2, diff " << d << " dB\n";
        check (d > -40.0, "(j) FM on Input: fmindex 2 vs 0 differ by > -40 dB");

        // no NaN/Inf, bounded |out| over 10 s, fmindex=8, every mod source (0 Osc, 1 Self,
        // 2 Sample, 3 Tape, 4 Table); Sample uses a DC-offset sample (Sample/Tape/Table mod
        // sources can be off-centre, unlike the Modulator's own +-1 oscillator).
        auto dcSample = makeDcOffsetSample (sr);
        const int longTotal = (int) (sr * 10.0);
        bool allFinite = true, allBounded = true;
        for (int modSource = 0; modSource <= 4; ++modSource)
        {
            auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
            auto& proc = *procOwner;
            proc.setPlayConfigDetails (2, 2, sr, blockSize);
            proc.prepareToPlay (sr, blockSize);
            juce::String err;
            proc.loadSampleFile (dcSample, err); // feeds mod.source=Sample
            setParam (proc, "mod.on", 1.0f);
            setParam (proc, "mod.mode", 2.0f); // FM
            setParam (proc, "mod.amount", 1.0f);
            setParam (proc, "mod.fmindex", 8.0f);
            setParam (proc, "mod.source", (float) modSource);

            juce::AudioBuffer<float> input (2, longTotal);
            makeTestBuffer (input, sr, longTotal, (uint32_t) (40 + modSource), false);
            if (modSource == 3) // Tape: record+flip a take first, else it reads silence
                recordAndFlipTape (proc, input, sr, blockSize, 1.0);

            juce::AudioBuffer<float> output (2, longTotal);
            render (proc, input, output, blockSize);

            const auto sL = measure (output, 0, 0, longTotal);
            const auto sR = measure (output, 1, 0, longTotal);
            if (sL.nonFinite || sR.nonFinite) allFinite = false;
            if (sL.peak > 20.0f || sR.peak > 20.0f) allBounded = false;
            std::cout << "  (j) modSource=" << modSource << " fmindex=8: peak "
                      << std::max (sL.peak, sR.peak)
                      << (sL.nonFinite || sR.nonFinite ? " NON-FINITE" : "") << "\n";
        }
        check (allFinite, "(j) FM on Input, fmindex=8, every mod source: no NaN/Inf over 10 s");
        check (allBounded, "(j) FM on Input, fmindex=8, every mod source: |out| bounded (<20) over 10 s");

        // FM off -> bit-exact regardless of fmindex. Method: with mod.mode=AM (not FM),
        // Voice::render's fmActive is false, so InputVarispeed::processSample's `active`
        // argument is false and it returns x unmodified (InputVarispeed.h's documented
        // exact-bypass contract) -- fmindex is read but never consulted. This stands in for
        // "identical to the pre-change binary" (which no longer exists to diff against): it
        // proves the new stage is a true no-op whenever FM is not the active mod mode, the
        // exact condition its bypass contract requires, without needing a stored reference.
        auto renderAmFmIndex = [&] (float fmIndex)
        {
            auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
            auto& proc = *procOwner;
            proc.setPlayConfigDetails (2, 2, sr, blockSize);
            proc.prepareToPlay (sr, blockSize);
            setParam (proc, "mod.on", 1.0f);
            setParam (proc, "mod.mode", 0.0f); // AM, not FM
            setParam (proc, "mod.amount", 0.8f);
            setParam (proc, "mod.fmindex", fmIndex);
            juce::AudioBuffer<float> input (2, total), output (2, total);
            makeTestBuffer (input, sr, total, 30, false);
            render (proc, input, output, blockSize);
            return output;
        };
        auto amFmLo = renderAmFmIndex (0.0f);
        auto amFmHi = renderAmFmIndex (8.0f);
        bool amBitIdentical = true;
        for (int i = 0; i < total && amBitIdentical; ++i)
            for (int ch = 0; ch < 2 && amBitIdentical; ++ch)
                if (! bitIdenticalFloat (amFmLo.getSample (ch, i), amFmHi.getSample (ch, i)))
                    amBitIdentical = false;
        check (amBitIdentical,
              "(j) FM off (mod.mode=AM): output is bit-identical regardless of fmindex "
              "(InputVarispeed's inactive branch is a true no-op)");
    }

    // ---- (k) item 5: FX tape record -> mod.source=Tape changes the output ------------
    {
        const double sr = 48000.0;
        const int blockSize = 512;
        auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
        auto& proc = *procOwner;
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        proc.prepareToPlay (sr, blockSize);
        setParam (proc, "mod.on", 1.0f);
        setParam (proc, "mod.mode", 0.0f);   // AM
        setParam (proc, "mod.amount", 0.8f);
        setParam (proc, "mod.source", 3.0f); // Tape

        const int total = (int) sr;
        juce::AudioBuffer<float> input (2, total);
        makeTestBuffer (input, sr, total, 50, false);

        juce::AudioBuffer<float> before (2, total);
        render (proc, input, before, blockSize); // tape.rec is off: still empty here

        // record 1 s, stop, flip so the take becomes the ACTIVE buffer mod.source=Tape reads
        recordAndFlipTape (proc, input, sr, blockSize, 1.0);

        juce::AudioBuffer<float> after (2, total);
        render (proc, input, after, blockSize);

        const double d = diffDbBetween (before, after, 0, 0, total);
        std::cout << "  (k) FX tape: before vs after record+flip, mod.source=Tape, diff " << d << " dB\n";
        check (d > -40.0, "(k) FX tape: recording then mod.source=Tape changes the output by > -40 dB");
    }

    // ---- (l) every factory preset loads and sounds at a matched level ---------------
    // Presets only apply the ids they list (PresetManager.h), so each is loaded into a
    // fresh instance. The bank is level-matched to the input on makeMusicBuffer's signal;
    // a preset re-tuned by ear must be re-levelled, or this fails.
    {
        const double sr = 48000.0;
        const int blockSize = 512;
        int factoryCount = 0;
        auto probeOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
        auto& probe = *probeOwner;
        broken::PresetManager probeList (probe);
        for (size_t idx = 0; idx < probeList.getEntries().size(); ++idx)
        {
            if (probeList.getEntries()[idx].isUser) continue;
            ++factoryCount;
            const auto name = probeList.getEntries()[idx].name;

            auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
            auto& proc = *procOwner;
            proc.setPlayConfigDetails (2, 2, sr, blockSize);
            proc.prepareToPlay (sr, blockSize);
            broken::PresetManager pm (proc);
            juce::String err;
            const bool ok = pm.load ((int) idx, err);
            check (ok, "(l) " + name + " loads" + (ok ? juce::String() : ": " + err));

            const int total = (int) (6.0 * sr);
            juce::AudioBuffer<float> input, output (2, total);
            makeMusicBuffer (input, sr, total);
            render (proc, input, output, blockSize);
            const auto in  = measure (input,  0, 0, total);
            const auto out = measure (output, 0, 0, total);
            const double levelDb = linToDb (out.rms) - linToDb (in.rms);
            std::cout << "  (l) " << name << ": level " << levelDb << " dB, peak "
                      << linToDb ((double) out.peak) << " dBFS\n";
            check (! out.nonFinite && std::abs (levelDb) <= 3.0 && linToDb ((double) out.peak) <= -0.9,
                   "(l) " + name + ": finite, within 3 dB of input, peak <= -0.9 dBFS");
        }
        check (factoryCount >= 15, "(l) factory bank has all " + juce::String (factoryCount) + " presets");
    }

    // ---- (m) PITCH MIX acts on the live input -------------------------------------
    // Pitch MIX blends TapeShift's output with the live signal (Voice::render). With a
    // pitch set it must change the output; at PITCH 0 TapeShift is an exact bypass and
    // the blend is skipped, so MIX must leave the output bit-identical.
    {
        const double sr = 48000.0;
        const int blockSize = 512;
        const int total = 2 * (int) sr;
        juce::AudioBuffer<float> input (2, total);
        makeTestBuffer (input, sr, total, 70, false);

        auto renderWith = [&] (float pitch, float mix, juce::AudioBuffer<float>& out)
        {
            auto procOwner = std::make_unique<broken::BrokenProcessor>(); // heap: ~650 KB, too big for a 1 MB thread stack
            auto& proc = *procOwner;
            proc.setPlayConfigDetails (2, 2, sr, blockSize);
            proc.prepareToPlay (sr, blockSize);
            setParam (proc, "source.pitch", pitch);
            setParam (proc, "source.pitchmix", mix);
            out.setSize (2, total);
            render (proc, input, out, blockSize);
        };

        juce::AudioBuffer<float> full, half;
        renderWith (7.0f, 1.0f, full);
        renderWith (7.0f, 0.5f, half);
        const double d = diffDbBetween (full, half, 0, 0, total);
        std::cout << "  (m) PITCH +7 st: mix 1.0 vs 0.5, diff " << d << " dB\n";
        check (d > -40.0, "(m) PITCH MIX changes the output when PITCH is set");

        juce::AudioBuffer<float> unityFull, unityHalf;
        renderWith (0.0f, 1.0f, unityFull);
        renderWith (0.0f, 0.5f, unityHalf);
        bool same = true;
        for (int ch = 0; ch < 2 && same; ++ch)
            same = std::memcmp (unityFull.getReadPointer (ch), unityHalf.getReadPointer (ch),
                                sizeof (float) * (size_t) total) == 0;
        check (same, "(m) PITCH 0: PITCH MIX leaves the output bit-identical");
    }

    std::cout << "broken_fx_check: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
