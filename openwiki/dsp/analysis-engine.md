---
type: architecture
title: Analysis Engine
description: Background thread that owns hop processing, STFT, loudness metering, and publishes results to view consumers
tags: [dsp, threading, analysis]
---

# Analysis Engine

The `AnalysisEngine` class owns the background thread that processes audio hops, runs FFT and loudness analysis, and distributes results to view consumers via lock-free queues and atomics. It is the bridge between the audio thread (which only writes to a ring buffer) and the UI thread (which only reads from queues and atomics).

## Ownership & Initialization

**Header:** `Source/dsp/AnalysisEngine.h`  
**Implementation:** `Source/dsp/AnalysisEngine.cpp`

```cpp
class AnalysisEngine final : private juce::Thread
```

### Lifecycle

1. **prepare(sampleRate, blockSize, numChannels)** — Called from `PluginProcessor::prepareToPlay`
   - Allocates hop buffers, queues, column rings, STFT analyzers
   - Starts the analysis thread at high priority
   - Called only when the plugin is ready to process audio

2. **release()** — Called from `PluginProcessor::releaseResources`
   - Stops the thread (waits up to 1 second)
   - Clears all ring buffers and queues
   - Releases atomics and state

## Consumer Registration

Views register interest on construction, unregister on destruction:

```cpp
engine.addConsumer();        // +1 to atomically-tracked consumer count
engine.removeConsumer();     // -1
bool active = engine.hasConsumers();  // true if count > 0
```

When `hasConsumers()` is false, the thread drains the ring buffer but skips STFT entirely. This is why turning off all instruments saves CPU.

## Main Loop

```cpp
void AnalysisEngine::run()
{
    while (!threadShouldExit()) {
        if (hasConsumers()) {
            if (!wasActive) {
                stft.reset();
                sideStft.reset();
                discardPendingAudio();
            }
            processPendingAudio();
        } else {
            discardPendingAudio();
        }
        wasActive = hasConsumers();
        wait(1);
    }
}
```

Poll rate of ~1 ms is much faster than the UI frame rate (60 Hz), so analysis keeps up without signalling.

## Per-Hop Processing

For each 256-sample hop:

### 1. Mono Sum & Envelope
Average channels and compute min/max/RMS, pushed to `envelopeQueue` for `WaveformView`.

### 2. FFT (Mono)
Runs STFT on mono sum, pushes columns to both `spectrumColumns` (spectrogram) and `analyserColumns` (spectrum analyser).

### 3. FFT (Side)
Processes side channel (L-R)/2 separately, pushes to `sideSpectrumColumns` for spectrum width trace.

### 4. Loudness Metering
Feeds L/R samples to `LoudnessMeter`, which updates LUFS atomics and pushes `LoudnessPoint` to queue.

### 5. Per-Sample Stereo Data
Pushes every sample as `StereoSample` to both `stereoSamples` (stereo field) and `scopeSamples` (oscilloscope).

### 6. Correlation & RMS
Computes Pearson r, stores smoothed correlation and per-channel RMS/peak atomics for needle meters.

## Data Structures

- **SampleRingBuffer** — Audio thread → analysis (non-blocking, drops if full)
- **LockFreeQueue** (envelope, loudness, stereo/scope samples) — Analysis → UI
- **ColumnRing** (spectrum, analyser, side) — Analysis → UI (FFT columns)
- **StftAnalyzer** (mono, side) — 2048-point Hann window, 256-hop, ~23 Hz bins
- **LoudnessMeter** — BS.1770-4 K-weighted, true peak
- **Atomics** (relaxed) — Correlation, RMS, peak, consumer count

## Configuration

Hop size: 256 samples (5.3 ms @ 48 kHz)  
FFT: 2048 points, Hann window, 75% overlap  
Frequency resolution: ~23 Hz per bin (@ 48 kHz)  
Sample rate: Detected at runtime

## Public Interface

Consumers pull from:
```cpp
engine.getEnvelopeQueue()              // WaveformView
engine.getSpectrumColumns()            // SpectrogramView
engine.getAnalyserColumns()            // SpectrumView
engine.getSideSpectrumColumns()        // SpectrumView (SIDE)
engine.getStereoSamples()              // StereoFieldView
engine.getScopeSamples()               // OscilloscopeView
engine.getLoudnessQueue()              // LoudnessHistoryView
engine.getLoudnessMeter()              // Any view reading LUFS
```
