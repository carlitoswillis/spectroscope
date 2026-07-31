#include "AnalysisEngine.h"

AnalysisEngine::AnalysisEngine()
    : juce::Thread ("Spectroscope analysis")
{
}

AnalysisEngine::~AnalysisEngine()
{
    release();
}

void AnalysisEngine::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    release();

    currentSampleRate = sampleRate;

    const auto channelsToStore = juce::jlimit (1, 2, numChannels);

    // A quarter second of headroom, and never less than a few host blocks, so a
    // scheduling hiccup on the analysis thread doesn't cost us samples.
    const auto capacity = juce::jmax (8 * juce::jmax (1, maximumBlockSize),
                                      juce::roundToInt (sampleRate * 0.25));

    ringBuffer.prepare (channelsToStore, capacity);
    hopBuffer.setSize (channelsToStore, hopSize, false, true, false);
    monoBuffer.setSize (1, hopSize, false, true, false);
    sideBuffer.setSize (1, hopSize, false, true, false);

    stft.prepare (fftOrder, hopSize, sampleRate);
    sideStft.prepare (fftOrder, hopSize, sampleRate);
    loudnessMeter.prepare (sampleRate);
    columnScratch.assign (static_cast<size_t> (stft.getNumBins()), StftAnalyzer::floorDb);
    sideColumnScratch.assign (static_cast<size_t> (sideStft.getNumBins()), StftAnalyzer::floorDb);

    // Roughly ten seconds of points: far more than any window will display, so
    // the UI only ever drops frames if it stops draining entirely.
    const auto pointsForTenSeconds = juce::roundToInt (sampleRate * 10.0 / hopSize);
    envelopeQueue.prepare (pointsForTenSeconds);
    loudnessQueue.prepare (pointsForTenSeconds);

    // Columns are much larger, so a shorter backlog. Two seconds is still far
    // more than one frame's worth of catching up.
    spectrumColumns.prepare (stft.getNumBins(), juce::roundToInt (sampleRate * 2.0 / hopSize));
    analyserColumns.prepare (stft.getNumBins(), juce::roundToInt (sampleRate * 2.0 / hopSize));
    sideSpectrumColumns.prepare (stft.getNumBins(), juce::roundToInt (sampleRate * 2.0 / hopSize));

    // The vectorscope trace carries every sample, so one second is already a
    // large backlog; anything older than that is stale for display purposes.
    stereoSamples.prepare (juce::roundToInt (sampleRate));
    scopeSamples.prepare (juce::roundToInt (sampleRate));

    smoothedCorrelation.store (1.0f, std::memory_order_relaxed);
    leftRms.store (0.0f, std::memory_order_relaxed);
    rightRms.store (0.0f, std::memory_order_relaxed);
    leftPeak.store (0.0f, std::memory_order_relaxed);
    rightPeak.store (0.0f, std::memory_order_relaxed);

    wasActive = false;

    startThread (juce::Thread::Priority::high);
}

void AnalysisEngine::release()
{
    stopThread (1000);
    ringBuffer.reset();
    currentSampleRate = 0.0;
}

void AnalysisEngine::addConsumer() noexcept
{
    consumerCount.fetch_add (1, std::memory_order_relaxed);
}

void AnalysisEngine::removeConsumer() noexcept
{
    consumerCount.fetch_sub (1, std::memory_order_relaxed);
}

void AnalysisEngine::run()
{
    // A hop is 5.3 ms at 48 kHz, so a 1 ms poll keeps latency well under one
    // frame without the cost of signalling from the audio callback.
    while (! threadShouldExit())
    {
        const auto active = hasConsumers();

        if (active)
        {
            // Coming back from idle, the sliding window holds audio from before
            // the gap. Starting clean avoids a smear across the discontinuity.
            if (! wasActive)
            {
                stft.reset();
                sideStft.reset();
                discardPendingAudio();
            }

            processPendingAudio();
        }
        else
        {
            // Nothing is drawing, so skip the FFT — but keep draining, or the
            // audio thread starts counting drops against a buffer nobody reads.
            discardPendingAudio();
        }

        wasActive = active;
        wait (1);
    }
}

void AnalysisEngine::discardPendingAudio()
{
    while (ringBuffer.getNumReady() >= hopSize)
        if (ringBuffer.read (hopBuffer, hopSize) != hopSize)
            break;
}

