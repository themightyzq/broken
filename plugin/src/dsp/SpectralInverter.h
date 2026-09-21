#pragma once

#include <algorithm>

namespace ts::dsp
{

// the spec's two inverter flavors, FFT-free:
//  Type A: ring-mod by Nyquist (+1/-1 sequence) mirrors the spectrum (f -> sr/2 - f).
//  Type B: ring-mod by sr/4 (the 1,0,-1,0 cosine sequence) makes TWO combined images —
//          f + sr/4 and sr/4 - f ("more upper midrange frequencies" per the spec).
class SpectralInverter
{
public:
    void prepare (double /*sampleRate*/)
    {
        reset();
    }

    void reset()
    {
        phase_ = 0; // so the first sample after reset gets +1 in both types
    }

    void setMix (float m) { mix_ = std::clamp (m, 0.0f, 1.0f); }
    void setType (int t)  { type_ = t == 1 ? 1 : 0; }

    float processSample (float x)
    {
        float carrier;
        if (type_ == 0)
            carrier = (phase_ & 1) == 0 ? 1.0f : -1.0f;
        else
        {
            static constexpr float seq[4] = { 1.0f, 0.0f, -1.0f, 0.0f };
            // x2 makes Type B +3 dB hotter than Type A (mean-square 2 vs 1; x√2 would
            // equalize). Kept deliberately: the spec describes Type B as "more upper
            // midrange", and changing it is a listening decision (v0.23).
            carrier = 2.0f * seq[phase_ & 3];
        }
        const float y = x * (1.0f - mix_) + mix_ * (carrier * x);
        phase_ = (phase_ + 1) & 3;
        return y;
    }

private:
    float mix_ { 0.0f };
    int type_ { 0 };
    int phase_ { 0 };
};

} // namespace ts::dsp
