---
type: testing
title: DSP Unit Tests
description: Comprehensive test suite for ring buffer, lock-free queues, STFT, and loudness metering
tags: [testing, dsp, validation]
---

# DSP Unit Tests

The test suite covers the core DSP pipeline: lock-free data structures, audio buffering, FFT analysis, and loudness metering. All tests run headless (no audio device, no display) and validate critical invariants.

## Build & Run

**CMakeLists.txt:**
```cmake
enable_testing()
juce_add_console_app(SpectroscopeTests)
target_sources(SpectroscopeTests PRIVATE Tests/DspTests.cpp)
target_link_libraries(SpectroscopeTests PRIVATE juce::juce_audio_basics)
```

**Command:**
```bash
cmake --build build --target test
```

Or directly:
```bash
build/SpectroscopeTests
```

JUCE's built-in unit test framework is used. Each test class inherits from `juce::UnitTest`.

## Test Classes

### SampleRingBufferTests

**File:** `Tests/DspTests.cpp`

Validates the lock-free circular buffer:

#### "round-trips samples in order across a wrap"
- Write 40 blocks of 128 samples each (total 5120 samples)
- Ring capacity is 1024, forcing ~5 wraps
- Read in 128-sample chunks, verify each chunk matches write order
- **Invariant:** Samples survive wrap-around without corruption

#### "reports nothing readable until a full request is available"
- Write 100 samples to a ring expecting 256
- Read request for 256 returns 0
- Partial reads are not allowed
- **Invariant:** FIFO respects atomic all-or-nothing semantics

#### "drops blocks instead of blocking when the consumer stalls"
- Write 20 blocks of 256 samples into a 1024-sample buffer (total 5120)
- Buffer overflows immediately
- `getNumDroppedBlocks()` > 0 (dropped blocks counted)
- Remaining data still readable and intact (no torn samples)
- **Invariant:** Drops preserve data integrity; readable count ≤ capacity

#### "duplicates a mono source into both stored channels"
- Mono input (1 channel) → stereo store (2 channels)
- Both stored channels contain the mono data
- **Invariant:** Channel duplication is transparent

### LockFreeQueueTests

**File:** `Tests/DspTests.cpp`

Validates the generic SPSC queue template:

#### "round-trips items through the queue"
- Template-instantiated for `int` and `EnvelopePoint`
- Push 1000 items, pop them back
- Verify count and order
- **Invariant:** Queue preserves order and completeness

#### "drops items when the consumer stalls"
- Prepare queue for 100 items
- Push 200 items
- Verify `getNumReady() ≤ 100`
- Popped items are still intact
- **Invariant:** Full queue drops new items; existing items are uncorrupted

### StftAnalyzerTests

**File:** `Tests/DspTests.cpp`

Validates FFT analysis:

#### "magnitude-scales a full-scale sine to 0 dBFS"
- Generate 2048-point full-scale sine (amplitude 1.0)
- Hann-window, FFT, convert to dBFS
- Bin containing the sine reads 0.0 dBFS ±0.5 dB
- **Invariant:** Window scaling and magnitude calculation are correct

#### "returns false until the window is full"
- Initialize STFT (2048-point, 256-hop)
- First hop: `processHop()` returns false (window not full)
- After 8 hops (2048 samples): returns true
- **Invariant:** Startup latency is enforced; no partial output

#### "reset() clears the sliding window"
- Feed audio, verify output
- Call `reset()`
- Next output is all noise floor (-100 dB)
- **Invariant:** Reset discards state without leaking old audio

#### "maintains correct magnitude across sample-rate changes"
- Prepare STFT at 48 kHz with sine at 1 kHz
- Verify magnitude ≈ 0 dBFS
- Prepare at 96 kHz with 1 kHz sine
- Verify magnitude still ≈ 0 dBFS (magnitude scaling is rate-independent)
- **Invariant:** Frequency resolution changes but magnitude stays calibrated

### LoudnessMeterTests

**File:** `Tests/DspTests.cpp`

Validates BS.1770-4 metering:

#### "measures momentary LUFS of a calibration tone"
- Feed a -23 LUFS reference tone (1 kHz, 997 Hz pre-K-weight)
- Wait for momentary to settle (~400 ms)
- Momentary reads -23 LUFS ±0.5 LU
- **Invariant:** K-weighting and RMS calculation match the standard

#### "short-term settles in ~3 seconds"
- Feed constant -23 LUFS tone for 4 seconds
- Short-term should track toward -23 LUFS
- At 4 seconds, short-term is within 0.5 LU of -23
- **Invariant:** 3-second window averaging works

#### "integrated accumulates gated blocks correctly"
- Feed -20 LUFS material for 10 seconds
- Integrated approaches -20 LUFS
- Feed silence for 10 seconds
- Integrated stays ~-20 (silence gated out)
- Feed -20 LUFS again
- Integrated remains ~-20 (historical average dominates)
- **Invariant:** Gates remove silence from integration

#### "true peak peaks at 4x oversampling"
- Generate a signal that's near but not at full-scale samples (-0.5 dBFS)
- True peak should exceed peak sample value (4x interpolation finds overshoots)
- Measure true peak, verify it's higher than sample max
- **Invariant:** Polyphase interpolator detects inter-sample peaks

#### "loudness range is zero until enough history"
- Prepare meter, feed tone for 1 second
- `getLoudnessRange()` returns 0.0 (not enough history)
- Feed for 4 more seconds
- `getLoudnessRange()` > 0.0 (now enough percentile data)
- **Invariant:** Range gating avoids spurious values

#### "reset() clears all histories"
- Feed tone, let integrated settle
- `requestReset()`
- Process one hop
- Integrated returns to -120 (no data)
- Feed tone again, integrated builds from scratch
- **Invariant:** Reset is complete; no remnants leak through

## Helper Functions

```cpp
namespace {
    juce::AudioBuffer<float> makeBuffer(
        int numChannels, int numSamples,
        const std::function<float(int ch, int index)>& generator)
    {
        // Allocate buffer, fill via generator callback
        // Generator returns sample value for (channel, sample index)
    }
}
```

Used throughout to synthesize test signals (silence, DC, sine, noise, etc.).

## Test Coverage

- **Ring Buffer:** Wrap-around, drops, channel handling, partial-read rejection
- **Lock-Free Queue:** Order preservation, drop behavior, multiple data types
- **STFT:** Magnitude scaling, startup latency, reset, sample-rate independence
- **Loudness Meter:** Calibration accuracy, window averaging, gating, true peak, range calculation, reset

## Notes

- Tests are deterministic (no randomness)
- No audio devices needed
- No display needed (JUCE_HEADLESS_TESTS or equivalent)
- Run time: ~1-2 seconds total
- Safe to run frequently (CI/CD friendly)

## Known Gaps

- GUI rendering not tested (can't validate without display)
- Host-specific parameter automation edge cases (requires DAW)
- Floating-point precision (± 0.5 dB tolerance used)
- Very long sessions (>2 hours loudness history not tested)
