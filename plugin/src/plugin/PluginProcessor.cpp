#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <set>
#include "dsp/PitchDetector.h"

namespace broken
{
namespace
{
    // every ID Params.h defines; gatherParams() reads them each block via cached atomics
    const char* const allIds[] = {
        "source.mode", "source.pitch", "source.ext", "source.finecents",
        "source.winpos", "source.winlen",
        "source.xfade", "source.oscwave", "source.intrim",
        "sample.regstart", "sample.regend", "sample.loopon", "sample.loopstyle",
        "sample.rev", "sample.xfade",
        "stretch.on", "stretch.freq", "stretch.amount", "stretch.predelay",
        "flat.on", "flat.response", "midi.bendrange",
        "mod.on", "mod.amount", "mod.freq", "mod.mode", "mod.wave", "mod.source",
        "mod.fmindex", "source.pitchmix",
        "ws.on", "ws.curve", "ws.drive", "ws.morph", "ws.trim", "ws.randseed",
        "flt.on", "flt.cutoff", "flt.ext", "flt.poles", "flt.envamt",
        "fenv.a", "fenv.d", "fenv.s", "fenv.r",
        "res.on", "res.freq", "res.fb", "res.damp",
        "inv.on", "inv.mix", "inv.type", "noise.amp", "noise.phase", "sample.xfadeshape",
        "dly.on", "dly.time", "dly.fine", "dly.mix", "dly.inv", "dly.fb",
        "amp.a", "amp.d", "amp.s", "amp.r",
        "aux.a", "aux.d", "aux.s", "aux.r", "aux.dest", "aux.amount",
        "voice.mode", "voice.retrig", "voice.unison", "voice.spread",
        "tape.rec", "tape.flip", "play.hold",
        "col.mode", "col.rate", "chain.mix", "out.level", "bypass"
    };

#if BROKEN_FX
    // Pushes in[0..n) through a ring of line.size() samples starting at startPos and writes
    // what comes out (the input delayed by line.size() samples) to out. in == out is fine.
    void ringDelay (const float* in, float* out, std::vector<float>& line, int startPos, int n) noexcept
    {
        const int len = (int) line.size();
        int pos = startPos;
        for (int i = 0; i < n; ++i)
        {
            const float y = line[(size_t) pos];
            line[(size_t) pos] = in[i];
            out[i] = y;
            if (++pos == len) pos = 0;
        }
    }
#endif
}

BrokenProcessor::BrokenProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      // state type tag "BrokenFX" vs "Broken": a state saved by one product must never
      // silently load into the other (different chain shape, different source lock)
#if BROKEN_FX
      apvts (*this, nullptr, "BrokenFX", params::createLayout())
#else
      apvts (*this, nullptr, "Broken", params::createLayout())
#endif
{
    for (auto* id : allIds)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr); // Params.h and allIds[] must never drift apart
        cached[id] = raw;
    }
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    jassert (bypassParam != nullptr);

    // Context default, keyed on the wrapper the host (or standalone app) created us as.
    // Session restore and preset loads arrive later and override whatever is set here.
    applyContextDefaults();

#if BROKEN_FX
    // InputVarispeed runs at a constant, reported latency in the effect (DSP-NOTES §14a).
    // prepareToPlay sets the exact value for the real sample rate; until then report the
    // 48 kHz figure rather than 0, for hosts that read the latency before preparing.
    engine.setInputFixedLatency (true);
    engineR.setInputFixedLatency (true);
    setLatencySamples (dsp::InputVarispeed::latencySamples (48000.0));
#endif

    // the 64 harmonic bars: ids built (and cached) once, never per block
    for (int k = 0; k < params::harmonicCount; ++k)
    {
        harmIds[(size_t) k] = params::harmonicId (k + 1).toStdString();
        auto* raw = apvts.getRawParameterValue (harmIds[(size_t) k]);
        jassert (raw != nullptr);
        cached[harmIds[(size_t) k]] = raw;
    }
    // the 128 Custom transfer-curve points (DSP-NOTES §3.1)
    for (int k = 0; k < params::curvePointCount; ++k)
    {
        curveIds[(size_t) k] = params::curvePointId (k + 1).toStdString();
        auto* raw = apvts.getRawParameterValue (curveIds[(size_t) k]);
        jassert (raw != nullptr);
        cached[curveIds[(size_t) k]] = raw;
    }
    // the 128 DRAW points, same treatment (DSP-NOTES §1.3b)
    for (int k = 0; k < params::drawPointCount; ++k)
    {
        drawIds[(size_t) k] = params::drawPointId (k + 1).toStdString();
        auto* raw = apvts.getRawParameterValue (drawIds[(size_t) k]);
        jassert (raw != nullptr);
        cached[drawIds[(size_t) k]] = raw;
    }
    cached["osc.mode"] = apvts.getRawParameterValue ("osc.mode");

    // Table mod source: built off the audio thread (syncModTable); the timer picks up
    // OSCILLATOR-panel edits and automation in a host, and frees retired sample buffers.
    syncModTable();
    startTimerHz (60);
}

BrokenProcessor::~BrokenProcessor()
{
    stopTimer();
    // a deferred sample reload queued by restoreFromXml must never touch a dead processor:
    // wait for one that is running, and make every later one a no-op
    const juce::ScopedLock sl (liveness->lock);
    liveness->alive = false;
}

