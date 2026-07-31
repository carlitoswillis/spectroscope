#include "WaveformView.h"
#include "Theme.h"

namespace
{
    // Readout box near the pointer, clamped inside the view: screenBlack glass
    // over amberBright print, the weight of a panel-mounted meter window.
    void drawReadoutBox (juce::Graphics& g, juce::Rectangle<float> bounds,
                         juce::Point<float> pointer, const juce::StringArray& lines)
    {
        const juce::Font font (Theme::mono (9.0f));

        auto textWidth = 0.0f;

        for (const auto& line : lines)
            textWidth = juce::jmax (textWidth, juce::GlyphArrangement::getStringWidth (font, line));

        constexpr auto lineHeight = 11.0f;
        const auto boxWidth = textWidth + 10.0f;
        const auto boxHeight = lineHeight * static_cast<float> (lines.size()) + 6.0f;

        auto x = pointer.x + 10.0f;
        auto y = pointer.y + 10.0f;

        // Flip to whichever side of the pointer keeps the box on the glass.
        if (x + boxWidth > bounds.getRight())
            x = pointer.x - 10.0f - boxWidth;

        if (y + boxHeight > bounds.getBottom())
            y = pointer.y - 10.0f - boxHeight;

        x = juce::jlimit (bounds.getX(), juce::jmax (bounds.getX(), bounds.getRight() - boxWidth), x);
        y = juce::jlimit (bounds.getY(), juce::jmax (bounds.getY(), bounds.getBottom() - boxHeight), y);

        const juce::Rectangle<float> box (x, y, boxWidth, boxHeight);

        g.setColour (Theme::palette().screenBlack.withAlpha (0.85f));
        g.fillRect (box);

        g.setFont (font);
        g.setColour (Theme::palette().amberBright);

        for (int i = 0; i < lines.size(); ++i)
            g.drawText (lines[i],
                        juce::Rectangle<float> (box.getX() + 5.0f, box.getY() + 3.0f + lineHeight * static_cast<float> (i),
                                                boxWidth - 10.0f, lineHeight),
                        juce::Justification::centredLeft);
    }
}

WaveformView::WaveformView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);

    engine.addConsumer();

    // Anything pending was captured before this view existed; rendering it would
    // paint a backlog of stale audio on open.
    engine.getEnvelopeQueue().discardPending();

    startTimerHz (60);
}

void WaveformView::clear()
{
    engine.getEnvelopeQueue().discardPending();
    head = 0;
    numStored = 0;
    repaint();
}

WaveformView::~WaveformView()
{
    engine.removeConsumer();
}

void WaveformView::mouseMove (const juce::MouseEvent& event)
{
    cursor = event.getPosition();
    repaint();
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    cursor = { -1, -1 };
    repaint();
}

const EnvelopePoint* WaveformView::pointAtAge (int age) const noexcept
{
    if (age < 0 || age >= numStored)
        return nullptr;

    auto index = head - 1 - age;

    while (index < 0)
        index += ringCapacity;

    return &ring[static_cast<size_t> (index % ringCapacity)];
}

double WaveformView::getVisibleTimeSpan() const noexcept
{
    return getWidth() * engine.getSecondsPerPoint();
}

void WaveformView::timerCallback()
{
    auto totalRead = 0;

    // Drain in bounded batches so a long stall can't turn one timer callback
    // into an unbounded copy.
    for (;;)
    {
        const auto numRead = engine.getEnvelopeQueue().pop (scratch.data(), maxPointsPerFrame);

        if (numRead <= 0)
            break;

        for (int i = 0; i < numRead; ++i)
        {
            ring[static_cast<size_t> (head)] = scratch[static_cast<size_t> (i)];
            head = (head + 1) % ringCapacity;
        }

        totalRead += numRead;

        if (numRead < maxPointsPerFrame)
            break;
    }

    if (totalRead > 0)
    {
        numStored = juce::jmin (ringCapacity, numStored + totalRead);
        repaint();
    }
}

void WaveformView::drawGraticule (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;

    // Quarter-scale rules, brighter on the centre line — an oscilloscope
    // graticule rather than a chart grid.
    for (const auto fraction : { 0.5f, 1.0f })
    {
        g.setColour (Theme::palette().grid.withAlpha (fraction > 0.9f ? 0.55f : 0.35f));

        for (const auto sign : { -1.0f, 1.0f })
        {
            const auto y = centreY + sign * fraction * halfHeight;
            g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());
        }
    }

    g.setColour (Theme::palette().gridBright.withAlpha (0.7f));
    g.drawHorizontalLine (juce::roundToInt (centreY), bounds.getX(), bounds.getRight());

    // Time ticks every second, counted back from the right-hand edge.
    const auto secondsPerColumn = engine.getSecondsPerPoint();

    if (secondsPerColumn > 0.0)
    {
        const auto columnsPerSecond = 1.0 / secondsPerColumn;

        g.setColour (Theme::palette().grid.withAlpha (0.4f));

        for (int second = 1;; ++second)
        {
            const auto x = bounds.getRight() - static_cast<float> (second * columnsPerSecond);

            if (x < bounds.getX())
                break;

            g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
        }
    }
}

