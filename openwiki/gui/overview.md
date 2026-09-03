---
type: system
title: GUI Architecture
description: Six instrument views, editor container, floating windows, theme system, and chassis rendering
---

# GUI Architecture

The GUI is organized as a console of six interchangeable instrument panes rendered behind CRT glass. The PluginEditor owns the layout, switch rail, and header chrome. Each pane is a juce::Component that drains a lock-free queue from the analysis engine and paints the latest data every 12 Hz.

## Component Hierarchy

**PluginEditor** (root)
├─ Header (wordmark, PWR/SIG/ERR lamps, unit switch, sample rate)
├─ Switch Rail (WAVE/DENSITY/SPECTRUM/FIELD/CHART/SCOPE + STORE/MARK/PHOTO/LOG/TONE)
├─ Stacked Panes (vertical layout, visible panes only)
│  ├─ WaveformView (scrolling min/max/RMS envelope)
│  ├─ SpectrogramView (scrolling spectrogram, optional GPU)
│  ├─ SpectrumView (instantaneous spectrum + peak hold + side width)
│  ├─ StereoFieldView (goniometer + VU needles + correlation)
│  ├─ LoudnessHistoryView (strip-chart loudness + loudness meter readout)
│  └─ OscilloscopeView (triggered 1024-sample window)
├─ CrtOverlay (scanlines + vignette)
├─ InstrumentWindow[] (floating panes, always-on-top)
└─ BootCard (test card, standalone only)

## Pane Visibility & Floating

Source: `Source/PluginEditor.h/cpp`

The editor tracks two masks:

- **panesMask** (6 bits): which panes are visible in the console
  - Bit 0: WAVE (WaveformView)
  - Bit 1: DENSITY (SpectrogramView)
  - Bit 2: SPECTRUM (SpectrumView)
  - Bit 3: FIELD (StereoFieldView)
  - Bit 4: CHART (LoudnessHistoryView)
  - Bit 5: SCOPE (OscilloscopeView)
  - Empty mask coerced to bit 0 (console never goes dark)

- **floatingMask** (6 bits): which panes float in their own windows
  - Orthogonal to visibility: a floating pane can be switched off, docking it first

When a pane is floating, its Component moves from the stacked console layout into an InstrumentWindow. The same Component object is used in both places; it's never copied or cloned.

## Timer Tick

Source: `Source/PluginEditor.cpp:149`

```cpp
startTimerHz(12);  // 83 ms per tick
```

The editor's timer (12 Hz) is the heartbeat. Each tick:

1. Poll parameter rising edges (CLR, STORE, MARKER)
2. Repaint active panes (which drain their queues in paint())
3. Update signal lamp (from engine's decaying peak)
4. Update loudness readout (read atomics)
5. Check for dropped frames (ERR lamp)

## Pane Integration Pattern

Each instrument view:
- Adds/removes itself as a consumer in constructor/destructor
- Drains its queue at 12 Hz on the message thread
- Reads loudness meter atomics directly (no queue)
- Calls `repaint()` after updating internal state
- Painting reads the latest state and draws it

Example (WaveformView):

```cpp
WaveformView::WaveformView(AnalysisEngine& e) : engine(e) {
  engine.addConsumer();
  engine.getEnvelopeQueue().discardPending();
  startTimerHz(60);  // additional 60 Hz timer for smooth scrolling
}

void WaveformView::timerCallback() {
  // Drain queue
  EnvelopePoint points[maxPointsPerFrame];
  int count = engine.getEnvelopeQueue().pop(points, maxPointsPerFrame);
  
  // Update ring buffer
  for (int i = 0; i < count; ++i) {
    ring[head++ % ringCapacity] = points[i];
    numStored++;
  }
  
  repaint();
}

void WaveformView::paint(Graphics& g) {
  // Draw the current ring contents
}
```

## Display Settings Persistence

Source: `Source/PluginProcessor.h:73-95`

Display state (not audio parameters) is serialized and survives session save/load:

- **themeIndex** (0-3): which livery (AMBER/NOSTROMO/TVA/GRTA)
- **panesMask** (6 bits): console visibility
- **floatingMask** (6 bits): floating state
- **windowLayout** (string): bounds of each floating window ("0:100,200,400,300;2:50,50,800,600")

These are stored in plugin state XML and restored on editor creation.

## Parameter Edge Counting

Source: `Source/PluginProcessor.cpp:61-73`

Three momentary parameters are exposed to the host for automation:

```cpp
addParameter(clearParam = new AudioParameterBool({\"clear\", 1}, \"Clear Displays\", false));
addParameter(storeParam = new AudioParameterBool({\"store\", 1}, \"Store Trace\", false));
addParameter(markerParam = new AudioParameterBool({\"marker\", 1}, \"Add Marker\", false));
```

The audio thread counts rising edges (false→true transitions) per block and increments a counter. The editor polls and diffs the counter on its timer, so even if a parameter rises and falls between two ticks, the edge is caught and the action fires.

## Queue Consumers by View

| View | Queue | Type | Purpose |
|------|-------|------|----------|
| WaveformView | envelopeQueue | LockFreeQueue<EnvelopePoint> | min/max/RMS scrolling history |
| SpectrogramView | spectrumColumns | ColumnRing | FFT columns (log-frequency scrolling) |
| SpectrumView | analyserColumns | ColumnRing | FFT columns (live + peak-hold curves) |
| SpectrumView (SIDE) | sideSpectrumColumns | ColumnRing | (L-R)/2 FFT for width-per-band |
| StereoFieldView | stereoSamples | LockFreeQueue<StereoSample> | every sample (256/hop) for goniometer |
| OscilloscopeView | scopeSamples | LockFreeQueue<StereoSample> | every sample for mono trigger sweep |
| LoudnessHistoryView | loudnessQueue | LockFreeQueue<LoudnessPoint> | RMS per hop for strip chart |
| LoudnessHistoryView | — | atomics | momentary/integrated LUFS, true peak |

Multiple consumers use separate SPSC rings (spectrumColumns and analyserColumns) so neither blocks the other.

## Active State Gating

Views with queue consumers call `setActive(bool)` when their visibility changes. An inactive view stops its timer, so it doesn't drain queued data that would otherwise be stale when reactivated.

```cpp
// When panesMask changes in applyPanes()
spectrogramView.setActive((mask & (1 << 1)) != 0);
spectrumView.setActive((mask & (1 << 2)) != 0);
stereoFieldView.setActive((mask & (1 << 3)) != 0);
loudnessView.setActive((mask & (1 << 4)) != 0);
```

On reactivation, queues are discarded so the view renders current data, not a backlog.