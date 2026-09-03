---
type: component
title: Audio Processor
description: JUCE AudioProcessor, zero-latency pass-through, ring buffer producer, parameter edge counting
---

# Audio Processor

The SpectroscopeAudioProcessor is the JUCE AudioProcessor implementation. Audio passes through bit-for-bit untouched; the only work on the audio thread is a bounded non-blocking ring buffer write and optional tone generation for meter validation.

## Zero Latency

Source: `Source/PluginProcessor.h/cpp`

```cpp
class SpectroscopeAudioProcessor : public juce::AudioProcessor {
  SpectroscopeAudioProcessor();
  void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(AudioBuffer<float>&, MidiBuffer&) override;
  
  AnalysisEngine& getAnalysisEngine() noexcept;
};
```

In the constructor, `setLatencySamples(0)` tells the host no delay is introduced. This is correct: audio goes straight from input to output without waiting for analysis.

## Bus Configuration

**Source:** `Source/PluginProcessor.cpp:5-7`

```cpp
AudioProcessor(BusesProperties()
  .withInput(\"Input\", AudioChannelSet::stereo(), true)
  .withOutput(\"Output\", AudioChannelSet::stereo(), true))
```

One stereo input, one stereo output. Both mono and stereo are supported; mismatched input/output counts are rejected by `isBusesLayoutSupported()`.

## Process Block (Audio Thread)

**Source:** `Source/PluginProcessor.cpp:52-113`

```cpp
processBlock(buffer, midiBuffer) {
  // Clear any uninitialized output channels
  for (ch = numInputChannels; ch < numOutputChannels; ++ch)
    buffer.clear(ch, 0, buffer.getNumSamples());
  
  // Count rising edges of parameter triggers
  countEdge(clearParam->get(), previousClear, clearEvents);
  countEdge(storeParam->get(), previousStore, storeEvents);
  countEdge(markerParam->get(), previousMarker, markerEvents);
  
  // Optionally generate -18 dBFS 1 kHz tone for meter validation
  if (alignmentToneEnabled) {
    generateAlignmentTone(toneBuffer);
    analysisEngine.pushAudio(toneBuffer);
  } else {
    analysisEngine.pushAudio(buffer);
  }
  
  // Audio output = input (or tone if engaged, but tone doesn't reach output)
}
```

**Key points:**
- Audio is passed through untouched (input pointer copied to output)
- No latency, no buffering before output
- Tone generation (if engaged) feeds the meters only; output is always the input
- Parameter edges are counted for UI consumption

## Alignment Tone

**Source:** `Source/PluginProcessor.h:55-63`

```cpp
void setAlignmentToneEnabled(bool enabled) noexcept {
  alignmentToneEnabled.store(enabled, memory_order_relaxed);
}
```

When enabled, processBlock generates a −18 dBFS 1 kHz sine wave into `toneBuffer` and pushes it to the analysis engine instead of the input. The audio output is still the input (bit-for-bit pass-through). The tone exists only for the meters.

**Purpose:** Validate the loudness meter. With the tone engaged, the chart should read −15.0 LUFS integrated and −1 dBTP true peak. This proves the K-weighting and true-peak detector are correctly calibrated.

**Tone generation:** Phase carries across blocks so the sine is continuous, not a burst per block.

## Parameter Edge Counting

**Source:** `Source/PluginProcessor.cpp:63-73`

```cpp
<!-- openwiki: broken internal link [bool state, bool& previous, std::atomic<int>& counter] file "bool state, bool& previous, std::atomic<int>& counter" does not exist. Fix the href or restore the target, then delete this comment. -->
const auto countEdge = [](bool state, bool& previous, std::atomic<int>& counter) {
  if (state && !previous)
    counter.fetch_add(1, memory_order_relaxed);
  previous = state;
};

countEdge(clearParam->get(), previousClear, clearEvents);
countEdge(storeParam->get(), previousStore, storeEvents);
countEdge(markerParam->get(), previousMarker, markerEvents);
```

Three momentary boolean parameters (default false) are monitored. On each block, if the parameter has risen (false→true), the corresponding counter increments. The editor polls these counters and diffs them on its timer. A rising edge that goes high and low between two timer ticks still increments the counter once, so the action fires.

**Why counts instead of levels:** Hosts may not poll parameters every block. A momentary parameter that pulses for one block would be missed if the editor only read the current level.

## State Serialization

**Source:** `Source/PluginProcessor.h:40-41`

```cpp
void getStateInformation(MemoryBlock& destData) override;
void setStateInformation(const void* data, int sizeInBytes) override;
```

Display settings (not audio parameters) are serialized to XML:
- Theme index (0-3)
- Panes mask (6 bits)
- Floating mask (6 bits)
- Window layout bounds

**Why not audio parameters:** The three parameters are momentary triggers; restoring their state would replay actions the user didn't just perform. Plugin state contains display choices, not audio settings.

## Ring Buffer & Analysis Engine

`analysisEngine` is owned by the processor and initialized in `prepareToPlay()`, released in `releaseResources()`. The processor passes each block to `analysisEngine.pushAudio()`, a lock-free non-blocking write.
