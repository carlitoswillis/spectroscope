# Spectroscope

A real-time waveform **and** spectrogram on one shared time axis — as an Audio Unit and VST3 for
Ableton, and as a standalone macOS app for everything else playing through your interface.

Built with [JUCE 8](https://juce.com). Audio passes through untouched and the plugin reports zero
latency, so hosts add nothing to the signal path.

## What you get today

The waveform view is live. **The spectrogram pane is still a placeholder** until Phase 3 — you'll
see a labelled empty panel where it will go.

| Phase | What it adds | State |
|---|---|---|
| 1 | CMake + JUCE, AU/VST3/Standalone targets, pass-through processor | **done** |
| 2 | Lock-free capture, waveform view | **done** |
| 3 | STFT spectrogram (CPU) | planned |
| 4 | OpenGL ring-texture renderer | planned |
| 5 | Standalone device picker + Apollo routing | planned |
| 6 | Log frequency axis, colour maps, cursor readout | planned |

---

## Installing

### Option A — download a build (no toolchain needed)

1. Go to the repo's [**Actions** tab](../../actions), open the most recent green run on your
   branch, and download the **`spectroscope-macos`** artefact.
2. Unzip it. You get three things: `Spectroscope.component` (AU), `Spectroscope.vst3`, and
   `Spectroscope.app`.
3. Move them into place:

   ```bash
   mv Spectroscope.component ~/Library/Audio/Plug-Ins/Components/
   mv Spectroscope.vst3      ~/Library/Audio/Plug-Ins/VST3/
   mv Spectroscope.app       /Applications/
   ```

4. **Clear the quarantine flag.** These builds aren't signed or notarised, so macOS will refuse to
   load them straight out of a downloaded zip — the AU will silently fail to appear in Live, and
   the app will claim it's damaged. This is the step people miss:

   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Spectroscope.component
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Spectroscope.vst3
   xattr -dr com.apple.quarantine /Applications/Spectroscope.app
   ```

The builds are universal (arm64 + x86_64), so they load whether Live is running natively on Apple
Silicon or under Rosetta.

### Option B — build it yourself

Requires CMake 3.22+, Ninja, and the Xcode command line tools. JUCE is fetched automatically, so
the first configure takes a minute.

```bash
brew install cmake ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

`COPY_PLUGIN_AFTER_BUILD` installs the AU and VST3 into `~/Library/Audio/Plug-Ins/` for you, and a
local build is ad-hoc signed — no quarantine step needed.

---

## Using it in Ableton

### Getting Live to find it

1. **Settings → Plug-Ins**.
2. Turn on **Use Audio Units**, and **Use VST3 Plug-In System Folders** if you want the VST3 too.
   Either format works; the AU is the more conventional choice on macOS.
3. Hit **Rescan**. Spectroscope appears under **Plug-Ins** in the browser.

If it doesn't show up, it's almost always the quarantine flag above. To confirm the AU itself is
sound, run `auval -v aufx Spct Cwil` — that's the same check Live relies on.

### Where to put it in the chain

It's an analyser: it shows you whatever reaches it and passes that audio on untouched. **Its
position never changes your sound — only what you're looking at.** So the question is just "what do
I want to see?"

| Put it here | And you see |
|---|---|
| **Master, last device** | Exactly what leaves Live — the mix after everything |
| **Master, just before your limiter** | What the limiter is being fed, i.e. how hard you're asking it to work |
| **A track, last device** | That track's contribution, post-effects |
| **A track, before an EQ or compressor** | What you're about to carve or squash |
| **A return track** | Only what's actually being sent to that return |

A useful trick: drop **two instances**, one either side of a processor, and watch the difference.
Because it adds zero latency, the two stay sample-aligned with each other — there's no compensation
offset to reason about.

Two things that follow from the zero-latency design:

- Inserting or removing it **never shifts the timing** of anything else, and never makes Live
  re-shuffle delay compensation. You can add and remove instances mid-session freely.
- You can leave instances parked wherever you like without a latency cost.

What you'll see is whatever audio Live is actually pushing through that point — a stopped transport
or a silent track draws a flat line, which is correct, not a bug.

---

## Using the standalone app

The app isn't in any chain — it taps an **input device** directly, which is how you visualise
things Live never touches: Spotify, YouTube, a reference mix in a browser.

1. Launch `/Applications/Spectroscope.app`.
2. macOS will ask for microphone permission the first time. Grant it — that's the permission that
   also covers audio inputs. Deny it and the app runs but shows silence.
3. Pick your input device and channels in the audio settings.

To feed it your Mac's system audio rather than a microphone, see
**[docs/apollo-routing.md](docs/apollo-routing.md)** — if you have a Universal Audio Apollo, its
Console Virtual channels do this for free, with no virtual audio driver and nothing added to the
latency.

---

## Known limitations

- The spectrogram pane is a placeholder until Phase 3.
- Each loaded instance runs its analysis thread whenever the plugin is instantiated, not only when
  its window is open. Negligible for a handful of instances; worth knowing if you park dozens.
- Builds are unsigned and un-notarised, hence the quarantine step.

---

## Validating a build

```bash
ctest --test-dir build --output-on-failure  # headless DSP tests
auval -v aufx Spct Cwil                     # Apple's Audio Unit validation
pluginval --strictness-level 8 build/Spectroscope_artefacts/RelWithDebInfo/VST3/Spectroscope.vst3
```

CI runs all three on every push, plus a Linux compile check.

The DSP tests cover the parts that can be verified without a display or an audio device: the ring
buffer's ordering across wraps and its drop-rather-than-block behaviour, the queue's ordering and
fullness handling, and the analysis chain end to end — a known 1 kHz sine in, correct peak and RMS
out.

## Design notes

**Threading.** Three stages, two lock-free hops, nothing that allocates or locks on the audio
thread:

1. `processBlock` copies samples into a preallocated SPSC ring buffer and returns. If the analysis
   thread has fallen behind, the block is dropped rather than waited on — and the drop count shows
   in the header, because a gap in the picture is worth seeing.
2. An analysis thread windows, runs the FFT, and pushes one spectrogram column plus one
   min/max/RMS envelope point into a second SPSC queue.
3. The GL thread uploads columns into a **ring texture** and draws a quad with a shifting UV
   offset, so scrolling costs nothing per frame.

That last part is the difference in smoothness — the usual approach redraws a `juce::Image` on the
CPU every frame.

**Why min/max rather than decimation.** Each pixel column carries the extremes of the hop it
covers, so a single-sample transient still shows up at any zoom level instead of falling between
sample points.

**Display latency.** A 256-sample hop at 48 kHz is 5.33 ms per column; one audio block plus one hop
plus one frame lands under ~20 ms end to end at 120 Hz. Audio-path latency stays at zero.

## Licence

JUCE 8 is dual-licensed under AGPLv3 or the JUCE licence. Design ideas for the FFT and windowing
came from [JadeSpectrogram](https://github.com/JoergBitzer/JadeSpectrogram) (MIT, Jörg Bitzer /
Jade Hochschule).
