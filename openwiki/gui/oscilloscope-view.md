---
type: component
title: Oscilloscope View
description: Triggered 1024-sample mono sweep with rising zero-crossing detection and hysteresis
---

# Oscilloscope View

The OscilloscopeView is a triggered oscilloscope: a ~21 ms window of the audio waveform that stands still on pitched material. The trigger hunts a rising zero crossing with hysteresis, so a sustained note draws its true wave shape, stationary. Silence or noise without a confident crossing free-runs from the newest samples.

## Design

Source: `Source/gui/OscilloscopeView.h/cpp`

```cpp
class OscilloscopeView : public Component, Timer {
  explicit OscilloscopeView(AnalysisEngine&);
  void paint(Graphics&) override;
  void setActive(bool);
  void clear();
};
```

## Trigger Algorithm

**Source:** `Source/gui/OscilloscopeView.h:42-56`

```cpp
static constexpr int windowLength = 1024;           // ~21 ms at 48 kHz
static constexpr int triggerSearchSpan = 4096;      // look back ~86 ms
static constexpr float triggerHysteresis = 0.005f;  // prevent chatter
```

The trigger logic:

1. Search backwards from the newest sample up to 4096 samples
2. Hunt for a rising zero crossing (sample went from ≤ 0 to > 0)
3. Apply hysteresis: the signal must dip below −0.005 before a rise above +0.005 counts
4. This prevents the trigger from re-arming on noise that never clearly crosses

If a crossing is found, the window starts at that sample (the trigger point). The display shows the last 1024 samples *after* the trigger.

## Free-Run Mode

If no crossing is found within the search span, the oscilloscope free-runs from the newest samples. This happens with silence (no signal to cross zero) or noise (no confident crossing).

The corner readout displays either:
- `TRIG RISING` (triggered, showing the waveform after a zero crossing)
- `FREE RUN` (no trigger found, showing the newest audio)

## Visual Appearance

A single trace of the mono sum (L+R)/2, drawn as a phosphor line. The X-axis is time (~21 ms), Y-axis is amplitude (−1 to +1, normalized to the window).

Use it to read wave *shape*:
- **Soft sine:** Bass that's nearly sinusoidal
- **Square-ish:** Bass that's saturated or distorted
- **Ragged spikes:** Noise or transient

## Queue Management

**Consumer:** Pops from `engine.getScopeSamples()` (LockFreeQueue<StereoSample>, single-consumer). One sample per analysis sample in the hop (256 per hop at 48 kHz).

**Active gating:** `setActive(bool)` stops the timer when not visible. On re-enable, pending samples are discarded so the trace shows current audio, not a backlog.

## Ring Buffer

**Source:** `Source/gui/OscilloscopeView.h:49-51`

```cpp
static constexpr int ringCapacity = 16384;      // ~341 ms at 48 kHz
```

Keeps 16384 samples of history so the trigger can search back up to 4096 samples without hitting the end of the buffer.

## Cursor Readout

Hover over the scope to show time-offset-from-trigger and amplitude at the cursor. Clamped to the view bounds like other instruments.

## Clear Action

`clear()` wipes the sample history. The sweep goes flat until new audio lands. Used when pressing CLR or CLR automation parameter.