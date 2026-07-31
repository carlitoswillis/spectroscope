#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Scanlines and vignette, drawn over everything else.

    The whole effect is baked into an image once per resize and blitted as a
    single draw, so the illusion costs one image copy per frame rather than
    thousands of lines. Real barrel distortion needs a shader — that comes free
    with the OpenGL renderer in Phase 4.

    Transparent to the mouse: this sits on top of the views but must never
    swallow their input.
*/
class CrtOverlay final : public juce::Component
{
public:
    CrtOverlay();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuild();

    juce::Image overlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrtOverlay)
};
