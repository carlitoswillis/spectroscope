---
type: guide
title: Building and Packaging
description: CMake configuration, output formats (AU/VST3/Standalone), universal binaries, code signing
---

# Building and Packaging

Spectroscope is built with CMake 3.22+ and JUCE 8. The build produces three formats: an Audio Unit (AU), a VST3 plugin, and a standalone macOS app.

## Build System

**Source:** `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)
project(Spectroscope VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET \"11.0\")
  set(CMAKE_OSX_ARCHITECTURES \"arm64;x86_64\")  # Universal binary
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15)
```

## Output Formats

```cmake
juce_add_plugin(Spectroscope
  FORMATS AU VST3 Standalone
  PLUGIN_MANUFACTURER_CODE Cwil
  PLUGIN_CODE Spct
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE  # for standalone input device access
)
```

**AU:** CoreAudio plugin for macOS. Scanned by Ableton Live, Logic Pro, etc.

**VST3:** Standardized cross-platform plugin format (though only macOS is built currently).

**Standalone:** Native macOS app (`Spectroscope.app`). Reads from an audio input device, no host required.

## Universal Binary

Both arm64 (Apple Silicon) and x86_64 (Intel) are compiled into one binary:

```cmake
set(CMAKE_OSX_ARCHITECTURES \"arm64;x86_64\")
```

The same plugin runs natively on both architectures.

## Compilation

```bash
cmake -B build
cmake --build build
```

Optionally enable the preview renderer:

```bash
cmake -B build -DSPECTROSCOPE_BUILD_PREVIEW=ON
```

## Output Locations

After build, the plugin is copied to standard plugin locations:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Spectroscope.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Spectroscope.vst3`
- **Standalone:** `/Applications/Spectroscope.app`

## Code Signing & Notarization

Not currently automated in CMakeLists.txt. For distribution, the binaries must be:

1. Code-signed with an Apple Developer certificate
2. Notarized through Apple's notarization service (gatekeeper requirement)

```bash
codesign -s \"Developer ID Application\" Spectroscope.app
xcrun altool --notarize-app -f Spectroscope.app -t osx -u <developer_id> -p <password>
```

## Compile Flags

**Disabled:**
- `JUCE_WEB_BROWSER=0` — no embedded browser
- `JUCE_USE_CURL=0` — no network library
- `JUCE_REPORT_APP_USAGE=0` — no telemetry
- `JUCE_DISPLAY_SPLASH_SCREEN=0` — no JUCE splash on launch

**Enabled (tests only):**
- `JUCE_UNIT_TESTS=1` — enable unit test framework

## Linked Libraries

```cmake
target_link_libraries(Spectroscope
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp      # FFT
    juce::juce_opengl   # GPU spectrogram
)
```

## Deployment Target

macOS 11.0 (Big Sur) or later. Earlier versions are not supported.
