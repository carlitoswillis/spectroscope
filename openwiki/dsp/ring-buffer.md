---
type: architecture
title: Sample Ring Buffer
description: Lock-free single-producer/single-consumer circular buffer bridging audio and analysis threads
tags: [dsp, threading, lock-free]
---

# Sample Ring Buffer

The `SampleRingBuffer` class is a bounded, lock-free circular buffer that bridges the audio thread (producer) and analysis thread (consumer). It is the entry point for all audio into the analysis pipeline.

## Design

**Header:** `Source/dsp/SampleRingBuffer.h`  
**Implementation:** `Source/dsp/SampleRingBuffer.cpp`

```cpp
class SampleRingBuffer
```

### Single-Producer / Single-Consumer (SPSC)

Only one thread writes (audio callback), only one thread reads (analysis engine). No locks, no atomic CAS loops, no memory barriers beyond release/acquire semantics implicit in the AbstractFifo.

### Non-Blocking Write

```cpp
void write(const juce::AudioBuffer<float>& source) noexcept
{
    if (fifo.getFreeSpace() < numSamples) {
        droppedBlocks.fetch_add(1);
        return;  // Drop, never block
    }
    // Copy into ring
}
```

If the consumer has fallen behind and the buffer is full, the block is silently dropped. This is preferable to blocking the audio callback, which would glitch.

### Drop Behavior

When a block is dropped, the ringBuffer state is left intact — no partially-written samples, no corruption. The write simply returns without modifying the FIFO pointer. Diagnostic counter `droppedBlocks` increments.

## Capacity & Sizing

```cpp
analysiEngine.prepare(sampleRate, blockSize, numChannels);
// Capacity = max(8 × blockSize, sampleRate × 0.25)
```

At 48 kHz with a 256-sample host block:
- 8 × 256 = 2048 samples = 42.7 ms of headroom
- 0.25 × 48000 = 12000 samples = 250 ms of headroom
- **Capacity = 12288 samples (rounded up to next power of two: 16384)**

This gives the analysis thread a quarter second to catch up if the UI stalls. With a 1 ms poll rate and typical DSP cost, this is never reached unless the analysis thread is completely blocked (which never happens — it's just a tight loop).

## Channel Handling

```cpp
ringBuffer.prepare(2, capacity);  // Store up to 2 channels
ringBuffer.write(monoBuffer);      // Mono source
// Result: both stored channels duplicate the mono input

ringBuffer.write(stereoBuffer);    // Stereo source
// Result: L stores channel 0, R stores channel 1
```

If the host sends mono, both channels store the same data. If the host sends more than 2 channels, only the first 2 are used. Spectroscope reports stereo bus configuration to the host so mono-only sends are rare, but the buffer handles both gracefully.

## Read Interface

```cpp
int read(juce::AudioBuffer<float>& destination, int numSamples) noexcept
{
    if (fifo.getNumReady() < numSamples)
        return 0;  // Partial reads not allowed
    
    // Copy exactly numSamples or nothing
    return numSamples;  // On success
}
```

The analysis engine only reads when a complete hop (256 samples) is available. Partial reads are never performed, which keeps the synchronization between analysis threads and the ring state simple.

## Diagnostics

```cpp
int getNumDroppedBlocks() const noexcept
{
    return droppedBlocks.load(memory_order_relaxed);
}
```

If drops are accumulating, the analysis thread is slower than the audio thread. This can happen if:
1. The analysis thread is starved by the OS scheduler (high system load)
2. The FFT is too expensive for the hop cadence (can't happen with 2048-point at 5.3 ms)
3. DSP computation is extremely heavy (not applicable in Spectroscope's case)

In practice, drops should be zero or near-zero on properly configured hardware.

## Lifecycle

```cpp
analysiEngine.prepare(...)   // Calls ringBuffer.prepare()
analysiEngine.release()      // Calls ringBuffer.reset()
```

`prepare` allocates storage and resets the FIFO.  
`reset` clears all pending samples and resets pointers. Safe only when both threads are stopped.

## Implementation Details

Underneath, `juce::AbstractFifo` tracks read and write pointers as atomics. The buffer itself is a regular `AudioBuffer`, with channel copying handled per-channel to support wrap-around (when the write pointer wraps, data may split into two contiguous regions).

The FIFO's `prepareToWrite` and `finishedWrite` calls ensure memory ordering is correct for the handoff between threads, even though no mutex is used.
