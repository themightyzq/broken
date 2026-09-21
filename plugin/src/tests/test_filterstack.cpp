#include <catch2/catch_test_macros.hpp>

#include "dsp/FilterStack.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace
{
// Runs `totalSamples` of a sine at `freq` through a prepared FilterStack and
// returns the peak |y| over the final `tailSamples`, once transients have
// died out.
float steadyStatePeak (ts::dsp::FilterStack& f, double sr, double freq, int totalSamples, int tailSamples)
{
    std::vector<float> tail (static_cast<std::size_t> (tailSamples));

    for (int n = 0; n < totalSamples; ++n)
    {
        const float x = static_cast<float> (std::sin (2.0 * std::numbers::pi * freq * n / sr));
        const float y = f.processSample (x);
        if (n >= totalSamples - tailSamples)
            tail[static_cast<std::size_t> (n - (totalSamples - tailSamples))] = y;
    }

    float peak = 0.0f;
    for (float v : tail)
        peak = std::max (peak, std::abs (v));
    return peak;
}
} // namespace

TEST_CASE ("single pole is roughly -3dB at its cutoff", "[filterstack]")
{
    ts::dsp::FilterStack f;
    const double sr = 48000.0;
    f.prepare (sr);
    f.setPoles (1);
    f.setCutoffHz (1000.0f);

    const float peak = steadyStatePeak (f, sr, 1000.0, 48000, 4800);
    const float dB = 20.0f * std::log10 (peak);

    REQUIRE (dB > -3.5f);
    REQUIRE (dB < -2.6f);
}

TEST_CASE ("single pole attenuates ~-12dB an octave above cutoff", "[filterstack]")
{
    ts::dsp::FilterStack f;
    const double sr = 48000.0;
    f.prepare (sr);
    f.setPoles (1);
    f.setCutoffHz (1000.0f);

    const float peak = steadyStatePeak (f, sr, 4000.0, 48000, 4800);
    const float dB = 20.0f * std::log10 (peak);

    REQUIRE (dB > -13.3f);
    REQUIRE (dB < -11.3f);
}

TEST_CASE ("four poles move the composite corner down to ~435 Hz", "[filterstack]")
{
    ts::dsp::FilterStack f;
    const double sr = 48000.0;
    f.prepare (sr);
    f.setPoles (4);
    f.setCutoffHz (1000.0f);

    const float peak = steadyStatePeak (f, sr, 435.0, 48000, 4800);
    const float dB = 20.0f * std::log10 (peak);

    REQUIRE (dB > -4.0f);
    REQUIRE (dB < -2.0f);
}

TEST_CASE ("DC passes through unattenuated regardless of pole count", "[filterstack]")
{
    for (int poles = 1; poles <= 4; ++poles)
    {
        ts::dsp::FilterStack f;
        f.prepare (48000.0);
        f.setPoles (poles);
        f.setCutoffHz (1000.0f);

        float y = 0.0f;
        for (int n = 0; n < 48000; ++n)
            y = f.processSample (0.5f);

        REQUIRE (std::abs (y - 0.5f) < 1e-3f);
    }
}

TEST_CASE ("stack stays finite and settles after an impulse", "[filterstack]")
{
    ts::dsp::FilterStack f;
    f.prepare (48000.0);
    f.setPoles (4);
    f.setCutoffHz (1000.0f);

    float y = f.processSample (1.0f);
    REQUIRE (std::isfinite (y));

    const int tailSamples = 100000;
    for (int n = 0; n < tailSamples; ++n)
    {
        y = f.processSample (0.0f);
        if (n % 10000 == 0)
            REQUIRE (std::isfinite (y));
    }

    REQUIRE (std::isfinite (y));
    REQUIRE (std::abs (y) < 1e-6f);
}
