---
type: plugin
title: Themes and Liveries
description: Four film-grade palettes (AMBER, NOSTROMO, TVA, GRTA) with palette system, title styles, colour tables
tags: [plugin, themes, ui, colours]
---

# Themes and Liveries

Spectroscope ships with four complete themes, each with a distinct visual character. Every colour in every theme is deliberately hue-shifted — there is no pure grey or pure black anywhere, which separates these from generic dark-mode and makes them read as photographed hardware rather than a UI.

## Theme System

**Header:** `Source/gui/Theme.h`

```cpp
namespace Theme
{
    struct Palette { ... };
    
    inline const std::array<Palette, 4> palettes {{ /* four themes */ }};
    inline int currentIndex = 0;
    
    inline void setCurrent(int index) noexcept;
    inline const Palette& palette() noexcept;
}
```

Theme is a namespace with global state, not a class. All views read `Theme::palette()` at paint time. Switching is atomic:

```cpp
Theme::setCurrent(newIndex);  // All views immediately see new palette
```

No rebuilding required except the spectrogram's colour table.

## Palette Structure

```cpp
struct Palette {
    const char* name;     // "AMBER", "NOSTROMO", etc.
    const char* unit;     // "UNIT A", "UNIT B", etc.
    
    // Chassis (physical object)
    juce::Colour shellDark, shellMid, bezel, bezelHi, bezelLo;
    
    // Screens (behind glass)
    juce::Colour screenBlack, grid, gridBright;
    
    // Phosphor traces
    juce::Colour amber, amberBright, amberDim, phosphor, rust, mustard;
    
    // Print (silkscreened labels)
    juce::Colour bone, boneDim;
    
    // Spectrogram color table (256 RGBA values)
    std::array<juce::PixelARGB, 256> spectrogramTable;
    
    // Style character
    TitleStyle titleStyle;  // filled, outline, or plate
    const char* subtitle;   // Header flavour text
    juce::Colour secondary; // SIDE trace colour, second readouts
    float scanlineAlpha;    // CRT overlay darkness
    float vignetteAlpha;    // Vignette strength
};
```

## The Four Themes

### 1. AMBER (UNIT A)

**Character:** Classic terminal phosphor  
**Inspiration:** Vintage amber CRT terminals

- **Shell:** Dark warm brown with brighter mid-tone
- **Screen:** Very dark, almost black
- **Trace:** Bright amber/orange (P3 phosphor value)
- **Dim tier:** Burnt orange (aged tube two-tone)
- **Secondary:** Mustard yellow
- **Title style:** Filled solid text
- **Scanlines:** 16% alpha (visible but not overwhelming)
- **Vignette:** 55% alpha (strong edge darkening)

Subtitle: "SPECTRAL ANALYSIS UNIT / MK I"

### 2. NOSTROMO (UNIT B)

**Character:** Industrial sci-fi, hazard warnings  
**Inspiration:** Alien (MU-TH-UR 6000 computer)

- **Shell:** Very dark blue-green (military grey-green)
- **Screen:** Dark blue-green
- **Trace:** Yellowy-green (MU-TH-UR terminal green, not stock terminal green)
- **Grid:** Shifted green
- **Alerts:** Semiotic Standard signal red (#cfr 3a2b)
- **Panel print:** Hazard yellow
- **Secondary:** Cyan (bright)
- **Title style:** Outline (stroked glyph paths, hollow)
- **Scanlines:** 22% alpha
- **Vignette:** 60% alpha

Subtitle: "MU-TH-UR 6000 LINK / SPECTRAL SUBSYSTEM"

### 3. TVA (UNIT C)

**Character:** Aged institutional print on walnut  
**Inspiration:** 1970s-80s scientific instrument

- **Shell:** Aged paper (beige/cream) over walnut shadow line
- **Screen:** Very dark (brown-black)
- **Trace:** Sepia orange (warm, aged amber)
- **Dim tier:** Dark orange
- **Grid:** Aged green (grey-green mint)
- **Secondary:** Mint green
- **Title style:** Plate (rounded placard in accent, text knocked out)
- **Scanlines:** 10% alpha (subtle)
- **Vignette:** 62% alpha (strong)

Subtitle: "SPECTRAL VARIANCE DIVISION / CASE FILE 7-C"

### 4. GRTA (UNIT D)

**Character:** LED wall and blinking lights  
**Inspiration:** 1980s-90s control room console

- **Shell:** Dark blue-grey (LED-wall mounting)
- **Screen:** Very dark blue
- **Trace:** LED blue-white (not terminal cyan)
- **Nominal lamp:** Console green
- **Alerts:** Red
- **Panel print:** Amber blinkenlights
- **Secondary:** Red
- **Title style:** Filled solid text
- **Scanlines:** 16% alpha
- **Vignette:** 58% alpha

Subtitle: "SPECTRAL THERAPY MODULE / SESSION 9"

## Color Tables (Spectrogram)

Four pre-built colour ramps in `Source/gui/ColourMaps.h`:

```cpp
namespace ColourMaps {
    std::array<juce::PixelARGB, 256> buildTable(const ColourRamp&);
    
    ColourRamp amberPhosphor;    // AMBER, TVA
    ColourRamp greenPhosphor;    // NOSTROMO
    ColourRamp bluePhosphor;     // GRTA
}
```

Each is a 256-entry linear ramp from dark (noise floor) through accent hues to bright (peaks).

## Title Styles

```cpp
enum class TitleStyle { filled, outline, plate };
```

### Filled
Solid phosphor text, opaque. Used by AMBER and GRTA.

### Outline
Stroked glyph paths, hollow interior. Used by NOSTROMO (MU-TH-UR boot screen aesthetic).

### Plate
Rounded placard background in the accent colour, with text knocked out in the shell mid-tone. Used by TVA (institutional label).

## CRT Overlay

All themes use CRT glass rendering (scanlines + vignette). Details in [[crt-overlay]] (planned; see CrtOverlay.h/cpp).

- **Scanlines:** Horizontal stripes, ~3 pixels apart, darkened by scanlineAlpha
- **Vignette:** Radial gradient from screen to edges, darkened by vignetteAlpha

Both are applied in-shader (GPU spectrogram) and as image overlays (CPU).

## Usage in Views

```cpp
const auto& pal = Theme::palette();
g.setColour(pal.amber);         // Main trace
g.setColour(pal.secondary);      // SIDE trace
g.setColour(pal.rust);           // Alerts
g.setColour(pal.screenBlack);    // Background
g.setColour(pal.bone);           // Labels
```

No hard-coded colours anywhere in views. All drawing reads from the current palette.

## Theme Switching

```cpp
void SpectroscopeAudioProcessorEditor::cycleTheme()
{
    auto index = processor.getThemeIndex();
    index = (index + 1) % 4;
    processor.setThemeIndex(index);
    
    Theme::setCurrent(index);
    spectrogramView.themeChanged();  // Rebuild colour table
    repaint();
}
```

All views repaint automatically (message-thread repaints). Spectrogram rebuilds its colour table and re-renders history.

## Font

```cpp
inline juce::FontOptions mono(float height, bool bold = false)
{
    return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                             height,
                             bold ? juce::Font::bold : juce::Font::plain);
}
```

Institutional monospace everywhere — mixing fonts breaks the hardware illusion. Bold used sparingly (readout headers).
