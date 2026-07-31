#include "LoudnessHistoryView.h"
#include "Theme.h"

LoudnessHistoryView::LoudnessHistoryView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);
    engine.addConsumer();
    scratch.resize (maxPointsPerFrame);
}

LoudnessHistoryView::~LoudnessHistoryView()
{
    engine.removeConsumer();
}

void LoudnessHistoryView::setActive (bool shouldBeActive)
{
    if (shouldBeActive == isTimerRunning())
        return;

    if (shouldBeActive)
    {
        engine.getLoudnessQueue().discardPending();

        // The backlog is gone, so a kept history would butt old audio against
        // new with an invisible seam. Restart the chart instead.
        writeIndex = 0;
        validCount = 0;
        targetDb = smoothedDb = -100.0f;

        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

void LoudnessHistoryView::clear()
{
    engine.getLoudnessQueue().discardPending();

    std::fill (history.begin(), history.end(), -100.0f);
    writeIndex = 0;
    validCount = 0;
    targetDb = -100.0f;
    smoothedDb = -100.0f;
    repaint();
}

void LoudnessHistoryView::timerCallback()
{
    const auto rate = engine.getSampleRate();

    if (rate <= 0.0)
        return;

    if (rate != configuredRate)
    {
        configuredRate = rate;
        ringSize = juce::jmax (1, juce::roundToInt (rate / engine.getHopSize() * historySeconds));
        history.assign (static_cast<size_t> (ringSize), -100.0f);
        writeIndex = 0;
        validCount = 0;
    }

    auto totalRead = 0;

    for (;;)
    {
        const auto numRead = engine.getLoudnessQueue().pop (scratch.data(), maxPointsPerFrame);

        if (numRead <= 0)
            break;

        for (int i = 0; i < numRead; ++i)
        {
            history[static_cast<size_t> (writeIndex)] = scratch[static_cast<size_t> (i)].momentaryDb;
            writeIndex = (writeIndex + 1) % ringSize;
            validCount = juce::jmin (validCount + 1, ringSize);
        }

        targetDb = scratch[static_cast<size_t> (numRead - 1)].momentaryDb;
        totalRead += numRead;

        if (numRead < maxPointsPerFrame)
            break;
    }

    const auto previous = smoothedDb;
    smoothedDb += (targetDb - smoothedDb) * readoutAlpha;

    if (totalRead > 0 || std::abs (smoothedDb - previous) > 0.01f)
        repaint();
}

float LoudnessHistoryView::toY (float db) const noexcept
{
    return static_cast<float> (getHeight())
         * juce::jlimit (0.0f, 1.0f, (db - dbCeiling) / (dbFloor - dbCeiling));
}

void LoudnessHistoryView::drawChartPaper (juce::Graphics& g)
{
    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());

    g.setFont (Theme::mono (9.0f));

    // Level rules every 12 dB. The edge rules sit on the bezel, so only the
    // interior ones carry labels.
    for (auto db = dbCeiling; db >= dbFloor; db -= 12.0f)
    {
        const auto y = toY (db);

        g.setColour (Theme::palette().grid.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (y), 0.0f, width);

        if (db < dbCeiling && db > dbFloor)
        {
            g.setColour (Theme::palette().boneDim.withAlpha (0.55f));
            g.drawText (juce::String (static_cast<int> (db)),
                        juce::Rectangle<float> (width - 34.0f, y + 1.0f, 30.0f, 11.0f),
                        juce::Justification::centredRight);
        }
    }

    // Time rules every 10 s, counted back from the pen at the right edge.
    for (int seconds = 10; seconds < historySeconds; seconds += 10)
    {
        const auto x = width * (1.0f - static_cast<float> (seconds) / historySeconds);

        g.setColour (Theme::palette().grid.withAlpha (0.5f));
        g.drawVerticalLine (juce::roundToInt (x), 0.0f, height);

        g.setColour (Theme::palette().boneDim.withAlpha (0.55f));
        g.drawText (juce::String (seconds) + "S",
                    juce::Rectangle<float> (x + 3.0f, height - 13.0f, 30.0f, 11.0f),
                    juce::Justification::centredLeft);
    }

    // The hot band: sustained level above -9 dB is worth a glance even before
    // anything clips.
    const auto hotY = toY (hotDb);

    g.setColour (Theme::palette().rust.withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, width, hotY));

    g.setColour (Theme::palette().rust.withAlpha (0.5f));
    g.drawHorizontalLine (juce::roundToInt (hotY), 0.0f, width);
}

void LoudnessHistoryView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::palette().screenBlack);
    drawChartPaper (g);

    const auto width = getWidth();
    const auto height = static_cast<float> (getHeight());

    if (width <= 1 || ringSize <= 0 || validCount <= 0)
        return;

    // Per pixel column, the loudest hop in that column's slice of time. Max
    // rather than mean: the pen registers the swing, so a one-hop transient
    // survives being squeezed into a sixty-second chart.
    juce::Path trace;
    auto penDown = false;
    auto firstX = 0.0f;
    auto lastX = 0.0f;

    const auto oldestValid = ringSize - validCount;

    for (int x = 0; x < width; ++x)
    {
        const auto hopLow  = static_cast<int> (static_cast<juce::int64> (x) * ringSize / width);
        const auto hopHigh = juce::jmax (hopLow + 1,
                                         static_cast<int> (static_cast<juce::int64> (x + 1) * ringSize / width));

        auto columnDb = -1000.0f;

        for (int hop = juce::jmax (hopLow, oldestValid); hop < juce::jmin (hopHigh, ringSize); ++hop)
            columnDb = juce::jmax (columnDb, history[static_cast<size_t> ((writeIndex + hop) % ringSize)]);

        if (columnDb <= -999.0f)
            continue;   // this stretch of paper has not been written yet

        const auto fx = static_cast<float> (x);
        const auto y = toY (columnDb);

        if (! penDown)
        {
            trace.startNewSubPath (fx, y);
            penDown = true;
            firstX = fx;
        }
        else
        {
            trace.lineTo (fx, y);
        }

        lastX = fx;
    }

    if (! penDown)
        return;

    // Fill under the trace, dimmest at the floor.
    {
        auto fill = trace;
        fill.lineTo (lastX, height);
        fill.lineTo (firstX, height);
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (Theme::palette().amber.withAlpha (0.18f), 0.0f, 0.0f,
                                                 Theme::palette().amber.withAlpha (0.03f), 0.0f, height,
                                                 false));
        g.fillPath (fill);
    }

    // The trace itself: wide dim pass, then narrow bright — phosphor bloom.
    g.setColour (Theme::palette().amber.withAlpha (0.25f));
    g.strokePath (trace, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Theme::palette().amberBright.withAlpha (0.9f));
    g.strokePath (trace, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto w = static_cast<float> (width);

    g.setColour (Theme::palette().amberBright);
    g.setFont (Theme::mono (12.0f, true));
    g.drawText (juce::String (smoothedDb, 1) + " DB",
                juce::Rectangle<float> (w - 110.0f, 6.0f, 100.0f, 14.0f),
                juce::Justification::centredRight);

    g.setColour (Theme::palette().boneDim);
    g.setFont (Theme::mono (8.0f));
    g.drawText (Theme::spaced ("MOMENTARY"),
                juce::Rectangle<float> (w - 110.0f, 21.0f, 100.0f, 10.0f),
                juce::Justification::centredRight);
}
