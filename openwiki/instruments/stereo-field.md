---
type: instrument
title: Stereo Field View (FIELD)
description: Period goniometer with needle VU meters, correlation meter, and phosphor persistence
tags: [instruments, stereo, goniometer, visualization]
---

# Stereo Field View (FIELD)

The stereo field display is a period goniometer (rotated 45° mid/side plot) with two needle VU meters and a correlation readout. Points land on a persistence image that fades each frame, so the trace trails like lit phosphor. Mono material reads as a vertical blade, wide material blooms into a cloud, and out-of-phase material leans horizontally.

## Display Characteristics

**Header:** `Source/gui/StereoFieldView.h`  
**Implementation:** `Source/gui/StereoFieldView.cpp`

- **Scope square:** 45° rotated L/R axes (mid/side)
  - Vertical axis: mono/mid (M = (L+R)/2)
  - Horizontal axis: width/side (S = (L-R)/2)
  - Dots: every sample of every processed hop
- **VU meters:** Left and right channel RMS (above/below scope, when space allows)
  - Range: -40 to 0 dBFS
  - Hot threshold: -6 dBFS (red zone)
  - Ballistics: coasting needle (~22% per frame)
- **Correlation meter:** Pearson r, 1.0 = fully correlated, 0.0 = uncorrelated, -1.0 = inverted
  - Below scope
  - Settles over ~200 ms
  - Smoothed on engine side, displayed directly
- **Persistence:** Image fades by ~10% per frame, older traces dim
- **Grid:** Radial lines at 45° intervals, reference circles

## Reading the Trace

- **Vertical blade** — Mono material or L ≈ R (in-phase)
- **Circular cloud** — Uncorrelated noise or white noise (no direction)
- **Ellipse leaning left/right** — Correlated stereo with some phase offset
- **Ellipse leaning diagonal** — Wide, highly panned material
- **Horizontal line** — Inverted phase (L ≈ -R), very rare and usually unwanted
- **Tight point at origin** — Silence

## Data Flow

```
AnalysisEngine::processPendingAudio()
    ↓
For each sample in hopBuffer:
    StereoSample { left, right }
        ↓
    LockFreeQueue::push (stereoSamples)
    ↓
StereoFieldView::timerCallback() (60 Hz, only if active)
    ├→ stereoSamples.pop() up to 4096
    ├→ Plot each sample as M/S dot
    ├→ Fade persistence image by ~10%
    ├→ Composite dots onto faded image
    ├→ Read correlation and RMS from atomics
    ├→ Animate needles toward new RMS levels
    └→ repaint()
```

Stereo samples are the per-sample L/R audio within each hop, not hop summaries. This gives a precise trace rather than a decimated one.

## Mid/Side Rotation

Transform from L/R to 45° rotated coordinates:
```cpp
float mid = (left + right) * 0.5f;
float side = (left - right) * 0.5f;

// Rotate 45°
float x = (mid - side) / std::sqrt(2.0f);  // Horizontal (width)
float y = (mid + side) / std::sqrt(2.0f);  // Vertical (mono)
```

The math places mono (L = R) on the vertical axis, width (L = -R) on the horizontal axis, and phase relationships on the diagonals.

## Persistence Image

```cpp
juce::Image persistence;  // 8-bit indexed ARGB, same size as component
std::vector<StereoSample> scratch;  // Up to 4096 samples per frame

void timerCallback() {
    // Fade: multiply all pixels by ~0.90 per frame
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixel *= persistenceFade;  // 0.90
    
    // Plot new dots, blending onto faded image
    for (const auto& sample : newSamples) {
        int px = screenXfromMidSide(sample);
        int py = screenYfromMidSide(sample);
        if (inBounds(px, py))
            compositePhosphorDot(persistence, px, py, color);
    }
}
```

This creates the phosphor trail effect: bright points persist for ~half a second as they fade, leaving a trace of recent stereo content.

## Needle Meters

```cpp
float leftRms = engine.getLoudnessMeter().getLeftRms();
float rightRms = engine.getLoudnessMeter().getRightRms();

// Ballistics: coasting needle
leftNeedle += (leftRms - leftNeedle) * 0.22f;  // ~22% per frame
rightNeedle += (rightRms - rightNeedle) * 0.22f;
```

Needles move sluggishly toward new RMS values, giving the VU meter feel. RMS values come from atomics on the analysis engine (updated every ~5.3 ms hop).

## Correlation Readout

```cpp
float r = engine.getAnalysisEngine().getSmoothedCorrelation();
// r = 1.0:  fully in-phase
// r = 0.0:  uncorrelated
// r = -1.0: inverted (antiphase)
```

Updated on the engine side every hop, smoothed over ~200 ms. Display reads atomically with no synchronization cost.

## Stored Trace

```cpp
void toggleStore()
{
    if (storedTrace.isValid()) {
        storedTrace.reset();
    } else {
        storedTrace = persistence.createCopy();  // Snapshot
    }
}
```

Freezes the current persistence cloud as a ghost overlay, allowing A/B comparison between two moments. Toggling again releases it. Survives clear (it is a stored reference, not history).

## Consumer Interface

```cpp
StereoFieldView(AnalysisEngine& engine);
void setActive(bool shouldBeActive);
```

`setActive(true)` starts the 60 Hz timer and discards any backlog of samples. `setActive(false)` stops the timer. Only one of StereoFieldView or OscilloscopeView can be active at a time (they share `stereoSamples` and `scopeSamples` queues).

## Layout

The view adapts based on available width:
- **Wide (>320 px):** Scope square on left, VU column on right, correlation strip below
- **Narrow:** Scope square fills width, VU column hidden, correlation strip below

This keeps the goniometer readable on smaller plugin windows.

## Performance

- **Memory:** ~1 MB (persistence image at typical screen size) + scratch buffers
- **CPU (60 Hz):** ~10-15 ms (fade, dot compositing, needle animation, rendering)
