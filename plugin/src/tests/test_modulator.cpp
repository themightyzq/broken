#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

#include "dsp/Modulator.h"

using broken::dsp::Modulator;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Single-frequency power via Goertzel. Callers pick N and freq bin-friendly
// (k = freq*N/sr an integer) so leakage doesn't blur adjacent bins.
double goertzelPower (const std::vector<float>& x, double freq, double sr)
{
    const int n = static_cast<int> (x.size());
    const double w = 2.0 * kPi * freq / sr;
    const double coeff = 2.0 * std::cos (w);
    double q0 = 0.0, q1 = 0.0, q2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        q0 = coeff * q1 - q2 + static_cast<double> (x[static_cast<size_t> (i)]);
        q2 = q1;
        q1 = q0;
    }
    const double real = q1 - q2 * std::cos (w);
    const double imag = q2 * std::sin (w);
    return real * real + imag * imag;
}
} // namespace

TEST_CASE ("RM suppresses the carrier and produces sum/difference sidebands", "[modulator]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;

    Modulator mod;
    mod.prepare (sr);
    mod.setWave (0);
    mod.setFreqHz (100.0f);
    mod.setAmount (1.0f);

    std::vector<float> y (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto x = static_cast<float> (std::sin (2.0 * kPi * 1000.0 * static_cast<double> (i) / sr));
        const float m = mod.tick();
        y[static_cast<size_t> (i)] = mod.applyRM (x, m);
    }

    const double p900 = goertzelPower (y, 900.0, sr);
    const double p1000 = goertzelPower (y, 1000.0, sr);
    const double p1100 = goertzelPower (y, 1100.0, sr);

    REQUIRE (p900 >= 100.0 * p1000);
    REQUIRE (p1100 >= 100.0 * p1000);
}

TEST_CASE ("AM keeps the carrier alongside sidebands", "[modulator]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;

    Modulator mod;
    mod.prepare (sr);
    mod.setWave (0);
    mod.setFreqHz (100.0f);
    mod.setAmount (1.0f);

    std::vector<float> y (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto x = static_cast<float> (std::sin (2.0 * kPi * 1000.0 * static_cast<double> (i) / sr));
        const float m = mod.tick();
        y[static_cast<size_t> (i)] = mod.applyAM (x, m);
    }

    const double p900 = goertzelPower (y, 900.0, sr);
    const double p1000 = goertzelPower (y, 1000.0, sr);
    const double p1100 = goertzelPower (y, 1100.0, sr);

    // At d=1, applyAM is exactly 0.5*x + 0.5*x*m: carrier at half amplitude,
    // sidebands at half of *that* -- a power ratio of 4 (~6.02 dB), which sits
    // essentially on the 6 dB line by construction. A hair of margin (6.1 dB)
    // avoids failing on that boundary from floating-point rounding alone.
    const double ratioDb = 10.0 * std::log10 (p1000 / p900);
    REQUIRE (std::abs (ratioDb) <= 6.1);

    const double total = p900 + p1000 + p1100;
    REQUIRE (p900 >= 1e-4 * total);
    REQUIRE (p1100 >= 1e-4 * total);
}

TEST_CASE ("amount 0 is an exact identity", "[modulator]")
{
    Modulator mod;
    mod.prepare (48000.0);
    mod.setAmount (0.0f);

    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    for (int i = 0; i < 1000; ++i)
    {
        const float x = dist (rng);
        const float m = dist (rng);
        REQUIRE (std::abs (mod.applyRM (x, m) - x) < 1e-7f);
        REQUIRE (std::abs (mod.applyAM (x, m) - x) < 1e-7f);
    }
}

TEST_CASE ("bell wave stays bounded and has real energy", "[modulator]")
{
    Modulator mod;
    mod.prepare (48000.0);
    mod.setWave (1);
    mod.setFreqHz (55.0f);

    double sumSq = 0.0;
    constexpr int n = 100000;
    for (int i = 0; i < n; ++i)
    {
        const float m = mod.tick();
        REQUIRE (std::abs (m) <= 1.0f + 1e-6f);
        sumSq += static_cast<double> (m) * static_cast<double> (m);
    }
    const double rms = std::sqrt (sumSq / n);
    REQUIRE (rms > 0.1);
}

TEST_CASE ("odd wave drops partials above 0.45*sr", "[modulator]")
{
    constexpr double sr = 8000.0;
    constexpr float f = 1000.0f;

    Modulator mod;
    mod.prepare (sr);
    mod.setWave (2);
    mod.setFreqHz (f);

    // Only k=1 (1000 Hz) and k=3 (3000 Hz) survive the 0.45*sr=3600 Hz cutoff;
    // k=5 (5000 Hz) and above are dropped.
    for (int i = 0; i < 100; ++i)
    {
        const double ph = std::fmod (static_cast<double> (i) * (f / sr), 1.0);
        const double expected = (std::sin (2.0 * kPi * ph) + (1.0 / 3.0) * std::sin (2.0 * kPi * 3.0 * ph))
                                 / (1.0 + 1.0 / 3.0);
        const float m = mod.tick();
        REQUIRE (std::abs (static_cast<double> (m) - expected) < 1e-5);
    }
}
