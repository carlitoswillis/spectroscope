# Seeing system audio through a UA Apollo

You want to visualise things that aren't in Ableton — Spotify, YouTube, a reference mix in a
browser. On most Macs that means installing a virtual audio driver like BlackHole and building a
Multi-Output device, which drifts and costs you hardware volume control.

With an Apollo you don't need any of that. Console has **Virtual channels**, and macOS can send
system audio straight to them. Nothing extra to install, nothing added to the latency.

> Not verified on hardware yet — this follows Universal Audio's own documentation. Corrections
> welcome once you've run through it. UA's article:
> [Routing macOS System Audio to Virtual Channels in Console](https://help.uaudio.com/hc/en-us/articles/211122563-Routing-macOS-System-Audio-to-Virtual-Channels-in-Console)

## One-time setup

1. Open **Audio MIDI Setup** (`/Applications/Utilities`).
2. Select your **Universal Audio Apollo** device, then the **Output** tab.
3. Click **Configure Speakers**.
4. Set the left and right outputs to the channel numbers of a **Virtual** pair rather than your
   monitor outputs. The exact numbers depend on your Apollo model — the Virtual channels sit above
   the physical outputs in the list.
5. In **System Settings → Sound**, set output to the Apollo.

Anything your Mac plays now arrives in Console on the Virtual input faders, labelled
**System Audio**.

Your monitors still work: keep monitoring through Console as usual, and the Virtual channel feeds
the meters and any DAW input that's listening.

## Using it in Ableton

1. In **Live → Settings → Audio**, make sure the Apollo's Virtual input channels are enabled under
   **Input Config**.
2. Create an audio track, set its **Audio From** to the Virtual channel pair, and arm monitoring
   to **In**.
3. Drop Spectroscope on that track.

## Using it in the standalone app

Launch `Spectroscope.app`, open the audio settings, choose the Apollo as the input device and the
Virtual pair as the input channels. That's the setup for watching audio with Ableton closed
entirely.

## If you ever need this without an Apollo

Two free fallbacks, in order of preference:

- **Core Audio process taps** (macOS 14.4+) — the native API, no virtual device, and it can tap a
  single application. Costs a permission prompt. This is the path we'd add to the standalone app
  if you ever work away from the Apollo.
- **[BlackHole](https://github.com/ExistentialAudio/BlackHole)** — free, open source virtual
  driver, paired with a Multi-Output device so you still hear what you're analysing.
