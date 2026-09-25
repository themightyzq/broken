#pragma once
// The whole instrument, JUCE-free: 6 voices, note allocation (mono last-note / poly /
// unison), the global tail (SamplerColour → TapeBuffer tap → output level), and the
// FX-mode free-run path. Single audio thread assumed.

#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>
#include "Voice.h"
#include "SamplerColour.h"
#include "TapeBuffer.h"
#include "Waves.h"

namespace broken::dsp
{
struct EngineParams
{
    VoiceParams voice;
    float inTrimDb = 0.0f;
    int   voiceMode = 0;        // 0 Mono, 1 Poly
    bool  retrigger = true;
    bool  unison = false;
    float spreadCents = 12.0f;
    int   colourMode = 1;       // 0 Off, 1 12bit, 2 8bit
    float colourRate = 44100.0f;
    bool  tapeRec = false;
    bool  tapeFlip = false;     // edge-detected here
    float chainMix = 1.0f;      // global wet/dry, 1 = full wet (bit-exact)
    float outLevelDb = 0.0f;
};

struct NoteEvent
{
    int samplePos = 0;
    bool on = false;
    int note = 60;
    float velocity = 1.0f;
};

// single-writer tap ring for the tuners: audio thread pushes, GUI copies the most
// recent window. Display-grade — relaxed atomics are all the ordering it needs.
struct TapRing
{
    static constexpr int size = 8192;
    std::array<float, size> data {};
    std::atomic<int> widx { 0 };

    void push (float x)
    {
        const int i = widx.load (std::memory_order_relaxed);
        data[(size_t) i] = x;
        widx.store ((i + 1) % size, std::memory_order_relaxed);
    }

    void copyLatest (float* dst, int n) const
    {
        int start = widx.load (std::memory_order_relaxed) - n;
        while (start < 0) start += size;
        for (int i = 0; i < n; ++i)
            dst[i] = data[(size_t) ((start + i) % size)];
    }
};

class Engine
{
public:
    static constexpr int numVoices = 6;
    // held-note stack cap (docs/DSP-NOTES.md §9): the vector is reserve()'d to this size
    // and never allowed to grow past it, so a 33rd simultaneously-held key can never
    // trigger a reallocation on the audio thread — it drops the oldest held note instead.
    static constexpr size_t maxHeld = 32;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v.prepare (sampleRate);
        colour.prepare (sampleRate);
        tape.prepare (sampleRate);
        heldNotes.clear();
        heldNotes.reserve (maxHeld);
        prevFlip = false;

        // Table mod source (DSP-NOTES §2a "Table"): same sineLUT trick as
        // SourceEngine::prepare, built once so the harmonic table rebuild is table
        // lookups + adds, never a per-entry sin() call.
        for (size_t i = 0; i < modTableSize; ++i)
            modSineLut[i] = (float) std::sin (6.283185307179586 * (double) i / (double) modTableSize);
        modOscMode = -1; // force a rebuild on the first applyParams after prepare
        modOscWave = -1;
    }

    void setSampleData (const float* d, size_t n, double dsr)
    {
        sampleData = d; sampleLen = n; sampleSr = dsr;
    }

    void applyParams (const EngineParams& p)
    {
        ep = p;
        const bool freeRun = (p.voice.sourceMode == SourceEngine::Input);
        const float comp = p.unison ? 1.0f / std::sqrt ((float) numVoices) : 1.0f;

        rebuildModTableIfNeeded (p.voice);

        for (int i = 0; i < numVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            v.applyParams (p.voice);
            v.setSampleData (sampleData, sampleLen, sampleSr);
            v.setTapeData (tape.activeData(), tape.activeLength(), tape.sampleRateOfContent());
            v.setModTable (modTable.data(), modTableSize);
            v.setFreeRun (freeRun);
            v.setGainComp (comp);
            v.setDetuneSemis (p.unison
                ? (p.spreadCents / 100.0f) * ((numVoices > 1 ? 2.0f * i / (numVoices - 1) : 0.0f) - 1.0f)
                : 0.0f);
        }

        colour.setMode (p.colourMode);
        colour.setRateHz (p.colourRate);

        if (p.tapeRec && ! tape.isRecording()) tape.startRec();
        if (! p.tapeRec && tape.isRecording()) tape.stopRec();
        if (p.tapeFlip && ! prevFlip) tape.flip(); // rising edge only
        prevFlip = p.tapeFlip;

        inGain  = std::pow (10.0f, p.inTrimDb / 20.0f);
        outGain = std::pow (10.0f, p.outLevelDb / 20.0f);
        mixTarget = p.chainMix;
    }

