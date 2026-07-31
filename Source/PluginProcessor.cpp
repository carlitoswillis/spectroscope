#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectroscopeAudioProcessor::SpectroscopeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // An analyser contributes no delay. Saying so explicitly keeps host delay
    // compensation from adding any.
    setLatencySamples (0);
}

void SpectroscopeAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBlockSize.store (maximumExpectedSamplesPerBlock, std::memory_order_relaxed);

    analysisEngine.prepare (sampleRate,
                            maximumExpectedSamplesPerBlock,
                            juce::jmax (1, getTotalNumInputChannels()));
}

void SpectroscopeAudioProcessor::releaseResources()
{
    analysisEngine.release();
}

bool SpectroscopeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Pass-through: whatever comes in must go straight back out.
    return layouts.getMainInputChannelSet() == out;
}

void SpectroscopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Any output channel the host gave us beyond our inputs would otherwise
    // carry whatever was left in the buffer.
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // The only thing this plugin does on the audio thread: a bounded copy into
    // a preallocated ring buffer. No allocation, no locks, and the audio itself
    // is left untouched on its way out.
    analysisEngine.pushAudio (buffer);
}

juce::AudioProcessorEditor* SpectroscopeAudioProcessor::createEditor()
{
    return new SpectroscopeAudioProcessorEditor (*this);
}

void SpectroscopeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // No audio parameters — the state is purely how the display is dressed.
    juce::XmlElement state ("SpectroscopeState");
    state.setAttribute ("theme", getThemeIndex());
    state.setAttribute ("spectrum", getSpectrumMode());

    copyXmlToBinary (state, destData);
}

void SpectroscopeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto state = getXmlFromBinary (data, sizeInBytes))
    {
        if (state->hasTagName ("SpectroscopeState"))
        {
            setThemeIndex (state->getIntAttribute ("theme", 0));
            setSpectrumMode (state->getBoolAttribute ("spectrum", false));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectroscopeAudioProcessor();
}
