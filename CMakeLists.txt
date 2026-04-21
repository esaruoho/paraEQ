# ParaEQ 301 — Universal macOS (arm64 + x86_64) Audio Unit
# Requires: Xcode command-line tools, CMake 3.22+
# Build: mkdir build && cd build && cmake .. -G Xcode && cmake --build . --config Release
# Or: cmake .. -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" && cmake --build . --config Release

cmake_minimum_required(VERSION 3.22)
project(ParaEQ301 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ParaEQ301
    COMPANY_NAME "ParaEQ"
    BUNDLE_ID com.paraeq.paraeq301
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Peq1
    PLUGIN_CODE P301
    FORMATS AU VST3
    PRODUCT_NAME "ParaEQ 301")

juce_generate_juce_header(ParaEQ301)

target_sources(ParaEQ301
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ParametricEqDesign.h)

target_compile_definitions(ParaEQ301
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ParaEQ301
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
