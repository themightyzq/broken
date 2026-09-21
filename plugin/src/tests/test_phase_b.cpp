#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include <algorithm>
#include "dsp/SourceEngine.h"
#include "dsp/SpectralInverter.h"
#include "dsp/PitchDetector.h"

// Phase B (v0.8): era Noise model, SpecInv Type B, equal-power loop crossfade.

using ts::dsp::SourceEngine;
using ts::dsp::SpectralInverter;
using ts::dsp::PitchDetector;

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

TEST_CASE ("era noise at 0/0 is a tunable sine", "[phaseB]")
{
    SourceEngine src;
    src.prepare (SR);
    src.setMode (SourceEngine::Noise);
    src.setNoiseModel (0.0f, 0.0f);
    src.noteOn (69 - 12, 1); // A3 in our root scheme -> 220 Hz
    std::vector<float> out ((size_t) PitchDetector::windowSize);
    for (auto& s : out) s = src.processSample (0.0f);
    PitchDetector det;
    det.prepare (SR);
    const auto r = det.detect (out.data());
    REQUIRE (r.clarity > 0.9f);
    REQUIRE (std::abs (1200.0f * std::log2 (r.hz / 220.0f)) < 5.0f);
}

TEST_CASE ("era noise at 100/100 is broadband", "[phaseB]")
{
    SourceEngine src;
    src.prepare (SR);
    src.setMode (SourceEngine::Noise);
    src.setNoiseModel (100.0f, 100.0f);
    src.noteOn (SourceEngine::rootNote, 1);
    std::vector<float> out ((size_t) PitchDetector::windowSize);
    for (auto& s : out) s = src.processSample (0.0f);
    PitchDetector det;
    det.prepare (SR);
    REQUIRE (det.detect (out.data()).clarity < PitchDetector::clarityGate);
}

TEST_CASE ("SpecInv Type B makes both quarter-rate images", "[phaseB]")
{
    SpectralInverter inv;
    inv.prepare (SR);
    inv.setType (1);
    inv.setMix (1.0f);
    std::vector<float> out;
    for (int i = 0; i < 48000; ++i)
        out.push_back (inv.processSample (0.5f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / SR)));
    const double up = goertzel (out, 13000.0);   // sr/4 + f
    const double down = goertzel (out, 11000.0); // sr/4 - f
    const double orig = goertzel (out, 1000.0);
    REQUIRE (up > orig * 10.0);
    REQUIRE (down > orig * 10.0);
}

TEST_CASE ("equal-power crossfade avoids the -3 dB seam power dip", "[phaseB]")
{
    // uncorrelated material (noise buffer): a linear blend dips to half power mid-fade,
    // equal-power holds it — compare mean power inside the fade window
    auto run = [] (int shape)
    {
        std::vector<float> buf (48000);
        uint32_t rng = 777;
        for (auto& s : buf)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            s = (float) rng / 4294967295.0f * 2.0f - 1.0f;
        }
        SourceEngine src;
        src.prepare (SR);
        src.setSampleData (buf.data(), buf.size(), SR);
        src.setRegion (0.2f, 0.4f);
        src.setLoopOn (true);
        src.setLoopStyle (SourceEngine::StyleLoop);
        src.setLoopXfadeMs (20.0f); // 960 smp fade in a 9600 smp region
        src.setXfadeShape (shape);
        src.noteOn (SourceEngine::rootNote, 1);
        std::vector<float> out;
        for (int i = 0; i < 96000; ++i) out.push_back (src.processSample (0.0f));

        // sliding 200-sample RMS over the steady region: a linear fade of uncorrelated
        // material dips ~-3 dB power mid-seam, equal-power holds level — compare the
        // MINIMUM window power to the median
        std::vector<double> pw;
        for (size_t i = 20000; i + 200 < out.size(); i += 100)
        {
            double p = 0.0;
            for (size_t k = 0; k < 200; ++k) p += (double) out[i + k] * out[i + k];
            pw.push_back (p / 200.0);
        }
        std::vector<double> sorted = pw;
        std::sort (sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        return sorted.front() / median; // min/median power ratio
    };
    const double linDip = run (0);
    const double eqDip = run (1);
    REQUIRE (linDip < 0.72);            // linear seam dips toward half power
    REQUIRE (eqDip > linDip * 1.2);     // eq-power holds the seam meaningfully better
}