void BrokenProcessor::applyContextDefaults()
{
    // wrapperType_Undefined (broken_cli, the gate tools, unit tests) is deliberately left
    // alone: every gate baseline assumes the declared Params.h defaults (Sample, 25/25).
    auto setReal = [this] (const char* id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
#if BROKEN_FX
    // Broken FX is the effect: in a DAW it opens listening to the track (Input). Standalone
    // keeps the declared default (also Input; gatherParams forces Input in this build anyway).
    if (wrapperType == wrapperType_VST3 || wrapperType == wrapperType_AudioUnit)
        setReal ("source.mode", (float) dsp::SourceEngine::Input);
#else
    // Broken is the instrument: hosts do not feed audio to instrument tracks, and the
    // declared Sample default is silent until a file is loaded, so a fresh VST3/AU/Standalone
    // instance would answer notes with nothing. It opens on white noise instead (owner
    // decision): source Noise with Amp Noise and Phase Noise at 100 %, the setting at which
    // SourceEngine::nextNoise is white (the 25/25 declared defaults are a near-pure sine).
    if (wrapperType == wrapperType_VST3 || wrapperType == wrapperType_AudioUnit
        || wrapperType == wrapperType_Standalone)
    {
        setReal ("source.mode",  (float) dsp::SourceEngine::Noise);
        setReal ("noise.amp",    100.0f);
        setReal ("noise.phase",  100.0f);
    }
#endif
}

void BrokenProcessor::timerCallback()
{
    syncModTable();
    collectRetiredSamples();
}

void BrokenProcessor::syncModTable()
{
    dsp::ModTableShape shape;
    shape.oscMode = (int) p ("osc.mode");
    shape.oscWave = (int) p ("source.oscwave");
    for (int k = 0; k < params::harmonicCount; ++k)
        shape.harmonics[(size_t) k] = cached.at (harmIds[(size_t) k])->load();
    for (int k = 0; k < params::drawPointCount; ++k)
        shape.drawPts[(size_t) k] = cached.at (drawIds[(size_t) k])->load();

    const juce::ScopedLock sl (modTableLock);
    modTableBuilder.update (shape, modTables);
}

void BrokenProcessor::adoptPendingSample() noexcept
{
    if (auto* slot = pendingSample.exchange (nullptr, std::memory_order_acq_rel))
    {
        audioSample = slot;
        engine.setSampleData (slot->data.data(), slot->data.size(), slot->sr);
#if BROKEN_FX
        // engineR keeps its OWN sample pointer (Engine::applyParams re-pushes it to its
        // voices every block), so it must be pointed at the new buffer too, or the
        // R channel's Sample source / Sample mod source serves the old one.
        engineR.setSampleData (slot->data.data(), slot->data.size(), slot->sr);
#endif
        // published last: from here on the message thread may free older slots
        audioSampleSeq.store (slot->seq, std::memory_order_release);
    }
}

void BrokenProcessor::collectRetiredSamples()
{
    // every slot older than the one the audio thread has adopted is unreachable from it
    // (it only ever moves forward through pendingSample); newer ones are pending or newest
    const auto inUse = audioSampleSeq.load (std::memory_order_acquire);
    sampleSlots.erase (std::remove_if (sampleSlots.begin(), sampleSlots.end(),
                                       [this, inUse] (const std::unique_ptr<SampleSlot>& s)
                                       { return s->seq < inUse && s.get() != displaySample; }),
                       sampleSlots.end());
}

void BrokenProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
#if BROKEN_FX
    engineR.prepare (sampleRate);
#endif
    // prepareToPlay never runs concurrently with processBlock, so it may adopt a sample
    // loaded before playback started; an already adopted one is re-pushed after prepare
    adoptPendingSample();
    if (audioSample != nullptr)
    {
        engine.setSampleData (audioSample->data.data(), audioSample->data.size(), audioSample->sr);
#if BROKEN_FX
        engineR.setSampleData (audioSample->data.data(), audioSample->data.size(), audioSample->sr);
#endif
    }
    // Table mod: build now from the current parameters (synchronous, not the audio thread)
    {
        const juce::ScopedLock sl (modTableLock);
        modTableBuilder.invalidate();
    }
    syncModTable();
    {
        const float* table = modTables.acquire();
        engine.setModTable (table);
#if BROKEN_FX
        engineR.setModTable (table);
#endif
    }
    monoIn.resize ((size_t) samplesPerBlock);
    monoOut.resize ((size_t) samplesPerBlock);
#if BROKEN_FX
    // R channel: prepared and sized identically to L so the two engines stay in lockstep
    monoInR.resize ((size_t) samplesPerBlock);
    monoOutR.resize ((size_t) samplesPerBlock);
    monoOutMix.resize ((size_t) samplesPerBlock);

    // the Input FM stage's constant delay (DSP-NOTES §14a): reported to the host, and
    // mirrored on the bypass path. Depends on the sample rate only, never the block size.
    const int latency = dsp::InputVarispeed::latencySamples (sampleRate);
    setLatencySamples (latency);
    bypassLineL.assign ((size_t) latency, 0.0f);
    bypassLineR.assign ((size_t) latency, 0.0f);
    bypassOutL.assign ((size_t) samplesPerBlock, 0.0f);
    bypassOutR.assign ((size_t) samplesPerBlock, 0.0f);
    bypassLinePos = 0;
#endif
    playHeld = false; // never strand a held note across a rate/buffer change
    pitchBendNorm = 0.0f; // nor a held wheel: it would leave the instrument detuned
    bypassFadeStep = 1.0f / (0.025f * (float) sampleRate); // 25 ms fade
    bypassFade = p ("bypass") > 0.5f ? 1.0f : 0.0f; // a session restored bypassed must not fade in
    events.reserve (maxNoteEventsPerBlock);
    chunkEvents.reserve (maxNoteEventsPerBlock); // never larger than `events`, same cap
    droppedNoteEvents.store (0, std::memory_order_relaxed);
}

