cmake_minimum_required(VERSION 3.22)

project(Spectroscope VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Universal binary so the same build runs on Apple Silicon and Intel hosts.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Spectroscope
    COMPANY_NAME              "carlitoswillis"
    BUNDLE_ID                 "com.carlitoswillis.spectroscope"
    PLUGIN_MANUFACTURER_CODE  Cwil
    PLUGIN_CODE               Spct
    FORMATS                   AU VST3 Standalone
    PRODUCT_NAME              "Spectroscope"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE

    # The standalone build opens an input device, which macOS gates behind this
    # permission. Without it the .app launches but records silence.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "Spectroscope reads audio input so it can show you what your Mac is playing.")

target_sources(Spectroscope PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/SampleRingBuffer.cpp
    Source/dsp/AnalysisEngine.cpp
    Source/dsp/StftAnalyzer.cpp
    Source/gui/WaveformView.cpp
    Source/gui/SpectrogramView.cpp
    Source/gui/CrtOverlay.cpp)

target_compile_definitions(Spectroscope PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Spectroscope
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Headless DSP tests. These cover the lock-free plumbing and the analysis maths,
# which is the part that can be verified without a display or an audio device.
enable_testing()

juce_add_console_app(SpectroscopeTests PRODUCT_NAME "SpectroscopeTests")

target_sources(SpectroscopeTests PRIVATE
    Tests/DspTests.cpp
    Source/dsp/SampleRingBuffer.cpp
    Source/dsp/AnalysisEngine.cpp
    Source/dsp/StftAnalyzer.cpp)

target_compile_definitions(SpectroscopeTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_UNIT_TESTS=1
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(SpectroscopeTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME dsp COMMAND SpectroscopeTests)

#==============================================================================
# Headless preview renderer. Paints the real editor into a PNG with synthetic
# audio driven through it, so the visual design can be reviewed without a DAW
# — or without a Mac. Off by default since it is a development tool.
option(SPECTROSCOPE_BUILD_PREVIEW "Build the headless preview renderer" OFF)

if(SPECTROSCOPE_BUILD_PREVIEW)
    juce_add_console_app(RenderPreview PRODUCT_NAME "RenderPreview")

    # Compiles the sources directly rather than linking the plugin's shared
    # code, so the whole target agrees on JUCE_MODAL_LOOPS_PERMITTED — the
    # preview needs to pump the message loop by hand to let view timers run.
    target_sources(RenderPreview PRIVATE
        Tools/RenderPreview.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/SampleRingBuffer.cpp
        Source/dsp/AnalysisEngine.cpp
        Source/dsp/StftAnalyzer.cpp
        Source/gui/WaveformView.cpp
        Source/gui/SpectrogramView.cpp
        Source/gui/CrtOverlay.cpp)

    target_compile_definitions(RenderPreview PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="Spectroscope")

    target_link_libraries(RenderPreview
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags)
endif()
