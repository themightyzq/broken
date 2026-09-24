#pragma once
// Live-input varispeed (docs/DSP-NOTES.md §14): a varispeed read head chasing a
// live-written tape loop — the only physical form varispeed can take on realtime
// audio. Two read taps a half-window apart; each is retired and re-seated with a
// raised-cosine handoff before it can collide with the write head. The residual
// warble/pitch-flutter at hand-offs is the era's rotating-head artifact, kept.

#include <cmath>
#include <cstddef>
#include <vector>

namespace broken::dsp
{
class TapeShift
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // loop must hold the largest per-pass head drift: |1-ratio|*window = 15*100 ms
        // at +48 st, plus fade headroom -> 1.6 s covers the full EXT pitch range
        buf.assign ((size_t) std::ceil (1.6 * sampleRate), 0.0f);
        windowSamples = 0.1 * sampleRate;                          // 100 ms per tap pass
        fadeSamples = 0.01 * sampleRate;                           // 10 ms handoff
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
        tapPhase = 0.0;
        ratio = 1.0;
    }

    void setSemitones (float st)
    {
        semis = st;
        ratio = std::pow (2.0, (double) st / 12.0);
    }

    float processSample (float x)
    {
        // exact bypass at unity so null/unity gates stay bit-true
        if (std::abs (semis) < 0.01f)
        {
            buf[(size_t) w] = x;           // keep the tape warm for click-free engage
            w = (w + 1) % (int) buf.size();
            return x;
        }

        buf[(size_t) w] = x;

        // tapPhase runs 0..1 over one tap pass; each tap reads at a delay that ramps
        // with (1 - ratio), re-seated every pass. Tap B is half a pass behind tap A;
        // raised-cosine gains sum to 1 across the overlap.
        const double drift = (1.0 - ratio) * windowSamples; // total delay change per pass
        auto tapDelay = [this, drift] (double phase)
        {
            // start each pass at a base delay that keeps the whole ramp inside the tape:
            // base chosen mid-buffer; ramp = phase*drift (negative drift = head catching up)
            const double base = drift > 0.0 ? fadeSamples + 1.0
                                            : 1.0 + fadeSamples - drift; // stay ahead of write head
            return base + phase * drift;
        };
        auto readAt = [this] (double delay)
        {
            double pos = (double) w - delay;
            const double n = (double) buf.size();
            while (pos < 0.0) pos += n;
            while (pos >= n) pos -= n;
            const auto i0 = (size_t) pos;
            const auto i1 = (i0 + 1) % buf.size();
            const float frac = (float) (pos - (double) i0);
            return buf[i0] + frac * (buf[i1] - buf[i0]);
        };

        const double phaseA = tapPhase;
        const double phaseB = tapPhase + 0.5 >= 1.0 ? tapPhase - 0.5 : tapPhase + 0.5;

        // raised-cosine window per tap over its own pass (0 at pass edges, 1 mid-pass);
        // half-offset passes overlap so the gains cross-fade the head re-seats
        auto gain = [] (double phase)
        {
            return 0.5 * (1.0 - std::cos (6.283185307179586 * phase));
        };

        const float ya = readAt (tapDelay (phaseA));
        const float yb = readAt (tapDelay (phaseB));
        const double ga = gain (phaseA), gb = gain (phaseB);
        const float y = (float) ((ya * ga + yb * gb) / std::max (1e-9, ga + gb));

        tapPhase += 1.0 / windowSamples;
        if (tapPhase >= 1.0) tapPhase -= 1.0;
        w = (w + 1) % (int) buf.size();
        return y;
    }

private:
    double sr = 48000.0;
    std::vector<float> buf;
    int w = 0;
    double windowSamples = 4800.0;
    double fadeSamples = 480.0;
    double tapPhase = 0.0;
    double ratio = 1.0;
    float semis = 0.0f;
};
} // namespace broken::dsp