bool BrokenProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out || in == juce::AudioChannelSet::disabled();
}

dsp::EngineParams BrokenProcessor::gatherParams() const
{
    dsp::EngineParams e;
    auto& v = e.voice;

    v.sourceMode  = (int) p ("source.mode");
#if BROKEN_FX
    // FX build: the source is always the live input, regardless of what the (hidden,
    // still-automatable) parameter holds — DESIGN split spec item 1.
    v.sourceMode = dsp::SourceEngine::Input;
#endif
    // Pitch bend rides on the source transpose rather than having its own path, so it
    // reaches varispeed, the oscillators and the live input shifter alike (DSP-NOTES §9.1)
    // PITCH EXT: ±24 st unless EXT, per PANEL.md — it was wired to a toggle that
    // nothing read (v0.23)
    v.sourcePitch = (p ("source.ext") > 0.5f ? p ("source.pitch")
                                              : std::clamp (p ("source.pitch"), -24.0f, 24.0f))
                  + p ("source.finecents") / 100.0f
                  + pitchBendNorm * p ("midi.bendrange");
    v.winPos      = p ("source.winpos");
    v.winLen      = p ("source.winlen");
    v.winXfade    = p ("source.xfade");
    v.oscWave     = (int) p ("source.oscwave");
    v.oscMode     = (int) p ("osc.mode");
    for (int k = 0; k < params::harmonicCount; ++k)
        v.harmonics[(size_t) k] = cached.at (harmIds[(size_t) k])->load();
    for (int k = 0; k < params::drawPointCount; ++k)
        v.drawPts[(size_t) k] = cached.at (drawIds[(size_t) k])->load();
    v.regStart    = p ("sample.regstart");
    v.regEnd      = p ("sample.regend");
    v.stretchOn        = p ("stretch.on") > 0.5f;
    v.stretchFreq      = p ("stretch.freq");
    v.stretchAmount    = p ("stretch.amount");
    v.stretchPredelayMs = p ("stretch.predelay");
    v.flatOn           = p ("flat.on") > 0.5f;
    v.flatResponseMs   = p ("flat.response");
    v.loopOn      = p ("sample.loopon") > 0.5f;
    v.loopStyle   = (int) p ("sample.loopstyle");
    v.rev         = p ("sample.rev") > 0.5f;
    v.loopXfadeMs = p ("sample.xfade");

    v.modOn = p ("mod.on") > 0.5f;
    v.modAmount = p ("mod.amount");
    v.modFreq = p ("mod.freq");
    v.modMode = (int) p ("mod.mode");
    v.modWave = (int) p ("mod.wave");
    v.modSource = (int) p ("mod.source");
    v.fmIndex = p ("mod.fmindex");
    v.pitchMix = p ("source.pitchmix");

    v.wsOn = p ("ws.on") > 0.5f;
    v.wsCurve = (int) p ("ws.curve");
    v.wsDriveDb = p ("ws.drive");
    v.wsMorph = p ("ws.morph");
    v.wsTrimDb = p ("ws.trim");
    v.wsSeed = (int) p ("ws.randseed");
    // ids built once in the constructor: formatting 128 strings per block would allocate
    // on the audio thread
    for (int i = 0; i < params::curvePointCount; ++i)
        v.wsCustom[(size_t) i] = cached.at (curveIds[(size_t) i])->load();

    v.fltOn = p ("flt.on") > 0.5f;
    // authentic 500 Hz floor unless EXT (docs/DSP-NOTES.md §4)
    v.fltCutoffHz = p ("flt.ext") > 0.5f ? p ("flt.cutoff")
                                         : std::max (500.0f, p ("flt.cutoff"));
    v.fltFloorHz = p ("flt.ext") > 0.5f ? 20.0f : 500.0f;
    v.fltPoles = (int) p ("flt.poles");
    v.fltEnvAmt = p ("flt.envamt");
    v.fenvA = p ("fenv.a"); v.fenvD = p ("fenv.d");
    v.fenvS = p ("fenv.s"); v.fenvR = p ("fenv.r");

    v.resOn = p ("res.on") > 0.5f;
    v.resFreq = p ("res.freq");
    v.resFb = p ("res.fb");
    v.resDamp = p ("res.damp");

    v.invOn = p ("inv.on") > 0.5f;
    v.invMix = p ("inv.mix");
    v.invType = (int) p ("inv.type");
    v.noiseAmpPct = p ("noise.amp");
    v.noisePhasePct = p ("noise.phase");
    v.xfadeShape = (int) p ("sample.xfadeshape");

    v.dlyOn = p ("dly.on") > 0.5f;
    v.dlyTimeMs = juce::jlimit (0.1f, 2000.0f, p ("dly.time") + p ("dly.fine"));
    v.dlyMix = p ("dly.mix");
    v.dlyInvert = p ("dly.inv") > 0.5f;
    v.dlyFb = p ("dly.fb");

    v.ampA = p ("amp.a"); v.ampD = p ("amp.d"); v.ampS = p ("amp.s"); v.ampR = p ("amp.r");
    v.auxA = p ("aux.a"); v.auxD = p ("aux.d"); v.auxS = p ("aux.s"); v.auxR = p ("aux.r");
    v.auxDest = (int) p ("aux.dest");
    v.auxAmount = p ("aux.amount");

    e.inTrimDb = p ("source.intrim");
    e.voiceMode = (int) p ("voice.mode");
    e.retrigger = p ("voice.retrig") > 0.5f;
    e.unison = p ("voice.unison") > 0.5f;
    e.spreadCents = p ("voice.spread");
    e.colourMode = (int) p ("col.mode");
    e.colourRate = p ("col.rate");
    e.tapeRec = p ("tape.rec") > 0.5f;
    e.tapeFlip = p ("tape.flip") > 0.5f;
    e.chainMix = p ("chain.mix");
    e.outLevelDb = p ("out.level");
    return e;
}

void BrokenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // A host that calls processBlock before prepareToPlay leaves monoIn empty (capacity 0);
    // the chunk loop below would then add 0 to `offset` forever and hang the audio thread.
    // Output silence and return. MIDI is input-only on this path (never written back), so
    // it needs no handling.
    if (monoIn.empty())
    {
        buffer.clear();
        return;
    }

    // lock-free hand-offs from the message thread (no allocation, no locks): a newly
    // loaded sample (loadSampleFile) and the newest Table-mod table (syncModTable). Both
    // run before applyParams, which pushes the engines' pointers to their voices.
    adoptPendingSample();
    {
        const float* table = modTables.acquire();
        engine.setModTable (table);
#if BROKEN_FX
        engineR.setModTable (table);
#endif
    }

    // `events` is reserve()'d once in prepareToPlay (maxNoteEventsPerBlock) and must never
    // grow past that here -- growth would allocate on the audio thread. A block with more
    // note-ons/offs than the cap is a MIDI storm; the overflow is dropped and counted
    // (droppedNoteEvents) rather than silently grown into or silently discarded unaccounted-for.
    const auto pushEvent = [this] (dsp::NoteEvent e)
    {
        if (events.size() >= maxNoteEventsPerBlock)
        {
            droppedNoteEvents.fetch_add (1, std::memory_order_relaxed);
            return;
        }
        events.push_back (e);
    };

    // MIDI is parsed BEFORE applyParams on purpose: gatherParams folds pitchBendNorm into
    // the source transpose, so parsing after would apply every bend one block late.
    // Note events are not consumed until engine.process below, so hoisting is safe.
    events.clear();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            pushEvent ({ meta.samplePosition, true, msg.getNoteNumber(), msg.getFloatVelocity() });
        else if (msg.isNoteOff())
            pushEvent ({ meta.samplePosition, false, msg.getNoteNumber(), 0.0f });
        else if (msg.isPitchWheel())
            pitchBendNorm = ((float) msg.getPitchWheelValue() - 8192.0f) / 8192.0f;
    }

    {
        // same EngineParams struct applied to both channels' engines: identical
        // construction + identical parameters + no note events is what keeps their
        // modulation sample-coherent (DESIGN split spec item 2)
        const auto ep = gatherParams();
        engine.applyParams (ep);
#if BROKEN_FX
        engineR.applyParams (ep);
#endif
    }

    const int inChans  = getTotalNumInputChannels();
    const int outChans = getTotalNumOutputChannels();
    const float bypTarget = p ("bypass") > 0.5f ? 1.0f : 0.0f;
    if (bypassFade >= 1.0f && bypTarget >= 1.0f)
    {
        // fully bypassed: the host input passes through untouched (true per-channel
        // stereo). MIDI parse + applyParams above stay live so the pitch wheel and tape
        // edge state don't go stale; the engine is skipped entirely (the 10 s tail is
        // chopped and notes arriving now are dropped -- documented trade-off).
        for (int ch = inChans; ch < outChans; ++ch)
            buffer.clear (ch, 0, n); // extra outs would otherwise carry stale data
#if BROKEN_FX
        // the bypassed signal carries the same reported latency as the processed one
        // (DSP-NOTES §14a), so toggling bypass never shifts the track in time
        if (! bypassLineL.empty())
        {
            const int passChans = juce::jmin (inChans, outChans);
            if (passChans > 0)
                ringDelay (buffer.getReadPointer (0), buffer.getWritePointer (0), bypassLineL, bypassLinePos, n);
            if (passChans > 1)
                ringDelay (buffer.getReadPointer (1), buffer.getWritePointer (1), bypassLineR, bypassLinePos, n);
            bypassLinePos = (int) (((size_t) bypassLinePos + (size_t) n) % bypassLineL.size());
        }
#endif
        playHeld = p ("play.hold") > 0.5f; // track the latch, push no synthetic notes
        float pk = 0.0f;
        for (int ch = 0; ch < juce::jmin (inChans, outChans); ++ch)
            pk = std::max (pk, buffer.getMagnitude (ch, 0, n));
        outPeak.store (std::max (pk, outPeak.load() * 0.8f));
        return;
    }
    // NOTE: events was cleared and filled from MIDI above, before applyParams. Do not
    // clear it here - that would drop every real note on the floor.

    // PLAY latch -> synthetic note at the root. Pushed into the SAME event list as real
    // MIDI so voice allocation, envelopes and the tuner taps behave identically.
    const bool playNow = p ("play.hold") > 0.5f;
    if (playNow != playHeld)
    {
        playHeld = playNow;
        pushEvent ({ 0, playNow, dsp::SourceEngine::rootNote, 1.0f });
    }

    // monoIn/monoOut are sized ONCE in prepareToPlay and never grown here: growing them on
    // this thread would allocate, which is forbidden on the audio thread (house
    // real-time-safety rules). A host that hands us a block bigger than the samplesPerBlock it declared
    // to prepareToPlay -- offline bounces do this routinely -- is instead split into chunks
    // no larger than that pre-allocated capacity and rendered one chunk at a time, so no
    // buffer ever has to grow regardless of host behaviour (the same chunking pattern used
    // in other ZQ SFX products). `events` is re-sliced per chunk with
    // sample positions shifted to be chunk-relative, mirroring
    // juce::MidiBuffer::addEvents(midi, offset, chunkLen, -offset) for our plain vector.
    const int capacity = (int) monoIn.size();
    jassert (capacity > 0);
    float peak = 0.0f;

    for (int offset = 0; offset < n; offset += capacity)
    {
        const int chunkLen = juce::jmin (capacity, n - offset);

        chunkEvents.clear(); // capacity reserved to maxNoteEventsPerBlock; never reallocates
        for (const auto& e : events)
            if (e.samplePos >= offset && e.samplePos < offset + chunkLen)
                chunkEvents.push_back ({ e.samplePos - offset, e.on, e.note, e.velocity });

#if BROKEN_FX
        // True stereo: channel 0 feeds the L engine, channel 1 feeds the R engine (or a
        // copy of channel 0 for a mono input bus) -- DESIGN split spec item 2.
        for (int i = 0; i < chunkLen; ++i)
        {
            const float l = inChans > 0 ? buffer.getReadPointer (0, offset)[i] : 0.0f;
            const float r = inChans > 1 ? buffer.getReadPointer (1, offset)[i] : l;
            monoIn[(size_t) i]  = l;
            monoInR[(size_t) i] = r;
        }

        // keep the bypass delay lines fed every block (so engaging bypass never reads a
        // stale line) and get this chunk's delayed input for the bypass crossfade
        if (! bypassLineL.empty())
        {
            ringDelay (monoIn.data(),  bypassOutL.data(), bypassLineL, bypassLinePos, chunkLen);
            ringDelay (monoInR.data(), bypassOutR.data(), bypassLineR, bypassLinePos, chunkLen);
            bypassLinePos = (int) (((size_t) bypassLinePos + (size_t) chunkLen) % bypassLineL.size());
        }

        engine.process  (monoIn.data(),  monoOut.data(),  chunkLen,
                         chunkEvents.data(), (int) chunkEvents.size());
        engineR.process (monoInR.data(), monoOutR.data(), chunkLen,
                         chunkEvents.data(), (int) chunkEvents.size());

        for (int i = 0; i < chunkLen; ++i)
            peak = std::max (peak, std::max (std::abs (monoOut[(size_t) i]), std::abs (monoOutR[(size_t) i])));

        if (bypassFade <= 0.0f && bypTarget <= 0.0f)
        {
            // active steady state: plain per-channel copy, bit-exact at the unity null.
            // A mono output bus is the one case that needs an actual blend (still
            // allocation-free: monoOutMix is sized once in prepareToPlay).
            if (outChans >= 2)
            {
                buffer.copyFrom (0, offset, monoOut.data(), chunkLen);
                buffer.copyFrom (1, offset, monoOutR.data(), chunkLen);
            }
            else if (outChans == 1)
            {
                for (int i = 0; i < chunkLen; ++i)
                    monoOutMix[(size_t) i] = 0.5f * (monoOut[(size_t) i] + monoOutR[(size_t) i]);
                buffer.copyFrom (0, offset, monoOutMix.data(), chunkLen);
            }
        }
        else
        {
            // engaging/releasing bypass: crossfade each output channel's OWN wet signal
            // against its OWN input, delayed by the reported latency (bypassOutL/R, filled
            // above) so the two legs of the fade are time-aligned -- keeps L and R
            // independent through the fade too, same one fade trajectory as the
            // instrument build.
            const float startFade = bypassFade;
            float f = startFade;
            for (int ch = 0; ch < outChans; ++ch)
            {
                f = startFade;
                auto* d = buffer.getWritePointer (ch, offset);
                const bool hasIn = ch < inChans;
                const float* delayedIn = ch == 0 ? bypassOutL.data() : bypassOutR.data();
                for (int i = 0; i < chunkLen; ++i)
                {
                    f = bypTarget > f ? std::min (bypTarget, f + bypassFadeStep)
                                      : std::max (bypTarget, f - bypassFadeStep);
                    const float in = hasIn ? delayedIn[i] : 0.0f;
                    const float w = (outChans == 1)
                                      ? 0.5f * (monoOut[(size_t) i] + monoOutR[(size_t) i])
                                      : (ch == 0 ? monoOut[(size_t) i] : monoOutR[(size_t) i]);
                    d[i] = (1.0f - f) * w + f * in;
                }
            }
            bypassFade = f;
        }
#else
        for (int i = 0; i < chunkLen; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < inChans; ++ch)
                s += buffer.getReadPointer (ch, offset)[i];
            monoIn[(size_t) i] = inChans > 0 ? s / (float) inChans : 0.0f;
        }

        engine.process (monoIn.data(), monoOut.data(), chunkLen,
                        chunkEvents.data(), (int) chunkEvents.size());

        for (int i = 0; i < chunkLen; ++i)
            peak = std::max (peak, std::abs (monoOut[(size_t) i]));

        if (bypassFade <= 0.0f && bypTarget <= 0.0f)
        {
            // active steady state: the plain copy, kept verbatim so the unity null stays
            // bit-exact (a 0-weighted blend is NOT guaranteed bit-identical)
            for (int ch = 0; ch < outChans; ++ch)
                buffer.copyFrom (ch, offset, monoOut.data(), chunkLen); // mono chain, duplicated (DESIGN §2)
        }
        else
        {
            // engaging/releasing bypass: crossfade wet against the TRUE per-channel input,
            // read in place before each write. All channels share one fade trajectory.
            const float startFade = bypassFade;
            float f = startFade;
            for (int ch = 0; ch < outChans; ++ch)
            {
                f = startFade;
                auto* d = buffer.getWritePointer (ch, offset);
                const bool hasIn = ch < inChans;
                for (int i = 0; i < chunkLen; ++i)
                {
                    f = bypTarget > f ? std::min (bypTarget, f + bypassFadeStep)
                                      : std::max (bypTarget, f - bypassFadeStep);
                    const float in = hasIn ? d[i] : 0.0f;
                    d[i] = (1.0f - f) * monoOut[(size_t) i] + f * in;
                }
            }
            bypassFade = f;
        }
