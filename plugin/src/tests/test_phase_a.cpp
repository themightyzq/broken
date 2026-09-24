#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include "dsp/Modulator.h"
#include "dsp/SourceEngine.h"
#include <functional>

// Exact float equality where bit-for-bit identity IS the contract (passthrough, hold,
// determinism, snapshot). std::equal_to keeps -Wfloat-equal out of it without weakening
// the check: this is still ==, not a tolerance.
static bool exactlyEqual (float a, float b) { return std::equal_to<float>{} (a, b); }

// Phase A (v0.7): PM mode, modulator sources (Self/Sample/Tape), Pitch MIX.
// Goertzel single-bin power for spectral assertions without an FFT library.

using broken::dsp::Modulator;
using broken::dsp::SourceEngine;

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
} // namespace

TEST_CASE ("PM on a sine produces vibrato sidebands", "[phaseA]")
{
    Modulator mod;
    mod.prepare (SR);
    mod.setFreqHz (30.0f);
    mod.setAmount (0.8f);
    std::vector<float> out;
    const int n = 48000;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / SR);
        out.push_back (mod.applyPM (x, mod.tick()));
    }
    std::vector<float> tail (out.begin() + 4800, out.end());
    // phase modulation spreads energy into carrier +- k*30 Hz Bessel lines
    const double carrier = goertzel (tail, 1000.0);
    const double sb1 = goertzel (tail, 1030.0), sb2 = goertzel (tail, 970.0);
    REQUIRE (sb1 > carrier * 1e-4);
    REQUIRE (sb2 > carrier * 1e-4);
    REQUIRE (std::isfinite ((float) carrier));
}

TEST_CASE ("self-RM squares: 2f line appears, fundamental drops", "[phaseA]")
{
    // emulate the Voice wiring: m = previous source sample, applyRM at full depth
    Modulator mod;
    mod.prepare (SR);
    mod.setAmount (1.0f);
    std::vector<float> out;
    float prev = 0.0f;
    const int n = 48000;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.9f * (float) std::sin (2.0 * 3.14159265358979 * 500.0 * i / SR);
        out.push_back (mod.applyRM (x, prev));
        prev = x;
    }
    const double f1 = goertzel (out, 500.0), f2 = goertzel (out, 1000.0);
    REQUIRE (f2 > f1 * 10.0); // squaring: energy lives at 2f (plus DC), not f
}

TEST_CASE ("sample-as-modulator reads the region as a wavetable at mod freq", "[phaseA]")
{
    SourceEngine src;
    src.prepare (SR);
    // single-cycle sine as the sample: wavetable read at 55 Hz must yield ~55 Hz
    std::vector<float> table (1024);
    for (size_t i = 0; i < table.size(); ++i)
        table[i] = (float) std::sin (2.0 * 3.14159265358979 * (double) i / 1024.0);
    src.setSampleData (table.data(), table.size(), SR);
    src.setRegion (0.0f, 1.0f);
    std::vector<float> m;
    for (int i = 0; i < 48000; ++i)
        m.push_back (src.tickModSource (false, 55.0));
    const double at55 = goertzel (m, 55.0), at110 = goertzel (m, 110.0);
    REQUIRE (at55 > at110 * 10.0);
}

TEST_CASE ("pitch mix blends pitched and dry heads", "[phaseA]")
{
    SourceEngine src;
    src.prepare (SR);
    // sample = 200 Hz sine, long enough to stay one-shot for the whole test
    std::vector<float> buf (96000);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 200.0 * (double) i / SR);
    src.setSampleData (buf.data(), buf.size(), SR);
    src.setRegion (0.0f, 1.0f);
    src.setTranspose (12.0f); // pitched head reads 400 Hz
    src.setPitchMix (0.5f);
    src.noteOn (SourceEngine::rootNote, 1);
    std::vector<float> out;
    for (int i = 0; i < 40000; ++i)
        out.push_back (src.processSample (0.0f));
    const double p200 = goertzel (out, 200.0), p400 = goertzel (out, 400.0);
    const double p300 = goertzel (out, 300.0);
    REQUIRE (p200 > p300 * 10.0); // dry line present
    REQUIRE (p400 > p300 * 10.0); // pitched line present
}

TEST_CASE ("pitch mix 1.0 leaves the pitched path untouched", "[phaseA]")
{
    SourceEngine a, b;
    std::vector<float> buf (48000);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 200.0 * (double) i / SR);
    for (auto* s : { &a, &b })
    {
        s->prepare (SR);
        s->setSampleData (buf.data(), buf.size(), SR);
        s->setTranspose (7.0f);
        s->noteOn (SourceEngine::rootNote, 1);
    }
    a.setPitchMix (1.0f);
    // b left at default (1.0) — outputs must be identical sample-for-sample
    for (int i = 0; i < 20000; ++i)
        REQUIRE (exactlyEqual (a.processSample (0.0f), b.processSample (0.0f)));
}
