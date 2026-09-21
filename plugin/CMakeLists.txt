cmake_minimum_required(VERSION 3.24)
project(TurboSynth VERSION 0.34.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64)

include(FetchContent)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# Factory presets compiled into the binary: loose JSON on disk would not exist on any
# other machine. CONFIGURE_DEPENDS so adding a preset needs no edit here.
file(GLOB TS_PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/snapshots/*.json)
juce_add_binary_data(TurboSynthPresets SOURCES ${TS_PRESET_FILES})

# v0.27 rack GUI assets: OFL fonts (Barlow Condensed / VT323 / IBM Plex Mono) and the
# Noisehead Analog Knob Kit 01 filmstrips (Julian Behrens — credited in README; his
# licence ships alongside the strips). ts_cli compiles the editor too, so it links this.
file(GLOB TS_FONT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf)
juce_add_binary_data(BrokenAssets HEADER_NAME BrokenAssets.h NAMESPACE BrokenAssets SOURCES
  ${TS_FONT_FILES}
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/knobs/strip_a.png
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/knobs/strip_b.png
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/knobs/strip_c.png)

set(TS_PLUGIN_SOURCES
  src/plugin/Params.h
  src/plugin/PluginProcessor.h
  src/plugin/PluginProcessor.cpp
  src/plugin/PluginEditor.h
  src/plugin/PluginEditor.cpp)

# --- the plugin itself: audio in + MIDI in (aumf), so Sample/Osc AND Input modes live in one
# binary. Product name "Broken" by ZQ SFX;
# the CMake target keeps the working name (internal only). Identity codes below are
# frozen (hosts key sessions on them). The manufacturer code is the company-wide ZQSF, set
# 2026-09-21 by owner override of the original Zqsx (see CHANGELOG); Brkn must never change.
juce_add_plugin(TurboSynth
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

target_sources(TurboSynth PRIVATE ${TS_PLUGIN_SOURCES})
target_include_directories(TurboSynth PRIVATE src)
target_compile_definitions(TurboSynth PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  BROKEN_VERSION="${PROJECT_VERSION}")
target_link_libraries(TurboSynth
  PRIVATE TurboSynthPresets BrokenAssets juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- headless render CLI: same processor sources compiled into a console app,
# so every render exercises the identical code path the plugin runs
juce_add_console_app(ts_cli PRODUCT_NAME "ts_cli")
target_sources(ts_cli PRIVATE src/cli/main.cpp ${TS_PLUGIN_SOURCES})
target_include_directories(ts_cli PRIVATE src)
# PluginProcessor avoids all JucePlugin_* macros so it compiles unchanged in a console app
target_compile_definitions(ts_cli PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
# ts_cli compiles the same TS_PLUGIN_SOURCES as TurboSynth, so it must see the same
# warning flags as the plugin target -- otherwise the "no new warnings" gate is silent
# whenever only ts_cli is built.
target_link_libraries(ts_cli
  PRIVATE TurboSynthPresets BrokenAssets juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- headless panel renderer: the look-and-feel regression gate (render, change, render,
# compare; the grime is seeded so identical code gives a byte-identical PNG)
juce_add_console_app(ts_ui_snapshot PRODUCT_NAME "ts_ui_snapshot")
target_sources(ts_ui_snapshot PRIVATE src/cli/ui_snapshot.cpp ${TS_PLUGIN_SOURCES})
target_include_directories(ts_ui_snapshot PRIVATE src)
target_compile_definitions(ts_ui_snapshot PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0
  BROKEN_VERSION="${PROJECT_VERSION}")
target_link_libraries(ts_ui_snapshot
  PRIVATE TurboSynthPresets BrokenAssets juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- unit tests: pure DSP only, JUCE-free by design (src/dsp must not include JUCE)
enable_testing()
# GLOB so test files can be added without touching this file (agents work on disjoint files)
file(GLOB TS_TEST_SOURCES CONFIGURE_DEPENDS src/tests/*.cpp)
add_executable(ts_tests ${TS_TEST_SOURCES})
target_include_directories(ts_tests PRIVATE src)
# ts_tests must see the same warnings as TurboSynth/ts_cli so the "no new warnings" gate
# cannot be silent just because only the test executable was built; juce_recommended_warning_flags
# is a JUCE INTERFACE library, so linking it here pulls in compile options only, no JUCE modules.
target_link_libraries(ts_tests PRIVATE Catch2::Catch2WithMain juce::juce_recommended_warning_flags)
include(CTest)
include(Catch)
catch_discover_tests(ts_tests)
