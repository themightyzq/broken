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
// died out. (Mirrors the helper in test_filterstack.cpp -- kept local since
// tests are separate translation units.)
float steadyStatePeak (broken::dsp::FilterStack& f, double sr, double freq, int totalSamples, int tailSamples)
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

// docs/DSP-NOTES.md §4: fc_eff is pitch-shifted by envmod*env (up to +-60 st) and then
// "clamped to the same floor as the static cutoff: 500 Hz unless EXT, then 20 Hz". Before
// v0.23 the floor was only applied to the static knob value in the plugin layer, and the
// DSP's own clamp used a hardcoded 20 Hz -- so with EXT off, a big negative envelope swing
// could pull fc_eff down through the 500 Hz floor period hardware could never cross.

TEST_CASE ("floor holds the cutoff up when envelope modulation pulls it down (EXT off)", "[filter]")
{
    broken::dsp::FilterStack f;
    const double sr = 48000.0;
    f.prepare (sr);
    f.setPoles (1);
    f.setCutoffHz (500.0f);
    f.setFloorHz (500.0f); // EXT off: authentic 500 Hz floor
    f.setCutoffModSemitones (-60.0f); // most negative envmod*env the panel allows

    // Without the floor, 500 Hz * 2^(-60/12) = ~15.6 Hz, which would bury a 100 Hz
    // tone. With the floor holding fc_eff at 500 Hz, 100 Hz sits well in the passband.
    const float peak = steadyStatePeak (f, sr, 100.0, 48000, 4800);
    const float dB = 20.0f * std::log10 (peak);

    REQUIRE (dB > -1.5f);
}

TEST_CASE ("EXT lets the same modulation pull the cutoff down near the 20 Hz floor", "[filter]")
{
    broken::dsp::FilterStack f;
    const double sr = 48000.0;
    f.prepare (sr);
    f.setPoles (1);
    f.setCutoffHz (500.0f);
    f.setFloorHz (20.0f); // EXT on
    f.setCutoffModSemitones (-60.0f);

    // fc_eff wants to sit at ~15.6 Hz, clamped up only to the 20 Hz floor -- a 100 Hz
    // tone is now several octaves above the corner and should be heavily attenuated.
    const float peak = steadyStatePeak (f, sr, 100.0, 48000, 4800);
    const float dB = 20.0f * std::log10 (peak);

    REQUIRE (dB < -10.0f);
}
