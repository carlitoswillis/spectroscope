---
type: instrument
title: Loudness History View (CHART)
description: Strip-chart recorder of momentary LUFS with compliance targets, markers, and CSV export
tags: [instruments, loudness, metering, compliance]
---

# Loudness History View (CHART)

The loudness history view is a broadcast compliance meter dressed as a strip-chart recorder: a scrolling paper showing 60 seconds of momentary LUFS values, a readout block displaying integrated and short-term figures, selectable loudness targets (EBU, Spotify, YouTube, ATSC) with a verdict lamp, and numbered pen-tick markers for session logging.

## Display Characteristics

**Header:** `Source/gui/LoudnessHistoryView.h`  
**Implementation:** `Source/gui/LoudnessHistoryView.cpp`

- **Chart:** 60-second scrolling history
  - Newest at right edge
  - Vertical scale: -72 to 0 dBFS
  - Bright band from floor to each hop's momentary LUFS
  - Pen goes red (-9 dB and above) for "hot" signals
- **Readout block:** Momentary, short-term, integrated LUFS + range
- **Target lamp:** Green (compliant) / Red (over target) / Off (no target)
- **Markers:** Numbered pen ticks (1-32) stamped at current position, scrolling with paper
- **Grid:** Time rules (every 10 seconds) and dB rules

## Reading the Trace

- **Solid bright stripe** — Continuous loud material (music, speech)
- **Thin line** — Quiet passages or silence
- **Jumps and drops** — Dynamic range, edits, track changes
- **Red band at top** — Over -9 dBFS (hot; usually okay for broadcast, but monitor true peak)
- **Pen stays below target line** — Material is compliant
- **Markers trail across** — Timestamps of manually-logged events (cues, cuts, etc.)

## Data Flow

```
AnalysisEngine::processPendingAudio()
    ↓
LoudnessPoint { momentaryDb }
    ↓
LockFreeQueue::push (loudnessQueue)
    ↓
LoudnessHistoryView::timerCallback() (60 Hz, only if active)
    ├→ loudnessQueue.pop() up to 256 points
    ├→ Store in ring buffer
    ├→ Read current LUFS from LoudnessMeter atomics
    ├→ Animate current target compliance lamp
    └→ repaint()
```

One `LoudnessPoint` per hop (~5.3 ms @ 48 kHz), pushed to queue on the analysis thread. UI drains the queue on a 60 Hz timer, storing values in a 60-second ring.

## Ring Buffer

```cpp
static constexpr int historySeconds = 60;
std::vector<float> history;  // Stores momentary dB per hop
int ringSize;  // Hops per 60 seconds = sampleRate * 60 / hopSize
int writeIndex;  // Next write position (0 to ringSize-1)
int validCount;  // How many valid samples stored (≤ ringSize)
```

New values wrap around, oldest values are overwritten. `validCount` tracks whether the ring is full yet (during the first 60 seconds).

## Compliance Targets

```cpp
void cycleTarget()
{
    // OFF → EBU → SPOTIFY → YOUTUBE → ATSC → repeat
}
```

| Target | Level | Range | Platform |
|--------|-------|-------|----------|
| EBU R128 | -23 LUFS | -2 LU | EU broadcast |
| Spotify | -14 LUFS | — | Streaming |
| YouTube | -14 LUFS | — | Streaming |
| ATSC A/85 | -24 LKFS | -2 LU | US broadcast |
| Off | — | — | No target |

### Verdict Lamp

Reads momentary LUFS from the engine's `LoudnessMeter` atomic:
```cpp
float momentary = engine.getLoudnessMeter().getMomentaryLufs();
if (target == OFF) {
    lampState = OFF;
} else if (momentary <= targetLevel + 0.5f) {
    lampState = GREEN;
} else {
    lampState = RED;
}
```

Allows 0.5 dB headroom for transient overshoot. Red lighting means immediate corrective action (lower levels, reduce peaks, check limiter).

## Readout Block

Displays:
- **Momentary:** Current hop's LUFS (settles in ~400 ms)
- **Short-Term:** 3-second window average (settles in ~3 s)
- **Integrated:** Cumulative since clear (settles over minutes)
- **Range:** LU spread of gated short-term values (EBU Tech 3342)
- **True Peak:** ITU 4x oversampled peak (useful for headroom)

Values come from the `LoudnessMeter` atomics, read every frame. Stale reads are acceptable (the meter is slow-moving anyway).

## Markers

```cpp
void addMarker()
{
    markers.push_back({ totalHops, nextMarkerNumber++ });
    // Wraps to 1 after 32
}
```

Stamps a numbered pen tick at the current scroll position. Markers persist until clear and scroll with the paper, allowing operators to tag song boundaries, ad breaks, compliance points, etc.

Marker age is measured against `totalHops` (all hops ever consumed), not the ring's write position, so markers stay anchored even as old data wraps around.

## CSV Export

```cpp
void appendCsv(juce::String& out) const
{
    // Header: "seconds,momentary_db"
    // Rows: oldest first, one per hop
    // Seconds counted backward from newest sample
}
```

Generates CSV suitable for analysis in spreadsheets or tools. Called by SessionReport when logging a session. Times are negative ("old" → "new") so the CSV is chronological when read top-to-bottom.

## Consumer Interface

```cpp
LoudnessHistoryView(AnalysisEngine& engine);
void setActive(bool shouldBeActive);
```

`setActive(true)` starts the 60 Hz timer. `setActive(false)` stops it and discards any backlog.

## Clear Behavior

```cpp
void clear()
{
    engine.resetLoudness();  // Resets meter histories
    std::fill(history.begin(), history.end(), -1000.0f);  // Sentinel
    writeIndex = 0;
    validCount = 0;
    markers.clear();
    totalHops = 0;
    repaint();
}
```

Tears the chart off the recorder: history, markers, and the meter's integration all start fresh. Called when the user presses CLR or starts a new song.

## Performance

- **Memory:** ~1.2 MB (60 seconds of hops at 48 kHz) + marker list
- **CPU (60 Hz):** ~3-5 ms (queue drain, ring bookkeeping, rendering)
