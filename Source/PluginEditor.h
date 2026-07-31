#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WaveformView.h"
#include "gui/SpectrogramView.h"
#include "gui/SpectrumView.h"
#include "gui/StereoFieldView.h"
#include "gui/LoudnessHistoryView.h"
#include "gui/OscilloscopeView.h"
#include "gui/CrtOverlay.h"

/**
    Waveform above, analysis view below, set into a chassis behind CRT glass.

    The lower screen cycles through five modes: a scrolling spectrogram sharing
    the waveform's time axis, an instantaneous spectrum with peak hold, a stereo
    field display (Lissajous scope with VU and correlation meters), a
    momentary-loudness history chart, and a triggered-sweep oscilloscope. Click
    the screen's label plate to advance.
    The livery switch in the header cycles the palettes; both choices persist
    in the plugin state.
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

    void applyViewMode (int mode);
    void cycleTheme();

    SpectroscopeAudioProcessor& processor;
    WaveformView waveformView;
    SpectrogramView spectrogramView;
    SpectrumView spectrumView;
    StereoFieldView stereoFieldView;
    LoudnessHistoryView loudnessView;
    OscilloscopeView oscilloscopeView;
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
