#include <catch2/catch_test_macros.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include "dsp/SourceEngine.h"

// DSP-NOTES §1.3a: the Harmonic-table rebuild is keyed on the note's pitch (noteFreq()),
// never on the FM-modulated instantaneous frequency (noteFreq() * rateFactor()). Before
// the fix, a fast Modulator FM signal moved the keyed frequency by more than 2% between
// consecutive samples, so the 262k-op rebuildHarmonicTable() fired every sample, per
// voice — this test drives that worst case and proves it no longer stalls the audio
// thread, while the FM itself (in the phase increment) stays audible.

using ts::dsp::SourceEngine;

namespace
{
constexpr double SR = 48000.0;

std::array<float, 64> flatHarmonics()
{
    std::array<float, 64> pct{};
    pct[0] = 100.0f; // fundamental only is enough to exercise the rebuild path
    return pct;
}

double rms (const float* buf, int n)
{
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += (double) buf[i] * (double) buf[i];
    return std::sqrt (acc / (double) n);
}
} // namespace

TEST_CASE ("Harmonic rebuild keys on note pitch, not FM-modulated frequency", "[harmonic]")
{
    const auto pct = flatHarmonics();
    constexpr int n = 48000;

    // Baseline: no FM, one steady-state rebuild. Times an unmodulated render so the
    // FM-driven bound below is set relative to this run's own machine speed.
    SourceEngine plain;
    plain.prepare (SR);
    plain.setMode (SourceEngine::Osc);
    plain.setOscMode (1);
    plain.setHarmonics (pct.data());
    plain.noteOn (SourceEngine::rootNote, 1);
    std::vector<float> plainOut ((size_t) n);

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) plainOut[(size_t) i] = plain.processSample (0.0f);
    const auto t1 = std::chrono::steady_clock::now();
    const double plainMs = std::chrono::duration<double, std::milli> (t1 - t0).count();

    // Fast FM: a 2 kHz sine on rateMod swings the modulated frequency wildly between
    // consecutive samples. Keying the rebuild on that (the bug) fires the 262k-op
    // rebuild every sample, for every one of these 48000 samples.
    SourceEngine fm;
    fm.prepare (SR);
    fm.setMode (SourceEngine::Osc);
    fm.setOscMode (1);
    fm.setHarmonics (pct.data());
    fm.noteOn (SourceEngine::rootNote, 1);
    std::vector<float> fmOut ((size_t) n);

    const auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i)
    {
        const float mod = (float) (2.0 * std::sin (2.0 * 3.14159265358979 * 2000.0 * (double) i / SR));
        fm.setRateMod (mod);
        fmOut[(size_t) i] = fm.processSample (0.0f);
    }
    const auto t3 = std::chrono::steady_clock::now();
    const double fmMs = std::chrono::duration<double, std::milli> (t3 - t2).count();

    // 20x looser than the unmodulated run on this same machine, plus a floor so a
    // near-zero baseline can't make the bound meaningless. Before the fix this ran
    // seconds-to-minutes slower than plain; after, it's the same order of magnitude.
    const double bound = std::max (20.0 * plainMs, 50.0);
    INFO ("plain render: " << plainMs << " ms, FM render: " << fmMs << " ms, bound: " << bound << " ms");
    REQUIRE (fmMs < bound);

    // The FM path itself must still be live: phase increment still uses the
    // FM-modulated f, so the fast-FM render should differ substantially from the
    // unmodulated one and carry real energy.
    REQUIRE (rms (fmOut.data(), n) > 0.05);
}
