#pragma once
// Display-grade monophonic pitch detection (docs/DSP-NOTES.md §13): NSDF/McLeod-style
// normalized autocorrelation over a 4096-sample window, parabolic peak interpolation.
// Runs on the GUI thread over tap buffers — never in the audio path.

#include <cmath>
#include <cstddef>
#include <vector>

namespace broken::dsp
{
struct PitchResult
{
    float hz = 0.0f;
    float clarity = 0.0f; // NSDF peak height, 0..1; below ~0.6 means "don't trust it"
};

class PitchDetector
{
public:
    static constexpr int windowSize = 4096;
    static constexpr float clarityGate = 0.6f;

    void prepare (double sampleRate) { sr = sampleRate; nsdf.assign ((size_t) maxLag + 1, 0.0f); }

    // window must hold windowSize samples
    PitchResult detect (const float* x)
    {
        PitchResult out;
        // NSDF: n(tau) = 2*sum(x[i]*x[i+tau]) / sum(x[i]^2 + x[i+tau]^2)
        const int n = windowSize;
        double energy0 = 0.0;
        for (int i = 0; i < n; ++i) energy0 += (double) x[i] * x[i];
        if (energy0 < 1e-9) return out; // silence

        const int lo = (int) (sr / 2000.0); // 2 kHz ceiling
        const int hi = maxLag;              // ~35 Hz floor at 48 k (lag 1371 < 2048 ok)
        float best = 0.0f; int bestLag = 0;

        // running tail energy lets the normalization track the shrinking overlap
        for (int lag = lo; lag <= hi; ++lag)
        {
            double ac = 0.0, e1 = 0.0, e2 = 0.0;
            const int m = n - lag;
            for (int i = 0; i < m; ++i)
            {
                ac += (double) x[i] * x[i + lag];
                e1 += (double) x[i] * x[i];
                e2 += (double) x[i + lag] * x[i + lag];
            }
            nsdf[(size_t) lag] = (e1 + e2) > 0.0 ? (float) (2.0 * ac / (e1 + e2)) : 0.0f;
        }

        // pick the FIRST significant NSDF maximum (not the global one) — octave-error
        // avoidance per McLeod: find all positive-crossing local maxima, take the first
        // whose height >= k * overall max
        float overallMax = 0.0f;
        for (int lag = lo + 1; lag < hi; ++lag)
            overallMax = std::max (overallMax, nsdf[(size_t) lag]);
        const float thresh = 0.9f * overallMax;
        for (int lag = lo + 1; lag < hi; ++lag)
        {
            const float c = nsdf[(size_t) lag];
            if (c >= thresh && c >= nsdf[(size_t) (lag - 1)] && c >= nsdf[(size_t) (lag + 1)])
            { best = c; bestLag = lag; break; }
        }
        if (bestLag <= 0 || best <= 0.0f) return out;

        // parabolic interpolation around the chosen lag for sub-sample period accuracy
        const float ym1 = nsdf[(size_t) (bestLag - 1)];
        const float y0  = nsdf[(size_t) bestLag];
        const float yp1 = nsdf[(size_t) (bestLag + 1)];
        const float denom = ym1 - 2.0f * y0 + yp1;
        const float shift = std::abs (denom) > 1e-12f ? 0.5f * (ym1 - yp1) / denom : 0.0f;

        out.hz = (float) (sr / ((double) bestLag + (double) shift));
        out.clarity = best;
        return out;
    }

    // nearest equal-tempered note (A4=440, middle C = C4 = midi 60 by convention) and the
    // cents offset from it
    static void centsFromHz (float hz, int& midiNote, float& centsOffset)
    {
        const float noteF = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        midiNote = (int) std::lround (noteF);
        centsOffset = (noteF - (float) midiNote) * 100.0f;
    }

    static const char* noteName (int midiNote)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        return names[((midiNote % 12) + 12) % 12];
    }
    static int octaveOf (int midiNote) { return midiNote / 12 - 1; } // C4 = 60

private:
    static constexpr int maxLag = 1400; // sr/35 at 48 k; window/2 headroom preserved
    double sr = 48000.0;
    std::vector<float> nsdf;
};
} // namespace broken::dsp
