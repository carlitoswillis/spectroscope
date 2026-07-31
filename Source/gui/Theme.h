#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ColourMaps.h"
#include <array>

/**
    Cassette futurism: the future as imagined by people with CRTs and beige
    plastic. Institutional monospace, chunky bezels with machined corner
    brackets — and a choice of liveries for what's behind the glass.

    Every colour is deliberately hue-shifted — there is no pure grey and no
    pure black anywhere in any palette, which is what separates these from
    generic "dark mode" and what makes them read as photographed hardware
    rather than a UI.

    The palette is selected at runtime. Views read colours through palette()
    at paint time, so switching is a repaint, not a rebuild — except the
    spectrogram's colour table, which its view rebuilds when told.
*/
namespace Theme
{
    //==========================================================================
    struct Palette
    {
        const char* name;       // silkscreened on the theme switch
        const char* unit;       // "UNIT A" .. — the livery's designation

        // Chassis — the physical object the screens are set into.
        juce::Colour shellDark, shellMid, bezel, bezelHi, bezelLo;

        // Screens — what's behind the glass.
        juce::Colour screenBlack, grid, gridBright;

        // Phosphor. "amber" is the trace colour whatever its actual hue —
        // the name survives from the first livery.
        juce::Colour amber, amberBright, amberDim;
        juce::Colour phosphor;   // "nominal" lamps
        juce::Colour rust;       // alerts
        juce::Colour mustard;

        // Print — the silkscreened labels on the panel itself.
        juce::Colour bone, boneDim;

        std::array<juce::PixelARGB, 256> spectrogramTable;
    };

    inline const std::array<Palette, 4> palettes
    {{
        {
            "AMBER", "UNIT A",
            juce::Colour (0xff14100e), juce::Colour (0xff1f1915), juce::Colour (0xff2e2620),
            juce::Colour (0xff473a2f), juce::Colour (0xff0d0a08),
            juce::Colour (0xff0a0906), juce::Colour (0xff3a2a16), juce::Colour (0xff5c421f),
            juce::Colour (0xffffa51f), juce::Colour (0xffffd591), juce::Colour (0xff8a5a12),
            juce::Colour (0xff35e08a), juce::Colour (0xffc4562a), juce::Colour (0xffc9a227),
            juce::Colour (0xffd8c9ae), juce::Colour (0xff8a7f6c),
            ColourMaps::buildTable (ColourMaps::amberPhosphor),
        },
        {
            "NOSTROMO", "UNIT B",
            juce::Colour (0xff0d100e), juce::Colour (0xff161c18), juce::Colour (0xff222b25),
            juce::Colour (0xff37453c), juce::Colour (0xff070908),
            juce::Colour (0xff050908), juce::Colour (0xff163021), juce::Colour (0xff234a30),
            juce::Colour (0xff2fd97e), juce::Colour (0xffc9ffdb), juce::Colour (0xff17754a),
            juce::Colour (0xff35e08a), juce::Colour (0xffc4562a), juce::Colour (0xffc9a227),
            juce::Colour (0xffbfcfc0), juce::Colour (0xff6f8272),
            ColourMaps::buildTable (ColourMaps::greenPhosphor),
        },
        {
            "TVA", "UNIT C",
            juce::Colour (0xffc4b394), juce::Colour (0xffdccdb0), juce::Colour (0xffb0a184),
            juce::Colour (0xfff2e8d0), juce::Colour (0xff6d5f47),
            juce::Colour (0xff100d09), juce::Colour (0xff3a2c18), juce::Colour (0xff5c451f),
            juce::Colour (0xffc9541f), juce::Colour (0xffe5762a), juce::Colour (0xff8f4a1a),
            juce::Colour (0xff2f9e6e), juce::Colour (0xffb03a20), juce::Colour (0xffb98f2c),
            juce::Colour (0xff4a3d2c), juce::Colour (0xff77664e),
            ColourMaps::buildTable (ColourMaps::amberPhosphor),
        },
        {
            "GRTA", "UNIT D",
            juce::Colour (0xff101318), juce::Colour (0xff1b212b), juce::Colour (0xff28303d),
            juce::Colour (0xff3e4a5c), juce::Colour (0xff090b0f),
            juce::Colour (0xff060a0f), juce::Colour (0xff17293e), juce::Colour (0xff254059),
            juce::Colour (0xff4fc4e8), juce::Colour (0xffd9f4ff), juce::Colour (0xff2a7a99),
            juce::Colour (0xff35e08a), juce::Colour (0xffe86a55), juce::Colour (0xffc9a227),
            juce::Colour (0xffc4cdd8), juce::Colour (0xff6f7b8a),
            ColourMaps::buildTable (ColourMaps::bluePhosphor),
        },
    }};

