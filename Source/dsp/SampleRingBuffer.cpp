#include "SampleRingBuffer.h"

void SampleRingBuffer::prepare (int numChannelsToStore, int capacityInSamples)
{
    const auto capacity = juce::nextPowerOfTwo (juce::jmax (1024, capacityInSamples));

    buffer.setSize (juce::jmax (1, numChannelsToStore), capacity, false, true, false);
    buffer.clear();

    fifo.setTotalSize (capacity);
    fifo.reset();

    droppedBlocks.store (0, std::memory_order_relaxed);
}

void SampleRingBuffer::reset()
{
    fifo.reset();
    buffer.clear();
}

void SampleRingBuffer::write (const juce::AudioBuffer<float>& source) noexcept
{
    const auto numSamples = source.getNumSamples();

    if (numSamples <= 0 || buffer.getNumSamples() == 0)
        return;

    if (fifo.getFreeSpace() < numSamples)
    {
        droppedBlocks.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    const auto channelsToCopy = juce::jmin (buffer.getNumChannels(), source.getNumChannels());

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        // A mono source feeding a stereo store repeats its channel rather than
        // leaving one side silent.
        const auto sourceChannel = juce::jmin (ch, channelsToCopy - 1);
        const auto* src = source.getReadPointer (juce::jmax (0, sourceChannel));

        if (size1 > 0) buffer.copyFrom (ch, start1, src, size1);
        if (size2 > 0) buffer.copyFrom (ch, start2, src + size1, size2);
    }

    fifo.finishedWrite (size1 + size2);
}

int SampleRingBuffer::read (juce::AudioBuffer<float>& destination, int numSamples) noexcept
{
    if (fifo.getNumReady() < numSamples || numSamples <= 0)
        return 0;

    int start1, size1, start2, size2;
    fifo.prepareToRead (numSamples, start1, size1, start2, size2);

    const auto channelsToFill = juce::jmin (destination.getNumChannels(), buffer.getNumChannels());

    for (int ch = 0; ch < channelsToFill; ++ch)
    {
        if (size1 > 0) destination.copyFrom (ch, 0,     buffer, ch, start1, size1);
        if (size2 > 0) destination.copyFrom (ch, size1, buffer, ch, start2, size2);
    }

    fifo.finishedRead (size1 + size2);
    return size1 + size2;
}
