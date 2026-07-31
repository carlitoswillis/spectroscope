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
        g.setColour (Theme::grid.withAlpha (fraction > 0.9f ? 0.55f : 0.35f));

        for (const auto sign : { -1.0f, 1.0f })
        {
            const auto y = centreY + sign * fraction * halfHeight;
            g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());
        }
    }

    g.setColour (Theme::gridBright.withAlpha (0.7f));
    g.drawHorizontalLine (juce::roundToInt (centreY), bounds.getX(), bounds.getRight());

    // Time ticks every second, counted back from the right-hand edge.
    const auto secondsPerColumn = engine.getSecondsPerPoint();

    if (secondsPerColumn > 0.0)
    {
        const auto columnsPerSecond = 1.0 / secondsPerColumn;

        g.setColour (Theme::grid.withAlpha (0.4f));

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

    g.fillAll (Theme::screenBlack);
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

    // Translucent body. Filling peak-to-peak at full strength turns any
    // continuous material into a solid slab — the graticule disappears behind
    // it and the shape stops reading. Keeping the body dim leaves the edges to
    // carry the information.
    g.setColour (Theme::amber.withAlpha (0.22f));

    for (int x = 0; x < width; ++x)
    {
        if (const auto* point = pointAtAge (width - 1 - x))
        {
            const auto top = toY (point->maxValue);
            const auto bottom = toY (point->minValue);

            g.fillRect (static_cast<float> (x), top, 1.0f, juce::jmax (1.0f, bottom - top));
        }
    }

    // RMS sits inside the peaks, a little hotter — the region the signal
    // actually spends its time in.
    g.setColour (Theme::amber.withAlpha (0.30f));

    for (int x = 0; x < width; ++x)
    {
        if (const auto* point = pointAtAge (width - 1 - x))
        {
            const auto top = toY (point->rms);
            const auto bottom = toY (-point->rms);

            g.fillRect (static_cast<float> (x), top, 1.0f, juce::jmax (1.0f, bottom - top));
        }
    }

    // The envelope edges are the trace. Drawn twice — a wide dim pass then a
    // narrow bright one — so the line blooms into the glass the way a lit
    // phosphor line does, instead of reading as a flat vector.
    struct EdgePass { float thickness; float alpha; juce::Colour colour; };

    const EdgePass edgePasses[] =
    {
        { 3.0f, 0.20f, Theme::amber },
        { 1.0f, 0.95f, Theme::amberBright },
    };

    for (const auto& pass : edgePasses)
    {
        g.setColour (pass.colour.withAlpha (pass.alpha));
        const auto half = pass.thickness * 0.5f;

        for (int x = 0; x < width; ++x)
        {
            if (const auto* point = pointAtAge (width - 1 - x))
            {
                const auto top = toY (point->maxValue);
                const auto bottom = toY (point->minValue);

                g.fillRect (static_cast<float> (x), top - half, 1.0f, pass.thickness);
                g.fillRect (static_cast<float> (x), bottom - half, 1.0f, pass.thickness);
            }
        }
    }
}
