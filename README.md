# Spectroscope

A real-time waveform **and** spectrogram on one shared time axis — as an Audio Unit and VST3 for
Ableton, and as a standalone macOS app for everything else playing through your interface.

Built with [JUCE 8](https://juce.com). Audio passes through untouched and the plugin reports zero
latency, so hosts add nothing to the signal path.

## Status

| Phase | What it adds | State |
|---|---|---|
| 1 | CMake + JUCE, AU/VST3/Standalone targets, pass-through processor | **done** |
| 2 | Lock-free capture, waveform view | planned |
| 3 | STFT spectrogram (CPU) | planned |
| 4 | OpenGL ring-texture renderer | planned |
| 5 | Standalone device picker + Apollo routing | planned |
| 6 | Log frequency axis, colour maps, cursor readout | planned |

## Building

Requires CMake 3.22+, Ninja, and Xcode command line tools. JUCE is fetched automatically.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Artefacts land in `build/Spectroscope_artefacts/RelWithDebInfo/`, and `COPY_PLUGIN_AFTER_BUILD`
installs the AU and VST3 into `~/Library/Audio/Plug-Ins/` so Ableton picks them up on next scan.

### Validating

```bash
auval -v aufx Spct Cwil                    # Apple's Audio Unit validation
pluginval --strictness-level 8 build/Spectroscope_artefacts/RelWithDebInfo/VST3/Spectroscope.vst3
```

CI runs both on every push, plus a Linux compile check.

## Seeing audio from outside your DAW

If you have a Universal Audio Apollo, you already have everything you need — no virtual audio
driver required. See [docs/apollo-routing.md](docs/apollo-routing.md).

## Design notes

**Threading.** Three stages, two lock-free hops, nothing that allocates or locks on the audio
thread:

1. `processBlock` copies samples into a preallocated SPSC ring buffer and returns.
2. An analysis thread windows, runs the FFT, and pushes one spectrogram column plus one
   min/max/RMS envelope point into a second SPSC queue.
3. The GL thread uploads columns into a **ring texture** and draws a quad with a shifting UV
   offset, so scrolling costs nothing per frame.

That last part is the difference in smoothness — the usual approach redraws a `juce::Image` on the
CPU every frame.

**Display latency.** A 256-sample hop at 48 kHz is 5.33 ms per column; one audio block plus one hop
plus one frame lands under ~20 ms end to end at 120 Hz. Audio-path latency stays at zero.

## Licence

JUCE 8 is dual-licensed under AGPLv3 or the JUCE licence. Design ideas for the FFT and windowing
came from [JadeSpectrogram](https://github.com/JoergBitzer/JadeSpectrogram) (MIT, Jörg Bitzer /
Jade Hochschule).
