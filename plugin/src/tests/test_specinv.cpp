#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "dsp/SpectralInverter.h"

using ts::dsp::SpectralInverter;

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

TEST_CASE ("mix=1 mirrors the spectrum to sr/2 - f", "[specinv]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;

    SpectralInverter inv;
    inv.prepare (sr);
    inv.setMix (1.0f);

    std::vector<float> y (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto x = static_cast<float> (std::sin (2.0 * kPi * 1000.0 * static_cast<double> (i) / sr));
        y[static_cast<size_t> (i)] = inv.processSample (x);
    }

    const double p1000 = goertzelPower (y, 1000.0, sr);
    const double p23000 = goertzelPower (y, 23000.0, sr);
    REQUIRE (p23000 >= 1000.0 * p1000);
}

TEST_CASE ("mix=0 is an exact passthrough", "[specinv]")
{
    constexpr double sr = 48000.0;

    SpectralInverter inv;
    inv.prepare (sr);
    inv.setMix (0.0f);

    for (int i = 0; i < 1000; ++i)
    {
        const auto x = static_cast<float> (std::sin (2.0 * kPi * 1000.0 * static_cast<double> (i) / sr));
        const float y = inv.processSample (x);
        REQUIRE (std::abs (y - x) < 1e-7f);
    }
}

TEST_CASE ("mix=0.5 balances the original and mirrored tones", "[specinv]")
{
    constexpr double sr = 48000.0;
    constexpr int n = 48000;

    SpectralInverter inv;
    inv.prepare (sr);
    inv.setMix (0.5f);

    std::vector<float> y (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto x = static_cast<float> (std::sin (2.0 * kPi * 1000.0 * static_cast<double> (i) / sr));
        y[static_cast<size_t> (i)] = inv.processSample (x);
    }

    const double p1000 = goertzelPower (y, 1000.0, sr);
    const double p23000 = goertzelPower (y, 23000.0, sr);
    const double ratio = p1000 / p23000;

    REQUIRE (ratio >= 0.79);
    REQUIRE (ratio <= 1.26);
}
