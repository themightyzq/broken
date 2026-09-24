#pragma once
// Envelope Removal, realtime take (docs/DSP-NOTES.md §17). The original was a
// destructive Sample-module utility; ours runs live so the sample path stays a path.
// "Similar to heavy compression" per the spec: divide by the tracked envelope.

#include <algorithm>
#include <cmath>

namespace broken::dsp
{
class Flatten
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void reset() { env = 0.0f; }

    void setResponseMs (float ms)
    {
        const float clamped = std::clamp (ms, 1.0f, 500.0f);
        if (std::abs (clamped - responseMs) < 1.0e-6f) return; // skip the exp() per block
        responseMs = clamped;
        // RELEASE uses the user's Response time; ATTACK is fixed-fast (1 ms). With a
        // single symmetric coefficient the envelope lagged a sudden burst, so the full
        // makeup landed before it caught up (+34 dBFS transient, v0.12 fuzz).
        coeff = 1.0f - std::exp (-1.0f / (float) (0.001 * (double) clamped * sr));
        attackCoeff = 1.0f - std::exp (-1.0f / (float) (0.001 * sr));
    }

    float processSample (float x)
    {
        const float mag = std::abs (x);
        env += (mag > env ? attackCoeff : coeff) * (mag - env);

        // Level toward a TARGET amplitude, not to unity: dividing straight by the
        // envelope let a spiky source reach +37 dBFS (v0.12 fuzz). +40 dB of makeup is
        // still allowed — a real decaying note spans more than the +24 dB first tried.
        float gain = std::min (maxGain, targetLevel / std::max (env, targetLevel / maxGain));
        // ...and below the gate the makeup fades back toward unity, so silence and
        // noise floors are never inflated
        if (env < gateLevel)
            gain = 1.0f + (gain - 1.0f) * (env / gateLevel);

        // A leveler has an output ceiling. Soft-limit above 4x the target so a transient
        // the envelope hasn't caught yet can't leave at +30 dBFS; below the knee this is
        // exactly transparent, so normal material is untouched.
        const float y = x * gain;
        const float knee = 4.0f * targetLevel;
        if (std::abs (y) <= knee) return y;
        const float over = std::abs (y) - knee;
        const float limited = knee + knee * std::tanh (over / knee);
        return y < 0.0f ? -limited : limited;
    }

private:
    static constexpr float maxGain = 100.0f;    // +40 dB
    static constexpr float gateLevel = 0.001f;  // -60 dBFS
    static constexpr float targetLevel = 0.25f; // the level it levels TO

    double sr = 48000.0;
    float responseMs = 0.0f;
    float coeff = 0.0004f;
    float attackCoeff = 0.02f;
    float env = 0.0f;
};
} // namespace broken::dsp
