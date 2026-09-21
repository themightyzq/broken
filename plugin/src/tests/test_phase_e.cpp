#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>
#include <vector>
#include "dsp/SourceEngine.h"
#include <functional>

// Exact float equality where bit-for-bit identity IS the contract (passthrough, hold,
// determinism, snapshot). std::equal_to keeps -Wfloat-equal out of it without weakening
// the check: this is still ==, not a tolerance.
static bool exactlyEqual (float a, float b) { return std::equal_to<float>{} (a, b); }

// Phase E (v0.11): Oscillator Harmonic Mode — 64 additive partials (manual's Harmonic
// Mode, minus the deferred waveform timeline).

using ts::dsp::SourceEngine;

namespace
{
constexpr double SR = 48000.0;

double goertzel (const std::vector<float>& x, double hz)
{
    const double w = 2.0 * 3.14159265358979 * hz / SR;
    const double c = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (float v : x) { s0 = (double) v + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

std::vector<float> renderHarmonic (const std::array<float, 64>& pct, int note, int n = 48000)
{
    SourceEngine src;
    src.prepare (SR);
    src.setMode (SourceEngine::Osc);
    src.setOscMode (1);
    src.setHarmonics (pct.data());
    src.noteOn (note, 1);
    std::vector<float> out;
    out.reserve ((size_t) n);
    for (int i = 0; i < n; ++i) out.push_back (src.processSample (0.0f));
    return out;
}
} // namespace

TEST_CASE ("harmonic mode with only the fundamental is a sine", "[phaseE]")
{
    std::array<float, 64> pct {};
    pct[0] = 100.0f;
    const auto out = renderHarmonic (pct, 57); // A3 -> 220 Hz
    const double f1 = goertzel (out, 220.0);
    REQUIRE (f1 > goertzel (out, 440.0) * 1000.0);
    REQUIRE (f1 > goertzel (out, 660.0) * 1000.0);
}

TEST_CASE ("saw recipe reproduces the 1/k harmonic series", "[phaseE]")
{
    std::array<float, 64> pct {};
    for (int k = 1; k <= 64; ++k) pct[(size_t) (k - 1)] = 100.0f / (float) k;
    const auto out = renderHarmonic (pct, 45); // A2 -> 110 Hz, room for many partials

    const double p1 = goertzel (out, 110.0);
    for (int k : { 2, 3, 4, 5 })
    {
        const double pk = goertzel (out, 110.0 * k);
        const double ratioDb = 10.0 * std::log10 (pk / p1);
        const double idealDb = 20.0 * std::log10 (1.0 / (double) k);
        REQUIRE (std::abs (ratioDb - idealDb) < 1.5); // within 1.5 dB of 1/k
    }
}

TEST_CASE ("harmonic mode band-limits against Nyquist", "[phaseE]")
{
    std::array<float, 64> pct {};
    pct[63] = 100.0f; // ONLY the 64th partial
    // at 1 kHz the 64th partial is 64 kHz — above 0.45*sr, so it must be dropped
    const auto high = renderHarmonic (pct, 83); // ~987 Hz
    double energy = 0.0;
    for (float v : high) energy += (double) v * v;
    REQUIRE (energy < 1e-6); // silence, not an aliased scream

    // at 110 Hz the 64th partial is 7040 Hz — in band, so it must sound
    const auto low = renderHarmonic (pct, 45);
    REQUIRE (goertzel (low, 7040.0) > 1.0);
}

TEST_CASE ("stacked partials stay inside +-1", "[phaseE]")
{
    std::array<float, 64> pct {};
    pct.fill (100.0f); // every partial at full — worst case for summing
    const auto out = renderHarmonic (pct, 45);
    for (float v : out)
    {
        REQUIRE (std::isfinite (v));
        REQUIRE (std::abs (v) <= 1.0001f);
    }
}

TEST_CASE ("wave mode is unaffected by harmonic settings", "[phaseE]")
{
    std::array<float, 64> pct {};
    pct.fill (100.0f);
    SourceEngine a, b;
    for (auto* s : { &a, &b })
    {
        s->prepare (SR);
        s->setMode (SourceEngine::Osc);
        s->setOscWave (0);
        s->setOscMode (0); // Wave mode
        s->noteOn (57, 1);
    }
    a.setHarmonics (pct.data()); // must not change Wave-mode output at all
    for (int i = 0; i < 20000; ++i)
        REQUIRE (exactlyEqual (a.processSample (0.0f), b.processSample (0.0f)));
}
