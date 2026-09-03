---
type: dsp
title: STFT Analysis
description: Short-time Fourier transform with 2048-point FFT, Hann window, 256-sample hops
tags: [dsp, analysis, fft, spectrum]
---

# STFT Analysis

The `StftAnalyzer` class computes the real-time spectrogram using a 2048-point FFT with 75% overlap. Two instances run in parallel: one on the mono sum (for spectrogram and spectrum analyser), one on the side channel (for stereo width measurement).

## Configuration

**Header:** `Source/dsp/StftAnalyzer.h`  
**Implementation:** `Source/dsp/StftAnalyzer.cpp`

```cpp
class StftAnalyzer
```

### Sizing
- **FFT Size:** 2048 points (11 bits = order 11)
- **Hop Size:** 256 samples
- **Overlap:** (2048 - 256) / 256 = 7 hops, 75% overlap
- **Cadence:** 5.33 ms per column @ 48 kHz
- **Frequency Resolution:** 48000 / 2048 ≈ 23.4 Hz per bin
- **Output Bins:** 1025 (DC, 1..1023, Nyquist)

### Window

Hann window (raised cosine), applied at FFT time, not pre-multiplied into the buffer. Normalization:
- Window sum = 1024 (for 2048-point Hann)
- With 75% overlap, overlapping windows sum to 1.0 (perfect reconstruction)
- Scaling factor = 2.0 / windowSum = 1/512 (accounts for bin splitting in real FFT)

For a full-scale (1.0 amplitude) sine on a bin center:
- Real sine energy splits between positive and negative frequency bins
- Each bin gets half the energy
- Scaling of 2/windowSum recovers 0 dBFS (1.0 linear)

## Processing Loop

```cpp
bool processHop(const float* monoHop, float* magnitudesDb) noexcept
{
    // Slide history left, append new hop
    std::memmove(slidingWindow.data(),
                  slidingWindow.data() + hop,
                  retained * sizeof(float));
    std::memcpy(slidingWindow.data() + retained,
                monoHop, hop * sizeof(float));
    
    if (samplesUntilFull > 0) {
        samplesUntilFull -= hop;
        if (samplesUntilFull > 0)
            return false;  // Not ready yet
    }
    
    // Apply window and FFT
    std::copy(slidingWindow.begin(), slidingWindow.end(), fftData.begin());
    window->multiplyWithWindowingTable(fftData.data(), fftSize);
    fft->performFrequencyOnlyForwardTransform(fftData.data());
    
    // Convert to dB
    for (int bin = 0; bin < numBins; ++bin) {
        float magnitude = fftData[bin] * magnitudeScale;
        magnitudesDb[bin] = magnitude > 0.0f
            ? jmax(floorDb, gainToDecibels(magnitude, floorDb))
            : floorDb;
    }
    return true;  // Column ready
}
```

### Startup Behavior

The first `samplesUntilFull = 2048` samples are discarded. `processHop` returns false until the sliding window is completely filled. This avoids rendering a smeared startup transient.

### Reset

```cpp
void reset()
{
    std::fill(slidingWindow.begin(), slidingWindow.end(), 0.0f);
    samplesUntilFull = fftSize;
}
```

Called when:
1. The analysis engine becomes active after an idle period (transport jump, pause → play)
2. Display is cleared manually

Clears the window and requires a full fill before output resumes.

## Magnitude Scaling

For a full-scale sine (1.0 amplitude) centered on a bin:

1. **Raw FFT magnitude:** 0.5 (due to window gain)
2. **Window scaling factor:** 2.0 / windowSum
3. **Final magnitude:** 0.5 × (2.0 / 1024) × 1024 = 1.0
4. **dBFS:** 20 × log10(1.0) = 0 dB ✓

Bins at DC and Nyquist use half the scaling because their energy doesn't split between positive/negative frequencies.

## dBFS Conversion

```cpp
const float floorDb = -100.0f;  // Minimum display level
magnitudesDb[bin] = gainToDecibels(magnitude, floorDb);
```

Formula: 20 × log₁₀(magnitude)  
Values below -100 dB are clamped to -100 dB (inaudible noise floor).

## Data Types

```cpp
std::vector<float> slidingWindow;      // 2048 samples of audio history
std::vector<float> fftData;            // 2 × 2048, interleaved Re/Im for JUCE FFT
std::unique_ptr<juce::dsp::FFT> fft;   // JUCE's FFT (Radix-2)
std::unique_ptr<juce::dsp::WindowingFunction<float>> window;  // Hann
```

All buffers are allocated once in `prepare`, never during processing.

## Public Interface

```cpp
void prepare(int fftOrder, int hopSize, double sampleRate);
bool processHop(const float* monoHop, float* magnitudesDb) noexcept;
void reset();

int getNumBins() const noexcept;       // 1025 for 2048-point
int getFftSize() const noexcept;       // 2048
float getBinFrequency(int bin) const;  // Center frequency of bin
int getBinForFrequency(double hz);     // Nearest bin
```

## Two Parallel Instances

AnalysisEngine owns:
1. `stft` — Processes mono sum (used by both spectrogram and spectrum analyser)
2. `sideStft` — Processes side channel (L-R)/2 (used by spectrum width trace)

Both run on the same hop, in sequence. Side is computed after mono so both are available before queuing.

## Performance

- **Memory:** ~32 KB per instance (sliding window + FFT buffers + tables)
- **CPU:** ~1-2% of real-time on modern CPUs (two FFTs per 5.3 ms hop)
- **Latency:** No additional latency; analysis is asynchronous to audio
