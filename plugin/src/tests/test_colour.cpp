#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "dsp/SamplerColour.h"
#include <functional>

// Exact float equality where bit-for-bit identity IS the contract (passthrough, hold,
// determinism, snapshot). std::equal_to keeps -Wfloat-equal out of it without weakening
// the check: this is still ==, not a tolerance.
static bool exactlyEqual (float a, float b) { return std::equal_to<float>{} (a, b); }

using ts::dsp::SamplerColour;

namespace
{
constexpr double kPi = 3.14159265358979323846;

double quantErrorRms (int mode, int n, double sr, double rate)
{
    SamplerColour sc;
    sc.prepare (sr);
    sc.setMode (mode);
    sc.setRateHz (static_cast<float> (rate));

    double sumSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto x = static_cast<float> (0.5011872 * std::sin (2.0 * kPi * 997.0 * static_cast<double> (i) / sr));
        const float y = sc.processSample (x);
        const double e = static_cast<double> (y) - static_cast<double> (x);
        sumSq += e * e;
    }
    return std::sqrt (sumSq / n);
}
} // namespace

TEST_CASE ("12-bit quantize noise floor matches LSB/sqrt(12)", "[colour]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;

    const double rms = quantErrorRms (1, n, sr, sr);
    const double theoretical = (1.0 / 2048.0) / std::sqrt (12.0);

    REQUIRE (rms >= theoretical / 2.0);
    REQUIRE (rms <= theoretical * 2.0);
}

TEST_CASE ("8-bit is coarser than 12-bit by about 16x", "[colour]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;

    const double rms12 = quantErrorRms (1, n, sr, sr);
    const double rms8 = quantErrorRms (2, n, sr, sr);
    const double ratio = rms8 / rms12;

    REQUIRE (ratio >= 8.0);
    REQUIRE (ratio <= 32.0);
}

TEST_CASE ("sample-and-hold repeats every 6 samples at rate 8000 / sr 48000", "[colour]")
{
    constexpr double sr = 48000.0;
    constexpr float rate = 8000.0f;
    constexpr float s = 2048.0f; // 12-bit quantize step, matches mode 1

    SamplerColour sc;
    sc.prepare (sr);
    sc.setMode (1);
    sc.setRateHz (rate);

    double acc = 1.0; // mirrors SamplerColour::reset()
    float held = 0.0f;
    int triggers = 0;

    for (int i = 0; i < 600; ++i)
    {
        const float x = static_cast<float> (i) * 1e-5f;

        acc += static_cast<double> (rate) / sr;
        const bool triggered = acc >= 1.0;
        if (triggered)
        {
            acc -= 1.0;
            held = std::round (x * s) / s;
            ++triggers;
        }

        const float y = sc.processSample (x);
        REQUIRE (exactlyEqual (y, held));
        REQUIRE (triggered == (i % 6 == 0));
    }

    REQUIRE (triggers == 100);
}

TEST_CASE ("Off mode is an exact passthrough", "[colour]")
{
    constexpr double sr = 48000.0;

    SamplerColour sc;
    sc.prepare (sr);
    sc.setMode (0);
    sc.setRateHz (8000.0f);

    for (int i = 0; i < 1000; ++i)
    {
        const auto x = static_cast<float> (std::sin (2.0 * kPi * 997.0 * static_cast<double> (i) / sr));
        const float y = sc.processSample (x);
        REQUIRE (std::abs (y - x) < 1e-9f);
    }
}