#endif
    }

    outPeak.store (std::max (peak, outPeak.load() * 0.8f)); // crude ballistic decay for the meter
}

juce::AudioProcessorEditor* BrokenProcessor::createEditor()
{
    return new BrokenEditor (*this);
}

void BrokenProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BrokenProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreFromXml (*xml);
}

void BrokenProcessor::restoreFromXml (juce::XmlElement& xml)
{
#if !BROKEN_FX
    // v0.24 rename: state saved before the product was named "Broken" carries the
    // working-title tag. Accept it, or every earlier session and preset refuses to load.
    // Instrument-only: Broken FX never existed under the old name, so it has no legacy
    // tag to migrate (DESIGN split spec item 1).
    if (xml.hasTagName ("TurboSynth"))
        xml.setTagName (apvts.state.getType().toString());
#endif

    if (xml.hasTagName (apvts.state.getType()))
    {
            apvts.replaceState (juce::ValueTree::fromXml (xml));

            // region params are meaningless without their file: reload the persisted
            // path (path-only by design — field recordings are never embedded).
            // Hosts do NOT guarantee setStateInformation runs on the message thread,
            // and the editor's timers read the display buffer — so the reload is
            // marshalled there rather than racing a repaint. (v0.12 review)
            const auto path = apvts.state.getProperty ("samplePath", juce::String()).toString();
            if (path.isNotEmpty())
            {
                auto doLoad = [this, path]
                {
                    juce::String err;
                    const juce::File f (path);
                    const bool exists = f.existsAsFile();
                    if (! exists || ! loadSampleFile (f, err))
                    {
                        sampleMissing = true;
                        sampleName = f.getFileName()
                                   + (exists ? " (cannot read: " + err + ")" : " (file not found)");
                    }
                };
                if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                    doLoad();
                else
                {
                    // The host may destroy this processor before the message thread gets
                    // to the queued reload. The lambda therefore holds the liveness token,
                    // not a bare `this`: the destructor marks it dead under its lock.
                    juce::MessageManager::callAsync ([token = liveness, doLoad]
                    {
                        const juce::ScopedLock sl (token->lock);
                        if (token->alive)
                            doLoad();
                    });
                }
            }
        }
}

