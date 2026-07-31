#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/AnalysisEngine.h"

/**
    Analyser front-end. Audio passes through bit-for-bit untouched and the
    processor reports zero latency, so hosts add nothing to the signal path.
*/
class SpectroscopeAudioProcessor final : public juce::AudioProcessor
{
public:
    SpectroscopeAudioProcessor();
    ~SpectroscopeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // Keeps the inherited double-precision overload visible; we only implement
    // the float one, and hiding it warns.
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                            { return true; }

    const juce::String getName() const override                { return JucePlugin_Name; }
    bool acceptsMidi() const override                          { return false; }
    bool producesMidi() const override                         { return false; }
    bool isMidiEffect() const override                         { return false; }
    double getTailLengthSeconds() const override               { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Sample rate last seen by prepareToPlay, for the editor to display.
        Read from the message thread, written from the audio thread.
    */
    double getCurrentSampleRate() const noexcept   { return currentSampleRate.load (std::memory_order_relaxed); }
    int getCurrentBlockSize() const noexcept       { return currentBlockSize.load (std::memory_order_relaxed); }

    AnalysisEngine& getAnalysisEngine() noexcept   { return analysisEngine; }

    /** Alignment tone: when engaged, processBlock feeds the meters a generated
        -18 dBFS 1 kHz sine instead of the input. The audio output stays
        bit-for-bit pass-through — the tone exists only for the analysis side.
    */
    void setAlignmentToneEnabled (bool enabled) noexcept
    {
        alignmentToneEnabled.store (enabled, std::memory_order_relaxed);
    }

    bool isAlignmentToneEnabled() const noexcept
    {
        return alignmentToneEnabled.load (std::memory_order_relaxed);
    }

    /** Rising-edge counts of the host-automatable trigger parameters. The
        editor polls and diffs these on its timer; a count rather than a level
        means a pulse that rises and falls between two ticks still lands.
    */
    int getClearEvents() const noexcept  { return clearEvents.load (std::memory_order_relaxed); }
    int getStoreEvents() const noexcept  { return storeEvents.load (std::memory_order_relaxed); }
    int getMarkerEvents() const noexcept { return markerEvents.load (std::memory_order_relaxed); }

    /** Display settings live on the processor so they survive the editor being
        closed and reopened, and travel with the session via state.
    */
    int getThemeIndex() const noexcept       { return themeIndex.load (std::memory_order_relaxed); }
    void setThemeIndex (int index) noexcept  { themeIndex.store (index, std::memory_order_relaxed); }

    /** Which panes the console shows, one bit per instrument: waveform,
        spectral density, spectrum, stereo field, level chart, oscilloscope.
        An empty mask is coerced to the waveform so the console is never dark.
    */
    int getPanesMask() const noexcept        { return panesMask.load (std::memory_order_relaxed); }

    void setPanesMask (int mask) noexcept
    {
        mask &= 0b111111;
        panesMask.store (mask != 0 ? mask : 1, std::memory_order_relaxed);
    }

    /** Which panes float in their own window, same bit order as the panes
        mask. Floating is orthogonal to enabled, so no never-dark coercion.
    */
    int getFloatingMask() const noexcept     { return floatingMask.load (std::memory_order_relaxed); }
    void setFloatingMask (int mask) noexcept { floatingMask.store (mask & 0b111111, std::memory_order_relaxed); }

    /** Saved floating-window bounds, "index:x,y,w,h" entries joined with ';'.
        A String can't be atomic, so a lock stands in for one.
    */
    juce::String getWindowLayout() const
    {
        const juce::ScopedLock lock (windowLayoutLock);
        return windowLayout;
    }

    void setWindowLayout (const juce::String& layout)
    {
        const juce::ScopedLock lock (windowLayoutLock);
        windowLayout = layout;
    }

private:
    AnalysisEngine analysisEngine;

    // Momentary trigger switches, host-automatable. Owned by the AudioProcessor
    // once addParameter has taken them.
    juce::AudioParameterBool* clearParam  = nullptr;
    juce::AudioParameterBool* storeParam  = nullptr;
    juce::AudioParameterBool* markerParam = nullptr;

    std::atomic<int> clearEvents  { 0 };
    std::atomic<int> storeEvents  { 0 };
    std::atomic<int> markerEvents { 0 };

    // Edge memory for the trigger parameters — audio thread only.
    bool previousClear  = false;
    bool previousStore  = false;
    bool previousMarker = false;

    std::atomic<bool> alignmentToneEnabled { false };

    // Tone store sized in prepareToPlay so the audio thread never allocates;
    // phase accumulates across blocks so the sine is continuous.
    juce::AudioBuffer<float> toneBuffer;
    double tonePhase = 0.0;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };

    std::atomic<int> themeIndex   { 0 };
    std::atomic<int> panesMask    { 0b000011 };
    std::atomic<int> floatingMask { 0 };

    juce::CriticalSection windowLayoutLock;
    juce::String windowLayout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessor)
};
