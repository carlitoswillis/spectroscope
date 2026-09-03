---
type: architecture
title: Threading Model and Lock-Free Primitives
description: Audio, analysis, and UI thread boundaries; lock-free queue design; backpressure semantics.
tags: [threading, lock-free, real-time]
---

# Threading Model and Lock-Free Primitives

Spectroscope enforces three independent threads with strict lock-free handoff between them. The audio thread is hard real-time (must never block); the analysis thread is high-priority but soft real-time (can drop work); the UI thread is standard message-loop.

## Thread Responsibilities

### Audio Thread (DAW host callback)
**Entry**: `PluginProcessor::processBlock()`

- Reads stereo input (or generates alignment tone if enabled)
- Copies up to two channels into SampleRingBuffer
- Advances alignment-tone phase (if tone is active)
- Detects rising edges of host-automatable trigger parameters (clear, store, marker)
- Atomically increments event counters for the UI to poll
- **Returns immediately** — no waits, no allocations, no system calls
- **Latency contribution**: Zero (pass-through analyser)

### Analysis Thread (high-priority, continuous)
**Entry**: `AnalysisEngine::run()`

- High-priority JUCE Thread, polled 1000× per second (1 ms interval)
- Drains SampleRingBuffer in hop-sized chunks
- Runs STFT (FFT + windowing) on mono sum and side channel
- Computes BS.1770 loudness (K-weighted, integrated, momentary, short-term)
- Computes correlation coefficient (L/R mid/side)
- Publishes six outputs to UI queues in one loop iteration
- **Consumer gating**: When no views are active, skips FFT work but still drains ring buffer
- **Backpressure**: If any queue is full, the hop is dropped (ERR lamp lights on UI)
- **Uptime requirement**: Must keep draining the ring to prevent audio thread dropout

### UI Thread (message loop)
**Entry**: View paint methods, timer callbacks, mouse events

- 12 Hz repaint timer (not synchronized to frame rate; runs independently)
- Drains one UI queue per active pane
- Accumulates data into view-local rings and scratch buffers
- Paints to the display or screenshot buffers (CPU or GPU path)
- Reads PluginProcessor atomics directly (pane mask, theme, window layout)
- No locks except for window layout string (rare, not on critical path)

## Lock-Free Primitives

### SampleRingBuffer
**Type**: SPSC (single-producer / single-consumer)  
**Producer**: Audio thread only  
**Consumer**: Analysis thread only

- `void write()` — audio thread, non-blocking
  - If buffer is full, drops the block and increments `droppedBlocks` counter
  - Returns immediately; never waits or allocates
- `int read()` — analysis thread, blocking wait on full hop
  - Reads exactly `numSamples` if available, else returns 0 (consumer must retry)
  - Uses JUCE's `AbstractFifo` under the hood

**Why SPSC**: Audio callbacks may be called from any thread (JUCE, host-dependent); only the analysis thread reads, guaranteeing no contention.

### LockFreeQueue<T>
**Type**: SPSC, templated on trivially copyable items  
**Producer**: Analysis thread only (one queue writer)  
**Consumer**: UI thread only (one queue reader per pane)

- `bool push(const T&)` — analysis thread
  - Copies item into circular buffer via memcpy
  - Returns `false` if consumer fell behind (queue full)
- `int pop(T* dest, int maxItems)` — UI thread
  - Drains up to `maxItems` into destination array
  - Returns number of items written
  - Never blocks

Used for: `envelopeQueue`, `stereoSamples`, `scopeSamples`, `loudnessQueue`.

### ColumnRing
**Type**: SPSC, fixed-width float columns (spectrogram FFT bins)  
**Producer**: Analysis thread only  
**Consumer**: UI thread only (but two views may share)

- `bool push(const float*)` — analysis thread
  - Copies one column of FFT magnitudes into ring buffer
  - Ring is circular; new data overwrites oldest when full
  - Returns `false` if full (consumer too slow)
- `int pop(float* dest, int maxCols)` — UI thread
  - Reads up to `maxCols` consecutive columns
  - No internal copying; destination provided by caller
  - Returns number of columns copied

Used for: `spectrumColumns`, `analyserColumns`, `sideSpectrumColumns`.

**Why separate from LockFreeQueue**: Columns are large (~2KB at 48 kHz); passing by value would bloat the queue structure. A count-based ring avoids that cost.

### Dual Columns (Spectrum + Spectrogram Problem)

AnalysisEngine publishes `spectrumColumns` (consumed by SpectrumView) and `analyserColumns` (consumed by SpectrogramView). Both receive identical data from the same STFT loop.

**Why not one queue?** SPSC principle: one queue, one consumer. If two views drain from one ring, they contend for position and can starve each other.

**Solution**: Analysis thread pushes to both rings on every hop. The `push()` may fail (queue full) independently per ring; if one consumer lags, it doesn't block the other.

## Atomics (Relaxed Memory Order)

Non-queue synchronization uses relaxed atomics (no barriers):

- `currentSampleRate`, `currentBlockSize` — audio thread writes, UI thread reads
- `panesMask`, `themeIndex`, `floatingMask` — audio thread writes, UI thread reads (state updates)
- `clearEvents`, `storeEvents`, `markerEvents` — audio thread increments counters, UI thread polls diffs
- `alignmentToneEnabled` — audio thread reads, UI thread writes
- `consumerCount` — analysis thread reads, UI threads increment/decrement on pane activation
- `smoothedCorrelation`, `leftRms`, `rightRms`, etc. — analysis thread writes, UI thread reads (meters)

**Memory order**: `memory_order_relaxed` throughout. Reads and writes are independent; there are no dependencies requiring ordering. This is safe because:
1. No value depends on another
2. Each atomic is read-only by one thread class (UI reads processor state; audio reads tone enable)
3. The analysis thread is the sole writer of all meter values

## Backpressure & Dropout Handling

**When the analysis thread can't keep up:**
- Queues become full; `push()` returns `false`
- Analysis engine tracks this via `ERR` lamp on UI (set when a column or envelope point is dropped)
- Old UI data is discarded; the newest complete hop is always shown
- Audio thread is **never blocked** — if analysis lags, blocks are dropped at the source (SampleRingBuffer)

**When the UI thread can't keep up:**
- Queues drain passively (analysis thread still pushes, old data falls off the ring)
- UI misses frames; the display updates less frequently
- The spec allows this: analysis stays real-time, UI is best-effort

**Consumer gating:**
When no panes are active (all switched off), the AnalysisEngine checks `consumerCount`:
```
if (hasConsumers()) {
    stft.processHop(...);  // FFT happens
    loudnessMeter.processHop(...);
} else {
    // Still drain the ring, but skip analysis work
}
```
This is the only place the analysis thread does conditional work based on UI state.

## State Serialization (Rare)

The plugin processor holds `windowLayout` as a string (e.g., `"0:100,50,400,300;2:500,100,800,600;"`). This is the only data that requires a lock:

```cpp
void setWindowLayout(const String& layout) {
    const juce::ScopedLock lock (windowLayoutLock);
    windowLayout = layout;
}
```

**Why a lock?** The string is not trivially copyable; reads and writes would race if both happened concurrently. In practice, only the UI thread writes (when a window moves) and state serialization reads (during save), so contention is nil and the lock is held for microseconds.

All other state (pane mask, theme, floating mask, trigger counts, meter readings) is atomic.

## Deadlock Prevention

No locks are held while waiting for other threads. The only lock (window layout) is dropped before calling into other subsystems. Audio thread never locks. Analysis thread never locks. **Impossible to deadlock.**