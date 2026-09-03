---
type: component
title: Stereo Field View
description: Goniometer (Lissajous scope) with VU needles and correlation meter, phosphor persistence
---

# Stereo Field View

The StereoFieldView displays the stereo image as three linked instruments: a goniometer (Lissajous scope) that plots L/R material as dots with phosphor persistence, per-channel VU needles, and a correlation meter showing phase compatibility.

## Goniometer (Centre)

Source: `Source/gui/StereoFieldView.h/cpp`

```cpp
class StereoFieldView : public Component, Timer {
  explicit StereoFieldView(AnalysisEngine&);
  void paint(Graphics&) override;
  void setActive(bool);
  void clear();
  void toggleStore();  // freeze current cloud as ghost
  bool hasStoredTrace() const;
};
```

Every sample is plotted as a dot on a 2D plane rotated 45°:
- **Y-axis:** Mono sum (L+R)/2
- **X-axis:** Side (L-R)/2

This rotation gives three easily-read shapes:
- **Vertical blade:** Mono material (side = 0)
- **Blooming cloud:** Wide material (large side component)
- **Horizontal lean:** Out-of-phase material (will fold to mono badly in a club PA)

L material leans toward the top-left diagonal; R material toward the top-right diagonal.

### Phosphor Persistence

Dots land on a persistence image that fades a step each frame (~50 ms decay time), creating a phosphor-like trail. Old dots fade and disappear; the goniometer reads the current shape, not a frozen snapshot.

## VU Needles (Right Side)

Per-channel needle meters with proper coil-meter ballistics:
- Rise instantly to peak
- Fall slowly (~500 ms)
- "Rust zone" shaded above −6 dB (headroom warning)

Needles matching = balanced image; one persistently hotter = your mix leans left or right.

## Correlation Meter (Bottom Strip)

Pearson correlation coefficient of L/R:
- **+1:** Mono-compatible (needles should sit here)
- **0:** Wide, uncorrelated
- **−1:** Perfectly out-of-phase (will flip when you go mono)

The single clearest "this will fold badly to mono" warning any instrument can give is the needle living left of centre.

## Stored Cloud

One latching comparison. Press STORE to freeze the current persistence cloud as a ghost trace held underneath the live one — a darkroom reference print beside the one still developing. Press again to clear.

## Queue Management

**Consumer:** Pops from `engine.getStereoSamples()` (LockFreeQueue<StereoSample>, single-consumer). This is one sample per analysis sample in the hop (256 samples per hop at 48 kHz), fed every 5.3 ms.

**Active gating:** `setActive(bool)` stops the timer when not visible, discards pending samples.

## Sizing & Layout

The goniometer is a square (height = width). VU needles sit to the right. The correlation meter is a horizontal strip below. The view resizes responsively; all elements scale to fit.
