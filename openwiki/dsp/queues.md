---
type: component
title: Lock-Free Queues (SPSC Data Synchronization)
description: LockFreeQueue and ColumnRing implement single-producer/single-consumer queues for UI thread draining.
tags: [lock-free, threading, data-structure]
---

# Lock-Free Queues

Two queue types bridge the analysis thread and UI thread, both SPSC (single-producer/single-consumer) to avoid contention:

1. **LockFreeQueue<T>** — generic queue for scalar items (envelopes, loudness, stereo samples)
2. **ColumnRing** — fixed-width float columns (spectrogram FFT bins)

## LockFreeQueue<T>

**Location**: `Source/dsp/LockFreeQueue.h`

A templated queue of trivially copyable items. Items are copied by memcpy into a circular buffer.

### API

#### `prepare(capacity)`
Allocates storage, rounds capacity up to the next power of two:

```cpp
void prepare(int capacity) {
    const auto size = nextPowerOfTwo(jmax(16, capacity));
    storage.assign(size, ItemType{});
    fifo.setTotalSize(size);
}
```

#### `bool push(const T& item)`
**Thread**: Analysis thread only

Copies item into the queue. Returns `false` if the consumer is too slow (queue full):

```cpp
bool push(const ItemType& item) noexcept {
    if (fifo.getFreeSpace() < 1)
        return false;  // Consumer fallen behind
    
    // JUCE's FIFO wraps if needed
    int start1, size1, start2, size2;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    
    if (size1 > 0)
        storage[start1] = item;  // Memcpy equivalent
    else if (size2 > 0)
        storage[start2] = item;
    else
        return false;
    
    fifo.finishedWrite(1);
    return true;
}
```

#### `int pop(T* destination, int maxItems)`
**Thread**: UI thread only

Drains up to `maxItems` into a destination array. Returns the number actually copied:

```cpp
int pop(T* dest, int maxItems) noexcept {
    const auto numToRead = jmin(maxItems, fifo.getNumReady());
    if (numToRead <= 0) return 0;
    
    int start1, size1, start2, size2;
    fifo.prepareToRead(numToRead, start1, size1, start2, size2);
    
    // Copy contiguous segment 1
    if (size1 > 0)
        memcpy(dest, &storage[start1], size1 * sizeof(T));
    // Then segment 2 (if wrap occurred)
    if (size2 > 0)
        memcpy(dest + size1, &storage[start2], size2 * sizeof(T));
    
    fifo.finishedRead(numToRead);
    return numToRead;
}
```

### Used For

| Queue | Producer | Consumer | Item Type | Capacity | Notes |
|-------|----------|----------|-----------|----------|-------|
| `envelopeQueue` | Analysis thread | WaveformView | `EnvelopePoint` (min/max/RMS) | 10s of hops (~2K items at 48k) | Waveform display |
| `loudnessQueue` | Analysis thread | LoudnessHistoryView | `LoudnessPoint` (momentary dB) | 10s of hops | Strip chart display |
| `stereoSamples` | Analysis thread | StereoFieldView | `StereoSample` (L/R pair) | 1s of samples (~48K items at 48k) | Goniometer persistence |
| `scopeSamples` | Analysis thread | OscilloscopeView | `StereoSample` (L/R pair) | 1s of samples | Triggered oscilloscope |

### Backpressure

When a queue is full:
- `push()` returns `false`
- The hop/sample is dropped
- Analysis thread continues (never waits)
- UI sees fewer updates but remains responsive

## ColumnRing

**Location**: `Source/dsp/ColumnRing.h`

A specialized ring buffer for large items (STFT columns, ~2KB each at 48 kHz). Instead of copying items, the ring tracks column counts in a FIFO and manages a flat storage buffer.

### API

#### `prepare(binsPerColumn, numColumns)`
Allocates a flat buffer to hold `bins * columns` floats:

```cpp
void prepare(int binsPerColumn, int numColumns) {
    bins = jmax(1, binsPerColumn);  // 1025 at 48 kHz
    const auto columns = nextPowerOfTwo(jmax(8, numColumns));
    
    storage.assign(bins * columns, 0.0f);
    fifo.setTotalSize(columns);  // FIFO of column counts, not samples
}
```

#### `bool push(const float* column)`
**Thread**: Analysis thread only

Copies one column into the ring, advancing the write pointer by `bins` floats:

