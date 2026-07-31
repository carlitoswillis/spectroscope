#include "SpectrogramView.h"
#include "Theme.h"

SpectrogramView::SpectrogramView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);

    colourTable = Theme::palette().spectrogramTable;

    engine.addConsumer();
    engine.getSpectrumColumns().discardPending();

    startTimerHz (60);
}

SpectrogramView::~SpectrogramView()
{
    engine.removeConsumer();
}

void SpectrogramView::themeChanged()
{
    colourTable = Theme::palette().spectrogramTable;
    reRenderAllHistory();
    repaint();
}

void SpectrogramView::setActive (bool shouldBeActive)
{
    if (shouldBeActive == isTimerRunning())
        return;

    if (shouldBeActive)
    {
        // Columns that arrived while the other view owned the queue belong to
        // its timeline, not this one's.
        engine.getSpectrumColumns().discardPending();
        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

void SpectrogramView::setDecibelRange (float floorDb, float ceilingDb)
{
    dbFloor = floorDb;
    dbCeiling = juce::jmax (floorDb + 1.0f, ceilingDb);
    reRenderAllHistory();
    repaint();
}

void SpectrogramView::resized()
{
    rebuildImage();
    rebuildRowMapping();
    reRenderAllHistory();
}

void SpectrogramView::rebuildImage()
{
    const auto w = juce::jmax (1, getWidth());
    const auto h = juce::jmax (1, getHeight());

    image = juce::Image (juce::Image::ARGB, w, h, true);
    imageWrite = 0;
}

void SpectrogramView::rebuildRowMapping()
{
    const auto h = juce::jmax (1, getHeight());

    rowBinLow.assign (static_cast<size_t> (h), 0);
    rowBinHigh.assign (static_cast<size_t> (h), 0);
    rowBinCentre.assign (static_cast<size_t> (h), 0.0f);

    if (numBins <= 0)
        return;

    const auto nyquist = engine.getSampleRate() * 0.5;
    lastMappedRate = engine.getSampleRate();

    // Row 0 is the top of the view and the top of the spectrum. Each row covers
    // a span of bins; taking the maximum across that span means a narrow peak
    // survives downsampling instead of being averaged away.
    //
    // The axis is logarithmic from minFrequencyHz to Nyquist, so each octave
    // gets the same vertical span. Until the sample rate is known there is no
    // Hz-to-bin mapping, so fall back to linear-by-bin.
    const auto logSpan = nyquist > minFrequencyHz ? std::log (nyquist / minFrequencyHz) : 0.0;

    for (int y = 0; y < h; ++y)
    {
        const auto upper = 1.0 - static_cast<double> (y) / h;
        const auto lower = 1.0 - static_cast<double> (y + 1) / h;

        int binLow = 0, binHigh = 0;
        auto binCentre = 0.0;

        if (logSpan > 0.0)
        {
            const auto fHigh = minFrequencyHz * std::exp (upper * logSpan);
            const auto fLow  = minFrequencyHz * std::exp (lower * logSpan);

            binHigh = juce::roundToInt (fHigh / nyquist * (numBins - 1));
            binLow  = juce::roundToInt (fLow  / nyquist * (numBins - 1));

            // Geometric mean: the row's centre on a log axis.
            binCentre = std::sqrt (fLow * fHigh) / nyquist * (numBins - 1);
        }
        else
        {
            binHigh = juce::roundToInt (upper * (numBins - 1));
            binLow  = juce::roundToInt (lower * (numBins - 1));
            binCentre = (upper + lower) * 0.5 * (numBins - 1);
        }

        binLow  = juce::jlimit (0, numBins - 1, binLow);
        binHigh = juce::jlimit (binLow, numBins - 1, binHigh);

        rowBinLow[static_cast<size_t> (y)]  = binLow;
        rowBinHigh[static_cast<size_t> (y)] = binHigh;
        rowBinCentre[static_cast<size_t> (y)] =
            static_cast<float> (juce::jlimit (0.0, static_cast<double> (numBins - 1), binCentre));
    }
}

void SpectrogramView::renderColumnIntoImage (const float* column, int imageColumn)
{
    const auto h = image.getHeight();

    if (h <= 0 || numBins <= 0 || rowBinLow.size() != static_cast<size_t> (h))
        return;

    const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);
    const auto range = juce::jmax (1.0f, dbCeiling - dbFloor);

    for (int y = 0; y < h; ++y)
    {
        const auto low = rowBinLow[static_cast<size_t> (y)];
        const auto high = rowBinHigh[static_cast<size_t> (y)];

        float level;

        if (high - low > 1)
        {
            // Many bins per row: the maximum keeps narrow peaks alive.
            level = column[low];

            for (int bin = low + 1; bin <= high; ++bin)
                level = juce::jmax (level, column[bin]);
        }
        else
        {
            // Many rows per bin: interpolating at the row's centre frequency
            // turns what would be flat repeated stripes into a gradient.
            const auto centre = rowBinCentre[static_cast<size_t> (y)];
            const auto bin = juce::jlimit (0, numBins - 2, static_cast<int> (centre));
            const auto frac = juce::jlimit (0.0f, 1.0f, centre - static_cast<float> (bin));

            level = column[bin] + (column[bin + 1] - column[bin]) * frac;
        }

        auto normalised = juce::jlimit (0.0f, 1.0f, (level - dbFloor) / range);

        // Gamma pushes the broadband mids back down the ramp, so sustained
        // material reads as structure over a dark ground instead of an even
        // orange wash.
        normalised = std::pow (normalised, 1.3f);

        const auto index = static_cast<size_t> (juce::jlimit (0, 255, juce::roundToInt (normalised * 255.0f)));

        *reinterpret_cast<juce::PixelARGB*> (bitmap.getPixelPointer (imageColumn, y)) = colourTable[index];
    }
}

void SpectrogramView::reRenderAllHistory()
{
    if (numBins <= 0 || numStored <= 0 || image.isNull())
        return;

    const auto width = image.getWidth();
    const auto columnsToDraw = juce::jmin (width, numStored);

    // Oldest of the visible columns first, so the ring ends up with the newest
    // column immediately behind imageWrite.
    for (int i = 0; i < columnsToDraw; ++i)
    {
        const auto age = columnsToDraw - 1 - i;
        auto index = historyWrite - 1 - age;

        while (index < 0)
            index += historyColumns;

        const auto* column = history.data()
                           + static_cast<std::ptrdiff_t> (index % historyColumns) * numBins;

        renderColumnIntoImage (column, i);
    }

    imageWrite = columnsToDraw % width;
}

void SpectrogramView::timerCallback()
{
    const auto engineBins = engine.getNumBins();

    if (engineBins <= 1)
        return;

    // The Hz-to-row mapping depends on the sample rate, which may only become
    // known (or change) after the view is laid out.
    if (engine.getSampleRate() != lastMappedRate && engineBins == numBins)
    {
        rebuildRowMapping();
        reRenderAllHistory();
    }

    if (engineBins != numBins)
    {
        numBins = engineBins;
        history.assign (static_cast<size_t> (numBins) * historyColumns, StftAnalyzer::floorDb);
        scratch.assign (static_cast<size_t> (numBins) * maxColumnsPerFrame, StftAnalyzer::floorDb);
        historyWrite = 0;
        numStored = 0;

        rebuildImage();
        rebuildRowMapping();
    }

    if (image.isNull())
        return;

    const auto width = image.getWidth();
    auto totalRead = 0;

    for (;;)
    {
        const auto numRead = engine.getSpectrumColumns().pop (scratch.data(), maxColumnsPerFrame);

        if (numRead <= 0)
            break;

        for (int i = 0; i < numRead; ++i)
        {
            const auto* column = scratch.data() + static_cast<std::ptrdiff_t> (i) * numBins;

            std::copy (column, column + numBins,
                       history.begin() + static_cast<std::ptrdiff_t> (historyWrite) * numBins);

            historyWrite = (historyWrite + 1) % historyColumns;

            renderColumnIntoImage (column, imageWrite);
            imageWrite = (imageWrite + 1) % width;
        }

        totalRead += numRead;

        if (numRead < maxColumnsPerFrame)
            break;
    }

    if (totalRead > 0)
    {
        numStored = juce::jmin (historyColumns, numStored + totalRead);
        repaint();
    }
}

void SpectrogramView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::palette().screenBlack);

    if (image.isNull())
        return;

    const auto width = image.getWidth();
    const auto height = image.getHeight();

    // Two blits with a shifting split point: the ring's write head is the
    // right-hand edge, so nothing has to be moved to make room for new columns.
    const auto tailWidth = width - imageWrite;

    if (tailWidth > 0)
        g.drawImage (image, 0, 0, tailWidth, height,
                     imageWrite, 0, tailWidth, height);

    if (imageWrite > 0)
        g.drawImage (image, tailWidth, 0, imageWrite, height,
                     0, 0, imageWrite, height);

    drawFrequencyGrid (g);
}

