#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>
#include <vector>
#include "dsp/Waveshaper.h"

// Phase C (v0.9): Random (seeded) and Custom (16-breakpoint) waveshaper curves.

using broken::dsp::Waveshaper;

namespace
{
// first sample after reset() bypasses DC-blocker history, so it shows the raw curve
float staticShape (Waveshaper& ws, float x)
{
    ws.reset();
    return ws.processSample (x);
}

Waveshaper makeShaper (int curve)
{
    Waveshaper ws;
    ws.prepare (48000.0);
    ws.setCurve (curve);
    ws.setDriveDb (0.0f);
    ws.setMorph (1.0f);
    ws.setTrimDb (0.0f);
    return ws;
}
} // namespace

TEST_CASE ("random curve is deterministic per seed and differs across seeds", "[phaseC]")
{
    auto sampleCurve = [] (int seed)
    {
        auto ws = makeShaper (8);
        ws.setRandomSeed (seed);
        std::vector<float> v;
        for (int i = 0; i <= 20; ++i)
            v.push_back (staticShape (ws, -1.0f + 2.0f * (float) i / 20.0f));
        return v;
    };
    const auto a1 = sampleCurve (1234);
    const auto a2 = sampleCurve (1234);
    const auto b  = sampleCurve (4321);

    REQUIRE (a1 == a2); // same seed -> byte-identical curve (snapshot recall works)

    int differences = 0;
    for (size_t i = 0; i < a1.size(); ++i)
        if (std::abs (a1[i] - b[i]) > 1e-4f) ++differences;
    REQUIRE (differences > 5); // different seed -> genuinely different shape
}

TEST_CASE ("random curve output stays bounded", "[phaseC]")
{
    auto ws = makeShaper (8);
    ws.setRandomSeed (99);
    for (int i = 0; i <= 200; ++i)
    {
        const float y = staticShape (ws, -1.0f + 2.0f * (float) i / 200.0f);
        REQUIRE (std::isfinite (y));
        REQUIRE (std::abs (y) <= 1.0001f);
    }
}

TEST_CASE ("custom curve honours its breakpoints", "[phaseC]")
{
    auto ws = makeShaper (9);
    constexpr int N = broken::dsp::Waveshaper::customPointCount; // 128 since v0.20
    std::array<float, N> pts {};
    for (int i = 0; i < N; ++i) // identity diagonal = the parameter defaults
        pts[(size_t) i] = -1.0f + 2.0f * (float) i / (float) (N - 1);
    ws.setCustomPoints (pts.data());

    // at a breakpoint x the curve must equal that breakpoint's y
    for (int i = 0; i < N; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / (float) (N - 1);
        REQUIRE (staticShape (ws, x) == Catch::Approx (pts[(size_t) i]).margin (2e-3f));
    }

    // a deliberately non-monotonic shape: flat zero everywhere -> output silent
    std::array<float, N> flat {};
    ws.setCustomPoints (flat.data());
    for (int i = 0; i <= 20; ++i)
        REQUIRE (std::abs (staticShape (ws, -1.0f + 0.1f * (float) i)) < 1e-6f);
}

TEST_CASE ("custom points are clamped into range", "[phaseC]")
{
    auto ws = makeShaper (9);
    std::array<float, broken::dsp::Waveshaper::customPointCount> wild {};
    wild.fill (7.5f); // absurd input must not produce absurd output
    ws.setCustomPoints (wild.data());
    for (int i = 0; i <= 20; ++i)
        REQUIRE (std::abs (staticShape (ws, -1.0f + 0.1f * (float) i)) <= 1.0001f);
}

TEST_CASE ("existing curves are untouched by the new ones", "[phaseC]")
{
    auto ws = makeShaper (1); // HardClip regression: the v0.2 values still hold
    REQUIRE (staticShape (ws, 0.8f) == Catch::Approx (1.0f).margin (1e-6f));
    REQUIRE (staticShape (ws, 0.4f) == Catch::Approx (0.6f).margin (1e-6f));
    auto lin = makeShaper (0);
    REQUIRE (staticShape (lin, 0.37f) == Catch::Approx (0.37f).margin (1e-6f));
}