void WaveformView::drawCursorReadout (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (cursor.x < 0 || ! bounds.contains (cursor.toFloat()))
        return;

    const auto fx = static_cast<float> (cursor.x);
    const auto fy = static_cast<float> (cursor.y);

    const float dashes[] = { 4.0f, 3.0f };

    g.setColour (Theme::palette().boneDim.withAlpha (0.5f));
    g.drawDashedLine (juce::Line<float> (bounds.getX(), fy, bounds.getRight(), fy), dashes, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (fx, bounds.getY(), fx, bounds.getBottom()), dashes, 2, 1.0f);

    const auto secondsBeforeNow = (bounds.getRight() - fx) * engine.getSecondsPerPoint();

    // Invert paint()'s toY(): centreY - clamp(value) * halfHeight.
    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;
    const auto amplitude = halfHeight > 0.0f
        ? juce::jlimit (-1.0f, 1.0f, (centreY - fy) / halfHeight)
        : 0.0f;

    const auto db = 20.0f * std::log10 (std::abs (amplitude));
    const auto dbText = db < -80.0f ? juce::String ("-INF DB") : juce::String (db, 1) + " DB";

    const juce::StringArray lines
    {
        juce::String (-secondsBeforeNow, 1) + " S",
        dbText,
    };

    drawReadoutBox (g, bounds, { fx, fy }, lines);
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (Theme::palette().screenBlack);
    drawGraticule (g);
    drawCursorReadout (g);

    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;

    if (numStored == 0 || halfHeight <= 0.0f)
        return;

    const auto width = getWidth();

    const auto toY = [centreY, halfHeight] (float value)
    {
        return centreY - juce::jlimit (-1.0f, 1.0f, value) * halfHeight;
    };

    // Collect the visible envelope, then smooth it over a few columns. At one
    // analysis hop per pixel, dense material flips between peak and silence
    // every few pixels and the raw trace turns into a comb; a short moving
    // average keeps transients while letting the outline flow like a drawn
    // wave instead of bristling.
    columnScratch.clear();

    if (columnScratch.capacity() < static_cast<size_t> (width))
        columnScratch.reserve (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
        if (const auto* point = pointAtAge (width - 1 - x))
            columnScratch.push_back ({ static_cast<float> (x), point->maxValue, point->minValue, point->rms });

    if (columnScratch.empty())
        return;

    const auto count = static_cast<int> (columnScratch.size());
    constexpr int smoothRadius = 2;

    smoothedScratch.assign (columnScratch.begin(), columnScratch.end());

    for (int i = 0; i < count; ++i)
    {
        auto maxSum = 0.0f, minSum = 0.0f, rmsSum = 0.0f;
        auto n = 0;

        for (int j = juce::jmax (0, i - smoothRadius); j <= juce::jmin (count - 1, i + smoothRadius); ++j)
        {
            maxSum += columnScratch[static_cast<size_t> (j)].maxValue;
            minSum += columnScratch[static_cast<size_t> (j)].minValue;
            rmsSum += columnScratch[static_cast<size_t> (j)].rms;
            ++n;
        }

        smoothedScratch[static_cast<size_t> (i)].maxValue = maxSum / static_cast<float> (n);
        smoothedScratch[static_cast<size_t> (i)].minValue = minSum / static_cast<float> (n);
        smoothedScratch[static_cast<size_t> (i)].rms      = rmsSum / static_cast<float> (n);
    }

    // The envelope as one closed shape: along the maxima left to right, back
    // along the minima. A stroked path with rounded joins reads as a drawn
    // trace; per-pixel rectangles read as a comb.
    juce::Path envelope, rmsBand;

    for (int i = 0; i < count; ++i)
    {
        const auto& s = smoothedScratch[static_cast<size_t> (i)];

        if (i == 0)
        {
            envelope.startNewSubPath (s.x, toY (s.maxValue));
            rmsBand.startNewSubPath (s.x, toY (s.rms));
        }
        else
        {
            envelope.lineTo (s.x, toY (s.maxValue));
            rmsBand.lineTo (s.x, toY (s.rms));
        }
    }

    for (int i = count - 1; i >= 0; --i)
    {
        const auto& s = smoothedScratch[static_cast<size_t> (i)];

        envelope.lineTo (s.x, toY (s.minValue));
        rmsBand.lineTo (s.x, toY (-s.rms));
    }

    envelope.closeSubPath();
    rmsBand.closeSubPath();

    // Translucent body, hotter RMS core: the region the signal actually lives
    // in glows through the peaks without turning the display into a slab.
    g.setColour (Theme::palette().amber.withAlpha (0.16f));
    g.fillPath (envelope);

    g.setColour (Theme::palette().amber.withAlpha (0.34f));
    g.fillPath (rmsBand);

    // The envelope edge is the trace. A wide dim pass then a narrow bright one,
    // so the line blooms into the glass the way a lit phosphor line does.
    const juce::PathStrokeType glowStroke (2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType lineStroke (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    g.setColour (Theme::palette().amber.withAlpha (0.22f));
    g.strokePath (envelope, glowStroke);

    g.setColour (Theme::palette().amberBright.withAlpha (0.85f));
    g.strokePath (envelope, lineStroke);
}
