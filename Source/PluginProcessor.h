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

    /** Display settings live on the processor so they survive the editor being
        closed and reopened, and travel with the session via state.
    */
    int getThemeIndex() const noexcept       { return themeIndex.load (std::memory_order_relaxed); }
    void setThemeIndex (int index) noexcept  { themeIndex.store (index, std::memory_order_relaxed); }

    int getViewMode() const noexcept         { return viewMode.load (std::memory_order_relaxed); }
    void setViewMode (int mode) noexcept     { viewMode.store (juce::jlimit (0, 4, mode), std::memory_order_relaxed); }

private:
    AnalysisEngine analysisEngine;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };

    std::atomic<int> themeIndex { 0 };
    std::atomic<int> viewMode   { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessor)
};
