#include "StereoFieldView.h"
#include "Theme.h"

#include <cmath>

StereoFieldView::StereoFieldView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);
    engine.addConsumer();

    scratch.resize (static_cast<size_t> (maxSamplesPerFrame));
}

StereoFieldView::~StereoFieldView()
{
    engine.removeConsumer();
}

void StereoFieldView::setActive (bool shouldBeActive)
{
    if (shouldBeActive == isTimerRunning())
        return;

    if (shouldBeActive)
    {
        engine.getStereoSamples().discardPending();

        // Start from a dark screen rather than the trace the last look left.
        if (persistence.isValid())
            persistence.clear (persistence.getBounds(), Theme::palette().screenBlack);

        leftNeedle = 0.0f;
        rightNeedle = 0.0f;
        correlationDisplay = 1.0f;

        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

//==============================================================================
bool StereoFieldView::vuColumnVisible() const noexcept
{
    return getWidth() >= minWidthForVuColumn;
}

juce::Rectangle<int> StereoFieldView::scopeSquare() const noexcept
{
    auto area = getLocalBounds().reduced (padding);

    if (vuColumnVisible())
        area.removeFromRight (vuColumnWidth + padding);

    const auto size = juce::jmin (area.getWidth(), area.getHeight() - correlationHeight - gap);

    if (size <= 0)
        return {};

    // The square and its correlation strip centre as one block.
    const auto blockHeight = size + gap + correlationHeight;

    return { area.getCentreX() - size / 2,
             area.getY() + (area.getHeight() - blockHeight) / 2,
             size, size };
}

juce::Rectangle<int> StereoFieldView::correlationStrip() const noexcept
{
    const auto square = scopeSquare();

    if (square.isEmpty())
        return {};

    return { square.getX(), square.getBottom() + gap, square.getWidth(), correlationHeight };
}

void StereoFieldView::clear()
{
    engine.getStereoSamples().discardPending();

    if (persistence.isValid())
        persistence.clear (persistence.getBounds(), Theme::palette().screenBlack);

    storedTrace = {};

    leftNeedle = 0.0f;
    rightNeedle = 0.0f;
    correlationDisplay = 1.0f;
    repaint();
}

void StereoFieldView::toggleStore()
{
    if (storedTrace.isValid())
    {
        storedTrace = {};
    }
    else if (persistence.isValid())
    {
        storedTrace = persistence.createCopy();

        // Wash the frozen cloud toward amberDim so it reads as a different
        // exposure than the live trace developing on top of it, not a
        // second copy of the same one.
        juce::Graphics tg (storedTrace);
        tg.setColour (Theme::palette().amberDim.withAlpha (0.30f));
        tg.fillAll();
    }

    repaint();
}

void StereoFieldView::resized()
{
    const auto square = scopeSquare();

    // The held trace was sized for the old square; there is no honest way to
    // rescale a cloud of dots, so a resize drops it rather than smear it.
    storedTrace = {};

    if (square.isEmpty())
    {
        persistence = {};
        return;
    }

    persistence = juce::Image (juce::Image::RGB, square.getWidth(), square.getHeight(), false);
    persistence.clear (persistence.getBounds(), Theme::palette().screenBlack);
}

//==============================================================================
void StereoFieldView::timerCallback()
{
    if (persistence.isValid())
    {
        juce::Graphics ig (persistence);

        // Fade before plotting: old dots sink one step further into the glass
        // each frame, and the new ones land on top at full heat.
        ig.setColour (Theme::palette().screenBlack.withAlpha (persistenceFade));
        ig.fillAll();

        const auto numRead = engine.getStereoSamples().pop (scratch.data(), maxSamplesPerFrame);

        const auto size = static_cast<float> (persistence.getWidth());
        const auto centre = size * 0.5f;
        const auto k = 0.48f * size;

        const auto dim = Theme::palette().amber.withAlpha (0.45f);
        const auto sparkle = Theme::palette().amberBright.withAlpha (0.6f);

        for (int i = 0; i < numRead; ++i)
        {
            const auto& s = scratch[static_cast<size_t> (i)];

            // Rotated 45 degrees: mono lands on the vertical axis, pure side
            // on the horizontal, anti-phase material leans over.
            const auto mid  = (s.left + s.right) * 0.7071f;
            const auto side = (s.right - s.left) * 0.7071f;

            const auto x = centre + side * k;
            const auto y = centre - mid * k;

            ig.setColour ((++dotPhase & 3) == 0 ? sparkle : dim);
            ig.fillRect (x - 0.75f, y - 0.75f, 1.5f, 1.5f);
        }
    }
    else
    {
        engine.getStereoSamples().discardPending();
    }

    const auto targetFor = [] (float rms)
    {
        const auto db = juce::Decibels::gainToDecibels (rms, vuFloorDb);
        return juce::jlimit (0.0f, 1.0f, (db - vuFloorDb) / -vuFloorDb);
    };

    leftNeedle  += (targetFor (engine.getLeftRms())  - leftNeedle)  * needleAlpha;
    rightNeedle += (targetFor (engine.getRightRms()) - rightNeedle) * needleAlpha;

    correlationDisplay += (engine.getCorrelation() - correlationDisplay) * correlationAlpha;

    repaint();
}

//==============================================================================
void StereoFieldView::drawGraticule (juce::Graphics& g, juce::Rectangle<int> square) const
{
    const auto bounds = square.toFloat();
    const auto k = 0.48f * bounds.getWidth();

    // The channel axes lie on the diagonals: a signal on one channel alone
    // traces the matching line.
    g.setColour (Theme::palette().grid.withAlpha (0.6f));
    g.drawLine (bounds.getX(), bounds.getY(), bounds.getRight(), bounds.getBottom(), 1.0f);
    g.drawLine (bounds.getRight(), bounds.getY(), bounds.getX(), bounds.getBottom(), 1.0f);

    g.setColour (Theme::palette().grid.withAlpha (0.35f));
    g.drawEllipse (juce::Rectangle<float> (1.5f * k, 1.5f * k).withCentre (bounds.getCentre()), 1.0f);

    g.setColour (Theme::palette().boneDim);
    g.setFont (Theme::mono (10.0f, true));
    g.drawText ("L", juce::Rectangle<float> (bounds.getX() + 5.0f, bounds.getY() + 3.0f, 14.0f, 12.0f),
                juce::Justification::centredLeft);
    g.drawText ("R", juce::Rectangle<float> (bounds.getRight() - 19.0f, bounds.getY() + 3.0f, 14.0f, 12.0f),
                juce::Justification::centredRight);
}

void StereoFieldView::drawVuMeter (juce::Graphics& g, juce::Rectangle<float> bounds,
                                   juce::StringRef channelLetter, float needlePosition) const
{
    const auto& p = Theme::palette();
    const auto face = bounds.reduced (2.0f);

    g.setColour (p.shellMid);
    g.fillRoundedRectangle (face, 4.0f);

    g.setColour (p.bezelLo);
    g.drawRoundedRectangle (face, 4.0f, 1.2f);

    // The pivot sits below the window, the way the coil does on a real meter;
    // scale and needle are clipped to the glass.
    const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.6f);
    const auto radius = pivot.y - face.getY() - 16.0f;

    if (radius <= 0.0f)
        return;

    const auto angleFor = [] (float position)
    {
        return juce::degreesToRadians (-45.0f + 90.0f * position);
    };

    const auto directionFor = [] (float angle)
    {
        return juce::Point<float> (std::sin (angle), -std::cos (angle));
    };

    const auto hotPosition = (vuHotDb - vuFloorDb) / -vuFloorDb;

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    juce::Path scaleArc;
    scaleArc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f,
                            angleFor (0.0f), angleFor (hotPosition), true);
    g.setColour (p.boneDim.withAlpha (0.8f));
    g.strokePath (scaleArc, juce::PathStrokeType (1.2f));

    juce::Path hotArc;
    hotArc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f,
                          angleFor (hotPosition), angleFor (1.0f), true);
    g.setColour (p.rust);
    g.strokePath (hotArc, juce::PathStrokeType (1.6f));

    for (const auto db : { -40.0f, -30.0f, -20.0f, -10.0f, -6.0f, -3.0f, 0.0f })
    {
        const auto angle = angleFor ((db - vuFloorDb) / -vuFloorDb);
        const auto direction = directionFor (angle);

        g.setColour (db >= vuHotDb ? p.rust : p.boneDim);
        g.drawLine (juce::Line<float> (pivot + direction * radius,
                                       pivot + direction * (radius + 5.0f)),
                    1.0f);
    }

    const auto needleAngle = angleFor (juce::jlimit (0.0f, 1.0f, needlePosition));
    g.setColour (p.amber);
    g.drawLine (juce::Line<float> (pivot, pivot + directionFor (needleAngle) * (radius + 8.0f)), 1.4f);

    g.restoreState();

    g.setColour (p.bone);
    g.setFont (Theme::mono (11.0f, true));
    g.drawText (channelLetter,
                juce::Rectangle<float> (face.getX() + 6.0f, face.getY() + 4.0f, 16.0f, 12.0f),
                juce::Justification::centredLeft);

    g.setColour (p.boneDim);
    g.setFont (Theme::mono (9.0f, true));
    g.drawText (Theme::spaced ("VU"),
                juce::Rectangle<float> (face.getCentreX() - 20.0f, face.getBottom() - 15.0f, 40.0f, 11.0f),
                juce::Justification::centred);
}

