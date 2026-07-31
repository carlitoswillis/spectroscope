#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../dsp/AnalysisEngine.h"

/**
    Triggered oscilloscope: a 1024-sample window of the mono sum, re-armed on a
    rising zero crossing so pitched material stands still on the glass instead
    of rolling. No crossing within reach — silence, or noise that never clears
    the hysteresis band — and it free-runs from the newest samples.

    Consumes the per-sample stereo queue. That queue is single-consumer, which
    is safe here because only one lower-screen view is ever active: setActive
    gates the timer exactly as SpectrumView's does.
*/
class OscilloscopeView final : public juce::Component,
                               private juce::Timer
{
public:
    explicit OscilloscopeView (AnalysisEngine&);
    ~OscilloscopeView() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** The stereo sample queue feeds whichever view is active; an inactive
        view stops its timer so it never steals samples from the other.
    */
    void setActive (bool shouldBeActive);

    /** Drops the sample history — the sweep goes flat until new audio lands. */
    void clear();

private:
    void timerCallback() override;
    void drawGraticule (juce::Graphics&);
    void drawCursorReadout (juce::Graphics&);

    /** Age of the trigger-point sample (0 = newest), or -1 when no crossing
        was found within the search span.
    */
    int findTriggerStart() const noexcept;

    float sampleAtAge (int age) const noexcept;

    static constexpr int ringCapacity = 16384;
    static constexpr int windowLength = 1024;
    static constexpr int triggerSearchSpan = 4096;
    static constexpr int maxSamplesPerFrame = 4096;

    // Hysteresis keeps low-level noise from re-arming the trigger every few
    // samples: the signal must dip below -h before a rise above +h counts.
    static constexpr float triggerHysteresis = 0.005f;

    AnalysisEngine& engine;

    // Parenthesised size construction: braces here would build a one-element
    // initializer list instead of a sized buffer.
    std::vector<float> ring = std::vector<float> (static_cast<size_t> (ringCapacity), 0.0f);
    std::vector<StereoSample> scratch = std::vector<StereoSample> (static_cast<size_t> (maxSamplesPerFrame));
    int head = 0;          // index one past the newest sample
    int numStored = 0;

    bool lastFrameTriggered = false;

    // -1 means the pointer is outside the view.
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscilloscopeView)
};
