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

    // Roughly ten seconds of points: far more than any window will display, so
    // the UI only ever drops frames if it stops draining entirely.
    envelopeQueue.prepare (juce::roundToInt (sampleRate * 10.0 / hopSize));

    startThread (juce::Thread::Priority::high);
}

void AnalysisEngine::release()
{
    stopThread (1000);
    ringBuffer.reset();
    currentSampleRate = 0.0;
}

void AnalysisEngine::run()
{
    // A hop is 5.3 ms at 48 kHz, so a 1 ms poll keeps latency well under one
    // frame without the cost of signalling from the audio callback.
    while (! threadShouldExit())
    {
        processPendingAudio();
        wait (1);
    }
}

void AnalysisEngine::processPendingAudio()
{
    while (ringBuffer.getNumReady() >= hopSize)
    {
        if (ringBuffer.read (hopBuffer, hopSize) != hopSize)
            break;

        // Sum to mono for the waveform. Per-channel and mid/side views come in
        // Phase 6; the ring buffer already carries both channels for that.
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

        // A full queue means no view is draining it. Dropping is correct here.
        envelopeQueue.push (point);
    }
}
