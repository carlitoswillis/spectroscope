---
type: plugin
title: Plugin Editor (Console)
description: Six-instrument console with switch rail, pane composition, floating windows, theme switching
tags: [plugin, ui, editor, console]
---

# Plugin Editor

The `SpectroscopeAudioProcessorEditor` class is the UI console — a six-instrument pane stack with a switch rail for toggling visibility, floating window support, theme cycling, and utility actions (clear, store, marker, photo, log, tone). Display state persists in the processor.

## Console Layout

**Header:** `Source/PluginEditor.h`  
**Implementation:** `Source/PluginEditor.cpp`

```
Header (38px)
  Wordmark | PWR/SIG/ERR lamps | FULL | CLR | Theme | Sample Rate

Switch Rail (18px)
  WAVE | DENSITY | SPECTRUM | FIELD | CHART | SCOPE | STORE | MARK | PHOTO | LOG | TONE

Screen Area (variable, grows to fill)
  Pane Stack (vertical, dynamic layout)
    - Waveform (weight 0.62)
    - Spectrogram (weight 1.0)
    - Spectrum (weight 1.0)
    - Stereo Field (weight 1.0)
    - Loudness History (weight 0.75)
    - Oscilloscope (weight 0.75)

All panes have pop-out glyphs to float into separate InstrumentWindows

CRT Overlay (scanlines, vignette, bezel)

Boot Card (standalone only, dismisses after ~2 seconds)
```

## Pane Composition

```cpp
class SpectroscopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
    // Six instrument views, always constructed
    WaveformView waveformView;
    SpectrogramView spectrogramView;
    SpectrumView spectrumView;
    StereoFieldView stereoFieldView;
    LoudnessHistoryView loudnessView;
    OscilloscopeView oscilloscopeView;
    
    // Floating windows (created/destroyed as needed)
    std::array<std::unique_ptr<InstrumentWindow>, 6> floatingWindows;
    
    CrtOverlay crtOverlay;  // Glass and bezel
    std::unique_ptr<BootCard> bootCard;  // Standalone only
};
```

All six views are always constructed and attached to the editor. Visibility is controlled by the panes mask and consumer gating (views still pull from analysis even when invisible, but that's harmless).

## Switch Rail

Latching switches for each instrument:

```cpp
void applyPanes(int mask)
{
    // Show/hide views based on mask
    waveformView.setVisible((mask & 0b000001) != 0);
    spectrogramView.setVisible((mask & 0b000010) != 0);
    // ...
}
```

At least one pane is always visible (mask enforced to be non-zero). Enabled panes stack vertically and share edges. Relative heights are weighted:

```cpp
const float paneWeights[] = { 0.62f, 1.0f, 1.0f, 1.0f, 0.75f, 0.75f };
```

Layout is computed in `resized()`: each enabled pane gets `weight / totalWeight × availableHeight`.

## Floating Windows

```cpp
void floatPane(int index)
{
    // Create InstrumentWindow, move view into it
    // Editor still owns the view; window borrows it
    floatingWindows[index] = std::make_unique<InstrumentWindow>(...);
}

void dockPane(int index)
{
    // Close window, move view back to console stack
    floatingWindows[index].reset();
}
```

Full details in [[floating-windows]].

## Theme Cycling

```cpp
void cycleTheme()
{
    auto index = processor.getThemeIndex();
    index = (index + 1) % 4;
    processor.setThemeIndex(index);
    
    Theme::setCurrent(index);
    spectrogramView.themeChanged();  // Rebuild colour table
    // All views repaint with new palette
}
```

Theme switch is a repaint-only operation except for the spectrogram, which rebuilds its colour table.

## Utility Actions

### CLR (Clear Displays)
```cpp
void clearAllDisplays()
{
    waveformView.clear();
    spectrogramView.clear();
    spectrumView.clear();
    stereoFieldView.clear();
    loudnessView.clear();
    oscilloscopeView.clear();
}
```

Also triggered by Cmd+R (host-dependent; some hosts consume this).

### STORE (Toggle Stored Traces)
Latches/unlatches the current spectrum and stereo field as comparison overlays.

### MARK (Add Marker)
Stamps a numbered pen tick on the loudness chart at the current scroll position.

### PHOTO (Capture to Desktop)
Renders the console as a PNG with a data strip along the bottom (theme name, current readings, timestamp).

### LOG (Write Session Report)
Writes a folder to Desktop containing CSV (loudness history), PNG (console snapshot), and TXT (headline figures).

### TONE (Alignment Tone)
Toggles the processor's alignment tone, allowing meters to measure a generated -18 dBFS 1 kHz sine instead of the input.

## Timer

```cpp
void timerCallback() override
{
    // ~12 Hz timer
    // Poll trigger parameters for rising edges
    int clears = processor.getClearEvents();
    if (clears != lastClearEvents) {
        clearAllDisplays();
        lastClearEvents = clears;
    }
    
    // Read sample rate from processor
    // Check for window bounds changes from floating panes
    storeWindowLayout();
}
```

Polls at 12 Hz (83 ms) to catch parameter pulses that may last only a few blocks. Diffs the counters to detect edges.

## State Restoration

```cpp
SpectroscopeAudioProcessorEditor::SpectroscopeAudioProcessorEditor(...)
{
    Theme::setCurrent(processor.getThemeIndex());
    applyPanes(processor.getPanesMask());
    
    // Restore floating windows in saved order
    const auto savedFloating = processor.getFloatingMask();
    const auto savedLayout = processor.getWindowLayout();
    
    for (int i = 0; i < numPanes; ++i) {
        if ((savedFloating & (1 << i)) != 0) {
            processor.setWindowLayout(savedLayout);
            floatPane(i);
        }
    }
    
    // Trigger counters: don't replay past actions
    lastClearEvents = processor.getClearEvents();
    lastStoreEvents = processor.getStoreEvents();
    lastMarkerEvents = processor.getMarkerEvents();
}
```

## Boot Card

Standalone only, displayed on first frame:
- Color bars (built from current livery)
- Unit designation and alignment grid
- Dismissible by click or ~2 second timer
- Validates display pipeline before audio arrives

## Resizing

```cpp
setResizable(true, true);
setResizeLimits(520, 360, 4096, 2400);
setSize(940, 600);
```

Minimum 520×360 (readable but tight). Default 940×600 (good for desktop plugin windows).

## Performance

- **Memory:** ~2 MB (six views, floating window pointers, CRT overlay)
- **CPU (12 Hz timer):** <1 ms (parameter polling, layout checks, repaints handled by view timers)
- **Display:** 60 Hz (view timers), but editor timer runs at 12 Hz for low-cost polling
