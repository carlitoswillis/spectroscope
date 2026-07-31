#include "InstrumentWindow.h"
#include "Theme.h"

namespace
{
    constexpr int margin      = 8;
    constexpr int labelHeight = 13;
    constexpr int screenGap   = 7;   // clearance for the recessed frame and brackets
}

//==============================================================================
/** The window's content: a patch of chassis with the same caption row and
    recessed screen the console gives every pane, so a floating instrument
    still reads as the same piece of hardware.
*/
class InstrumentWindow::Panel final : public juce::Component
{
public:
    Panel (juce::StringRef captionIn, juce::StringRef annotationIn, juce::Component& viewIn)
        : caption (captionIn), annotation (annotationIn), view (&viewIn)
    {
        addAndMakeVisible (*view);
    }

    /** The editor owns the view; hand it back rather than letting it ride
        this component's destructor into ambiguity.
    */
    void releaseView()
    {
        if (view != nullptr)
            removeChildComponent (view);

        view = nullptr;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (margin);
        area.removeFromTop (labelHeight);

        if (view != nullptr)
            view->setBounds (area.reduced (screenGap));
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (Theme::palette().shellMid, 0.0f, 0.0f,
                                                 Theme::palette().shellDark, 0.0f, static_cast<float> (getHeight()),
                                                 false));
        g.fillAll();

        auto label = getLocalBounds().reduced (margin).removeFromTop (labelHeight).reduced (10, 0);

        g.setColour (Theme::palette().bone.withAlpha (0.85f));
        g.setFont (Theme::mono (9.5f, true));
        g.drawText (Theme::spaced (caption), label.removeFromLeft (label.getWidth() / 2),
                    juce::Justification::centredLeft);

        g.setColour (Theme::palette().boneDim.withAlpha (0.65f));
        g.setFont (Theme::mono (9.0f));
        g.drawText (annotation, label, juce::Justification::centredRight);

        if (view != nullptr)
        {
            Theme::drawRecessedScreen (g, view->getBounds());
            Theme::drawCornerBrackets (g, view->getBounds().expanded (6),
                                       Theme::palette().amberDim.withAlpha (0.75f), 12, 1.4f);
        }

        const auto inset = static_cast<float> (margin) - 4.0f;
        const auto right = static_cast<float> (getWidth()) - inset;
        const auto bottom = static_cast<float> (getHeight()) - inset;

        for (const auto& corner : { juce::Point<float> (inset, inset),
                                    juce::Point<float> (right, inset),
                                    juce::Point<float> (inset, bottom),
                                    juce::Point<float> (right, bottom) })
            Theme::drawRivet (g, corner);
    }

private:
    juce::String caption, annotation;
    juce::Component* view;   // non-owning — the editor holds the instrument

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

//==============================================================================
InstrumentWindow::InstrumentWindow (juce::StringRef caption, juce::StringRef annotation,
                                    juce::Component& view)
    : juce::DocumentWindow (caption, Theme::palette().shellDark, juce::DocumentWindow::allButtons),
      panel (std::make_unique<Panel> (caption, annotation, view))
{
    setUsingNativeTitleBar (true);
    setContentNonOwned (panel.get(), false);
    setResizable (true, false);
    setAlwaysOnTop (true);
    centreWithSize (560, 320);
    setVisible (true);
}

InstrumentWindow::~InstrumentWindow()
{
    // The base destructor clears the content component last; detach the view
    // and the panel here, while both are still alive.
    panel->releaseView();
    clearContentComponent();
}

void InstrumentWindow::liveryChanged()
{
    setBackgroundColour (Theme::palette().shellDark);
    panel->repaint();
}

void InstrumentWindow::closeButtonPressed()
{
    // Closing means docking — a null handler here is a wiring bug, not a
    // condition to shrug past.
    jassert (onDock != nullptr);

    if (onDock != nullptr)
        onDock();
}

void InstrumentWindow::moved()
{
    juce::DocumentWindow::moved();

    if (onBoundsChanged != nullptr)
        onBoundsChanged (getBounds());
}

void InstrumentWindow::resized()
{
    juce::DocumentWindow::resized();

    if (onBoundsChanged != nullptr)
        onBoundsChanged (getBounds());
}
