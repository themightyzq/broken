#pragma once

#include <algorithm>
#include <cmath>

namespace ts::dsp
{

// Exponential-segment ADSR. Attack overshoots to 1.5x the target peak so the
// one-pole crosses the real peak at exactly the requested attack time
// (1.5*(1 - 1/3) == 1.0), giving a punchier attack than a plain asymptotic
// approach without needing a separate shaping curve.
class EnvelopeADSR
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dirty = true;
    }

    void reset()
    {
        level = 0.0f;
        peak = 0.0f;
        stage = Stage::Idle;
        gateIsOn = false;
        flip = false;
    }

    void setTimes (float aSec, float dSec, float sus, float rSec)
    {
        attackSec = std::clamp (aSec, 0.001f, 10.0f);
        decaySec = std::clamp (dSec, 0.001f, 10.0f);
        sustainLevel = std::clamp (sus, 0.0f, 1.0f);
        releaseSec = std::clamp (rSec, 0.001f, 10.0f);
        dirty = true;
    }

    void gateOn (float velocity01)
    {
        // Restart the attack from the CURRENT level (not zero) so retriggering
        // mid-envelope never produces an audible click/jump.
        peak = std::clamp (velocity01, 0.0f, 1.0f);
        stage = Stage::Attack;
        gateIsOn = true;
    }

    void gateOff()
    {
        gateIsOn = false;
        stage = Stage::Release;
        // Release always targets 0 from whatever level we're currently at,
        // so no separate "L0" needs to be stored -- the recurrence uses
        // `level` directly each sample.
    }

    bool isGateOn() const { return gateIsOn; }
    bool isActive() const
    {
        // With sustain effectively 0, DecaySustain recurses toward 0 forever
        // (the gate never releases it), so a held voice that has already
        // decayed past -120dB is done making sound -- report it inactive so
        // the engine stops rendering silence for it. A sustain above the
        // floor means the voice is meant to hold audibly forever, so it
        // always stays active while gated.
        if (gateIsOn)
            return ! (sustainLevel < 1.0e-6f && level < 1.0e-6f);

        return level >= 1e-4f;
    }

    float processSample()
    {
        if (dirty)
            updateCoefficients();

        // Alternate the nudge sign each sample so denormal protection doesn't
        // accumulate into an audible DC bias (same idiom as FilterStack/Resonator).
        const float dn = flip ? 1.0e-18f : -1.0e-18f;
        flip = ! flip;

        switch (stage)
        {
            case Stage::Attack:
            {
                // A retrigger whose new peak sits BELOW the current level (poly voice
                // stealing always retriggers, and a quiet note can steal a loud voice)
                // used to satisfy `level >= peak` on the first sample and snap the level
                // down in one step — an audible click. Glide down through DecaySustain
                // instead; only a genuine rise gets clamped to peak. (v0.12)
                if (level >= peak)
                {
                    stage = Stage::DecaySustain;
                    const float target = sustainLevel * peak;
                    level = target + (level - target) * decayPole + dn;
                    break;
                }
                const float target = 1.5f * peak;
                level = target + (level - target) * attackPole;
                if (level >= peak)
                {
                    level = peak;
                    stage = Stage::DecaySustain;
                }
                break;
            }
            case Stage::DecaySustain:
            {
                const float target = sustainLevel * peak;
                level = target + (level - target) * decayPole + dn;
                break;
            }
            case Stage::Release:
            {
                level = level * releasePole + dn;
                break;
            }
            case Stage::Idle:
            default:
                break;
        }

        return level;
    }

private:
    enum class Stage
    {
        Idle,
        Attack,
        DecaySustain,
        Release
    };

    void updateCoefficients()
    {
        const double tauA = static_cast<double> (attackSec) / std::log (3.0);
        const double tauD = static_cast<double> (decaySec) / 3.0;
        const double tauR = static_cast<double> (releaseSec) / std::log (10.0);

        attackPole = static_cast<float> (std::exp (-1.0 / (tauA * sr)));
        decayPole = static_cast<float> (std::exp (-1.0 / (tauD * sr)));
        releasePole = static_cast<float> (std::exp (-1.0 / (tauR * sr)));

        dirty = false;
    }

    double sr = 48000.0;

    float attackSec = 0.01f;
    float decaySec = 0.1f;
    float sustainLevel = 0.7f;
    float releaseSec = 0.2f;

    float attackPole = 0.0f;
    float decayPole = 0.0f;
    float releasePole = 0.0f;

    float level = 0.0f;
    float peak = 0.0f;
    Stage stage = Stage::Idle;
    bool gateIsOn = false;
    bool dirty = true;
    bool flip = false;
};

} // namespace ts::dsp
