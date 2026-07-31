#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleRingBuffer.h"
#include "LockFreeQueue.h"
#include "ColumnRing.h"
#include "StftAnalyzer.h"

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
    ring buffer and, for each one, publishes both a waveform envelope point and
    a spectrogram column.

    Both come off the same hop in the same loop, which is what keeps the two
    views sample-aligned rather than merely approximately in step.
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

    /** Views call these on construction and destruction. With no consumers the
        thread keeps draining the ring buffer — so the audio thread never sees a
        full buffer — but skips the FFT entirely.
    */
    void addConsumer() noexcept;
    void removeConsumer() noexcept;
    bool hasConsumers() const noexcept { return consumerCount.load (std::memory_order_relaxed) > 0; }

    LockFreeQueue<EnvelopePoint>& getEnvelopeQueue() noexcept { return envelopeQueue; }
    ColumnRing& getSpectrumColumns() noexcept                 { return spectrumColumns; }

    int getNumBins() const noexcept       { return stft.getNumBins(); }
    int getHopSize() const noexcept       { return hopSize; }
    double getSampleRate() const noexcept { return currentSampleRate; }

    /** Centre frequency of a spectrogram row, for axis labelling. */
    float getBinFrequency (int bin) const noexcept { return stft.getBinFrequency (bin); }

    /** Seconds of audio represented by one column. */
    double getSecondsPerPoint() const noexcept
    {
        return currentSampleRate > 0.0 ? hopSize / currentSampleRate : 0.0;
    }

    int getNumDroppedBlocks() const noexcept { return ringBuffer.getNumDroppedBlocks(); }

    static constexpr int hopSize  = 256;
    static constexpr int fftOrder = 11;   // 2048 points, ~23 Hz bins at 48 kHz

private:
    void run() override;
    void processPendingAudio();
    void discardPendingAudio();

    SampleRingBuffer ringBuffer;
    LockFreeQueue<EnvelopePoint> envelopeQueue;
    ColumnRing spectrumColumns;
    StftAnalyzer stft;

    juce::AudioBuffer<float> hopBuffer;      // analysis thread only
    juce::AudioBuffer<float> monoBuffer;     // analysis thread only
    std::vector<float> columnScratch;        // analysis thread only

    std::atomic<int> consumerCount { 0 };
    bool wasActive = false;

    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisEngine)
};
