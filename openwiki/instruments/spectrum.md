---
type: instrument
title: Spectrum View (SPECTRUM)
description: Instantaneous spectrum with peak-hold ghost, SIDE width trace, stored snapshot, reference curve import
tags: [instruments, spectrum, analysis, visualization]
---

# Spectrum View (SPECTRUM)

The spectrum analyser displays the instantaneous frequency response as a traced curve: the live curve follows the input in real-time, a peak-hold ghost shows resonances that have passed, a secondary SIDE trace reveals stereo width by frequency, and optional stored and reference curves allow A/B comparison and calibration.

## Display Characteristics

**Header:** `Source/gui/SpectrumView.h`  
**Implementation:** `Source/gui/SpectrumView.cpp`

- **Axes:** Log frequency (30 Hz to Nyquist), linear dB (typically -80 to 0)
- **Live curve:** Bright amber trace, chases the input
- **Peak ghost:** Darker overlay, decays slowly (~2 sec half-life)
- **SIDE trace:** Secondary colour (mustard), shows (L-R)/2 magnitude (stereo width)
- **Stored curve:** Pale overlay, frozen snapshot for comparison
- **Reference curve:** Thin line, averaged from a dropped audio file
- **Grid:** Frequency rules, optional note grid and tilt guides (for mixing reference)

## Reading the Trace

- **Peak at 100 Hz** — Bass emphasis or muddy low-end
- **Dip at 2 kHz** — Presence reduction (less clarity)
- **SIDE trace hugging live curve** — Material is wide at that frequency
- **SIDE trace near floor** — Mono or highly correlated (in-phase L/R)
- **Stored curve differing from live** — Tone has changed
- **Reference curve showing bumps live doesn't** — Lack of presence or colour

## Data Flow

```
AnalysisEngine::processPendingAudio()
    ↓
StftAnalyzer (mono)::processHop
    ↓
ColumnRing::push (analyserColumns)
    ↓
StftAnalyzer (side)::processHop
    ↓
ColumnRing::push (sideSpectrumColumns)
    ↓
SpectrumView::timerCallback() (60 Hz, only if active)
    ├→ analyserColumns.pop() up to 128
    ├→ sideSpectrumColumns.pop() up to 128
    ├→ Average columns, update peak ghost
    ├→ Render to per-pixel curves
    └→ repaint()
```

Only one of Spectrum or Spectrogram can be active at a time (they share `analyserColumns`). Whichever is inactive stops its timer, so the other gets all the columns.

## Averaging & Peak-Hold

Live curve:
```cpp
// Exponential moving average per bin
averagedDb[bin] += (newDb[bin] - averagedDb[bin]) * 0.1f;
```

Peak ghost:
```cpp
// Per bin, track max over recent history, decay if no new peak
peakDb[bin] = jmax(peakDb[bin] * 0.98f, newDb[bin]);
```

SIDE curve uses the same averaging but from side-channel columns.

## Per-Pixel Rendering

Bins are ~23 Hz wide; screen width varies. Pixel-to-frequency mapping:
```cpp
float binLow = pixelFreqToBin(pixelX * freqPerPixel);
float binHigh = pixelFreqToBin((pixelX + 1) * freqPerPixel);

// Max-over-span when multiple bins land in one pixel
// Interpolate when one bin spans multiple pixels
float pixelDb = levelAt(averagedDb, binLow, binHigh);
```

Builds a per-pixel curve for rendering without the jitter of bin-level data above 1 kHz (where pixel width < bin width).

## Stored Curve

```cpp
void toggleStore()
{
    if (hasStoredTrace) {
        storedAveraged.clear();
        storedSide.clear();
    } else {
        storedAveraged = averagedDb;
        storedSide = sideDb;
    }
}
```

Latches the current averaged curve as a reference overlay, or drops an existing one. Survives clear (CLR resets live but not stored). One stored curve at a time; toggling again releases it.

## Reference Curve

Audio files can be drag-and-dropped to compute a reference spectrum:

1. File read asynchronously via AsyncUpdater
2. Analysis job:
   - Reads full file into memory
   - Applies same STFT to entire audio
   - Averages all columns
   - Stores result
3. View re-renders with reference curve overlaid
4. Reference survives clear (it is a reference, not history)

Useful for matching tone to a commercial mix or calibration standard.

## Optional Overlays

### Note Grid
Vertical lines at semitone intervals (A440 reference). Helps identify pitch content and harmonic spacing.

### Tilt Guide
Diagonal reference line for mixing: modern broadcast targets a slight high-frequency tilt (more presence, less boom). The line is just a visual guide, not a measurement.

## Consumer Interface

```cpp
SpectrumView(AnalysisEngine& engine);
void setActive(bool shouldBeActive);
```

`setActive(true)` starts the 60 Hz timer and clears the queue backlog. `setActive(false)` stops the timer and drains pending columns.

## Mouse Interaction

- **Hover:** Cursor readout with frequency and level
- **Click on note grid toggle:** Show/hide semitone grid
- **Click on tilt toggle:** Show/hide mixing reference line
- **Drag file:** Import reference curve
- **Hold to export:** (Future feature, not yet implemented)

## Performance

- **Memory:** ~160 KB (per-pixel curve buffers, averages, peaks, stored/reference)
- **CPU (60 Hz):** ~5 ms (averaging, per-pixel rendering, overlays)
- **Reference analysis:** ~50-500 ms (file size dependent, async)
