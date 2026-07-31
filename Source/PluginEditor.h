#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
    Phase 1 shell. The layout it establishes — waveform above, spectrogram
    below, both sharing one horizontal time axis — is the layout the real views
    drop into in Phases 2 and 3.
*/
class SpectroscopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SpectroscopeAudioProcessorEditor (SpectroscopeAudioProcessor&);
    ~SpectroscopeAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintPlaceholder (juce::Graphics&, juce::Rectangle<int> area, const juce::String& label);

    SpectroscopeAudioProcessor& processor;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> waveformArea;
    juce::Rectangle<int> spectrogramArea;
    juce::Rectangle<int> timeAxisArea;

    juce::String statusText { "no audio yet" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessorEditor)
};
