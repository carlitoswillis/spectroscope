#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../dsp/AnalysisEngine.h"

/**
    Broadcast compliance meter drawn as a strip-chart recorder: a phosphor pen
    on ruled paper, newest at the right edge like the waveform, with a BS.1770
    readout block, a selectable loudness target with a verdict lamp, and
    numbered pen-tick markers that ride the paper.

    One value per analysis hop comes off the loudness queue, so the chart is
    measuring exactly the audio the other views drew — the same hops, the same
    cadence, just integrated to a single level. The LUFS figures come straight
    off the engine's LoudnessMeter atomics on the timer.
*/
class LoudnessHistoryView final : public juce::Component,
                                  private juce::Timer
{
public:
    explicit LoudnessHistoryView (AnalysisEngine&);
    ~LoudnessHistoryView() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** An inactive view stops its timer; on return it discards whatever the
        queue accumulated while it was dark.
    */
    void setActive (bool shouldBeActive);

    /** Tears the chart off the recorder — history, markers and the meter's
        integration all start fresh.
    */
    void clear();

    /** Stamps a numbered pen tick at the newest chart position. It scrolls
        with the paper and falls off the left edge like everything else.
    */
    void addMarker();

    /** Steps OFF -> EBU -23 -> SPOTIFY -14 -> YOUTUBE -14 -> ATSC -24. */
    void cycleTarget();

    juce::String getTargetName() const;

    /** "seconds,momentary_db" header then one row per stored hop, oldest
        first, seconds counted back from the newest sample so history reads
        as negative time.
    */
    void appendCsv (juce::String& out) const;

private:
    struct Marker
    {
        juce::int64 hop = 0;    // totalHops when stamped — position is its age
        int number = 0;
    };

    void timerCallback() override;
    void drawChartPaper (juce::Graphics&);
    void drawMarkers (juce::Graphics&);
    void drawReadout (juce::Graphics&);
    void drawCursor (juce::Graphics&);

    float toY (float db) const noexcept;

    /** Loudest stored hop in pixel column x, or the -1000 sentinel where the
        paper has not been written yet.
    */
    float columnDbAt (int x) const noexcept;

    static constexpr int maxPointsPerFrame = 256;
    static constexpr int historySeconds = 60;
    static constexpr int maxMarkers = 32;

    static constexpr float dbCeiling = 0.0f;
    static constexpr float dbFloor = -72.0f;
    static constexpr float hotDb = -9.0f;   // above this the pen is in the red

    AnalysisEngine& engine;

    std::vector<LoudnessPoint> scratch;

    // Ring of per-hop momentary dB spanning historySeconds. Chronological index
    // i counts up from the oldest slot; the newest hop sits at ringSize - 1.
    std::vector<float> history;
    int ringSize = 0;
    int writeIndex = 0;
    int validCount = 0;
    double configuredRate = 0.0;

    // Every hop ever consumed; marker ages are measured against it, so the
    // counter never resets while the chart is merely scrolling.
    juce::int64 totalHops = 0;
    std::vector<Marker> markers;
    int markerCounter = 0;

    int targetIndex = 0;

    // Meter values cached on the timer, so paint reads each atomic exactly once
    // per frame and a repaint fires only when a figure actually moved.
    float momentaryLufs = -100.0f;
    float shortTermLufs = -100.0f;
    float integratedLufs = -100.0f;
    float loudnessRange = -100.0f;
    float maxTruePeakDb = -100.0f;

    juce::Rectangle<float> targetPlate;   // set by paint; hit-tested in mouseDown
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessHistoryView)
};
