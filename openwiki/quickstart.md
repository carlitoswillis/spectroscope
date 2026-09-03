---
type: guide
title: Spectroscope Wiki Quick Start
description: Navigation guide and high-level overview of the audio analysis console architecture
---

# Spectroscope Wiki Quick Start

Spectroscope is a real-time audio analysis console built with JUCE 8, available as an AU/VST3 plugin and standalone macOS application. Audio passes through bit-for-bit untouched at zero latency. Six independent instruments show different aspects of the signal simultaneously.

## Main Sections

### [Architecture Overview](./architecture/overview.md)
System design, thread model, and data flow. Read this first to understand how the audio thread, analysis thread, and UI coordinate without blocking each other.

### [Audio Pipeline](./architecture/audio-pipeline.md)
End-to-end signal path from plugin audio input through hop-based analysis into the lock-free queues that feed each instrument view.

### DSP & Analysis
- [AnalysisEngine](./dsp/analysis-engine.md) — Background thread that owns all analysis, pulls hops from the ring buffer, publishes results
- [Ring Buffer](./dsp/ring-buffer.md) — Lock-free sample buffer bridging audio thread to analysis thread, drops blocks when consumer lags
- [STFT Analysis](./dsp/stft-analysis.md) — FFT configuration (2048 points, 256-sample hops, ~23 Hz bins), dBFS output
- [Loudness Metering](./dsp/loudness-metering.md) — BS.1770-4 momentary/short-term/integrated LUFS, K-weighting, true peak

### The Six Instruments
- [Waveform (WAVE)](./instruments/waveform.md) — Scrolling envelope, min/max/RMS per hop
- [Spectrogram (DENSITY)](./instruments/spectrogram.md) — Log-frequency waterfall, GPU-rendered in standalone
- [Spectrum (SPECTRUM)](./instruments/spectrum.md) — Instantaneous + peak-hold + SIDE width, stored curve, reference import
- [Stereo Field (FIELD)](./instruments/stereo-field.md) — Goniometer, VU meters, correlation meter, phosphor persistence
- [Loudness History (CHART)](./instruments/loudness-history.md) — Strip-chart compliance recorder, target cycling, markers
- [Oscilloscope (SCOPE)](./instruments/oscilloscope.md) — Triggered mono waveform, rising zero-crossing detection

### Plugin & UI
- [Processor](./plugin/processor.md) — Audio thread entry, parameter counting, alignment tone
- [Editor](./plugin/editor.md) — Pane composition, rail switches, theme switching
- [Themes](./plugin/themes.md) — Four liveries (AMBER, NOSTROMO, TVA, GRTA), palette system, title styles
- [Floating Windows](./plugin/floating-windows.md) — Window management, bounds persistence, OBS feed mode
- [State Persistence](./plugin/state.md) — XML serialization, display settings restoration

### Testing & Tools
- [DSP Tests](./testing/dsp-tests.md) — Unit test suite for ring buffer, STFT, loudness meter
<!-- openwiki: broken internal link [./tools/preview-renderer.md] file "./tools/preview-renderer.md" does not exist. Fix the href or restore the target, then delete this comment. -->
- [Preview Renderer](./tools/preview-renderer.md) — Headless PNG rendering tool for visual validation

## Task Routing

| Intent | Entry Point | Key Files | Tests |
|--------|-------------|-----------|-------|
| Add a new instrument view | [Editor](./plugin/editor.md), [AnalysisEngine](./dsp/analysis-engine.md) | Source/gui/YourView.h/cpp, Source/PluginEditor.cpp | Tests/DspTests.cpp |
| Adjust FFT resolution or hop size | [STFT Analysis](./dsp/stft-analysis.md) | Source/dsp/StftAnalyzer.h, Source/dsp/AnalysisEngine.cpp | Tests/DspTests.cpp (STFT tests) |
| Change loudness metering (gates, ballistics) | [Loudness Metering](./dsp/loudness-metering.md) | Source/dsp/LoudnessMeter.h/cpp | Tests/DspTests.cpp (Loudness tests) |
| Add a new livery or adjust colours | [Themes](./plugin/themes.md) | Source/gui/Theme.h, Source/gui/ColourMaps.h | None (visual) |
| Fix window bounds persistence | [State Persistence](./plugin/state.md) | Source/PluginEditor.cpp (storeWindowLayout) | None (manual) |
| Debug ring buffer drops or audio glitches | [Ring Buffer](./dsp/ring-buffer.md) | Source/dsp/SampleRingBuffer.h/cpp | Tests/DspTests.cpp (SampleRingBuffer tests) |
| Make a view respond to parameter automation | [Processor](./plugin/processor.md), [Editor](./plugin/editor.md) | Source/PluginProcessor.cpp (parameter counting), Source/PluginEditor.cpp (polling) | None (host-dependent) |

## Key Concepts

### Zero Latency
Audio passes straight through the processor to output on the audio thread; analysis happens asynchronously on a background thread. The processor reports zero latency to the host, so delay compensation never shifts around a Spectroscope instance.

### Lock-Free Data Flow
The audio thread writes to a bounded ring buffer that drops blocks if the analysis thread falls behind (preferable to stalling audio). Analysis outputs (envelope points, FFT columns, stereo samples, loudness values) flow through single-producer/single-consumer lock-free queues into the UI.

### Consumer-Gated Analysis
Views register as consumers with the AnalysisEngine. When no view cares about the FFT (e.g., both spectrogram and spectrum are off), the analysis thread still drains the ring buffer but skips the FFT entirely. This is why switching instruments off saves CPU.

### State Persistence
Display settings (theme, visible panes, which ones float, window bounds) ride the plugin state as XML. They survive editor close/reopen and session save/load. Audio parameters (the three momentary triggers) are edge-counted rather than stored, so reopening doesn't replay past actions.

### Phosphor Aesthetics
Every instrument is drawn as a phosphor trace on ruled paper, with scanline overlay and vignetting. The four themes use completely distinct hue shifts and title treatments — no palette is a tint of another. The spectrogram is the only instrument that uses GPU acceleration (GL_R8 ring texture with log-frequency remap in the fragment shader), and only in standalone; DAW hosts use the CPU fallback.

## Build & Test

Build the plugin (AU/VST3/Standalone):
```bash
cd /Users/carlitoswillis/workspace/spectroscope
cmake -B build
cmake --build build
```

Run the DSP test suite:
```bash
cmake --build build --target test
```

Render a headless preview (requires SPECTROSCOPE_BUILD_PREVIEW=ON):
```bash
cmake -DSPECTROSCOPE_BUILD_PREVIEW=ON -B build
cmake --build build
./build/RenderPreview
```
