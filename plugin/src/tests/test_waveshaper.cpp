#include <catch2/catch_test_macros.hpp>

#include "dsp/Waveshaper.h"

#include <cmath>
#include <random>

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Builds a Waveshaper prepared at 48 kHz with the given static settings, ready
// for a first-sample-after-reset check (blocker state starts at exactly zero).
ts::dsp::Waveshaper makeShaper (int curve, float driveDb, float morph, float trimDb)
{
    ts::dsp::Waveshaper ws;
    ws.prepare (48000.0);
    ws.setCurve (curve);
    ws.setDriveDb (driveDb);
    ws.setMorph (morph);
    ws.setTrimDb (trimDb);
    return ws;
}
} // namespace

TEST_CASE ("Linear curve at drive 0, morph 1, trim 0 is passthrough", "[waveshaper]")
{
    auto ws = makeShaper (0, 0.0f, 1.0f, 0.0f);

    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

    for (int i = 0; i < 1000; ++i)
    {
        const float x = dist (rng);
        const float y = ws.processSample (x);
        REQUIRE (std::abs (y - x) < 1e-6f);
    }
}

TEST_CASE ("HardClip static shape on first sample after reset", "[waveshaper]")
{
    {
        auto ws = makeShaper (1, 0.0f, 1.0f, 0.0f);
        REQUIRE (std::abs (ws.processSample (0.8f) - 1.0f) < 1e-6f);
    }
    {
        auto ws = makeShaper (1, 0.0f, 1.0f, 0.0f);
        REQUIRE (std::abs (ws.processSample (-0.8f) - (-1.0f)) < 1e-6f);
    }
    {
        auto ws = makeShaper (1, 0.0f, 1.0f, 0.0f);
        REQUIRE (std::abs (ws.processSample (0.4f) - 0.6f) < 1e-6f);
    }
}

TEST_CASE ("Fold static shape on first sample after reset", "[waveshaper]")
{
    {
        auto ws = makeShaper (3, 0.0f, 1.0f, 0.0f);
        REQUIRE (std::abs (ws.processSample (0.25f) - 0.5f) < 1e-6f);
    }
    {
        auto ws = makeShaper (3, 0.0f, 1.0f, 0.0f);
        REQUIRE (std::abs (ws.processSample (0.75f) - 0.5f) < 1e-6f);
    }
}

TEST_CASE ("Stair static shape on first sample after reset", "[waveshaper]")
{
    auto ws = makeShaper (5, 0.0f, 1.0f, 0.0f);
    REQUIRE (std::abs (ws.processSample (0.3f) - 0.25f) < 1e-6f);
}

TEST_CASE ("InvertS static shape on first sample after reset", "[waveshaper]")
{
    {
        auto ws = makeShaper (7, 0.0f, 1.0f, 0.0f);
        REQUIRE (std::abs (ws.processSample (1.0f) - (-1.0f)) < 1e-6f);
    }
    {
        auto ws = makeShaper (7, 0.0f, 1.0f, 0.0f);
        REQUIRE (std::abs (ws.processSample (0.5f) - (-0.75f)) < 1e-6f);
    }
}

TEST_CASE ("Asym curve DC blocker removes offset once settled", "[waveshaper]")
{
    auto ws = makeShaper (4, 12.0f, 1.0f, 0.0f);

    constexpr int kNumSamples = 48000;
    double sum = 0.0;
    int countedSamples = 0;

    for (int n = 0; n < kNumSamples; ++n)
    {
        const float x = static_cast<float> (std::sin (2.0 * kPi * 100.0 * n / 48000.0) * 0.5);
        const float y = ws.processSample (x);

        if (n >= kNumSamples / 2)
        {
            sum += y;
            ++countedSamples;
        }
    }

    const double mean = sum / countedSamples;
    REQUIRE (std::abs (mean) < 1e-3);
}

TEST_CASE ("Morph 0 equals drive-clipped passthrough for every non-linear curve", "[waveshaper]")
{
    for (int curve = 1; curve <= 7; ++curve)
    {
        auto ws = makeShaper (curve, 0.0f, 0.0f, 0.0f);
        const float y = ws.processSample (0.4f);
        REQUIRE (std::abs (y - 0.4f) < 1e-6f);
    }
}

TEST_CASE ("reset() clears blocker state so the static shape reappears", "[waveshaper]")
{
    auto ws = makeShaper (1, 0.0f, 1.0f, 0.0f);

    // Drive the blocker state away from zero.
    ws.processSample (0.8f);
    ws.processSample (100.0f);
    ws.processSample (-100.0f);

    ws.reset();

    REQUIRE (std::abs (ws.processSample (0.8f) - 1.0f) < 1e-6f);
}
