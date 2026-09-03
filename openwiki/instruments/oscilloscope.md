---
type: instrument
title: Oscilloscope View (SCOPE)
description: Triggered mono waveform with rising zero-crossing detection, hysteresis, free-run fallback
tags: [instruments, oscilloscope, waveform, trigger]
---

# Oscilloscope View (SCOPE)

The oscilloscope displays a 1024-sample triggered window of the mono sum (L+R average), re-armed on a rising zero crossing so pitched material stands still on the screen instead of rolling. No rising crossing within reach, or silence that never clears the hysteresis band, and it free-runs from the newest samples. The display shares the per-sample stereo queue with the stereo field view, so only one can draw at a time.

## Display Characteristics

**Header:** `Source/gui/OscilloscopeView.h`  
**Implementation:** `Source/gui/OscilloscopeView.cpp`

- **Window:** 1024 samples of mono audio
- **Newest at:** Right edge (time flows left)
- **Time scale:** ~21 ms @ 48 kHz (one full period of a 50 Hz sine)
- **Vertical scale:** -1.0 to +1.0 (full-scale sine)
- **Triggering:** Rising zero crossing with hysteresis
- **Grid:** 10 vertical divisions (dB levels), horizontal time rules
- **Cursor readout:** Sample age, level in dBFS

## Reading the Trace

- **Sine wave standing still** — Triggered, captured from fundamental frequency
- **Rolling wave** — Free-running (no rising zero crossing found)
- **Clean sine** — Pure tone
- **Fuzzy/noisy trace** — Broadband content or noise mixed with tone
- **Flat line** — Silence
- **Clipping (flat top/bottom)** — Signal is hitting hard ceiling (likely -1.0 dBFS)

## Trigger Logic

```cpp
int findTriggerStart() const noexcept
{
    // Search back up to triggerSearchSpan (~4096 samples, ~85 ms)
    // for a rising zero crossing with hysteresis
    
    // 1. Wait for signal to dip below -h
    if (previousSample < -triggerHysteresis)  // -0.005
        triggerArmed = true;
    
    // 2. On rising edge through +h, trigger
    if (triggerArmed && currentSample > triggerHysteresis)
        return foundTriggerAge;
    
    // 3. If no crossing found within span, free-run
    return -1;  // Use newest sample
}
```

Hysteresis = 0.005 (about -46 dBFS) keeps low-level noise from re-triggering every sample. The signal must dip below -0.005 before a rise above +0.005 counts as a crossing.

## Search Span

```cpp
static constexpr int triggerSearchSpan = 4096;  // ~85 ms @ 48 kHz
```

If no rising crossing is found within the last 85 ms, the oscilloscope free-runs from the newest samples. This is better than no display and keeps the window updating even during silence or when trigger conditions fail.

## Data Flow

```
AnalysisEngine::processPendingAudio()
    ↓
For each sample in hopBuffer:
    StereoSample { left, right }
        ↓
    LockFreeQueue::push (scopeSamples)
    ↓
OscilloscopeView::timerCallback() (60 Hz, only if active)
    ├→ scopeSamples.pop() up to 4096
    ├→ Store samples in ring
    ├→ Find trigger point
    ├→ Extract 1024-sample window from trigger
    └→ repaint() with window
```

ScopeSamples queue is single-consumer (only Oscilloscope pulls from it). If both Stereo Field and Oscilloscope are visible, only one can be active; switching turns off the other.

## Ring Buffer

```cpp
static constexpr int ringCapacity = 16384;  // ~340 ms of samples
std::vector<float> ring;  // Stores mono sum (L+R)/2
int head;  // Index one past newest
int numStored;  // How many valid samples
```

One per-sample value per audio sample, recycled. With a 16 KB ring at 48 kHz, about 340 ms of history is available for trigger search.

## Mono Sum

Fed from the stereo queue, averaged to mono:
```cpp
float mono = (left + right) * 0.5f;
```

Simpler than displaying stereo and makes trigger detection stable (a single zero-crossing reference).

## Consumer Interface

```cpp
OscilloscopeView(AnalysisEngine& engine);
void setActive(bool shouldBeActive);
```

`setActive(true)` starts the 60 Hz timer and discards any backlog. `setActive(false)` stops the timer. Only one of OscilloscopeView or StereoFieldView can be active.

## Clear Behavior

```cpp
void clear()
{
    std::fill(ring.begin(), ring.end(), 0.0f);
    head = 0;
    numStored = 0;
    lastFrameTriggered = false;
    repaint();
}
```

Clears the sample ring and resets trigger state. Display goes flat until new audio arrives.

## Cursor Readout

Hover to see:
- **Sample age:** How many samples back from the newest (0 = newest)
- **Level:** Sample value in dBFS

Readout box flips position to stay inside the view.

## Rendering

Per-pixel rendering:
1. For each screen column, find the sample range over the time span it covers
2. Draw min-to-max as a thin line (antialiased)
3. Overlay the grid and time axis labels
4. Composite the cursor readout if hovering

## Performance

- **Memory:** ~64 KB (16384 samples) + scratch buffers
- **CPU (60 Hz):** ~3-5 ms (queue drain, trigger search, per-pixel rendering, grid)
- **Timer:** 60 Hz repaint, stops when view is inactive
