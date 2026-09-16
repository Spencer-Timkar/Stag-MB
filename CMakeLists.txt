cmake_minimum_required(VERSION 3.22)

project(StagMotionBands VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(STAG_PLUGIN_FORMATS AU VST3 Standalone)
else()
    set(STAG_PLUGIN_FORMATS VST3 Standalone)
endif()

juce_add_plugin(StagMotionBands
    COMPANY_NAME "Stag Audio"
    BUNDLE_ID "com.stagaudio.motionbands"
    PLUGIN_MANUFACTURER_CODE Stag
    PLUGIN_CODE MoBd
    FORMATS ${STAG_PLUGIN_FORMATS}
    PRODUCT_NAME "Stag Motion Bands"
    DESCRIPTION "Interactive multiband modulation effect"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(StagMotionBands)

target_sources(StagMotionBands
    PRIVATE
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/ModulationDSP.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
)

target_compile_definitions(StagMotionBands
    PUBLIC
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(StagMotionBands
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_executable(StagMotionBandsTests)

target_sources(StagMotionBandsTests
    PRIVATE
        Tests/ProcessorSmokeTests.cpp
)

target_include_directories(StagMotionBandsTests
    PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/StagMotionBands_artefacts/JuceLibraryCode"
)

target_compile_definitions(StagMotionBandsTests
    PRIVATE
        JucePlugin_Name="Stag Motion Bands"
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(StagMotionBandsTests
    PRIVATE
        StagMotionBands
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME StagMotionBandsProcessorSmoke COMMAND StagMotionBandsTests)
