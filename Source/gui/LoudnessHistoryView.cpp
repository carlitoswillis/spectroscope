#include "LoudnessHistoryView.h"
#include "Theme.h"
#include "../dsp/LoudnessMeter.h"

#include <algorithm>

namespace
{
    struct LoudnessTarget
    {
        const char* name;
        float lufs;         // ignored for OFF
    };

    // Broadcast first, streaming after; OFF keeps the recorder a plain chart.
    constexpr LoudnessTarget loudnessTargets[] =
    {
        { "OFF",         0.0f },
        { "EBU -23",   -23.0f },
        { "SPOTIFY -14", -14.0f },
        { "YOUTUBE -14", -14.0f },
        { "ATSC -24",  -24.0f },
    };

    constexpr int numLoudnessTargets = static_cast<int> (sizeof (loudnessTargets) / sizeof (loudnessTargets[0]));
}

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
        // new with an invisible seam. Restart the chart instead — markers
        // stamped on the torn-off paper go with it. The meter's integration
        // keeps running: hiding a pane is not the end of the programme.
        writeIndex = 0;
        validCount = 0;
        markers.clear();
        markerCounter = 0;

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
    engine.resetLoudness();

    std::fill (history.begin(), history.end(), -100.0f);
    writeIndex = 0;
    validCount = 0;
    markers.clear();
    markerCounter = 0;
    repaint();
}

void LoudnessHistoryView::addMarker()
{
    if (validCount <= 0)
        return;   // no paper written yet, nothing to stamp

    if (static_cast<int> (markers.size()) >= maxMarkers)
        markers.erase (markers.begin());

    markers.push_back ({ totalHops, ++markerCounter });
    repaint();
}

void LoudnessHistoryView::cycleTarget()
{
    targetIndex = (targetIndex + 1) % numLoudnessTargets;
    repaint();
}

juce::String LoudnessHistoryView::getTargetName() const
{
    return loudnessTargets[targetIndex].name;
}

void LoudnessHistoryView::appendCsv (juce::String& out) const
{
    out << "seconds,momentary_db\n";

    const auto secondsPerHop = engine.getSecondsPerPoint();

    if (ringSize <= 0 || validCount <= 0 || secondsPerHop <= 0.0)
        return;

    for (int i = 0; i < validCount; ++i)
    {
        const auto chrono = ringSize - validCount + i;
        const auto db = history[static_cast<size_t> ((writeIndex + chrono) % ringSize)];
        const auto seconds = -static_cast<double> (validCount - 1 - i) * secondsPerHop;

        out << juce::String (seconds, 3) << "," << juce::String (db, 2) << "\n";
    }
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
        markers.clear();
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

        totalHops += numRead;
        totalRead += numRead;

        if (numRead < maxPointsPerFrame)
            break;
    }

    // Markers ride the paper; once one scrolls past the left edge it is gone.
    markers.erase (std::remove_if (markers.begin(), markers.end(),
                                   [this] (const Marker& m) { return totalHops - m.hop >= ringSize; }),
                   markers.end());

    const auto& meter = engine.getLoudnessMeter();
    const auto m   = meter.getMomentaryLufs();
    const auto s   = meter.getShortTermLufs();
    const auto in  = meter.getIntegratedLufs();
    const auto lra = meter.getLoudnessRange();
    const auto tp  = meter.getMaxTruePeakDb();

    const auto changed = std::abs (m - momentaryLufs) > 0.01f
                      || std::abs (s - shortTermLufs) > 0.01f
                      || std::abs (in - integratedLufs) > 0.01f
                      || std::abs (lra - loudnessRange) > 0.01f
                      || std::abs (tp - maxTruePeakDb) > 0.01f;

    momentaryLufs = m;
    shortTermLufs = s;
    integratedLufs = in;
    loudnessRange = lra;
    maxTruePeakDb = tp;

    if (totalRead > 0 || changed)
        repaint();
}

float LoudnessHistoryView::toY (float db) const noexcept
{
    return static_cast<float> (getHeight())
         * juce::jlimit (0.0f, 1.0f, (db - dbCeiling) / (dbFloor - dbCeiling));
}