bool BrokenProcessor::loadSnapshotJson (const juce::var& parsed, juce::String& errorOut)
{
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) { errorOut = "snapshot root is not a JSON object"; return false; }

    auto paramsVar = obj->getProperty ("params");
    auto* paramsObj = paramsVar.getDynamicObject();
    if (paramsObj == nullptr) { errorOut = "snapshot has no \"params\" object"; return false; }

    // validate every id BEFORE touching anything, so a bad file never half-applies a reset
    for (const auto& kv : paramsObj->getProperties())
        if (apvts.getParameter (kv.name.toString()) == nullptr)
        { errorOut = "unknown parameter id: " + kv.name.toString(); return false; }

    // "reset": true (the 00 Init presets): every parameter back to its declared Params.h
    // default first, then the wrapper's context default, then the listed ids below. Every
    // other preset applies only the ids it lists (a sound design layered on the current
    // state). The sample region markers are exempt: like the sample itself they belong to
    // the loaded file, and no factory preset may move them (broken_cli --preset-check).
    if ((bool) obj->getProperty ("reset"))
    {
        for (auto* param : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                if (rp->paramID == "sample.regstart" || rp->paramID == "sample.regend")
                    continue;
                rp->setValueNotifyingHost (rp->getDefaultValue());
            }
        applyContextDefaults();
    }

    for (const auto& kv : paramsObj->getProperties())
    {
        const auto id = kv.name.toString();
        auto* param = apvts.getParameter (id);
        if (param == nullptr) { errorOut = "unknown parameter id: " + id; return false; }
        // snapshot values are real-world units; convert through the parameter's own
        // range, CLAMPED — convertTo0to1 doesn't clamp, so an out-of-range value in a
        // hand-edited preset would store a normalized value outside [0,1]. (v0.12)
        const auto& range = param->getNormalisableRange();
        const float v = juce::jlimit (range.start, range.end, (float) (double) kv.value);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }
    return true;
}

juce::var BrokenProcessor::snapshotToJson() const
{
    auto paramsObj = new juce::DynamicObject();
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            paramsObj->setProperty (rp->paramID, rp->convertFrom0to1 (rp->getValue()));

    auto root = new juce::DynamicObject();
    root->setProperty ("params", juce::var (paramsObj));
    return juce::var (root);
}

