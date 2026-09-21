#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ts::dsp
{

// Cascade of 1..4 identical one-pole low-passes. Deliberately uncompensated:
// the composite -3 dB point drops as poles stack (each stage attenuates the
// prior stage's output further at the nominal cutoff) -- that's the design,
// not a bug to be corrected here.
class FilterStack
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
        dirty = true;
    }

    void reset()
    {
        stages.fill (0.0f);
        flip = false;
    }

    void setCutoffHz (float hz)
    {
        cutoffHz = hz;
        dirty = true;
    }

    void setPoles (int n)
    {
        numPoles = std::clamp (n, 1, 4);
    }

    void setCutoffModSemitones (float st)
    {
        modSemitones = st;
        dirty = true;
    }

    void setFloorHz (float f)
    {
        floorHz = std::max (20.0f, f);
        dirty = true;
    }

    float processSample (float x)
    {
        if (dirty)
            updateCoefficient();

        // Alternate the nudge sign each sample so denormal protection doesn't
        // accumulate into an audible DC bias.
        const float nudge = flip ? 1e-18f : -1e-18f;
        flip = ! flip;

        float y = x;
        for (int i = 0; i < numPoles; ++i)
        {
            float& s = stages[static_cast<std::size_t> (i)];
            s = s + a * (y - s) + nudge;
            y = s;
        }
        return y;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void updateCoefficient()
    {
        const double fcMod = static_cast<double> (cutoffHz) * std::pow (2.0, static_cast<double> (modSemitones) / 12.0);
        // floor arrives from the plugin layer: 500 Hz era-authentic unless EXT --
        // modulation must not cross a floor the original could not (DSP-NOTES §4, v0.23)
        const double fcClamped = std::clamp (fcMod, static_cast<double> (floorHz), 0.45 * sr);
        a = static_cast<float> (1.0 - std::exp (-2.0 * kPi * fcClamped / sr));
        dirty = false;
    }

    std::array<float, 4> stages { 0.0f, 0.0f, 0.0f, 0.0f };
    double sr = 48000.0;
    float cutoffHz = 1000.0f;
    float floorHz = 20.0f;
    float modSemitones = 0.0f;
    int numPoles = 1;
    float a = 0.0f;
    bool dirty = true;
    bool flip = false;
};

} // namespace ts::dsp
