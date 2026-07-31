#include "BootCard.h"
#include "Theme.h"
#include <array>

BootCard::BootCard()
{
    // Opaque and on top for the whole hold — the console underneath doesn't
    // need to paint a single frame until the fade starts letting it through.
    setOpaque (true);
    setAlpha (1.0f);
    setInterceptsMouseClicks (true, false);

    startTime = juce::Time::getMillisecondCounter();
    startTimerHz (60);
}

BootCard::~BootCard()
{
    stopTimer();
}

void BootCard::mouseDown (const juce::MouseEvent&)
{
    beginFade();
}

void BootCard::beginFade()
{
    if (fading)
        return;

    fading = true;
    fadeStartTime = juce::Time::getMillisecondCounter();
}

void BootCard::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    if (! fading)
    {
        if (now - startTime >= static_cast<juce::uint32> (holdMs))
            beginFade();

        return;
    }

    const auto elapsed = now - fadeStartTime;

    if (elapsed >= static_cast<juce::uint32> (fadeMs))
    {
        setAlpha (0.0f);
        setVisible (false);
        stopTimer();

        if (onDismissed != nullptr)
            onDismissed();

        return;
    }

    setAlpha (1.0f - static_cast<float> (elapsed) / static_cast<float> (fadeMs));
}

void BootCard::paint (juce::Graphics& g)
{
    const auto& p = Theme::palette();
    auto bounds = getLocalBounds();

    g.fillAll (p.screenBlack);

    auto barsArea = bounds.removeFromTop (juce::roundToInt (getHeight() * 0.42f));
    drawColourBars (g, barsArea);

    drawCrosshatch (g, bounds);

    // Wordmark and print, stacked in a block held off-centre from the bars
    // the way a real alignment card keeps its legend below the pattern.
    auto legend = bounds.reduced (0, juce::roundToInt (bounds.getHeight() * 0.16f));
    auto wordmarkArea = legend.removeFromTop (legend.getHeight() * 2 / 3);
    drawWordmark (g, wordmarkArea);

    g.setColour (p.boneDim.withAlpha (0.85f));
    g.setFont (Theme::mono (11.0f, true));
    g.drawText (Theme::spaced (juce::String (p.unit) + " / " + juce::String (p.name)),
               legend.removeFromTop (20), juce::Justification::centred);

    g.setColour (p.boneDim.withAlpha (0.65f));
    g.setFont (Theme::mono (9.0f));
    g.drawText (Theme::spaced ("ALIGNMENT"), legend, juce::Justification::centred);

    // Registration crosshair, dead centre of the card — the point every
    // other mark on the pattern is squared against.
    const auto centre = getLocalBounds().getCentre().toFloat();
    g.setColour (p.gridBright.withAlpha (0.8f));
    g.drawLine (centre.x - 10.0f, centre.y, centre.x + 10.0f, centre.y, 1.0f);
    g.drawLine (centre.x, centre.y - 10.0f, centre.x, centre.y + 10.0f, 1.0f);

    Theme::drawCornerBrackets (g, getLocalBounds().reduced (6), p.bone.withAlpha (0.8f));
}

void BootCard::drawColourBars (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& p = Theme::palette();

    // Seven bars, built entirely from the livery's own colours rather than
    // broadcast-standard hues — each theme throws a different pattern.
    const juce::Colour bars[] = { p.bone, p.mustard, p.secondary, p.phosphor,
                                  p.amber, p.rust, p.boneDim };

    const auto barCount = static_cast<int> (std::size (bars));
    const auto barWidth = area.getWidth() / barCount;
    auto remaining = area;

    for (int i = 0; i < barCount; ++i)
    {
        const auto bar = (i == barCount - 1) ? remaining : remaining.removeFromLeft (barWidth);

        g.setColour (bars[static_cast<size_t> (i)]);
        g.fillRect (bar);
    }
}

void BootCard::drawCrosshatch (juce::Graphics& g, juce::Rectangle<int> area)
{
    constexpr int pitch = 18;

    g.setColour (Theme::palette().grid.withAlpha (0.5f));

    for (int x = area.getX(); x < area.getRight(); x += pitch)
        g.drawVerticalLine (x, static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));

    for (int y = area.getY(); y < area.getBottom(); y += pitch)
        g.drawHorizontalLine (y, static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
}

void BootCard::drawWordmark (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& p = Theme::palette();
    const auto titleText = Theme::spaced ("SPECTROSCOPE");
    const juce::Font titleFont (Theme::mono (28.0f, true));

    // Same three treatments the header uses, just centred rather than set to
    // the left margin — the card is a standalone plate, not a title bar.
    switch (p.titleStyle)
    {
        case Theme::TitleStyle::filled:
            g.setColour (p.amber);
            g.setFont (titleFont);
            g.drawText (titleText, area, juce::Justification::centred);
            break;

        case Theme::TitleStyle::outline:
        {
            const auto textWidth = juce::GlyphArrangement::getStringWidth (titleFont, titleText);
            const auto baseline = juce::Point<float> (area.getCentreX() - textWidth * 0.5f,
                                                       area.getCentreY() + titleFont.getAscent() * 0.5f);

            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (titleFont, titleText, baseline.x, baseline.y);

            juce::Path outline;
            glyphs.createPath (outline);

            g.setColour (p.amber);
            g.strokePath (outline, juce::PathStrokeType (1.4f));
            break;
        }

        case Theme::TitleStyle::plate:
        {
            const auto textWidth = juce::GlyphArrangement::getStringWidth (titleFont, titleText);
            const auto plate = juce::Rectangle<float> (textWidth, titleFont.getHeight())
                                   .withCentre (area.getCentre().toFloat())
                                   .expanded (8.0f, 3.0f);

            g.setColour (p.amber);
            g.fillRoundedRectangle (plate, 4.0f);

            g.setColour (p.shellMid);
            g.setFont (titleFont);
            g.drawText (titleText, area, juce::Justification::centred);
            break;
        }
    }
}
