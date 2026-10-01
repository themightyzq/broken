// broken_source_default_check: headless console gate for the instrument build's
// (BROKEN_FX=0) context default. A fresh instance created as a VST3 / AU / Standalone
// must open on white noise so a note is audible without loading anything; an Undefined
// wrapper (broken_cli, gate tools) must keep the declared Params.h defaults, because every
// gate baseline assumes them. Wrapper types are injected the way the real wrappers do it,
// via AudioProcessor::setTypeOfNextNewPlugin (thread-local and sticky, so reset after use).
// Since 2026-10-01 it also gates the "00 Init" preset as a full reset: every parameter
// back to its Params.h default (sample region excepted), then the same wrapper context
// default as a fresh instance; other presets still apply only the ids they list.
// Exit 0 on pass, 1 on any failure.

#include <cmath>
#include <iostream>
#include <memory>
#include <type_traits>
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/PluginProcessor.h"

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (! ok) ++failures;
}

std::unique_ptr<broken::BrokenProcessor> make (juce::AudioProcessor::WrapperType type)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (type);
    auto p = std::make_unique<broken::BrokenProcessor>(); // heap: too big for a 1 MB thread stack
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    return p;
}

float real (broken::BrokenProcessor& p, const char* id)
{
    auto* prm = p.apvts.getParameter (id);
    return prm->convertFrom0to1 (prm->getValue());
}

// holds middle C for `seconds` with silent input; returns the mono (L) output
std::vector<float> renderNote (broken::BrokenProcessor& proc, double seconds)
{
    const double sr = 48000.0;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    const int total = (int) (sr * seconds);
    std::vector<float> out;
    out.reserve ((size_t) total);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        buf.clear();
        midi.clear();
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        proc.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
    }
    return out;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using AP = juce::AudioProcessor;

    const struct { AP::WrapperType type; const char* name; } hosted[] = {
        { AP::wrapperType_VST3,       "VST3" },
        { AP::wrapperType_AudioUnit,  "AU" },
        { AP::wrapperType_Standalone, "Standalone" },
    };
    for (const auto& c : hosted)
    {
        auto p = make (c.type);
        const juce::String n (c.name);
        check (std::lround (real (*p, "source.mode")) == 3, "fresh " + n + " instance: source.mode == Noise (3)");
        check (std::lround (real (*p, "noise.amp")) == 100,  "fresh " + n + " instance: noise.amp == 100");
        check (std::lround (real (*p, "noise.phase")) == 100, "fresh " + n + " instance: noise.phase == 100");
    }

    {
        auto p = make (AP::wrapperType_Undefined);
        check (std::lround (real (*p, "source.mode")) == 0, "Undefined wrapper keeps the declared source.mode (Sample, 0)");
        check (std::lround (real (*p, "noise.amp")) == 25 && std::lround (real (*p, "noise.phase")) == 25,
               "Undefined wrapper keeps the declared noise.amp/phase (25/25)");
    }

    // a note on a fresh hosted instance is audible AND white: first-difference energy of white
    // noise is 2x the signal energy (lag-1 correlation ~ 0); a pitched sine gives << 1.
    {
        auto p = make (AP::wrapperType_VST3);
        const auto y = renderNote (*p, 1.0);
        double e = 0.0, d = 0.0;
        for (size_t i = 24000; i < y.size(); ++i)
        {
            e += (double) y[i] * y[i];
            const double df = (double) y[i] - (double) y[i - 1];
            d += df * df;
        }
        const double rms = std::sqrt (e / (double) (y.size() - 24000));
        const double ratio = e > 0.0 ? d / e : 0.0;
        std::cout << "  note render: rms=" << rms << " diffEnergy/energy=" << ratio << " (white ~ 2)\n";
        check (rms > 1.0e-3, "fresh VST3 instance: a held note is audible (rms > 1e-3)");
        check (ratio > 1.2, "fresh VST3 instance: the note is broadband noise, not a pitched tone (diff/energy > 1.2; a sine is ~0.01)");
    }

    // "00 Init" is a full reset (2026-10-01). Before, it applied only chain.mix and bypass,
    // so everything else stayed wherever the previous sound left it.
    auto setRealValue = [] (broken::BrokenProcessor& p, const char* id, float v)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    using Case = std::remove_cv_t<std::remove_reference_t<decltype (hosted[0])>>;
    const Case initCases[] = { hosted[0], { AP::wrapperType_Undefined, "Undefined" } };
    for (const auto& c : initCases)
    {
        auto p = make (c.type);
        const juce::String n (c.name);
        setRealValue (*p, "source.mode", 2.0f); // Osc
        setRealValue (*p, "noise.amp", 10.0f);
        setRealValue (*p, "ws.drive", 30.0f);
        setRealValue (*p, "flt.cutoff", 900.0f);
        setRealValue (*p, "mod.on", 1.0f);
        setRealValue (*p, "osc.h05", 77.0f);
        setRealValue (*p, "osc.d010", -0.5f);
        setRealValue (*p, "amp.r", 3.0f);
        setRealValue (*p, "chain.mix", 0.4f);
        setRealValue (*p, "sample.regstart", 0.3f);

        juce::String err;
        const int idx = p->presets.indexOf ("00-init");
        const bool ok = idx >= 0 && p->presets.load (idx, err);
        check (ok, n + ": 00-init loads" + (ok ? juce::String() : ": " + err));

        const bool hostedCtx = c.type != AP::wrapperType_Undefined;
        int wrong = 0;
        for (auto* prm : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                const auto& id = rp->paramID;
                if (id == "sample.regstart" || id == "sample.regend") continue;
                if (hostedCtx && (id == "source.mode" || id == "noise.amp" || id == "noise.phase")) continue;
                if (std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-6f)
                {
                    ++wrong;
                    std::cout << "  " << n << ": not at default after Init: " << id << "\n";
                }
            }
        check (wrong == 0, n + ": after 00 Init every parameter is at its Params.h default");
        if (hostedCtx)
            check (std::lround (real (*p, "source.mode")) == 3 && std::lround (real (*p, "noise.amp")) == 100
                       && std::lround (real (*p, "noise.phase")) == 100,
                   n + ": after 00 Init the source is the fresh-instance white noise (Noise, 100/100)");
        else
            check (std::lround (real (*p, "source.mode")) == 0 && std::lround (real (*p, "noise.amp")) == 25,
                   n + ": after 00 Init the source is the declared default (Sample, 25/25)");
        check (std::abs (real (*p, "sample.regstart") - 0.3f) < 1.0e-4f, n + ": 00 Init leaves the sample region alone");

        // a normal preset is still layered: osc.h05 is not in 01-start-here, so it stays
        setRealValue (*p, "osc.h05", 77.0f);
        const int idx1 = p->presets.indexOf ("01-start-here");
        const bool ok1 = idx1 >= 0 && p->presets.load (idx1, err);
        check (ok1 && std::abs (real (*p, "osc.h05") - 77.0f) < 1.0e-3f,
               n + ": a normal preset still applies only the ids it lists");
    }

    std::cout << "broken_source_default_check: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