void StereoFieldView::drawCorrelationStrip (juce::Graphics& g, juce::Rectangle<int> strip) const
{
    const auto& p = Theme::palette();
    auto bounds = strip.toFloat();

    g.setColour (p.boneDim);
    g.setFont (Theme::mono (9.0f, true));
    g.drawText (Theme::spaced ("CORR"), bounds.removeFromLeft (44.0f),
                juce::Justification::centredLeft);

    const auto scale = bounds.reduced (2.0f, 2.0f);
    const auto centreX = scale.getCentreX();

    // Anti-phase territory carries a warning tint.
    g.setColour (p.rust.withAlpha (0.08f));
    g.fillRect (scale.withRight (centreX));

    g.setColour (p.grid.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (scale.getCentreY()), scale.getX(), scale.getRight());

    for (const auto position : { -1.0f, 0.0f, 1.0f })
    {
        const auto x = centreX + position * scale.getWidth() * 0.5f;
        g.drawLine (x, scale.getY(), x, scale.getBottom(), 1.0f);
    }

    g.setColour (p.boneDim.withAlpha (0.8f));
    g.setFont (Theme::mono (8.5f));
    g.drawText ("-1", juce::Rectangle<float> (scale.getX() + 4.0f, scale.getY(), 20.0f, scale.getHeight()),
                juce::Justification::centredLeft);
    g.drawText ("0", juce::Rectangle<float> (centreX + 4.0f, scale.getY(), 14.0f, scale.getHeight()),
                juce::Justification::centredLeft);
    g.drawText ("+1", juce::Rectangle<float> (scale.getRight() - 24.0f, scale.getY(), 20.0f, scale.getHeight()),
                juce::Justification::centredRight);

    const auto needleX = centreX + juce::jlimit (-1.0f, 1.0f, correlationDisplay) * scale.getWidth() * 0.5f;

    g.setColour (p.amberBright);
    g.fillRect (needleX - 1.0f, scale.getY(), 2.0f, scale.getHeight());
}

