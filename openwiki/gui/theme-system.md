---
type: component
title: Theme System
description: Four film-grade liveries (AMBER/NOSTROMO/TVA/GRTA) with palette data and chassis rendering
---

# Theme System

The theme system manages four complete visual palettes, each with cassette-futurist styling and a historical reference. Colors are not tints of each other; every hue is deliberately shifted so no palette is pure grey or pure black.

## Architecture

Source: `Source/gui/Theme.h`

```cpp
namespace Theme {
  enum class TitleStyle { filled, outline, plate };
  
  struct Palette {
    const char* name, *unit;
    Colour shellDark, shellMid, bezel, bezelHi, bezelLo;  // chassis
    Colour screenBlack, grid, gridBright;                 // screen
    Colour amber, amberBright, amberDim;                  // phosphor
    Colour phosphor;                                      // nominal lamps
    Colour rust;                                          // alerts
    Colour mustard;
    Colour bone, boneDim;                                 // print/labels
    std::array<PixelARGB, 256> spectrogramTable;          // palette
    TitleStyle titleStyle;
    const char* subtitle;                                 // header flavour
    Colour secondary;                                     // SIDE trace, secondary readouts
    float scanlineAlpha, vignetteAlpha;                   // CRT overlay
  };
  
  inline const std::array<Palette, 4> palettes { ... };  // AMBER, NOSTROMO, TVA, GRTA
  
  void setCurrent(int index);
  const Palette& palette();
  const Font& mono(float height);  // monospace typeface
}
```

## The Four Liveries

**A — AMBER** (default)
- Reference: Canonical P3 amber phosphor on warm near-black
- Character: Cassette-futurist terminal aesthetic
- Title style: Filled (solid text)
- Secondary colour: Cyan

**B — NOSTROMO**
- Reference: MU-TH-UR's yellow-green phosphor (Alien 1979)
- Character: Ship terminal, military hardware
- Title style: Outline (hollow strokes)
- Secondary colour: Signal red (Semiotic Standard)

**C — TVA**
- Reference: Aged-paper chassis with walnut shadow line (bureaucratic hardware from outside time)
- Character: Office equipment, 1970s institutional
- Title style: Plate (text on rounded placard)
- Secondary colour: Grey-green mint

**D — GRTA**
- Reference: Ice blue-white in slate cabinet (ELIZA/therapy mainframe)
- Character: Clinical, sterile, unsettling
- Title style: Filled
- Secondary colour: Salmon

## Chassis Elements

Every palette defines three shell tones (dark/mid/highlight) for depth and beveling. The bezel is the frame around the screens; bezelHi and bezelLo are the highlight and shadow edges for a 3D appearance.

## Screen Colors

**screenBlack:** The CRT background (never pure black, always tinted).

**grid, gridBright:** Grid lines and bright grid lines for time/frequency axes.

## Phosphor Traces

**amber, amberBright, amberDim:** The main trace colour in three weights. Despite the name "amber", these are palette-specific and vary per livery.

**phosphor:** Colour for nominal lamps (signal indicators).

**rust:** Alert/warning colour (ERR lamp, out-of-compliance states).

**mustard:** Accent colour for specific UI elements.

## Print (Labels)

**bone, boneDim:** Silkscreened text colour in two weights.

## Spectrogram Palette

`spectrogramTable` is a 256-entry colour lookup table, one entry per normalized magnitude level. Built by `ColourMaps::buildTable()` with a palette-specific gradient. On theme switch, all spectrogram history is re-rendered into the new palette.

## CRT Overlay

**scanlineAlpha:** Darkness of the horizontal scanlines (every 3rd pixel row). Values ~0.1–0.3.

**vignetteAlpha:** Strength of the radial vignette (darker edges). Values ~0.15–0.25.

## Usage

**Set theme:** `Theme::setCurrent(0)` through `Theme::setCurrent(3)` switches the active palette.

**Read colours:** Views call `Theme::palette().amberBright`, etc., at paint time. All views read through the same global namespace, so switching themes instantly updates the display (except the spectrogram, which re-renders history).

**Font:** `Theme::mono(height)` returns a monospace typeface suitable for all text. Same font across all liveries.

## Persistence

The theme index is stored in the plugin state (`PluginProcessor::themeIndex`) and restored on session load. The choice persists with the session.
