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
}

void SpectroscopeAudioProcessor::releaseResources()
{
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

    // Audio is deliberately left untouched. Phase 2 adds a lock-free write of
    // these samples into the analysis ring buffer here; the buffer itself stays
    // read-only for the life of this plugin.
}

juce::AudioProcessorEditor* SpectroscopeAudioProcessor::createEditor()
{
    return new SpectroscopeAudioProcessorEditor (*this);
}

void SpectroscopeAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
    // No parameters yet. Display settings land here in Phase 6.
}

void SpectroscopeAudioProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectroscopeAudioProcessor();
}
