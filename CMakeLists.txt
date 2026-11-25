cmake_minimum_required(VERSION 3.22)

# Must be cached before project(): enabling the languages on macOS creates an empty
# cache entry, which a later set(... CACHE ...) would not override.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")

# Link the MSVC runtime statically so the Windows plug-ins need no VC++ redistributable.
# Applies to every target created below (policy CMP0091 is NEW from CMake 3.15).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(StagMotionBands VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

option(STAG_LICENSING_ENABLED "Require a Stag Motion Bands license (off for developer builds)" OFF)
set(STAG_LICENSE_ENVIRONMENT "test" CACHE STRING "Licensing environment: test or live")
foreach(id STORE PRODUCT VARIANT)
    set(STAG_LICENSE_${id}_ID "" CACHE STRING "Motion Bands Lemon Squeezy ${id} ID")
endforeach()
if(STAG_LICENSING_ENABLED)
    if(NOT STAG_LICENSE_ENVIRONMENT MATCHES "^(test|live)$")
        message(FATAL_ERROR "STAG_LICENSE_ENVIRONMENT must be test or live")
    endif()
    foreach(id STORE PRODUCT VARIANT)
        if(NOT STAG_LICENSE_${id}_ID MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR "Set STAG_LICENSE_${id}_ID to the Motion Bands catalog ID")
        endif()
    endforeach()
endif()

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
    NEEDS_CURL ${STAG_LICENSING_ENABLED}
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(StagMotionBands)

juce_add_binary_data(StagMotionBandsBrand
    SOURCES
        Assets/Inter400.ttf
        Assets/Inter600.ttf
        Assets/stag-mark.svg
)

target_sources(StagMotionBands
    PRIVATE
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/ModulationDSP.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/StagLookAndFeel.cpp
        Source/StagLookAndFeel.h
)

target_compile_definitions(StagMotionBands
    PUBLIC
        JUCE_REPORT_APP_USAGE=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(StagMotionBands
    PRIVATE
        StagMotionBandsBrand
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(STAG_LICENSING_ENABLED)
    target_sources(StagMotionBands PRIVATE Source/LicenseManager.cpp Source/LicenseManager.h)
    target_compile_definitions(StagMotionBands PUBLIC STAG_LICENSING_ENABLED=1)
    target_compile_definitions(StagMotionBands PRIVATE
        STAG_LICENSE_STORE_ID="${STAG_LICENSE_STORE_ID}"
        STAG_LICENSE_PRODUCT_ID="${STAG_LICENSE_PRODUCT_ID}"
        STAG_LICENSE_VARIANT_ID="${STAG_LICENSE_VARIANT_ID}"
        STAG_LICENSE_ENVIRONMENT="${STAG_LICENSE_ENVIRONMENT}"
    )
endif()

juce_add_console_app(StagMotionBandsTests)
juce_generate_juce_header(StagMotionBandsTests)

target_sources(StagMotionBandsTests
    PRIVATE
        Tests/ProcessorSmokeTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StagLookAndFeel.cpp
)

target_include_directories(StagMotionBandsTests
    PRIVATE
        Source
)

target_compile_definitions(StagMotionBandsTests
    PRIVATE
        JucePlugin_Name="Stag Motion Bands"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        STAG_PROCESSOR_TESTS=1
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(StagMotionBandsTests
    PRIVATE
        StagMotionBandsBrand
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME StagMotionBandsProcessorSmoke COMMAND StagMotionBandsTests)

juce_add_console_app(StagMotionBandsLicenseTests NEEDS_CURL TRUE)
juce_generate_juce_header(StagMotionBandsLicenseTests)
target_sources(StagMotionBandsLicenseTests PRIVATE Source/LicenseManager.cpp Tests/LicenseTests.cpp)
target_include_directories(StagMotionBandsLicenseTests PRIVATE Source)
# Explicit non-catalog IDs for the injected-transport test executable only.
target_compile_definitions(StagMotionBandsLicenseTests PRIVATE
    STAG_LICENSE_STORE_ID="1"
    STAG_LICENSE_PRODUCT_ID="2"
    STAG_LICENSE_VARIANT_ID="3"
    STAG_LICENSE_ENVIRONMENT="test"
)
target_link_libraries(StagMotionBandsLicenseTests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME StagMotionBandsLicense COMMAND StagMotionBandsLicenseTests)
