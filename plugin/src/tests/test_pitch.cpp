#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include <string>
#include "dsp/PitchDetector.h"
#include "dsp/TapeShift.h"
#include <functional>

// Exact float equality where bit-for-bit identity IS the contract (passthrough, hold,
// determinism, snapshot). std::equal_to keeps -Wfloat-equal out of it without weakening
// the check: this is still ==, not a tolerance.
static bool exactlyEqual (float a, float b) { return std::equal_to<float>{} (a, b); }

using broken::dsp::PitchDetector;
using broken::dsp::TapeShift;

namespace
{
constexpr double SR = 48000.0;

std::vector<float> sine (double hz, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * hz * i / SR);
    return v;
}

std::vector<float> saw (double hz, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double ph = hz * i / SR;
        v[(size_t) i] = amp * (float) (2.0 * (ph - std::floor (ph + 0.5)));
    }
    return v;
}

float detectHz (const std::vector<float>& x, float* clarityOut = nullptr)
{
    PitchDetector d;
    d.prepare (SR);
    auto r = d.detect (x.data());
    if (clarityOut) *clarityOut = r.clarity;
    return r.hz;
}

float centsError (float hz, double expected)
{
    return 1200.0f * std::log2 (hz / (float) expected);
}
} // namespace

TEST_CASE ("detector: 440 sine within half a Hz, high clarity", "[pitch]")
{
    float clarity = 0.0f;
    const float hz = detectHz (sine (440.0, PitchDetector::windowSize), &clarity);
    REQUIRE (std::abs (hz - 440.0f) < 0.5f);
    REQUIRE (clarity > 0.9f);
}

TEST_CASE ("detector: E2 sawtooth within 3 cents", "[pitch]")
{
    const float hz = detectHz (saw (82.41, PitchDetector::windowSize));
    REQUIRE (std::abs (centsError (hz, 82.41)) < 3.0f);
}

TEST_CASE ("detector: noise has low clarity", "[pitch]")
{
    std::vector<float> v ((size_t) PitchDetector::windowSize);
    uint32_t rng = 12345;
    for (auto& s : v)
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        s = (float) rng / 4294967295.0f * 2.0f - 1.0f;
    }
    float clarity = 1.0f;
    (void) detectHz (v, &clarity);
    REQUIRE (clarity < PitchDetector::clarityGate);
}

TEST_CASE ("detector: silence and sub-range input stay finite", "[pitch]")
{
    std::vector<float> z ((size_t) PitchDetector::windowSize, 0.0f);
    float clarity = 1.0f;
    const float hz = detectHz (z, &clarity);
    REQUIRE (std::isfinite (hz));
    REQUIRE (exactlyEqual (clarity, 0.0f));
    const float hz30 = detectHz (sine (30.0, PitchDetector::windowSize));
    REQUIRE (std::isfinite (hz30)); // 30 Hz is below range; must not NaN/crash
}

TEST_CASE ("note math: hz -> nearest note + cents (C4 = 60)", "[pitch]")
{
    int note = 0; float cents = 0.0f;
    PitchDetector::centsFromHz (440.0f, note, cents);
    REQUIRE (note == 69);
    REQUIRE (std::abs (cents) < 0.01f);
    REQUIRE (std::string (PitchDetector::noteName (note)) == "A");
    REQUIRE (PitchDetector::octaveOf (60) == 4); // middle C = C4, manual Appendix D

    // 143.2 Hz: nearest equal-tempered note is D3 (146.83 Hz), 43.4 cents flat
    PitchDetector::centsFromHz (143.2f, note, cents);
    REQUIRE (std::string (PitchDetector::noteName (note)) == "D");
    REQUIRE (PitchDetector::octaveOf (note) == 3);
    REQUIRE (cents == Catch::Approx (-43.4f).margin (1.0f));
}

TEST_CASE ("tapeshift: unity is bit-exact passthrough", "[pitch]")
{
    TapeShift t;
    t.prepare (SR);
    t.setSemitones (0.0f);
    auto in = sine (440.0, 4800);
    for (auto s : in)
        REQUIRE (exactlyEqual (t.processSample (s), s));
}

TEST_CASE ("tapeshift: +12 st reads an octave up", "[pitch]")
{
    TapeShift t;
    t.prepare (SR);
    t.setSemitones (12.0f);
    auto in = sine (440.0, 48000);
    std::vector<float> out;
    out.reserve (in.size());
    for (auto s : in) out.push_back (t.processSample (s));
    // skip the fill-in transient, detect on a late window
    std::vector<float> win (out.end() - PitchDetector::windowSize, out.end());
    const float hz = detectHz (win);
    REQUIRE (std::abs (hz - 880.0f) < 880.0f * 0.02f);
}

TEST_CASE ("tapeshift: +2 st lands on 493.9 Hz", "[pitch]")
{
    TapeShift t;
    t.prepare (SR);
    t.setSemitones (2.0f);
    auto in = sine (440.0, 48000);
    std::vector<float> out;
    for (auto s : in) out.push_back (t.processSample (s));
    std::vector<float> win (out.end() - PitchDetector::windowSize, out.end());
    const float hz = detectHz (win);
    REQUIRE (std::abs (hz - 493.88f) < 493.88f * 0.02f);
}

TEST_CASE ("tapeshift: stays finite over long runs at extremes", "[pitch]")
{
    for (float st : { -48.0f, 48.0f })
    {
        TapeShift t;
        t.prepare (SR);
        t.setSemitones (st);
        double acc = 0.0;
        for (int i = 0; i < 1000000; ++i)
        {
            const float y = t.processSample ((float) std::sin (0.06 * i));
            REQUIRE (std::isfinite (y));
            acc += std::abs (y);
        }
        REQUIRE (acc > 0.0);
    }
}
