#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include "dsp/SourceEngine.h"

// exact float equality where bit-for-bit identity IS the contract; std::equal_to
// keeps -Wfloat-equal quiet without weakening the check (still ==, not a tolerance)
#include <functional>
static bool exactlyEqual (float a, float b) { return std::equal_to<float>{} (a, b); }

// Ramp buffer (data[i] = i) at the root note makes the read position directly
// observable in the output — every region/loop assertion below is an exact
// position-sequence check, not a tolerance smear.
//
// v0.5 control mapping: rev is an independent direction toggle; loopOn off = one-shot;
// loopStyle = Loop (wrap) | PingPong (bounce).

using broken::dsp::SourceEngine;

namespace
{
struct Rig
{
    SourceEngine src;
    std::vector<float> data;

    explicit Rig (size_t n = 1001)
    {
        data.resize (n);
        for (size_t i = 0; i < n; ++i) data[i] = (float) i;
        src.prepare (48000.0);
        src.setSampleData (data.data(), data.size(), 48000.0);
        src.setMode (SourceEngine::Sample);
        src.setLoopXfadeMs (0.0f); // exact sequences need raw seams; xfade tested separately
    }

    float tick() { return src.processSample (0.0f); }
};

float maxSecondDiff (Rig& r, int n)
{
    float prev2 = r.tick(), prev1 = r.tick(), worst = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float y = r.tick();
        worst = std::max (worst, std::abs (y - 2.0f * prev1 + prev2));
        prev2 = prev1; prev1 = y;
    }
    return worst;
}
} // namespace

// region [0.2, 0.3] of 1001 samples -> rs = 200, re = 300, L = 100

TEST_CASE ("one-shot plays region then silence", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.2f, 0.3f);
    r.src.setLoopOn (false);
    r.src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i <= 100; ++i)
        REQUIRE (r.tick() == Catch::Approx ((float) (200 + i)).margin (1e-3));
    REQUIRE (r.tick() == 0.0f);
}

TEST_CASE ("loop wraps end to start", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.2f, 0.3f);
    r.src.setLoopOn (true);
    r.src.setLoopStyle (SourceEngine::StyleLoop);
    r.src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i <= 100; ++i) r.tick(); // 200..300
    REQUIRE (r.tick() == Catch::Approx (201.0f).margin (1e-3)); // 301 wraps -> 201
}

TEST_CASE ("rev one-shot plays backward then silence", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.2f, 0.3f);
    r.src.setLoopOn (false);
    r.src.setReverse (true);
    r.src.noteOn (SourceEngine::rootNote, 1);
    REQUIRE (r.tick() == Catch::Approx (300.0f).margin (1e-3)); // rev starts at region end
    for (int i = 1; i <= 100; ++i)
        REQUIRE (r.tick() == Catch::Approx ((float) (300 - i)).margin (1e-3));
    REQUIRE (r.tick() == 0.0f);
}

TEST_CASE ("rev loop wraps start to end", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.2f, 0.3f);
    r.src.setLoopOn (true);
    r.src.setLoopStyle (SourceEngine::StyleLoop);
    r.src.setReverse (true);
    r.src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i <= 100; ++i) r.tick(); // 300..200
    REQUIRE (r.tick() == Catch::Approx (299.0f).margin (1e-3)); // 199 wraps -> 299
}

TEST_CASE ("pingpong reflects at both bounds (raw, xfade 0)", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.2f, 0.3f);
    r.src.setLoopOn (true);
    r.src.setLoopStyle (SourceEngine::StylePingPong);
    r.src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i < 100; ++i) r.tick();                      // 200..299
    REQUIRE (r.tick() == Catch::Approx (300.0f).margin (1e-3));  // top turnaround, once
    REQUIRE (r.tick() == Catch::Approx (299.0f).margin (1e-3));
    for (int i = 0; i < 98; ++i) r.tick();                       // 298..201
    REQUIRE (r.tick() == Catch::Approx (200.0f).margin (1e-3));  // bottom turnaround
    REQUIRE (r.tick() == Catch::Approx (201.0f).margin (1e-3));
}

