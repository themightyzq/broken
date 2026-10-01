#pragma once
// Deliberately free of JucePlugin_* macros so the identical translation unit
// builds inside the plugin targets AND the broken_cli console app.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "PresetManager.h"
#include "dsp/Engine.h"

namespace broken
{
class BrokenProcessor : public juce::AudioProcessor,
                        private juce::Timer
{
public:
    BrokenProcessor();
    ~BrokenProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

#if BROKEN_FX
    const juce::String getName() const override { return "Broken FX"; }
    // effect build: always fed from the track, never note-triggered (DESIGN split spec)
    bool acceptsMidi() const override  { return false; }
#else
    const juce::String getName() const override { return "Broken"; }
    bool acceptsMidi() const override  { return true; }
#endif
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // delay max is 2 s, but res.fb 0.995 / dly.fb 0.9 ring far longer; hosts use this
    // to size bounce/freeze tails, so err generous (v0.23)
    double getTailLengthSeconds() const override { return 10.0; }
    // hosts bind their bypass control here; processBlock honours it with a 25 ms
    // crossfade to true input pass-through (DSP-NOTES 12b)
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    // Public so the CLI can prove the legacy-tag migration; setStateInformation calls it.
    void restoreFromXml (juce::XmlElement& xml);
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Snapshot I/O shared by editor and CLI: JSON {"params": {"<id>": <real value>, ...}}.
    // Only the listed ids are applied, EXCEPT when the root carries "reset": true (the
    // "00 Init" presets): then every parameter first returns to its declared Params.h
    // default (the sample region markers excepted, they belong to the loaded sample), the
    // wrapper's context default is re-applied (applyContextDefaults), and the listed ids
    // are applied last.
    bool loadSnapshotJson (const juce::var& parsed, juce::String& errorOut);
    juce::var snapshotToJson() const;

    // mono-summed sample import for Sample/Cycle modes (editor drag-drop + CLI --sample).
    // Message thread (or the CLI's main thread). Reads and decodes the file WITHOUT the
    // callback lock, then hands the finished buffer to the audio thread through an atomic
    // pointer (adopted at the top of the next processBlock or prepareToPlay); the buffer it
    // replaces is freed later on this thread, never on the audio thread.
    bool loadSampleFile (const juce::File& file, juce::String& errorOut);

    // Table mod source: rebuild the OSCILLATOR-shape table from the current parameters if
    // they changed, off the audio thread. A 60 Hz message-thread timer calls this in a
    // host; prepareToPlay calls it; tools without a message loop (broken_cli, the gates)
    // call it after changing parameters. Any non-audio thread.
    void syncModTable();

    // GUI support: message-thread reads for the waveform display and output meter
    const std::vector<float>& getSampleBuffer() const
    {
        static const std::vector<float> none;
        return displaySample != nullptr ? displaySample->data : none;
    }

    // ---- what the GUI should DRAW -------------------------------------------------
    // In Tape mode the engine plays the tape take, not the loaded sample, so drawing
    // sampleBuf there showed one thing while you heard another (and region drags landed
    // on a waveform you could not see). These return the buffer that is actually sounding.
    // Call refreshDisplaySource() from the GUI's timer first; it is message-thread only.
    void refreshDisplaySource()
    {
        const bool tape = engine.getTape().activeLength() > 0
                       && apvts.getRawParameterValue ("source.mode") != nullptr
                       && (int) apvts.getRawParameterValue ("source.mode")->load() == 5;
        if (tape)
        {
            engine.getTape().copyActiveTo (tapeSnapshot);
            tapeSnapshotSr = engine.getTape().sampleRateOfContent();
        }
        else tapeSnapshot.clear();
        showingTape = tape;
    }
    bool isShowingTape() const { return showingTape; }
    const std::vector<float>& getDisplayBuffer() const
    {
        return showingTape ? tapeSnapshot : getSampleBuffer();
    }
    // the FILE's native rate for time readouts follows the shown buffer too
    juce::String getDisplayName() const
    {
        return showingTape ? juce::String ("TAPE TAKE") : sampleName;
    }
    double getDisplaySr() const { return showingTape ? tapeSnapshotSr : getSampleFileSr(); }
    double getSampleFileSr() const // the FILE's native rate, for time readouts
    {
        return displaySample != nullptr ? displaySample->sr : 48000.0;
    }
    juce::String getSampleName() const { return sampleName; }
    bool isTapeRecording() { return engine.getTape().isRecording(); }
    size_t getTapeLength() { return engine.getTape().activeLength(); }
    size_t getLatestTakeLength() { return engine.getTape().latestLength(); } // SAVE/drag source
    float getPlayhead01() const { return engine.getPlayhead01(); }
    bool isSampleMissing() const { return sampleMissing; }

    // tuner taps (display-grade): copy the latest n samples of the chosen tap
    void copyTunerTap (bool postChain, float* dst, int n) const
    {
        (postChain ? engine.tunerTapOut() : engine.tunerTapIn()).copyLatest (dst, n);
    }

    // one-press nearest-note lock (docs/DSP-NOTES.md §15); returns false when the IN
    // tap has no confident pitch
    bool applyTuneLock();

    // RND (DSP-NOTES §12b): randomizes every automatable param except the source itself,
    // output level, bypass, the transport latches, and the hand-drawn point banks; hot
    // params draw in reduced ranges so a roll can be ugly but never damaging. Message
    // thread only (setValueNotifyingHost is the sanctioned path).
    void randomizeParams();
    void undoRandomize();     // restores the exact pre-RND state
    bool hasRandomUndo() const { return hasRandomUndoState; }
    std::atomic<float> outPeak { 0.0f };

    // Note events dropped this session because a block carried more than
    // maxNoteEventsPerBlock note-ons/offs (a MIDI storm): `events` is reserve()'d once in
    // prepareToPlay and never allowed to grow past that in processBlock (growth would
    // allocate on the audio thread). Exposed so a host/GUI/diagnostic can surface it;
    // Broken's panel does not currently display it.
    int getDroppedNoteEventCount() const { return droppedNoteEvents.load (std::memory_order_relaxed); }

    // writes the latest complete tape take as a 24-bit WAV (mono; the FX build writes its
    // matched stereo pair) -- the era's design-then-export step. Copies the take without
    // the callback lock (TapeBuffer's seqlock), then writes the file.
    bool saveTapeToFile (const juce::File& file, juce::String& errorOut);

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { *this };

private:
    void timerCallback() override;
    // the wrapper-keyed fresh-instance default (constructor and Init reset share it)
    void applyContextDefaults();
    void adoptPendingSample() noexcept; // audio thread / prepareToPlay only
    void collectRetiredSamples();       // message thread only

    dsp::EngineParams gatherParams() const;
    float p (const char* id) const { return cached.at (id)->load(); }
    // ids for the indexed parameter banks, built once so gatherParams never formats
    // strings on the audio thread
    std::array<std::string, 64> harmIds;
    std::array<std::string, 128> drawIds;
    std::array<std::string, 128> curveIds;

    // FX build: true stereo, two independently-parameterised engines (DESIGN split spec
    // item 2). `engine` stays the name used everywhere else in this header (tape, tuner
    // taps, playhead) and is the L/primary channel; `engineR` is the R channel, prepared
    // and parameterised identically every block so a mono input produces bit-identical
    // L/R output. The instrument build keeps the single mono `engine` unchanged.
    dsp::Engine engine;
#if BROKEN_FX
    dsp::Engine engineR;
#endif
    std::vector<float> monoIn, monoOut;
#if BROKEN_FX
    std::vector<float> monoInR, monoOutR, monoOutMix; // monoOutMix: used only for a mono output bus
#endif
    // Capped at maxNoteEventsPerBlock and never allowed to grow past that in processBlock,
    // so a MIDI storm cannot trigger a reallocation on the audio thread. `chunkEvents` is
    // the per-chunk re-slice used when a host hands us a block bigger than samplesPerBlock
    // (see processBlock's chunk loop, the same pattern used in other ZQ SFX products).
    static constexpr size_t maxNoteEventsPerBlock = 256;
    std::vector<dsp::NoteEvent> events;
    std::vector<dsp::NoteEvent> chunkEvents;
    std::atomic<int> droppedNoteEvents { 0 };

    // ---- loaded sample, handed to the audio thread without a lock ----------------------
    // Every SampleSlot is owned by sampleSlots (message thread). loadSampleFile publishes a
    // new slot through pendingSample; the audio thread exchanges it out, points the engines
    // at it and stores its seq in audioSampleSeq. collectRetiredSamples() then frees, on
    // the message thread, every slot older than the one the audio thread now uses.
    struct SampleSlot
    {
        std::vector<float> data;
        double sr = 48000.0;
        std::uint64_t seq = 0;
    };
    std::vector<std::unique_ptr<SampleSlot>> sampleSlots; // message thread
    SampleSlot* displaySample = nullptr;                  // message thread: newest loaded
    std::uint64_t nextSampleSeq = 1;                      // message thread
    std::atomic<SampleSlot*> pendingSample { nullptr };
    std::atomic<std::uint64_t> audioSampleSeq { 0 };
    SampleSlot* audioSample = nullptr;                    // audio thread

    // ---- Table mod source (dsp/ModTable.h) ---------------------------------------------
    dsp::ModTableBuilder modTableBuilder;   // guarded by modTableLock (never the audio thread)
    dsp::ModTableExchange modTables;        // writer: syncModTable; reader: processBlock
    juce::CriticalSection modTableLock;

    // ---- liveness token for the deferred sample reload (restoreFromXml) ---------------
    // The callAsync lambda holds a shared_ptr to this, never a bare `this`: the destructor
    // marks it dead under the lock, so a reload queued before the processor was destroyed
    // finds it dead and does nothing (and a reload already running finishes first).
    struct Liveness
    {
        juce::CriticalSection lock;
        bool alive = true;
    };
    std::shared_ptr<Liveness> liveness = std::make_shared<Liveness>();

#if BROKEN_FX
    // InputVarispeed's constant FX latency, mirrored on the bypass path so bypass stays
    // time-aligned with the processed signal (DSP-NOTES §14a). Sized in prepareToPlay.
    // bypassLine*: one ring per channel, exactly getLatencySamples() long; bypassOut*: the
    // delayed input for the current chunk (used by the bypass crossfade).
    std::vector<float> bypassLineL, bypassLineR, bypassOutL, bypassOutR;
    int bypassLinePos = 0;
#endif

    juce::String sampleName;
    bool sampleMissing = false;
    std::vector<float> tapeSnapshot;   // GUI-owned copy of the active take (display only)
    double tapeSnapshotSr = 48000.0;
    bool showingTape = false;

    std::unordered_map<std::string, std::atomic<float>*> cached;
    juce::ValueTree lastRandomUndoState;
    bool hasRandomUndoState = false;
    bool playHeld = false; // edge state for the PLAY latch
    juce::AudioParameterBool* bypassParam = nullptr;
    float bypassFade = 0.0f;     // 0 = active, 1 = bypassed; ramped in processBlock
    float bypassFadeStep = 0.0f; // per-sample step for a 25 ms fade, set in prepareToPlay
    // -1..+1, cleared in prepareToPlay so a wheel held at a transport stop cannot strand
    // the instrument detuned (DSP-NOTES §9.1)
    float pitchBendNorm = 0.0f;
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrokenProcessor)
};
} // namespace broken
