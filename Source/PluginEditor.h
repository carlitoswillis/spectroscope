#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WaveformView.h"
#include "gui/SpectrogramView.h"

/**
    Waveform above, spectrogram below, both sharing one horizontal time axis.

    Both views scroll on the same clock — one column per analysis hop — so a
    transient lines up vertically across the two.
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

    SpectroscopeAudioProcessor& processor;
    WaveformView waveformView;
    SpectrogramView spectrogramView;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> timeAxisArea;

    juce::String statusText { "no audio yet" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessorEditor)
};
