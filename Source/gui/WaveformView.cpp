#include "WaveformView.h"

namespace
{
    const juce::Colour panelFill  { 0xff17171d };
    const juce::Colour gridLine   { 0xff26262f };
    const juce::Colour peakColour { 0xff3f8f88 };
    const juce::Colour rmsColour  { 0xff4fd1c5 };
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

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (panelFill);
    g.fillRect (bounds);

    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;

    g.setColour (gridLine);
    g.drawHorizontalLine (juce::roundToInt (centreY), bounds.getX(), bounds.getRight());

    if (numStored == 0 || halfHeight <= 0.0f)
        return;

    const auto width = getWidth();

    const auto toY = [centreY, halfHeight] (float value)
    {
        return centreY - juce::jlimit (-1.0f, 1.0f, value) * halfHeight;
    };

    // Two passes so the RMS body always sits on top of the peak outline.
    g.setColour (peakColour);

    for (int x = 0; x < width; ++x)
    {
        if (const auto* point = pointAtAge (width - 1 - x))
        {
            const auto top = toY (point->maxValue);
            const auto bottom = toY (point->minValue);

            // Silence still deserves a visible line.
            g.fillRect (static_cast<float> (x), top, 1.0f, juce::jmax (1.0f, bottom - top));
        }
    }

    g.setColour (rmsColour);

    for (int x = 0; x < width; ++x)
    {
        if (const auto* point = pointAtAge (width - 1 - x))
        {
            const auto top = toY (point->rms);
            const auto bottom = toY (-point->rms);

            g.fillRect (static_cast<float> (x), top, 1.0f, juce::jmax (1.0f, bottom - top));
        }
    }
}
