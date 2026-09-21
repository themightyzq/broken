#pragma once
// Shared oscillator wave functions, phase-domain (ph in [0,1)).
// Bell/odd ratios trace to docs/DSP-NOTES.md §1.3 — the Modulator's documented diet.

#include <cmath>

namespace ts::dsp::waves
{
inline constexpr double twoPi = 6.283185307179586476925286766559;

inline float sine (double ph)     { return (float) std::sin (twoPi * ph); }
inline float tri (double ph)      { return (float) (4.0 * std::abs (ph - std::floor (ph + 0.5)) - 1.0); }
inline float saw (double ph)      { return (float) (2.0 * (ph - std::floor (ph + 0.5))); } // naive: aliasing is era-correct
inline float square (double ph)   { return ph < 0.5 ? 1.0f : -1.0f; }

// inharmonic bar/bell modal ratios; normalized by the amplitude sum so |out| <= 1
inline float bell (double ph)
{
    constexpr double rho[] { 1.0, 2.76, 5.40, 8.93 };
    constexpr double amp[] { 1.0, 0.6, 0.35, 0.2 };
    double s = 0.0;
    for (int k = 0; k < 4; ++k)
        s += amp[k] * std::sin (twoPi * rho[k] * ph);
    return (float) (s / 2.15);
}

// odd harmonics 1..9; partials above maxRatio*fundamental are dropped by the caller's
// choice of maxHarmonic (band-limit decision lives with whoever knows f and sr)
inline float odd (double ph, int maxHarmonic = 9)
{
    double s = 0.0, norm = 0.0;
    for (int k = 1; k <= maxHarmonic; k += 2)
    {
        s += std::sin (twoPi * k * ph) / k;
        norm += 1.0 / k;
    }
    return norm > 0.0 ? (float) (s / norm) : 0.0f;
}

inline float byIndex (int wave, double ph, int oddMaxHarmonic = 9)
{
    switch (wave)
    {
        case 0: return sine (ph);
        case 1: return tri (ph);
        case 2: return saw (ph);
        case 3: return square (ph);
        case 4: return bell (ph);
        case 5: return odd (ph, oddMaxHarmonic);
        default: return 0.0f;
    }
}
} // namespace ts::dsp::waves
