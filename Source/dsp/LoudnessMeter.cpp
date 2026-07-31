#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
    // The BS.1770-4 offset: the K filter lifts a 997 Hz tone by this much, and
    // subtracting it back out makes such a tone read its own power in dB.
    constexpr double loudnessOffsetDb = -0.691;

    constexpr double absoluteGateLufs = -70.0;
    constexpr double relativeGateLu = 10.0;
    constexpr double rangeGateLu = 20.0;
}

void LoudnessMeter::prepare (double sampleRate)
{
    currentSampleRate = sampleRate;
    subBlockLength = juce::jmax (1, juce::roundToInt (0.1 * sampleRate));

    // K-weighting stage 1: the spherical-head shelf, ~+4 dB above ~1.5 kHz.
    // These are the analogue prototype constants behind the published 48 kHz
    // table; bilinear-transforming them at the actual rate reproduces that
    // table exactly and stays correct at every other rate.
    {
        const auto gainDb = 3.999843853973347;
        const auto f0 = 1681.974450955533;
        const auto q = 0.7071752369554196;

        const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const auto vh = std::pow (10.0, gainDb / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;

        for (auto& stage : shelf)
        {
            stage.b0 = (vh + vb * k / q + k * k) / a0;
            stage.b1 = 2.0 * (k * k - vh) / a0;
            stage.b2 = (vh - vb * k / q + k * k) / a0;
            stage.a1 = 2.0 * (k * k - 1.0) / a0;
            stage.a2 = (1.0 - k / q + k * k) / a0;
        }
    }

    // Stage 2: the RLB high-pass. The numerator stays {1, -2, 1} as published,
    // so only the poles depend on the rate.
    {
        const auto f0 = 38.13547087602444;
        const auto q = 0.5003270373238773;

        const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const auto a0 = 1.0 + k / q + k * k;

        for (auto& stage : highPass)
        {
            stage.b0 = 1.0;
            stage.b1 = -2.0;
            stage.b2 = 1.0;
            stage.a1 = 2.0 * (k * k - 1.0) / a0;
            stage.a2 = (1.0 - k / q + k * k) / a0;
        }
    }

    absoluteGatePower = std::pow (10.0, (absoluteGateLufs - loudnessOffsetDb) / 10.0);

    integratedPowers.assign (static_cast<size_t> (maxHistoryBlocks), 0.0);
    shortTermPowers.assign (static_cast<size_t> (maxHistoryBlocks), 0.0);
    percentileScratch.assign (static_cast<size_t> (maxHistoryBlocks), 0.0);

    // True-peak interpolator: a 48-tap windowed sinc for 4x oversampling,
    // split into four 12-tap phases. Each phase is normalised to unity DC
    // gain so the interpolation itself adds no level error.
    {
        constexpr int numTaps = numPhases * tapsPerPhase;
        double taps[numTaps];

        for (int m = 0; m < numTaps; ++m)
        {
            const auto t = (m - (numTaps - 1) * 0.5) / numPhases;
            const auto sinc = std::sin (juce::MathConstants<double>::pi * t)
                              / (juce::MathConstants<double>::pi * t);
            const auto hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * m
                                                    / (numTaps - 1));
            taps[m] = sinc * hann;
        }

        for (int phase = 0; phase < numPhases; ++phase)
        {
            auto sum = 0.0;

            for (int k = 0; k < tapsPerPhase; ++k)
                sum += taps[k * numPhases + phase];

            for (int k = 0; k < tapsPerPhase; ++k)
                firPhases[phase][k] = taps[k * numPhases + phase] / sum;
        }
    }

    resetState();
}

void LoudnessMeter::requestReset() noexcept
{
    resetRequested.store (true, std::memory_order_relaxed);
}

void LoudnessMeter::resetState() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        shelf[ch].resetState();
        highPass[ch].resetState();

        std::fill (std::begin (peakHistory[ch]), std::end (peakHistory[ch]), 0.0);
    }

    subBlockPos = 0;
    subBlockSum = 0.0;
    subBlockRingPos = 0;
    completedSubBlocks = 0;
    std::fill (std::begin (subBlockPowers), std::end (subBlockPowers), 0.0);

    numIntegratedPowers = 0;
    numShortTermPowers = 0;

    peakWritePos = 0;
    maxTruePeakLinear = 0.0;

    momentaryLufs.store (noDataLufs, std::memory_order_relaxed);
    shortTermLufs.store (noDataLufs, std::memory_order_relaxed);
    integratedLufs.store (noDataLufs, std::memory_order_relaxed);
    loudnessRange.store (0.0f, std::memory_order_relaxed);
    maxTruePeakDb.store (noDataLufs, std::memory_order_relaxed);
}

