---
type: architecture
title: Spectroscope System Architecture
description: Overall system design, thread model, and real-time audio flow through six independent analysis instruments
tags: [architecture, audio, threading, analysis]
---

# System Architecture

Spectroscope is a real-time audio analysis console with zero-latency pass-through. The core design separates the audio thread (which never blocks or allocates) from an analysis thread that processes hops independently, and a UI thread that renders results asynchronously.

## Thread Model

Three threads work in concert:

- **Audio Thread** — The plugin's `processBlock` callback. Copies incoming audio into a bounded ring buffer, reports zero latency to the host. Non-blocking, non-allocating. If the analysis thread falls behind, blocks are dropped.
- **Analysis Thread** — Owned by `AnalysisEngine`. Polls the ring buffer every ~1 ms, pulls complete hops, runs FFT and loudness metering, pushes results into per-consumer lock-free queues.
- **UI Thread** — JUCE's message thread. Pulls analysis results on a 60 Hz timer, repaints instruments, handles user interactions (pane toggling, theme switching, floating windows).

## Data Flow

```
Audio Input
    ↓
PluginProcessor::processBlock
    ├→ [optional alignment tone generation]
    └→ SampleRingBuffer::write (audio thread, non-blocking, drops if full)
         ↓
    AnalysisEngine::run (analysis thread)
         ├→ SampleRingBuffer::read (one hop = 256 samples at a time)
         │
         ├→ For each hop:
         │   ├→ StftAnalyzer::processHop (mono sum)
         │   │   └→ ColumnRing (spectrum) + ColumnRing (analyser)
         │   │
         │   ├→ StftAnalyzer::processHop (side channel)
         │   │   └→ ColumnRing (side spectrum)
         │   │
         │   ├→ LoudnessMeter::processHop
         │   │   └→ atomics: momentary, short-term, integrated LUFS
         │   │
         │   ├→ EnvelopePoint (min/max/rms)
         │   │   └→ LockFreeQueue
         │   │
         │   ├→ LoudnessPoint
         │   │   └→ LockFreeQueue
         │   │
         │   ├→ Per-sample stereo points
         │   │   ├→ LockFreeQueue (stereo field)
         │   │   └→ LockFreeQueue (oscilloscope)
         │   │
         │   └→ Correlation coefficient (smoothed)
         │
         └→ atomics: L/R RMS, L/R peak, correlation
              ↓
        UI Timer (60 Hz)
              ├→ WaveformView pulls EnvelopeQueue
              ├→ SpectrogramView pulls SpectrumColumns (CPU image or GPU texture)
              ├→ SpectrumView pulls AnalyserColumns + SideColumns
              ├→ StereoFieldView pulls StereoSamples
              ├→ LoudnessHistoryView pulls LoudnessQueue + reads LUFS atomics
              ├→ OscilloscopeView pulls ScopeSamples + detects trigger
              └→ All views repaint
                    ↓
                PluginEditor
                    └→ Host/Standalone display
```

## Consumer-Gated Analysis

Views register as consumers with `AnalysisEngine::addConsumer` on construction. The analysis thread checks `hasConsumers()` each poll:

- **With consumers:** Process hops normally — STFT, loudness, queues.
- **Without consumers:** Drain ring buffer but skip STFT entirely. This keeps the audio thread from seeing a full buffer while saving CPU.

A consumer count, not individual flags, means a single atomic tracks visibility. When both Spectrum and Spectrogram are off, the STFT work stops entirely.

## Lock-Free Guarantees

All cross-thread data uses single-producer/single-consumer structures:

- **SampleRingBuffer** — Audio → Analysis. Unbounded capacity, drops blocks if consumer lags.
- **ColumnRing** — Two instances (spectrum, analyser). Analysis → UI. FFT-size chunks in a ring.
- **LockFreeQueue<T>** — Generic SPSC. Envelope points, loudness points, stereo samples, etc.

No locks, no allocations after prepare, no blocking. The UI poll rate (60 Hz) is slow enough to keep queues drained; analysis thread poll rate (1000 Hz) is fast enough that a UI stall doesn't cause audio drops.

## State Persistence

Display state (theme, visible panes, floating mask, window bounds) rides `PluginProcessor` as atomics and a lock-protected String, serialized by `getStateInformation`/`setStateInformation` to XML. Audio parameters (trigger counters) are not stored — they are edge-counted so reopening the editor never replays past actions.

## Liveries and Themes

`Theme` namespace holds four complete palettes (AMBER, NOSTROMO, TVA, GRTA). Each specifies:
- Chassis colors (shell, bezel, bezels)
- Screen colors (black, grid, grid bright)
- Phosphor trace colors (amber, bright, dim, secondary)
- Print colors (bone, dim)
- Spectrogram color table (built at theme selection)
- Title style (filled, outline, plate) and subtitle text
- Scanline and vignette intensity

Theme switching is a repaint-only operation for most views; the spectrogram rebuilds its color table.
