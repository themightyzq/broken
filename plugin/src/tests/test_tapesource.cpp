#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include "dsp/SourceEngine.h"
#include "dsp/TapeBuffer.h"
#include <functional>

// Exact float equality where bit-for-bit identity IS the contract (passthrough, hold,
// determinism, snapshot). std::equal_to keeps -Wfloat-equal out of it without weakening
// the check: this is still ==, not a tolerance.
static bool exactlyEqual (float a, float b) { return std::equal_to<float>{} (a, b); }

// v0.14: TAPE reads through the same head machinery as SAMPLE. Before this, Tape was a
// bare one-shot with no region, loop or reverse — while the docs claimed otherwise.

using ts::dsp::SourceEngine;

namespace
{
constexpr double SR = 48000.0;

// ramp buffer: the output value IS the read position, so behaviour is directly readable
std::vector<float> ramp (size_t n)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = (float) i;
    return v;
}

// SourceEngine holds an atomic (non-copyable), so configure in place
void makeTape (SourceEngine& src, std::vector<float>& buf)
{
    src.prepare (SR);
    src.setTapeData (buf.data(), buf.size(), SR);
    src.setMode (SourceEngine::Tape);
    src.setLoopXfadeMs (0.0f);
}
} // namespace

TEST_CASE ("tape one-shot still stops at the end (LOOP off)", "[tape]")
{
    auto buf = ramp (1001);
    SourceEngine src;
    makeTape (src, buf);
    src.setLoopOn (false);
    src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i <= 1000; ++i)
        REQUIRE (src.processSample (0.0f) == Catch::Approx ((float) i).margin (1e-3));
    REQUIRE (exactlyEqual (src.processSample (0.0f), 0.0f));
}

TEST_CASE ("tape LOOPS a captured take", "[tape]")
{
    auto buf = ramp (1001);
    SourceEngine src;
    makeTape (src, buf);
    src.setLoopOn (true);
    src.setLoopStyle (SourceEngine::StyleLoop);
    src.noteOn (SourceEngine::rootNote, 1);
    for (int i = 0; i <= 1000; ++i) src.processSample (0.0f);
    // wraps instead of going silent — the thing generational resampling wants
    REQUIRE (src.processSample (0.0f) == Catch::Approx (1.0f).margin (1e-3));
}

TEST_CASE ("tape plays in REVERSE", "[tape]")
{
    auto buf = ramp (1001);
    SourceEngine src;
    makeTape (src, buf);
    src.setLoopOn (false);
    src.setReverse (true);
    src.noteOn (SourceEngine::rootNote, 1);
    REQUIRE (src.processSample (0.0f) == Catch::Approx (1000.0f).margin (1e-3));
    REQUIRE (src.processSample (0.0f) == Catch::Approx (999.0f).margin (1e-3));
}

TEST_CASE ("tape honours the region markers", "[tape]")
{
    auto buf = ramp (1001);
    SourceEngine src;
    makeTape (src, buf);
    src.setLoopOn (false);
    src.setRegion (0.4f, 0.6f);   // 400..600 of the take
    src.noteOn (SourceEngine::rootNote, 1);
    REQUIRE (src.processSample (0.0f) == Catch::Approx (400.0f).margin (1e-3));
    for (int i = 1; i <= 200; ++i) src.processSample (0.0f);
    REQUIRE (exactlyEqual (src.processSample (0.0f), 0.0f)); // stopped at the region end, not the file end
}

