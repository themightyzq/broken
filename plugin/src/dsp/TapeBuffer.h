#pragma once
// The generational-resampling buffer (docs/DESIGN.md §3.13, DSP-NOTES.md §11).
// Two buffers ping-pong: REC always writes the INACTIVE one, playback always reads the
// ACTIVE one, FLIP swaps. Reader and writer can never touch the same memory, so
// recording generation N+1 while playing generation N is race-free by construction.
// Single audio thread assumed (JUCE processBlock); no locks needed.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

namespace ts::dsp
{
class TapeBuffer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const auto cap = (size_t) (maxSeconds * sampleRate);
        for (auto& b : buffers) { b.assign (cap, 0.0f); }
        lengths = { 0, 0 };
        active.store (0, std::memory_order_relaxed);
        recording = false;
        recPos = 0;
        inactiveIsNewer = false;
    }

    void reset()
    {
        for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f);
        lengths = { 0, 0 };
        recording = false;
        recPos = 0;
        inactiveIsNewer = false;
    }

    void startRec()
    {
        recording = true;
        recPos = 0;
        lengths[inactive()] = 0;
        inactiveIsNewer = false; // the previous finished take is being overwritten
    }

    void stopRec()
    {
        if (! recording) return;
        recording = false;
        lengths[inactive()] = recPos;
        inactiveIsNewer = recPos > 0; // a finished recording is now the newest take
    }

    bool isRecording() const { return recording; }

    // feed one master sample; auto-stops at capacity
    void writeMasterSample (float x)
    {
        if (! recording) return;
        auto& b = buffers[inactive()];
        if (recPos >= b.size()) { stopRec(); return; }
        b[recPos++] = x;
    }

    // swap generations; an in-progress recording is finalized first so FLIP always
    // lands on a complete take
    void flip()
    {
        stopRec();
        active.store (inactive(), std::memory_order_release);
        inactiveIsNewer = false; // after the swap the ACTIVE side is the newest
    }

    const float* activeData() const { return buffers[active.load (std::memory_order_relaxed)].data(); }
    size_t activeLength() const     { return lengths[active.load (std::memory_order_relaxed)]; }

    // The most recent COMPLETE take: the finished recording if one is newer than the
    // active take, else the active take. SAVE-without-FLIP reads this (v0.33); FLIP
    // keeps its meaning (make the newest take the source).
    const float* latestData() const { return buffers[latestIndex()].data(); }
    size_t latestLength() const     { return lengths[latestIndex()]; }

    // Display-only snapshot for the GUI (message thread). Latches the generation index
    // ONCE so a FLIP part-way through cannot mix two takes together.
    //
    // Honest about what this does NOT guarantee: a FLIP immediately followed by REC can
    // start overwriting the very take being copied, so the result may be part old / part
    // new for one frame. That is acceptable because nothing but a waveform drawing depends
    // on it and the GUI's poll corrects it on the next tick. Do not reuse this for audio.
    size_t copyActiveTo (std::vector<float>& dst) const
    {
        const size_t idx = active.load (std::memory_order_acquire);
        const size_t n   = lengths[idx];
        dst.resize (n);
        if (n > 0)
            std::copy (buffers[idx].begin(),
                       buffers[idx].begin() + (std::ptrdiff_t) n,
                       dst.begin());
        return n;
    }
    double sampleRateOfContent() const { return sr; }

    static constexpr double maxSeconds = 60.0; // was 10; ~11 MB/buffer at 48 k (v0.33)

private:
    size_t inactive() const { return 1 - active.load (std::memory_order_relaxed); }
    size_t latestIndex() const
    {
        return (inactiveIsNewer && lengths[inactive()] > 0) ? inactive()
                                                            : active.load (std::memory_order_relaxed);
    }

    std::array<std::vector<float>, 2> buffers;
    std::array<size_t, 2> lengths { 0, 0 };
    // atomic so the GUI's copyActiveTo() can latch a single generation (display only)
    std::atomic<size_t> active { 0 };
    size_t recPos = 0;
    bool recording = false;
    bool inactiveIsNewer = false; // a finished, un-flipped recording exists
    double sr = 48000.0;
};
} // namespace ts::dsp
