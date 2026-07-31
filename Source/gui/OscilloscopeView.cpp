#include "OscilloscopeView.h"
#include "Theme.h"

OscilloscopeView::OscilloscopeView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);
    engine.addConsumer();
}

OscilloscopeView::~OscilloscopeView()
{
    engine.removeConsumer();
}

void OscilloscopeView::setActive (bool shouldBeActive)
{
    if (shouldBeActive == isTimerRunning())
        return;

    if (shouldBeActive)
    {
        engine.getScopeSamples().discardPending();

        // Start from silence rather than whatever the last look held.
        std::fill (ring.begin(), ring.end(), 0.0f);
        head = 0;
        numStored = 0;
        lastFrameTriggered = false;

        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

void OscilloscopeView::clear()
{
    engine.getScopeSamples().discardPending();

    std::fill (ring.begin(), ring.end(), 0.0f);
    head = 0;
    numStored = 0;
    lastFrameTriggered = false;
    repaint();
}

void OscilloscopeView::timerCallback()
{
    auto totalRead = 0;

    // Drain in bounded batches so a long stall can't turn one timer callback
    // into an unbounded copy.
    for (;;)
    {
        const auto numRead = engine.getScopeSamples().pop (scratch.data(), maxSamplesPerFrame);

        if (numRead <= 0)
            break;

        for (int i = 0; i < numRead; ++i)
        {
            const auto& s = scratch[static_cast<size_t> (i)];

            ring[static_cast<size_t> (head)] = (s.left + s.right) * 0.5f;
            head = (head + 1) % ringCapacity;
        }

        totalRead += numRead;

        if (numRead < maxSamplesPerFrame)
            break;
    }

    if (totalRead > 0)
    {
        numStored = juce::jmin (ringCapacity, numStored + totalRead);
        repaint();
    }
}

float OscilloscopeView::sampleAtAge (int age) const noexcept
{
    auto index = head - 1 - age;

    while (index < 0)
        index += ringCapacity;

    return ring[static_cast<size_t> (index % ringCapacity)];
}

int OscilloscopeView::findTriggerStart() const noexcept
{
    // Walk backwards from just behind the free-run window. Backwards, a rising
    // crossing appears as an above-band sample first, then a below-band one;
    // the trigger point is the first non-negative sample between them, taken
    // forward in time.
    const auto deepest = juce::jmin (numStored - 1, windowLength + triggerSearchSpan);

    auto armedAge = -1;   // newest above-band sample seen so far

    for (int age = windowLength; age <= deepest; ++age)
    {
        const auto value = sampleAtAge (age);

        if (value > triggerHysteresis)
        {
            armedAge = age;
        }
        else if (value < -triggerHysteresis && armedAge >= 0)
        {
            for (int c = age - 1; c >= armedAge; --c)
                if (sampleAtAge (c) >= 0.0f)
                    return c;

            return armedAge;
        }
    }

    return -1;
}

void OscilloscopeView::drawGraticule (juce::Graphics& g)
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

    // Ten time divisions across the sweep.
    g.setColour (Theme::palette().grid.withAlpha (0.4f));

    for (int division = 1; division < 10; ++division)
    {
        const auto x = bounds.getX() + bounds.getWidth() * static_cast<float> (division) / 10.0f;
        g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
    }
}

void OscilloscopeView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (Theme::palette().screenBlack);
    drawGraticule (g);

    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;
    const auto width = bounds.getWidth();

    if (numStored >= windowLength && halfHeight > 0.0f && width > 1.0f)
    {
        const auto triggerAge = findTriggerStart();
        lastFrameTriggered = triggerAge >= 0;

        const auto startAge = lastFrameTriggered ? triggerAge : windowLength - 1;

        const auto toY = [centreY, halfHeight] (float value)
        {
            return centreY - juce::jlimit (-1.0f, 1.0f, value) * halfHeight;
        };

        juce::Path trace;

        for (int i = 0; i < windowLength; ++i)
        {
            const auto x = width * static_cast<float> (i) / static_cast<float> (windowLength - 1);
            const auto y = toY (sampleAtAge (startAge - i));

            if (i == 0)
                trace.startNewSubPath (x, y);
            else
                trace.lineTo (x, y);
        }

        // The trace itself: wide dim pass, then narrow bright — phosphor bloom.
        g.setColour (Theme::palette().amber.withAlpha (0.25f));
        g.strokePath (trace, juce::PathStrokeType (2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (Theme::palette().amberBright.withAlpha (0.9f));
        g.strokePath (trace, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Silkscreened readout, bottom-right: sweep length and trigger state.
    {
        g.setFont (Theme::mono (9.0f));
        g.setColour (Theme::palette().boneDim);

        const auto sampleRate = engine.getSampleRate();

        if (sampleRate > 0.0)
        {
            const auto sweepMs = 1000.0 * windowLength / sampleRate;

            g.drawText (juce::String (sweepMs, 1) + " MS SWEEP",
                        juce::Rectangle<float> (width - 128.0f, bounds.getBottom() - 26.0f, 120.0f, 11.0f),
                        juce::Justification::centredRight);
        }

        g.drawText (lastFrameTriggered ? "TRIG RISING" : "FREE RUN",
                    juce::Rectangle<float> (width - 128.0f, bounds.getBottom() - 14.0f, 120.0f, 11.0f),
                    juce::Justification::centredRight);
    }
}
