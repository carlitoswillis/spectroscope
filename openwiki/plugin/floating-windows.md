---
type: plugin
title: Floating Windows
description: Per-pane InstrumentWindow with native macOS title bar, bounds persistence, OBS feed-out mode
tags: [plugin, ui, windows]
---

# Floating Windows

Any of the six instrument panes can pop out into their own floating, always-on-top window via a glyph in the pane's caption. The view moves (not copied) between console and window — there is only one instance of each view. Floating state and window bounds persist in the plugin state.

## InstrumentWindow Class

**Header:** `Source/gui/InstrumentWindow.h`  
**Implementation:** `Source/gui/InstrumentWindow.cpp`

```cpp
class InstrumentWindow final : public juce::DocumentWindow
```

DocumentWindow provides a native macOS title bar and close button. The window is always-on-top so metering floats above the DAW.

## Window Lifecycle

```cpp
void SpectroscopeAudioProcessorEditor::floatPane(int index)
{
    // Create window, move view into it
    floatingWindows[index] = std::make_unique<InstrumentWindow>(
        paneCaption(index),      // e.g., "Waveform"
        paneAnnotation(index),   // e.g., "UNIT A"
        views[index]);
    
    // Callbacks for bounds changes and close button
    floatingWindows[index]->onBoundsChanged = [this, index](juce::Rectangle<int> bounds) {
        storeWindowLayout();
    };
    
    floatingWindows[index]->onDock = [this, index]() {
        dockPane(index);
    };
    
    floatingWindows[index]->setVisible(true);
    processor.setFloatingMask(processor.getFloatingMask() | (1 << index));
}

void SpectroscopeAudioProcessorEditor::dockPane(int index)
{
    storeWindowLayout();
    floatingWindows[index].reset();
    processor.setFloatingMask(processor.getFloatingMask() & ~(1 << index));
}
```

The view is reparented, not copied. Window close button triggers `onDock` callback, which re-parents the view back to the console.

## Bounds Persistence

```cpp
void InstrumentWindow::moved() override { onBoundsChanged(getBounds()); }
void InstrumentWindow::resized() override { onBoundsChanged(getBounds()); }
```

Every move or resize triggers the callback. The editor collects live window bounds and serializes them:

```cpp
void SpectroscopeAudioProcessorEditor::storeWindowLayout()
{
    juce::String layout;
    for (int i = 0; i < numPanes; ++i) {
        if (floatingWindows[i] != nullptr) {
            auto b = floatingWindows[i]->getBounds();
            layout += juce::String::formatted("%d:%d,%d,%d,%d;",
                i, b.getX(), b.getY(), b.getWidth(), b.getHeight());
        }
    }
    processor.setWindowLayout(layout);
}
```

Format: `"index:x,y,w,h;index:x,y,w,h;..."` stored in processor state.

## Restoration on Session Load

```cpp
const auto savedFloating = processor.getFloatingMask();
const auto savedLayout = processor.getWindowLayout();

for (int i = 0; i < numPanes; ++i) {
    if ((savedFloating & (1 << i)) != 0) {
        processor.setWindowLayout(savedLayout);  // Restore before creating window
        floatPane(i);                            // Reads layout to position window
    }
}
```

Bounds are restored before the window is shown, so it appears in the saved position.

## OBS Feed-Out Mode

```cpp
void InstrumentWindow::setFeedMode(bool shouldBeFeedMode);
bool InstrumentWindow::isFeedMode() const;
```

For streaming/recording, feed mode removes:
- Native title bar
- Bezel and chassis chrome
- All decorations

The view fills the window, with only:
- A hover-only drag strip at the top
- A corner glyph to toggle feed mode back

Allows clean window capture without OBS/Streamlabs cropping.

## Rendering

InstrumentWindow owns a Panel inner component:

```cpp
class InstrumentWindow::Panel : public juce::Component
{
    Component& view;  // Reference to the borrowed view
    bool feedMode = false;
    
    void paint(juce::Graphics& g) override;
    void resized() override { view.setBounds(getLocalBounds()); }
};
```

In normal mode:
- Paints chassis frame and bezel (CRT aesthetic)
- Draws caption with title and unit designation
- Hosts the view at a fixed inset
- Draws pop-out glyph at top-right of caption

In feed mode:
- Transparent except for drag strip hover area
- View fills entire window
- Corner toggle glyph only

## Always-On-Top

```cpp
InstrumentWindow::InstrumentWindow(...)
{
    setAlwaysOnTop(true);  // Float above DAW windows
}
```

Metering should never be hidden behind other windows.

## Theme Changes

```cpp
void InstrumentWindow::liveryChanged()
{
    // Repaint chassis and bezel
    repaint();
}
```

Called from the editor when theme changes. Window re-renders with the new palette.

## Performance

- **Memory:** ~50 KB per window (title bar, native windowing overhead)
- **CPU:** Negligible (windowing system handles rendering)
- **Latency:** None (view rendering happens on the same timer, just in a different window)

## Constraints

- Only one instance of each view (sharing is not supported)
- Maximum of 6 floating windows (one per pane)
- Floating state is per-pane, not per-preset (theme changes affect all)
