#pragma once
// The generational-resampling buffer (docs/DESIGN.md §3.13, DSP-NOTES.md §11).
// Two buffers ping-pong: REC always writes the INACTIVE one, playback always reads the
// ACTIVE one, FLIP swaps. Reader and writer can never touch the same memory, so
// recording generation N+1 while playing generation N is race-free by construction.
// Single audio thread assumed (JUCE processBlock); no locks needed.
//
// Message-thread reads (SAVE, drag-out) copy a take WITHOUT the callback lock: every
// state change the audio thread makes (startRec / stopRec / flip / reset) is bracketed by
// a seqlock generation counter, so a reader that copies a take and then sees the counter
// unchanged knows no REC started over that take during the copy (readBegin / readValidate
// / copyLatestTo). Through v0.36.0 SAVE copied under the callback lock, stalling the audio
// thread for the length of a 60 s memcpy.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace broken::dsp
{
class TapeBuffer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const auto cap = (size_t) (maxSeconds * sampleRate);
        const WriteSection ws (gen);
        for (auto& b : buffers) { b.assign (cap, 0.0f); }
        setLen (0, 0); setLen (1, 0);
        active.store (0, std::memory_order_relaxed);
        recording.store (false, std::memory_order_relaxed);
        recPos = 0;
        inactiveIsNewer.store (false, std::memory_order_relaxed);
    }

    void reset()
    {
        const WriteSection ws (gen);
        for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f);
        setLen (0, 0); setLen (1, 0);
        recording.store (false, std::memory_order_relaxed);
        recPos = 0;
        inactiveIsNewer.store (false, std::memory_order_relaxed);
    }

    void startRec()
    {
        const WriteSection ws (gen);
        recording.store (true, std::memory_order_relaxed);
        recPos = 0;
        setLen (inactive(), 0);
        inactiveIsNewer.store (false, std::memory_order_relaxed); // the previous finished take is being overwritten
    }

    void stopRec()
    {
        if (! isRecording()) return;
        const WriteSection ws (gen);
        recording.store (false, std::memory_order_relaxed);
        setLen (inactive(), recPos);
        inactiveIsNewer.store (recPos > 0, std::memory_order_relaxed); // a finished recording is now the newest take
    }

    bool isRecording() const { return recording.load (std::memory_order_relaxed); }

    // feed one master sample; auto-stops at capacity
    void writeMasterSample (float x)
    {
        if (! isRecording()) return;
        auto& b = buffers[inactive()];
        if (recPos >= b.size()) { stopRec(); return; }
        b[recPos++] = x;
    }

    // swap generations; an in-progress recording is finalized first so FLIP always
    // lands on a complete take
    void flip()
    {
        stopRec();
        const WriteSection ws (gen);
        active.store (inactive(), std::memory_order_release);
        inactiveIsNewer.store (false, std::memory_order_relaxed); // after the swap the ACTIVE side is the newest
    }

    const float* activeData() const { return buffers[active.load (std::memory_order_relaxed)].data(); }
    size_t activeLength() const     { return lengths[active.load (std::memory_order_relaxed)].load (std::memory_order_relaxed); }

    // The most recent COMPLETE take: the finished recording if one is newer than the
    // active take, else the active take. SAVE-without-FLIP reads this (v0.33); FLIP
    // keeps its meaning (make the newest take the source).
    const float* latestData() const { return buffers[latestIndex()].data(); }
    size_t latestLength() const     { return lengths[latestIndex()].load (std::memory_order_relaxed); }

    // ---- lock-free consistent read of the latest take (message thread) ----------------
    // Usage: g = readBegin(); if g is odd a state change is in progress, retry later;
    // copyLatestTo (dst); if readValidate (g) the copy is one complete take, else retry.
    uint32_t readBegin() const { return gen.load (std::memory_order_acquire); }
    bool readValidate (uint32_t g) const
    {
        std::atomic_thread_fence (std::memory_order_acquire);
        return (g & 1u) == 0 && gen.load (std::memory_order_relaxed) == g;
    }
    size_t copyLatestTo (std::vector<float>& dst) const
    {
        const size_t idx = latestIndex();
        const size_t n = std::min (lengths[idx].load (std::memory_order_relaxed), buffers[idx].size());
        dst.assign (buffers[idx].begin(), buffers[idx].begin() + (std::ptrdiff_t) n);
        return n;
    }

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
        const size_t n   = lengths[idx].load (std::memory_order_relaxed);
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
        return (inactiveIsNewer.load (std::memory_order_relaxed)
                && lengths[inactive()].load (std::memory_order_relaxed) > 0)
                   ? inactive()
                   : active.load (std::memory_order_relaxed);
    }
    void setLen (size_t idx, size_t n) { lengths[idx].store (n, std::memory_order_relaxed); }

    // seqlock writer bracket: odd while the audio thread is changing take state
    struct WriteSection
    {
        explicit WriteSection (std::atomic<uint32_t>& g) : gen (g)
        {
            gen.store (gen.load (std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);
        }
        ~WriteSection() { gen.store (gen.load (std::memory_order_relaxed) + 1u, std::memory_order_release); }
        std::atomic<uint32_t>& gen;
    };

    std::array<std::vector<float>, 2> buffers;
    // atomics: the message thread reads them (SAVE, display); only the audio thread writes
    std::array<std::atomic<size_t>, 2> lengths { };
    // atomic so the GUI's copyActiveTo() can latch a single generation (display only)
    std::atomic<size_t> active { 0 };
    size_t recPos = 0;
    std::atomic<bool> recording { false };
    std::atomic<bool> inactiveIsNewer { false }; // a finished, un-flipped recording exists
    std::atomic<uint32_t> gen { 0 };            // seqlock generation, see readBegin()
    double sr = 48000.0;
};
} // namespace broken::dsp
