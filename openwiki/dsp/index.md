# Files

- [Analysis Engine](analysis-engine.md) - Background thread that owns hop processing, STFT, loudness metering, and publishes results to view consumers
- [BS.1770-4 Loudness Metering](loudness-metering.md) - Broadcast-standard K-weighted loudness measurement with true peak, integrated and short-term analysis, EBU range calculation
- [BS.1770-4 Loudness Meter](loudness.md) - LoudnessMeter implements ITU-R BS.1770-4 loudness measurement with K-weighting, gating, and true-peak detection.
- [Audio Processor (Zero-Latency Pass-Through)](processor.md) - PluginProcessor handles audio callbacks, manages the analysis engine lifecycle, and implements host-automatable triggers.
- [Lock-Free Queues (SPSC Data Synchronization)](queues.md) - LockFreeQueue and ColumnRing implement single-producer/single-consumer queues for UI thread draining.
- [Sample Ring Buffer](ring-buffer.md) - Lock-free single-producer/single-consumer circular buffer bridging audio and analysis threads
- [STFT Analysis](stft-analysis.md) - Short-time Fourier transform with 2048-point FFT, Hann window, 256-sample hops
- [Short-Time Fourier Transform (STFT Analyzer)](stft.md) - StftAnalyzer performs windowed FFT on hop-sized audio chunks, producing frequency-domain bins for spectrogram and spectrum display.
