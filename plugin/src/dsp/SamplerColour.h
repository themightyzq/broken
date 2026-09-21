#pragma once

#include <algorithm>
#include <cmath>

namespace ts::dsp
{

// Era sampler's output medium: bit-depth quantize + zero-order sample-and-hold
// rate reduction. Deliberately has NO anti-alias filtering -- the aliasing is
// the point of this module.
class SamplerColour
{
public:
    void prepare (double sampleRate)
    {
        sr_ = sampleRate;
        reset();
    }

    void reset()
    {
        acc_ = 1.0; // forces a sample-and-hold on the very first processSample()
        held_ = 0.0f;
    }

    void setMode (int m) { mode_ = std::clamp (m, 0, 2); }
    void setRateHz (float r) { rateHz_ = std::clamp (r, 8000.0f, 48000.0f); }

    float processSample (float x)
    {
        if (mode_ == 0)
            return x; // Off bypasses both hold and quantize

        if (sr_ > 0.0)
            acc_ += static_cast<double> (rateHz_) / sr_;
        if (acc_ >= 1.0)
        {
            acc_ -= 1.0;
            held_ = quantize (x);
        }
        return held_;
    }

private:
    float quantize (float x) const
    {
        const float bits = (mode_ == 1) ? 12.0f : 8.0f;
        const float s = std::pow (2.0f, bits - 1.0f);
        return std::round (x * s) / s;
    }

    double sr_ { 48000.0 };
    float rateHz_ { 48000.0 };
    int mode_ { 0 };

    double acc_ { 1.0 };
    float held_ { 0.0f };
};

} // namespace ts::dsp