    // interleaved-free mono processing: input may be null (instrument mode)
    void process (const float* inMono, float* outMono, int numSamples,
                  const NoteEvent* events, int numEvents)
    {
        int ev = 0;
        // MIX ramps linearly across the block; snapped to target below so steady-state
        // 1.0f stays exactly 1.0f (the unity-null contract, DSP-NOTES §12b)
        const float mixStep = numSamples > 0 ? (mixTarget - mixCur) / (float) numSamples : 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            while (ev < numEvents && events[ev].samplePos <= n)
            {
                handleEvent (events[ev]);
                ++ev;
            }

            const float input = inMono != nullptr ? inMono[n] * inGain : 0.0f;

            float sum = 0.0f, dry = 0.0f;
            const bool freeRun = (ep.voice.sourceMode == SourceEngine::Input);
            if (freeRun)
            {
                sum = voices[0].render (input); // FX mode is forced-mono (DESIGN §2)
                dry = voices[0].getLastDrySample();
            }
            else
            {
                for (auto& v : voices)
                    if (v.isActive())
                    {
                        sum += v.render (0.0f);
                        dry += v.getLastDrySample();
                    }
            }

            mixCur += mixStep;
            // blend before COLOUR/OUT so they act on the result; at mix==1 this is
            // bit-exact wet
            const float pre = (1.0f - mixCur) * dry + mixCur * sum;
            float y = colour.processSample (pre);
            tape.writeMasterSample (y);
            outMono[n] = y * outGain;

            // tuner taps: A = what enters the mangle (monitor voice's source),
            // B = what leaves the chain. A stale/inactive voice pushes silence so the
            // IN tuner dims instead of freezing on an old reading.
            auto& mv = voices[(size_t) monitorVoice];
            tapIn.push (freeRun || mv.isActive() ? mv.getLastSourceSample() : 0.0f);
            tapOut.push (y);
        }
        mixCur = mixTarget; // kill float drift from the per-sample ramp
    }

    TapeBuffer& getTape() { return tape; }

    // most recently triggered voice's read position, for the editor's live playhead
    float getPlayhead01() const { return voices[(size_t) monitorVoice].getPlayhead01(); }

    const TapRing& tunerTapIn() const  { return tapIn; }
    const TapRing& tunerTapOut() const { return tapOut; }

