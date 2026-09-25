cmake_minimum_required(VERSION 3.24)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version (universal binaries cannot go below 11.0)")
endif()

project(Broken VERSION 0.35.1 LANGUAGES C CXX)

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

# Broken FX ships only "00 Init" (snapshots/fx/) -- the owner authors the rest of that
# bank separately. A distinct binary-data target so the FX bundle never carries the
# instrument's presets (they reference sample/cycle/osc params that FX hides).
file(GLOB BROKEN_FX_PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/snapshots/fx/*.json)
juce_add_binary_data(BrokenFXPresets SOURCES ${BROKEN_FX_PRESET_FILES})

# Knobs, fonts, and every shared control come from zqsfx_ui (the knobs are its CC0 house
# filmstrips), so this project carries no GUI art of its own.

set(BROKEN_PLUGIN_SOURCES
  src/plugin/Params.h
  src/plugin/PluginProcessor.h
  src/plugin/PluginProcessor.cpp
  src/plugin/PluginEditor.h
  src/plugin/PluginEditor.cpp)

# --- the plugin itself: a true instrument (aumu) with MIDI in, so Sample/Osc AND Input
# modes live in one binary and it still keeps its stereo input bus for hosts that feed an
# instrument audio (the Broken/Broken FX split, DESIGN split spec). Product name "Broken"
# by ZQ SFX; the CMake target is also Broken. Identity codes below are frozen (hosts key
# sessions on them). The manufacturer code is the company-wide ZQSF, set 2026-09-21 by
# owner override of the original Zqsx (see CHANGELOG); Brkn must never change.
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
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Synth
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Broken PRIVATE ${BROKEN_PLUGIN_SOURCES})
target_include_directories(Broken PRIVATE src)
target_compile_definitions(Broken PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  BROKEN_VERSION="${PROJECT_VERSION}" BROKEN_FX=0)
target_link_libraries(Broken
  PRIVATE BrokenPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- Broken FX: the same codebase (${BROKEN_PLUGIN_SOURCES}) built as an insert effect,
# gated by BROKEN_FX=1 (see plugin/CLAUDE.md). True stereo, two dsp::Engine instances,
# no MIDI, its own state-type tag, its own preset folder. Identity per ../CLAUDE.md
# section 2's registry: PLUGIN_CODE BrFx is reserved there for this product.
# COPY_PLUGIN_AFTER_BUILD stays FALSE, same as Broken -- this project does not
# auto-install either target.
juce_add_plugin(BrokenFX
  PRODUCT_NAME "Broken FX"
  COMPANY_NAME "ZQ SFX"
  COMPANY_WEBSITE "https://www.zq-sfx.com"
  COMPANY_EMAIL "connect@zq-sfx.com"
  COMPANY_COPYRIGHT "Copyright (c) 2026 ZQ SFX"
  BUNDLE_ID com.zqsfx.brokenfx
  PLUGIN_MANUFACTURER_CODE ZQSF
  PLUGIN_CODE BrFx
  FORMATS AU VST3 Standalone
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx Distortion
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BrokenFX PRIVATE ${BROKEN_PLUGIN_SOURCES})
target_include_directories(BrokenFX PRIVATE src)
target_compile_definitions(BrokenFX PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  BROKEN_VERSION="${PROJECT_VERSION}" BROKEN_FX=1)
target_link_libraries(BrokenFX
  PRIVATE BrokenFXPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- headless render CLI: same processor sources compiled into a console app,
# so every render exercises the identical code path the plugin runs
juce_add_console_app(broken_cli PRODUCT_NAME "broken_cli")
target_sources(broken_cli PRIVATE src/cli/main.cpp ${BROKEN_PLUGIN_SOURCES})
target_include_directories(broken_cli PRIVATE src)
# PluginProcessor avoids all JucePlugin_* macros so it compiles unchanged in a console app
target_compile_definitions(broken_cli PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 BROKEN_FX=0)
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
  BROKEN_VERSION="${PROJECT_VERSION}" BROKEN_FX=0)
target_link_libraries(broken_ui_snapshot
  PRIVATE BrokenPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- broken_fx_ui_snapshot: same regression gate as broken_ui_snapshot, but for the
# BrokenFX editor (BROKEN_FX=1). Renders the compact FX window at its default size and
# at its resize-limit minimum.
juce_add_console_app(broken_fx_ui_snapshot PRODUCT_NAME "broken_fx_ui_snapshot")
target_sources(broken_fx_ui_snapshot PRIVATE src/cli/ui_snapshot.cpp ${BROKEN_PLUGIN_SOURCES})
target_include_directories(broken_fx_ui_snapshot PRIVATE src)
target_compile_definitions(broken_fx_ui_snapshot PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0
  BROKEN_VERSION="${PROJECT_VERSION}" BROKEN_FX=1)
target_link_libraries(broken_fx_ui_snapshot
  PRIVATE BrokenFXPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
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

# --- broken_fx_check: console-app gate for the BrokenFX processor itself (BROKEN_FX=1),
# same link pattern as broken_cli. Proves the stereo/no-MIDI/default-level contract in
# plugin/CLAUDE.md's Broken FX section without needing a host.
juce_add_console_app(broken_fx_check PRODUCT_NAME "broken_fx_check")
target_sources(broken_fx_check PRIVATE src/cli/fx_check.cpp ${BROKEN_PLUGIN_SOURCES})
target_include_directories(broken_fx_check PRIVATE src)
target_compile_definitions(broken_fx_check PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 BROKEN_FX=1)
target_link_libraries(broken_fx_check
  PRIVATE BrokenFXPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME broken_fx_check COMMAND broken_fx_check)

# --- broken_control_audit / broken_fx_control_audit: headless control-wiring audit.
# Same link pattern as broken_cli/broken_fx_check, built for both BROKEN_FX values so it
# can exercise each product's own gatherParams()/source forcing. Deliberately NOT
# registered with ctest (add_test) -- this is an audit tool for the owner to read the
# findings from, not a pass/fail gate yet.
juce_add_console_app(broken_control_audit PRODUCT_NAME "broken_control_audit")
target_sources(broken_control_audit PRIVATE src/cli/control_audit.cpp ${BROKEN_PLUGIN_SOURCES})
target_include_directories(broken_control_audit PRIVATE src)
target_compile_definitions(broken_control_audit PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 BROKEN_FX=0)
target_link_libraries(broken_control_audit
  PRIVATE BrokenPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(broken_fx_control_audit PRODUCT_NAME "broken_fx_control_audit")
target_sources(broken_fx_control_audit PRIVATE src/cli/control_audit.cpp ${BROKEN_PLUGIN_SOURCES})
target_include_directories(broken_fx_control_audit PRIVATE src)
target_compile_definitions(broken_fx_control_audit PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 BROKEN_FX=1)
target_link_libraries(broken_fx_control_audit
  PRIVATE BrokenFXPresets zqsfx::ui juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
