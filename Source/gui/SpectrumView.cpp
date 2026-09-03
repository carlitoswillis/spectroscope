#include "SpectrumView.h"
#include "Theme.h"
#include "../dsp/StftAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>

//==============================================================================
/** Offline whole-file spectrum for the reference overlay. Same FFT size and dB
    scaling as the live analysis, hop of half a window, whole file averaged as
    power per bin — power rather than dB, so silent stretches dim the result
    instead of dragging every bin to the floor.
*/
class SpectrumView::ReferenceAnalysisJob final : public juce::Thread
{
public:
    ReferenceAnalysisJob (SpectrumView& ownerView, const juce::File& fileToAnalyse)
        : juce::Thread ("Spectrum Reference Analysis"),
          owner (ownerView),
          file (fileToAnalyse)
    {
    }

    void run() override
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels < 1)
            return;

        constexpr auto fftSize = 1 << AnalysisEngine::fftOrder;
        constexpr auto hop = fftSize / 2;

        StftAnalyzer stft;
        stft.prepare (AnalysisEngine::fftOrder, hop, reader->sampleRate);

        const auto numBins = stft.getNumBins();
        std::vector<float> columnDb (static_cast<size_t> (numBins), StftAnalyzer::floorDb);
        std::vector<double> powerSum (static_cast<size_t> (numBins), 0.0);
        auto numColumns = 0;

        juce::AudioBuffer<float> block (static_cast<int> (reader->numChannels), hop);
        std::vector<float> mono (static_cast<size_t> (hop));

        for (juce::int64 position = 0;
             position < reader->lengthInSamples && ! threadShouldExit();
             position += hop)
        {
            // Cast hop to int64 up front so both arguments already share a type and
            // jmin can deduce Type=int64 without an explicit template argument. An
            // explicit <juce::int64> forces substitution into JUCE's SIMDRegister
            // overload of jmin too, which instantiates SIMDRegister<int64> even
            // though it's never selected -- and that specialisation doesn't exist
            // for SSE, so it fails to compile on Linux/gcc even though it's fine on
            // AppleClang/NEON.
            const auto numToRead = static_cast<int> (
                juce::jmin (static_cast<juce::int64> (hop), reader->lengthInSamples - position));

            if (! reader->read (&block, 0, numToRead, position, true, true))
                break;

            // Mono sum, matching the mid the live analyser feeds its STFT.
            // The final partial hop stays zero-padded past numToRead.
            std::fill (mono.begin(), mono.end(), 0.0f);

            const auto channelGain = 1.0f / static_cast<float> (block.getNumChannels());

            for (int channel = 0; channel < block.getNumChannels(); ++channel)
            {
                const auto* source = block.getReadPointer (channel);

                for (int i = 0; i < numToRead; ++i)
                    mono[static_cast<size_t> (i)] += source[i] * channelGain;
            }

            if (! stft.processHop (mono.data(), columnDb.data()))
                continue;

            for (int bin = 0; bin < numBins; ++bin)
            {
                const auto gain = juce::Decibels::decibelsToGain (columnDb[static_cast<size_t> (bin)],
                                                                  StftAnalyzer::floorDb);
                powerSum[static_cast<size_t> (bin)] += static_cast<double> (gain) * gain;
            }

            ++numColumns;
        }

        if (threadShouldExit() || numColumns == 0)
            return;

        std::vector<float> resultDb (static_cast<size_t> (numBins), StftAnalyzer::floorDb);

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto meanGain = std::sqrt (powerSum[static_cast<size_t> (bin)] / numColumns);

            resultDb[static_cast<size_t> (bin)] = juce::jmax (
                StftAnalyzer::floorDb,
                juce::Decibels::gainToDecibels (static_cast<float> (meanGain), StftAnalyzer::floorDb));
        }

        {
            const juce::ScopedLock lock (owner.referenceLock);
            owner.pendingReferenceDb = std::move (resultDb);
            owner.pendingReferenceRate = reader->sampleRate;
        }

        // The view outlives this thread — its destructor stops the job before
        // any member goes away, and the AsyncUpdater cancels pending updates.
        owner.triggerAsyncUpdate();
    }

