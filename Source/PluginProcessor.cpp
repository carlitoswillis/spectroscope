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

    // Momentary triggers so a host can automate display actions. Plain bools:
    // anything fancier trips AU validation for no benefit.
    addParameter (clearParam  = new juce::AudioParameterBool ({ "clear",  1 }, "Clear Displays", false));
    addParameter (storeParam  = new juce::AudioParameterBool ({ "store",  1 }, "Store Trace",    false));
    addParameter (markerParam = new juce::AudioParameterBool ({ "marker", 1 }, "Add Marker",     false));
}

void SpectroscopeAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBlockSize.store (maximumExpectedSamplesPerBlock, std::memory_order_relaxed);

    analysisEngine.prepare (sampleRate,
                            maximumExpectedSamplesPerBlock,
                            juce::jmax (1, getTotalNumInputChannels()));

    // The alignment tone's store matches what the engine was just prepared
    // for, so pushing it needs no per-block allocation or channel juggling.
    toneBuffer.setSize (juce::jmax (1, getTotalNumInputChannels()), maximumExpectedSamplesPerBlock);
    toneBuffer.clear();
    tonePhase = 0.0;
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

    // The trigger parameters are momentary switches: only the rising edge
    // counts, and the count is what the editor polls and diffs.
    const auto countEdge = [] (bool state, bool& previous, std::atomic<int>& counter)
    {
        if (state && ! previous)
            counter.fetch_add (1, std::memory_order_relaxed);

        previous = state;
    };

    countEdge (clearParam->get(),  previousClear,  clearEvents);
    countEdge (storeParam->get(),  previousStore,  storeEvents);
    countEdge (markerParam->get(), previousMarker, markerEvents);

    if (alignmentToneEnabled.load (std::memory_order_relaxed)
        && toneBuffer.getNumChannels() > 0
        && toneBuffer.getNumSamples() > 0)
    {
        // The meters read a generated -18 dBFS 1 kHz sine instead of the
        // input; the audio itself still passes through untouched. Phase
        // carries across blocks so the tone is continuous, not a burst per
        // block.
        const auto numSamples = juce::jmin (buffer.getNumSamples(), toneBuffer.getNumSamples());
        const auto rate = currentSampleRate.load (std::memory_order_relaxed);
        const auto increment = rate > 0.0 ? juce::MathConstants<double>::twoPi * 1000.0 / rate : 0.0;
        const auto gain = juce::Decibels::decibelsToGain (-18.0f);

        auto* tone = toneBuffer.getWritePointer (0);

        for (int i = 0; i < numSamples; ++i)
        {
            tone[i] = gain * static_cast<float> (std::sin (tonePhase));
            tonePhase += increment;
        }

        tonePhase = std::fmod (tonePhase, juce::MathConstants<double>::twoPi);

        for (int channel = 1; channel < toneBuffer.getNumChannels(); ++channel)
            toneBuffer.copyFrom (channel, 0, toneBuffer, 0, 0, numSamples);

        // A non-owning view over the preallocated store, sized to this block.
        const juce::AudioBuffer<float> toneBlock (toneBuffer.getArrayOfWritePointers(),
                                                  toneBuffer.getNumChannels(), numSamples);
        analysisEngine.pushAudio (toneBlock);
    }
    else
    {
        // The only other thing this plugin does on the audio thread: a bounded
        // copy into a preallocated ring buffer. No allocation, no locks, and
        // the audio itself is left untouched on its way out.
        analysisEngine.pushAudio (buffer);
    }
}

juce::AudioProcessorEditor* SpectroscopeAudioProcessor::createEditor()
{
    return new SpectroscopeAudioProcessorEditor (*this);
}

void SpectroscopeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The only parameters are momentary triggers, which would be meaningless
    // restored — the state is purely how the display is dressed.
    juce::XmlElement state ("SpectroscopeState");
    state.setAttribute ("theme", getThemeIndex());
    state.setAttribute ("panes", getPanesMask());
    state.setAttribute ("floating", getFloatingMask());
    state.setAttribute ("windows", getWindowLayout());

    copyXmlToBinary (state, destData);
}

void SpectroscopeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto state = getXmlFromBinary (data, sizeInBytes))
    {
        if (state->hasTagName ("SpectroscopeState"))
        {
            setThemeIndex (state->getIntAttribute ("theme", 0));

            // Sessions from before floating windows carry neither attribute:
            // the defaults dock everything, exactly as they were saved.
            setFloatingMask (state->getIntAttribute ("floating", 0));
            setWindowLayout (state->getStringAttribute ("windows", {}));

            if (state->hasAttribute ("panes"))
            {
                setPanesMask (state->getIntAttribute ("panes", 0b000011));
            }
            else if (state->hasAttribute ("view"))
            {
                // Sessions saved before the multi-pane console stored a single
                // lower-screen index: keep the waveform plus that instrument.
                const auto view = juce::jlimit (0, 4, state->getIntAttribute ("view", 0));
                setPanesMask (1 | (1 << (view + 1)));
            }
            else
            {
                // Older still, a bool chose spectrogram or spectrum.
                setPanesMask (state->getBoolAttribute ("spectrum", false) ? 0b101 : 0b011);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectroscopeAudioProcessor();
}
