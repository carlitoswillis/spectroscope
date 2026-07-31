#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleRingBuffer.h"
#include "LockFreeQueue.h"
#include "ColumnRing.h"
#include "StftAnalyzer.h"
#include "LoudnessMeter.h"

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

/** One frame of the stereo field. Pushed for every sample of every processed
    hop, so a vectorscope can draw the actual trace rather than a summary.
*/
struct StereoSample
{
    float left  = 0.0f;
    float right = 0.0f;
};

/** One item per hop: RMS of the mid (mono-sum) hop in dBFS, floored at -100. */
struct LoudnessPoint
{
    float momentaryDb = -100.0f;
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

    /** The same mid columns as getSpectrumColumns(), duplicated because each
        SPSC ring supports exactly one consumer and the spectrogram and analyser
        may now be visible at once. Single consumer: SpectrumView.
    */
    ColumnRing& getAnalyserColumns() noexcept                 { return analyserColumns; }

    /** Every sample of every processed hop. Single consumer: StereoFieldView. */
    LockFreeQueue<StereoSample>& getStereoSamples() noexcept  { return stereoSamples; }

    /** The same samples as getStereoSamples(), duplicated for the same reason
        as the analyser columns. Single consumer: OscilloscopeView.
    */
    LockFreeQueue<StereoSample>& getScopeSamples() noexcept   { return scopeSamples; }

    /** One item per hop. Single consumer: LoudnessHistoryView. */
    LockFreeQueue<LoudnessPoint>& getLoudnessQueue() noexcept { return loudnessQueue; }

    /** BS.1770 loudness and true peak, fed every hop. Getters are relaxed
        atomics, so views read it directly rather than through a queue.
    */
    LoudnessMeter& getLoudnessMeter() noexcept { return loudnessMeter; }

    /** Any thread. The reset lands at the next processed hop. */
    void resetLoudness() noexcept { loudnessMeter.requestReset(); }

    /** STFT dB columns of the side channel (L-R)/2, same bin count and cadence
        as getSpectrumColumns(). Single consumer: SpectrumView.
    */
    ColumnRing& getSideSpectrumColumns() noexcept             { return sideSpectrumColumns; }

    /** Pearson r of L/R per hop, smoothed over roughly 200 ms. Reads 1.0 for
        mono input and for silence.
    */
    float getCorrelation() const noexcept { return smoothedCorrelation.load (std::memory_order_relaxed); }

    /** Linear RMS of the last hop, per channel. Mono input reports the same
        value on both.
    */
    float getLeftRms() const noexcept  { return leftRms.load (std::memory_order_relaxed); }
    float getRightRms() const noexcept { return rightRms.load (std::memory_order_relaxed); }

    /** Per-channel decaying peak, same decay as getRecentPeak(). */
    float getLeftPeak() const noexcept  { return leftPeak.load (std::memory_order_relaxed); }
    float getRightPeak() const noexcept { return rightPeak.load (std::memory_order_relaxed); }

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

    /** Decaying peak, for a signal-present indicator. */
    float getRecentPeak() const noexcept { return recentPeak.load (std::memory_order_relaxed); }

    static constexpr int hopSize  = 256;
    static constexpr int fftOrder = 11;   // 2048 points, ~23 Hz bins at 48 kHz

private:
    void run() override;
    void processPendingAudio();
    void discardPendingAudio();

    SampleRingBuffer ringBuffer;
    LockFreeQueue<EnvelopePoint> envelopeQueue;
    LockFreeQueue<StereoSample> stereoSamples;
    LockFreeQueue<StereoSample> scopeSamples;
    LockFreeQueue<LoudnessPoint> loudnessQueue;
    ColumnRing spectrumColumns;
    ColumnRing analyserColumns;
    ColumnRing sideSpectrumColumns;
    StftAnalyzer stft;
    StftAnalyzer sideStft;
    LoudnessMeter loudnessMeter;

    juce::AudioBuffer<float> hopBuffer;      // analysis thread only
    juce::AudioBuffer<float> monoBuffer;     // analysis thread only
    juce::AudioBuffer<float> sideBuffer;     // analysis thread only
    std::vector<float> columnScratch;        // analysis thread only
    std::vector<float> sideColumnScratch;    // analysis thread only

    std::atomic<int> consumerCount { 0 };
    std::atomic<float> recentPeak { 0.0f };
    std::atomic<float> smoothedCorrelation { 1.0f };
    std::atomic<float> leftRms { 0.0f };
    std::atomic<float> rightRms { 0.0f };
    std::atomic<float> leftPeak { 0.0f };
    std::atomic<float> rightPeak { 0.0f };
    bool wasActive = false;

    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisEngine)
};