private:
    SpectrumView& owner;
    juce::File file;
};

//==============================================================================
SpectrumView::SpectrumView (AnalysisEngine& e)
    : engine (e)
{
    setOpaque (true);
    engine.addConsumer();
}

SpectrumView::~SpectrumView()
{
    // Stop the reference job before any member it touches is destroyed.
    if (referenceJob != nullptr)
        referenceJob->stopThread (4000);

    engine.removeConsumer();
}

void SpectrumView::setActive (bool shouldBeActive)
{
    if (shouldBeActive == isTimerRunning())
        return;

    if (shouldBeActive)
    {
        engine.getAnalyserColumns().discardPending();
        engine.getSideSpectrumColumns().discardPending();

        // Start from silence rather than whatever the last look held.
        std::fill (averaged.begin(), averaged.end(), StftAnalyzer::floorDb);
        std::fill (peakHold.begin(), peakHold.end(), StftAnalyzer::floorDb);
        std::fill (averagedSide.begin(), averagedSide.end(), StftAnalyzer::floorDb);

        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

void SpectrumView::clear()
{
    engine.getAnalyserColumns().discardPending();
    engine.getSideSpectrumColumns().discardPending();

    std::fill (averaged.begin(), averaged.end(), StftAnalyzer::floorDb);
    std::fill (peakHold.begin(), peakHold.end(), StftAnalyzer::floorDb);
    std::fill (averagedSide.begin(), averagedSide.end(), StftAnalyzer::floorDb);

    // The snapshot is history, so CLR takes it; the reference curve is not,
    // so it stays.
    storedAveraged.clear();
    storedSide.clear();

    repaint();
}

void SpectrumView::toggleStore()
{
    if (! storedAveraged.empty())
    {
        storedAveraged.clear();
        storedSide.clear();
    }
    else if (numBins > 1)
    {
        storedAveraged = averaged;
        storedSide = averagedSide;
    }

    repaint();
}

bool SpectrumView::hasStoredTrace() const
{
    return ! storedAveraged.empty();
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
        sideScratch.assign (static_cast<size_t> (numBins) * maxColumnsPerFrame, StftAnalyzer::floorDb);
        averagedSide.assign (static_cast<size_t> (numBins), StftAnalyzer::floorDb);

        // A snapshot taken at the old bin count no longer lines up.
        storedAveraged.clear();
        storedSide.clear();
    }

    auto totalRead = 0;

    for (;;)
    {
        const auto numRead = engine.getAnalyserColumns().pop (scratch.data(), maxColumnsPerFrame);

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

    // The side columns arrive at the same cadence but through their own queue,
    // so they get their own drain loop rather than lockstep pops.
    for (;;)
    {
        const auto numRead = engine.getSideSpectrumColumns().pop (sideScratch.data(), maxColumnsPerFrame);

        if (numRead <= 0)
            break;

        for (int i = 0; i < numRead; ++i)
        {
            const auto* column = sideScratch.data() + static_cast<std::ptrdiff_t> (i) * numBins;

            for (int bin = 0; bin < numBins; ++bin)
            {
                const auto b = static_cast<size_t> (bin);

                averagedSide[b] += (column[bin] - averagedSide[b]) * averageAlpha;
            }
        }

        totalRead += numRead;

        if (numRead < maxColumnsPerFrame)
            break;
    }

    if (totalRead > 0)
        repaint();
}

void SpectrumView::handleAsyncUpdate()
{
    {
        const juce::ScopedLock lock (referenceLock);
        referenceDb.swap (pendingReferenceDb);
        referenceRate = pendingReferenceRate;
        pendingReferenceDb.clear();
    }

    repaint();
}

//==============================================================================
void SpectrumView::mouseMove (const juce::MouseEvent& event)
{
    cursorPosition = event.getPosition();
    cursorVisible = true;
    repaint();
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    cursorVisible = false;
    repaint();
}

void SpectrumView::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (noteToggleBounds().contains (position))
    {
        showNoteGrid = ! showNoteGrid;
        repaint();
    }
    else if (tiltToggleBounds().contains (position))
    {
        showTiltGuides = ! showTiltGuides;
        repaint();
    }
    else if (! referenceDb.empty() && refTagBounds().contains (position))
    {
        // Non-blocking: a job still reading would only republish, and the
        // rare late result simply reinstates what was just dropped.
        if (referenceJob != nullptr)
            referenceJob->signalThreadShouldExit();

        referenceDb.clear();
        referenceRate = 0.0;
        repaint();
    }
}

//==============================================================================
bool SpectrumView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.size() != 1)
        return false;

    const auto& file = files.getReference (0);

    return file.endsWithIgnoreCase (".wav")
        || file.endsWithIgnoreCase (".aif")
        || file.endsWithIgnoreCase (".aiff");
}

