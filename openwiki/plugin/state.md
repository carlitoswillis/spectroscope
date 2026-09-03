---
type: plugin
title: State Persistence
description: XML serialization of display settings, theme, pane visibility, floating windows, and bounds
tags: [plugin, state, persistence]
---

# State Persistence

Spectroscope persists all display state through the plugin's standard state save/restore mechanism. Audio parameters (the three trigger counters) are not stored — reopening the editor never replays past actions.

## What Persists

**Display settings** (survive editor close/reopen and session save/load):
- Theme index (0-3)
- Panes mask (which instruments are visible)
- Floating mask (which panes have windows)
- Window layout (saved bounds per floating pane)

**What does NOT persist:**
- Trigger parameter values (edge-counted, not stored)
- History (waveform, spectrogram, loudness chart, etc.)
- Stored traces (A/B snapshot overlays)
- Markers on the loudness chart

## XML Format

```cpp
void SpectroscopeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement state("SpectroscopeState");
    state.setAttribute("theme", getThemeIndex());
    state.setAttribute("panes", getPanesMask());
    state.setAttribute("floating", getFloatingMask());
    state.setAttribute("windows", getWindowLayout());
    
    copyXmlToBinary(state, destData);
}
```

Result (text view):
```xml
<SpectroscopeState theme="2" panes="31" floating="4" windows="2:100,200,800,400;"/>
```

### Attributes

- **theme:** 0=AMBER, 1=NOSTROMO, 2=TVA, 3=GRTA
- **panes:** Bitmask (6 bits, one per instrument)
  - Bit 0: Waveform
  - Bit 1: Spectrogram
  - Bit 2: Spectrum
  - Bit 3: Stereo Field
  - Bit 4: Loudness History
  - Bit 5: Oscilloscope
  - Example: `31 = 0b011111` (all visible except Oscilloscope)
- **floating:** Bitmask (same bit order as panes)
  - Example: `4 = 0b000100` (Spectrum has a floating window)
- **windows:** Serialized bounds, format `"index:x,y,w,h;index:x,y,w,h;..."`
  - Example: `"2:100,200,800,400;"` (Spectrum window at 100,200 sized 800×400)

## Restoration

```cpp
void SpectroscopeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto state = getXmlFromBinary(data, sizeInBytes)) {
        if (state->hasTagName("SpectroscopeState")) {
            setThemeIndex(state->getIntAttribute("theme", 0));
            setFloatingMask(state->getIntAttribute("floating", 0));
            setWindowLayout(state->getStringAttribute("windows", {}));
            
            // Panes attribute (or legacy fallback)
            if (state->hasAttribute("panes")) {
                setPanesMask(state->getIntAttribute("panes", 0b000011));
            }
            // ... handle older formats
        }
    }
}
```

## Backward Compatibility

Older sessions use different attribute names and formats:

### Pre-multi-pane ("view" attribute)
```xml
<SpectroscopeState spectrum="true" view="2"/>
```

Upgraded to:
- Panes mask: bit 0 (waveform) + bit (view+1)
- Example: `view=2` → mask = 0b000110 (waveform + spectrum)

### Even older ("spectrum" boolean)
```xml
<SpectroscopeState spectrum="true"/>
```

Upgraded to:
- Panes mask: 0b000101 (waveform + spectrum) if spectrum==true
- Panes mask: 0b000011 (waveform + spectrogram) if spectrum==false

## Atomics & Thread Safety

Display state is stored in the processor as relaxed atomics and a lock-protected String:

```cpp
std::atomic<int> themeIndex;
std::atomic<int> panesMask;
std::atomic<int> floatingMask;

juce::CriticalSection windowLayoutLock;
juce::String windowLayout;
```

Write from the message thread (editor). Read by audio thread or display thread. Relaxed atomics mean no synchronization — a stale read is acceptable (display state changes are rare).

## Lifecycle

1. **Plugin Construction:** Atomics initialized to defaults (theme 0, all panes visible, no floating windows).
2. **Editor Construction:** Reads processor state, applies theme, sets up panes, restores floating windows from saved layout.
3. **User Interaction:** Editor updates processor state atomically (theme change, pane toggle, window move).
4. **Session Save:** Host calls `getStateInformation`, processor serializes current state to XML.
5. **Session Load:** Host calls `setStateInformation`, processor atomics updated, editor constructor applies restored state.
6. **Editor Close/Reopen:** State survives in processor; editor re-reads and reapplies on construction.

## Editor Initialization

```cpp
SpectroscopeAudioProcessorEditor::SpectroscopeAudioProcessorEditor(...)
{
    // Restore theme from processor
    Theme::setCurrent(processor.getThemeIndex());
    spectrogramView.themeChanged();  // Rebuild colour table
    
    // Restore pane visibility
    applyPanes(processor.getPanesMask());
    
    // Restore floating windows
    const auto savedFloating = processor.getFloatingMask();
    const auto savedLayout = processor.getWindowLayout();
    
    for (int i = 0; i < numPanes; ++i) {
        if ((savedFloating & (1 << i)) != 0) {
            processor.setWindowLayout(savedLayout);  // Ensure layout is set before creating window
            floatPane(i);
        }
    }
    
    // Trigger counts: don't replay actions fired while editor was closed
    lastClearEvents = processor.getClearEvents();
    lastStoreEvents = processor.getStoreEvents();
    lastMarkerEvents = processor.getMarkerEvents();
}
```

## Notes

- Panes mask is coerced to non-zero (at least waveform is always visible).
- Floating mask has no such constraint (panes can be off but have saved bounds).
- Window bounds are clipped to screen bounds on restoration (if monitor was unplugged, windows snap back on-screen).
- History (chart data, spectrogram pixels, markers) is not persisted — it's UI state, not project state.
