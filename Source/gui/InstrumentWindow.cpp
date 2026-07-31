#include "InstrumentWindow.h"
#include "Theme.h"

namespace
{
    constexpr int margin      = 8;
    constexpr int labelHeight = 13;
    constexpr int screenGap   = 7;   // clearance for the recessed frame and brackets

    // Feed mode: a chrome-less capture surface for OBS.
    constexpr int feedDragStripHeight  = 14;   // hover-only strip along the top edge
    constexpr int feedGlyphSize        = 12;
    constexpr int feedGlyphInset       = 3;
    constexpr int defaultTitleBarHeight = 26;  // DocumentWindow's own built-in default
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

    /** Invoked when the corner glyph is clicked — the window owns the chrome
        change (native title bar, chassis), this panel only owns the layout
        and paint of its own feed state.
    */
    std::function<void()> onToggleFeedMode;

    void setFeedMode (bool shouldBeFeedMode)
    {
        if (feedMode == shouldBeFeedMode)
            return;

        feedMode = shouldBeFeedMode;
        dragStripHovered = false;
        draggingViaStrip = false;
        resized();
        repaint();
    }

    bool isFeedMode() const noexcept { return feedMode; }

    void resized() override
    {
        if (feedMode)
        {
            auto area = getLocalBounds();
            area.removeFromTop (feedDragStripHeight);

            if (view != nullptr)
                view->setBounds (area);

            return;
        }

        auto area = getLocalBounds().reduced (margin);
        area.removeFromTop (labelHeight);

        if (view != nullptr)
            view->setBounds (area.reduced (screenGap));
    }

    void paint (juce::Graphics& g) override
    {
        if (feedMode)
        {
            paintFeedMode (g);
            return;
        }

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

        // The feed-mode toggle rides along in normal mode too — it's the
        // only way back in once the chassis is gone, so it has to be
        // reachable before it's ever needed.
        drawFeedGlyph (g);
    }

private:
    void paintFeedMode (juce::Graphics& g)
    {
        g.fillAll (Theme::palette().screenBlack);

        if (dragStripHovered)
        {
            auto strip = getLocalBounds().removeFromTop (feedDragStripHeight).toFloat();
            g.setColour (Theme::palette().boneDim.withAlpha (0.35f));

            constexpr float dashWidth = 22.0f, gap = 3.0f;
            const auto left = strip.getCentreX() - dashWidth * 0.5f;
            const auto centreY = strip.getCentreY();

            for (int i = -1; i <= 1; ++i)
                g.drawLine (left, centreY + static_cast<float> (i) * gap,
                           left + dashWidth, centreY + static_cast<float> (i) * gap, 1.0f);
        }

        drawFeedGlyph (g);
    }

    /** A silkscreen-scale echo of the chassis corner brackets, small enough
        to sit out of the way in either mode; the dot inside brightens once
        feed mode is live.
    */
    void drawFeedGlyph (juce::Graphics& g) const
    {
        auto bounds = feedGlyphBounds();

        Theme::drawCornerBrackets (g, bounds, Theme::palette().boneDim.withAlpha (0.55f), 4, 1.0f);

        g.setColour (Theme::palette().boneDim.withAlpha (feedMode ? 0.75f : 0.4f));
        g.fillEllipse (bounds.toFloat().reduced (4.0f));
    }

    juce::Rectangle<int> feedGlyphBounds() const
    {
        return { getWidth() - feedGlyphInset - feedGlyphSize, feedGlyphInset, feedGlyphSize, feedGlyphSize };
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! feedMode)
            return;

        const bool hovered = e.position.y < static_cast<float> (feedDragStripHeight);

        if (hovered != dragStripHovered)
        {
            dragStripHovered = hovered;
            repaint (getLocalBounds().removeFromTop (feedDragStripHeight));
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (dragStripHovered)
        {
            dragStripHovered = false;
            repaint (getLocalBounds().removeFromTop (feedDragStripHeight));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        draggingViaStrip = feedMode
                            && e.position.y < static_cast<float> (feedDragStripHeight)
                            && ! feedGlyphBounds().contains (e.getPosition());

        if (! draggingViaStrip)
            return;

        if (auto* window = getTopLevelComponent())
            dragger.startDraggingComponent (window, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! draggingViaStrip)
            return;

        if (auto* window = getTopLevelComponent())
            dragger.dragComponent (window, e, nullptr);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        draggingViaStrip = false;

        if (feedGlyphBounds().contains (e.getPosition()) && onToggleFeedMode != nullptr)
            onToggleFeedMode();
    }

    juce::String caption, annotation;
    juce::Component* view;   // non-owning — the editor holds the instrument

    bool feedMode = false;
    bool dragStripHovered = false;
    bool draggingViaStrip = false;
    juce::ComponentDragger dragger;

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

    panel->onToggleFeedMode = [this] { setFeedMode (! panel->isFeedMode()); };
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

void InstrumentWindow::setFeedMode (bool shouldBeFeedMode)
{
    if (panel->isFeedMode() == shouldBeFeedMode)
        return;

    panel->setFeedMode (shouldBeFeedMode);

    if (shouldBeFeedMode)
    {
        setTitleBarHeight (0);
        setUsingNativeTitleBar (false);
    }
    else
    {
        setUsingNativeTitleBar (true);
        setTitleBarHeight (defaultTitleBarHeight);
    }

    resized();
}

bool InstrumentWindow::isFeedMode() const
{
    return panel->isFeedMode();
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
