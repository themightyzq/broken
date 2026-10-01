#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <vector>
#include "dsp/InputVarispeed.h"
#include "dsp/ModTable.h"
#include "dsp/TapeBuffer.h"

// 2026-10-01: the real-time hand-offs that replaced work done on (or under a lock shared
// with) the audio thread -- the Table-mod triple buffer and builder, TapeBuffer's seqlock,
// and InputVarispeed's constant-latency mode for Broken FX.

static bool exactlyEqual (float a, float b) { return std::equal_to<float>{} (a, b); }

using namespace broken::dsp;

TEST_CASE ("ModTableExchange: reader sees the newest published table, never the back slot")
{
    ModTableExchange ex;
    const float* first = ex.acquire();
    REQUIRE (exactlyEqual (first[0], 0.0f)); // nothing published yet: silence

    ex.backBuffer()[0] = 1.0f;
    ex.publish();
    ex.backBuffer()[0] = 2.0f; // writer moves on to another slot
    ex.publish();
    const float* t = ex.acquire();
    REQUIRE (exactlyEqual (t[0], 2.0f));       // the newest one wins
    REQUIRE (ex.acquire() == t);                // nothing new: same slot kept

    // while the reader holds t, the writer never hands out t as its back buffer
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE (ex.backBuffer() != t);
        ex.backBuffer()[0] = 3.0f + (float) i;
        ex.publish();
        REQUIRE (ex.backBuffer() != t);
    }
    REQUIRE (exactlyEqual (ex.acquire()[0], 12.0f));
}

TEST_CASE ("ModTableBuilder: rebuilds only on a real change, content follows the shape")
{
    ModTableBuilder b;
    ModTableExchange ex;
    ModTableShape s;
    s.oscMode = 2; // Draw
    for (size_t k = 0; k < 128; ++k)
        s.drawPts[k] = (float) std::sin (6.283185307179586 * (double) k / 128.0);

    REQUIRE (b.update (s, ex));        // first call always builds
    REQUIRE_FALSE (b.update (s, ex));  // unchanged: no rebuild
    const float* t = ex.acquire();
    REQUIRE (std::abs (t[1024] - 1.0f) < 1.0e-6f); // a quarter cycle in: the sine's peak

    s.drawPts[32] = 0.25f;
    REQUIRE (b.update (s, ex));
    REQUIRE (std::abs (ex.acquire()[1024] - 0.25f) < 1.0e-6f);

    // Harmonic: a pure fundamental at 100 % is a unit sine
    s.oscMode = 1;
    s.harmonics.fill (0.0f);
    s.harmonics[0] = 100.0f;
    REQUIRE (b.update (s, ex));
    t = ex.acquire();
    REQUIRE (std::abs (t[1024] - 1.0f) < 1.0e-6f);
    REQUIRE (std::abs (t[3072] + 1.0f) < 1.0e-6f);

    // invalidate forces the next build even when nothing changed (prepareToPlay)
    REQUIRE_FALSE (b.update (s, ex));
    b.invalidate();
    REQUIRE (b.update (s, ex));
}

TEST_CASE ("TapeBuffer seqlock: a REC that starts during a copy invalidates it")
{
    TapeBuffer tape;
    tape.prepare (1000.0);
    tape.startRec();
    for (int i = 0; i < 100; ++i) tape.writeMasterSample ((float) i);
    tape.stopRec();

    // clean read: one complete take
    auto g = tape.readBegin();
    std::vector<float> copy;
    REQUIRE (tape.copyLatestTo (copy) == 100);
    REQUIRE (tape.readValidate (g));
    REQUIRE (exactlyEqual (copy[99], 99.0f));

    // a FLIP then a new REC over the copied take, between begin and validate
    g = tape.readBegin();
    tape.copyLatestTo (copy);
    tape.flip();
    tape.startRec();
    REQUIRE_FALSE (tape.readValidate (g));

    // while a REC runs, the generation is stable (it only marks state changes), and the
    // latest COMPLETE take is the active one, never the buffer being recorded
    g = tape.readBegin();
    for (int i = 0; i < 10; ++i) tape.writeMasterSample (-1.0f);
    REQUIRE (tape.copyLatestTo (copy) == 100);
    REQUIRE (tape.readValidate (g));
    REQUIRE (exactlyEqual (copy[0], 0.0f));
}

TEST_CASE ("InputVarispeed fixed latency: the same exact delay with FM off and FM on")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const int lat = InputVarispeed::latencySamples (sr);
        REQUIRE (lat == (int) std::floor (std::ceil (0.1 * sr) * 0.5) - 1);

        for (bool active : { false, true })
        {
            InputVarispeed v;
            v.prepare (sr);
            v.setFixedLatency (true);
            int arrival = -1;
            for (int n = 0; n < lat + 50; ++n)
            {
                const float y = v.processSample (n == 10 ? 1.0f : 0.0f, 0.0f, active);
                if (exactlyEqual (y, 1.0f)) arrival = n;
                else REQUIRE (exactlyEqual (y, 0.0f));
            }
            REQUIRE (arrival - 10 == lat);
        }
    }

    // the instrument (fixed latency off) keeps the zero-latency inactive path
    InputVarispeed inst;
    inst.prepare (48000.0);
    REQUIRE (exactlyEqual (inst.processSample (0.5f, 0.0f, false), 0.5f));
}
