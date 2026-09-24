#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace broken::dsp
{

// Tuned comb (feedback delay) — the Broken "ringing" module.
class Resonator
{
public:
    void prepare (double sampleRate)
    {
        // Sized from the ACTUAL rate: the lowest resonant frequency (20 Hz) needs
        // sr/20 samples, so a fixed 8192 silently mistuned the comb above ~164 kHz
        // (at 192 kHz, 20 Hz asked for 9600 and wrapped to 1408 -> ~136 Hz). v0.12.
        sr = sampleRate;
        bufferSize = std::max (kMinBufferSize, (int) std::ceil (sampleRate / 20.0) + 4);
        buffer.assign ((size_t) bufferSize, 0.0f);
        writePos = 0;
        dampState = 0.0f;
        updateDelay();
        updateDampCoeff();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        dampState = 0.0f;
    }

    void setFreqHz (float f)
    {
        freqHz = std::clamp (f, 20.0f, 2000.0f);
        updateDelay();
    }

    void setFeedback (float fb)
    {
        feedback = std::clamp (fb, -0.995f, 0.995f);
    }

    void setDampHz (float hz)
    {
        dampHz = std::clamp (hz, 500.0f, 15000.0f);
        updateDampCoeff();
    }

    float processSample (float x)
    {
        // Linear-interpolated read at (writePos - delaySamples).
        float readPos = static_cast<float> (writePos) - delaySamples;
        while (readPos < 0.0f)
            readPos += static_cast<float> (bufferSize);

        int i0 = static_cast<int> (readPos);
        float frac = readPos - static_cast<float> (i0);
        int idx0 = i0 % bufferSize;
        int idx1 = (idx0 + 1) % bufferSize;
        float r = buffer[static_cast<size_t> (idx0)] * (1.0f - frac) + buffer[static_cast<size_t> (idx1)] * frac;

        // Alternating sign avoids a steady DC bias from the denormal guard.
        float denormalGuard = (writePos & 1) ? 1e-18f : -1e-18f;

        dampState += dampCoeff * (r - dampState);
        dampState += denormalGuard;

        float rd = dampState;
        float y = x + feedback * rd;
        y += denormalGuard;

        buffer[static_cast<size_t> (writePos)] = y; // write-after-read gives the loop its unit delay
        writePos = (writePos + 1) % bufferSize;

        return y;
    }

private:
    void updateDelay()
    {
        if (sr > 0.0)
            delaySamples = static_cast<float> (sr / static_cast<double> (freqHz));
    }

    void updateDampCoeff()
    {
        if (sr > 0.0)
            dampCoeff = 1.0f - std::exp (static_cast<float> (-2.0 * kPi * static_cast<double> (dampHz) / sr));
    }

    static constexpr int kMinBufferSize = 8192; // floor; prepare() grows it with sr
    int bufferSize = kMinBufferSize;
    static constexpr double kPi = 3.14159265358979323846;

    double sr = 44100.0;
    std::vector<float> buffer;
    int writePos = 0;

    float freqHz = 220.0f;
    float feedback = 0.0f;
    float dampHz = 15000.0f;
    float dampCoeff = 1.0f;
    float delaySamples = 200.0f;
    float dampState = 0.0f;
};

} // namespace broken::dsp
