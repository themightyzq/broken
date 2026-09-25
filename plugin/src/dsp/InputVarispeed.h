#pragma once
// FM on the live Input source (docs/DSP-NOTES.md §14a): a rate-modulated read head on a
// short delay line -- true varispeed of the live signal, not a bolt-on effect. FM
// everywhere else works by speeding up/slowing down a stored buffer's read rate
// (SourceEngine::setRateMod); Input has no buffer to vary the rate of (case Input just
// returns the live sample), so instrument FM and mod.fmindex were both silently dead on
// Input (and therefore dead in Broken FX, whose source is always Input). This class gives
// Input the same read-head-speed mechanism used everywhere else, chasing a live-written
// ring buffer the way TapeShift.h does for pitch -- house pattern, different math.
//
// r[n] (== fmIndex * modAmt * m[n], computed by the caller exactly as every other FM
// source) advances the read head by (1 + r) samples per sample: delay d[n] = d[n-1] - r[n].
// A DC-offset modulator (Sample/Tape/Table mod sources can sit off-centre, unlike the
// Modulator's own +-1 oscillator) would otherwise walk d monotonically until it pins
// against a clamp rail and the varispeed effect stops tracking; a one-pole pull-back
// toward the centre delay D0 (corner ~2 Hz, slow enough to not itself audibly modulate
// the signal) continuously re-centres the head. d is hard-clamped to [1, maxDelay-2] as a
// last resort against the pull-back's own math (belt and braces, same policy as
// Modulator::applyPM's delay clamp).
//
// Exact bypass (docs/DSP-NOTES.md §0 unity-null policy): `active` is false whenever FM is
// not engaged on Input (modOn/modMode gate, checked by the caller), and processSample then
// returns x completely unprocessed -- bit-exact passthrough, matching today's behaviour
// with no InputVarispeed stage at all. The delay line itself is still fed every call
// (active or not) so a stale/empty buffer can never be read the instant FM engages; the
// documented trade-off is a ONE-TIME delay jump of D0 samples (~50 ms) at the moment FM
// turns on, because `active` flips straight from "return x" (zero added delay) to a read
// already centred D0 samples behind the write head. The alternative (crossfading the
// dry/delayed paths across the transition) was rejected: it would smear the very first
// engage/disengage transient rather than being a single clean edge, and D0's audio is
// always real prior signal (never silence), so there is no click -- only a latency step.
#include <cmath>
#include <cstddef>
#include <vector>

namespace broken::dsp
{
class InputVarispeed
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // ~100 ms, sized from sr in prepare() per house real-time rule -- no audio-thread
        // allocation. Long enough that the +-8 fmindex extreme (see test j) cannot run the
        // read head off either clamp rail before the pull-back catches it.
        buf.assign ((size_t) std::ceil (0.1 * sampleRate), 0.0f);
        maxDelay = (double) buf.size();
        d0 = maxDelay * 0.5;
        // one-pole pull-back toward D0, corner ~2 Hz (the standard 1-e^-2*pi*fc/sr
        // one-pole used throughout this codebase, e.g. FilterStack); computed here, not
        // per sample
        pull = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 2.0 / sr);
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
        delay = d0;
    }

    // x: the live signal (post liveShift, pre-Modulator -- see Voice::render).
    // r: fmIndex * modAmt * m, identical to every other FM source's rate-mod term.
    // active: modOn && modMode == FM (checked by the caller, which also gates on
    //         sourceMode == Input before ever calling this).
    float processSample (float x, float r, bool active)
    {
        buf[(size_t) w] = x;
        w = (w + 1) % (int) buf.size();

        if (! active)
        {
            delay = d0; // re-centre while idle: engaging later always starts from D0,
            return x;   // never from wherever a previous FM session left the head
        }

        delay -= (double) r;
        delay += pull * (d0 - delay);
        if (! (delay >= 1.0)) delay = 1.0;               // also catches NaN
        if (delay > maxDelay - 2.0) delay = maxDelay - 2.0;

        double pos = (double) w - delay;
        const double n = (double) buf.size();
        while (pos < 0.0) pos += n;
        while (pos >= n) pos -= n;
        const auto i0 = (size_t) pos;
        const auto i1 = (i0 + 1) % buf.size();
        const float frac = (float) (pos - (double) i0);
        return buf[i0] + frac * (buf[i1] - buf[i0]);
    }

private:
    double sr = 48000.0;
    std::vector<float> buf;
    int w = 0;
    double maxDelay = 4800.0;
    double d0 = 2400.0;
    double delay = 2400.0;
    double pull = 0.0;
};
} // namespace broken::dsp