bool BrokenProcessor::loadSampleFile (const juce::File& file, juce::String& errorOut)
{
    // Everything slow (open, decode, mono-sum) happens here, on the calling thread, with
    // NO lock held: the audio thread keeps playing the old buffer meanwhile. Through v0.36.0
    // this read the file under the callback lock with processing suspended, so a large
    // file stalled the audio callback (and suspendProcessing made the host output silence).
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { errorOut = "cannot read " + file.getFullPathName(); return false; }

    auto slot = std::make_unique<SampleSlot>();
    {
        const int numCh = (int) reader->numChannels;
        const int len = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> tmp (numCh, len);
        reader->read (&tmp, 0, len, 0, true, true);
        slot->data.assign ((size_t) len, 0.0f);
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < len; ++i)
                slot->data[(size_t) i] += tmp.getReadPointer (ch)[i] / (float) numCh;
    }
    slot->sr = reader->sampleRate;
    slot->seq = nextSampleSeq++;

    // Publish. Whatever the exchange hands back was never adopted by the audio thread
    // (it only takes slots out of pendingSample), so it can be freed right here.
    auto* published = slot.get();
    sampleSlots.push_back (std::move (slot));
    if (auto* stale = pendingSample.exchange (published, std::memory_order_acq_rel))
        sampleSlots.erase (std::remove_if (sampleSlots.begin(), sampleSlots.end(),
                                           [stale] (const std::unique_ptr<SampleSlot>& s) { return s.get() == stale; }),
                           sampleSlots.end());
    displaySample = published;
    collectRetiredSamples();

    sampleName = file.getFileName();
    sampleMissing = false;
    apvts.state.setProperty ("samplePath", file.getFullPathName(), nullptr);
    return true;
}

void BrokenProcessor::randomizeParams()
{
    lastRandomUndoState = apvts.copyState();
    hasRandomUndoState = true;

    std::set<juce::String> skip = { "source.mode", "out.level", "bypass",
                                    "play.hold", "tape.rec", "tape.flip" };
    // hand-drawn content stays, like the sample; ids via the Params.h builders (a
    // "ws.c" prefix test would swallow ws.curve — the v0.20 lesson)
    for (int k = 0; k < params::drawPointCount; ++k)  skip.insert (params::drawPointId (k + 1));
    for (int k = 0; k < params::curvePointCount; ++k) skip.insert (params::curvePointId (k + 1));

    // reduced draw ranges: regeneration params capped, audibility floored (§12b)
    struct Cap { const char* id; float lo, hi; };
    constexpr Cap caps[] = {
        { "res.fb",     -0.85f,  0.85f    },
        { "dly.fb",      0.0f,   0.63f    },
        { "ws.drive",    0.0f,   30.0f    },
        { "amp.a",       0.0f,   2.0f     },
        { "flt.cutoff",  200.0f, 20000.0f },
    };

    juce::Random rng;
    for (auto* param : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (rp == nullptr || skip.count (rp->paramID) > 0)
            continue;

        const auto& range = rp->getNormalisableRange();
        float lo = range.start, hi = range.end;
        for (const auto& c : caps)
            if (rp->paramID == c.id) { lo = std::max (lo, c.lo); hi = std::min (hi, c.hi); }

        const float value = range.snapToLegalValue (lo + rng.nextFloat() * (hi - lo));
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (range.convertTo0to1 (value));
        rp->endChangeGesture();
    }
}

void BrokenProcessor::undoRandomize()
{
    if (! hasRandomUndoState) return;
    // copy: replaceState adopts the tree, and a second undo must still work
    apvts.replaceState (lastRandomUndoState.createCopy());
}

bool BrokenProcessor::applyTuneLock()
{
    std::vector<float> window ((size_t) dsp::PitchDetector::windowSize);
    copyTunerTap (false, window.data(), dsp::PitchDetector::windowSize);

    dsp::PitchDetector det;
    det.prepare (currentSampleRate);
    const auto r = det.detect (window.data());
    if (r.clarity < dsp::PitchDetector::clarityGate || r.hz <= 0.0f)
        return false;

    int note = 0; float cents = 0.0f;
    dsp::PitchDetector::centsFromHz (r.hz, note, cents);

    // shift by -cents to land on the nearest note; nearest-note offsets are <= 50 by
    // construction, but a live FINE value can push the total past the knob range —
    // roll whole semitones into PITCH first, remainder into FINE
    auto* fineP  = apvts.getParameter ("source.finecents");
    auto* pitchP = apvts.getParameter ("source.pitch");
    const float curFine  = fineP->convertFrom0to1 (fineP->getValue());
    const float curPitch = pitchP->convertFrom0to1 (pitchP->getValue());

    float totalCents = curFine - cents;
    float pitchAdj = 0.0f;
    while (totalCents > 50.0f)  { totalCents -= 100.0f; pitchAdj += 1.0f; }
    while (totalCents < -50.0f) { totalCents += 100.0f; pitchAdj -= 1.0f; }

    auto setReal = [] (juce::RangedAudioParameter* p, float v)
    {
        p->beginChangeGesture();
        // clamp: convertTo0to1 doesn't, so `--set flt.poles=99` would store an out-of-range
        // normalized value that the DSP then reads as garbage (v0.12 review)
        const auto& range = p->getNormalisableRange();
        v = juce::jlimit (range.start, range.end, v);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    };
    if (pitchAdj != 0.0f) setReal (pitchP, curPitch + pitchAdj);
    setReal (fineP, std::round (totalCents));
    return true;
}

// PresetManager needs the complete processor, so its two methods live here.
bool PresetManager::load (int index, juce::String& errorOut)
{
    if (index < 0 || index >= (int) entries.size()) { errorOut = "no such preset"; return false; }
    const auto& e = entries[(size_t) index];

    juce::String text;
    if (e.isUser)
        text = e.file.loadFileAsString();
    else
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource (
                BinaryData::namedResourceList[e.binaryIndex], size))
            text = juce::String::fromUTF8 (data, size);
    }
    if (text.isEmpty()) { errorOut = "preset is empty: " + e.name; return false; }

    if (! processor.loadSnapshotJson (juce::JSON::parse (text), errorOut))
        return false;

    currentIndex = index;
    return true;
}

