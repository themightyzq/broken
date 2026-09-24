#include <catch2/catch_test_macros.hpp>

#include "dsp/EnvelopeADSR.h"

#include <cmath>

TEST_CASE ("attack crosses peak within ~5ms for a 5ms attack time", "[envelope]")
{
    broken::dsp::EnvelopeADSR env;
    const double sr = 48000.0;
    env.prepare (sr);
    env.setTimes (0.005f, 0.1f, 0.7f, 0.2f);
    env.gateOn (1.0f);

    int samplesToReach = -1;
    for (int n = 1; n <= 1000; ++n)
    {
        const float y = env.processSample();
        if (y >= 0.99f)
        {
            samplesToReach = n;
            break;
        }
    }

    REQUIRE (samplesToReach >= 216);
    REQUIRE (samplesToReach <= 264);
}

TEST_CASE ("decay settles near the sustain level after the decay time", "[envelope]")
{
    broken::dsp::EnvelopeADSR env;
    const double sr = 48000.0;
    env.prepare (sr);
    env.setTimes (0.01f, 0.2f, 0.5f, 0.2f);
    env.gateOn (1.0f);

    int attackEndSample = -1;
    float levelAtTarget = 0.0f;
    const int dSamples = static_cast<int> (0.2 * sr);

    for (int n = 0; n < 200000; ++n)
    {
        const float y = env.processSample();
        if (attackEndSample < 0 && y >= 0.999f)
            attackEndSample = n;

        if (attackEndSample >= 0 && n == attackEndSample + dSamples)
        {
            levelAtTarget = y;
            break;
        }
    }

    REQUIRE (attackEndSample >= 0);
    REQUIRE (levelAtTarget >= 0.45f);
    REQUIRE (levelAtTarget <= 0.55f);
}

TEST_CASE ("release crosses 10% of its start level at the release time", "[envelope]")
{
    broken::dsp::EnvelopeADSR env;
    const double sr = 48000.0;
    env.prepare (sr);
    env.setTimes (0.01f, 0.05f, 0.5f, 0.15f);
    env.gateOn (1.0f);

    // Run long enough for attack + decay to fully settle at the sustain level
    // before releasing, so the release start level is a known ~0.5.
    for (int n = 0; n < static_cast<int> (1.0 * sr); ++n)
        env.processSample();

    env.gateOff();

    int samplesToThreshold = -1;
    for (int n = 1; n <= static_cast<int> (1.0 * sr); ++n)
    {
        const float y = env.processSample();
        if (y <= 0.05f)
        {
            samplesToThreshold = n;
            break;
        }
    }

    const int expected = static_cast<int> (0.15 * sr);
    REQUIRE (samplesToThreshold >= static_cast<int> (expected * 0.9));
    REQUIRE (samplesToThreshold <= static_cast<int> (expected * 1.1));
}

TEST_CASE ("retriggering mid-attack does not jump to zero", "[envelope]")
{
    broken::dsp::EnvelopeADSR env;
    env.prepare (48000.0);
    env.setTimes (0.05f, 0.1f, 0.7f, 0.2f); // slow attack so 100 samples is still mid-attack
    env.gateOn (1.0f);

    float before = 0.0f;
    for (int n = 0; n < 100; ++n)
        before = env.processSample();

    env.gateOn (0.8f);
    const float after = env.processSample();

    REQUIRE (std::abs (after - before) < 0.05f);
}

TEST_CASE ("isActive goes false after release settles, level always finite", "[envelope]")
{
    broken::dsp::EnvelopeADSR env;
    const double sr = 48000.0;
    env.prepare (sr);
    env.setTimes (0.01f, 0.05f, 0.5f, 0.1f);
    env.gateOn (1.0f);

    REQUIRE (env.isActive());

    for (int n = 0; n < static_cast<int> (0.5 * sr); ++n)
    {
        const float y = env.processSample();
        REQUIRE (std::isfinite (y));
    }

    env.gateOff();

    // Several release time-constants plus margin -- release tau is rSec/ln(10),
    // so 2s at rSec=0.1s is ~20 taus, comfortably below the 1e-4 floor.
    const int releaseMarginSamples = static_cast<int> (2.0 * sr);
    for (int n = 0; n < releaseMarginSamples; ++n)
    {
        const float y = env.processSample();
        REQUIRE (std::isfinite (y));
    }

    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("sustain 0 held: level does not go subnormal and voice sleeps", "[envelope]")
{
    broken::dsp::EnvelopeADSR env;
    const double sr = 48000.0;
    env.prepare (sr);
    env.setTimes (0.001f, 0.05f, 0.0f, 0.1f);
    env.gateOn (1.0f);

    bool activeInFirst10ms = false;
    const int tenMsSamples = static_cast<int> (0.01 * sr);
    const int totalSamples = static_cast<int> (10.0 * sr);

    for (int n = 0; n < totalSamples; ++n)
    {
        const float y = env.processSample();

        // Never a float subnormal -- the denormal nudge must keep the
        // recursion off the floating-point floor even after the level has
        // decayed well past audibility, with sustain = 0 and the gate never
        // releasing it.
        REQUIRE (std::fpclassify (y) != FP_SUBNORMAL);
        REQUIRE ((y == 0.0f || std::abs (y) >= 1e-30f));

        if (n < tenMsSamples && env.isActive())
            activeInFirst10ms = true;
    }

    // The note must still start: it has to report active at some point
    // during the attack, well before it decays into silence.
    REQUIRE (activeInFirst10ms);

    // Gate is still held, but sustain = 0 means the voice has decayed past
    // -120dB with nothing left to sustain -- the engine should stop
    // rendering silence for it.
    REQUIRE_FALSE (env.isActive());
}

TEST_CASE ("sustain > 0 held stays active", "[envelope]")
{
    broken::dsp::EnvelopeADSR env;
    const double sr = 48000.0;
    env.prepare (sr);
    env.setTimes (0.001f, 0.05f, 0.5f, 0.1f);
    env.gateOn (1.0f);

    const int totalSamples = static_cast<int> (2.0 * sr);
    for (int n = 0; n < totalSamples; ++n)
    {
        env.processSample();
        REQUIRE (env.isActive());
    }
}
