#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <chrono>
#include <cmath>
#include <vector>
#include "dsp/EnvelopeADSR.h"
#include "dsp/Flatten.h"
#include "dsp/SamplerColour.h"
#include "dsp/Resonator.h"
#include "dsp/SourceEngine.h"
#include "dsp/Modulator.h"

// v0.12 hardening pass: regression tests for defects found by the parameter fuzz,
// the sanitizer run, and the two code reviews. Each test FAILED before its fix.

using namespace broken::dsp;

namespace { constexpr double SR = 48000.0; }

TEST_CASE ("PM survives a modulator hotter than +-1 (ASan container-overflow)", "[hardening]")
{
    // Self/Sample modulator sources can exceed +-1; unclamped this drove the PM read
    // index off the end of its buffer (found by --fuzz + AddressSanitizer)
    Modulator mod;
    mod.prepare (SR);
    mod.setAmount (1.0f);
    for (float m : { -50.0f, -1.5f, 1.5f, 50.0f, 1.0e6f, -1.0e6f })
        for (int i = 0; i < 2000; ++i)
        {
            const float y = mod.applyPM (0.5f * (float) std::sin (i * 0.01), m);
            REQUIRE (std::isfinite (y));
            REQUIRE (std::abs (y) <= 1.0f);
        }
}

TEST_CASE ("flatten levels toward a target instead of amplifying without limit", "[hardening]")
{
    // before the fix a spiky source reached +37 dBFS out of a -6 dBFS input
    Flatten flat;
    flat.prepare (SR);
    flat.setResponseMs (30.0f);
    float peak = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        // high crest factor: short bursts separated by near-silence
        const bool burst = (i % 4800) < 200;
        const float x = burst ? 0.5f * (float) std::sin (i * 0.7) : 1.0e-4f;
        peak = std::max (peak, std::abs (flat.processSample (x)));
    }
    REQUIRE (peak < 4.0f); // was ~70 with the old y = x/env
}

TEST_CASE ("envelope retrigger to a lower velocity does not snap", "[hardening]")
{
    // poly voice-stealing always retriggers; a quiet note stealing a loud sustaining
    // voice used to jump the level in a single sample (an audible click)
    EnvelopeADSR env;
    env.prepare (SR);
    env.setTimes (0.005f, 0.2f, 0.8f, 0.15f);
    env.gateOn (1.0f);
    for (int i = 0; i < (int) (0.5 * SR); ++i) env.processSample(); // settle at sustain
    const float before = env.processSample();

    env.gateOn (0.1f); // stolen by a much quieter note
    const float after = env.processSample();
    REQUIRE (std::abs (after - before) < 0.05f);
}

TEST_CASE ("sample-and-hold keeps holding when the rate exceeds the sample rate", "[hardening]")
{
    // at a 44.1k project with COLOUR RATE at 48k the accumulator outran a single
    // decrement and the hold degenerated into per-sample quantization
    SamplerColour col;
    col.prepare (44100.0);
    col.setMode (1);
    col.setRateHz (48000.0f);
    int changes = 0;
    float prev = 0.0f;
    for (int i = 0; i < 4410; ++i)
    {
        const float y = col.processSample (0.4f * (float) std::sin (i * 0.02));
        if (i > 0 && std::abs (y - prev) > 1.0e-9f) ++changes;
        prev = y;
    }
    REQUIRE (std::isfinite (prev));
    REQUIRE (changes <= 4410); // must not run away; holding at most every sample
}

TEST_CASE ("resonator tunes correctly at a high sample rate", "[hardening]")
{
    // a fixed 8192-sample buffer could not hold 20 Hz at 192 kHz, so the comb silently
    // rang at the wrong pitch
    Resonator res;
    res.prepare (192000.0);
    res.setFreqHz (20.0f);
    res.setFeedback (0.9f);
    res.setDampHz (15000.0f);

    std::vector<float> out;
    out.push_back (res.processSample (1.0f));
    for (int i = 1; i < 192000; ++i) out.push_back (res.processSample (0.0f));

    // the comb's first repeat must land at sr/20 = 9600 samples
    int best = 0; float bestV = 0.0f;
    for (int lag = 1000; lag < 20000; ++lag)
        if (std::abs (out[(size_t) lag]) > bestV) { bestV = std::abs (out[(size_t) lag]); best = lag; }
    REQUIRE (best == Catch::Approx (9600).margin (50));
}

TEST_CASE ("extreme playback rate does not stall the wrap loops", "[hardening]")
{
    // the loop/cycle wraps used repeated subtraction; a huge step meant millions of
    // iterations per sample (an audio dropout, not a glitch)
    std::vector<float> buf (48000, 0.25f);
    SourceEngine src;
    src.prepare (SR);
    src.setSampleData (buf.data(), buf.size(), SR);
    src.setRegion (0.5f, 0.5001f);   // tiny region
    src.setLoopOn (true);
    src.setLoopStyle (SourceEngine::StyleLoop);
    src.setTranspose (48.0f);
    src.noteOn (127, 1);             // highest note + max transpose
    src.setRateMod (8.0f);           // plus full FM rate modulation

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 48000; ++i)
        REQUIRE (std::isfinite (src.processSample (0.0f)));
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    REQUIRE (elapsed < 1.0); // 1 s of audio must not take 1 s of CPU even here
}

TEST_CASE ("swapping the sample mid-note cannot strand the stretch segment state", "[hardening]")
{
    std::vector<float> big (96000, 0.3f), small (2000, 0.3f);
    SourceEngine src;
    src.prepare (SR);
    src.setSampleData (big.data(), big.size(), SR);
    src.setStretch (true, 110.0f, 80.0f, 0.0f);
    src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i < 20000; ++i) src.processSample (0.0f); // segment state far out

    src.setSampleData (small.data(), small.size(), SR); // user drops a new, shorter file
    for (int i = 0; i < 20000; ++i)
    {
        const float y = src.processSample (0.0f);
        REQUIRE (std::isfinite (y));
        REQUIRE (std::abs (y) <= 1.0f);
    }
}
