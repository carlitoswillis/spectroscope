#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Cassette futurism: the future as imagined by people with CRTs and beige
    plastic. Amber phosphor on a warm near-black chassis, institutional
    monospace, chunky bezels with machined corner brackets.

    Every colour is warm-shifted — there is no pure grey and no pure black
    anywhere in the palette, which is what separates this from generic "dark
    mode" and what makes it read as a photographed screen rather than a UI.
*/
namespace Theme
{
    //==========================================================================
    // Chassis — the physical object the screens are set into.
    inline const juce::Colour shellDark   { 0xff14100e };
    inline const juce::Colour shellMid    { 0xff1f1915 };
    inline const juce::Colour bezel       { 0xff2e2620 };
    inline const juce::Colour bezelHi     { 0xff473a2f };
    inline const juce::Colour bezelLo     { 0xff0d0a08 };

    // Screens — what's behind the glass.
    inline const juce::Colour screenBlack { 0xff0a0906 };
    inline const juce::Colour grid        { 0xff3a2a16 };
    inline const juce::Colour gridBright  { 0xff5c421f };

    // Phosphor.
    inline const juce::Colour amber       { 0xffffa51f };
    inline const juce::Colour amberBright { 0xffffd591 };
    inline const juce::Colour amberDim    { 0xff8a5a12 };
    inline const juce::Colour phosphor    { 0xff35e08a };   // "nominal" lamps
    inline const juce::Colour rust        { 0xffc4562a };   // alerts
    inline const juce::Colour mustard     { 0xffc9a227 };

    // Print — the silkscreened labels on the panel itself.
    inline const juce::Colour bone        { 0xffd8c9ae };
    inline const juce::Colour boneDim     { 0xff8a7f6c };

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

        g.setColour (bezelLo);
        g.drawRoundedRectangle (outer, 3.0f, 3.0f);

        g.setColour (bezelHi.withAlpha (0.5f));
        g.drawLine (outer.getX() + 2.0f, outer.getBottom(), outer.getRight(), outer.getBottom(), 1.0f);
        g.drawLine (outer.getRight(), outer.getY() + 2.0f, outer.getRight(), outer.getBottom(), 1.0f);
    }

    /** Silkscreened caption sitting in a notch on the panel, the way a label
        plate interrupts a border on real equipment.
    */
    inline void drawLabelPlate (juce::Graphics& g, juce::Rectangle<int> area,
                                juce::StringRef text, juce::Colour colour = boneDim)
    {
        g.setColour (shellMid);
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

        g.setColour (bezelLo);
        g.fillEllipse (bounds);

        g.setColour (bezelHi.withAlpha (0.7f));
        g.drawEllipse (bounds.reduced (0.4f), 0.8f);
    }
}
