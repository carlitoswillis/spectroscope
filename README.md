# Spectroscope

A six-instrument audio analysis console dressed as cassette-futurist broadcast hardware — an
Audio Unit and VST3 for Ableton, and a standalone macOS app for everything else playing through
your interface. Audio passes through bit-for-bit untouched at zero latency; the only thing this
device changes is how much you can see.

Built with [JUCE 8](https://juce.com). Four film-grade liveries, a GPU spectrogram, genuine
BS.1770 loudness metering, and every screen popping out into its own floating window.

This README is the manual. Read it top to bottom once and you'll know how to *read* the
instruments, not just look at them.

---

## The two ways to run it

**In Ableton (or any AU/VST3 host):** drop it on a track or the master like any plugin. It shows
whatever audio reaches that point in the chain and passes it on unchanged. Zero latency means Live
never re-shuffles delay compensation around it — park instances anywhere, add and remove them
mid-session, nothing shifts.

**Standalone (`/Applications/Spectroscope.app`):** not in any chain — it listens to an input
device. This is how you visualise things Live never touches: Spotify, YouTube, a reference mix in
a browser, your whole Mac.

---

## First light: getting audio into the standalone

The number-one support question in advance. The app can only show you what arrives at an input,
and macOS plays your music to an *output* — so something has to loop playback around to an input
the app can hear.

1. Launch the app. macOS asks for **microphone permission** the first time — that permission
   covers all audio inputs, not just microphones. Deny it and the app runs but draws silence,
   with no error. Grant it.
2. Open **Options → Audio/MIDI Settings**. Pick your interface as the input device, and — this is
   the step everyone misses — pick the **input channels that actually carry your music**.
3. Where's the music? Three cases:
   - **Universal Audio Apollo:** the driver already exposes the monitor mix as recordable inputs
     — look for **MON L + R** in the channel list. Select that pair and you're done: everything
     the Mac plays appears in the instruments, no extra routing, no drivers.
     [docs/apollo-routing.md](docs/apollo-routing.md) covers the Virtual-channel alternative.
   - **Other interfaces with loopback** (RME, MOTU, Focusrite …): enable loopback in the
     interface's control software, select those channels.
   - **No loopback hardware:** install [BlackHole](https://github.com/ExistentialAudio/BlackHole),
     make a Multi-Output device so you still hear your speakers, select BlackHole as the input.
4. Leave the app's **output channels disabled** (they ship disabled). The app is a pass-through:
   if it ever outputs into the same monitor mix it's recording, you've built a feedback loop.
   You're already hearing the music through your monitors — the app only needs to listen.
5. The **SIG** lamp in the header lights when signal arrives. If it stays dark: wrong input
   channels, or permission denied (check System Settings → Privacy & Security → Microphone).

On launch the standalone runs a brief **test card** — colour bars built from the current livery,
the unit designation, an alignment grid. Click to dismiss it early. It isn't just theatre: if the
card looks wrong, your display pipeline is wrong before audio ever enters the picture.

---

## The console

Under the header sits the **switch rail** — six latching channel switches:

```
WAVE   DENSITY   SPECTRUM   FIELD   CHART   SCOPE          STORE MARK PHOTO LOG TONE
```

Each switch lights one instrument. Light any combination; enabled panes stack vertically and
share left/right edges so the time-based instruments stay sample-aligned with each other. At
least one pane always stays lit — the console never goes dark. Your selection persists (per
plugin instance, and across standalone launches).

An instrument that's switched off costs nothing: its timer stops, its data feed drains and
drops, the analysis thread skips work nobody's watching.

The header, left to right: the wordmark and the unit's designation line, the **PWR / SIG / ERR**
lamps (power, signal present, dropped frames — ERR lighting means the UI fell behind and the gap
is honestly shown rather than smoothed over), then **FULL** (standalone fullscreen; Esc exits),
**CLR**, the **livery switch**, and the sample-rate readout.

---

## The six instruments, and how to read them

### WAVE — the waveform

The last few seconds of the signal as a scrolling envelope: the filled band spans each moment's
minimum-to-maximum, the hotter core inside it is RMS — where the signal actually *lives*. Peaks
tell you about transients; the RMS core tells you about loudness. A track that looks like a
solid slab has no dynamics left; tall thin spikes over a low core is a percussive, dynamic mix.
Vertical rules tick off seconds. Hover for a cursor readout: time-ago and level in dBFS.

### DENSITY — the spectrogram

Time scrolls left, frequency runs bottom-to-top on a **logarithmic axis** (each octave gets equal
space — the axis follows how you hear, with rules at 60 / 125 / 250 / 500 / 1K / 2K / 4K / 8K /
16K). Brightness is level. Learn to spot:

- **Horizontal lines** — sustained pitches. A bassline reads as a thick stripe with fainter
  copies above it: the harmonics.
- **Vertical strokes** — transients. Kick drums are short bright columns reaching surprisingly
  high.
- **The bright floor** — where your mix's energy actually sits. If everything below 250 Hz is a
  solid wall, your low end is crowded; the spectrogram shows the crowd.
- **The dark top** — a mix with nothing above 12 kHz looks "closed"; air and cymbals live up
  there.

Hover for frequency **as a note name with cents** and time-ago — "that resonance is F#1" is a
sentence you can act on in an EQ. In the standalone this raster renders on the GPU (a ring
texture; the log remap, gamma, and palette all happen in the fragment shader), which is why it
stays smooth at any size.

### SPECTRUM — the analyser

The right-now view: level against log frequency. Three traces —

- the **live averaged curve** (bright), smoothed just enough to read;
- the **peak-hold ghost** (dim), falling slowly — resonances and one-off spikes hang there long
  enough to measure. This is the precision trace: the live curve tells you the trend, the ghost
  tells you the worst case;
- the **SIDE trace** (secondary colour), the width-per-band reading. Where SIDE hugs the mid
  curve the material is wide; where it falls away it's mono. Bass that's wide down at 60 Hz is a
  vinyl-cutting and club-system problem — you'll see it here instantly.

Corner toggles: **NOTE** rules the octaves (every C, labelled) so spectral features map to
musical pitches; **TILT** draws −3 and −4.5 dB/oct reference slopes — balanced mixes tend to
follow roughly a pink-noise tilt, and these lines are the "is my mix tilted right" ruler.

**Drag a WAV or AIFF onto this pane** and it's analysed offline into a pinned reference curve
("REF" tag — click the tag to remove it). Mixing toward a reference record stops being memory
work: their long-term spectrum sits under your live one. The reference survives CLR on purpose.

**STORE** (rail switch or `S`) freezes the current curve as a dim stored trace. Freeze, tweak
the EQ, and compare live-versus-frozen on one screen. Press again to clear.

### FIELD — the stereo field

Three period instruments on one screen:

- **The goniometer** (centre): every sample plotted as a dot, mid/side rotated 45°, with phosphor
  persistence. The shapes to know: a **vertical blade** is mono; a **blooming cloud** is width;
  a **horizontal lean** is out-of-phase content — the shape that disappears in a mono club PA.
  L and R material lean along their labelled diagonals.
- **The VU needles** (right): per-channel level with proper coil-meter ballistics, rust zone
  above −6. Needles matching = balanced image; one persistently hotter = your mix leans.
- **The correlation meter** (bottom strip): +1 means mono-compatible, 0 means wide/uncorrelated,
  negative means phase trouble — the needle living left of centre is the single clearest "this
  will fold badly to mono" warning any instrument can give you.

STORE freezes the current cloud as a ghost exposure under the live one — compare stereo width
before and after a widener honestly.

### CHART — the compliance recorder

A strip-chart recorder of level over the last 60 seconds — and a real broadcast loudness meter.
The readout block, top right:

- **M** — momentary LUFS (400 ms): the "right now" number, bouncy by design.
- **S** — short-term (3 s): the phrase-level number.
- **I** — integrated: the whole-programme number, gated per BS.1770-4 — **this is the number
  streaming platforms judge**. It needs several seconds of audio before it reports.
- **LRA** — loudness range in LU: how dynamic the programme is overall.
- **TP** — maximum true peak in dBTP, 4×-oversampled, so intersample overs that a plain sample
  peak misses are caught.

Click the **target plate** to cycle delivery targets: EBU −23, Spotify −14, YouTube −14,
ATSC −24, or OFF. With a target set, a rule appears on the chart at the target level and the
verdict lamp judges you: **PASS** when integrated loudness is within ±1 LU of target *and* true
peak stays at or under −1 dBTP; **FAIL** otherwise; **WAIT** until there's enough audio to gate.

**MARK** (`M`) stamps a numbered pen tick that scrolls with the paper — mark the chorus, mark
the drop, and the chart becomes annotated evidence instead of a scrolling mystery.

The teacher's recipe for "is my master ready for Spotify": set target SPOTIFY −14, press CLR,
play the whole track top to tail without touching anything, read I and TP, believe the lamp.

### SCOPE — the oscilloscope

A triggered sweep of the actual waveform, ~21 ms per screen. The trigger hunts a rising zero
crossing and re-arms with hysteresis, so pitched material **stands still** — a sustained bass
note draws its true wave shape, stationary, the thing a scrolling waveform can never show you.
No confident trigger (silence, noise) and it free-runs; the corner readout says which
(`TRIG RISING` / `FREE RUN`). Read it for wave *shape*: soft sine-ish bass versus square-ish
saturated bass is instantly visible here and nowhere else.

---

## The field kit

Right end of the rail:

| Control | Key | What it does |
|---|---|---|
| **STORE** | `S` | Freeze/clear the spectrum + goniometer comparison traces |
| **MARK** | `M` | Drop a numbered marker on the chart recorder |
| **PHOTO** | `P` | Save the whole console to your Desktop as a PNG with a burned-in data strip — livery, date, integrated LUFS, max true peak. The screenshot you send the client. |
| **LOG** | — | Write a session folder to the Desktop: loudness history as CSV, a console snapshot, and a stats sheet — then reveal it in Finder. The deliverable-proof artefact. |
| **TONE** | — | Latching: feeds a −18 dBFS 1 kHz reference tone **to the meters only** — your monitors never hear it, the audio path is untouched. Flip it on and the chart should read −15.0 LUFS integrated: the built-in proof the meter is honest. |
| **CLR** | `⌘R` | Wipe every instrument's accumulated history — spectrogram scroll, goniometer cloud, peak holds, chart, markers, loudness gates. New song, dark glass. |

CLR, STORE and MARK are also **host-automatable parameters** — automate a display clear on the
downbeat of the drop, from Live.

---

## Liveries

The boxed **UNIT** switch in the header cycles four palettes, live, history intact:

| Unit | Name | The reference |
|---|---|---|
| A | **AMBER** | Canonical P3 amber phosphor on warm near-black — the default cassette-futurism terminal |
| B | **NOSTROMO** | MU-TH-UR's yellow-green phosphor, hollow outline wordmark, Semiotic-Standard signal red — the ship terminal |
| C | **TVA** | Aged-paper chassis over a walnut shadow line, burnt-orange placard wordmark, grey-green mint accents — bureaucratic hardware from an office outside time |
| D | **GRTA** | Ice blue-white in a slate cabinet, salmon alert lamp — the therapy mainframe |

Every colour, the wordmark treatment, the subtitle line, even the CRT scanline weight is palette
data — no livery is a tint of another. The choice persists with the session.

---

## Floating windows

Every docked pane's caption row ends in a small **pop-out glyph**. Click it and that instrument
lifts out of the console into its own **always-on-top floating window** — same chassis dressing,
native title bar (so the green button gives each window its own fullscreen). Close the window or
flip its rail switch and it docks back. Which panes float, and exactly where their windows sit,
persists with everything else.

Goniometer floating on one display, spectrogram fullscreen on another, the console on a third:
that's the intended endgame.

**FEED OUT:** inside any floating window, the corner glyph strips it to a chrome-less borderless
surface — no title bar, no bezel, just the instrument. Built for OBS window-capture: a
broadcast-grade meter as a stream overlay. Hover the top edge for the drag strip; the same glyph
brings the chassis back.

---

## Easter-egg hunting

Some music carries pictures. Artists paint images into the frequency domain (the tool of choice
is MetaSynth), and the DENSITY pane is exactly the instrument that reveals them:

- **Aphex Twin** — the grinning face in the *Windowlicker* B-side ("Formula") around 5:27; the
  spiral at the end of "Windowlicker" itself.
- **Venetian Snares — "Look"** — his cats, in the spectrogram.
- **Nine Inch Nails — *Year Zero*** — the reaching hand at the end of "My Violent Heart".
- **DOOM (2016) OST** — pentagrams, because of course.

And a whole genre exists for the FIELD pane: **oscilloscope music** (Jerobeam Fenderson's
*Oscilloscope Music*) is composed so that left-as-X, right-as-Y literally draws animations while
still functioning as music. Our goniometer is rotated 45° into the broadcast mid/side
orientation, so those pieces appear tilted — an XY mode is on the wishlist.

---

## Using it in Ableton

**Getting Live to find it:** Settings → Plug-Ins → enable **Use Audio Units** (and VST3 system
folders if you want both) → **Rescan**. If it doesn't appear, it's almost always the quarantine
flag (below). `auval -v aufx Spct Cwil` runs the same validation Live does.

**Where to put it** — it never changes the sound, so the only question is what you want to see:

| Put it here | And you see |
|---|---|
| **Master, last device** | Exactly what leaves Live |
| **Master, before the limiter** | How hard you're driving the limiter |
| **A track, last device** | That track's contribution, post-effects |
| **A track, before an EQ** | What you're about to carve |
| **A return** | Only what's actually sent there |

The two-instance trick: one either side of a processor, watch the difference. Zero latency means
they stay sample-aligned with no compensation offset to reason about. Each instance keeps its own
livery, pane selection, and window layout in the set.

A stopped transport draws a flat line. That's correct, not a bug.

---

## Installing

### Option A — download a build

1. Repo **Actions** tab → most recent green run → download the **`spectroscope-macos`** artefact.
2. Unzip GitHub's wrapper to get `spectroscope-macos.tar.gz` — **don't unpack the tarball by
   double-clicking**; the installer does it correctly.
3. Run the installer:

   ```bash
   ./scripts/install-macos.sh ~/Downloads/spectroscope-macos.tar.gz
   ```

It extracts the tarball, strips the quarantine flag, restores executable permissions, re-applies
an ad-hoc signature, installs all three bundles, and runs `auval` to confirm the AU is sound.
(The builds ship as a tarball because a plain zip drops the executable bit and macOS then claims
the app is "damaged" — same symptom as quarantine, different cause.)

### Option B — build it yourself

CMake 3.22+, Ninja, Xcode command line tools. JUCE is fetched automatically.

```bash
brew install cmake ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

`COPY_PLUGIN_AFTER_BUILD` installs the AU/VST3 into `~/Library/Audio/Plug-Ins/` for you; local
builds are ad-hoc signed, no quarantine step needed.

### Validating a build

```bash
ctest --test-dir build --output-on-failure  # headless DSP tests, incl. the BS.1770 suite
auval -v aufx Spct Cwil                     # Apple's Audio Unit validation
pluginval --strictness-level 8 build/Spectroscope_artefacts/RelWithDebInfo/VST3/Spectroscope.vst3
```

The DSP tests cover what can be verified without a display: the lock-free plumbing under stalled
consumers, STFT correctness (a bin-centred full-scale tone reads 0 dB in the exact bin), and the
loudness meter against reference levels (a −18 dBFS 997 Hz sine must read −15.0 LUFS integrated;
a full-scale sine ~0 dBTP — if those drift, the meter is lying and the tests fail).

### Previewing without a DAW

`Tools/RenderPreview.cpp` builds the real editor, drives synthetic stereo test audio through the
real processor, and paints the result to a PNG — no display server required.

```bash
cmake -B build -G Ninja -DSPECTROSCOPE_BUILD_PREVIEW=ON
cmake --build build --target RenderPreview
./build/RenderPreview_artefacts/*/RenderPreview preview.png 940 600 2 63   # theme 0-3, panes bitmask
```

---

## Performance

The audio thread does one bounded copy into a lock-free ring and returns — the plugin cannot add
latency, glitch audio, or cost the audio path anything, whatever the UI is doing. The analysis
thread (FFTs, loudness, correlation) is well under 1% of a core. The UI draws only the panes you
have lit; the standalone's spectrogram renders on the GPU. On an M1 Max, a three-pane console
runs around a quarter to a third of one core — single-digit percent of the machine.

## Design notes

**Threading.** Three stages, lock-free hops, nothing that allocates or locks on the audio thread.
`processBlock` copies into a preallocated SPSC ring and returns; if the analysis thread falls
behind, blocks drop and the count shows in the header, because a gap worth having is worth
seeing. The analysis thread windows, FFTs (mid and side), meters loudness, and fans results out
through **per-instrument SPSC queues** — which is why any combination of instruments can run at
once, docked or floating, without stealing from each other.

**The GL spectrogram.** Raw dB columns upload one texel column per hop into a ring texture; the
fragment shader does the log remap (bin interpolation falls out of linear filtering), the dB
normalisation, the gamma, and a 256×1 palette LUT — so a livery switch or a resize is a uniform
update and history survives. The context attaches to the spectrogram component alone, event
driven: whole-editor GL compositing is a measured loss on modern macOS, so everything else stays
on CoreGraphics, and DAW/headless builds keep the identical CPU raster as the fallback.

**Why min/max rather than decimation.** Each waveform pixel carries the extremes of the hop it
covers, so a single-sample transient shows at any zoom instead of falling between sample points.

**Phases.** The roadmap that got here: 1 skeleton → 2 lock-free capture + waveform → 3 STFT
spectrogram → 4 GPU ring texture → 5 input routing (the JUCE device dialog plus
[docs/apollo-routing.md](docs/apollo-routing.md)) → 6 log axis, liveries, cursors → and beyond
the original plan: the instrument console, floating windows, and the measurement expansion.

## Licence

JUCE 8 is dual-licensed under AGPLv3 or the JUCE licence. Design ideas for the FFT and windowing
came from [JadeSpectrogram](https://github.com/JoergBitzer/JadeSpectrogram) (MIT, Jörg Bitzer /
Jade Hochschule). The liveries are homages; all trademarks belong to their studios.
