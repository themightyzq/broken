#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ts::dsp
{

// Drive -> saturate -> morph-blend with dry -> trim, with an optional DC blocker
// for curves that are asymmetric (or otherwise DC-producing) once driven.
class Waveshaper
{
public:
    void prepare (double sampleRate)
    {
        // R sets the blocker's high-pass corner at ~8 Hz regardless of sample rate.
        r_ = 1.0f - static_cast<float> (2.0 * 3.14159265358979323846 * 8.0 / sampleRate);
        reset();
    }

    void reset()
    {
        blockerXPrev_ = 0.0f;
        blockerYPrev_ = 0.0f;
    }

    // manual-verified extras: 8 = Random (seeded breakpoint curve — the original's
    // randomizer preset), 9 = Custom (the pencil-drawn curve, customPointCount breakpoints)
    void setRandomSeed (int seed)
    {
        if (seed == seed_) return;
        seed_ = seed;
        uint32_t s = (uint32_t) seed * 2654435761u + 0x9E3779B9u;
        auto next = [&s]
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) ((double) s / 4294967295.0) * 2.0f - 1.0f;
        };
        for (auto& p : randPts_) p = next();
    }

    static constexpr int customPointCount = 128;   // DSP-NOTES §3.1
    void setCustomPoints (const float* pts)   // must point at customPointCount floats
    {
        for (int i = 0; i < customPointCount; ++i)
            custPts_[(size_t) i] = std::clamp (pts[i], -1.0f, 1.0f);
    }

    void setCurve (int c)
    {
        curve_ = std::clamp (c, 0, 9);
        // Linear must be a bit-exact passthrough, so switching to it drops any
        // residual blocker state rather than letting it bleed into later output.
        if (curve_ == 0)
            reset();
    }

    void setDriveDb (float db) { driveDb_ = std::clamp (db, 0.0f, 40.0f); }
    void setMorph (float m) { morph_ = std::clamp (m, 0.0f, 1.0f); }
    void setTrimDb (float db) { trimDb_ = std::clamp (db, -24.0f, 24.0f); }

    float processSample (float x)
    {
        const float g = std::pow (10.0f, driveDb_ / 20.0f);
        const float u = std::clamp (x * g, -1.0f, 1.0f);
        const float shaped = shape (u);
        const float trimGain = std::pow (10.0f, trimDb_ / 20.0f);
        const float y = trimGain * ((1.0f - morph_) * u + morph_ * shaped);

        if (curve_ == 0)
            return y;

        // Bias the recursive state away from zero so it can't decay into denormal
        // range (costly on some CPUs), then remove the bias from the returned sample.
        float yb = y - blockerXPrev_ + r_ * blockerYPrev_;
        yb += 1.0e-18f;
        blockerXPrev_ = y;
        blockerYPrev_ = yb;
        return yb - 1.0e-18f;
    }

private:
    float shape (float u) const
    {
        switch (curve_)
        {
            case 0: return u;
            case 1: return std::clamp (1.5f * u, -1.0f, 1.0f);
            case 2: return 1.5f * u - 0.5f * u * u * u;
            case 3:
            {
                const float v = 2.0f * u;
                const float t = v + 1.0f;
                const float fmod4 = t - 4.0f * std::floor (t / 4.0f);
                return 1.0f - std::abs (fmod4 - 2.0f);
            }
            case 4: return u >= 0.0f ? 1.0f - (1.0f - u) * (1.0f - u) : 0.6f * u;
            case 5: return std::round (u * 8.0f) / 8.0f;
            case 6:
                return std::sin ((3.14159265358979323846f / 2.0f) * u
                                  * (1.0f + 2.0f * (driveDb_ / 40.0f)));
            case 7: return -u * (2.0f - std::abs (u));
            case 8: return breakpointEval (randPts_.data(), 12, u);
            case 9: return breakpointEval (custPts_.data(), customPointCount, u);
            default: return u;
        }
    }

    // cosine-smooth interpolation across N evenly-spaced breakpoints over u in [-1,1]
    static float breakpointEval (const float* pts, int n, float u)
    {
        const float t = (u + 1.0f) * 0.5f * (float) (n - 1);
        const int i0 = std::clamp ((int) t, 0, n - 2);
        const float frac = t - (float) i0;
        const float w = 0.5f * (1.0f - std::cos (3.14159265f * frac));
        return pts[i0] + w * (pts[i0 + 1] - pts[i0]);
    }

    int curve_ { 0 };
    int seed_ { 0 };
    std::array<float, 12> randPts_ {};
    std::array<float, (size_t) customPointCount> custPts_ {};
    float driveDb_ { 0.0f };
    float morph_ { 0.0f };
    float trimDb_ { 0.0f };

    float r_ { 0.0f };
    float blockerXPrev_ { 0.0f };
    float blockerYPrev_ { 0.0f };
};

} // namespace ts::dsp