void SpectrogramView::drawFrequencyGrid (juce::Graphics& g)
{
    const auto nyquist = engine.getSampleRate() * 0.5;

    if (nyquist <= 0.0)
        return;

    const auto height = static_cast<float> (getHeight());
    const auto width = static_cast<float> (getWidth());

    g.setFont (Theme::mono (9.0f));

    // Octave-spaced rules with labels on the audiographic landmarks. On a log
    // axis these come out evenly spaced, like the ruled lines on chart paper.
    const auto logSpan = std::log (nyquist / minFrequencyHz);

    struct Mark { double hz; const char* label; };

    const Mark marks[] =
    {
        { 60.0,     "60" },
        { 125.0,    "125" },
        { 250.0,    "250" },
        { 500.0,    "500" },
        { 1000.0,   "1K" },
        { 2000.0,   "2K" },
        { 4000.0,   "4K" },
        { 8000.0,   "8K" },
        { 16000.0,  "16K" },
    };

    for (const auto& mark : marks)
    {
        if (mark.hz <= minFrequencyHz || mark.hz >= nyquist)
            continue;

        const auto y = height * (1.0f - static_cast<float> (std::log (mark.hz / minFrequencyHz) / logSpan));

        g.setColour (Theme::palette().amber.withAlpha (0.13f));
        g.drawHorizontalLine (juce::roundToInt (y), 0.0f, width);

        g.setColour (Theme::palette().amber.withAlpha (0.5f));
        g.drawText (mark.label,
                    juce::Rectangle<float> (4.0f, y + 1.0f, 34.0f, 11.0f),
                    juce::Justification::centredLeft);
    }
}
