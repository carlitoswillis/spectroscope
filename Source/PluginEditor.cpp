#include "PluginEditor.h"

namespace
{
    const juce::Colour background   { 0xff101014 };
    const juce::Colour panelFill    { 0xff17171d };
    const juce::Colour panelOutline { 0xff2a2a34 };
    const juce::Colour textDim      { 0xff6e6e7d };
    const juce::Colour textBright   { 0xffd6d6e0 };
    const juce::Colour accent       { 0xff4fd1c5 };

    constexpr int headerHeight   = 34;
    constexpr int timeAxisHeight = 20;
    constexpr int padding        = 8;
}

SpectroscopeAudioProcessorEditor::SpectroscopeAudioProcessorEditor (SpectroscopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setResizable (true, true);
    setResizeLimits (480, 320, 4096, 2400);
    setSize (900, 560);

    startTimerHz (10);
}

void SpectroscopeAudioProcessorEditor::timerCallback()
{
    const auto rate = processor.getCurrentSampleRate();
    const auto block = processor.getCurrentBlockSize();

    const auto text = rate > 0.0
        ? juce::String (rate / 1000.0, 1) + " kHz  /  " + juce::String (block) + " smp  /  0 ms latency"
        : juce::String ("no audio yet");

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

    // Waveform takes the top third, spectrogram the rest. Both are inset by the
    // same amount so their time axes line up pixel for pixel.
    area.removeFromTop (padding / 2);
    waveformArea = area.removeFromTop (juce::roundToInt (area.getHeight() * 0.32f));
    area.removeFromTop (padding / 2);
    spectrogramArea = area;
}

void SpectroscopeAudioProcessorEditor::paintPlaceholder (juce::Graphics& g,
                                                         juce::Rectangle<int> area,
                                                         const juce::String& label)
{
    g.setColour (panelFill);
    g.fillRoundedRectangle (area.toFloat(), 4.0f);

    g.setColour (panelOutline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);

    g.setColour (textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (label, area, juce::Justification::centred);
}

void SpectroscopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = headerArea;

    g.setColour (textBright);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("SPECTROSCOPE", header.removeFromLeft (150),
                juce::Justification::centredLeft);

    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (statusText, header, juce::Justification::centredRight);

    paintPlaceholder (g, waveformArea,    "waveform");
    paintPlaceholder (g, spectrogramArea, "spectrogram");

    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("shared time axis", timeAxisArea, juce::Justification::centred);
}