void SpectrumView::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void SpectrumView::fileDragExit (const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void SpectrumView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;

    if (! isInterestedInFileDrag (files))
    {
        repaint();
        return;
    }

    // A second drop replaces the first: the old job stops at its next hop
    // boundary, so this never stalls the message thread for long.
    if (referenceJob != nullptr)
        referenceJob->stopThread (4000);

    referenceJob = std::make_unique<ReferenceAnalysisJob> (*this, juce::File (files[0]));
    referenceJob->startThread (juce::Thread::Priority::low);
    repaint();
}

//==============================================================================
juce::Rectangle<int> SpectrumView::noteToggleBounds() const
{
    return { getWidth() - 70, 6, 30, 12 };
}

juce::Rectangle<int> SpectrumView::tiltToggleBounds() const
{
    return { getWidth() - 36, 6, 30, 12 };
}

juce::Rectangle<int> SpectrumView::refTagBounds() const
{
    return { getWidth() - 36, 22, 30, 12 };
}

//==============================================================================
float SpectrumView::levelAt (const std::vector<float>& source, int sourceNumBins,
                             float binLow, float binHigh) const
{
    const auto lo = juce::jlimit (0, sourceNumBins - 1, static_cast<int> (binLow));
    const auto hi = juce::jlimit (lo, sourceNumBins - 1, static_cast<int> (binHigh));

    if (hi - lo > 1)
    {
        auto peak = source[static_cast<size_t> (lo)];

        for (int bin = lo + 1; bin <= hi; ++bin)
            peak = juce::jmax (peak, source[static_cast<size_t> (bin)]);

        return peak;
    }

    const auto centre = (binLow + binHigh) * 0.5f;
    const auto bin = juce::jlimit (0, sourceNumBins - 2, static_cast<int> (centre));
    const auto frac = juce::jlimit (0.0f, 1.0f, centre - static_cast<float> (bin));
    const auto b = static_cast<size_t> (bin);

    return source[b] + (source[b + 1] - source[b]) * frac;
}

void SpectrumView::buildPixelCurve (const std::vector<float>& source, int sourceNumBins,
                                    double binsPerHz, double logSpan, std::vector<float>& dest)
{
    const auto width = getWidth();

    dest.resize (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
    {
        const auto fLow  = minFrequencyHz * std::exp (static_cast<double> (x) / width * logSpan);
        const auto fHigh = minFrequencyHz * std::exp (static_cast<double> (x + 1) / width * logSpan);

        dest[static_cast<size_t> (x)] = levelAt (source, sourceNumBins,
                                                 static_cast<float> (fLow * binsPerHz),
                                                 static_cast<float> (fHigh * binsPerHz));
    }

    constexpr int radius = 2;
    smoothScratch.assign (dest.begin(), dest.end());

    for (int x = 0; x < width; ++x)
    {
        auto sum = 0.0f;
        auto n = 0;

        for (int j = juce::jmax (0, x - radius); j <= juce::jmin (width - 1, x + radius); ++j)
        {
            sum += smoothScratch[static_cast<size_t> (j)];
            ++n;
        }

        dest[static_cast<size_t> (x)] = sum / static_cast<float> (n);
    }
}

//==============================================================================
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

void SpectrumView::drawNoteGrid (juce::Graphics& g, float width, float height,
                                 double logSpan, double nyquist) const
{
    g.setFont (Theme::mono (8.0f));

    for (int octave = 1; octave <= 10; ++octave)
    {
        // Every C, equal temperament from A4 = 440: C1 is MIDI 24.
        const auto midi = 12 * (octave + 1);
        const auto hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);

        if (hz <= minFrequencyHz || hz >= nyquist)
            continue;

        const auto x = width * static_cast<float> (std::log (hz / minFrequencyHz) / logSpan);

        g.setColour (Theme::palette().boneDim.withAlpha (0.22f));
        g.drawVerticalLine (juce::roundToInt (x), 0.0f, height);

        // A row above the frequency landmarks, so the two label runs never
        // collide.
        g.setColour (Theme::palette().boneDim.withAlpha (0.5f));
        g.drawText ("C" + juce::String (octave),
                    juce::Rectangle<float> (x + 2.0f, height - 24.0f, 22.0f, 10.0f),
                    juce::Justification::centredLeft);
    }
}

