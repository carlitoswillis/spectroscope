#include "SpectrumView.h"
#include "Theme.h"
#include "../dsp/StftAnalyzer.h"

SpectrumView::SpectrumView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);
    engine.addConsumer();
}

SpectrumView::~SpectrumView()
{
    engine.removeConsumer();
}

void SpectrumView::setActive (bool shouldBeActive)
{
    if (shouldBeActive == isTimerRunning())
        return;

    if (shouldBeActive)
    {
        engine.getSpectrumColumns().discardPending();

        // Start from silence rather than whatever the last look held.
        std::fill (averaged.begin(), averaged.end(), StftAnalyzer::floorDb);
        std::fill (peakHold.begin(), peakHold.end(), StftAnalyzer::floorDb);

        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

void SpectrumView::timerCallback()
{
    const auto engineBins = engine.getNumBins();

    if (engineBins <= 1)
        return;

    if (engineBins != numBins)
    {
        numBins = engineBins;
        scratch.assign (static_cast<size_t> (numBins) * maxColumnsPerFrame, StftAnalyzer::floorDb);
        averaged.assign (static_cast<size_t> (numBins), StftAnalyzer::floorDb);
        peakHold.assign (static_cast<size_t> (numBins), StftAnalyzer::floorDb);
    }

    auto totalRead = 0;

    for (;;)
    {
        const auto numRead = engine.getSpectrumColumns().pop (scratch.data(), maxColumnsPerFrame);

        if (numRead <= 0)
            break;

        for (int i = 0; i < numRead; ++i)
        {
            const auto* column = scratch.data() + static_cast<std::ptrdiff_t> (i) * numBins;

            for (int bin = 0; bin < numBins; ++bin)
            {
                const auto b = static_cast<size_t> (bin);

                averaged[b] += (column[bin] - averaged[b]) * averageAlpha;
                peakHold[b] = juce::jmax (column[bin], peakHold[b] - peakDecayDbPerColumn);
            }
        }

        totalRead += numRead;

        if (numRead < maxColumnsPerFrame)
            break;
    }

    if (totalRead > 0)
        repaint();
}

float SpectrumView::levelAt (const std::vector<float>& source, float binLow, float binHigh) const
{
    const auto lo = juce::jlimit (0, numBins - 1, static_cast<int> (binLow));
    const auto hi = juce::jlimit (lo, numBins - 1, static_cast<int> (binHigh));

    if (hi - lo > 1)
    {
        auto peak = source[static_cast<size_t> (lo)];

        for (int bin = lo + 1; bin <= hi; ++bin)
            peak = juce::jmax (peak, source[static_cast<size_t> (bin)]);

        return peak;
    }

    const auto centre = (binLow + binHigh) * 0.5f;
    const auto bin = juce::jlimit (0, numBins - 2, static_cast<int> (centre));
    const auto frac = juce::jlimit (0.0f, 1.0f, centre - static_cast<float> (bin));
    const auto b = static_cast<size_t> (bin);

    return source[b] + (source[b + 1] - source[b]) * frac;
}

void SpectrumView::drawGraticule (juce::Graphics& g)
{
    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());
    const auto nyquist = engine.getSampleRate() * 0.5;

    g.setFont (Theme::mono (9.0f));

    // Level rules every 15 dB.
    for (auto db = dbCeiling - 15.0f; db > dbFloor; db -= 15.0f)
    {
        const auto y = height * (db - dbCeiling) / (dbFloor - dbCeiling);

        g.setColour (Theme::palette().grid.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (y), 0.0f, width);

        g.setColour (Theme::palette().boneDim.withAlpha (0.55f));
        g.drawText (juce::String (static_cast<int> (db)),
                    juce::Rectangle<float> (width - 34.0f, y + 1.0f, 30.0f, 11.0f),
                    juce::Justification::centredRight);
    }

    if (nyquist <= minFrequencyHz)
        return;

    // The same landmark frequencies the spectrogram rules, upright here.
    const auto logSpan = std::log (nyquist / minFrequencyHz);

    struct Mark { double hz; const char* label; };

    const Mark marks[] =
    {
        { 60.0, "60" }, { 125.0, "125" }, { 250.0, "250" }, { 500.0, "500" },
        { 1000.0, "1K" }, { 2000.0, "2K" }, { 4000.0, "4K" }, { 8000.0, "8K" },
        { 16000.0, "16K" },
    };

    for (const auto& mark : marks)
    {
        if (mark.hz <= minFrequencyHz || mark.hz >= nyquist)
            continue;

        const auto x = width * static_cast<float> (std::log (mark.hz / minFrequencyHz) / logSpan);

        g.setColour (Theme::palette().grid.withAlpha (0.5f));
        g.drawVerticalLine (juce::roundToInt (x), 0.0f, height);

        g.setColour (Theme::palette().amber.withAlpha (0.5f));
        g.drawText (mark.label,
                    juce::Rectangle<float> (x + 3.0f, height - 13.0f, 30.0f, 11.0f),
                    juce::Justification::centredLeft);
    }
}

void SpectrumView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::palette().screenBlack);
    drawGraticule (g);

    const auto width = getWidth();
    const auto height = static_cast<float> (getHeight());
    const auto nyquist = engine.getSampleRate() * 0.5;

    if (numBins <= 1 || width <= 1 || nyquist <= minFrequencyHz)
        return;

    const auto logSpan = std::log (nyquist / minFrequencyHz);
    const auto binsPerHz = (numBins - 1) / nyquist;

    const auto toY = [height] (float db)
    {
        return height * juce::jlimit (0.0f, 1.0f, (db - dbCeiling) / (dbFloor - dbCeiling));
    };

    // One x-step per pixel, same span logic as the spectrogram's rows.
    std::vector<float> avgDb (static_cast<size_t> (width));
    std::vector<float> peakDb (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
    {
        const auto fLow  = minFrequencyHz * std::exp (static_cast<double> (x) / width * logSpan);
        const auto fHigh = minFrequencyHz * std::exp (static_cast<double> (x + 1) / width * logSpan);

        const auto binLow  = static_cast<float> (fLow * binsPerHz);
        const auto binHigh = static_cast<float> (fHigh * binsPerHz);

        avgDb[static_cast<size_t> (x)]  = levelAt (averaged, binLow, binHigh);
        peakDb[static_cast<size_t> (x)] = levelAt (peakHold, binLow, binHigh);
    }

    // A short moving average across frequency calms the bin-level jitter that
    // fuzzes the trace above 1 kHz, where each pixel spans many bins. The
    // peak-hold ghost stays raw — it is the precision instrument of the two.
    {
        constexpr int radius = 2;
        std::vector<float> raw (avgDb);

        for (int x = 0; x < width; ++x)
        {
            auto sum = 0.0f;
            auto n = 0;

            for (int j = juce::jmax (0, x - radius); j <= juce::jmin (width - 1, x + radius); ++j)
            {
                sum += raw[static_cast<size_t> (j)];
                ++n;
            }

            avgDb[static_cast<size_t> (x)] = sum / static_cast<float> (n);
        }
    }

    juce::Path average, peaks;

    for (int x = 0; x < width; ++x)
    {
        const auto fx = static_cast<float> (x);
        const auto yAvg = toY (avgDb[static_cast<size_t> (x)]);
        const auto yPeak = toY (peakDb[static_cast<size_t> (x)]);

        if (x == 0)
        {
            average.startNewSubPath (fx, yAvg);
            peaks.startNewSubPath (fx, yPeak);
        }
        else
        {
            average.lineTo (fx, yAvg);
            peaks.lineTo (fx, yPeak);
        }
    }

    // Peak-hold first: a ghost behind the live trace, bright enough to read
    // resonances off, dim enough never to compete.
    g.setColour (Theme::palette().amberDim.withAlpha (0.75f));
    g.strokePath (peaks, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

    // Fill under the averaged curve, dimmest at the floor.
    {
        auto fill = average;
        fill.lineTo (static_cast<float> (width - 1), height);
        fill.lineTo (0.0f, height);
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (Theme::palette().amber.withAlpha (0.28f), 0.0f, 0.0f,
                                                 Theme::palette().amber.withAlpha (0.04f), 0.0f, height,
                                                 false));
        g.fillPath (fill);
    }

    // The trace itself: wide dim pass, then narrow bright — phosphor bloom.
    g.setColour (Theme::palette().amber.withAlpha (0.25f));
    g.strokePath (average, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Theme::palette().amberBright.withAlpha (0.9f));
    g.strokePath (average, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
