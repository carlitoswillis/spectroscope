---
type: dsp
title: BS.1770-4 Loudness Metering
description: Broadcast-standard K-weighted loudness measurement with true peak, integrated and short-term analysis, EBU range calculation
tags: [dsp, loudness, metering, bs1770]
---

# BS.1770-4 Loudness Metering

The `LoudnessMeter` class implements the ITU BS.1770-4 loudness standard for broadcast compliance. It measures momentary (400 ms), short-term (3 s), and integrated (cumulative) loudness in LUFS, applies K-weighting and absolute/relative gates, and computes true peak using 4x oversampling.

## Configuration

**Header:** `Source/dsp/LoudnessMeter.h`  
**Implementation:** `Source/dsp/LoudnessMeter.cpp`

```cpp
class LoudnessMeter
```

### K-Weighting Filter

Two cascaded biquad filters per channel:

1. **Shelving Stage** (high-shelf, ~+4 dB above 1.5 kHz)
   - Gain: 3.999843853973347 dB
   - Center frequency: 1681.974450955533 Hz
   - Q: 0.7071752369554196

2. **RLB High-Pass** (Relative Loudness Balance filter)
   - Center frequency: 38.13547087602444 Hz
   - Q: 0.5003270373238773

Both filter coefficients are bilinear-transformed at the actual sample rate to remain accurate at any sample rate (not copied from a 48 kHz table). At 48 kHz they reproduce the standard tables exactly.

Filter design ensures a full-scale 1 kHz sine reads 0 LU before gating, matching the standard reference tone.

### Processing Cadence

Audio flows through 100 ms sub-blocks at 75% overlap:

```
400 ms momentary = 4 sub-blocks
3 s short-term = 30 sub-blocks
Integrated = all sub-blocks since reset (capped at 2 hours = 72000 blocks)
```

Sub-block length is `round(0.1 × sampleRate)` — exactly 4800 samples at 48 kHz.

Each sub-block:
1. Accumulates K-weighted squared samples from both channels
2. Computes mean square power
3. Stores in a ring history
4. Every 10 sub-blocks (1 second), applies gates and updates LUFS values

### Loudness Calculations

```cpp
momentaryLufs = 10 × log10(blockMeanSquarePower / reference) - K_offset
shortTermLufs = 10 × log10(average of last 30 blocks) - K_offset
integratedLufs = 10 × log10(average of gated blocks) - K_offset
```

K_offset = 0.691 dB (standard offset to make a 997 Hz calibration tone read its own power).

### Gating

Two-stage gate as per BS.1770-4:

1. **Absolute Gate:** -70 LUFS
   - Discard blocks below this threshold (silence)
2. **Relative Gate:** -10 LU below the short-term average
   - Further prune blocks that fall 10 LU below the running average
   - Avoids quiet passages inflating the integrated average

Integrated loudness is computed only from blocks passing both gates.

### True Peak Measurement

ITU 4x oversampling using a 48-tap windowed sinc polyphase interpolator:

```cpp
// 4 phases, 12 taps each, Hann-windowed
// DC-normalized so interpolation adds no level error
firPhases[phase][tap];  // Pre-computed at prepare()
```

For each sample, the true peak is estimated by interpolating between adjacent samples and tracking the maximum across all oversampling points. Useful for detecting clipping and for compliance headroom calculation.

### Loudness Range (EBU Tech 3342)

Calculated from short-term LUFS history:

```cpp
1. Collect all short-term values computed so far
2. Apply -20 LU relative gate (discard values > 20 LU below the short-term average)
3. Sort gated values
4. Return (95th percentile - 10th percentile) in LU
```

Range is not available until enough history has accumulated (at least 3 seconds of gated short-term values).

## Public Interface

```cpp
void prepare(double sampleRate);
void requestReset() noexcept;  // Consumed at next processHop()
void processHop(const float* left, const float* right, int numSamples) noexcept;

float getMomentaryLufs() const noexcept;   // -120.0 = no data
float getShortTermLufs() const noexcept;
float getIntegratedLufs() const noexcept;
float getLoudnessRange() const noexcept;   // 0.0 = not enough data
float getMaxTruePeakDb() const noexcept;
```

All getters use relaxed atomics, safe to read from any thread without locking.

### Atomic Updates

```cpp
std::atomic<float> momentaryLufs{-120.0f};
std::atomic<float> shortTermLufs{-120.0f};
std::atomic<float> integratedLufs{-120.0f};
std::atomic<float> loudnessRange{0.0f};
std::atomic<float> maxTruePeakDb{-120.0f};
```

Written by the analysis thread, read by UI views. No synchronization — stale reads are acceptable (the meter settles over seconds anyway).

## Data Structures

```cpp
struct Biquad {
    double b0, b1, b2, a1, a2;  // Coefficients
    double z1, z2;              // State (delay line)
};

Biquad shelf[2];     // K-shelf per channel
Biquad highPass[2];  // RLB high-pass per channel

// Sub-block ring
double subBlockPowers[shortTermSubBlocks] = {};  // 30 entries
int subBlockRingPos;
int completedSubBlocks;

// Gated history (capped at 72000 blocks ≈ 2 hours)
std::vector<double> integratedPowers;
std::vector<double> shortTermPowers;
std::vector<double> percentileScratch;
```

## Lifecycle

```cpp
loudnessMeter.prepare(sampleRate);  // Called once at plugin startup
// In AnalysisEngine::processPendingAudio()
loudnessMeter.processHop(left, right, hopSize);
// Repeats every ~5.3 ms
```

On reset (user clears displays or transport jumps):
```cpp
engine.resetLoudness();
// Consumed at next processPendingAudio()
loudnessMeter.requestReset();
// All histories and gates clear; atomics return to -120.0
```

## Compliance Targets

Common loudness targets (cycled via LoudnessHistoryView):

| Standard | Target | Range | Platform |
|----------|--------|-------|----------|
| EBU R128 | -23 LUFS | -2 LU | European broadcast |
| Spotify | -14 LUFS | N/A | Streaming |
| YouTube | -14 LUFS | N/A | Streaming |
| ATSC A/85 | -24 LKFS | -2 LU | US broadcast |
| Off | — | — | No target (compliance disabled) |

The history view compares momentary against the target and lights a verdict lamp (green = compliant, red = over).

## Performance

- **Memory:** ~1 MB (filter states, block histories, scratch buffers)
- **CPU:** ~0.5% per 5.3 ms hop (two channels, two filters, one true-peak pass)
- **Latency:** ~400 ms to first momentary value (one momentary block)
- **Settling:** Short-term settles in ~3 seconds, integrated over minutes
