#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../dsp/AnalysisEngine.h"

/**
    Stereo field: a period goniometer with a pair of needle VU meters. The
    plot is rotated 45 degrees so mono material stands as a vertical blade,
    wide material blooms into a cloud, and out-of-phase material leans over
    horizontally — the three shapes an engineer reads without thinking.

    Dots land on a persistence image that fades a step each frame, so the
    trace trails like lit phosphor rather than strobing frame to frame.
*/
class StereoFieldView final : public juce::Component,
                              private juce::Timer
{
public:
    explicit StereoFieldView (AnalysisEngine&);
    ~StereoFieldView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The stereo sample queue feeds only this view; an inactive view stops
        its timer, and whatever queued up meanwhile is discarded on the way
        back in rather than replayed as a burst of stale audio.
    */
    void setActive (bool shouldBeActive);

private:
    void timerCallback() override;

    void drawGraticule (juce::Graphics&, juce::Rectangle<int> square) const;
    void drawVuMeter (juce::Graphics&, juce::Rectangle<float> bounds,
                      juce::StringRef channelLetter, float needlePosition) const;
    void drawCorrelationStrip (juce::Graphics&, juce::Rectangle<int> strip) const;

    juce::Rectangle<int> scopeSquare() const noexcept;
    juce::Rectangle<int> correlationStrip() const noexcept;
    bool vuColumnVisible() const noexcept;

    static constexpr int maxSamplesPerFrame = 4096;
    static constexpr int padding = 8;
    static constexpr int gap = 6;
    static constexpr int correlationHeight = 18;
    static constexpr int vuColumnWidth = 118;

    // Below this the VU column would crush the goniometer, so it yields.
    static constexpr int minWidthForVuColumn = 320;

    // A VU face reads -40..0 dBFS across its 90-degree arc, rust above -6.
    static constexpr float vuFloorDb = -40.0f;
    static constexpr float vuHotDb = -6.0f;

    static constexpr float persistenceFade = 0.10f;

    // View-side ballistics, per 60 Hz frame: the needle coasts like a coil
    // meter, and the correlation readout settles on top of the engine's own
    // ~200 ms smoothing.
    static constexpr float needleAlpha = 0.22f;
    static constexpr float correlationAlpha = 0.2f;

    AnalysisEngine& engine;

    juce::Image persistence;
    std::vector<StereoSample> scratch;
    int dotPhase = 0;

    float leftNeedle = 0.0f;
    float rightNeedle = 0.0f;
    float correlationDisplay = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoFieldView)
};
