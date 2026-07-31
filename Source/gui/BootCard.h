#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Boot test card: the alignment pattern a rack unit throws up on the glass
    before it's warmed through, standing in front of the console for the
    first couple of seconds after a standalone launch.

    Fully opaque while it's showing — nothing behind it needs to paint. A
    Timer holds it at full strength for a beat, fades it over half a second,
    then drops it invisible so it stops costing a paint entirely. Any click
    skips straight to the fade; nobody sits through a test card twice.
*/
class BootCard final : public juce::Component,
                       private juce::Timer
{
public:
    BootCard();
    ~BootCard() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Fired once the fade has finished and the card has gone invisible. */
    std::function<void()> onDismissed;

private:
    void timerCallback() override;

    /** Starts the fade if it hasn't already — idempotent, so a click during
        the fade is a no-op rather than restarting it.
    */
    void beginFade();

    void drawColourBars (juce::Graphics&, juce::Rectangle<int> area);
    void drawCrosshatch (juce::Graphics&, juce::Rectangle<int> area);
    void drawWordmark (juce::Graphics&, juce::Rectangle<int> area);

    static constexpr int holdMs = 2000;
    static constexpr int fadeMs = 500;

    juce::uint32 startTime = 0;
    juce::uint32 fadeStartTime = 0;
    bool fading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BootCard)
};
