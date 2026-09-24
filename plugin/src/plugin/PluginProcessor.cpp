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
}

BrokenProcessor::BrokenProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Broken", params::createLayout())
{
    for (auto* id : allIds)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr); // Params.h and allIds[] must never drift apart
        cached[id] = raw;
    }
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    jassert (bypassParam != nullptr);

    // Context default (v0.33): a DAW instance is FX-shaped, so it opens listening to the
    // track (Input); standalone keeps Sample. Explicitly VST3/AU only — broken_cli constructs
    // with wrapperType_Undefined and every gate baseline assumes the Sample default.
    // Session restore and preset loads arrive later and override this.
    if (wrapperType == wrapperType_VST3 || wrapperType == wrapperType_AudioUnit)
        if (auto* sm = apvts.getParameter ("source.mode"))
            sm->setValueNotifyingHost (sm->convertTo0to1 (4.0f)); // 4 = Input

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
}

void BrokenProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    if (! sampleBuf.empty())
        engine.setSampleData (sampleBuf.data(), sampleBuf.size(), sampleFileSr);
    monoIn.resize ((size_t) samplesPerBlock);
    monoOut.resize ((size_t) samplesPerBlock);
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

    engine.applyParams (gatherParams());

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
    // this thread would allocate, which is forbidden on the audio thread (../CLAUDE.md
    // section 4). A host that hands us a block bigger than the samplesPerBlock it declared
    // to prepareToPlay -- offline bounces do this routinely -- is instead split into chunks
    // no larger than that pre-allocated capacity and rendered one chunk at a time, so no
    // buffer ever has to grow regardless of host behaviour (same pattern as Worldizer's
    // processChunk / Reality Reborn's renderChunk). `events` is re-sliced per chunk with
    // sample positions shifted to be chunk-relative, mirroring
    // juce::MidiBuffer::addEvents(midi, offset, chunkLen, -offset) for our plain vector.
    const int capacity = (int) monoIn.size();
    jassert (capacity > 0);
    float peak = 0.0f;

    for (int offset = 0; offset < n; offset += capacity)
    {
        const int chunkLen = juce::jmin (capacity, n - offset);

        for (int i = 0; i < chunkLen; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < inChans; ++ch)
                s += buffer.getReadPointer (ch, offset)[i];
            monoIn[(size_t) i] = inChans > 0 ? s / (float) inChans : 0.0f;
        }

        chunkEvents.clear(); // capacity reserved to maxNoteEventsPerBlock; never reallocates
        for (const auto& e : events)
            if (e.samplePos >= offset && e.samplePos < offset + chunkLen)
                chunkEvents.push_back ({ e.samplePos - offset, e.on, e.note, e.velocity });

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
    // v0.24 rename: state saved before the product was named "Broken" carries the
    // working-title tag. Accept it, or every earlier session and preset refuses to load.
    if (xml.hasTagName ("TurboSynth"))
        xml.setTagName (apvts.state.getType().toString());

    if (xml.hasTagName (apvts.state.getType()))
    {
            apvts.replaceState (juce::ValueTree::fromXml (xml));

            // region params are meaningless without their file: reload the persisted
            // path (path-only by design — field recordings are never embedded).
            // Hosts do NOT guarantee setStateInformation runs on the message thread,
            // and the editor's timers read sampleBuf — so the reload is marshalled
            // there rather than racing a repaint. (v0.12 review)
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
                    juce::MessageManager::callAsync (doLoad);
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
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { errorOut = "cannot read " + file.getFullPathName(); return false; }

    // the audio thread reads sampleBuf through raw pointers — pause it for the swap
    const juce::ScopedLock sl (getCallbackLock());
    suspendProcessing (true);

    juce::AudioBuffer<float> tmp ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&tmp, 0, (int) reader->lengthInSamples, 0, true, true);

    sampleBuf.assign ((size_t) tmp.getNumSamples(), 0.0f);
    for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
        for (int i = 0; i < tmp.getNumSamples(); ++i)
            sampleBuf[(size_t) i] += tmp.getReadPointer (ch)[i] / (float) tmp.getNumChannels();

    sampleFileSr = reader->sampleRate;
    sampleName = file.getFileName();
    sampleMissing = false;
    engine.setSampleData (sampleBuf.data(), sampleBuf.size(), sampleFileSr);
    suspendProcessing (false);
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
    double sr = 48000.0;
    {
        // a FLIP on the audio thread would swap the active buffer under us mid-read;
        // copy under the callback lock (~2 MB worst case), write outside it
        const juce::ScopedLock sl (getCallbackLock());
        auto& tape = engine.getTape();
        const auto len = tape.latestLength(); // latest COMPLETE take; no FLIP required (v0.33)
        if (len == 0) { errorOut = "tape is empty - press REC first"; return false; }
        copy.assign (tape.latestData(), tape.latestData() + len);
        sr = tape.sampleRateOfContent();
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk()) { errorOut = "cannot write " + file.getFullPathName(); return false; }
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), sr, 1, 24, {}, 0));
    if (writer == nullptr) { errorOut = "cannot create WAV writer"; return false; }

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
