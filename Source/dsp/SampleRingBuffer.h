#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

/**
    Single-producer / single-consumer sample buffer bridging the audio thread to
    the analysis thread.

    The audio thread only ever calls write(), which never allocates, never locks
    and never blocks. If the consumer has fallen behind, the incoming block is
    dropped rather than waited on — losing a frame of a picture is always
    preferable to stalling the audio callback.
*/
class SampleRingBuffer
{
public:
    SampleRingBuffer() = default;

    /** Allocates storage. Call from prepareToPlay, never from the audio thread. */
    void prepare (int numChannelsToStore, int capacityInSamples);

    /** Discards pending samples. Safe only while the audio thread is stopped. */
    void reset();

    /** Audio thread. Copies up to two channels of the incoming block. */
    void write (const juce::AudioBuffer<float>& source) noexcept;

    /** Analysis thread. */
    int getNumReady() const noexcept    { return fifo.getNumReady(); }

    /** Analysis thread. Reads exactly numSamples if available, else returns 0. */
    int read (juce::AudioBuffer<float>& destination, int numSamples) noexcept;

    int getNumChannels() const noexcept { return buffer.getNumChannels(); }

    /** Diagnostic: blocks dropped because the consumer fell behind. */
    int getNumDroppedBlocks() const noexcept { return droppedBlocks.load (std::memory_order_relaxed); }

private:
    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> buffer;
    std::atomic<int> droppedBlocks { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleRingBuffer)
};