float LoudnessHistoryView::columnDbAt (int x) const noexcept
{
    const auto width = getWidth();

    if (width <= 0 || ringSize <= 0 || validCount <= 0)
        return -1000.0f;

    const auto hopLow  = static_cast<int> (static_cast<juce::int64> (x) * ringSize / width);
    const auto hopHigh = juce::jmax (hopLow + 1,
                                     static_cast<int> (static_cast<juce::int64> (x + 1) * ringSize / width));

    const auto oldestValid = ringSize - validCount;
    auto columnDb = -1000.0f;

    for (int hop = juce::jmax (hopLow, oldestValid); hop < juce::jmin (hopHigh, ringSize); ++hop)
        columnDb = juce::jmax (columnDb, history[static_cast<size_t> ((writeIndex + hop) % ringSize)]);

    return columnDb;
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

    // The target rule: dashed so it reads as an operator's pencil line rather
    // than another printed grid rule.
    if (targetIndex != 0)
    {
        const auto y = toY (loudnessTargets[targetIndex].lufs);
        const float dashes[] = { 6.0f, 4.0f };

        g.setColour (Theme::palette().bone.withAlpha (0.7f));
        g.drawDashedLine (juce::Line<float> (0.0f, y, width, y), dashes, 2, 1.2f);
    }
}

void LoudnessHistoryView::drawMarkers (juce::Graphics& g)
{
    if (ringSize <= 0 || markers.empty())
        return;

    const auto width = static_cast<float> (getWidth());

    g.setFont (Theme::mono (8.0f, true));

    for (const auto& marker : markers)
    {
        const auto age = totalHops - marker.hop;
        const auto chrono = ringSize - 1 - static_cast<int> (age);

        if (chrono < ringSize - validCount)
            continue;   // stamped on paper that has since been torn off

        const auto x = (static_cast<float> (chrono) + 0.5f) * width / static_cast<float> (ringSize);

        g.setColour (Theme::palette().mustard);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 1.0f, 8.0f));
        g.drawText (juce::String (marker.number),
                    juce::Rectangle<float> (x + 2.0f, 0.0f, 22.0f, 10.0f),
                    juce::Justification::centredLeft);
    }
}

void LoudnessHistoryView::drawReadout (juce::Graphics& g)
{
    const auto w = static_cast<float> (getWidth());
    const auto hasData = validCount > 0;

    // After a clear with the transport stopped, the meter atomics keep their
    // stale figures until the next hop consumes the reset — so an empty chart
    // overrides them and reads "---" across the board.
    const auto fmt = [hasData] (float value)
    {
        return hasData && value > -100.0f ? juce::String (value, 1) : juce::String ("---");
    };

    const auto blockLeft = w - 118.0f;
    const auto blockRight = w - 10.0f;
    const auto blockWidth = blockRight - blockLeft;

    // Backing keeps the figures legible when the pen swings underneath them.
    g.setColour (Theme::palette().screenBlack.withAlpha (0.65f));
    g.fillRect (juce::Rectangle<float> (blockLeft - 4.0f, 4.0f, blockWidth + 8.0f, 66.0f));

    g.setColour (Theme::palette().amberBright);
    g.setFont (Theme::mono (11.0f, true));
    g.drawText ("M " + fmt (momentaryLufs) + " LUFS",
                juce::Rectangle<float> (blockLeft, 6.0f, blockWidth, 13.0f),
                juce::Justification::centredRight);

    g.setFont (Theme::mono (10.0f));

    const auto hasIntegrated = hasData && integratedLufs > -100.0f;
    auto lineY = 20.0f;

    const auto drawLine = [&] (const juce::String& text, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawText (text,
                    juce::Rectangle<float> (blockLeft, lineY, blockWidth, 12.0f),
                    juce::Justification::centredRight);
        lineY += 12.0f;
    };

    drawLine ("S " + fmt (shortTermLufs), Theme::palette().bone);
    drawLine ("I " + fmt (integratedLufs), Theme::palette().bone);
    drawLine ("LRA " + (hasIntegrated ? juce::String (loudnessRange, 1) : juce::String ("---")) + " LU",
              Theme::palette().bone);

    // True peak over the -1 dBTP ceiling is a fail condition, so it goes rust
    // before the verdict does.
    const auto tpHot = hasData && maxTruePeakDb > -1.0f;
    drawLine ("TP " + fmt (maxTruePeakDb) + " DBTP",
              tpHot ? Theme::palette().rust : Theme::palette().bone);

    // The target plate: the view's own switch, cycled by clicking it.
    targetPlate = juce::Rectangle<float> (blockRight - 116.0f, lineY + 4.0f, 116.0f, 15.0f);

    g.setColour (Theme::palette().shellMid);
    g.fillRect (targetPlate);

    g.setColour (targetIndex == 0 ? Theme::palette().boneDim : Theme::palette().bone);
    g.setFont (Theme::mono (8.0f, true));
    g.drawText (Theme::spaced (getTargetName()), targetPlate, juce::Justification::centred);

    if (targetIndex == 0)
        return;

    // Verdict: within a LU of target and never over -1 dBTP, or it fails.
    // No integrated figure yet means the jury is still out.
    const auto pass = hasIntegrated
                   && std::abs (integratedLufs - loudnessTargets[targetIndex].lufs) <= 1.0f
                   && maxTruePeakDb <= -1.0f;

    const auto word = hasIntegrated ? (pass ? "PASS" : "FAIL") : "WAIT";
    const auto colour = hasIntegrated ? (pass ? Theme::palette().phosphor : Theme::palette().rust)
                                      : Theme::palette().boneDim;

    Theme::drawLamp (g, juce::Rectangle<float> (blockLeft - 58.0f, 7.0f, 10.0f, 10.0f),
                     colour, hasIntegrated);

    g.setColour (colour);
    g.setFont (Theme::mono (10.0f, true));
    g.drawText (word,
                juce::Rectangle<float> (blockLeft - 46.0f, 5.0f, 40.0f, 14.0f),
                juce::Justification::centredLeft);
}

