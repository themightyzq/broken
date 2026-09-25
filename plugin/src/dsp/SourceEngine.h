#pragma once
// Per-voice sound source (docs/DSP-NOTES.md §1): Sample (varispeed within a
// non-destructive region, five play modes, loop-seam crossfade), Cycle (a tiny window
// looped as a raw oscillator — the classic trick), Osc, Noise, Input (FX mode), Tape.
// Varispeed IS the original Pitch Shifter: transposing changes duration, on purpose.
// Region/loop edits are ordinary block-rate settings — live editing is the design
// (a MIDI key is the preview); bounds are enforced per sample so a region shrinking
// under a playing note can never read out of range.

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include "Waves.h"

namespace broken::dsp
{
class SourceEngine
{
public:
    enum Mode { Sample = 0, Cycle, Osc, Noise, Input, Tape };
    enum LoopStyle { StyleLoop = 0, StylePingPong };
    // per-head segmentation state for STRETCH/COMPRESS (declared here because
    // advanceHead's signature names it)
    struct SegState { double base = 0.0, phase = 0.0, elapsed = 0.0; int repeat = 0; bool armed = false; };
    static constexpr int rootNote = 48;          // sample/tape root = C3 (DSP-NOTES §1.6)
    static constexpr double minRegionSamples = 64.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (size_t i = 0; i < harmTableSize; ++i)
            sineLut[i] = (float) std::sin (6.283185307179586 * (double) i / (double) harmTableSize);
        harmDirty = true;
        reset();
    }

    void reset()
    {
        readPos = 0.0;
        oscPhase = 0.0;
        modPhase = 0.0;
        rateMod = 0.0f;
        pingDir = 1.0;
        finished = false;
        dry = {};
        playhead01.store (0.0f);
    }

    void setMode (int m)                { mode = m; }
    void setTranspose (float st)        { transpose = st; }
    void setOscWave (int w)             { oscWave = w; }
    // Harmonic Mode (manual: 64 partials as % amplitude). The table is rebuilt only when
    // the levels actually change — per-sample additive over 64 partials would be silly.
    void setOscMode (int m)             { oscMode = m; }
    void setHarmonics (const float* pct64)
    {
        bool changed = false;
        for (int k = 0; k < 64; ++k)
            if (std::abs (pct64[k] - harmPct[(size_t) k]) > 1.0e-4f)
            {
                harmPct[(size_t) k] = pct64[k];
                changed = true;
            }
        if (changed) harmDirty = true;
    }
    // DRAW mode table (docs/DSP-NOTES.md §1.3b): 128 points, -1..+1, one per 1/128 cycle.
    static constexpr int drawPointCount = 128;
    void setDrawPoints (const float* pts)
    {
        bool changed = false;
        for (int k = 0; k < drawPointCount; ++k)
            if (std::abs (pts[k] - drawPts[(size_t) k]) > 1.0e-5f)
            {
                drawPts[(size_t) k] = pts[k];
                changed = true;
            }
        if (changed) drawDirty = true;
    }

    void setWindow (float pos01, float lenSamples, float xfadeSamples)
    {
        winPos = pos01; winLen = lenSamples < 2.0f ? 2.0f : lenSamples;
        winXfade = xfadeSamples;
    }
    // Region/loop setters flag an ACTUAL change so a finished one-shot can re-arm
    // (docs/DSP-NOTES.md §1.1a "Re-arm on edit"). Blocks that re-apply identical values
    // must not flag anything, or a finished one-shot would restart every block.
    void setRegion (float start01, float end01)
    {
        if (std::abs (start01 - regStart01) > 1.0e-7f || std::abs (end01 - regEnd01) > 1.0e-7f)
            playbackDirty = true;
        regStart01 = start01; regEnd01 = end01;
    }
    void setLoopOn (bool on)            { if (on != loopOn) playbackDirty = true; loopOn = on; }
    void setLoopStyle (int s)           { if (s != loopStyle) playbackDirty = true; loopStyle = s; }
    void setReverse (bool r)            { if (r != reversed) playbackDirty = true; reversed = r; }
    void setLoopXfadeMs (float ms)
    {
        const float v = ms < 0.0f ? 0.0f : ms;
        if (std::abs (v - xfadeMs) > 1.0e-7f) playbackDirty = true;
        xfadeMs = v;
    }

