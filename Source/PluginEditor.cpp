#include "PluginEditor.h"

namespace
{
    const juce::Colour background   { 0xff101014 };
    const juce::Colour panelOutline { 0xff2a2a34 };
    const juce::Colour textDim      { 0xff6e6e7d };
    const juce::Colour textBright   { 0xffd6d6e0 };
    const juce::Colour accent       { 0xff4fd1c5 };
    const juce::Colour warning      { 0xffe0a33f };

    constexpr int headerHeight   = 34;
    constexpr int timeAxisHeight = 20;
    constexpr int padding        = 8;
}

SpectroscopeAudioProcessorEditor::SpectroscopeAudioProcessorEditor (SpectroscopeAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      waveformView (p.getAnalysisEngine()),
      spectrogramView (p.getAnalysisEngine())
{
    addAndMakeVisible (waveformView);
    addAndMakeVisible (spectrogramView);

    setResizable (true, true);
    setResizeLimits (480, 320, 4096, 2400);
    setSize (900, 560);

    startTimerHz (10);
}

void SpectroscopeAudioProcessorEditor::timerCallback()
{
    const auto rate = processor.getCurrentSampleRate();
    const auto block = processor.getCurrentBlockSize();
    const auto dropped = processor.getAnalysisEngine().getNumDroppedBlocks();

    auto text = rate > 0.0
        ? juce::String (rate / 1000.0, 1) + " kHz  /  " + juce::String (block) + " smp  /  0 ms latency"
        : juce::String ("no audio yet");

    // Dropped blocks mean the analysis thread fell behind. Audio is unaffected,
    // but the picture has gaps, so it's worth surfacing rather than hiding.
    if (dropped > 0)
        text += "  /  " + juce::String (dropped) + " dropped";

    if (text != statusText)
    {
        statusText = text;
        repaint (headerArea);
    }
}

void SpectroscopeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (padding);

    headerArea   = area.removeFromTop (headerHeight);
    timeAxisArea = area.removeFromBottom (timeAxisHeight);

    // Waveform takes the top third, spectrogram the rest. Both keep the same
    // left and right edges so their time axes line up pixel for pixel.
    area.removeFromTop (padding / 2);
    waveformView.setBounds (area.removeFromTop (juce::roundToInt (area.getHeight() * 0.32f)));
    area.removeFromTop (padding / 2);
    spectrogramView.setBounds (area);
}

void SpectroscopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = headerArea;

    g.setColour (textBright);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("SPECTROSCOPE", header.removeFromLeft (150),
                juce::Justification::centredLeft);

    g.setColour (statusText.contains ("dropped") ? warning : accent);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (statusText, header, juce::Justification::centredRight);

    g.setColour (panelOutline);
    g.drawRect (waveformView.getBounds().expanded (1), 1);
    g.drawRect (spectrogramView.getBounds().expanded (1), 1);

    const auto span = waveformView.getVisibleTimeSpan();
    const auto nyquist = processor.getCurrentSampleRate() * 0.5;

    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.0f));

    auto axis = timeAxisArea;

    g.drawText (nyquist > 0.0 ? "0 - " + juce::String (nyquist / 1000.0, 1) + " kHz" : juce::String(),
                axis.removeFromLeft (110), juce::Justification::centredLeft);

    g.drawText (span > 0.0 ? juce::String (span, 2) + " s visible" : juce::String ("shared time axis"),
                axis, juce::Justification::centred);
}
