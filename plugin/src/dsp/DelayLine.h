#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ts::dsp
{

// Simple era delay: coarse+fine time, mix, polarity, optional feedback.
class DelayLine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // Ceil covers the 2000 ms max time clamp with margin.
        bufferSize = static_cast<int> (std::ceil (2.1 * sampleRate));
        buffer.assign (static_cast<size_t> (bufferSize), 0.0f);
        writePos = 0;
        updateDelay();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void setTimeMs (float ms)
    {
        timeMs = std::clamp (ms, 0.1f, 2000.0f);
        updateDelay();
    }

    void setMix (float m)
    {
        mix = std::clamp (m, 0.0f, 1.0f);
    }

    void setInvert (bool inv)
    {
        invert = inv;
    }

    void setFeedback (float fb)
    {
        feedback = std::clamp (fb, 0.0f, 0.9f);
    }

    float processSample (float x)
    {
        float readPos = static_cast<float> (writePos) - delaySamples;
        while (readPos < 0.0f)
            readPos += static_cast<float> (bufferSize);

        int i0 = static_cast<int> (readPos);
        float frac = readPos - static_cast<float> (i0);
        int idx0 = i0 % bufferSize;
        int idx1 = (idx0 + 1) % bufferSize;
        float d = buffer[static_cast<size_t> (idx0)] * (1.0f - frac) + buffer[static_cast<size_t> (idx1)] * frac;

        // Alternating sign avoids a steady DC bias from the denormal guard.
        float w = x + feedback * d;
        w += (writePos & 1) ? 1e-18f : -1e-18f;
        buffer[static_cast<size_t> (writePos)] = w;
        writePos = (writePos + 1) % bufferSize;

        float wet = invert ? -d : d;
        return (1.0f - mix) * x + mix * wet;
    }

private:
    void updateDelay()
    {
        if (sr > 0.0)
            delaySamples = static_cast<float> (static_cast<double> (timeMs) * 0.001 * sr);
    }

    double sr = 44100.0;
    std::vector<float> buffer;
    int bufferSize = 0;
    int writePos = 0;

    float timeMs = 100.0f;
    float mix = 0.5f;
    bool invert = false;
    float feedback = 0.0f;
    float delaySamples = 4410.0f;
};

} // namespace ts::dsp
