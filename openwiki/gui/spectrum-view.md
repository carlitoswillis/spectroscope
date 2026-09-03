---
type: component
title: Spectrum View
description: Instantaneous spectrum with live curve, peak-hold ghost, SIDE width trace, note grid, reference file support
---

# Spectrum View

The SpectrumView displays the current frequency response as a set of overlaid traces on a log-frequency axis: a live averaged curve, a slowly-falling peak-hold ghost, and a secondary SIDE trace showing width-per-frequency band.

## Traces

Source: `Source/gui/SpectrumView.h/cpp`

```cpp
class SpectrumView : public Component, FileDragAndDropTarget, Timer, AsyncUpdater {
  explicit SpectrumView(AnalysisEngine&);
  void paint(Graphics&) override;
  void setActive(bool);
  void clear();
  void toggleStore();  // freeze current for A/B
  bool hasStoredTrace() const;
};
```

**Live curve:** Averaged STFT magnitude, smoothed just enough to read without strobing.

**Peak-hold ghost:** Decays slowly, hangs resonances and transient peaks long enough to measure. This is the precision trace; the live curve tells you the trend, the ghost tells you the worst case.

**SIDE trace:** The width-per-band reading. Computed from (L-R)/2 FFT. Where SIDE hugs the mid curve, the material is wide; where it falls away, it's mono.

## Stored Trace

One latching comparison memory. Press STORE (or `S` key) to freeze the current live + SIDE curves as a dim stored trace. Compare live-versus-frozen on one screen. Press again to clear.

## Reference File Support

Drag a WAV or AIFF file onto the spectrum view. The file is analysed offline (via `ReferenceAnalysisJob`, an async worker) into a pinned reference curve. The reference survives CLR on purpose — it's a reference, not transient history.

**Import flow:**
1. Drop file
2. `fileDragEnter()` shows a visual cue
3. `filesDropped()` launches async analysis
4. `ReferenceAnalysisJob::runJob()` reads the file, computes long-term averaged spectrum
5. `handleAsyncUpdate()` stores the result and repaints

## Overlays

**NOTE toggle:** Adds octave-spaced vertical lines at every C note, so spectral features map to musical pitches.

**TILT toggle:** Draws −3 and −4.5 dB/octave reference slopes. Balanced mixes tend to follow roughly pink-noise tilt; these lines are the "is my mix tilted right" ruler.

**Cursor:** A vertical line at the pointer X position, with frequency read-out and level read-out at the cursor Y.

## Measurement Cursor

On mouse move:

1. Compute frequency from the pointer X position
2. Interpolate the live curve at that frequency (max-over-span if a pixel covers many bins)
3. Display frequency as note name + cents, and level in dB

## Queue Management

**Consumers:** 
- `engine.getAnalyserColumns()` (ColumnRing, single-consumer) for live + peak-hold
- `engine.getSideSpectrumColumns()` (ColumnRing, single-consumer) for SIDE trace

Both are independent SPSC rings so neither blocks the other.

**Active gating:** `setActive(bool)` stops the timer when not visible.

**Discard on activate:** Pending columns are thrown away when the view re-enables.

## State Serialization

The reference file path, stored trace, overlays (NOTE/TILT), and cursor position are not persisted. Visibility is saved.