void SpectrumView::drawTiltGuides (juce::Graphics& g, float width, float height,
                                   double nyquist) const
{
    const float dashes[] = { 4.0f, 4.0f };

    const auto toY = [height] (double db)
    {
        return height * juce::jlimit (0.0f, 1.0f,
                                      static_cast<float> ((db - dbCeiling) / (dbFloor - dbCeiling)));
    };

    struct Guide { double slopeDbPerOctave; const char* label; };

    const Guide guides[] = { { -3.0, "-3.0/OCT" }, { -4.5, "-4.5/OCT" } };

    g.setFont (Theme::mono (8.0f));

    for (const auto& guide : guides)
    {
        // Anchored through (1 kHz, -30 dB). dB per octave is linear in x on a
        // log frequency axis, so each guide is one straight line edge to edge.
        const auto dbAt = [&guide] (double hz)
        {
            return -30.0 + guide.slopeDbPerOctave * std::log2 (hz / 1000.0);
        };

        const auto yLeft = toY (dbAt (minFrequencyHz));
        const auto yRight = toY (dbAt (nyquist));

        g.setColour (Theme::palette().boneDim.withAlpha (0.55f));
        g.drawDashedLine (juce::Line<float> (0.0f, yLeft, width, yRight), dashes, 2, 1.0f);

        // Tucked left of the dB scale numerals at the right edge.
        g.setColour (Theme::palette().boneDim.withAlpha (0.7f));
        g.drawText (guide.label,
                    juce::Rectangle<float> (width - 92.0f, yRight - 12.0f, 54.0f, 10.0f),
                    juce::Justification::centredRight);
    }
}

void SpectrumView::drawToggles (juce::Graphics& g) const
{
    // Same engaged treatment as the console's switch rail, shrunk to fit the
    // pane corner.
    const auto drawPlate = [&g] (juce::Rectangle<int> plate, juce::StringRef label,
                                 bool engaged, juce::Colour accent)
    {
        g.setColour (engaged ? accent.withAlpha (0.7f)
                             : Theme::palette().boneDim.withAlpha (0.3f));
        g.drawRect (plate, 1);

        g.setColour (engaged ? Theme::palette().bone
                             : Theme::palette().boneDim.withAlpha (0.5f));
        g.setFont (Theme::mono (8.0f, true));
        g.drawText (label, plate, juce::Justification::centred);
    };

    drawPlate (noteToggleBounds(), "NOTE", showNoteGrid, Theme::palette().amber);
    drawPlate (tiltToggleBounds(), "TILT", showTiltGuides, Theme::palette().amber);

    if (! referenceDb.empty())
        drawPlate (refTagBounds(), "REF x", true, Theme::palette().phosphor);
}

