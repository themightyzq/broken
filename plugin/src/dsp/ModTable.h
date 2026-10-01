#pragma once
// Table mod source (DSP-NOTES §2a "Table"): the 4096-entry one-cycle table built from the
// OSCILLATOR panel, and the lock-free hand-off that gets it to the audio thread.
//
// Until 2026-10-01 (through v0.36.0) the table was rebuilt inside Engine::applyParams, i.e. on the audio thread
// (up to 64 x 4096 multiply-adds in one block whenever a harmonic bar moved). It is now
// built OFF the audio thread by ModTableBuilder into the back slot of a ModTableExchange
// (a wait-free triple buffer); the audio thread only ever swaps a pointer to the newest
// published slot. JUCE-free so the DSP unit tests can drive it directly.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include "Waves.h"

namespace broken::dsp
{
inline constexpr size_t modTableSize = 4096;

// What the table depends on: osc.mode, osc.wave, the 64 harmonic bars, the 128 drawn points.
struct ModTableShape
{
    int oscMode = 0;   // 0 Wave, 1 Harmonic, 2 Draw
    int oscWave = 0;
    std::array<float, 64>  harmonics {};
    std::array<float, 128> drawPts {};
};

// Single writer (any one non-audio thread at a time), single reader (the audio thread).
// Writer: fill backBuffer(), then publish(). Reader: acquire() returns the newest
// published table and keeps it exclusively until the next acquire(), so the writer can
// never be writing the slot the audio thread is reading. No locks, no allocation.
class ModTableExchange
{
public:
    float* backBuffer() { return slots[(size_t) back].data(); }

    void publish()
    {
        back = latest.exchange (back | freshBit, std::memory_order_acq_rel) & indexMask;
    }

    const float* acquire()
    {
        if ((latest.load (std::memory_order_acquire) & freshBit) != 0)
            front = latest.exchange (front, std::memory_order_acq_rel) & indexMask;
        return slots[(size_t) front].data();
    }

private:
    static constexpr int freshBit = 4, indexMask = 3;
    std::array<std::array<float, modTableSize>, 3> slots {};
    int front = 0;                 // reader-owned
    int back = 2;                  // writer-owned
    std::atomic<int> latest { 1 }; // the slot in between, plus freshBit when unread
};

// Rebuilds the table only when the shape actually changed (same tolerances as the
// old (through v0.36.0) Engine::rebuildModTableIfNeeded, so a change that used to rebuild still does).
// Not thread-safe: the owner serialises calls (BrokenProcessor guards it with a lock that
// the audio thread never takes).
class ModTableBuilder
{
public:
    ModTableBuilder()
    {
        // same sineLUT trick as SourceEngine::prepare: the Harmonic build is table lookups
        // and adds, never a per-entry sin() call
        for (size_t i = 0; i < modTableSize; ++i)
            sineLut[i] = (float) std::sin (6.283185307179586 * (double) i / (double) modTableSize);
    }

    // next build always runs (prepareToPlay calls this, like the old Engine::prepare did)
    void invalidate() { lastMode = -1; lastWave = -1; }

    // Builds into ex's back slot and publishes when the shape changed. Returns true if it built.
    bool update (const ModTableShape& s, ModTableExchange& ex)
    {
        bool changed = false;
        if (s.oscMode != lastMode) { lastMode = s.oscMode; changed = true; }
        if (s.oscMode == 0 && s.oscWave != lastWave) { lastWave = s.oscWave; changed = true; }
        if (s.oscMode == 1)
            for (size_t k = 0; k < 64; ++k)
                if (std::abs (s.harmonics[k] - lastHarm[k]) > 1.0e-4f)
                { lastHarm[k] = s.harmonics[k]; changed = true; }
        if (s.oscMode == 2)
            for (size_t k = 0; k < 128; ++k)
                if (std::abs (s.drawPts[k] - lastDraw[k]) > 1.0e-5f)
                { lastDraw[k] = s.drawPts[k]; changed = true; }
        if (! changed) return false;

        build (s, ex.backBuffer());
        ex.publish();
        return true;
    }

    // The table content, a pure function of the shape (math unchanged from v0.35).
    void build (const ModTableShape& s, float* table) const
    {
        if (s.oscMode == 1) // Harmonic: all 64 partials, no band-limit (DSP-NOTES §2a)
        {
            std::fill (table, table + modTableSize, 0.0f);
            for (int k = 1; k <= 64; ++k)
            {
                const float a = s.harmonics[(size_t) (k - 1)] * 0.01f;
                if (a <= 0.0f) continue;
                size_t idx = 0;
                for (size_t i = 0; i < modTableSize; ++i)
                {
                    table[i] += a * sineLut[idx];
                    idx += (size_t) k;
                    if (idx >= modTableSize) idx -= modTableSize;
                }
            }
        }
        else if (s.oscMode == 2) // Draw: 128 points, linear-interp (SourceEngine::rebuildDrawTable)
        {
            for (size_t i = 0; i < modTableSize; ++i)
            {
                const double t  = 128.0 * (double) i / (double) modTableSize;
                const auto   k0 = (size_t) t;
                const auto   k1 = (k0 + 1) % 128;
                const float  fr = (float) (t - (double) k0);
                table[i] = s.drawPts[k0] + fr * (s.drawPts[k1] - s.drawPts[k0]);
            }
        }
        else // Wave: the same analytic set playOsc reads, fixed (non-band-limited) partials
        {
            for (size_t i = 0; i < modTableSize; ++i)
                table[i] = waves::byIndex (s.oscWave, (double) i / (double) modTableSize);
        }

        float peak = 0.0f;
        for (size_t i = 0; i < modTableSize; ++i) peak = std::max (peak, std::abs (table[i]));
        if (peak > 1.0f) // peak-normalised only if > 1, same as the Osc source
            for (size_t i = 0; i < modTableSize; ++i) table[i] /= peak;
    }

private:
    std::array<float, modTableSize> sineLut {};
    int lastMode = -1, lastWave = -1;
    std::array<float, 64>  lastHarm {};
    std::array<float, 128> lastDraw {};
};
} // namespace broken::dsp