//==============================================================================
void StereoFieldView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::palette().screenBlack);

    const auto square = scopeSquare();

    if (! square.isEmpty())
    {
        if (storedTrace.isValid())
        {
            g.setOpacity (0.45f);
            g.drawImageAt (storedTrace, square.getX(), square.getY());
            g.setOpacity (1.0f);
        }

        if (persistence.isValid())
        {
            // Opaque, as ever — except when a trace is held, when it eases
            // back a touch so the ghost underneath keeps showing through
            // rather than being paved over frame after frame.
            g.setOpacity (storedTrace.isValid() ? 0.82f : 1.0f);
            g.drawImageAt (persistence, square.getX(), square.getY());
            g.setOpacity (1.0f);
        }

        drawGraticule (g, square);
        drawCorrelationStrip (g, correlationStrip());

        if (storedTrace.isValid())
        {
            g.setColour (Theme::palette().boneDim);
            g.setFont (Theme::mono (9.0f, true));
            g.drawText (Theme::spaced ("STORED"),
                        juce::Rectangle<int> (square.getX() + 4, square.getY() + 3, 70, 12),
                        juce::Justification::centredLeft);
        }
    }

    if (vuColumnVisible())
    {
        auto column = getLocalBounds().reduced (padding).removeFromRight (vuColumnWidth);
        const auto meterHeight = (column.getHeight() - gap) / 2;

        if (meterHeight <= 0)
            return;

        drawVuMeter (g, column.removeFromTop (meterHeight).toFloat(), "L", leftNeedle);
        column.removeFromTop (gap);
        drawVuMeter (g, column.toFloat(), "R", rightNeedle);
    }
}
