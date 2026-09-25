#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace broken::dsp
{

// Broken "death vocal" module: an internal low-range oscillator multiplied
// against the incoming signal (AM/RM). FM is applied by the caller (it drives
// the sound source's own rate), so this class only exposes the raw oscillator
// sample for that purpose.
class Modulator
{
public:
    void prepare (double sampleRate)
    {
        sr_ = sampleRate;
        pmBuf_.assign ((size_t) std::ceil (0.021 * sampleRate), 0.0f); // 10 ms swing + headroom
        reset();
    }

    void reset()
    {
        phase_ = 0.0f;
        std::fill (pmBuf_.begin(), pmBuf_.end(), 0.0f);
        pmW_ = 0;
    }

    void setFreqHz (float f) { freqHz_ = std::clamp (f, 0.1f, 2000.0f); }
    void setWave (int w) { wave_ = std::clamp (w, 0, 2); }
    void setAmount (float d) { amount_ = std::clamp (d, 0.0f, 1.0f); }

    // Uses the current phase, then advances it (increment f/sr, wrapped into [0,1)).
    float tick()
    {
        const float m = sample (phase_);

        const float next = phase_ + static_cast<float> (freqHz_ / sr_);
        phase_ = next - std::floor (next);

        return m;
    }

    float applyAM (float x, float m) const
    {
        return x * (1.0f - amount_ + amount_ * (m + 1.0f) * 0.5f);
    }

    float applyRM (float x, float m) const
    {
        return x * ((1.0f - amount_) + amount_ * m);
    }

    // PM: modulated short delay (docs/DSP-NOTES.md §2a) — the classic chorus/vibrato
    // recipe. Delay swings 0..10 ms with the modulator; amount scales the swing.
    float applyPM (float x, float m)
    {
        const int n = (int) pmBuf_.size();
        if (n < 4) return x;

        pmBuf_[(size_t) pmW_] = x;

        // m is CLAMPED to the modulator's defined +-1 range (DSP-NOTES §2): the Self /
        // Sample / Tape sources can deliver a hotter signal, which made `delay` negative
        // and walked the read index off the buffer (ASan container-overflow, v0.12).
        const float mc = m < -1.0f ? -1.0f : (m > 1.0f ? 1.0f : m);
        const double d0 = 0.010 * sr_;
        double delay = 1.0 + (double) amount_ * (double) (mc + 1.0f) * 0.5 * d0;
        // belt and braces: whatever the maths above ever produces, the read stays inside
        if (! (delay >= 1.0)) delay = 1.0;                 // also catches NaN
        if (delay > (double) (n - 2)) delay = (double) (n - 2);

        double pos = (double) pmW_ - delay;
        while (pos < 0.0) pos += (double) n;
        while (pos >= (double) n) pos -= (double) n;

        const auto i0 = (size_t) pos;
        const auto i1 = (i0 + 1) % (size_t) n;
        const float frac = (float) (pos - (double) i0);
        pmW_ = (pmW_ + 1) % n;
        return pmBuf_[i0] + frac * (pmBuf_[i1] - pmBuf_[i0]);
    }

private:
    static constexpr float kTwoPi = 6.28318530717958647692f;

    float sample (float ph) const
    {
        switch (wave_)
        {
            case 1: return bell (ph);
            case 2: return odd (ph);
            default: return std::sin (kTwoPi * ph);
        }
    }

    // Inharmonic bar/bell partials: fixed ratios and weights, normalized by
    // sum(a) so |m| <= 1 regardless of phase.
    float bell (float ph) const
    {
        static constexpr float rho[4] = { 1.0f, 2.76f, 5.40f, 8.93f };
        static constexpr float a[4] = { 1.0f, 0.6f, 0.35f, 0.2f };
        static constexpr float sumA = 2.15f; // 1 + 0.6 + 0.35 + 0.2

        float m = 0.0f;
        for (int k = 0; k < 4; ++k)
            m += a[k] * std::sin (kTwoPi * rho[k] * ph);
        return m / sumA;
    }

    // Odd harmonics 1..9, band-limited against Nyquist headroom (0.45*sr) and
    // renormalized by the weights actually used so the result stays in [-1,1].
    float odd (float ph) const
    {
        static constexpr int ks[5] = { 1, 3, 5, 7, 9 };
        const float nyquistLimit = 0.45f * static_cast<float> (sr_);

        float m = 0.0f;
        float sumInv = 0.0f;
        for (int k : ks)
        {
            if (static_cast<float> (k) * freqHz_ > nyquistLimit)
                continue;
            const float inv = 1.0f / static_cast<float> (k);
            m += inv * std::sin (kTwoPi * static_cast<float> (k) * ph);
            sumInv += inv;
        }
        return sumInv > 0.0f ? m / sumInv : 0.0f;
    }

    double sr_ { 48000.0 };
    float freqHz_ { 220.0f };
    int wave_ { 0 };
    float amount_ { 0.0f };
    float phase_ { 0.0f };
    std::vector<float> pmBuf_;
    int pmW_ { 0 };
};

} // namespace broken::dsp