    // Rewinds a head that has already run off the end of a one-shot into the CURRENT
    // region, so a live edit is audible without retriggering. Called by the Voice only
    // while the note is still gated. Does NOT touch the amp envelope: no re-attack, so
    // dragging a region edge scrubs instead of machine-gunning transients.
    void rearmIfEdited()
    {
        if (! playbackDirty) return;
        playbackDirty = false;
        if (! (mode == Sample || mode == Tape) || activeLen() < 2) return;
        if (! finished && ! dry.finished) return;

        double rs, re;
        regionBounds (activeLen(), rs, re);
        if (finished)
        {
            readPos  = reversed ? re : rs;
            pingDir  = reversed ? -1.0 : 1.0;
            finished = false;
            mainSeg  = {};
        }
        if (dry.finished)
        {
            dry.pos      = reversed ? re : rs;
            dry.dir      = reversed ? -1.0 : 1.0;
            dry.finished = false;
            drySeg       = {};
        }
    }
    void setPitchMix (float m)          { pitchMix = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m); }
    void setNoiseModel (float ampPct, float phasePct) { noiseAmpPct = ampPct; noisePhasePct = phasePct; }
    void setXfadeShape (int s)          { xfadeShape = s; } // 0 Linear, 1 EqPower
    // Stretcher / Time Compressor (docs/DSP-NOTES.md §16)
    void setStretch (bool on, float freqHz, float amount, float predelayMs)
    {
        stretchOn = on;
        stretchFreq = freqHz < 20.0f ? 20.0f : (freqHz > 2000.0f ? 2000.0f : freqHz);
        stretchAmt = amount < -100.0f ? -100.0f : (amount > 100.0f ? 100.0f : amount);
        stretchPredelay = predelayMs < 0.0f ? 0.0f : predelayMs;
    }
    // FM from the Modulator: instantaneous rate multiplied by (1 + index*amount*m),
    // wired by the Voice each sample; negative totals reverse travel (era chaos, kept)
    void setRateMod (float m)           { rateMod = m; }

    void setSampleData (const float* data, size_t len, double dataSr)
    {
        if (data != sampleData || len != sampleLen) playbackDirty = true;
        sampleData = data; sampleLen = len; sampleSr = dataSr > 0 ? dataSr : sr;
    }
    void setTapeData (const float* data, size_t len, double dataSr)
    {
        if (data != tapeData || len != tapeLen) playbackDirty = true;
        tapeData = data; tapeLen = len; tapeSr = dataSr > 0 ? dataSr : sr;
    }

    void noteOn (int midiNote, uint32_t voiceSeed)
    {
        note = midiNote;
        oscPhase = 0.0;
        pingDir = 1.0;
        finished = false;
        // a fresh note has already picked up every pending edit; leaving the flag set
        // would make the NEXT one-shot re-arm itself once and play twice
        playbackDirty = false;
        if ((mode == Sample || mode == Tape) && activeLen() >= 2)
        {
            double rs, re;
            regionBounds (activeLen(), rs, re);
            readPos = reversed ? re : rs; // start marker is where the key starts playing
            pingDir = reversed ? -1.0 : 1.0;
            dry.pos = readPos;
            dry.dir = pingDir;
            dry.finished = false;
            mainSeg = {};
            drySeg = {};
        }
        else
            readPos = 0.0;
        rng = 0x9E3779B9u ^ (voiceSeed * 2654435761u);
        if (rng == 0) rng = 1;
    }

    float processSample (float input)
    {
        switch (mode)
        {
            case Sample: return playRegioned();
            case Tape:   return playRegioned(); // same head machinery as Sample (v0.14)
            case Cycle:  return playCycle();
            case Osc:    return playOsc();
            case Noise:  return nextNoise();
            case Input:  return input;
            default:     return 0.0f;
        }
    }

    // normalized playhead over the whole file, for the editor's live cursor
    float getPlayhead01() const { return playhead01.load (std::memory_order_relaxed); }

    // the sample region (or active tape take) read as a looping wavetable at an
    // arbitrary frequency — the Modulator's Sample/Tape sources (DSP-NOTES §2a)
    float tickModSource (bool fromTape, double freqHz)
    {
        const float* data = fromTape ? tapeData : sampleData;
        const size_t len = fromTape ? tapeLen : sampleLen;
        if (data == nullptr || len < 2) return 0.0f;

        double rs = 0.0, re = (double) (len - 1);
        if (! fromTape) regionBounds (len, rs, re);

        modPhase += freqHz / sr;
        modPhase -= std::floor (modPhase);
        return readInterp (data, len, rs + modPhase * (re - rs));
    }

