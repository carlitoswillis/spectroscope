---
type: component
title: BS.1770-4 Loudness Meter
description: LoudnessMeter implements ITU-R BS.1770-4 loudness measurement with K-weighting, gating, and true-peak detection.
tags: [dsp, loudness, broadcast, compliance]
---

# BS.1770-4 Loudness Meter

The `LoudnessMeter` class implements ITU-R BS.1770-4 loudness metering, the broadcast standard used by streaming platforms (Spotify −14 LUFS, YouTube −14 LUFS, EBU −23 LUFS, ATSC −24 LUFS).

## Specification

**Location**: `Source/dsp/LoudnessMeter.h/cpp`

**Outputs** (published every hop, readable as relaxed atomics):
- **Momentary LUFS**: Loudness of the current 400 ms window (bouncy, responsive)
- **Short-term LUFS**: Average of the last 3 seconds of momentary blocks (phrase-level)
- **Integrated LUFS**: Gated average since reset (broadcast delivery loudness)
- **Loudness Range (LU)**: Spread between 10th and 95th percentiles of short-term values (dynamic range indicator)
- **Max True Peak (dBTP)**: ITU 4×-oversampled intersample peak (catches overshoots missed by sample-level peaks)

## K-Weighting Filters

BS.1770-4 specifies two cascaded biquad filters:

### Stage 1: Spherical-Head Shelf
Lifts frequencies above ~1.5 kHz by ~4 dB (approximates how ears perceive loudness—our ears are less sensitive to bass and treble).

**Analogue prototype coefficients** (published in BS.1770-4):
```
Gain: 3.999843853973347 dB
f0: 1681.974450955533 Hz
Q: 0.7071752369554196
```

**Digital realization** via bilinear transform at runtime:
```cpp
const auto k = tan(π * f0 / sampleRate);
const auto vh = pow(10, gainDb / 20);
const auto vb = pow(vh, 0.4996667741545416);
const auto a0 = 1 + k/q + k²;

stage.b0 = (vh + vb*k/q + k²) / a0;
stage.b1 = 2*(k² - vh) / a0;
stage.b2 = (vh - vb*k/q + k²) / a0;
stage.a1 = 2*(k² - 1) / a0;
stage.a2 = (1 - k/q + k²) / a0;
```

This ensures exact reproduction of the published 48 kHz filter at any sample rate.

### Stage 2: RLB High-Pass
Attenuates bass below ~40 Hz (irrelevant for loudness perception).

**Parameters**:
```
f0: 38.13547087602444 Hz
Q: 0.5003270373238773
```

Both stages are applied to left and right channels independently, then averaged to mono.

## Momentary (400 ms)

**Computation**:
1. Accumulate samples in a 400 ms block (19200 samples at 48 kHz)
2. Measure RMS power after K-weighting
3. Convert to LUFS: `lufs = 10 * log10(power) - 0.691 dB`

The −0.691 dB offset (the loudness offset) makes a 997 Hz reference tone read its own power in dB. This is published in the standard.

**Cadence**: One momentary value per analysis hop (256 samples = 5.3 ms at 48 kHz). The momentary value is pushed to `loudnessQueue` for the chart.

## Short-Term (3 Seconds)

**Computation**:
Average the last 30 momentary blocks (roughly 3 seconds at 48 kHz).

```cpp
shortTermLufs = 10 * log10(mean(momentary_powers))
```

Used by the CHART pane to show phrase-level loudness trends.

## Integrated (Program Loudness)

**Gating** (two-stage, per BS.1770-4):
1. **Absolute gate**: Discard momentary blocks below −70 LUFS (silence)
2. **Relative gate**: Discard blocks below (average − 10 LU) (soft gating to handle fades)

**Computation**:
```cpp
integrated = 10 * log10(mean(gated_momentary_powers))
```

**Lifetime**: Resets only via `requestReset()`. This is the number a streaming platform audits.

## Loudness Range (EBU Tech 3342)

**Computation**:
1. Gate short-term values at −20 LU relative to the mean
2. Sort the gated values
3. Return (95th percentile − 10th percentile)

Range of 4 LU is very dynamic (classical music); range of 1 LU is very compressed (club track).

## True Peak (ITU-R BS.1770-4)

**Problem**: Sample-level peak detection misses intersample peaks (peaks that occur between samples).

**Solution**: ITU 4×-oversampled linear interpolation via a 48-tap windowed-sinc polyphase filter:

```cpp
// Polyphase FIR: four 12-tap phases for 4x oversampling
const auto phase = (sampleIndex * 4) % 4;  // Which phase to use
const auto* taps = firPhases[phase];

// Interpolate by convolving taps with neighbouring samples
for (int k = 0; k < tapsPerPhase; ++k) {
    interpolatedSample += inputSample[index - k] * taps[k];
}
```

**Result**: Catches peaks 0–3 dB higher than sample-level measurements. True peak must stay at or below −1 dBTP for broadcast compliance.

## API

### `prepare(sampleRate)`
Allocates filter state, history buffers, and windowed-sinc taps:

```cpp
void prepare(double sampleRate);
```

### `processHop(left, right, numSamples)`
Analyzes one hop of stereo audio (analysis thread):

```cpp
void processHop(const float* left, const float* right, int numSamples) noexcept;
```

**Behavior**:
1. Apply K-weighting filters (left and right independently)
2. Sum to mono, measure power, update momentary
3. Update short-term and integrated (with gating)
4. Measure true peak via polyphase interpolation
5. Update all atomics

### `requestReset()`
Any thread can call this; the reset lands at the next `processHop()` call:

```cpp
void requestReset() noexcept;
```

**Effect**: Integrated loudness, loudness range, and true peak are zeroed. Momentary and short-term continue from the new audio.

### Getter atomics (any thread, read-only)
```cpp
float getMomentaryLufs() const;
float getShortTermLufs() const;
float getIntegratedLufs() const;
float getLoudnessRange() const;
float getMaxTruePeakDb() const;
```

All return `noDataLufs` (−120.0f) until enough audio has arrived (momentary blocks have accumulated).

## Validation (Test Card)

The alignment tone feature provides built-in calibration:

**Expected readings for −18 dBFS 1 kHz sine**:
- **Momentary**: −15.0 LUFS (within 0.1 LU)
- **True Peak**: −0.5 dBTP (approximately)

If readings drift, the meter is miscalibrated or the K-weighting filters have drifted.

**Reference**: DspTests.cpp includes loudness validation tests at multiple sample rates.

## Per-Sample Atomics (Meter Readout)

The CHART pane reads meter values directly on every paint (no queue):

```cpp
auto m = engine.getLoudnessMeter().getMomentaryLufs();  // Current momentary
auto i = engine.getLoudnessMeter().getIntegratedLufs();  // Integrated since reset
```

Since these are relaxed atomics, no lock is needed and UI updates are always seeing the most recent values.

## Real-Time Safety

- `processHop()` never allocates or locks
- Filter state is local to the analysis thread (no contention)
- All results published via relaxed atomics (wait-free reads)
- Deterministic runtime: filtering is O(1) per sample, true peak interpolation is O(1) per sample
