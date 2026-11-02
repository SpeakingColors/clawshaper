cmake_minimum_required(VERSION 3.22)
# The release workflow passes the tag as CS_VERSION so menu, installer and release agree.
set(CS_VERSION "0.1.0" CACHE STRING "Plugin version")
project(ClawShaper VERSION ${CS_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

include(FetchContent)
if(CS_JUCE_DIR)
    add_subdirectory(${CS_JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.16-security
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(ClawShaperAssets SOURCES
    assets/fonts/BarlowCondensed-Medium.ttf
    assets/fonts/BarlowCondensed-SemiBold.ttf
    assets/fonts/BarlowCondensed-Bold.ttf
    assets/fonts/BarlowCondensed-ExtraBoldItalic.ttf
    assets/fonts/JetBrainsMono-Regular.ttf)
set_target_properties(ClawShaperAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(ClawShaper
    COMPANY_NAME "Studio 87"
    PLUGIN_MANUFACTURER_CODE Stu8
    PLUGIN_CODE ClSh
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "ClawShaper")

set(CS_SOURCES
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
    src/plugin/ParamTable.h
    src/plugin/ParamMap.h
    src/editor/PluginEditor.h
    src/editor/PluginEditor.cpp
    src/editor/Theme.h
    src/editor/Layout.h
    src/core/ClawShaperCore.h
    src/editor/Presets.h)
target_sources(ClawShaper PRIVATE ${CS_SOURCES})

target_compile_definitions(ClawShaper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(ClawShaper
    PRIVATE ClawShaperAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Offscreen window renderer for checking the editor against the mockup (not part of releases).
option(CS_BUILD_SNAPSHOT "Build the editor snapshot tool" OFF)
if(CS_BUILD_SNAPSHOT)
    juce_add_console_app(ClawShaperSnapshot PRODUCT_NAME "ClawShaperSnapshot")
    target_sources(ClawShaperSnapshot PRIVATE tools/snapshot.cpp ${CS_SOURCES})
    target_compile_definitions(ClawShaperSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="ClawShaper" JucePlugin_VersionString="0.1.0")
    target_link_libraries(ClawShaperSnapshot PRIVATE ClawShaperAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
