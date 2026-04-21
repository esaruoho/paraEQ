# ParaEQ 301 — Universal macOS (arm64 + x86_64) Audio Unit
# Requires: Xcode command-line tools, CMake 3.22+
# Build: mkdir build && cd build && cmake .. -G Xcode && cmake --build . --config Release
# Or: cmake .. -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" && cmake --build . --config Release
#
# Quick UI iteration (no DAW): Standalone format is enabled below. After configure:
#   cmake --build build --target ParaEQ301_Standalone
#   open "build/ParaEQ301_artefacts/Release/Standalone/ParaEQ 301.app"

cmake_minimum_required(VERSION 3.22)
project(ParaEQ301 VERSION 1.0.0 LANGUAGES C CXX)

# macOS: LDFLAGS / shell env often inject -L/usr/local/opt/zlib/lib (old Intel Homebrew). If that directory is missing,
# ld warns on every link. ParaEQ links zlib via the SDK — remove this specific -L from cached linker flags (harmless when the path exists).
if(APPLE)
    set(_peq_stale_zlib_l "-L/usr/local/opt/zlib/lib")
    foreach(_peq_v IN ITEMS
            CMAKE_EXE_LINKER_FLAGS
            CMAKE_EXE_LINKER_FLAGS_RELEASE
            CMAKE_EXE_LINKER_FLAGS_DEBUG
            CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
            CMAKE_EXE_LINKER_FLAGS_MINSIZEREL
            CMAKE_MODULE_LINKER_FLAGS
            CMAKE_MODULE_LINKER_FLAGS_RELEASE
            CMAKE_MODULE_LINKER_FLAGS_DEBUG
            CMAKE_SHARED_LINKER_FLAGS
            CMAKE_SHARED_LINKER_FLAGS_RELEASE
            CMAKE_SHARED_LINKER_FLAGS_DEBUG)
        string(REPLACE "${_peq_stale_zlib_l}" "" _peq_tmp "${${_peq_v}}")
        if(NOT _peq_tmp STREQUAL "${${_peq_v}}")
            string(REGEX REPLACE "  +" " " _peq_tmp "${_peq_tmp}")
            string(STRIP "${_peq_tmp}" _peq_tmp)
            set(${_peq_v} "${_peq_tmp}" CACHE STRING "" FORCE)
        endif()
    endforeach()
endif()

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
    FORMATS AU VST3 Standalone
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
