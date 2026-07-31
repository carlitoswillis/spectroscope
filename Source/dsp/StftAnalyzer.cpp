#include "StftAnalyzer.h"

#include <algorithm>
#include <cstring>
#include <numeric>

void StftAnalyzer::prepare (int fftOrder, int hopSize, double sampleRate)
{
    fftSize = 1 << fftOrder;
    hop = juce::jlimit (1, fftSize, hopSize);
    currentSampleRate = sampleRate;

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>> (
        static_cast<size_t> (fftSize),
        juce::dsp::WindowingFunction<float>::hann,
        false);

    slidingWindow.assign (static_cast<size_t> (fftSize), 0.0f);
    fftData.assign (static_cast<size_t> (fftSize) * 2, 0.0f);

    // A Hann window sums to N/2, and a real sine splits its energy between the
    // positive and negative frequency bins. Scaling by 2/sum puts a full-scale
    // sine at 0 dB.
    std::vector<float> unitWindow (static_cast<size_t> (fftSize), 1.0f);
    window->multiplyWithWindowingTable (unitWindow.data(), static_cast<size_t> (fftSize));

    const auto windowSum = std::accumulate (unitWindow.begin(), unitWindow.end(), 0.0f);
    magnitudeScale = windowSum > 0.0f ? 2.0f / windowSum : 1.0f;

    samplesUntilFull = fftSize;
}

void StftAnalyzer::reset()
{
    std::fill (slidingWindow.begin(), slidingWindow.end(), 0.0f);
    samplesUntilFull = fftSize;
}

bool StftAnalyzer::processHop (const float* monoHop, float* magnitudesDb) noexcept
{
    if (fft == nullptr || fftSize == 0)
        return false;

    // Slide the history left by one hop and append the new samples.
    const auto retained = static_cast<size_t> (fftSize - hop);
    std::memmove (slidingWindow.data(),
                  slidingWindow.data() + hop,
                  retained * sizeof (float));
    std::memcpy (slidingWindow.data() + retained,
                 monoHop,
                 static_cast<size_t> (hop) * sizeof (float));

    if (samplesUntilFull > 0)
    {
        samplesUntilFull -= hop;

        if (samplesUntilFull > 0)
            return false;
    }

    std::copy (slidingWindow.begin(), slidingWindow.end(), fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    window->multiplyWithWindowingTable (fftData.data(), static_cast<size_t> (fftSize));
    fft->performFrequencyOnlyForwardTransform (fftData.data());

    const auto numBins = getNumBins();

    for (int bin = 0; bin < numBins; ++bin)
    {
        // DC and Nyquist aren't mirrored, so they don't get the factor of two.
        const auto scale = (bin == 0 || bin == numBins - 1) ? magnitudeScale * 0.5f
                                                            : magnitudeScale;
        const auto magnitude = fftData[static_cast<size_t> (bin)] * scale;

        magnitudesDb[bin] = magnitude > 0.0f
            ? juce::jmax (floorDb, juce::Decibels::gainToDecibels (magnitude, floorDb))
            : floorDb;
    }

    return true;
}