    inline int currentIndex = 0;

    inline void setCurrent (int index) noexcept
    {
        currentIndex = juce::jlimit (0, static_cast<int> (palettes.size()) - 1, index);
    }

    inline const Palette& palette() noexcept
    {
        return palettes[static_cast<size_t> (currentIndex)];
    }

    //==========================================================================
    /** Institutional monospace. Everything in this UI is monospaced — mixing in
        a proportional face immediately breaks the illusion.
    */
    inline juce::FontOptions mono (float height, bool bold = false)
    {
        return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                  height,
                                  bold ? juce::Font::bold : juce::Font::plain);
    }

    /** Letterspacing for headings. JUCE has no tracking, and period panel
        lettering is widely spaced, so we space it by hand.
    */
    inline juce::String spaced (juce::StringRef text)
    {
        const juce::String source (text);
        juce::String result;

        for (int i = 0; i < source.length(); ++i)
        {
            result += source.substring (i, i + 1);
            result += " ";
        }

        return result.trimEnd();
    }

    //==========================================================================
    /** Machined corner brackets — four L-shapes, no full rectangle. Reads as a
        registration mark rather than a border.
    */
    inline void drawCornerBrackets (juce::Graphics& g, juce::Rectangle<int> area,
                                    juce::Colour colour, int armLength = 10, float thickness = 1.5f)
    {
        g.setColour (colour);

        const auto bounds = area.toFloat();
        const auto arm = static_cast<float> (armLength);

        const auto x1 = bounds.getX(), y1 = bounds.getY();
        const auto x2 = bounds.getRight(), y2 = bounds.getBottom();

        g.drawLine (x1, y1, x1 + arm, y1, thickness);
        g.drawLine (x1, y1, x1, y1 + arm, thickness);

        g.drawLine (x2 - arm, y1, x2, y1, thickness);
        g.drawLine (x2, y1, x2, y1 + arm, thickness);

        g.drawLine (x1, y2 - arm, x1, y2, thickness);
        g.drawLine (x1, y2, x1 + arm, y2, thickness);

        g.drawLine (x2 - arm, y2, x2, y2, thickness);
        g.drawLine (x2, y2 - arm, x2, y2, thickness);
    }

    /** A screen recessed into the chassis: dark inner edge at the top left,
        catch-light at the bottom right, so the panel reads as having depth.
    */
    inline void drawRecessedScreen (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto outer = area.expanded (3).toFloat();

        g.setColour (palette().bezelLo);
        g.drawRoundedRectangle (outer, 3.0f, 3.0f);

        g.setColour (palette().bezelHi.withAlpha (0.5f));
        g.drawLine (outer.getX() + 2.0f, outer.getBottom(), outer.getRight(), outer.getBottom(), 1.0f);
        g.drawLine (outer.getRight(), outer.getY() + 2.0f, outer.getRight(), outer.getBottom(), 1.0f);
    }

    /** Silkscreened caption sitting in a notch on the panel, the way a label
        plate interrupts a border on real equipment.
    */
    inline void drawLabelPlate (juce::Graphics& g, juce::Rectangle<int> area,
                                juce::StringRef text, juce::Colour colour = palette().boneDim)
    {
        g.setColour (palette().shellMid);
        g.fillRect (area);

        g.setColour (colour);
        g.setFont (mono (9.5f, true));
        g.drawText (spaced (text), area, juce::Justification::centred);
    }

    /** Indicator lamp with a bloom, as though lit from behind the panel. */
    inline void drawLamp (juce::Graphics& g, juce::Rectangle<float> area,
                          juce::Colour colour, bool lit)
    {
        const auto centre = area.getCentre();
        const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

        if (lit)
        {
            g.setColour (colour.withAlpha (0.18f));
            g.fillEllipse (juce::Rectangle<float> (radius * 4.0f, radius * 4.0f).withCentre (centre));

            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.6f, radius * 2.6f).withCentre (centre));
        }

        g.setColour (lit ? colour : colour.withMultipliedBrightness (0.22f));
        g.fillEllipse (juce::Rectangle<float> (radius * 1.6f, radius * 1.6f).withCentre (centre));
    }

    /** Panel fastener. Four of these at the corners of the chassis do more for
        the illusion than any amount of gradient work.
    */
    inline void drawRivet (juce::Graphics& g, juce::Point<float> centre, float radius = 2.5f)
    {
        const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

        g.setColour (palette().bezelLo);
        g.fillEllipse (bounds);

        g.setColour (palette().bezelHi.withAlpha (0.7f));
        g.drawEllipse (bounds.reduced (0.4f), 0.8f);
    }
}