void AnalysisEngine::processPendingAudio()
{
    while (ringBuffer.getNumReady() >= hopSize)
    {
        if (ringBuffer.read (hopBuffer, hopSize) != hopSize)
            break;

        // Sum to mono. Per-channel and mid/side views come in Phase 6; the ring
        // buffer already carries both channels for that.
        monoBuffer.clear();
        const auto numChannels = hopBuffer.getNumChannels();

        for (int ch = 0; ch < numChannels; ++ch)
            monoBuffer.addFrom (0, 0, hopBuffer, ch, 0, hopSize, 1.0f / static_cast<float> (numChannels));

        const auto* mono = monoBuffer.getReadPointer (0);
        const auto range = juce::FloatVectorOperations::findMinAndMax (mono, hopSize);

        EnvelopePoint point;
        point.minValue = range.getStart();
        point.maxValue = range.getEnd();
        point.rms      = monoBuffer.getRMSLevel (0, 0, hopSize);

        // Decaying peak for the signal lamp: rises instantly, falls over about
        // half a second, so a lamp driven from it doesn't flicker on transients.
        const auto hopPeak = juce::jmax (point.maxValue, -point.minValue);
        const auto decayed = recentPeak.load (std::memory_order_relaxed) * 0.99f;
        recentPeak.store (juce::jmax (decayed, hopPeak), std::memory_order_relaxed);

        // A full queue means no view is draining it. Dropping is correct here.
        envelopeQueue.push (point);
        loudnessQueue.push ({ juce::Decibels::gainToDecibels (point.rms, -100.0f) });

        // Two rings, one column: the spectrogram and analyser each own an SPSC
        // ring, so the same data goes to both rather than sharing a consumer.
        if (stft.processHop (mono, columnScratch.data()))
        {
            spectrumColumns.push (columnScratch.data());
            analyserColumns.push (columnScratch.data());
        }

        // Mono input has no right channel and no side content: both channels
        // read as channel 0, the side signal is silence, and correlation is 1.
        const auto stereo      = numChannels >= 2;
        const auto* left       = hopBuffer.getReadPointer (0);
        const auto* right      = stereo ? hopBuffer.getReadPointer (1) : left;
        auto* side             = sideBuffer.getWritePointer (0);

        if (stereo)
        {
            for (int i = 0; i < hopSize; ++i)
                side[i] = (left[i] - right[i]) * 0.5f;
        }
        else
        {
            juce::FloatVectorOperations::clear (side, hopSize);
        }

        if (sideStft.processHop (side, sideColumnScratch.data()))
            sideSpectrumColumns.push (sideColumnScratch.data());

        // Mono input feeds the same pointer twice: BS.1770 sums channel energy,
        // and a dual-mono pair is the correct reading for a mono source.
        loudnessMeter.processHop (left, right, hopSize);

        // Likewise for the vectorscope and oscilloscope: one pass over the hop,
        // one push into each view's queue.
        for (int i = 0; i < hopSize; ++i)
        {
            stereoSamples.push ({ left[i], right[i] });
            scopeSamples.push ({ left[i], right[i] });
        }

        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

        for (int i = 0; i < hopSize; ++i)
        {
            sumLR += left[i] * right[i];
            sumLL += left[i] * left[i];
            sumRR += right[i] * right[i];
        }

        // Silence carries no phase information, so it reads as correlated
        // rather than letting the meter drift on numerical noise.
        const auto denominator = std::sqrt (sumLL * sumRR);
        const auto r           = denominator < 1e-12 ? 1.0f
                                                     : static_cast<float> (sumLR / denominator);

        // ~200 ms settling at 5.3 ms hops, computed here so every hop counts
        // even when the UI frame rate stutters.
        auto smoothed = smoothedCorrelation.load (std::memory_order_relaxed);
        smoothed += (r - smoothed) * 0.08f;
        smoothedCorrelation.store (smoothed, std::memory_order_relaxed);

        const auto hopLeftRms  = hopBuffer.getRMSLevel (0, 0, hopSize);
        const auto hopRightRms = stereo ? hopBuffer.getRMSLevel (1, 0, hopSize) : hopLeftRms;
        leftRms.store  (hopLeftRms,  std::memory_order_relaxed);
        rightRms.store (hopRightRms, std::memory_order_relaxed);

        const auto leftRange   = juce::FloatVectorOperations::findMinAndMax (left, hopSize);
        const auto rightRange  = stereo ? juce::FloatVectorOperations::findMinAndMax (right, hopSize)
                                        : leftRange;
        const auto hopLeftPeak  = juce::jmax (leftRange.getEnd(),  -leftRange.getStart());
        const auto hopRightPeak = juce::jmax (rightRange.getEnd(), -rightRange.getStart());

        leftPeak.store  (juce::jmax (leftPeak.load  (std::memory_order_relaxed) * 0.99f, hopLeftPeak),
                         std::memory_order_relaxed);
        rightPeak.store (juce::jmax (rightPeak.load (std::memory_order_relaxed) * 0.99f, hopRightPeak),
                         std::memory_order_relaxed);
    }
}
