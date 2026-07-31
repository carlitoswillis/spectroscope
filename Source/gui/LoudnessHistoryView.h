#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../dsp/AnalysisEngine.h"

/**
    Momentary level over the last minute, drawn as a strip-chart recorder: a
    phosphor pen on ruled paper, newest at the right edge like the waveform.

    One value per analysis hop comes off the loudness queue, so the chart is
    measuring exactly the audio the other views drew — the same hops, the same
    cadence, just integrated to a single level.
*/
class LoudnessHistoryView final : public juce::Component,
                                  private juce::Timer
{
public:
    explicit LoudnessHistoryView (AnalysisEngine&);
    ~LoudnessHistoryView() override;

    void paint (juce::Graphics&) override;

    /** An inactive view stops its timer; on return it discards whatever the
        queue accumulated while it was dark.
    */
    void setActive (bool shouldBeActive);

private:
    void timerCallback() override;
    void drawChartPaper (juce::Graphics&);

    float toY (float db) const noexcept;

    static constexpr int maxPointsPerFrame = 256;
    static constexpr int historySeconds = 60;

    static constexpr float dbCeiling = 0.0f;
    static constexpr float dbFloor = -72.0f;
    static constexpr float hotDb = -9.0f;   // above this the pen is in the red

    // Readout smoothing per frame — fast enough to track, slow enough to read.
    static constexpr float readoutAlpha = 0.25f;

    AnalysisEngine& engine;

    std::vector<LoudnessPoint> scratch;

    // Ring of per-hop momentary dB spanning historySeconds. Chronological index
    // i counts up from the oldest slot; the newest hop sits at ringSize - 1.
    std::vector<float> history;
    int ringSize = 0;
    int writeIndex = 0;
    int validCount = 0;
    double configuredRate = 0.0;

    float targetDb = -100.0f;
    float smoothedDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessHistoryView)
};