void LoudnessMeter::processHop (const float* left, const float* right, int numSamples) noexcept
{
    if (subBlockLength == 0)
        return;

    if (resetRequested.exchange (false, std::memory_order_relaxed))
        resetState();

    for (int i = 0; i < numSamples; ++i)
    {
        const double in[2] = { left[i], right[i] };

        // Loudness path: K-weight, square, accumulate into the 100 ms step.
        const auto kL = highPass[0].process (shelf[0].process (in[0]));
        const auto kR = highPass[1].process (shelf[1].process (in[1]));

        subBlockSum += kL * kL + kR * kR;

        if (++subBlockPos == subBlockLength)
            finishSubBlock();

        // True-peak path: the unweighted samples through the interpolator.
        // The history is mirrored at +tapsPerPhase so x[n - k] always lives
        // at writePos + tapsPerPhase - k and each dot product reads linearly.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* history = peakHistory[ch];
            history[peakWritePos] = in[ch];
            history[peakWritePos + tapsPerPhase] = in[ch];

            maxTruePeakLinear = juce::jmax (maxTruePeakLinear, std::abs (in[ch]));

            const auto* newest = history + peakWritePos + tapsPerPhase;

            for (int phase = 0; phase < numPhases; ++phase)
            {
                auto value = 0.0;

                for (int k = 0; k < tapsPerPhase; ++k)
                    value += firPhases[phase][k] * newest[-k];

                maxTruePeakLinear = juce::jmax (maxTruePeakLinear, std::abs (value));
            }
        }

        peakWritePos = (peakWritePos + 1) % tapsPerPhase;
    }

    if (maxTruePeakLinear > 0.0)
        maxTruePeakDb.store (juce::Decibels::gainToDecibels (static_cast<float> (maxTruePeakLinear),
                                                             noDataLufs),
                             std::memory_order_relaxed);
}

void LoudnessMeter::finishSubBlock() noexcept
{
    subBlockPowers[subBlockRingPos] = subBlockSum / subBlockLength;
    subBlockRingPos = (subBlockRingPos + 1) % shortTermSubBlocks;
    subBlockSum = 0.0;
    subBlockPos = 0;
    ++completedSubBlocks;

    const auto meanOfLast = [this] (int count) noexcept
    {
        auto sum = 0.0;

        for (int i = 1; i <= count; ++i)
            sum += subBlockPowers[(subBlockRingPos - i + shortTermSubBlocks) % shortTermSubBlocks];

        return sum / count;
    };

    if (completedSubBlocks >= momentarySubBlocks)
    {
        const auto power = meanOfLast (momentarySubBlocks);
        momentaryLufs.store (powerToLufs (power), std::memory_order_relaxed);

        // The absolute gate is applied on the way in; the relative gate has
        // to wait until the whole distribution is known.
        if (power > absoluteGatePower && numIntegratedPowers < maxHistoryBlocks)
            integratedPowers[static_cast<size_t> (numIntegratedPowers++)] = power;
    }

    if (completedSubBlocks >= shortTermSubBlocks)
    {
        const auto power = meanOfLast (shortTermSubBlocks);
        shortTermLufs.store (powerToLufs (power), std::memory_order_relaxed);

        if (power > absoluteGatePower && numShortTermPowers < maxHistoryBlocks)
            shortTermPowers[static_cast<size_t> (numShortTermPowers++)] = power;
    }

    // Both gate passes walk the whole history, so they run once a second
    // rather than per block.
    if (completedSubBlocks % recomputeInterval == 0)
    {
        updateIntegrated();
        updateLoudnessRange();
    }
}

void LoudnessMeter::updateIntegrated() noexcept
{
    if (numIntegratedPowers == 0)
    {
        integratedLufs.store (noDataLufs, std::memory_order_relaxed);
        return;
    }

    auto sum = 0.0;

    for (int i = 0; i < numIntegratedPowers; ++i)
        sum += integratedPowers[static_cast<size_t> (i)];

    const auto threshold = (sum / numIntegratedPowers)
                           * std::pow (10.0, -relativeGateLu / 10.0);

    auto gatedSum = 0.0;
    int gatedCount = 0;

    for (int i = 0; i < numIntegratedPowers; ++i)
    {
        const auto power = integratedPowers[static_cast<size_t> (i)];

        if (power > threshold)
        {
            gatedSum += power;
            ++gatedCount;
        }
    }

    integratedLufs.store (gatedCount > 0 ? powerToLufs (gatedSum / gatedCount) : noDataLufs,
                          std::memory_order_relaxed);
}

void LoudnessMeter::updateLoudnessRange() noexcept
{
    if (numShortTermPowers < 2)
    {
        loudnessRange.store (0.0f, std::memory_order_relaxed);
        return;
    }

    auto sum = 0.0;

    for (int i = 0; i < numShortTermPowers; ++i)
        sum += shortTermPowers[static_cast<size_t> (i)];

    const auto threshold = (sum / numShortTermPowers)
                           * std::pow (10.0, -rangeGateLu / 10.0);

    int kept = 0;

    for (int i = 0; i < numShortTermPowers; ++i)
    {
        const auto power = shortTermPowers[static_cast<size_t> (i)];

        if (power > threshold)
            percentileScratch[static_cast<size_t> (kept++)] = power;
    }

    if (kept < 2)
    {
        loudnessRange.store (0.0f, std::memory_order_relaxed);
        return;
    }

    const auto lowIndex = static_cast<int> (std::lround (0.10 * (kept - 1)));
    const auto highIndex = static_cast<int> (std::lround (0.95 * (kept - 1)));

    const auto begin = percentileScratch.begin();
    std::nth_element (begin, begin + lowIndex, begin + kept);
    const auto lowPower = percentileScratch[static_cast<size_t> (lowIndex)];

    std::nth_element (begin, begin + highIndex, begin + kept);
    const auto highPower = percentileScratch[static_cast<size_t> (highIndex)];

    const auto range = lowPower > 0.0 ? 10.0 * std::log10 (highPower / lowPower) : 0.0;
    loudnessRange.store (juce::jmax (0.0f, static_cast<float> (range)), std::memory_order_relaxed);
}

float LoudnessMeter::powerToLufs (double power) noexcept
{
    if (power <= 0.0)
        return noDataLufs;

    return juce::jmax (noDataLufs,
                       static_cast<float> (loudnessOffsetDb + 10.0 * std::log10 (power)));
}
