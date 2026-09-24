#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include "dsp/SourceEngine.h"
#include "dsp/Flatten.h"

// Phase D (v0.10): Stretcher / Time Compressor, realtime Envelope Removal (FLATTEN).

using broken::dsp::SourceEngine;
using broken::dsp::Flatten;

namespace
{
constexpr double SR = 48000.0;

// how many samples until a one-shot head goes silent = the rendered duration
int oneShotDuration (SourceEngine& src, int limit = 400000)
{
    int lastNonZero = 0;
    for (int i = 0; i < limit; ++i)
        if (std::abs (src.processSample (0.0f)) > 1e-6f) lastNonZero = i;
    return lastNonZero;
}

std::vector<float> ramp (size_t n)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i)
        v[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 110.0 * (double) i / SR);
    return v;
}
} // namespace

TEST_CASE ("stretch lengthens playback, compress shortens it", "[phaseD]")
{
    auto buf = ramp (48000); // 1 s at 110 Hz
    auto make = [&buf] (float amount)
    {
        auto src = std::make_unique<SourceEngine>();
        src->prepare (SR);
        src->setSampleData (buf.data(), buf.size(), SR);
        src->setLoopOn (false);
        src->setStretch (amount != 0.0f, 110.0f, amount, 0.0f);
        src->noteOn (SourceEngine::rootNote, 1);
        return src;
    };

    auto plain = make (0.0f);
    const int dPlain = oneShotDuration (*plain);

    auto stretched = make (75.0f);   // k = 4
    const int dStretch = oneShotDuration (*stretched);

    auto compressed = make (-75.0f); // k = 4
    const int dCompress = oneShotDuration (*compressed);

    REQUIRE (dStretch > (int) (dPlain * 2.5));   // materially longer
    REQUIRE (dCompress < (int) (dPlain * 0.6));  // materially shorter
}

TEST_CASE ("stretch predelay leaves the attack untouched", "[phaseD]")
{
    auto buf = ramp (48000);
    auto make = [&buf] (bool stretch)
    {
        auto src = std::make_unique<SourceEngine>();
        src->prepare (SR);
        src->setSampleData (buf.data(), buf.size(), SR);
        src->setLoopOn (false);
        src->setStretch (stretch, 110.0f, 100.0f, 100.0f); // 100 ms predelay
        src->noteOn (SourceEngine::rootNote, 1);
        return src;
    };
    auto a = make (false);
    auto b = make (true);
    const int predelaySamples = (int) (0.100 * SR);
    for (int i = 0; i < predelaySamples; ++i)
        REQUIRE (a->processSample (0.0f) == Catch::Approx (b->processSample (0.0f)).margin (1e-6f));
}

TEST_CASE ("stretched output stays finite and in range", "[phaseD]")
{
    auto buf = ramp (48000);
    SourceEngine src;
    src.prepare (SR);
    src.setSampleData (buf.data(), buf.size(), SR);
    src.setLoopOn (true);
    src.setLoopStyle (SourceEngine::StylePingPong);
    src.setStretch (true, 37.0f, -100.0f, 0.0f); // deliberately mismatched freq + max compress
    src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i < 200000; ++i)
    {
        const float y = src.processSample (0.0f);
        REQUIRE (std::isfinite (y));
        REQUIRE (std::abs (y) <= 1.0f);
    }
}

TEST_CASE ("flatten evens out a decaying envelope", "[phaseD]")
{
    Flatten flat;
    flat.prepare (SR);
    flat.setResponseMs (20.0f);

    // exponentially decaying 200 Hz tone: 40 dB of decay over 2 s
    std::vector<float> in, out;
    for (int i = 0; i < 96000; ++i)
    {
        const double t = (double) i / SR;
        const float x = 0.8f * (float) (std::exp (-t * 2.3) * std::sin (2.0 * 3.14159265358979 * 200.0 * t));
        in.push_back (x);
        out.push_back (flat.processSample (x));
    }

    auto rmsOf = [] (const std::vector<float>& v, int a, int b)
    {
        double s = 0.0;
        for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
        return std::sqrt (s / (b - a));
    };
    const double inEarly = rmsOf (in, 24000, 28000), inLate = rmsOf (in, 76000, 80000);
    const double outEarly = rmsOf (out, 24000, 28000), outLate = rmsOf (out, 76000, 80000);
    const double inSpreadDb = 20.0 * std::log10 (inEarly / inLate);
    const double outSpreadDb = 20.0 * std::log10 (outEarly / outLate);

    REQUIRE (inSpreadDb > 15.0);                  // the source really does decay
    REQUIRE (outSpreadDb < inSpreadDb - 10.0);    // flatten removes most of that spread
}

TEST_CASE ("flatten does not amplify silence past its ceiling", "[phaseD]")
{
    Flatten flat;
    flat.prepare (SR);
    flat.setResponseMs (5.0f);
    for (int i = 0; i < 48000; ++i)
        REQUIRE (std::abs (flat.processSample (0.0f)) < 1e-9f);
    // a signal far below the gate is left essentially alone (never lifted by the
    // full +40 dB makeup, which is what would turn a noise floor into a roar)
    for (int i = 0; i < 48000; ++i)
    {
        const float y = flat.processSample (1e-6f);
        REQUIRE (std::abs (y) <= 1e-6f * 1.2f);
    }
}
