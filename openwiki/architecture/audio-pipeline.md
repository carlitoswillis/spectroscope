---
type: architecture
title: Audio Signal Pipeline
description: Complete audio path from plugin input through analysis and display
tags: [architecture, audio, dsp, flow]
---

# Audio Signal Pipeline

The audio pipeline flows from the host's audio buffer through zero-latency pass-through to the analysis thread, which produces results that feed six independent instrument views. At no point does the signal path block, allocate, or hold up the audio callback.

## 1. Audio Input → Ring Buffer

**Entry:** `PluginProcessor::processBlock` (audio thread)

```cpp
void SpectroscopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Optional alignment tone for testing (no effect on audio output)
    if (alignmentToneEnabled) {
        // Generate -18 dBFS 1 kHz sine into buffer, measure it
        analysisEngine.pushAudio(toneBuffer);
    } else {
        // Bit-for-bit pass-through; pushAudio only reads
        analysisEngine.pushAudio(buffer);
    }
    // Buffer proceeds unchanged to host output
}
```

**Non-blocking write to SampleRingBuffer:**
- Bounded capacity (0.25 seconds + headroom)
- If full, drops the block rather than waiting
- Handles mono-to-stereo duplication (mono source fills both stored channels)

Capacity is `max(8 × blockSize, sampleRate × 0.25)` so a 256-sample host block with a scheduling hiccup doesn't drop audio.

## 2. Ring Buffer → Analysis Thread

**Owned by:** `AnalysisEngine::run()` (analysis thread)

Polls every ~1 ms:

```cpp
while (!threadShouldExit()) {
    if (hasConsumers()) {
        if (!wasActive) {
            stft.reset();
            sideStft.reset();
            discardPendingAudio();
        }
        processPendingAudio();  // Pull hops, run DSP
    } else {
        discardPendingAudio();  // No view is consuming, skip FFT
    }
    wasActive = hasConsumers();
    wait(1);  // 1 ms poll
}
```

**Consumer-gated analysis:** If no view is consuming analysis output (all instruments off), the thread drains the ring buffer but skips STFT entirely. This is why switching views off saves CPU.

## 3. Per-Hop Processing

For each 256-sample hop available in the ring buffer:

### 3a. Mono Sum & Envelope
```
hopBuffer (up to 2 channels)
    ↓
sum to mono (equal-power average)
    ↓
EnvelopePoint: min, max, rms
    ↓
LockFreeQueue<EnvelopePoint>
    ↓
WaveformView
```

### 3b. Spectrum Analysis (Mono)
```
monoHop (256 samples)
    ↓
StftAnalyzer::processHop
    ├→ Slide 1792 historical samples left by 256
    ├→ Append new 256 samples to form 2048-sample window
    ├→ Apply Hann window
    ├→ FFT → 1025 magnitude bins
    └→ Scale to dBFS (0 dB = full-scale sine)
    ↓
ColumnRing (spectrum) → SpectrogramView (CPU image)
ColumnRing (analyser) → SpectrumView (live curve)
```

Bins are ~23 Hz wide at 48 kHz (2048-point FFT). Returns false until the window is full (the first hop is discarded).

### 3c. Spectrum Analysis (Side Channel)
```
side = (L - R) × 0.5
    ↓
StftAnalyzer (sideStft)::processHop
    ↓
ColumnRing (sideSpectrumColumns) → SpectrumView (width trace)
```

Side spectrum shows frequency-dependent stereo width.

### 3d. Loudness Metering
```
hopBuffer (L, R samples)
    ↓
LoudnessMeter::processHop
    ├→ K-weighting (K-filter stage 1 + RLB high-pass per channel)
    ├→ 100 ms sub-blocks at 75% overlap
    ├→ Momentary: one 400 ms block
    ├→ Short-term: 3 second window
    ├→ Integrated: gated history since reset
    ├→ True peak: 4x oversampled (48-tap polyphase interpolator)
    └→ Gates: absolute -70 LUFS, relative -10 LU
    ↓
atomics (relaxed):
    ├→ momentaryLufs
    ├→ shortTermLufs
    ├→ integratedLufs
    ├→ loudnessRange (EBU 3342, 10th-95th percentile)
    └→ maxTruePeakDb
    
plus LoudnessPoint (momentary) → LockFreeQueue → LoudnessHistoryView
```

### 3e. Per-Sample Stereo Data
```
for each sample in hopBuffer:
    StereoSample { left, right }
        ↓
    LockFreeQueue (stereoSamples) → StereoFieldView
    LockFreeQueue (scopeSamples) → OscilloscopeView
```

Both views are single-consumer; only one draws at a time. The analysis thread pushes to both queues, and whichever view is inactive drains its queue silently.

### 3f. Correlation & RMS
```
Per-hop Pearson r = Σ(L×R) / √(ΣL² × ΣR²)
    ↓
smoothed correlation (exponential averaging, ~200 ms settling)
    ↓
atomic: smoothedCorrelation → StereoFieldView needle
```

Left/right RMS and peak also go to atomics for needle meters and the PWR/SIG lamps.

## 4. Analysis → UI Thread

Views poll on a 60 Hz timer:

```cpp
void YourView::timerCallback() {
    int numNewPoints = 0;
    while (const auto numPopped = queue.pop(scratch.data(), maxPerFrame)) {
        // Process numPopped items
        numNewPoints += numPopped;
    }
    if (numNewPoints > 0) repaint();
}
```

Lock-free queues are sized so the UI can keep up:
- Envelope/Loudness: ~10 seconds of points (plenty of headroom)
- Spectrum columns: ~2 seconds (far more than any window displays)
- Stereo samples: ~1 second (old samples are stale for visual purpose)

A 60 Hz repaint pulling from a 1 ms analysis poll means worst-case queue depth is 16 items. Queues are sized in powers of two with headroom, so they never back up under normal operation.

## 5. Views Read Live Atomics

Loudness views also read LUFS atomics directly (not through queues):

```cpp
auto momentary = engine.getLoudnessMeter().getMomentaryLufs();
auto shortTerm = engine.getLoudnessMeter().getShortTermLufs();
```

This is safe because:
- Atomics use relaxed ordering (no synchronization overhead)
- A stale value is acceptable (the meter settles over 400 ms anyway)
- No lock, no wait, no allocation

## 6. Display State → Plugin State → Host

`PluginProcessor` holds atomic display state:
- Theme index
- Panes mask (which instruments are visible)
- Floating mask (which ones have windows)
- Window layout (bounds per floating pane)

`PluginEditor::storeWindowLayout()` serializes live window bounds back to the processor when windows move. On save, `getStateInformation` writes to XML; on load, `setStateInformation` restores the console exactly as it was.

## 7. Optional Test Signal

The alignment tone path allows measurement without disrupting playback:

1. **setAlignmentToneEnabled(true)** (from UI)
2. **processBlock** generates -18 dBFS 1 kHz sine into a pre-allocated buffer
3. **analysisEngine.pushAudio(toneBuffer)** feeds meters only
4. Audio output remains the input signal, untouched
5. Meters show what the tone is doing, not the music

This is for calibration and is orthogonal to the main signal path.
