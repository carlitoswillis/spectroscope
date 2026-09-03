---
type: instrument
title: Spectrogram View (DENSITY)
description: Scrolling log-frequency waterfall with GPU rendering in standalone, CPU fallback for DAW hosts
tags: [instruments, spectrogram, spectrum, visualization]
---

# Spectrogram View (DENSITY)

The spectrogram displays a scrolling waterfall of real-time frequency content: time flows left, frequency runs bottom-to-top on a logarithmic axis, and brightness indicates magnitude. The display is the same ring-texture algorithm whether rendered on GPU or CPU, with GPU acceleration optional in standalone mode only.

## Display Characteristics

**Header:** `Source/gui/SpectrogramView.h`  
**Implementation:** `Source/gui/SpectrogramView.cpp`

- **Time axis:** Newest column at right edge, scrolls left
- **Frequency axis:** Log scale, 30 Hz floor to Nyquist (24 kHz @ 48 kHz)
- **History:** 4096 columns (~21 seconds @ 48 kHz)
- **Colour depth:** 8-bit per bin (256-step palette)
- **Cadence:** One column per hop (~5.3 ms)
- **Grid:** Frequency rules at 60/125/250/500/1K/2K/4K/8K/16K Hz

## Reading the Trace

- **Horizontal lines** — Sustained pitches (basslines, held notes)
- **Vertical sweeps** — Transients, chirps, sliding tones
- **Dark background** — Broadband noise or energy below the floor
- **Bright clusters** — Formants, resonances
- **Mirrored patterns** — Harmonics of fundamental frequencies

## Data Flow

```
AnalysisEngine::processPendingAudio()
    ↓
StftAnalyzer::processHop (1025 bins, dBFS)
    ↓
ColumnRing::push (spectrumColumns)
    ↓
SpectrogramView::timerCallback() (60 Hz)
    ├→ ColumnRing.pop() up to 128 columns
    ├→ Store in CPU ring buffer
    ├→ (GPU path: stage columns for GL)
    └→ repaint() triggers paint() → (CPU) or renderOpenGL() → (GPU)
```

## CPU Path (Fallback)

Maintains a 4096-column ring image:
```cpp
static constexpr int historyColumns = 4096;
std::vector<float> history;        // numBins * historyColumns
juce::Image image;                 // 8-bit indexed ARGB, historyColumns × screenHeight
int imageWrite = 0;                // Ring write position
```

New columns are stored as raw dB values, then rendered into the image on every frame:
1. Lookup row mapping (which bins map to which screen rows — log remap)
2. For each new column, render dB → palette index → ARGB pixel
3. Blit image to screen with offset (showing newest at right edge)

On palette change (theme switch), the entire history is re-rendered using the new colour table.

## GPU Path (Standalone Only)

A GL_R8 ring texture (8-bit, single channel) holds normalized dB values. Fragment shader:
1. Maps log-frequency axis
2. Normalizes stored dB to 0..1
3. Looks up palette (a second 256×1 texture)
4. Applies scanlines and vignette

Changes to display range (dB floor/ceiling) are uniform updates, not re-uploads. Palette changes still require a full re-upload of the 256×4 RGBA palette texture.

GPU is enabled only in standalone via `setGpuEnabled(true)`. DAW hosts use CPU fallback (GL-in-plugin has DPI and z-order issues on some hosts).

## Row Mapping

On resize or sample-rate change, compute the log-frequency mapping:

```cpp
std::vector<int> rowBinLow;       // Lowest bin whose center lands in this row
std::vector<int> rowBinHigh;      // Highest bin
std::vector<float> rowBinCentre;  // Fractional bin at row centre frequency
```

For each screen row, find the range of FFT bins that contribute to that row. This allows proper antialiasing: if a pixel spans several bins, average them; if several pixels span one bin, interpolate.

## dB Range Control

```cpp
void setDecibelRange(float floorDb, float ceilingDb)
{
    dbFloor = floorDb;    // Default -75 dB
    dbCeiling = ceilingDb;  // Default 0 dB
}
```

Floor defaults to -75 dB (just above the noise floor of well-recorded material, so the background stays dark instead of glowing). Ceiling is 0 dBFS. Changing the range invalidates the image; it is re-rendered on the next frame.

## Cursor Readout

Hover to see:
- **Frequency:** Note name (e.g., "A4 +5C") or Hz
- **Level:** Magnitude in dBFS

Readout is positioned near the cursor, clamped inside the view.

## Clear Behavior

```cpp
void clear()
{
    std::fill(history.begin(), history.end(), StftAnalyzer::floorDb);
    imageWrite = 0;
    (GPU path: reset texture to zero)
    repaint();
}
```

All history is discarded, image and texture zeroed. The pane darkens until new audio arrives.

## Consumer Interface

```cpp
SpectrogramView(AnalysisEngine& engine);
void setActive(bool shouldBeActive);
```

Constructor calls `engine.addConsumer()`. `setActive(false)` stops the 60 Hz timer and drains the column ring, saving the STFT cost. Only one of Spectrogram or Spectrum analyser can be active at a time (they share the same column ring).

## Theme Changes

```cpp
void themeChanged()
{
    // Rebuild colour table from the new palette
    colourTable = buildTable(palette.spectrogramTable);
    (GPU path: upload new palette to texture)
    // Re-render entire history with new colours
    reRenderAllHistory();
    repaint();
}
```

## Performance

- **Memory (CPU):** ~640 KB (history) + 40 KB (image buffer)
- **Memory (GPU):** ~2 MB texture + 1 KB palette
- **CPU (60 Hz):** ~5-10 ms (row mapping, rendering new columns, blitting)
- **GPU (60 Hz):** ~1-2 ms (shader dispatch, palette upload on theme change)
