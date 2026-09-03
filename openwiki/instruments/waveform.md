---
type: instrument
title: Waveform View (WAVE)
description: Scrolling envelope display with min/max/RMS per analysis hop, time-domain visualization
tags: [instruments, waveform, visualization]
---

# Waveform View (WAVE)

The waveform display shows the last few seconds of audio as a scrolling envelope: the filled band spans minimum-to-maximum amplitude each hop, with the inner RMS core showing where the energy actually lives. Peaks tell you about transients; the RMS core tells you about loudness.

## Display Characteristics

**Header:** `Source/gui/WaveformView.h`  
**Implementation:** `Source/gui/WaveformView.cpp`

- **Newest at:** Right edge
- **Age:** ~10 seconds of history
- **Cadence:** One column per 256-sample hop (~5.3 ms @ 48 kHz)
- **Time grid:** Vertical rules every second
- **Colours:** Amber band (min-to-max), brighter amber core (RMS)

## Reading the Trace

- **Tall thin spikes** — Dynamic material with sharp transients (drums, vocals)
- **Solid slab** — Compressed/normalized audio with no dynamics
- **Wide gap between core and extremes** — Dynamic range preserved
- **Narrow core close to edges** — Brick-wall limiting or clipping

The display combines both views: a track that looks like a solid slab has been aggressively compressed, while one with a thin core surrounded by peaks is percussive.

## Data Flow

```
AnalysisEngine::processPendingAudio()
    ↓
EnvelopePoint { minValue, maxValue, rms }
    ↓
LockFreeQueue<EnvelopePoint>
    ↓
WaveformView::timerCallback() (60 Hz)
    ├→ Queue.pop() up to 2048 points
    ├→ Store in ring buffer
    └→ repaint()
    ↓
WaveformView::paint()
    ├→ Per-pixel column: min/max/rms from all hops visible
    ├→ Draw extremes as filled band
    ├→ Draw RMS as bright core
    └→ Draw time grid (seconds)
```

## Ring Buffer

```cpp
static constexpr int ringCapacity = 8192;  // ~43 seconds @ 48 kHz
std::vector<EnvelopePoint> ring;
int head;    // Index one past the newest point
int numStored;  // How many valid points in ring
```

One `EnvelopePoint` per hop, recycled. When full, oldest points are overwritten by newest.

## Smoothing & Rendering

Per visible pixel column:
1. Collect all envelope points within that column's time span
2. Find the min and max across those points
3. Average the RMS values (exponential moving average)
4. Draw as band from min to max, with RMS core

Smoothing prevents pixel-level jitter while preserving envelope shape.

## Cursor Readout

Hover to see:
- **Time ago:** How many seconds back from the newest sample
- **Level:** Peak level (max of the envelope) in dBFS

Readout box is clamped inside the view and flips position to avoid the cursor.

## Consumer Interface

```cpp
WaveformView(AnalysisEngine& engine);
```

Constructor calls `engine.addConsumer()`, destructor calls `removeConsumer()`. Turns the waveform pane on/off via the rail switch, which constructs/destroys the view.

## Clear Behavior

```cpp
void clear()
{
    engine.getEnvelopeQueue().discardPending();
    head = 0;
    numStored = 0;
    repaint();
}
```

Clears the ring buffer and queue, rendering the pane dark until new audio arrives. Called when the user presses CLR or displays are reset.

## Performance

- **Memory:** ~320 KB (8192 EnvelopePoints @ 10 bytes each) + scratch buffers
- **CPU:** ~1-2 ms per frame (collecting points, drawing band)
- **Timer:** 60 Hz repaint, stops when view is invisible
