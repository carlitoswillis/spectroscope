---
type: component
title: Spectrogram View
description: Scrolling spectrogram with GPU rendering option, log-frequency axis, colour palette
---

# Spectrogram View

The SpectrogramView displays audio history as a scrolling spectrogram: time scrolls left, frequency (log scale) runs bottom-to-top, brightness is level. The display is a ring that new columns overwrite in place, avoiding full-history reallocation on every frame.

## Architecture

Source: `Source/gui/SpectrogramView.h/cpp`

```cpp
class SpectrogramView : public Component, Timer, OpenGLRenderer {
  explicit SpectrogramView(AnalysisEngine&);
  void paint(Graphics&) override;
  void setDecibelRange(float floorDb, float ceilingDb);
  void themeChanged();  // rebuild palette table
  void clear();
  void setActive(bool);
  void setGpuEnabled(bool);  // GPU only in standalone
};
```

## CPU Ring (Fallback Path)

The image is a circular buffer:

1. **Allocate:** 2D image large enough to hold ~2 seconds of columns
2. **On new column:** Write the column into the image at position `head % capacity`
3. **Draw:** Two blits with a shifting offset, so the newest column appears at the right edge

This avoids moving every pixel left on every frame. All scrolling is done via redraw offset.

**Raw data storage:** Alongside the image, the actual dB values are kept so the view can re-render history if the sample rate, palette, or dB range changes.

## GPU Ring (Standalone Only)

In the standalone app, the same ring lives in an OpenGL texture (GL_R8, one-channel unsigned byte). The fragment shader performs:

1. **Log-frequency remap:** vUV.y → fractional bin via exp() mapping
2. **dB normalisation:** texture value → [floor, ceiling] range
3. **Palette lookup:** normalized value → RGB via 1D colour table
4. **Scanlines & vignette:** Applied in-shader

This moves the expensive operations from CPU to GPU and lets the spectrogram scale smoothly without re-rendering.

## Frequency Axis

**Log scale:** Each octave gets equal visual space. Axes tick at 60 / 125 / 250 / 500 / 1K / 2K / 4K / 8K / 16K Hz.

**Mapping:** Bin index → fractional bin → frequency via inverse of `getBinFrequency()`:

```
bin = (frequency / (nyquist / numBins)) * numBins
frequency = bin * sampleRate / fftSize
```

The log remap in the GPU shader uses `uMinBin * exp(vUV.y * uLogSpan)` to map from the screen's normalized Y coordinate back to the log-frequency axis.

## Colour Palette

Source: `Source/gui/ColourMaps.h` and `Source/gui/Theme.h:56`

Each livery (AMBER/NOSTROMO/TVA/GRTA) has its own 256-entry colour table. Brightness is gamma-corrected (1.3) so midtones don't wash everything in colour.

`themeChanged()` rebuilds the palette and re-renders all history so switching liveries updates the display instantly.

## Cursor Readout

Hover over the spectrogram to see frequency as a note name with cents (e.g., "F#1 +7C") and time-ago. The note readout is computed from the exact frequency at the pointer position.

## Queue Management

**Consumer:** Pops from `engine.getSpectrumColumns()` (ColumnRing, single-consumer).

**Active gating:** When inactive (not visible), `setActive(false)` stops the timer so the queue isn't drained and old columns aren't rendered when the view reappears.

**Discard on activate:** When re-enabled, pending columns are thrown away.

## State Serialization

The dB range, GPU enable flag, and cursor position are not persisted — they're session state. Visibility is saved in `panesMask`.