void SpectrumView::drawCursor (juce::Graphics& g, float width, float height,
                               double logSpan) const
{
    const auto x = juce::jlimit (0, getWidth() - 1, cursorPosition.x);
    const auto fx = static_cast<float> (x);
    const auto fy = juce::jlimit (0.0f, height, static_cast<float> (cursorPosition.y));

    const float dashes[] = { 3.0f, 3.0f };

    g.setColour (Theme::palette().boneDim.withAlpha (0.55f));
    g.drawDashedLine (juce::Line<float> (fx, 0.0f, fx, height), dashes, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (0.0f, fy, width, fy), dashes, 2, 1.0f);

    const auto frequency = minFrequencyHz * std::exp (static_cast<double> (x) / width * logSpan);

    const auto freqText = frequency >= 1000.0
        ? juce::String (frequency / 1000.0, 1) + "K HZ"
        : juce::String (juce::roundToInt (frequency)) + " HZ";

    // Nearest equal-tempered note, A4 = 440, plus the offset in cents.
    const auto midi = 69.0 + 12.0 * std::log2 (frequency / 440.0);
    const auto nearest = juce::roundToInt (midi);
    const auto cents = juce::roundToInt ((midi - nearest) * 100.0);

    static const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };

    const auto noteText = juce::String (noteNames[((nearest % 12) + 12) % 12])
                        + juce::String (nearest / 12 - 1)
                        + (cents >= 0 ? " +" : " ")
                        + juce::String (cents);

    // The live curve's level at this x, from the same per-pixel samples the
    // trace was just drawn from.
    const auto haveLevel = x < static_cast<int> (avgDbScratch.size());
    const auto dbText = haveLevel
        ? juce::String (avgDbScratch[static_cast<size_t> (x)], 1) + " DB"
        : juce::String ("--.- DB");

    constexpr auto boxWidth = 78.0f;
    constexpr auto boxHeight = 42.0f;

    auto boxX = fx + 14.0f;
    auto boxY = fy + 12.0f;

    if (boxX + boxWidth > width)
        boxX = fx - 14.0f - boxWidth;

    if (boxY + boxHeight > height)
        boxY = fy - 12.0f - boxHeight;

    const juce::Rectangle<float> box (juce::jmax (0.0f, boxX), juce::jmax (0.0f, boxY),
                                      boxWidth, boxHeight);

    g.setColour (Theme::palette().screenBlack.withAlpha (0.85f));
    g.fillRect (box);

    g.setColour (Theme::palette().boneDim.withAlpha (0.5f));
    g.drawRect (box, 1.0f);

    g.setFont (Theme::mono (9.0f));

    auto rows = box.reduced (6.0f, 4.0f);
    const auto rowHeight = rows.getHeight() / 3.0f;

    g.setColour (Theme::palette().amberBright.withAlpha (0.9f));
    g.drawText (freqText, rows.removeFromTop (rowHeight), juce::Justification::centredLeft);

    g.setColour (Theme::palette().boneDim);
    g.drawText (noteText, rows.removeFromTop (rowHeight), juce::Justification::centredLeft);

    g.setColour (Theme::palette().amberBright.withAlpha (0.75f));
    g.drawText (dbText, rows, juce::Justification::centredLeft);
}

