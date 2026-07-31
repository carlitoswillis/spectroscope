#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleRingBuffer.h"
#include "LockFreeQueue.h"

/** One hop's worth of waveform summary: the extremes that hop covered, plus its
    RMS level. Drawing min-to-max rather than decimating means a single-sample
    transient still shows up at any zoom level.
*/
struct EnvelopePoint
{
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float rms      = 0.0f;
};

/**
    Owns the analysis thread. Pulls hop-sized chunks out of the audio thread's
    ring buffer, summarises each one, and publishes the result for the UI.

    Phase 3 adds the FFT alongside the envelope, in the same loop and off the
    same hop, so the spectrogram and the waveform stay sample-aligned by
    construction.
*/
class AnalysisEngine final : private juce::Thread
{
public:
    AnalysisEngine();
    ~AnalysisEngine() override;

    /** Allocates and starts the analysis thread. Call from prepareToPlay. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);

    /** Stops the thread and releases storage. Call from releaseResources. */
    void release();

    /** Audio thread. Non-blocking, non-allocating. */
    void pushAudio (const juce::AudioBuffer<float>& buffer) noexcept { ringBuffer.write (buffer); }

    LockFreeQueue<EnvelopePoint>& getEnvelopeQueue() noexcept { return envelopeQueue; }

    int getHopSize() const noexcept       { return hopSize; }
    double getSampleRate() const noexcept { return currentSampleRate; }

    /** Seconds of audio represented by one envelope point. */
    double getSecondsPerPoint() const noexcept
    {
        return currentSampleRate > 0.0 ? hopSize / currentSampleRate : 0.0;
    }

    int getNumDroppedBlocks() const noexcept { return ringBuffer.getNumDroppedBlocks(); }

private:
    void run() override;
    void processPendingAudio();

    static constexpr int hopSize = 256;

    SampleRingBuffer ringBuffer;
    LockFreeQueue<EnvelopePoint> envelopeQueue;

    juce::AudioBuffer<float> hopBuffer;   // analysis thread only
    juce::AudioBuffer<float> monoBuffer;  // analysis thread only

    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisEngine)
};