TEST_CASE ("rev pingpong starts backward", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.2f, 0.3f);
    r.src.setLoopOn (true);
    r.src.setLoopStyle (SourceEngine::StylePingPong);
    r.src.setReverse (true);
    r.src.noteOn (SourceEngine::rootNote, 1);
    REQUIRE (r.tick() == Catch::Approx (300.0f).margin (1e-3));
    REQUIRE (r.tick() == Catch::Approx (299.0f).margin (1e-3)); // descending first
}

TEST_CASE ("extreme rate in a tiny region stays in bounds", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.5f, 0.5001f); // sub-minimum -> clamps to 64 samples
    r.src.setLoopOn (true);
    r.src.setLoopStyle (SourceEngine::StylePingPong);
    r.src.setTranspose (48.0f);      // rate 16x
    r.src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i < 10000; ++i)
    {
        const float y = r.tick();
        REQUIRE (std::isfinite (y));
        REQUIRE (y >= 499.0f);
        REQUIRE (y <= 566.0f);
    }
}

TEST_CASE ("live region shrink never reads out of bounds", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.0f, 1.0f);
    r.src.setLoopOn (true);
    r.src.setLoopStyle (SourceEngine::StyleLoop);
    r.src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i < 100; ++i) r.tick();
    r.src.setRegion (0.9f, 0.95f);
    const float y = r.tick();
    REQUIRE (y >= 900.0f);
    REQUIRE (y <= 950.0f);
}

TEST_CASE ("start >= end falls back to the full file", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.8f, 0.2f);
    r.src.setLoopOn (false);
    r.src.noteOn (SourceEngine::rootNote, 1);
    REQUIRE (r.tick() == Catch::Approx (0.0f).margin (1e-3));
    REQUIRE (r.tick() == Catch::Approx (1.0f).margin (1e-3));
}

TEST_CASE ("CYCLE window maps inside the region", "[sourceengine]")
{
    Rig r;
    r.src.setMode (SourceEngine::Cycle);
    r.src.setRegion (0.2f, 0.3f);
    r.src.setWindow (0.0f, 64.0f, 0.0f);
    r.src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i < 5000; ++i)
    {
        const float y = r.tick();
        REQUIRE (y >= 200.0f);
        REQUIRE (y <= 265.0f);
    }
}

TEST_CASE ("internal loop crossfade removes the wrap discontinuity", "[sourceengine]")
{
    auto runSeam = [] (float xfadeMs, float rs, float re)
    {
        Rig r;
        r.src.setRegion (rs, re);
        r.src.setLoopOn (true);
        r.src.setLoopStyle (SourceEngine::StyleLoop);
        r.src.setLoopXfadeMs (xfadeMs);
        r.src.noteOn (SourceEngine::rootNote, 1);
        float prev = r.tick(), maxStep = 0.0f;
        for (int i = 0; i < 400; ++i)
        {
            const float y = r.tick();
            maxStep = std::max (maxStep, std::abs (y - prev));
            prev = y;
        }
        return maxStep;
    };
    REQUIRE (runSeam (0.0f, 0.2f, 0.3f) > 90.0f);            // raw: the era click
    REQUIRE (runSeam (25.0f / 48.0f, 0.2f, 0.3f) < 5.0f);    // internal fade: smooth
    // the v0.4 external-material fade silently did NOTHING for a region at the file
    // head; the internal crossfade must work there identically
    REQUIRE (runSeam (25.0f / 48.0f, 0.0f, 0.1f) < 5.0f);
}

TEST_CASE ("pingpong turnaround blend smooths the slope corner", "[sourceengine]")
{
    auto runTurnaround = [] (float xfadeMs)
    {
        Rig r;
        r.src.setRegion (0.2f, 0.3f);
        r.src.setLoopOn (true);
        r.src.setLoopStyle (SourceEngine::StylePingPong);
        r.src.setLoopXfadeMs (xfadeMs);
        r.src.noteOn (SourceEngine::rootNote, 1);
        return maxSecondDiff (r, 400); // covers several turnarounds
    };
    // raw reflection: slope flips +1 -> -1 in one sample => second difference ~2
    REQUIRE (runTurnaround (0.0f) > 1.5f);
    // raised-cosine blend: C1 at window edges and boundary => curvature stays small
    REQUIRE (runTurnaround (50.0f / 48.0f) < 0.5f);
}

