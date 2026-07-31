#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

/**
    Short-time Fourier transform with overlap.

    FFT size and hop size are independent: the window sets frequency resolution,
    the hop sets time resolution, and the overlap between them buys both. A
    2048-point window advanced 256 samples at a time gives ~23 Hz bins updating
    every 5.3 ms.

    Magnitudes come out in dBFS, scaled so a full-scale sine sitting on a bin
    centre reads 0 dB.
*/
class StftAnalyzer
{
public:
    StftAnalyzer() = default;

    /** Allocates. Not safe to call while processHop is running. */
    void prepare (int fftOrder, int hopSize, double sampleRate);

    /** Feeds one hop of mono samples and writes getNumBins() dB values.

        Returns false until enough audio has arrived to fill the first window,
        so callers can skip the partially-populated frames at startup.
    */
    bool processHop (const float* monoHop, float* magnitudesDb) noexcept;

    /** Discards the sliding window, e.g. after a transport jump. */
    void reset();

    int getNumBins() const noexcept  { return fftSize / 2 + 1; }
    int getFftSize() const noexcept  { return fftSize; }

    float getBinFrequency (int bin) const noexcept
    {
        return fftSize > 0 ? static_cast<float> (bin * currentSampleRate / fftSize) : 0.0f;
    }

    /** Bin whose centre is nearest the given frequency. */
    int getBinForFrequency (double frequency) const noexcept
    {
        return currentSampleRate > 0.0
            ? juce::roundToInt (frequency * fftSize / currentSampleRate)
            : 0;
    }

    static constexpr float floorDb = -100.0f;

private:
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    std::vector<float> slidingWindow;   // fftSize samples of history
    std::vector<float> fftData;         // 2 * fftSize, as JUCE requires

    int fftSize = 0;
    int hop = 0;
    int samplesUntilFull = 0;
    float magnitudeScale = 1.0f;
    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StftAnalyzer)
};