//==============================================================================
void SpectrumView::drawTraces (juce::Graphics& g)
{
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

    const auto makePath = [&toY] (const std::vector<float>& dbPerPixel, int lastX)
    {
        juce::Path path;

        for (int x = 0; x <= lastX; ++x)
        {
            const auto fx = static_cast<float> (x);
            const auto y = toY (dbPerPixel[static_cast<size_t> (x)]);

            if (x == 0)
                path.startNewSubPath (fx, y);
            else
                path.lineTo (fx, y);
        }

        return path;
    };

    // Stored snapshot first: the comparison memory sits under everything that
    // moves. Same sampling as the live curves, dimmer phosphor.
    if (static_cast<int> (storedAveraged.size()) == numBins)
    {
        buildPixelCurve (storedAveraged, numBins, binsPerHz, logSpan, overlayDbScratch);

        g.setColour (Theme::palette().amberDim.withAlpha (0.8f));
        g.strokePath (makePath (overlayDbScratch, width - 1),
                      juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

        buildPixelCurve (storedSide, numBins, binsPerHz, logSpan, overlayDbScratch);

        // The side snapshot keeps its trace's hue, dropped to snapshot weight.
        g.setColour (Theme::palette().secondary.withAlpha (0.4f));
        g.strokePath (makePath (overlayDbScratch, width - 1),
                      juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
    }

    // Reference curve, under the live traces for the same reason.
    if (! referenceDb.empty() && referenceRate > 0.0)
    {
        const auto refBins = static_cast<int> (referenceDb.size());
        const auto refNyquist = referenceRate * 0.5;
        const auto refBinsPerHz = (refBins - 1) / refNyquist;

        buildPixelCurve (referenceDb, refBins, refBinsPerHz, logSpan, overlayDbScratch);

        // A lower-rate file's spectrum ends at its own nyquist; stop the
        // curve there rather than smearing the last bin across the remaining
        // octaves.
        auto lastX = width - 1;

        if (refNyquist < nyquist)
            lastX = juce::jmin (lastX,
                                static_cast<int> (width * std::log (refNyquist / minFrequencyHz) / logSpan));

        if (lastX > 0)
        {
            g.setColour (Theme::palette().phosphor.withAlpha (0.8f));
            g.strokePath (makePath (overlayDbScratch, lastX),
                          juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
        }
    }

    // One x-step per pixel, same span logic as the spectrogram's rows. The
    // averaged curve gets the short smoothing pass; the peak-hold ghost stays
    // raw — it is the precision instrument of the two.
    buildPixelCurve (averaged, numBins, binsPerHz, logSpan, avgDbScratch);

    if (static_cast<int> (peakDbScratch.size()) != width)
        peakDbScratch.resize (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
    {
        const auto fLow  = minFrequencyHz * std::exp (static_cast<double> (x) / width * logSpan);
        const auto fHigh = minFrequencyHz * std::exp (static_cast<double> (x + 1) / width * logSpan);

        peakDbScratch[static_cast<size_t> (x)] = levelAt (peakHold, numBins,
                                                          static_cast<float> (fLow * binsPerHz),
                                                          static_cast<float> (fHigh * binsPerHz));
    }

    const auto average = makePath (avgDbScratch, width - 1);
    const auto peaks = makePath (peakDbScratch, width - 1);

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

    // SIDE trace: same sampling and smoothing as the average, drawn once and
    // thin — the secondary reading. Where it hugs the mid trace the material
    // is wide; where it falls away it is mono.
    buildPixelCurve (averagedSide, numBins, binsPerHz, logSpan, sideDbScratch);

    g.setColour (Theme::palette().secondary.withAlpha (0.75f));
    g.strokePath (makePath (sideDbScratch, width - 1),
                  juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Legend, tucked into the top-left corner of the screen.
    {
        g.setFont (Theme::mono (8.0f));

        const auto swatchY1 = 10.0f;
        const auto swatchY2 = 22.0f;

        g.setColour (Theme::palette().amberBright.withAlpha (0.85f));
        g.drawLine (8.0f, swatchY1, 20.0f, swatchY1, 1.2f);
        g.drawText ("MID", juce::Rectangle<float> (24.0f, swatchY1 - 5.0f, 40.0f, 10.0f),
                    juce::Justification::centredLeft);

        g.setColour (Theme::palette().secondary.withAlpha (0.85f));
        g.drawLine (8.0f, swatchY2, 20.0f, swatchY2, 1.0f);
        g.drawText ("SIDE", juce::Rectangle<float> (24.0f, swatchY2 - 5.0f, 40.0f, 10.0f),
                    juce::Justification::centredLeft);
    }
}

void SpectrumView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::palette().screenBlack);
    drawGraticule (g);

    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());
    const auto nyquist = engine.getSampleRate() * 0.5;
    const auto axisValid = getWidth() > 1 && nyquist > minFrequencyHz;

    // Overlay rules under the traces, cursor and chrome over them.
    if (axisValid)
    {
        const auto logSpan = std::log (nyquist / minFrequencyHz);

        if (showNoteGrid)
            drawNoteGrid (g, width, height, logSpan, nyquist);

        if (showTiltGuides)
            drawTiltGuides (g, width, height, nyquist);
    }

    drawTraces (g);

    drawToggles (g);

    if (cursorVisible && axisValid)
        drawCursor (g, width, height, std::log (nyquist / minFrequencyHz));

    if (dragHover)
    {
        g.setColour (Theme::palette().amberBright.withAlpha (0.8f));
        g.drawRect (getLocalBounds(), 2);
    }
}
