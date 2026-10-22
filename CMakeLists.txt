cmake_minimum_required(VERSION 3.22)
project(STILL VERSION 0.1.0 LANGUAGES C CXX)
if(NOT APPLE)
    message(FATAL_ERROR "STILL's first supported target is macOS Audio Unit.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(STILL_BUILD_TESTS "Build STILL's offline validation tests" ON)
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
juce_add_binary_data(STILLAssets
    HEADER_NAME StillAssets.h
    NAMESPACE StillAssets
    SOURCES
        Assets/Stage/D_A_BODY_STAGE_48k.wav
        Assets/Stage/D_A_SWIRL_STAGE_48k.wav
        Assets/Stage/D_C_AIR_STAGE_48k.wav)
add_library(StillDsp STATIC
    Source/Dsp/DroneEngine.cpp Source/Dsp/DroneMath.h Source/Dsp/DroneEngine.h)
target_include_directories(StillDsp PUBLIC Source)
target_link_libraries(StillDsp PUBLIC STILLAssets juce::juce_audio_formats juce::juce_audio_basics juce::juce_dsp)
target_compile_definitions(StillDsp PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
juce_add_plugin(STILL
    COMPANY_NAME "Jack Ammon"
    BUNDLE_ID com.jackammon.still
    PLUGIN_MANUFACTURER_CODE JAKm
    PLUGIN_CODE Stil
    PRODUCT_NAME "STILL"
    FORMATS AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(STILL PRIVATE
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h)
target_include_directories(STILL PRIVATE Source)
target_link_libraries(STILL PRIVATE StillDsp juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_recommended_config_flags)
target_compile_definitions(STILL PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(STILL_BUILD_TESTS)
    enable_testing()
    juce_add_binary_data(STILLTestReference
        HEADER_NAME StillTestReference.h
        NAMESPACE StillTestReference
        SOURCES Assets/Stage/D_A_FULL_STAGE_BACKUP_48k.wav)
    add_executable(StillDspTests Tests/DroneEngineTests.cpp)
    target_link_libraries(StillDspTests PRIVATE StillDsp STILLTestReference juce::juce_audio_utils)
    target_compile_definitions(StillDspTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    add_test(NAME audio_asset_integrity COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_audio_assets.py ${CMAKE_CURRENT_SOURCE_DIR}/Assets)
    add_test(NAME drone_engine_smoke COMMAND StillDspTests)
endif()
