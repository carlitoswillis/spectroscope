#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WaveformView.h"
#include "gui/SpectrogramView.h"
#include "gui/CrtOverlay.h"

/**
    Waveform above, spectrogram below, both sharing one horizontal time axis,
    set into a warm chassis behind CRT glass.

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
    void paintHeader (juce::Graphics&);
    void paintScreenSurround (juce::Graphics&,
                              juce::Rectangle<int> screen,
                              juce::Rectangle<int> labelArea,
                              juce::StringRef caption,
                              juce::StringRef annotation);

    SpectroscopeAudioProcessor& processor;
    WaveformView waveformView;
    SpectrogramView spectrogramView;
    CrtOverlay crtOverlay;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> footerArea;
    juce::Rectangle<int> waveformLabelArea;
    juce::Rectangle<int> spectrogramLabelArea;

    juce::String readoutText { "STANDBY" };
    bool signalPresent = false;
    bool dropsSeen = false;
    int droppedCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessorEditor)
};
