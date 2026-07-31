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

    /** Seconds currently visible, for the time axis to label. */
    double getVisibleTimeSpan() const noexcept;

private:
    void timerCallback() override;

    static constexpr int ringCapacity = 8192;
    static constexpr int maxPointsPerFrame = 2048;

    AnalysisEngine& engine;

    std::vector<EnvelopePoint> ring { static_cast<size_t> (ringCapacity) };
    std::vector<EnvelopePoint> scratch { static_cast<size_t> (maxPointsPerFrame) };
    int head = 0;          // index one past the newest point
    int numStored = 0;

    const EnvelopePoint* pointAtAge (int age) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
