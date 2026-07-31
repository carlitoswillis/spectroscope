#include "SpectrogramView.h"

SpectrogramView::SpectrogramView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);

    engine.addConsumer();
    engine.getSpectrumColumns().discardPending();

    startTimerHz (60);
}

SpectrogramView::~SpectrogramView()
{
    engine.removeConsumer();
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

    if (numBins <= 0)
        return;

    // Row 0 is the top of the view and the top of the spectrum. Each row covers
    // a span of bins; taking the maximum across that span means a narrow peak
    // survives downsampling instead of being averaged away.
    for (int y = 0; y < h; ++y)
    {
        const auto upper = 1.0 - static_cast<double> (y) / h;
        const auto lower = 1.0 - static_cast<double> (y + 1) / h;

        auto binHigh = juce::roundToInt (upper * (numBins - 1));
        auto binLow  = juce::roundToInt (lower * (numBins - 1));

        binLow  = juce::jlimit (0, numBins - 1, binLow);
        binHigh = juce::jlimit (binLow, numBins - 1, binHigh);

        rowBinLow[static_cast<size_t> (y)]  = binLow;
        rowBinHigh[static_cast<size_t> (y)] = binHigh;
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

        auto peak = column[low];

        for (int bin = low + 1; bin <= high; ++bin)
            peak = juce::jmax (peak, column[bin]);

        const auto normalised = juce::jlimit (0.0f, 1.0f, (peak - dbFloor) / range);
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
    g.fillAll (juce::Colour (0xff0a0a0e));

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
}
