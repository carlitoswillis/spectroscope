---
type: component
title: Audio Processor (Zero-Latency Pass-Through)
description: PluginProcessor handles audio callbacks, manages the analysis engine lifecycle, and implements host-automatable triggers.
tags: [dsp, processor, audio-callback, real-time]
---

# Audio Processor

The `PluginProcessor` class is the plugin's audio-thread boundary. It inherits from `juce::AudioProcessor` and is instantiated once per plugin instance (one per DAW track or window).

## Interface Contract

**Entry points** (called by the DAW host):
- `prepareToPlay(sampleRate, blockSize)` — allocates buffers, starts the AnalysisEngine
- `releaseResources()` — stops the AnalysisEngine, deallocates
- `processBlock(buffer, midiBuffer)` — called for every audio callback (hard real-time)
- `isBusesLayoutSupported()` — validates mono or stereo input/output matching
- `getStateInformation()` / `setStateInformation()` — persistence (UI pane mask, theme, window layout)

## processBlock: The Audio Thread

**Location**: `Source/PluginProcessor.cpp:processBlock()`

Called by the host at every audio callback (e.g., every 2.67 ms at 44.1 kHz, 512-sample block). Must return immediately; cannot allocate, lock, or wait.

```cpp
void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
{
    // 1. Pass audio through untouched — ringBuffer.write() is non-blocking
    analysisEngine.pushAudio (buffer);
    
    // 2. Detect rising edges of host-automatable triggers
    countEdge (clearParam->get(),  previousClear,  clearEvents);
    countEdge (storeParam->get(),  previousStore,  storeEvents);
    countEdge (markerParam->get(), previousMarker, markerEvents);
    
    // 3. If alignment tone is enabled, fill a pre-allocated buffer with -18 dBFS 1 kHz sine
    if (alignmentToneEnabled && toneBuffer.getNumSamples() > 0) {
        generateAlignmentTone();
        analysisEngine.pushAudio (toneBuffer);  // Tone goes to meters only, not output
    }
}
```

**Key invariants:**
- Audio is never blocked or copied expensively; `ringBuffer.write()` is one bounded memcpy or a drop
- `processBlock` returns immediately — analysis happens in AnalysisEngine on a separate thread
- Latency is zero: reported to the host via `setLatencySamples(0)`
- Output is bit-for-bit identical to input (pass-through analyser)
- Trigger parameters use edge counting, not levels, because a host may automate a parameter that rises and falls between plugin callbacks

## Trigger Parameters

Three `AudioParameterBool` instances registered in the constructor:
- `clearParam` — CLEAR DISPLAYS
- `storeParam` — STORE TRACE  
- `markerParam` — ADD MARKER

**Edge detection** (audio thread):
```cpp
void countEdge (bool state, bool& previous, std::atomic<int>& counter) {
    if (state && !previous)  // Rising edge only
        counter.fetch_add(1, std::memory_order_relaxed);
    previous = state;
}
```

**UI polling** (message thread):
The editor's timer polls the counters:
```cpp
auto clearCount = processor.getClearEvents();
if (clearCount != lastClearEvents) {
    clearAllDisplays();
    lastClearEvents = clearCount;
}
```

Using counters instead of bools ensures a pulse that rises and falls between two timer ticks still lands.

## Alignment Tone

**Purpose**: A -18 dBFS 1 kHz sine generated inside the processor, fed to the AnalysisEngine (and thus the meters) only. The audio output is untouched.

**Validation recipe**:
1. Press TONE to enable alignment tone
2. Read the CHART meter: integrated LUFS should stabilize at -15.0 (a -18 dBFS sine is -15 LUFS)
3. True peak should read ~-0.5 dBTP (4x-oversampled estimate of the tone)

If the tone reads differently, the loudness meter is miscalibrated.

**Implementation**:
- Phase accumulator (`tonePhase`, double) persists across blocks for continuity
- Tone buffer allocated in `prepareToPlay()` with the same size as the host blockSize
- Sine generation via `std::sin(2π * phase)` at 1000 Hz
- Both left and right channels receive the same tone

## State Persistence

Plugin state (saved with the DAW session) is serialized via JUCE's binary blob API:

```cpp
void getStateInformation (juce::MemoryBlock& destData) override;
void setStateInformation (const void* data, int sizeInBytes) override;
```

**Saved fields:**
- `panesMask` (6 bits: which instruments are visible)
- `themeIndex` (0–3: livery palette)
- `floatingMask` (6 bits: which panes float in windows)
- `windowLayout` (string: `"index:x,y,w,h;..."` for each floating window)

All four are read back on plugin load and applied to the editor, restoring the exact UI state.

## Atomics for Cross-Thread State

The processor holds several atomic fields for the UI to read without locks:

- `currentSampleRate`, `currentBlockSize` — audio thread writes, UI reads for display
- `panesMask`, `themeIndex`, `floatingMask` — UI writes (state updates), audio thread reads (state save)
- `alignmentToneEnabled` — UI writes, audio thread reads
- `clearEvents`, `storeEvents`, `markerEvents` — audio thread increments, UI polls diffs
- `windowLayout` — guarded by one lock (rare, not on hot path)

All are `memory_order_relaxed`: no ordering needed between independent values.

## getAnalysisEngine()

The editor calls this to get a reference to the AnalysisEngine:
```cpp
AnalysisEngine& getAnalysisEngine() noexcept { return analysisEngine; }
```

The engine lives for the lifetime of the processor and coordinates all analysis work.

## Audio Bus Layout

**Supported configurations** (validated in `isBusesLayoutSupported()`):
- Mono input → mono output
- Stereo input → stereo output

**Why matching?** The analyser is pass-through; if the input is stereo, the output must be stereo unchanged. Mono in, mono out.

## Sample Rate & Block Size

Both are cached atomically in `prepareToPlay()` for the UI to read:
```cpp
CurrentSampleRate.store(sampleRate, std::memory_order_relaxed);
currentBlockSize.store(maximumExpectedSamplesPerBlock, std::memory_order_relaxed);
```

The CHART pane displays the sample rate in the header. AnalysisEngine uses both to configure FFT size, hop size, and history buffers.