// v0.17 regression: a finished one-shot used to swallow every live sample-editor edit.
// With PLAY latched and LOOP off the head parks at the region end and returns 0 forever,
// so dragging a region marker changed nothing until the user hit STOP/PLAY. The ramp
// buffer makes the re-armed read position directly observable.
TEST_CASE ("a live region edit re-arms a finished one-shot", "[sourceengine]")
{
    Rig r;
    r.src.setRegion (0.2f, 0.3f);   // rs = 200, re = 300
    r.src.setLoopOn (false);
    r.src.noteOn (SourceEngine::rootNote, 1);

    for (int i = 0; i < 400; ++i) r.tick();   // run well past the region end
    REQUIRE (r.tick() == 0.0f);               // finished

    SECTION ("re-applying the SAME values leaves it finished")
    {
        r.src.setRegion (0.2f, 0.3f);
        r.src.setLoopOn (false);
        r.src.rearmIfEdited();
        REQUIRE (r.tick() == 0.0f);
    }

    SECTION ("moving the region rewinds into the NEW region")
    {
        r.src.setRegion (0.6f, 0.7f);         // rs = 600, re = 700
        r.src.rearmIfEdited();
        const float first = r.tick();
        REQUIRE (first >= 600.0f);
        REQUIRE (first < 700.0f);
    }

    SECTION ("turning LOOP on revives it too")
    {
        r.src.setLoopOn (true);
        r.src.rearmIfEdited();
        REQUIRE (r.tick() > 0.0f);
    }

    SECTION ("REV rewinds to the region end")
    {
        r.src.setReverse (true);
        r.src.rearmIfEdited();
        REQUIRE (r.tick() > 290.0f);          // near re = 300, travelling down
    }
}

// v0.18 DRAW mode (docs/DSP-NOTES.md §1.3b): the 64 drawn points ARE the waveform,
// linearly interpolated into the oscillator table and read with no band-limiting.
TEST_CASE ("DRAW mode plays the drawn table", "[sourceengine]")
{
    auto runCycle = [] (const std::array<float, 128>& pts, int n)
    {
        SourceEngine src;
        src.prepare (48000.0);
        src.setMode (SourceEngine::Osc);
        src.setOscMode (2);                 // Draw
        src.setDrawPoints (pts.data());
        src.noteOn (SourceEngine::rootNote, 1);
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i) out[(size_t) i] = src.processSample (0.0f);
        return out;
    };

    SECTION ("a drawn square comes out as a square")
    {
        std::array<float, 128> pts {};
        for (int k = 0; k < 128; ++k) pts[(size_t) k] = k < 64 ? 1.0f : -1.0f;
        auto y = runCycle (pts, 4000);

        int high = 0, low = 0, mid = 0;
        for (auto v : y)
        {
            if (v > 0.9f) ++high; else if (v < -0.9f) ++low; else ++mid;
        }
        // a hard square spends almost all its time at the rails; only the interpolated
        // edges land in between
        REQUIRE (high > 1500);
        REQUIRE (low > 1500);
        REQUIRE (mid < 400);
    }

    SECTION ("a quiet drawing stays quiet - normalization only scales DOWN")
    {
        std::array<float, 128> pts {};
        for (int k = 0; k < 128; ++k)
            pts[(size_t) k] = 0.25f * (float) std::sin (6.283185307179586 * k / 128.0);
        auto y = runCycle (pts, 4000);
        float peak = 0.0f;
        for (auto v : y) peak = std::max (peak, std::abs (v));
        REQUIRE (peak < 0.30f);
        REQUIRE (peak > 0.20f);
    }

    SECTION ("the table never exceeds the +-1 convention")
    {
        std::array<float, 128> pts {};
        for (int k = 0; k < 128; ++k) pts[(size_t) k] = (k % 2 == 0) ? 1.0f : -1.0f;
        auto y = runCycle (pts, 4000);
        for (auto v : y) REQUIRE (std::abs (v) <= 1.0f);
    }

    SECTION ("switching Harmonic -> Draw rebuilds the shared table")
    {
        SourceEngine src;
        src.prepare (48000.0);
        src.setMode (SourceEngine::Osc);
        std::array<float, 64> harm {}; harm[0] = 100.0f;   // pure fundamental
        src.setHarmonics (harm.data());
        src.setOscMode (1);
        src.noteOn (SourceEngine::rootNote, 1);
        for (int i = 0; i < 500; ++i) src.processSample (0.0f);

        std::array<float, 128> pts {};
        for (int k = 0; k < 128; ++k) pts[(size_t) k] = k < 64 ? 1.0f : -1.0f;
        src.setDrawPoints (pts.data());
        src.setOscMode (2);

        // must now be reading the square, not the leftover sine
        int rails = 0;
        for (int i = 0; i < 2000; ++i)
            if (std::abs (src.processSample (0.0f)) > 0.9f) ++rails;
        REQUIRE (rails > 1500);
    }
}

