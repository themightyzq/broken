#pragma once
// Preset browsing (docs/PANEL.md "Preset bar"). Factory presets are compiled into the
// binary so they travel inside the AU/VST3 bundle; user presets are the same JSON format
// written to ~/Library/Audio/Presets/ZQ SFX/Broken.
//
// A preset is a SOUND DESIGN, not a sample: loading one only applies the parameter ids
// present in the file, so it never disturbs the loaded sample or its path — and the
// factory presets omit the region markers, leaving your selection alone.

#include <algorithm>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <BinaryData.h>

namespace broken
{
class BrokenProcessor;

class PresetManager
{
public:
    struct Entry
    {
        juce::String name;      // file stem, e.g. "10-death-vocal"
        bool isUser = false;
        int binaryIndex = -1;   // factory: index into BinaryData
        juce::File file;        // user: the file on disk
    };

    explicit PresetManager (BrokenProcessor& p) : processor (p) { rescan(); }

    static juce::File userDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Presets/ZQ SFX/Broken");
    }

    void rescan()
    {
        entries.clear();

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String original (BinaryData::originalFilenames[i]);
            if (! original.endsWithIgnoreCase (".json")) continue;
            entries.push_back ({ original.dropLastCharacters (5), false, i, {} });
        }
        std::sort (entries.begin(), entries.end(),
                   [] (const Entry& a, const Entry& b) { return a.name < b.name; });

        auto dir = userDirectory();
        if (dir.isDirectory())
        {
            auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
            files.sort();
            for (const auto& f : files)
                entries.push_back ({ f.getFileNameWithoutExtension(), true, -1, f });
        }
    }

    const std::vector<Entry>& getEntries() const { return entries; }
    int getCurrentIndex() const { return currentIndex; }

    bool load (int index, juce::String& errorOut);
    bool saveUser (const juce::String& name, juce::String& errorOut);

    // USER-preset management (v0.34); each refuses factory entries.
    bool renameUser (int index, const juce::String& newName, juce::String& errorOut);
    bool deleteUser (int index, juce::String& errorOut);
    bool overwriteUser (int index, juce::String& errorOut); // current sound over the entry

    bool step (int delta, juce::String& errorOut)
    {
        if (entries.empty()) return false;
        const int n = (int) entries.size();
        const int idx = currentIndex < 0 ? 0 : ((currentIndex + delta) % n + n) % n;
        return load (idx, errorOut);
    }

    int indexOf (const juce::String& name) const // used by broken_cli --preset
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name.equalsIgnoreCase (name)) return (int) i;
        return -1;
    }

private:
    BrokenProcessor& processor;
    std::vector<Entry> entries;
    int currentIndex = -1;
};
} // namespace broken
