#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../dsp/AnalysisEngine.h"

/**
    Instantaneous spectrum: magnitude against a log frequency axis, the view a
    channel-strip analyser gives you — but drawn as a phosphor trace, not a bar
    chart.

    Two traces: an averaged curve that moves like the sound, and a peak-hold
    ghost that decays slowly enough to read resonances off. Both come from the
    same STFT columns the spectrogram scrolls, so switching views never changes
    what is being measured.
*/
class SpectrumView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit SpectrumView (AnalysisEngine&);
    ~SpectrumView() override;

    void paint (juce::Graphics&) override;

    /** The spectrum column queue feeds whichever view is active; an inactive
        view stops its timer so it never steals columns from the other.
    */
    void setActive (bool shouldBeActive);

private:
    void timerCallback() override;
    void drawGraticule (juce::Graphics&);

    /** Averaged level at a fractional bin, max-over-span when a pixel covers
        several bins and interpolated when several pixels cover one bin.
    */
    float levelAt (const std::vector<float>& source, float binLow, float binHigh) const;

    static constexpr int maxColumnsPerFrame = 128;
    static constexpr double minFrequencyHz = 30.0;

    // Per incoming column (~5 ms of audio): the average chases the input, the
    // peak trace falls at a readable rate. Matched to the ~187 columns/s an
    // FFT hop of 256 produces at 48 kHz.
    static constexpr float averageAlpha = 0.18f;
    static constexpr float peakDecayDbPerColumn = 0.08f;

    static constexpr float dbFloor = -75.0f;
    static constexpr float dbCeiling = 0.0f;

    AnalysisEngine& engine;

    std::vector<float> scratch;    // maxColumnsPerFrame * numBins
    std::vector<float> averaged;   // dB per bin, exponential average
    std::vector<float> peakHold;   // dB per bin, slow decay
    int numBins = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};
