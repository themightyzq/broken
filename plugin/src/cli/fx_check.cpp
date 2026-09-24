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
// Exit 0 on pass, 1 on any failure. Every check is printed so a failure is diagnosable
// without re-running under a debugger.

#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/PluginProcessor.h"

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
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for APVTS internals, headless-safe

    // ---- (a) + (b): default instance, -14 dBFS tone, identical L/R -----------------
    {
        broken::BrokenProcessor proc;
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
        broken::BrokenProcessor proc;
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
                broken::BrokenProcessor proc;
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
        broken::BrokenProcessor proc;
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

    std::cout << "broken_fx_check: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