private:
    // whichever buffer the current mode plays; one head path serves both (v0.14)
    const float* activeData() const { return mode == Tape ? tapeData : sampleData; }
    size_t activeLen() const        { return mode == Tape ? tapeLen : sampleLen; }
    double activeSr() const         { return mode == Tape ? tapeSr : sampleSr; }

    float noteFreq() const
    {
        return 440.0f * std::pow (2.0f, ((float) note - 69.0f + transpose) / 12.0f);
    }

    float rateFactor() const { return 1.0f + rateMod; }

    // effective region in fractional sample indices over [0, len-1]; start >= end or a
    // sub-minimum region falls back sensibly (docs/DSP-NOTES.md §1.1a)
    void regionBounds (size_t len, double& rs, double& re) const
    {
        const double last = (double) (len - 1);
        rs = (double) regStart01 * last;
        re = (double) regEnd01 * last;
        if (rs >= re) { rs = 0.0; re = last; return; }
        if (re - rs < minRegionSamples)
            re = std::min (last, rs + minRegionSamples);
        if (re - rs < 2.0) { rs = 0.0; re = last; }
    }

    float playRegioned()
    {
        const double pitchedRate = (activeSr() / sr)
                                 * std::pow (2.0, ((double) note - rootNote + transpose) / 12.0)
                                 * (double) rateFactor();
        const float main = advanceHead (readPos, pingDir, finished, pitchedRate, true, mainSeg);
        if (pitchMix > 0.999f)
            return main;

        // Pitch MIX (docs/DSP-NOTES.md §2a): a second, root-rate head renders the "dry"
        // stream — the original Pitch Shifter's Mix, for detune/chorus at ~50%.
        // Truly dry: no transpose, no FM rate-mod.
        const double dryRate = activeSr() / sr;
        const float d = advanceHead (dry.pos, dry.dir, dry.finished, dryRate, false, drySeg);
        return pitchMix * main + (1.0f - pitchMix) * d;
    }

    float advanceHead (double& hPos, double& hDir, bool& hFin, double baseRate, bool publish,
                       SegState& seg)
    {
        const float* data = activeData();
        const size_t len = activeLen();
        if (data == nullptr || len < 2 || hFin) return 0.0f;

        double rs, re;
        regionBounds (len, rs, re);
        const double L = re - rs;

        // live region edits can strand the position outside the region: clamp, don't read OOB
        double pos = hPos;
        if (pos < rs) pos = rs;
        if (pos > re) pos = re;

        const bool ping = loopOn && loopStyle == StylePingPong;
        const double dir = ping ? hDir : (reversed ? -1.0 : 1.0);
        const double step = baseRate * dir;
        const double X = std::min ((double) xfadeMs * 0.001 * sr, L * 0.5);

        float y = readInterp (data, len, pos);

        if (loopOn && X >= 1.0)
        {
            if (loopStyle == StyleLoop)
            {
                // internal crossfade (docs/DSP-NOTES.md §1.1a): blend the loop tail into
                // the loop head using only material INSIDE the region; the wrap target is
                // offset by X, so the effective loop is L − X. Works for any selection,
                // including regions touching the file head/tail.
                auto blend = [this] (float a, float b, float t)
                {
                    if (xfadeShape == 1) // EqPower (manual's Crossfade Looping option)
                    {
                        const float g2 = std::sin (1.5707963f * t);
                        const float g1 = std::cos (1.5707963f * t);
                        return a * g1 + b * g2;
                    }
                    return (1.0f - t) * a + t * b;
                };
                if (! reversed && pos > re - X)
                {
                    const float t = (float) ((pos - (re - X)) / X);
                    y = blend (y, readInterp (data, len, pos - (L - X)), t);
                }
                else if (reversed && pos < rs + X)
                {
                    const float t = (float) ((rs + X - pos) / X);
                    y = blend (y, readInterp (data, len, pos + (L - X)), t);
                }
            }
            else // PingPong turnaround blend
            {
                // fade the outgoing stream into the incoming reflected stream BEFORE the
                // boundary (self-contained). Raised-cosine weight: a linear ramp has
                // nonzero slope at the window edges and just moves the corner there;
                // cosine makes the blend C1 (the audible fix for the turnaround thunk).
                // Tradeoff: the perceived bounce point shifts inward by up to X.
                if (dir > 0.0 && pos > re - X)
                {
                    const double t = (pos - (re - X)) / X;               // 0..1
                    const float w = 0.5f * (1.0f - (float) std::cos (3.14159265358979 * t));
                    y = (1.0f - w) * y + w * readInterp (data, len, 2.0 * re - X - pos);
                }
                else if (dir < 0.0 && pos < rs + X)
                {
                    const double t = ((rs + X) - pos) / X;               // 0..1
                    const float w = 0.5f * (1.0f - (float) std::cos (3.14159265358979 * t));
                    y = (1.0f - w) * y + w * readInterp (data, len, 2.0 * rs + X - pos);
                }
            }
        }

        // STRETCH / COMPRESS (docs/DSP-NOTES.md §16): segment-repeat time scaling.
        // Acts on the position advance, so it composes with region/loop/rev; a loop wrap
        // or bounce below resyncs segmentation (documented rule).
        const bool stretchActive = stretchOn && std::abs (stretchAmt) >= 1.0f
                                && seg.elapsed >= (double) stretchPredelay * 0.001 * sr;
        seg.elapsed += 1.0;
        if (stretchActive)
        {
            const double S = std::max (16.0, sr / (double) stretchFreq);
            const double k = 1.0 + (double) std::abs (stretchAmt) / 25.0; // 1..5
            const double dirSign = step >= 0.0 ? 1.0 : -1.0;

            if (! seg.armed) { seg.base = pos; seg.phase = 0.0; seg.repeat = 0; seg.armed = true; }

            // seam crossfade: blend this segment's tail into its own head (self-contained)
            const double F = std::min (S * 0.25, 0.005 * sr);
            if (F >= 1.0 && seg.phase > S - F)
            {
                const float t = (float) ((seg.phase - (S - F)) / F);
                const float w = 0.5f * (1.0f - std::cos (3.14159265f * t));
                const float head = readInterp (data, len,
                                               clampToBuffer (seg.base + (seg.phase - S) * dirSign, len));
                y = (1.0f - w) * y + w * head;
            }

            seg.phase += std::abs (step);
            while (seg.phase >= S)
            {
                seg.phase -= S;
                if (stretchAmt > 0.0f) // Stretcher: replay each segment k times
                {
                    if (++seg.repeat >= (int) k) { seg.base += S * dirSign; seg.repeat = 0; }
                }
                else                   // Time Compressor: skip ahead k segments
                    seg.base += k * S * dirSign;
            }
            // deliberately NOT clamped to the buffer: the boundary logic below must be
            // able to see pos run past the region end, or a compressed one-shot would
            // pin at the last sample and never finish (readInterp clamps its own reads)
            pos = seg.base + seg.phase * dirSign;
        }
        else
        {
            seg.armed = false;
            pos += step;
        }

        if (! loopOn)
        {
            if (! reversed)
            {
                if (pos > re) { hFin = true; pos = re; }
                else if (pos < rs) pos = rs; // net-negative FM rate: park at start
            }
            else
            {
                if (pos < rs) { hFin = true; pos = rs; }
                else if (pos > re) pos = re;
            }
        }
        else if (loopStyle == StyleLoop)
        {
            const double Lw = (X >= 1.0) ? (L - X) : L; // crossfaded wrap lands mid-stream
            if (! reversed) { while (pos > re) pos -= Lw; while (pos < rs) pos += Lw; }
            else            { while (pos < rs) pos += Lw; while (pos > re) pos -= Lw; }
        }
        else // PingPong
        {
            for (int guard = 0; guard < 8 && (pos > re || pos < rs); ++guard)
            {
                if (pos > re)
                {
                    // with a blend window the bounce hands off to the incoming stream's
                    // current position (2re − X − pos); X=0 degenerates to pure reflection
                    pos = (X >= 1.0 ? 2.0 * re - X - pos : 2.0 * re - pos);
                    hDir = -hDir;
                }
                else
                {
                    pos = (X >= 1.0 ? 2.0 * rs + X - pos : 2.0 * rs - pos);
                    hDir = -hDir;
                }
            }
            if (pos > re || pos < rs) pos = rs + 0.5 * L; // absurd rate: recentre
        }

        // a wrap/bounce/park moved us off the segment grid: resync so the next segment
        // starts from the new position instead of jumping back across the loop point
        if (stretchActive && std::abs (pos - (seg.base + seg.phase
                                             * (step >= 0.0 ? 1.0 : -1.0))) > 1e-6)
        {
            seg.base = pos;
            seg.phase = 0.0;
            seg.repeat = 0;
        }

        hPos = pos;
        if (publish)
            playhead01.store ((float) (pos / (double) (len - 1)), std::memory_order_relaxed);
        return y;
    }

    float playCycle()
    {
        const float* data = sampleData;
        const size_t len = sampleLen;
        if (data == nullptr || len < 4) return 0.0f;

        // CYCLE windows inside the region: the region IS the sample (§1.1a)
        double rs, re;
        regionBounds (len, rs, re);
        const double L = std::min ((double) winLen, re - rs);
        if (L < 2.0) return 0.0f;
        const double start = rs + winPos * (re - rs - L);
        const double inc = (double) noteFreq() * L / sr * rateFactor();

        double p = readPos; // phase within [0, L)
        p += inc;
        while (p >= L) p -= L;
        while (p < 0.0) p += L;
        readPos = p;

        float y = readInterp (data, len, start + p);
        // micro-crossfade across the seam so the loop click is a *choice* (xfade 0 = raw)
        if (winXfade > 0.5f && p < (double) winXfade)
        {
            const float t = (float) (p / (double) winXfade);
            const float tail = readInterp (data, len, start + L - (double) winXfade + p);
            y = tail * (1.0f - t) + y * t;
        }
        playhead01.store ((float) ((start + p) / (double) (len - 1)), std::memory_order_relaxed);
        return y;
    }

    float playOsc()
    {
        const double f = (double) noteFreq() * rateFactor();
        // Rebuild keying (DSP-NOTES §1.3a): the note's own pitch, never the FM-modulated
        // instantaneous frequency. A fast Modulator can swing f by >2% every sample, and
        // keying the rebuild on that would fire the 262k-op rebuild every sample, per
        // voice. FM sidebands above the partial cap still alias — that's the era.
        const double fNote = (double) noteFreq();

        if (oscMode == 1 || oscMode == 2) // Harmonic / Draw: read the oscillator table
        {
            if (oscMode == 2)
            {
                // A drawn table is literal: its content does not depend on the note, so
                // unlike Harmonic it is never rebuilt on a pitch change (DSP-NOTES §1.3b).
                if (drawDirty || tableIsHarmonic) rebuildDrawTable();
            }
            else if (harmDirty || ! tableIsHarmonic
                     || std::abs (fNote - harmBuiltFreq) > harmBuiltFreq * 0.02)
                rebuildHarmonicTable (fNote);
            const double t = oscPhase * (double) harmTableSize;
            const auto i0 = (size_t) t % harmTableSize;
            const auto i1 = (i0 + 1) % harmTableSize;
            const float frac = (float) (t - std::floor (t));
            const float y = harmTable[i0] + frac * (harmTable[i1] - harmTable[i0]);
            oscPhase += f / sr;
            oscPhase -= std::floor (oscPhase);
            return y;
        }

        // band-limit only the additive odd wave; naive saw/square alias by design
        const int maxH = f > 0.0 ? (int) std::floor (0.45 * sr / f) : 9;
        const float y = waves::byIndex (oscWave, oscPhase, maxH < 1 ? 1 : (maxH > 9 ? 9 : maxH));
        oscPhase += f / sr;
        oscPhase -= std::floor (oscPhase);
        return y;
    }

    // One wavetable cycle summed from the 64 partials, band-limited against 0.45*sr.
    // Partial k at table index i is exactly sineLUT[(i*k) mod N], so the whole rebuild
    // is table lookups and adds — 262k sin() calls here would stall the audio thread.
    // Linear-interpolates the 64 drawn points up into the 4096-entry oscillator table.
    // Deliberately NOT band-limited: a drawn corner buzzes and aliases, which is the era
    // (DSP-NOTES §1.3b, aliasing policy §5).
    void rebuildDrawTable()
    {
        for (size_t i = 0; i < harmTableSize; ++i)
        {
            const double t  = (double) drawPointCount * (double) i / (double) harmTableSize;
            const auto   k0 = (size_t) t;
            const auto   k1 = (k0 + 1) % (size_t) drawPointCount;
            const float  fr = (float) (t - (double) k0);
            harmTable[i] = drawPts[k0] + fr * (drawPts[k1] - drawPts[k0]);
        }
        float peak = 0.0f;
        for (auto v : harmTable) peak = std::max (peak, std::abs (v));
        if (peak > 1.0f) // a quiet drawing stays quiet; the source still cannot exceed +-1
            for (auto& v : harmTable) v /= peak;

        drawDirty = false;
        tableIsHarmonic = false;
    }

    void rebuildHarmonicTable (double f)
    {
        const int maxK = f > 0.0 ? (int) std::floor (0.45 * sr / f) : 64;
        for (auto& v : harmTable) v = 0.0f;

        for (int k = 1; k <= 64 && k <= maxK; ++k)
        {
            const float a = harmPct[(size_t) (k - 1)] * 0.01f;
            if (a <= 0.0f) continue;
            size_t idx = 0;
            for (size_t i = 0; i < harmTableSize; ++i)
            {
                harmTable[i] += a * sineLut[idx];
                idx += (size_t) k;
                if (idx >= harmTableSize) idx -= harmTableSize;
            }
        }

        float peak = 0.0f;
        for (auto v : harmTable) peak = std::max (peak, std::abs (v));
        if (peak > 1.0f) // keep the source inside +-1 however the bars are stacked
            for (auto& v : harmTable) v /= peak;

        harmDirty = false;
        tableIsHarmonic = true;
        harmBuiltFreq = f > 0.0 ? f : 1.0;
    }

    // era Noise (manual-verified): a randomized SINE at the note frequency — Amp Noise %
    // re-rolls the cycle's amplitude at each wrap, Phase Noise % jitters the phase per
    // sample. 0/0 = plain sine, 100/100 ≈ white. (Pre-v0.8 plain white = max settings.)
    float nextNoise()
    {
        const double f = (double) noteFreq();
        noisePhase += f / sr;
        if (noisePhase >= 1.0)
        {
            noisePhase -= std::floor (noisePhase);
            noiseCycleAmp = 1.0f + noiseAmpPct * 0.01f * (rnd01() * 2.0f - 1.0f);
        }
        const double jitter = (double) (noisePhasePct * 0.01f) * ((double) rnd01() - 0.5);
        return noiseCycleAmp * (float) std::sin (6.283185307179586 * (noisePhase + jitter));
    }

    float rnd01()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; // xorshift32
        return (float) ((double) rng / 4294967295.0);
    }

    static double clampToBuffer (double p, size_t len)
    {
        const double last = (double) (len - 1);
        return p < 0.0 ? 0.0 : (p > last ? last : p);
    }

    static float readInterp (const float* data, size_t len, double pos)
    {
        if (pos < 0.0) pos = 0.0;
        const double last = (double) (len - 1);
        if (pos > last) pos = last;
        const auto i0 = (size_t) pos;
        const auto i1 = i0 + 1 < len ? i0 + 1 : i0;
        const float frac = (float) (pos - (double) i0);
        return data[i0] + frac * (data[i1] - data[i0]);
    }

    double sr = 48000.0;
    int mode = Sample;
    int note = 48;
    float transpose = 0.0f;
    int oscWave = 2;
    float winPos = 0.1f, winLen = 512.0f, winXfade = 8.0f;
    float rateMod = 0.0f;

    float regStart01 = 0.0f, regEnd01 = 1.0f;
    bool loopOn = false;
    int loopStyle = StyleLoop;
    bool reversed = false;
    float xfadeMs = 10.0f;
    double pingDir = 1.0;
    bool finished = false;
    bool playbackDirty = false;   // a region/loop/buffer control actually moved

    const float* sampleData = nullptr; size_t sampleLen = 0; double sampleSr = 48000.0;
    const float* tapeData = nullptr;   size_t tapeLen = 0;   double tapeSr = 48000.0;

    struct DryHead { double pos = 0.0; double dir = 1.0; bool finished = false; };
    DryHead dry;
    SegState mainSeg, drySeg;
    bool stretchOn = false;
    float stretchFreq = 110.0f, stretchAmt = 0.0f, stretchPredelay = 0.0f;
    float pitchMix = 1.0f;
    float noiseAmpPct = 25.0f, noisePhasePct = 25.0f;
    double noisePhase = 0.0;
    float noiseCycleAmp = 1.0f;
    int xfadeShape = 0;

    int oscMode = 0;
    static constexpr size_t harmTableSize = 4096;
    std::array<float, 64> harmPct {};
    std::array<float, (size_t) drawPointCount> drawPts {};
    bool drawDirty = true;
    // harmTable is shared by Harmonic and Draw (they never sound at once); this says who
    // filled it last, so switching modes always rebuilds
    bool tableIsHarmonic = true;
    std::array<float, harmTableSize> harmTable {};
    std::array<float, harmTableSize> sineLut {};
    bool harmDirty = true;
    double harmBuiltFreq = 1.0;

    double readPos = 0.0;
    double oscPhase = 0.0;
    double modPhase = 0.0;
    uint32_t rng = 1;
    std::atomic<float> playhead01 { 0.0f };
};
} // namespace broken::dsp
