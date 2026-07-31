#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    A single instrument lifted out of the console into its own floating chunk
    of chassis. The editor owns the view for the window's whole lifetime; this
    window only borrows it — the destructor hands it back unharmed.

    Native macOS title bar, so the green zoom button gives per-window
    fullscreen for free; always-on-top, so metering floats over the DAW.
*/
class InstrumentWindow final : public juce::DocumentWindow
{
public:
    InstrumentWindow (juce::StringRef caption, juce::StringRef annotation, juce::Component& view);
    ~InstrumentWindow() override;

    /** Invoked by the title-bar close button — closing a floating instrument
        means docking it, never destroying the view.
    */
    std::function<void()> onDock;

    /** Fed getBounds() from moved() and resized(), so the editor can persist
        the window layout.
    */
    std::function<void (juce::Rectangle<int>)> onBoundsChanged;

    /** Re-tints the chassis after a palette switch and repaints. */
    void liveryChanged();

    void closeButtonPressed() override;
    void moved() override;
    void resized() override;

private:
    class Panel;
    std::unique_ptr<Panel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentWindow)
};
