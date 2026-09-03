---
type: component
title: Short-Time Fourier Transform (STFT Analyzer)
description: StftAnalyzer performs windowed FFT on hop-sized audio chunks, producing frequency-domain bins for spectrogram and spectrum display.
tags: [dsp, fft, spectral-analysis]
---

# STFT Analyzer

The `StftAnalyzer` class performs overlapping windowed FFT (fast Fourier transform) on fixed-size audio chunks. One instance analyzes the mono mix; another analyzes the stereo side channel (L−R).

## Configuration

**Location**: `Source/dsp/StftAnalyzer.h/cpp`

**Parameters** (set in `prepare()`):
- **FFT order**: 11 (2048-point FFT)
- **Hop size**: 256 samples
- **Window**: Hann window (raised cosine)
- **Overlap**: 75% (1792 samples of the previous frame are retained)

**At 48 kHz**:
- One hop = 256 samples = 5.3 ms
- Frequency resolution = 48000 / 2048 ≈ 23.4 Hz per bin
- Spectral updates every 5.3 ms (188 Hz refresh rate)

## API

### `prepare(fftOrder, hopSize, sampleRate)`
Allocates FFT workspace, initializes the sliding window, and calculates the magnitude scale:

```cpp
void prepare(int fftOrder, int hopSize, double sampleRate);
```

**Magnitude scaling**:
A Hann window sums to N/2. A real sine splits its power between positive and negative frequency bins. The scale factor ensures a full-scale sine sitting on a bin centre reads exactly 0 dB:

```
magnitude_scale = 2.0 / window_sum
```

DC and Nyquist bins (which have no mirror) are scaled by half.

### `processHop(const float* monoHop, float* magnitudesDb)`
Processes one 256-sample hop and writes dB magnitudes:

```cpp
bool processHop(const float* monoHop, float* magnitudesDb) noexcept;
```

**Returns**: `true` if a full FFT was produced, `false` on startup (until the sliding window fills).

**Behavior**:
1. Slide the history left by hop size (256 samples)
2. Append new 256 samples to the right
3. Apply Hann window to the full 2048-sample frame
4. Run FFT
5. Convert magnitudes to dB (floored at −100 dB)
6. Write 1025 bin values (0 Hz to 24 kHz at 48 kHz)

### `reset()`
Clears the sliding window, used on transport jump or consumer reactivation.

### `getNumBins() const`
Returns `fftSize / 2 + 1` (1025 at 2048-point FFT).

### `getBinFrequency(int bin) const`
Returns the center frequency in Hz for a given bin:

```cpp
float getBinFrequency(int bin) const {
    return bin * sampleRate / fftSize;
}
```

### `getBinForFrequency(double frequency) const`
Inverse: returns the bin index nearest a given frequency.

## Sliding Window Mechanics

**Memory**: `std::vector<float> slidingWindow` holds 2048 samples at 48 kHz.

**Update per hop**:
```cpp
// Slide left by 256 samples
std::memmove(slidingWindow.data(), 
             slidingWindow.data() + 256, 
             1792 * sizeof(float));

// Append new hop at the right
std::memcpy(slidingWindow.data() + 1792,
            monoHop, 
            256 * sizeof(float));
```

**Why overlap?** The Hann window tapers to zero at the edges. Overlapping windows ensure continuous coverage: samples at the edge of one frame are weighted heavily in an adjacent frame, preventing a gap or discontinuity.

## Magnitude Computation

After FFT, magnitudes are computed from the complex FFT output:

```cpp
for (int bin = 0; bin < numBins; ++bin) {
    const auto scale = (bin == 0 || bin == numBins - 1) ? 
        magnitudeScale * 0.5f : magnitudeScale;
    
    const auto magnitude = fftData[bin] * scale;
    magnitudesDb[bin] = magnitude > 0.0f 
        ? jmax(floorDb, Decibels::gainToDecibels(magnitude, floorDb))
        : floorDb;
}
```

**DC and Nyquist**: Scaled by half because they have no mirror in the complex spectrum.

**Floor**: −100 dB. Anything quieter is reported as floor (prevents log(0) and clutter at low levels).

## Sample-Rate Independence

The window and FFT are computed at runtime for the actual session sample rate:

```cpp
void prepare(int fftOrder, int hopSize, double sampleRate) {
    fftSize = 1 << fftOrder;  // 2048
    hop = hopSize;             // 256
    currentSampleRate = sampleRate;  // 44.1k, 48k, 96k, etc.
    
    // JUCE FFT constructor takes only fftOrder, not sample rate
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
}
```

Frequency resolution automatically scales: at 96 kHz with the same 2048-point FFT, bins are twice as close together.

## Startup Behavior

The FFT returns `false` until the sliding window is full:

```cpp
if (samplesUntilFull > 0) {
    samplesUntilFull -= hop;
    if (samplesUntilFull > 0)
        return false;  // Not ready yet
}
```

At 48 kHz, it takes 2048 samples = ~42 ms before the first FFT result. The UI waits until `processHop()` returns true before drawing the first spectrogram column.

## Data Output

**2048-point FFT at 48 kHz**:
- 1025 bins (0 to 24 kHz, each ~23.4 Hz wide)
- dB scale: −100 (silence) to 0 (full scale)
- Written to a flat float array (caller-provided)

**AnalysisEngine usage**:
```cpp
if (stft.processHop(mono, columnScratch.data())) {
    spectrumColumns.push(columnScratch.data());  // 1025 floats
    analyserColumns.push(columnScratch.data());  // Same data, different ring
}
```

## Real-Time Safety

- Pre-allocated FFT workspace (JUCE's `dsp::FFT` allocates once in `prepare()`)
- Pre-allocated sliding window
- `processHop()` never allocates or locks
- Deterministic runtime: O(n log n) for the FFT, dominated by JUCE's implementation

## Integration: Mono and Side Channels

AnalysisEngine runs **two** identical analyzers:

1. **`stft`**: Mono sum (L+R)/2 → `spectrumColumns` and `analyserColumns`
2. **`sideStft`**: Stereo side (L−R)/2 → `sideSpectrumColumns`

Both are fed from the same hop-sized chunk, so their 1025-bin results line up in time. SpectrumView overlays both to show width-per-frequency: where the side trace hugs the mid trace, the material is mono; where it falls away, it's wide.
