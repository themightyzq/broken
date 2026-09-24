#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "dsp/DelayLine.h"

using broken::dsp::DelayLine;

TEST_CASE ("DelayLine echoes at the programmed time", "[delayline]")
{
    DelayLine d;
    d.prepare (48000.0);
    d.setTimeMs (80.0f); // 80 ms == 3840.0 samples exactly at 48 kHz
    d.setMix (0.5f);
    d.setFeedback (0.0f);

    std::vector<float> out (4801);
    out[0] = d.processSample (1.0f);
    for (size_t i = 1; i < out.size(); ++i)
        out[i] = d.processSample (0.0f);

    REQUIRE (std::abs (out[0] - 0.5f) < 1.0e-6f);
    REQUIRE (std::abs (out[3840] - 0.5f) < 1.0e-3f);

    for (size_t i = 0; i <= 4800; ++i)
    {
        if (i == 0 || i == 3840)
            continue;
        REQUIRE (std::abs (out[i]) < 1.0e-6f);
    }
}

TEST_CASE ("DelayLine invert flips the wet polarity", "[delayline]")
{
    DelayLine d;
    d.prepare (48000.0);
    d.setTimeMs (80.0f);
    d.setMix (0.5f);
    d.setFeedback (0.0f);
    d.setInvert (true);

    std::vector<float> out (3841);
    out[0] = d.processSample (1.0f);
    for (size_t i = 1; i < out.size(); ++i)
        out[i] = d.processSample (0.0f);

    REQUIRE (std::abs (out[3840] - (-0.5f)) < 1.0e-3f);
}

TEST_CASE ("DelayLine feedback repeats the echo with decay", "[delayline]")
{
    DelayLine d;
    d.prepare (48000.0);
    d.setTimeMs (80.0f);
    d.setMix (1.0f);
    d.setFeedback (0.5f);

    const int numSamples = 11521;
    std::vector<float> out (static_cast<size_t> (numSamples));
    out[0] = d.processSample (1.0f);
    for (int i = 1; i < numSamples; ++i)
        out[static_cast<size_t> (i)] = d.processSample (0.0f);

    // write[0] = 1.0; read@3840 = 1.0 -> y[3840] = 1.0; write[3840] = 0.5
    // read@7680 = 0.5 -> y[7680] = 0.5; write[7680] = 0.25
    // read@11520 = 0.25 -> y[11520] = 0.25
    REQUIRE (std::abs (out[3840] - 1.0f) < 1.0e-3f);
    REQUIRE (std::abs (out[7680] - 0.5f) < 1.0e-3f);
    REQUIRE (std::abs (out[11520] - 0.25f) < 1.0e-3f);
}

TEST_CASE ("DelayLine splits energy across neighboring samples for a fractional delay", "[delayline]")
{
    DelayLine d;
    d.prepare (48000.0);
    d.setTimeMs (80.01f); // 3840.48 samples
    d.setMix (0.5f);
    d.setFeedback (0.0f);

    std::vector<float> out (3843);
    out[0] = d.processSample (1.0f);
    for (size_t i = 1; i < out.size(); ++i)
        out[i] = d.processSample (0.0f);

    // Linear-interpolation taps sum to 1, so with mix 0.5 and dry input 0 at
    // both landing samples, out[3840] + out[3841] should land at mix (0.5).
    float sum = out[3840] + out[3841];
    REQUIRE (std::abs (sum - 0.5f) < 0.05f * 0.5f);
}

TEST_CASE ("DelayLine reset clears the buffer", "[delayline]")
{
    DelayLine d;
    d.prepare (48000.0);
    d.setTimeMs (80.0f);
    d.setMix (1.0f);
    d.setFeedback (0.0f);

    d.processSample (1.0f);
    for (int i = 0; i < 500; ++i)
        d.processSample (0.0f);

    d.reset();

    // If the buffer (and write position) were truly cleared, feeding zeros
    // from here on never turns up the old impulse as it would have on wrap.
    for (int i = 0; i < 5000; ++i)
        REQUIRE (std::abs (d.processSample (0.0f)) < 1.0e-6f);
}
