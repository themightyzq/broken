#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "dsp/Resonator.h"

using broken::dsp::Resonator;

namespace
{

// Peak lag (within [minLag, maxLag]) of the unnormalized autocorrelation of
// samples[start, end) — used to recover the ring period without an FFT.
int findPeakLag (const std::vector<float> &samples, int start, int end, int minLag, int maxLag)
{
    int bestLag = minLag;
    double bestCorr = -1.0e300;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int i = start; i < end; ++i)
            sum += static_cast<double> (samples[static_cast<size_t> (i)])
                 * static_cast<double> (samples[static_cast<size_t> (i - lag)]);
        if (sum > bestCorr)
        {
            bestCorr = sum;
            bestLag = lag;
        }
    }
    return bestLag;
}

std::vector<float> renderImpulseResponse (Resonator &r, int numSamples)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    out[0] = r.processSample (1.0f);
    for (int i = 1; i < numSamples; ++i)
        out[static_cast<size_t> (i)] = r.processSample (0.0f);
    return out;
}

} // namespace

TEST_CASE ("Resonator rings at the tuned frequency", "[resonator]")
{
    Resonator r;
    r.prepare (48000.0);
    r.setFreqHz (220.0f);
    r.setFeedback (0.9f);
    r.setDampHz (15000.0f);

    auto out = renderImpulseResponse (r, 48000);

    int lag = findPeakLag (out, 4800, 43200, 100, 300);
    REQUIRE (lag >= 216);
    REQUIRE (lag <= 220);
}

TEST_CASE ("Resonator decays with the expected t60", "[resonator]")
{
    const double sr = 48000.0;
    Resonator r;
    r.prepare (sr);
    r.setFreqHz (220.0f);
    r.setFeedback (0.9f);
    r.setDampHz (15000.0f);

    auto out = renderImpulseResponse (r, 48000);

    const int windowSize = static_cast<int> (0.01 * sr); // 10 ms
    std::vector<double> windowRms;
    for (size_t start = 0; start + static_cast<size_t> (windowSize) <= out.size(); start += static_cast<size_t> (windowSize))
    {
        double sumSq = 0.0;
        for (size_t i = start; i < start + static_cast<size_t> (windowSize); ++i)
            sumSq += static_cast<double> (out[i]) * static_cast<double> (out[i]);
        windowRms.push_back (std::sqrt (sumSq / windowSize));
    }

    double maxRms = 0.0;
    size_t maxIdx = 0;
    for (size_t i = 0; i < windowRms.size(); ++i)
    {
        if (windowRms[i] > maxRms)
        {
            maxRms = windowRms[i];
            maxIdx = i;
        }
    }

    const double threshold = maxRms * std::pow (10.0, -60.0 / 20.0); // -60 dB below the peak window
    size_t dropIdx = windowRms.size();
    for (size_t i = maxIdx; i < windowRms.size(); ++i)
    {
        if (windowRms[i] < threshold)
        {
            dropIdx = i;
            break;
        }
    }
    REQUIRE (dropIdx < windowRms.size());

    const double tDrop = static_cast<double> (dropIdx * static_cast<size_t> (windowSize)) / sr;

    const double delaySamples = sr / 220.0;
    const double t60 = (delaySamples / sr) * 3.0 / (-std::log10 (0.9));

    REQUIRE (std::abs (tDrop - t60) <= 0.30 * t60);
}

TEST_CASE ("Resonator remains stable at near-unity feedback", "[resonator]")
{
    Resonator r;
    r.prepare (48000.0);
    r.setFreqHz (220.0f);
    r.setFeedback (0.995f);
    r.setDampHz (15000.0f);

    std::srand (12345);
    for (int i = 0; i < 1000; ++i)
    {
        float x = (static_cast<float> (std::rand()) / static_cast<float> (RAND_MAX)) * 2.0f - 1.0f;
        r.processSample (x);
    }

    float last = 0.0f;
    for (int i = 0; i < 100000; ++i)
    {
        last = r.processSample (0.0f);
        if ((i + 1) % 10000 == 0)
            REQUIRE (std::isfinite (last));
    }

    REQUIRE (std::abs (last) < 1.0f);
}

TEST_CASE ("Resonator with negative feedback rings at double the period", "[resonator]")
{
    // For y[n] = x[n] + fb*y[n-D] with fb < 0, the poles sit at odd multiples
    // of sr/(2D) instead of sr/D (z^D = fb < 0 needs an extra half-turn of
    // phase per period), so the ring period *doubles* to 2D, not halves to
    // D/2. Verified empirically: for D = 48000/220 = 218.18, autocorrelation
    // peaks at lag ~437 (~2D), never near D/2 = 109.
    Resonator r;
    r.prepare (48000.0);
    r.setFreqHz (220.0f);
    r.setFeedback (-0.9f);
    r.setDampHz (15000.0f);

    auto out = renderImpulseResponse (r, 48000);

    int lag = findPeakLag (out, 4800, 43200, 350, 500);
    REQUIRE (lag >= 433);
    REQUIRE (lag <= 440);
}

TEST_CASE ("Resonator reset silences the ring", "[resonator]")
{
    Resonator r;
    r.prepare (48000.0);
    r.setFreqHz (220.0f);
    r.setFeedback (0.9f);
    r.setDampHz (15000.0f);

    r.processSample (1.0f);
    for (int i = 0; i < 1000; ++i)
        r.processSample (0.0f);

    r.reset();

    // Outputs settle at the denormal-guard floor (~1e-18), not bit-exact
    // zero, so the check uses a tolerance far below any audible level.
    for (int i = 0; i < 100; ++i)
        REQUIRE (std::abs (r.processSample (0.0f)) < 1.0e-6f);
}
