#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WaveformView.h"
#include "gui/SpectrogramView.h"
#include "gui/SpectrumView.h"
#include "gui/CrtOverlay.h"

/**
    Waveform above, spectral view below, set into a chassis behind CRT glass.

    The lower screen is switchable: a scrolling spectrogram sharing the
    waveform's time axis, or an instantaneous spectrum with peak hold. Click
    the screen's label plate to swap. The livery switch in the header cycles
    the palettes; both choices persist in the plugin state.
*/
class SpectroscopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SpectroscopeAudioProcessorEditor (SpectroscopeAudioProcessor&);
    ~SpectroscopeAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void paintHeader (juce::Graphics&);
    void paintScreenSurround (juce::Graphics&,
                              juce::Rectangle<int> screen,
                              juce::Rectangle<int> labelArea,
                              juce::StringRef caption,
                              juce::StringRef annotation);

    void applySpectrumMode (bool spectrumOn);
    void cycleTheme();

    SpectroscopeAudioProcessor& processor;
    WaveformView waveformView;
    SpectrogramView spectrogramView;
    SpectrumView spectrumView;
    CrtOverlay crtOverlay;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> footerArea;
    juce::Rectangle<int> waveformLabelArea;
    juce::Rectangle<int> spectralLabelArea;
    juce::Rectangle<int> themeSwitchArea;

    juce::String readoutText { "STANDBY" };
    bool signalPresent = false;
    bool dropsSeen = false;
    int droppedCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessorEditor)
};
