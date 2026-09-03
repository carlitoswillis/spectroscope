---
type: component
title: Waveform View
description: Scrolling min/max/RMS envelope with second-level time grid
---

# Waveform View

The WaveformView displays the last ~10 seconds of audio as a scrolling min/max envelope with an RMS core. One envelope point is drawn per pixel column, so horizontal scale is the analysis hop rate (5.3 ms at 48 kHz). Time ticks mark seconds.

## Data Flow

Source: `Source/gui/WaveformView.h/cpp`

```cpp
class WaveformView : public Component, Timer {
  explicit WaveformView(AnalysisEngine&);
  void paint(Graphics&) override;
  void clear();
};
```

**Constructor:**
- Calls `engine.addConsumer()` to enable analysis
- Discards any pending envelope points (stale from before view creation)
- Starts its own timer at 60 Hz for smooth scrolling

**Destructor:**
- Calls `engine.removeConsumer()` to disable analysis if no other views are active

## Ring Buffer

Source: `Source/gui/WaveformView.h:39-46`

```cpp
static constexpr int ringCapacity = 8192;      // ~43 seconds at 48 kHz
std::vector<EnvelopePoint> ring;
int head = 0;          // index one past the newest point
int numStored = 0;
```

The ring stores up to 8192 envelope points. At 48 kHz, 256-sample hops, that's 43 seconds of history. The head index wraps around the capacity.

## Timer Callback (60 Hz)

Once per frame:

1. **Drain queue:** Pop all pending envelope points
2. **Append to ring:** Advance head, store each point
3. **Repaint:** Trigger the paint callback

The 60 Hz timer is independent of the analysis engine's 12 Hz UI update timer. This smooths the visual scroll even when analysis publishes data at a different rate.

## Painting

The paint callback receives a Graphics context. For each pixel column:

1. **Fetch the point** at that age (seconds ago)
2. **Plot min/max as a filled band** in the RMS colour
3. **Plot RMS core** brighter, to show where the signal actually lives
4. **Draw time grid** (vertical lines marking 1-second ticks)
5. **Cursor readout:** If the mouse is over the view, show time-ago and level in dBFS in a hover box

## Mouse Interaction

- **mouseMove():** Store the current pointer position and repaint (draws hover readout)
- **mouseExit():** Clear the pointer position

The readout box is clamped to the view bounds so it never overflows the screen.

## Clear Action

`clear()` wipes the ring and repaint. Used when the user presses CLR or sends the clear automation parameter.

## Visible Time Span

```cpp
double getVisibleTimeSpan() const noexcept;
```

Returns the time span currently visible on screen (in seconds). Used by the editor to label the time axis appropriately — at different window heights, different durations fit.
