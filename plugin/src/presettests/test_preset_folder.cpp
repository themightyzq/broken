#include <catch2/catch_test_macros.hpp>

#include "plugin/PresetFolder.h"

#include <juce_core/juce_core.h>

using namespace broken::presetfolder;

// User-preset folder per OS and the one-time migration from the old location (2026-10-01).
// Before this, PresetManager::userDirectory() returned ~/Library/Audio/Presets/ZQ SFX/<product>
// on every OS, a macOS-style path that is wrong on Windows and Linux. Built twice: as
// broken_preset_tests (BROKEN_FX=0) and broken_fx_preset_tests (BROKEN_FX=1), so both
// products' folder names are checked. Everything runs against temp directories; the real
// per-user folders are never touched.

namespace
{
    struct TempRoot
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("broken_preset_folder_test_" + juce::Uuid().toString());

        TempRoot() { dir.deleteRecursively(); dir.createDirectory(); }
        ~TempRoot() { dir.deleteRecursively(); }
    };

    juce::File makePreset (const juce::File& dir, const juce::String& name, const juce::String& body)
    {
        dir.createDirectory();
        auto f = dir.getChildFile (name + kExtension);
        REQUIRE (f.replaceWithText (body));
        return f;
    }

    juce::String contentsOf (const juce::File& dir, const juce::String& name)
    {
        return dir.getChildFile (name + kExtension).loadFileAsString();
    }
}

// ---- folder ---------------------------------------------------------------------------------

TEST_CASE ("folder: this build's product folder name", "[presets][folder]")
{
   #if BROKEN_FX
    REQUIRE (productFolderName() == "Broken FX");
   #else
    REQUIRE (productFolderName() == "Broken");
   #endif
}

TEST_CASE ("folder: macOS uses the house location, Windows and Linux the per-user app-data folder",
           "[presets][folder]")
{
    TempRoot root;
    const auto home    = root.dir.getChildFile ("home");
    const auto appData = root.dir.getChildFile ("appdata");

    for (const juce::String product : { "Broken", "Broken FX" })
    {
        REQUIRE (userPresetDir (Platform::Mac, home, appData, product)
                 == home.getChildFile ("Library/Audio/Presets/ZQ SFX/" + product));

        const auto expected = appData.getChildFile ("ZQ SFX").getChildFile (product);
        REQUIRE (userPresetDir (Platform::Windows, home, appData, product) == expected);
        REQUIRE (userPresetDir (Platform::Linux, home, appData, product) == expected);
    }

    // the two products never share a folder
    REQUIRE (userPresetDir (Platform::Windows, home, appData, "Broken")
             != userPresetDir (Platform::Windows, home, appData, "Broken FX"));
}

TEST_CASE ("folder: the legacy location is the macOS location, so nothing migrates there",
           "[presets][folder]")
{
    TempRoot root;
    const auto home    = root.dir.getChildFile ("home");
    const auto appData = root.dir.getChildFile ("appdata");
    const auto product = productFolderName();

    REQUIRE (legacyUserPresetDir (home, product) == userPresetDir (Platform::Mac, home, appData, product));
    REQUIRE (legacyUserPresetDir (home, product) != userPresetDir (Platform::Windows, home, appData, product));
    REQUIRE (legacyUserPresetDir (home, product) != userPresetDir (Platform::Linux, home, appData, product));
}

TEST_CASE ("folder: the running OS is the one this build targets", "[presets][folder]")
{
   #if JUCE_MAC
    REQUIRE (currentPlatform() == Platform::Mac);
   #elif JUCE_WINDOWS
    REQUIRE (currentPlatform() == Platform::Windows);
   #else
    REQUIRE (currentPlatform() == Platform::Linux);
   #endif
}

// ---- migration ------------------------------------------------------------------------------

TEST_CASE ("migration: a missing legacy folder copies nothing and creates nothing", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (! fresh.exists());
}

TEST_CASE ("migration: the same folder on both sides is left alone (macOS)", "[presets][migration]")
{
    TempRoot root;
    const auto dir = root.dir.getChildFile ("same");
    makePreset (dir, "A", "body A");

    REQUIRE (migrateLegacyUserPresets (dir, dir) == 0);
    REQUIRE (! dir.getChildFile (kMarkerFileName).exists());
    REQUIRE (contentsOf (dir, "A") == "body A");
}

TEST_CASE ("migration: presets are copied, the legacy folder is untouched", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "{\"params\":{}}");
    makePreset (legacy, "B", "body B");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 2);

    REQUIRE (contentsOf (fresh, "A") == "{\"params\":{}}");
    REQUIRE (contentsOf (fresh, "B") == "body B");
    REQUIRE (fresh.getChildFile (kMarkerFileName).existsAsFile());

    // copy, not move
    REQUIRE (contentsOf (legacy, "A") == "{\"params\":{}}");
    REQUIRE (contentsOf (legacy, "B") == "body B");
}

TEST_CASE ("migration: a preset already in the new folder is never overwritten", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "Same", "OLD content");
    makePreset (legacy, "Only Old", "old only");
    makePreset (fresh, "Same", "NEW content the user kept");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);

    REQUIRE (contentsOf (fresh, "Same") == "NEW content the user kept");
    REQUIRE (contentsOf (fresh, "Only Old") == "old only");
}

TEST_CASE ("migration: runs once; a preset deleted afterwards is not resurrected", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "body A");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);
    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0); // marker present: idempotent

    fresh.getChildFile ("A" + juce::String (kExtension)).deleteFile();
    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (! fresh.getChildFile ("A" + juce::String (kExtension)).exists());
}

TEST_CASE ("migration: only preset files are copied, and the marker is not a listed preset",
           "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "body A");
    legacy.getChildFile ("notes.txt").replaceWithText ("not a preset");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);
    REQUIRE (! fresh.getChildFile ("notes.txt").exists());

    // PresetManager::rescan lists "*.json": the dotfile marker must not appear in it
    const auto listed = fresh.findChildFiles (juce::File::findFiles, false, "*.json");
    REQUIRE (listed.size() == 1);
    REQUIRE (listed[0].getFileNameWithoutExtension() == "A");
}

TEST_CASE ("migration: an empty legacy folder still completes and is not retried", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    legacy.createDirectory();

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (fresh.getChildFile (kMarkerFileName).existsAsFile());
}

TEST_CASE ("migration: an unusable destination leaves no marker so the next launch retries",
           "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "body A");

    // a regular FILE where the new folder should be
    fresh.replaceWithText ("i am a file, not a folder");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (! fresh.isDirectory());

    fresh.deleteFile();
    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);
    REQUIRE (contentsOf (fresh, "A") == "body A");
    REQUIRE (fresh.getChildFile (kMarkerFileName).existsAsFile());
}
