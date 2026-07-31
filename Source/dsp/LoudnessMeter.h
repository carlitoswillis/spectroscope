#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

/**
    BS.1770-4 loudness and true peak over a stereo feed.

    The K-weighting biquads are derived from the analogue prototype at the
    actual sample rate rather than copied from the 48 kHz table, so the meter
    reads the same at 44.1 or 96 kHz. Loudness comes out of 400 ms blocks
    advanced 100 ms at a time: momentary is one block, short-term averages
    3 s of them, and integrated applies the two-stage gate (absolute -70 LUFS,
    then relative -10 LU) over every block since the last reset. Loudness
    range follows EBU Tech 3342: short-term values, a -20 LU relative gate,
    and the spread between the 10th and 95th percentiles.

    True peak is the ITU 4x-oversampled estimate, a 48-tap windowed-sinc
    polyphase interpolator, tracked as a maximum since reset.

    processHop runs on the analysis thread and never allocates; the getters
    are relaxed atomics readable from anywhere. LUFS values at or below -100
    mean no data yet.
*/
class LoudnessMeter
{
public:
    LoudnessMeter() = default;

    /** Allocates everything, including the block histories. Not safe to call
        while processHop is running.
    */
    void prepare (double sampleRate);

    /** Any thread. Consumed at the start of the next processHop. */
    void requestReset() noexcept;

    /** Analysis thread. Non-allocating. */
    void processHop (const float* left, const float* right, int numSamples) noexcept;

    float getMomentaryLufs() const noexcept  { return momentaryLufs.load (std::memory_order_relaxed); }
    float getShortTermLufs() const noexcept  { return shortTermLufs.load (std::memory_order_relaxed); }
    float getIntegratedLufs() const noexcept { return integratedLufs.load (std::memory_order_relaxed); }

    /** LU spread of the gated short-term distribution; 0 until there is
        enough history to take percentiles.
    */
    float getLoudnessRange() const noexcept  { return loudnessRange.load (std::memory_order_relaxed); }

    float getMaxTruePeakDb() const noexcept  { return maxTruePeakDb.load (std::memory_order_relaxed); }

    static constexpr float noDataLufs = -120.0f;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        double process (double x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void resetState() noexcept { z1 = z2 = 0.0; }
    };

    void resetState() noexcept;
    void finishSubBlock() noexcept;
    void updateIntegrated() noexcept;
    void updateLoudnessRange() noexcept;

    static float powerToLufs (double power) noexcept;

    // K-weighting per channel: the shelving stage, then the RLB high-pass.
    Biquad shelf[2];
    Biquad highPass[2];

    // 100 ms sub-blocks; four of them make one 400 ms block at 75% overlap,
    // thirty make the 3 s short-term window.
    static constexpr int momentarySubBlocks = 4;
    static constexpr int shortTermSubBlocks = 30;
    static constexpr int recomputeInterval = 10;   // gate passes once per second

    int subBlockLength = 0;
    int subBlockPos = 0;
    double subBlockSum = 0.0;                      // K-weighted squares, both channels
    double subBlockPowers[shortTermSubBlocks] = {};
    int subBlockRingPos = 0;
    int completedSubBlocks = 0;

    // Gated block histories, one entry per 100 ms step. Capped at two hours:
    // beyond that the integrated value and range simply stop taking new
    // blocks rather than growing without bound.
    static constexpr int maxHistoryBlocks = 72000;

    std::vector<double> integratedPowers;
    std::vector<double> shortTermPowers;
    std::vector<double> percentileScratch;
    int numIntegratedPowers = 0;
    int numShortTermPowers = 0;

    double absoluteGatePower = 0.0;                // -70 LUFS as linear power

    // 4x true-peak interpolator: 48 taps split across the phases, with a
    // double-length history so each dot product reads linearly.
    static constexpr int numPhases = 4;
    static constexpr int tapsPerPhase = 12;

    double firPhases[numPhases][tapsPerPhase] = {};
    double peakHistory[2][tapsPerPhase * 2] = {};
    int peakWritePos = 0;
    double maxTruePeakLinear = 0.0;

    std::atomic<bool> resetRequested { false };
    std::atomic<float> momentaryLufs { noDataLufs };
    std::atomic<float> shortTermLufs { noDataLufs };
    std::atomic<float> integratedLufs { noDataLufs };
    std::atomic<float> loudnessRange { 0.0f };
    std::atomic<float> maxTruePeakDb { noDataLufs };

    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeter)
};