TEST_CASE ("tape and sample buffers stay independent", "[tape]")
{
    auto tapeBuf = ramp (1001);
    std::vector<float> sampleBuf (1001, 7.0f); // a constant, unmistakable from the ramp
    SourceEngine src;
    src.prepare (SR);
    src.setTapeData (tapeBuf.data(), tapeBuf.size(), SR);
    src.setSampleData (sampleBuf.data(), sampleBuf.size(), SR);
    src.setLoopXfadeMs (0.0f);

    src.setMode (SourceEngine::Tape);
    src.noteOn (SourceEngine::rootNote, 1);
    REQUIRE (src.processSample (0.0f) == Catch::Approx (0.0f).margin (1e-3)); // ramp[0]
    src.processSample (0.0f);

    src.setMode (SourceEngine::Sample);
    src.noteOn (SourceEngine::rootNote, 1);
    REQUIRE (src.processSample (0.0f) == Catch::Approx (7.0f).margin (1e-3)); // the sample
}

// v0.21: the GUI needs to DRAW the tape take (it was drawing the loaded sample in Tape
// mode, so you saw one thing and heard another). copyActiveTo is that read.
TEST_CASE ("tape snapshot returns the active take", "[tape]")
{
    ts::dsp::TapeBuffer t;
    t.prepare (48000.0);

    t.startRec();
    for (int i = 0; i < 500; ++i) t.writeMasterSample ((float) i);
    t.stopRec();

    std::vector<float> snap;
    REQUIRE (t.copyActiveTo (snap) == 0);   // nothing FLIPped yet: active take is empty

    t.flip();
    REQUIRE (t.copyActiveTo (snap) == 500);
    REQUIRE (snap.size() == 500u);
    for (int i = 0; i < 500; ++i) REQUIRE (exactlyEqual (snap[(size_t) i], (float) i));

    SECTION ("a second recording does not disturb the take being shown")
    {
        t.startRec();
        for (int i = 0; i < 300; ++i) t.writeMasterSample (-1.0f);
        std::vector<float> again;
        REQUIRE (t.copyActiveTo (again) == 500);
        for (int i = 0; i < 500; ++i) REQUIRE (exactlyEqual (again[(size_t) i], (float) i));
    }

    SECTION ("the snapshot matches what playback reads")
    {
        REQUIRE (t.activeLength() == snap.size());
        for (size_t i = 0; i < snap.size(); ++i)
            REQUIRE (exactlyEqual (t.activeData()[i], snap[i]));
    }
}

// v0.33: "latest take" — SAVE/drag export the newest COMPLETE take without a FLIP.
// DSP-NOTES §11: a finished recording is latest until FLIP makes it active or a new
// REC starts overwriting it.
TEST_CASE ("tape latest-take tracks the newest complete recording", "[tape]")
{
    ts::dsp::TapeBuffer tape;
    tape.prepare (48000.0);
    REQUIRE (tape.latestLength() == 0);

    // record take A (3 samples)
    tape.startRec();
    for (float v : { 0.1f, 0.2f, 0.3f }) tape.writeMasterSample (v);
    tape.stopRec();
    REQUIRE (tape.activeLength() == 0);       // not flipped: nothing active yet
    REQUIRE (tape.latestLength() == 3);       // but SAVE can already reach take A
    REQUIRE (exactlyEqual (tape.latestData()[2], 0.3f));

    // FLIP: A becomes the source; latest == active
    tape.flip();
    REQUIRE (tape.activeLength() == 3);
    REQUIRE (tape.latestLength() == 3);
    REQUIRE ((tape.latestData() == tape.activeData())); // pre-parenthesised: keeps Catch2's decomposer (and -Wfloat-equal) out of the pointer compare

    // record take B (2 samples), no flip: B is now latest, A still active
    tape.startRec();
    for (float v : { 0.7f, 0.8f }) tape.writeMasterSample (v);
    tape.stopRec();
    REQUIRE (tape.activeLength() == 3);
    REQUIRE (tape.latestLength() == 2);
    REQUIRE (exactlyEqual (tape.latestData()[0], 0.7f));

    // starting a new REC invalidates B as "latest" mid-overwrite
    tape.startRec();
    REQUIRE (tape.latestLength() == 3);       // falls back to the active take
    tape.stopRec();                            // zero-length recording: stays on active
    REQUIRE (tape.latestLength() == 3);
}
