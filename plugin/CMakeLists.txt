cmake_minimum_required(VERSION 3.24)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version (universal binaries cannot go below 11.0)")
endif()

project(Broken VERSION 0.34.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64") # universal is mandatory: Soundminer will not load anything else (../CLAUDE.md section 5)

include(FetchContent)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The ZQ SFX house UI (tokens, LookAndFeel, bound controls, embedded OFL fonts). Lifted from
# this project; must come AFTER JUCE. Pinned by tag: bump deliberately, then re-run
# broken_ui_snapshot and compare (see CHANGELOG).
FetchContent_Declare(zqsfx_ui
  GIT_REPOSITORY https://github.com/themightyzq/zqsfx_ui.git
  GIT_TAG v0.4.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zqsfx_ui)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# Factory presets compiled into the binary: loose JSON on disk would not exist on any
# other machine. CONFIGURE_DEPENDS so adding a preset needs no edit here.
file(GLOB BROKEN_PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/snapshots/*.json)
juce_add_binary_data(BrokenPresets SOURCES ${BROKEN_PRESET_FILES})

# Knobs, fonts, and every shared control come from zqsfx_ui (the knobs are its CC0 house
# filmstrips), so this project carries no GUI art of its own.

set(BROKEN_PLUGIN_SOURCES
  src/plugin/Params.h
  src/plugin/PluginProcessor.h
  src/plugin/PluginProcessor.cpp
  src/plugin/PluginEditor.h
  src/plugin/PluginEditor.cpp)

# --- the plugin itself: audio in + MIDI in (aumf), so Sample/Osc AND Input modes live in one
# binary. Product name "Broken" by ZQ SFX; the CMake target is also Broken. Identity codes
# below are frozen (hosts key sessions on them). The manufacturer code is the company-wide
# ZQSF, set 2026-09-21 by owner override of the original Zqsx (see CHANGELOG); Brkn must
# never change.
juce_add_plugin(Broken
  PRODUCT_NAME "Broken"
  COMPANY_NAME "ZQ SFX"
  COMPANY_WEBSITE "https://www.zq-sfx.com"
  COMPANY_EMAIL "connect@zq-sfx.com"
  COMPANY_COPYRIGHT "Copyright (c) 2026 ZQ SFX"
  BUNDLE_ID com.zqsfx.broken
  PLUGIN_MANUFACTURER_CODE ZQSF
  PLUGIN_CODE Brkn
  FORMATS AU VST3 Standalone
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx Instrument
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Broken PRIVATE ${BROKEN_PLUGIN_SOURCES})
target_include_directories(Broken PRIVATE src)
target_compile_definitions(Broken PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  BROKEN_VERSION="${PROJECT_VERSION}")
target_link_libraries(Broken
  PRIVATE BrokenPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- headless render CLI: same processor sources compiled into a console app,
# so every render exercises the identical code path the plugin runs
juce_add_console_app(broken_cli PRODUCT_NAME "broken_cli")
target_sources(broken_cli PRIVATE src/cli/main.cpp ${BROKEN_PLUGIN_SOURCES})
target_include_directories(broken_cli PRIVATE src)
# PluginProcessor avoids all JucePlugin_* macros so it compiles unchanged in a console app
target_compile_definitions(broken_cli PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
# broken_cli compiles the same BROKEN_PLUGIN_SOURCES as Broken, so it must see the same
# warning flags as the plugin target -- otherwise the "no new warnings" gate is silent
# whenever only broken_cli is built.
target_link_libraries(broken_cli
  PRIVATE BrokenPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- headless panel renderer: the look-and-feel regression gate (render, change, render,
# compare; the grime is seeded so identical code gives a byte-identical PNG)
juce_add_console_app(broken_ui_snapshot PRODUCT_NAME "broken_ui_snapshot")
target_sources(broken_ui_snapshot PRIVATE src/cli/ui_snapshot.cpp ${BROKEN_PLUGIN_SOURCES})
target_include_directories(broken_ui_snapshot PRIVATE src)
target_compile_definitions(broken_ui_snapshot PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0
  BROKEN_VERSION="${PROJECT_VERSION}")
target_link_libraries(broken_ui_snapshot
  PRIVATE BrokenPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- unit tests: pure DSP only, JUCE-free by design (src/dsp must not include JUCE)
enable_testing()
# GLOB so test files can be added without touching this file (agents work on disjoint files)
file(GLOB BROKEN_TEST_SOURCES CONFIGURE_DEPENDS src/tests/*.cpp)
add_executable(broken_tests ${BROKEN_TEST_SOURCES})
target_include_directories(broken_tests PRIVATE src)
# broken_tests must see the same warnings as Broken/broken_cli so the "no new warnings" gate
# cannot be silent just because only the test executable was built; juce_recommended_warning_flags
# is a JUCE INTERFACE library, so linking it here pulls in compile options only, no JUCE modules.
target_link_libraries(broken_tests PRIVATE Catch2::Catch2WithMain juce::juce_recommended_warning_flags)
include(CTest)
include(Catch)
catch_discover_tests(broken_tests)
