---
type: component
title: Instrument Window
description: Floating chassis for popped-out panes, always-on-top, native title bar, feed mode for OBS
---

# Instrument Window

InstrumentWindow is a DocumentWindow (native title bar + frame) that hosts one instrument view in a floating, always-on-top chassis. Clicking the close button docks the pane back into the console; the same Component object moves back unharmed.

## Architecture

Source: `Source/gui/InstrumentWindow.h/cpp`

```cpp
class InstrumentWindow : public DocumentWindow {
  InstrumentWindow(StringRef caption, StringRef annotation, Component& view);
  
  std::function<void()> onDock;                    // closes the window
  std::function<void(Rectangle<int>)> onBoundsChanged;  // persists bounds
  
  void liveryChanged();   // re-tint after theme switch
  void setFeedMode(bool); // OBS-friendly borderless mode
  bool isFeedMode() const;
};
```

## Component Ownership

The view Component is **borrowed**, not owned. The editor owns the actual view object and passes a reference to the window. When the window closes, the view is returned to the editor's stacked pane layout. This allows seamless floating/docking without copying or reallocating.

## Floating & Always-On-Top

The window is created with `DocumentWindow::allWindowsVisible | DocumentWindow::closeButton` and manually set to always-on-top via the underlying native window calls.

On macOS, the green zoom button in the title bar gives per-window fullscreen for free.

## Callbacks

**onDock:** Invoked when the user clicks the close button. The editor listens for this and docks the pane back.

**onBoundsChanged:** Called from `moved()` and `resized()`. The editor uses this to serialize the window bounds into the plugin state.

## Livery Integration

`liveryChanged()` is called by the editor when the theme changes. The window re-reads the palette and repaints the chassis (the custom Panel inner class that draws the bezel and chrome).

## Feed Mode (Broadcast/Streaming)

**Source:** `Source/gui/InstrumentWindow.cpp` (FeedMode implementation)

`setFeedMode(true)` strips the window to a chrome-less borderless surface:
- No native title bar
- No JUCE chassis bezel
- Just the view filling the window
- A hover-only drag strip at the top (appears on mouse hover)
- A corner glyph (appears on hover) to flip back

Built for OBS window-capture: float the spectrogram on one display, feed-mode it, and capture as a broadcast overlay. The glyph at the top-right lets you drag or switch back to normal mode without leaving OBS.

## Bounds Persistence

The editor calls `onBoundsChanged()` whenever the window moves or resizes. Bounds are serialized into the plugin state as a string: `"index:x,y,w,h;index:x,y,w,h;..."`. On editor recreation, the bounds are restored and windows are refloated at their saved positions.

## Screen-Relative Positioning

Bounds are stored in absolute screen coordinates, not relative to the editor or a primary display. This allows windows to survive across display disconnections better than relative positioning, though they'll be off-screen if a display is removed.