```cpp
bool push(const float* column) noexcept {
    if (fifo.getFreeSpace() < 1)
        return false;  // Consumer too slow
    
    int slot = (write position in FIFO)
    std::copy(column, column + bins, storage + slot * bins);
    
    fifo.finishedWrite(1);
    return true;
}
```

#### `int pop(float* destination, int maxColumns)`
**Thread**: UI thread only

Reads up to `maxColumns` columns into a flat destination buffer:

```cpp
int pop(float* dest, int maxColumns) noexcept {
    const auto numToRead = jmin(maxColumns, fifo.getNumReady());
    if (numToRead <= 0) return 0;
    
    // Copy one or two contiguous runs (if wrap occurred)
    std::copy(storage + start * bins, 
              storage + start * bins + numToRead * bins,
              dest);
    
    fifo.finishedRead(numToRead);
    return numToRead;  // Number of columns, not floats
}
```

### Used For

| Ring | Producer | Consumer | Content | Capacity | Notes |
|------|----------|----------|---------|----------|-------|
| `spectrumColumns` | Analysis thread | SpectrumView | FFT mid (1025 bins) | 2s of hops (~188 items at 48k) | Spectrum live curve |
| `analyserColumns` | Analysis thread | SpectrogramView | FFT mid (1025 bins) | 2s of hops | Spectrogram display |
| `sideSpectrumColumns` | Analysis thread | SpectrumView | FFT side (1025 bins) | 2s of hops | Spectrum width trace |

### Why Separate from LockFreeQueue

Columns are ~2 KB each (1025 floats × 4 bytes). If copied by value through LockFreeQueue:
- Queue storage would be huge (2 KB × 188 columns = 376 KB per ring)
- Every push/pop would memcpy 2 KB (overhead)
- FIFO pointer arithmetic already handles wrapping

ColumnRing avoids the double-copy: data lives in one flat buffer, the FIFO only tracks column positions.

## Dual Columns (Spectrum + Spectrogram)

**Problem**: Both SpectrumView and SpectrogramView need identical STFT data.

**Why not share one ring?** SPSC rule: one consumer per queue. If both views drained from one ring:
1. SpectrumView reads columns 0–5
2. SpectrogramView reads columns 0–5
3. They collide at position 5; one starves

**Solution**: AnalysisEngine publishes to **two rings**:

```cpp
if (stft.processHop(mono, columnScratch.data())) {
    spectrumColumns.push(columnScratch.data());   // Spectrum
    analyserColumns.push(columnScratch.data());   // Spectrogram (identical data)
}
```

Both receive the same columns. If one consumer lags, it doesn't block the other. The tradeoff: memory cost of ~376 KB × 2 (duplicate storage).

## Backlog Sizing

**Envelopes and loudness**: 10 seconds
- 48 kHz ÷ 256 samples/hop = 187.5 hops/sec
- 10 sec × 187.5 = 1875 items
- Rounded to next power of 2: 2048 slots
- Each EnvelopePoint: 12 bytes → 24 KB total

**Spectrum/spectrogram columns**: 2 seconds
- 2 sec × 187.5 = 375 hops
- Rounded: 512 slots
- Each column: 1025 floats × 4 = 4100 bytes → 2 MB per ring (×2 for dual rings = 4 MB)

**Stereo samples**: 1 second
- 48000 samples
- Rounded: 65536 slots
- Each StereoSample: 8 bytes → 512 KB per ring (×2 for dual = 1 MB)

**Total**: ~5–6 MB overhead for all queues.

## UI Draining (12 Hz Timer)

Each view's timer (set to 12 Hz, 83 ms interval) drains its queue:

```cpp
void WaveformView::timerCallback() {
    const auto numToDrain = 2048;  // Max per frame
    const auto numDrained = engine.getEnvelopeQueue().pop(scratch.data(), numToDrain);
    
    for (int i = 0; i < numDrained; ++i) {
        ring[head] = scratch[i];
        head = (head + 1) % ringCapacity;
    }
    
    repaint();
}
```

If the analysis thread produces faster than the UI drains:
- Old data falls off the display ring
- New data from the queue takes its place
- The display shows the freshest data, never stale

If the analysis thread stalls:
- Queue empties
- UI repaints with no new data (lag visible, but no crash)

## Thread Safety

- **LockFreeQueue**: No locks, no atomics. JUCE's FIFO is lock-free by design (pointer arithmetic only).
- **ColumnRing**: Identical: no locks, no atomics.
- **Analysis thread**: Only calls `push()`
- **UI thread**: Only calls `pop()`
- **Impossible to deadlock**: One producer, one consumer, no waiting.
