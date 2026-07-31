#include "WaveformView.h"
#include "Theme.h"

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

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (Theme::palette().screenBlack);
    drawGraticule (g);

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
