---
type: component
title: Loudness History View
description: Strip-chart recorder of BS.1770 loudness over 60 seconds with target compliance lamp and pen markers
---

# Loudness History View

The LoudnessHistoryView displays a 60-second strip-chart recording of momentary loudness, with a broadcast compliance readout block (M/S/I/LRA/TP), a selectable loudness target with a verdict lamp, and numbered pen-tick markers that scroll with the paper.

## Display Elements

Source: `Source/gui/LoudnessHistoryView.h/cpp`

```cpp
class LoudnessHistoryView : public Component, Timer {
  explicit LoudnessHistoryView(AnalysisEngine&);
  void paint(Graphics&) override;
  void setActive(bool);
  void clear();
  void addMarker();
  void cycleTarget();
  juce::String getTargetName() const;
  void appendCsv(juce::String& out) const;
};
```

**Strip chart:** Newest data at the right edge, scrolls left. Y-axis spans −30 to −5 LUFS. Time-gridded every 10 seconds.

**Readout block (top right):**
- **M:** Momentary LUFS (400 ms window) — bouncy, right-now
- **S:** Short-term LUFS (3 s window) — phrase-level
- **I:** Integrated LUFS (whole session, gated) — what streaming platforms judge
- **LRA:** Loudness range (EBU Tech 3342) — dynamic spread in LU
- **TP:** Maximum true peak in dBTP — 4x-oversampled intersample peaks

**Target plate:** Clickable selector cycling through delivery targets:
- OFF (no target)
- EBU −23 LUFS (European standard)
- Spotify −14 LUFS
- YouTube −14 LUFS
- ATSC −24 LUFS

When a target is set, a reference line appears on the chart at that level, and a verdict lamp judges the session:
- **PASS:** Integrated within ±1 LU of target AND true peak at or under −1 dBTP
- **FAIL:** Outside the window
- **WAIT:** Not enough audio to gate yet (integrated shows -∞)

## Data Flow

**Per-hop queue:** AnalysisEngine pushes one `LoudnessPoint` per hop (~5.3 ms) containing the RMS in dB. The view pops these and extends the chart left-to-right.

**Loudness meter atomics:** M/S/I/LRA/TP are read directly from the engine's LoudnessMeter on each paint. No queue, just relaxed atomic loads.

## Markers

Press MARK or `M` key to stamp a numbered pen tick at the newest chart position. The marker scrolls with the paper and falls off the left edge like everything else.

Use case: Mark the chorus, mark the drop, and the chart becomes annotated evidence instead of a scrolling mystery.

Markers are stored in a vector with their hop index; on repaint, their age is computed and they're drawn if visible.

## CSV Export

`appendCsv()` writes the loudness history as CSV: a header `"seconds,momentary_db"` then one row per stored hop. Seconds are counted backward from the newest sample, so history reads as negative time (−60 s to 0 s).

This is used by SessionReport to save a session log.

## Queue Management

**Consumer:** Pops from `engine.getLoudnessQueue()` (LockFreeQueue<LoudnessPoint>, single-consumer). One point per hop.

**Atomics:** Direct reads from `loudnessMeter.getMomentaryLufs()`, `getShortTermLufs()`, `getIntegratedLufs()`, `getLoudnessRange()`, `getMaxTruePeakDb()`.

**Active gating:** `setActive(bool)` stops the timer when not visible.

## History Storage

History is stored in a ring buffer of LoudnessPoint entries, up to ~600 hops (60 seconds at 48 kHz). Newest point advances the write head; oldest falls off.

## Alignment Tone Validation

The TONE button (latching) feeds a −18 dBFS 1 kHz sine to the meters only. The audio output is untouched — the tone exists only for analysis.

When engaged, the chart should read:
- M: ~−15.0 LUFS (bouncy)
- I: −15.0 LUFS (after a few seconds)
- TP: −1 dBTP

This is the built-in proof the meter is honest. If the numbers don't match, the K-weighting or true-peak detector is broken.

## Clear Action

`clear()` wipes all history, markers, and resets the loudness meter. New song, dark glass. Used when pressing CLR or CLR automation parameter.