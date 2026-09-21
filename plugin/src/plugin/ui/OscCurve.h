#pragma once
// The single answer to "what does one cycle of OSC look like", shared by the small SOURCE
// waveform display and the pop-out OscEditor so the two can never drift apart.
//
// Mirrors dsp/SourceEngine.h (rebuildDrawTable / rebuildHarmonicTable) and dsp/Waves.h.
// Keep in lockstep with them: the picture must match what you hear.

#include <array>
#include <atomic>
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Waves.h"
#include "../Params.h"

namespace ts::ui::osccurve
{
// Raw parameter taps, resolved once so building a curve never looks up strings.
struct Taps
{
    std::atomic<float>* mode = nullptr;   // osc.mode: 0 Wave, 1 Harmonic, 2 Draw
    std::atomic<float>* wave = nullptr;   // source.oscwave
    static constexpr int drawCount = params::drawPointCount;
    std::array<std::atomic<float>*, (size_t) drawCount> draw {};
    std::array<std::atomic<float>*, 64> harm {};

    void attach (juce::AudioProcessorValueTreeState& apvts)
    {
        mode = apvts.getRawParameterValue ("osc.mode");
        wave = apvts.getRawParameterValue ("source.oscwave");
        for (int k = 0; k < drawCount; ++k)
            draw[(size_t) k] = apvts.getRawParameterValue (params::drawPointId (k + 1));
        for (int k = 0; k < params::harmonicCount; ++k)
            harm[(size_t) k] = apvts.getRawParameterValue (params::harmonicId (k + 1));
    }
};

// Fills dst[0..n) with one cycle, peak-limited to +-1 exactly as the engine's table is.
inline void build (const Taps& t, float* dst, int n)
{
    const int m = t.mode != nullptr ? (int) t.mode->load() : 0;

    if (m == 2) // Draw: linear interpolation of the 64 points
    {
        for (int i = 0; i < n; ++i)
        {
            const double u  = (double) Taps::drawCount * (double) i / (double) n;
            const auto   k0 = (size_t) u;
            const auto   k1 = (k0 + 1) % (size_t) Taps::drawCount;
            const float  fr = (float) (u - (double) k0);
            const float  a  = t.draw[k0] != nullptr ? t.draw[k0]->load() : 0.0f;
            const float  b  = t.draw[k1] != nullptr ? t.draw[k1]->load() : 0.0f;
            dst[i] = a + fr * (b - a);
        }
    }
    else if (m == 1) // Harmonic: the additive sum
    {
        for (int i = 0; i < n; ++i) dst[i] = 0.0f;
        for (int k = 1; k <= 64; ++k)
        {
            const float a = (t.harm[(size_t) (k - 1)] != nullptr
                             ? t.harm[(size_t) (k - 1)]->load() : 0.0f) * 0.01f;
            if (a <= 0.0f) continue;
            for (int i = 0; i < n; ++i)
                dst[i] += a * (float) std::sin (6.283185307179586 * (double) k
                                                * (double) i / (double) n);
        }
    }
    else // Wave: the analytic set
    {
        const int w = t.wave != nullptr ? (int) t.wave->load() : 0;
        for (int i = 0; i < n; ++i)
            dst[i] = dsp::waves::byIndex (w, (double) i / (double) n, 9);
    }

    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = std::max (peak, std::abs (dst[i]));
    if (peak > 1.0f)
        for (int i = 0; i < n; ++i) dst[i] /= peak;
}
} // namespace ts::ui::osccurve