void LoudnessHistoryView::drawCursor (juce::Graphics& g)
{
    const auto width = getWidth();
    const auto height = static_cast<float> (getHeight());

    if (cursor.x < 0 || cursor.x >= width || ringSize <= 0)
        return;

    const auto fx = static_cast<float> (cursor.x);

    g.setColour (Theme::palette().bone.withAlpha (0.3f));
    g.drawVerticalLine (cursor.x, 0.0f, height);

    const auto seconds = (1.0f - (fx + 0.5f) / static_cast<float> (width)) * historySeconds;
    const auto columnDb = columnDbAt (cursor.x);

    auto text = juce::String (-seconds, 1) + " S";

    if (columnDb > -999.0f)
        text << "  " << juce::String (columnDb, 1) << " DB";

    // The label flips to the other side of the hairline near the right edge.
    const auto onLeft = fx > static_cast<float> (width) - 120.0f;
    const auto textArea = onLeft
        ? juce::Rectangle<float> (fx - 116.0f, height - 26.0f, 110.0f, 12.0f)
        : juce::Rectangle<float> (fx + 6.0f, height - 26.0f, 110.0f, 12.0f);

    g.setColour (Theme::palette().bone);
    g.setFont (Theme::mono (9.0f));
    g.drawText (text, textArea,
                onLeft ? juce::Justification::centredRight : juce::Justification::centredLeft);
}

void LoudnessHistoryView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::palette().screenBlack);
    drawChartPaper (g);

    const auto width = getWidth();
    const auto height = static_cast<float> (getHeight());

    if (width > 1 && ringSize > 0 && validCount > 0)
    {
        // Per pixel column, the loudest hop in that column's slice of time. Max
        // rather than mean: the pen registers the swing, so a one-hop transient
        // survives being squeezed into a sixty-second chart.
        juce::Path trace;
        auto penDown = false;
        auto firstX = 0.0f;
        auto lastX = 0.0f;

        for (int x = 0; x < width; ++x)
        {
            const auto columnDb = columnDbAt (x);

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

        if (penDown)
        {
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
        }
    }

    drawMarkers (g);
    drawReadout (g);
    drawCursor (g);
}

void LoudnessHistoryView::mouseDown (const juce::MouseEvent& event)
{
    if (targetPlate.contains (event.position))
        cycleTarget();
}

void LoudnessHistoryView::mouseMove (const juce::MouseEvent& event)
{
    cursor = event.getPosition();
    setMouseCursor (targetPlate.contains (event.position) ? juce::MouseCursor::PointingHandCursor
                                                          : juce::MouseCursor::NormalCursor);
    repaint();
}

void LoudnessHistoryView::mouseExit (const juce::MouseEvent&)
{
    cursor = { -1, -1 };
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}