bool PresetManager::saveUser (const juce::String& name, juce::String& errorOut)
{
    auto dir = userDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
    { errorOut = "cannot create " + dir.getFullPathName(); return false; }

    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) { errorOut = "please give the preset a name"; return false; }

    auto f = dir.getChildFile (safe + ".json");
    if (! f.replaceWithText (juce::JSON::toString (processor.snapshotToJson())))
    { errorOut = "cannot write " + f.getFullPathName(); return false; }

    rescan();
    currentIndex = indexOf (safe);
    return true;
}

bool PresetManager::renameUser (int index, const juce::String& newName, juce::String& errorOut)
{
    if (index < 0 || index >= (int) entries.size() || ! entries[(size_t) index].isUser)
    { errorOut = "only USER presets can be renamed"; return false; }
    auto safe = juce::File::createLegalFileName (newName.trim());
    if (safe.isEmpty()) { errorOut = "please give the preset a name"; return false; }
    auto& e = entries[(size_t) index];
    auto target = e.file.getSiblingFile (safe + ".json");
    if (target == e.file) return true;
    if (target.existsAsFile()) { errorOut = "a preset named \"" + safe + "\" already exists"; return false; }
    if (! e.file.moveFileTo (target)) { errorOut = "cannot rename " + e.file.getFileName(); return false; }
    const bool wasCurrent = currentIndex == index;
    rescan();
    if (wasCurrent) currentIndex = indexOf (safe);
    return true;
}

bool PresetManager::deleteUser (int index, juce::String& errorOut)
{
    if (index < 0 || index >= (int) entries.size() || ! entries[(size_t) index].isUser)
    { errorOut = "only USER presets can be deleted"; return false; }
    // moveToTrash, not deleteFile: a preset is someone's sound design (recoverable)
    if (! entries[(size_t) index].file.moveToTrash())
    { errorOut = "cannot delete " + entries[(size_t) index].file.getFileName(); return false; }
    const auto currentName = currentIndex >= 0 && currentIndex < (int) entries.size()
                                 ? entries[(size_t) currentIndex].name : juce::String();
    rescan();
    currentIndex = currentName.isNotEmpty() ? indexOf (currentName) : -1;
    return true;
}

bool PresetManager::overwriteUser (int index, juce::String& errorOut)
{
    if (index < 0 || index >= (int) entries.size() || ! entries[(size_t) index].isUser)
    { errorOut = "only USER presets can be overwritten"; return false; }
    return saveUser (entries[(size_t) index].name, errorOut); // same name = same file
}

bool BrokenProcessor::saveTapeToFile (const juce::File& file, juce::String& errorOut)
{
    std::vector<float> copy;
#if BROKEN_FX
    std::vector<float> copyR;
    bool stereo = false;
#endif
    double sr = 48000.0;
    {
        // Copy the latest COMPLETE take (no FLIP required, v0.33) WITHOUT the callback
        // lock (up to Tape maxSeconds of audio: ~11.5 MB mono at 48 kHz, ~23 MB for the FX
        // stereo pair). TapeBuffer's seqlock tells us whether a REC/FLIP on the audio thread
        // changed the takes while we copied; if so, copy again. Through v0.36.0 this copy ran
        // under the callback lock and stalled the audio thread for its whole length.
        auto& tape = engine.getTape();
#if BROKEN_FX
        auto& tapeR = engineR.getTape();
#endif
        bool consistent = false;
        for (int attempt = 0; attempt < 50 && ! consistent; ++attempt)
        {
            const auto g = tape.readBegin();
#if BROKEN_FX
            const auto gR = tapeR.readBegin();
#endif
            if ((g & 1u) != 0
#if BROKEN_FX
                || (gR & 1u) != 0
#endif
               )
            {
                juce::Thread::sleep (1); // a state change is in flight on the audio thread
                continue;
            }
            const auto len = tape.copyLatestTo (copy);
#if BROKEN_FX
            // item 5: each FX engine records its own channel's output into its own
            // TapeBuffer (Engine.h), started/stopped by the identical tape.rec param applied
            // to both engines the same block, so they record in lockstep. Equal latest-take
            // lengths therefore mean a genuine matched stereo pair; a mismatch falls back to
            // writing L alone as mono rather than guessing an alignment between two takes
            // of different length.
            const auto lenR = tapeR.copyLatestTo (copyR);
            stereo = lenR == len && len > 0;
            consistent = tape.readValidate (g) && tapeR.readValidate (gR);
#else
            consistent = tape.readValidate (g);
#endif
        }
        if (! consistent) { errorOut = "tape changed while saving - try again"; return false; }
        if (copy.empty()) { errorOut = "tape is empty - press REC first"; return false; }
        sr = tape.sampleRateOfContent();
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk()) { errorOut = "cannot write " + file.getFullPathName(); return false; }
#if BROKEN_FX
    const unsigned int numChans = stereo ? 2u : 1u;
#else
    const unsigned int numChans = 1u;
#endif
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), sr, numChans, 24, {}, 0));
    if (writer == nullptr) { errorOut = "cannot create WAV writer"; return false; }

#if BROKEN_FX
    if (stereo)
    {
        const float* chans[] = { copy.data(), copyR.data() };
        if (! writer->writeFromFloatArrays (chans, 2, (int) copy.size()))
        { errorOut = "write failed"; return false; }
        return true;
    }
#endif
    const float* chans[] = { copy.data() };
    if (! writer->writeFromFloatArrays (chans, 1, (int) copy.size()))
    { errorOut = "write failed"; return false; }
    return true;
}
} // namespace broken

// Factory for the plugin targets; broken_cli instantiates the processor directly instead.
#if defined (JucePlugin_Name)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(); // prototype: the flags now reach every target
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new broken::BrokenProcessor();
}
#endif