private:
    // Table mod source (item 2, DSP-NOTES §2a "Table"): one 4096-entry table per Engine,
    // NOT SourceEngine::harmTable -- the Osc source and the modulator's Table source read
    // different memory, so they can sound at once (e.g. OSC as the source, Osc-panel-shape
    // as the modulator, at different implied rates). Every voice reads this table via its
    // own phase (Voice::tickTableMod); this class only rebuilds the shared content, at
    // most once per block (applyParams runs once per processBlock call) and only when the
    // OSCILLATOR panel actually changed -- dirty-flagged the same way SourceEngine's
    // harmDirty/drawDirty are, so an unrelated knob move costs one branch, not a rebuild.
    //
    // The Harmonic and Draw builds are deliberately NOT band-limited (DSP-NOTES §5 aliasing
    // policy: this product's documented era grit) and therefore never depend on a note or
    // mod frequency, unlike SourceEngine::rebuildHarmonicTable's 0.45*sr/f cap -- so, unlike
    // that table, this one never needs rebuilding when a frequency changes, only when the
    // shape itself (harmonics/draw points/osc.mode/osc.wave) does.
    static constexpr size_t modTableSize = 4096;
    std::array<float, modTableSize> modTable {};
    std::array<float, modTableSize> modSineLut {};
    int modOscMode = -1, modOscWave = -1;
    std::array<float, 64> modHarm {};
    std::array<float, 128> modDraw {};

    void rebuildModTableIfNeeded (const VoiceParams& vp)
    {
        bool changed = false;
        if (vp.oscMode != modOscMode) { modOscMode = vp.oscMode; changed = true; }
        if (vp.oscMode == 0 && vp.oscWave != modOscWave) { modOscWave = vp.oscWave; changed = true; }
        if (vp.oscMode == 1)
            for (int k = 0; k < 64; ++k)
                if (std::abs (vp.harmonics[(size_t) k] - modHarm[(size_t) k]) > 1.0e-4f)
                { modHarm[(size_t) k] = vp.harmonics[(size_t) k]; changed = true; }
        if (vp.oscMode == 2)
            for (int k = 0; k < 128; ++k)
                if (std::abs (vp.drawPts[(size_t) k] - modDraw[(size_t) k]) > 1.0e-5f)
                { modDraw[(size_t) k] = vp.drawPts[(size_t) k]; changed = true; }
        if (! changed) return;

        if (vp.oscMode == 1) // Harmonic: all 64 partials, no band-limit (see class comment)
        {
            for (auto& v : modTable) v = 0.0f;
            for (int k = 1; k <= 64; ++k)
            {
                const float a = vp.harmonics[(size_t) (k - 1)] * 0.01f;
                if (a <= 0.0f) continue;
                size_t idx = 0;
                for (size_t i = 0; i < modTableSize; ++i)
                {
                    modTable[i] += a * modSineLut[idx];
                    idx += (size_t) k;
                    if (idx >= modTableSize) idx -= modTableSize;
                }
            }
        }
        else if (vp.oscMode == 2) // Draw: 128 points, linear-interp (SourceEngine::rebuildDrawTable)
        {
            for (size_t i = 0; i < modTableSize; ++i)
            {
                const double t  = 128.0 * (double) i / (double) modTableSize;
                const auto   k0 = (size_t) t;
                const auto   k1 = (k0 + 1) % 128;
                const float  fr = (float) (t - (double) k0);
                modTable[i] = vp.drawPts[k0] + fr * (vp.drawPts[k1] - vp.drawPts[k0]);
            }
        }
        else // Wave: the same analytic set playOsc reads, fixed (non-band-limited) partial
             // count -- a table built once has no note frequency to band-limit against.
        {
            for (size_t i = 0; i < modTableSize; ++i)
                modTable[i] = waves::byIndex (vp.oscWave, (double) i / (double) modTableSize);
        }

        float peak = 0.0f;
        for (auto v : modTable) peak = std::max (peak, std::abs (v));
        if (peak > 1.0f) // peak-normalised only if > 1 (item 2 spec), same as the Osc source
            for (auto& v : modTable) v /= peak;
    }

    void handleEvent (const NoteEvent& e)
    {
        if (ep.unison)      handleUnison (e);
        else if (ep.voiceMode == 0) handleMono (e);
        else                handlePoly (e);
    }

    void handleMono (const NoteEvent& e)
    {
        auto& v = voices[0];
        if (e.on)
        {
            // cap at maxHeld: never reallocate on the audio thread (docs/DSP-NOTES.md §9)
            if (heldNotes.size() >= maxHeld)
                heldNotes.erase (heldNotes.begin());
            heldNotes.push_back ({ e.note, e.velocity });
            v.noteOn (e.note, e.velocity, 1, ep.retrigger || ! v.isActive());
            monitorVoice = 0;
        }
        else
        {
            heldNotes.erase (std::remove_if (heldNotes.begin(), heldNotes.end(),
                                             [&] (const Held& h) { return h.note == e.note; }),
                             heldNotes.end());
            if (heldNotes.empty())
                v.noteOff();
            else // last-note priority: fall back to the most recent still-held note
                v.noteOn (heldNotes.back().note, heldNotes.back().velocity, 1, false);
        }
    }

    void handlePoly (const NoteEvent& e)
    {
        if (e.on)
        {
            int idx = -1;
            for (int i = 0; i < numVoices; ++i)
                if (! voices[(size_t) i].isActive()) { idx = i; break; }
            if (idx < 0) { idx = stealCursor; stealCursor = (stealCursor + 1) % numVoices; }
            voices[(size_t) idx].noteOn (e.note, e.velocity, (uint32_t) (idx + 1), true);
            monitorVoice = idx;
        }
        else
        {
            for (auto& v : voices)
                if (v.isActive() && v.currentNote() == e.note)
                    v.noteOff();
        }
    }

    void handleUnison (const NoteEvent& e)
    {
        // unison overrides poly: all voices stack one note (mono-style last-note logic)
        if (e.on)
        {
            // cap at maxHeld: never reallocate on the audio thread (docs/DSP-NOTES.md §9)
            if (heldNotes.size() >= maxHeld)
                heldNotes.erase (heldNotes.begin());
            heldNotes.push_back ({ e.note, e.velocity });
            for (int i = 0; i < numVoices; ++i)
                voices[(size_t) i].noteOn (e.note, e.velocity, (uint32_t) (i + 1),
                                           ep.retrigger || ! voices[(size_t) i].isActive());
            monitorVoice = 0;
        }
        else
        {
            heldNotes.erase (std::remove_if (heldNotes.begin(), heldNotes.end(),
                                             [&] (const Held& h) { return h.note == e.note; }),
                             heldNotes.end());
            if (heldNotes.empty())
                for (auto& v : voices) v.noteOff();
            else
                for (int i = 0; i < numVoices; ++i)
                    voices[(size_t) i].noteOn (heldNotes.back().note, heldNotes.back().velocity,
                                               (uint32_t) (i + 1), false);
        }
    }

    struct Held { int note; float velocity; };

    std::array<Voice, numVoices> voices;
    SamplerColour colour;
    TapeBuffer tape;
    TapRing tapIn, tapOut;
    EngineParams ep;
    std::vector<Held> heldNotes;

    const float* sampleData = nullptr;
    size_t sampleLen = 0;
    double sampleSr = 48000.0;

    double sr = 48000.0;
    float inGain = 1.0f, outGain = 1.0f;
    float mixTarget = 1.0f, mixCur = 1.0f; // init 1.0 = default, no first-block ramp
    bool prevFlip = false;
    int stealCursor = 0;
    int monitorVoice = 0;
};
} // namespace broken::dsp
