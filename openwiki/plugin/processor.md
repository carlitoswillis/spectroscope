---
type: plugin
title: Audio Processor
description: Audio thread entry point, zero-latency pass-through, parameter automation, alignment tone generation
tags: [plugin, audio, processor]
---

# Audio Processor

The `SpectroscopeAudioProcessor` class is the JUCE AudioProcessor that receives audio from the host, performs zero-latency pass-through, and bridges to the analysis engine. It also manages display state (theme, pane visibility) and provides automation parameters for the three trigger actions (clear, store, marker).

## Zero-Latency Design

**Header:** `Source/PluginProcessor.h`  
**Implementation:** `Source/PluginProcessor.cpp`

```cpp
class SpectroscopeAudioProcessor final : public juce::AudioProcessor
```

### Entry Point

```cpp
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
{
    analysisEngine.pushAudio(buffer);
}
```

The audio callback copies audio into the analysis engine's ring buffer and returns the buffer unchanged to the host. Audio processing is read-only; the output is bit-for-bit identical to the input.

**Latency:** Reported as 0 samples. Hosts use latency compensation to align plugin delay with the session timeline. Zero latency means no re-shuffling when added or removed.

### Non-Blocking, Non-Allocating

```cpp
juce::ScopedNoDenormals noDenormals;
analysisEngine.pushAudio(buffer);
```

No memory allocation, no mutex, no blocking. If the ring buffer is full, the block is dropped rather than stalling.

## Parameter Management

Three momentary trigger parameters (all boolean, edge-detected):

```cpp
addParameter(clearParam  = new juce::AudioParameterBool(
    { "clear",  1 }, "Clear Displays", false));
addParameter(storeParam  = new juce::AudioParameterBool(
    { "store",  1 }, "Store Trace",    false));
addParameter(markerParam = new juce::AudioParameterBool(
    { "marker", 1 }, "Add Marker",     false));
```

### Edge Counting

Host automation modulates these parameters over time. The processor counts rising edges:

```cpp
<!-- openwiki: broken internal link [bool state, bool& previous, std::atomic<int>& counter] file "bool state, bool& previous, std::atomic<int>& counter" does not exist. Fix the href or restore the target, then delete this comment. -->
const auto countEdge = [](bool state, bool& previous, std::atomic<int>& counter) {
    if (state && !previous)
        counter.fetch_add(1, std::memory_order_relaxed);
    previous = state;
};
```

A parameter that rises and falls between timer ticks still increments once. The editor polls these counters and diffs them to detect actions.

## Alignment Tone

For measurement, an internal -18 dBFS 1 kHz sine can feed the meters:

```cpp
void setAlignmentToneEnabled(bool enabled) noexcept;
bool isAlignmentToneEnabled() const noexcept;
```

When enabled:
- Meters measure the generated tone (not the input)
- Audio output remains the input signal (untouched)
- Useful for verifying meter calibration

Phase is maintained across blocks, so the tone is continuous.

## Display State

Three properties persist across session save/load:

### Theme Index
```cpp
int getThemeIndex() const noexcept;
void setThemeIndex(int index) noexcept;
```

Index into the four-livery theme array (0-3).

### Panes Mask
```cpp
int getPanesMask() const noexcept;
void setPanesMask(int mask) noexcept;
```

Bitmask of visible instruments (1 bit per pane):
- Bit 0: Waveform
- Bit 1: Spectrogram
- Bit 2: Spectrum
- Bit 3: Stereo Field
- Bit 4: Loudness History
- Bit 5: Oscilloscope

If zero, coerced to 1 (waveform). Console is never dark.

### Floating Mask & Window Layout
```cpp
int getFloatingMask() const noexcept;
void setFloatingMask(int mask) noexcept;
juce::String getWindowLayout() const;
void setWindowLayout(const juce::String& layout);
```

Floating mask: which panes have windows. Layout: serialized bounds as `"index:x,y,w,h;..."` format.

## State Serialization

```cpp
void getStateInformation(juce::MemoryBlock& destData) override;
void setStateInformation(const void* data, int sizeInBytes) override;
```

XML format with backward compatibility: older sessions (single-pane, legacy format) are detected and upgraded to multi-pane.

## Configuration

```cpp
BusesProperties()
    .withInput("Input", juce::AudioChannelSet::stereo(), true)
    .withOutput("Output", juce::AudioChannelSet::stereo(), true);

setLatencySamples(0);
```

Stereo in/out. Mono input upmixed (both channels duplicate). Zero latency reported.

## Lifecycle

```cpp
void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
void releaseResources() override;
```

Prepare allocates and starts the analysis engine. Release stops it and deallocates.
