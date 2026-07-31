#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../dsp/AnalysisEngine.h"

/**
    Scrolling min/max waveform, newest sample at the right edge.

    One envelope point per pixel column, so the horizontal scale is exactly the
    analysis hop rate — the same clock the spectrogram will scroll on, which is
    what keeps the two views aligned.

    CPU-drawn for now. Phase 4 moves this to OpenGL; the display ring below is
    already shaped like the vertex buffer that will replace it.
*/
class WaveformView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit WaveformView (AnalysisEngine&);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Drops all accumulated history — dark glass, as though just powered on. */
    void clear();

    /** Seconds currently visible, for the time axis to label. */
    double getVisibleTimeSpan() const noexcept;

private:
    void timerCallback() override;
    void drawGraticule (juce::Graphics&);
    void drawCursorReadout (juce::Graphics&);

    static constexpr int ringCapacity = 8192;
    static constexpr int maxPointsPerFrame = 2048;

    AnalysisEngine& engine;

    std::vector<EnvelopePoint> ring { static_cast<size_t> (ringCapacity) };
    std::vector<EnvelopePoint> scratch { static_cast<size_t> (maxPointsPerFrame) };
    int head = 0;          // index one past the newest point
    int numStored = 0;

    // One entry per visible pixel column. Members rather than paint() locals
    // so the 60 Hz repaint reuses capacity instead of hitting the allocator.
    struct Sample { float x, maxValue, minValue, rms; };

    std::vector<Sample> columnScratch;
    std::vector<Sample> smoothedScratch;

    const EnvelopePoint* pointAtAge (int age) const noexcept;

    // -1 means the pointer is outside the view.
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
