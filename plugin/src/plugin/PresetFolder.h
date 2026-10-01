#pragma once
// User-preset folder location and the one-time migration from the old location.
// Header-only and juce_core-only (no APVTS, no BinaryData) so broken_preset_tests can exercise
// all of it against temp directories without linking the plugin. Same shape as LFlOw's
// source/presets/PresetFolder.h.

#include <juce_core/juce_core.h>

namespace broken::presetfolder
{
constexpr auto kExtension = ".json";

// Dropped into the new folder once a migration has run, so a preset the user later deletes
// there is not resurrected from the legacy folder on the next launch. Not a *.json file, so
// the preset scan never lists it.
constexpr auto kMarkerFileName = ".migrated-from-legacy-location";

// The product folder name: "Broken" for the instrument, "Broken FX" for the effect.
inline juce::String productFolderName()
{
   #if BROKEN_FX
    return "Broken FX";
   #else
    return "Broken";
   #endif
}

enum class Platform { Mac, Windows, Linux };

inline constexpr Platform currentPlatform() noexcept
{
   #if JUCE_MAC
    return Platform::Mac;
   #elif JUCE_WINDOWS
    return Platform::Windows;
   #else
    return Platform::Linux;
   #endif
}

// The house location on macOS (~/Library/Audio/Presets/ZQ SFX/<product>), and each OS's
// conventional per-user application-data folder elsewhere: %APPDATA% on Windows, ~/.config
// (or $XDG_CONFIG_HOME) on Linux. `appData` is juce::File::userApplicationDataDirectory; both
// roots are parameters so tests can point at temp directories.
inline juce::File userPresetDir (Platform platform, const juce::File& home, const juce::File& appData,
                                 const juce::String& product)
{
    if (platform == Platform::Mac)
        return home.getChildFile ("Library/Audio/Presets/ZQ SFX").getChildFile (product);

    return appData.getChildFile ("ZQ SFX").getChildFile (product);
}

// Where every build before the OS-aware fix kept user presets, on every OS. On macOS this is
// the current location, so nothing migrates there.
inline juce::File legacyUserPresetDir (const juce::File& home, const juce::String& product)
{
    return home.getChildFile ("Library/Audio/Presets/ZQ SFX").getChildFile (product);
}

// Copies every preset in legacyDir that does not already exist (by file name) in newDir.
// Copies, never moves: the legacy folder is left untouched, so an older build keeps working
// and a failed copy loses nothing. A file already in newDir always wins and is never
// overwritten. Idempotent through the marker file, which is written only after every copy
// succeeded, so a partial failure is retried (just the missing files) on the next launch.
//
// Does nothing, and writes nothing, when legacyDir is absent (a fresh install) or is the same
// folder as newDir (macOS). Returns the number of files copied.
inline int migrateLegacyUserPresets (const juce::File& legacyDir, const juce::File& newDir)
{
    if (legacyDir == newDir || ! legacyDir.isDirectory())
        return 0;

    const auto marker = newDir.getChildFile (kMarkerFileName);
    if (marker.existsAsFile())
        return 0;

    if (! newDir.createDirectory().wasOk())
        return 0; // destination unusable: leave no marker, try again next launch

    int copied = 0;
    bool allOk = true;
    for (const auto& entry : legacyDir.findChildFiles (juce::File::findFiles, false,
                                                       juce::String ("*") + kExtension))
    {
        const auto target = newDir.getChildFile (entry.getFileName());
        if (target.exists())
            continue;

        if (entry.copyFileTo (target))
            ++copied;
        else
            allOk = false;
    }

    if (allOk)
        marker.replaceWithText ("Presets copied from " + legacyDir.getFullPathName() + "\n");

    return copied;
}
} // namespace broken::presetfolder