// v0.19 draw-to-enter: the GUI seeds the 128 points from the cycle you were LOOKING at
// before flipping to Draw, so the shape survives the flip (draw on a saw, dent a saw).
// The seeding itself lives in the UI, but the half that matters — a seeded table playing
// back as the wave it came from — is testable here.
TEST_CASE ("a table seeded from a wave plays back as that wave", "[sourceengine]")
{
    constexpr int N = 128;

    // returns residual energy of (drawn - analytic) relative to the analytic wave, in dB
    auto seedAndCompare = [] (int waveIndex)
    {
        std::array<float, N> pts {};
        for (int k = 0; k < N; ++k)
            pts[(size_t) k] = broken::dsp::waves::byIndex (waveIndex, (double) k / (double) N, 9);

        SourceEngine drawn;
        drawn.prepare (48000.0);
        drawn.setMode (SourceEngine::Osc);
        drawn.setOscMode (2);
        drawn.setDrawPoints (pts.data());
        drawn.noteOn (SourceEngine::rootNote, 1);

        SourceEngine analytic;
        analytic.prepare (48000.0);
        analytic.setMode (SourceEngine::Osc);
        analytic.setOscMode (0);
        analytic.setOscWave (waveIndex);
        analytic.noteOn (SourceEngine::rootNote, 1);

        double err2 = 0.0, ref2 = 0.0;
        for (int i = 0; i < 4000; ++i)
        {
            const double a = drawn.processSample (0.0f);
            const double b = analytic.processSample (0.0f);
            err2 += (a - b) * (a - b);
            ref2 += b * b;
        }
        return 10.0 * std::log10 ((err2 / ref2) + 1.0e-30);
    };

    SECTION ("a continuous wave survives seeding almost exactly")
    {
        // Tri and Sine have no discontinuity, so the only error is the 128-point
        // interpolation residue. This is the real test of the seeding math.
        REQUIRE (seedAndCompare (0) < -40.0); // Sine
        REQUIRE (seedAndCompare (1) < -40.0); // Tri
    }

    SECTION ("a discontinuous wave is limited by the jump, not by seeding")
    {
        // A saw's instantaneous wrap CANNOT be represented by any point table: the table
        // must ramp across one point-interval. That alone predicts
        //   10*log10( (1/128)*(2^2/3) / (1/3) ) = -15.05 dB,
        // and Saw measures ~-14.8 dB. Asserting a tight bound here would be asserting
        // something false about tables, so this pins the KNOWN floor instead: close to
        // the analytic prediction, and nowhere near "a different waveform".
        const double saw = seedAndCompare (2);
        REQUIRE (saw < -12.0);
        REQUIRE (saw > -18.0);
    }
}
