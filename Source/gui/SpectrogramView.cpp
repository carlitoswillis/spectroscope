#include "SpectrogramView.h"
#include "Theme.h"

using namespace ::juce::gl;

namespace
{
    // Written in 2.1-style GLSL; the translate helpers rewrite attribute /
    // varying / texture2D for the core profile the context requests.
    constexpr const char* spectrogramVertexShader = R"(
        attribute vec2 position;
        varying vec2 vUV;

        void main()
        {
            vUV = position * 0.5 + 0.5;
            gl_Position = vec4 (position, 0.0, 1.0);
        }
    )";

    constexpr const char* spectrogramFragmentShader = R"(
        varying vec2 vUV;

        uniform sampler2D uHistory;
        uniform sampler2D uPalette;
        uniform float uHeadCols;        // ring write position, in columns
        uniform float uSpanCols;        // columns visible across the view
        uniform float uRingWidth;
        uniform float uBinCount;
        uniform float uMinBin;          // fractional bin of the axis floor frequency
        uniform float uLogSpan;         // ln (nyquist / minFrequency), 0 = linear fallback
        uniform float uLevelScale;      // texture value -> display level, folded
        uniform float uLevelBias;       //   with the fixed storage range
        uniform float uScanlineAlpha;
        uniform float uVignetteAlpha;
        uniform vec2 uResolution;       // physical pixels

        void main()
        {
            // Newest column at the right edge, wrapped through the ring's
            // write head. Snapping to the texel centre keeps LINEAR filtering
            // from blending the newest column with the oldest at the seam.
            float column = uHeadCols - uSpanCols + vUV.x * uSpanCols;
            float u = fract ((floor (column) + 0.5) / uRingWidth);

            // Log frequency axis, the same mapping the CPU row tables encode;
            // until the sample rate is known, linear-by-bin as on the CPU.
            float bin = uLogSpan > 0.0 ? uMinBin * exp (vUV.y * uLogSpan)
                                       : vUV.y * (uBinCount - 1.0);
            float v = (bin + 0.5) / uBinCount;

            float level = clamp (texture2D (uHistory, vec2 (u, v)).r * uLevelScale + uLevelBias,
                                 0.0, 1.0);

            // Same 1.3 gamma as the CPU path: broadband mids sink back down
            // the ramp instead of washing the whole field in accent colour.
            level = pow (level, 1.3);

            vec3 colour = texture2D (uPalette, vec2 ((level * 255.0 + 0.5) / 256.0, 0.5)).rgb;

            // CRT glass, in-shader: the overlay component can never composite
            // above this surface. gl_FragCoord is in physical pixels because
            // the viewport spans the scaled framebuffer, so the scanlines
            // stay single-pixel-fine on Retina.
            if (mod (gl_FragCoord.y, 3.0) < 1.0)
                colour *= 1.0 - uScanlineAlpha;

            // Vignette approximating CrtOverlay's radial gradient: transparent
            // to 0.55 of the overshooting radius, then the same two stops.
            float radius = max (uResolution.x, uResolution.y) * 0.78;
            float t = distance (gl_FragCoord.xy, uResolution * 0.5) / radius;
            float vignette = t <= 0.55 ? 0.0
                           : t <= 0.80 ? uVignetteAlpha * 0.35 * (t - 0.55) / 0.25
                           : mix (uVignetteAlpha * 0.35, uVignetteAlpha,
                                  min ((t - 0.80) / 0.20, 1.0));
            colour *= 1.0 - vignette;

            gl_FragColor = vec4 (colour, 1.0);
        }
    )";

    // Nearest semitone off an A440 reference, plus the residual in cents. No
    // enharmonic preference beyond sharps, matching period frequency counters.
    juce::String noteNameForFrequency (double hz)
    {
        static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const auto nearest = juce::roundToInt (midi);
        const auto cents = juce::roundToInt ((midi - static_cast<double> (nearest)) * 100.0);

        const auto octave = nearest / 12 - 1;
        const auto index = ((nearest % 12) + 12) % 12;

        return juce::String (names[index]) + juce::String (octave) + " "
             + (cents > 0 ? "+" : "") + juce::String (cents) + "C";
    }

    // Readout box near the pointer, clamped inside the view: screenBlack glass
    // over amberBright print, the weight of a panel-mounted meter window.
    void drawReadoutBox (juce::Graphics& g, juce::Rectangle<float> bounds,
                         juce::Point<float> pointer, const juce::StringArray& lines)
    {
        const juce::Font font (Theme::mono (9.0f));

        auto textWidth = 0.0f;

        for (const auto& line : lines)
            textWidth = juce::jmax (textWidth, juce::GlyphArrangement::getStringWidth (font, line));

        constexpr auto lineHeight = 11.0f;
        const auto boxWidth = textWidth + 10.0f;
        const auto boxHeight = lineHeight * static_cast<float> (lines.size()) + 6.0f;

        auto x = pointer.x + 10.0f;
        auto y = pointer.y + 10.0f;

        // Flip to whichever side of the pointer keeps the box on the glass.
        if (x + boxWidth > bounds.getRight())
            x = pointer.x - 10.0f - boxWidth;

        if (y + boxHeight > bounds.getBottom())
            y = pointer.y - 10.0f - boxHeight;

        x = juce::jlimit (bounds.getX(), juce::jmax (bounds.getX(), bounds.getRight() - boxWidth), x);
        y = juce::jlimit (bounds.getY(), juce::jmax (bounds.getY(), bounds.getBottom() - boxHeight), y);

        const juce::Rectangle<float> box (x, y, boxWidth, boxHeight);

        g.setColour (Theme::palette().screenBlack.withAlpha (0.85f));
        g.fillRect (box);

        g.setFont (font);
        g.setColour (Theme::palette().amberBright);

        for (int i = 0; i < lines.size(); ++i)
            g.drawText (lines[i],
                        juce::Rectangle<float> (box.getX() + 5.0f, box.getY() + 3.0f + lineHeight * static_cast<float> (i),
                                                boxWidth - 10.0f, lineHeight),
                        juce::Justification::centredLeft);
    }
}

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
    // Must run before anything the render thread touches is destroyed;
    // detach blocks until that thread has finished. No-op if never attached.
    glContext.detach();

    engine.removeConsumer();
}

void SpectrogramView::setGpuEnabled (bool shouldUseGpu)
{
    if (shouldUseGpu == gpuEnabled)
        return;

    gpuEnabled = shouldUseGpu;

    if (shouldUseGpu)
    {
        stagePaletteForGl();
        stageUniformsForGl();
        stageFullHistoryForGl();

        glViewWidth = juce::jmax (1, getWidth());
        glViewHeight = juce::jmax (1, getHeight());

        // Core profile: GL_LUMINANCE is gone, GL_R8/GL_RED replaces it, and
        // '#version 150' shaders become available on macOS.
        glContext.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
        glContext.setRenderer (this);

        // paint() keeps drawing the grid and labels above the GL frame — a
        // sibling overlay could never composite over this surface.
        glContext.setComponentPaintingEnabled (true);

        // The 60 Hz timer's repaint() drives frames; a free-running render
        // thread burns a core redrawing an unchanged picture.
        glContext.setContinuousRepainting (false);

        // paint() stops covering every pixel once the raster moves to GL.
        setOpaque (false);

        glContext.attachTo (*this);
    }
    else
    {
        glContext.detach();
        setOpaque (true);

        // The CPU image idled while GL owned the raster; bring it back in step.
        rebuildImage();
        rebuildRowMapping();
        reRenderAllHistory();
        repaint();
    }
}

void SpectrogramView::themeChanged()
{
    colourTable = Theme::palette().spectrogramTable;

    if (gpuEnabled)
    {
        stagePaletteForGl();
        stageUniformsForGl();   // scanline and vignette strengths travel with the livery
    }
    else
    {
        reRenderAllHistory();
    }

    repaint();
}

void SpectrogramView::clear()
{
    engine.getSpectrumColumns().discardPending();
    historyWrite = 0;
    numStored = 0;
    rebuildImage();

    if (gpuEnabled)
        stageFullHistoryForGl();   // nothing stored, so this stages a bare reset

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

    // The GL texture stores dB over a fixed range, so a display-range change
    // is two uniforms — the point of not baking the range into the bytes.
    if (gpuEnabled)
        stageUniformsForGl();
    else
        reRenderAllHistory();

    repaint();
}

void SpectrogramView::resized()
{
    glViewWidth = juce::jmax (1, getWidth());
    glViewHeight = juce::jmax (1, getHeight());

    if (gpuEnabled)
        return;   // the shader reads the new size as uniforms; the ring survives

    rebuildImage();
    rebuildRowMapping();
    reRenderAllHistory();
}

void SpectrogramView::mouseMove (const juce::MouseEvent& event)
{
    cursor = event.getPosition();
    repaint();
}

void SpectrogramView::mouseExit (const juce::MouseEvent&)
{
    cursor = { -1, -1 };
    repaint();
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
    // known (or change) after the view is laid out. On the GPU it is only a
    // uniform change — history stays put in the ring.
    if (engine.getSampleRate() != lastMappedRate && engineBins == numBins)
    {
        if (gpuEnabled)
        {
            lastMappedRate = engine.getSampleRate();
            stageUniformsForGl();
        }
        else
        {
            rebuildRowMapping();
            reRenderAllHistory();
        }
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

        if (gpuEnabled)
        {
            stageUniformsForGl();
            stageFullHistoryForGl();
        }
    }

    // A recreated context comes up with empty textures; restage the CPU ring.
    if (gpuEnabled && glContextReset.exchange (false))
        stageFullHistoryForGl();

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

            if (gpuEnabled)
            {
                stageColumnForGl (column);
            }
            else
            {
                renderColumnIntoImage (column, imageWrite);
                imageWrite = (imageWrite + 1) % width;
            }
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
    if (gpuEnabled)
    {
        // The GL frame beneath already carries the raster and the glass; only
        // the grid, labels and cursor readout belong up here.
        drawFrequencyGrid (g);
        drawCursorReadout (g);
        return;
    }

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
    drawCursorReadout (g);
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

void SpectrogramView::drawCursorReadout (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (cursor.x < 0 || ! bounds.contains (cursor.toFloat()))
        return;

    const auto nyquist = engine.getSampleRate() * 0.5;

    // Same guard rebuildRowMapping() uses — until the sample rate is known,
    // there is no Hz-to-row mapping to invert.
    if (nyquist <= minFrequencyHz)
        return;

    const auto fx = static_cast<float> (cursor.x);
    const auto fy = static_cast<float> (cursor.y);

    const float dashes[] = { 4.0f, 3.0f };

    g.setColour (Theme::palette().boneDim.withAlpha (0.5f));
    g.drawDashedLine (juce::Line<float> (bounds.getX(), fy, bounds.getRight(), fy), dashes, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (fx, bounds.getY(), fx, bounds.getBottom()), dashes, 2, 1.0f);

    // Invert drawFrequencyGrid's y = height * (1 - log(hz/min) / logSpan).
    const auto logSpan = std::log (nyquist / minFrequencyHz);
    const auto hz = minFrequencyHz * std::exp ((1.0 - fy / bounds.getHeight()) * logSpan);

    const auto secondsBeforeNow = (bounds.getRight() - fx) * engine.getSecondsPerPoint();

    const juce::StringArray lines
    {
        hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + "K HZ" : juce::String (hz, 1) + " HZ",
        noteNameForFrequency (hz),
        juce::String (-secondsBeforeNow, 1) + " S",
    };

    drawReadoutBox (g, bounds, { fx, fy }, lines);
}

//==============================================================================
// GPU path — message-thread staging.

void SpectrogramView::stageColumnForGl (const float* column)
{
    if (numBins <= 0)
        return;

    const juce::ScopedLock lock (glLock);

    if (glStaging.numBins != numBins)
    {
        glStaging.columns.clear();
        glStaging.numColumns = 0;
        glStaging.numBins = numBins;
        glStaging.reset = true;
    }

    // If the render thread has fallen a whole ring behind, the oldest staged
    // column would be overwritten before it was ever seen.
    if (glStaging.numColumns >= glRingColumns)
    {
        glStaging.columns.erase (glStaging.columns.begin(),
                                 glStaging.columns.begin() + numBins);
        --glStaging.numColumns;
    }

    const auto offset = glStaging.columns.size();
    glStaging.columns.resize (offset + static_cast<size_t> (numBins));

    auto* out = glStaging.columns.data() + offset;

    // Bytes cover the analyser's full floorDb..0 range, not the display range,
    // so setDecibelRange never has to re-upload history. ~0.39 dB per step is
    // invisible after palette mapping.
    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto normalised = juce::jlimit (0.0f, 1.0f,
            (column[bin] - StftAnalyzer::floorDb) / -StftAnalyzer::floorDb);

        out[bin] = static_cast<juce::uint8> (juce::roundToInt (normalised * 255.0f));
    }

    ++glStaging.numColumns;
}

void SpectrogramView::stageFullHistoryForGl()
{
    const juce::ScopedLock lock (glLock);

    glStaging.columns.clear();
    glStaging.numColumns = 0;
    glStaging.numBins = numBins;
    glStaging.reset = true;

    if (numBins <= 0)
        return;

    // Newest columns only — the GL ring is narrower than the CPU one — staged
    // oldest-first so the write head ends up just past the newest.
    const auto columnsToStage = juce::jmin (numStored, glRingColumns);

    for (int i = 0; i < columnsToStage; ++i)
    {
        const auto age = columnsToStage - 1 - i;
        auto index = historyWrite - 1 - age;

        while (index < 0)
            index += historyColumns;

        stageColumnForGl (history.data()
                          + static_cast<std::ptrdiff_t> (index % historyColumns) * numBins);
    }
}

void SpectrogramView::stagePaletteForGl()
{
    const juce::ScopedLock lock (glLock);

    for (size_t i = 0; i < colourTable.size(); ++i)
    {
        const auto colour = colourTable[i];

        glStaging.palette[i * 4 + 0] = colour.getRed();
        glStaging.palette[i * 4 + 1] = colour.getGreen();
        glStaging.palette[i * 4 + 2] = colour.getBlue();
        glStaging.palette[i * 4 + 3] = 255;
    }

    glStaging.paletteDirty = true;
}

void SpectrogramView::stageUniformsForGl()
{
    const juce::ScopedLock lock (glLock);

    glStaging.dbFloor = dbFloor;
    glStaging.dbCeiling = dbCeiling;
    glStaging.nyquist = engine.getSampleRate() * 0.5;
    glStaging.scanlineAlpha = Theme::palette().scanlineAlpha;
    glStaging.vignetteAlpha = Theme::palette().vignetteAlpha;
}

//==============================================================================
// GPU path — render thread.

void SpectrogramView::newOpenGLContextCreated()
{
    auto program = std::make_unique<juce::OpenGLShaderProgram> (glContext);

    if (program->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (spectrogramVertexShader))
        && program->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (spectrogramFragmentShader))
        && program->link())
    {
        glProgram = std::move (program);
    }
    else
    {
        jassertfalse;   // shader compile/link failure — the view stays dark
        glProgram.reset();
    }

    glGenVertexArrays (1, &glVertexArray);
    glBindVertexArray (glVertexArray);

    glGenBuffers (1, &glQuadBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, glQuadBuffer);

    static constexpr float quadVertices[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    glBufferData (GL_ARRAY_BUFFER, sizeof (quadVertices), quadVertices, GL_STATIC_DRAW);

    if (glProgram != nullptr)
    {
        const auto position = glGetAttribLocation (glProgram->getProgramID(), "position");

        if (position >= 0)
        {
            glVertexAttribPointer (static_cast<GLuint> (position), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray (static_cast<GLuint> (position));
        }
    }

    glBindVertexArray (0);

    // Single-byte texels: without this, uploads whose row length is not a
    // multiple of 4 shear.
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);

    // Palette LUT, uploaded now so a recreated context is not colourless.
    glGenTextures (1, &glPaletteTexture);
    glBindTexture (GL_TEXTURE_2D, glPaletteTexture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    {
        std::array<juce::uint8, 256 * 4> palette {};

        {
            const juce::ScopedLock lock (glLock);
            palette = glStaging.palette;
            glStaging.paletteDirty = false;
        }

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, palette.data());
    }

    // History ring: wrap on the time axis so fract() sampling is free, clamp
    // on the bin axis, LINEAR for the in-shader bin interpolation. Storage is
    // allocated once the bin count is known.
    glGenTextures (1, &glHistoryTexture);
    glBindTexture (GL_TEXTURE_2D, glHistoryTexture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTextureBins = 0;
    glWriteColumn = 0;

    // The timer restages the CPU ring into the fresh texture.
    glContextReset = true;
}

void SpectrogramView::reallocateHistoryTexture (int bins)
{
    // Uploading zeroes doubles as the clear: fresh GL storage is undefined,
    // and zero bytes decode to the dB floor — dark glass.
    std::vector<juce::uint8> zeroes (static_cast<size_t> (glRingColumns) * static_cast<size_t> (bins), 0);

    glBindTexture (GL_TEXTURE_2D, glHistoryTexture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_R8, glRingColumns, bins, 0,
                  GL_RED, GL_UNSIGNED_BYTE, zeroes.data());

    glTextureBins = bins;
    glWriteColumn = 0;
}

void SpectrogramView::renderOpenGL()
{
    int stagedBins = 0;
    int pendingColumns = 0;
    bool reset = false;
    bool paletteDirty = false;
    std::array<juce::uint8, 256 * 4> palette {};
    float stagedDbFloor = -75.0f, stagedDbCeiling = 0.0f;
    float scanlineAlpha = 0.0f, vignetteAlpha = 0.0f;
    double nyquist = 0.0;

    {
        const juce::ScopedLock lock (glLock);

        glUploadScratch.swap (glStaging.columns);
        glStaging.columns.clear();   // keeps its capacity for the next batch

        pendingColumns = glStaging.numColumns;
        glStaging.numColumns = 0;

        stagedBins = glStaging.numBins;

        reset = glStaging.reset;
        glStaging.reset = false;

        paletteDirty = glStaging.paletteDirty;
        glStaging.paletteDirty = false;

        if (paletteDirty)
            palette = glStaging.palette;

        stagedDbFloor = glStaging.dbFloor;
        stagedDbCeiling = glStaging.dbCeiling;
        nyquist = glStaging.nyquist;
        scanlineAlpha = glStaging.scanlineAlpha;
        vignetteAlpha = glStaging.vignetteAlpha;
    }

    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);

    if (stagedBins > 0 && stagedBins != glTextureBins)
        reallocateHistoryTexture (stagedBins);
    else if (reset && glTextureBins > 0)
        reallocateHistoryTexture (glTextureBins);   // same size: reallocation is the clear

    if (reset)
        glWriteColumn = 0;

    if (glTextureBins > 0 && pendingColumns > 0
        && glUploadScratch.size() >= static_cast<size_t> (pendingColumns) * static_cast<size_t> (glTextureBins))
    {
        glBindTexture (GL_TEXTURE_2D, glHistoryTexture);

        // One column = one texel column.
        for (int i = 0; i < pendingColumns; ++i)
        {
            glTexSubImage2D (GL_TEXTURE_2D, 0, glWriteColumn, 0, 1, glTextureBins,
                             GL_RED, GL_UNSIGNED_BYTE,
                             glUploadScratch.data() + static_cast<size_t> (i) * static_cast<size_t> (glTextureBins));

            glWriteColumn = (glWriteColumn + 1) % glRingColumns;
        }
    }

    if (paletteDirty && glPaletteTexture != 0)
    {
        glBindTexture (GL_TEXTURE_2D, glPaletteTexture);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, palette.data());
    }

    // Only valid inside this callback; 2.0 on Retina. The viewport spans the
    // physical framebuffer so gl_FragCoord counts physical pixels.
    const auto scale = glContext.getRenderingScale();
    const auto viewportWidth = juce::roundToInt (scale * glViewWidth.load());
    const auto viewportHeight = juce::roundToInt (scale * glViewHeight.load());

    glViewport (0, 0, viewportWidth, viewportHeight);
    juce::OpenGLHelpers::clear (juce::Colours::black);

    if (glProgram == nullptr || glTextureBins <= 0)
        return;

    glDisable (GL_BLEND);
    glDisable (GL_DEPTH_TEST);

    glProgram->use();

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, glHistoryTexture);
    glActiveTexture (GL_TEXTURE1);
    glBindTexture (GL_TEXTURE_2D, glPaletteTexture);

    glProgram->setUniform ("uHistory", 0);
    glProgram->setUniform ("uPalette", 1);

    // One texel column per logical pixel matches the CPU raster's time scale.
    glProgram->setUniform ("uHeadCols", static_cast<float> (glWriteColumn));
    glProgram->setUniform ("uSpanCols", static_cast<float> (juce::jmin (glViewWidth.load(), glRingColumns)));
    glProgram->setUniform ("uRingWidth", static_cast<float> (glRingColumns));
    glProgram->setUniform ("uBinCount", static_cast<float> (glTextureBins));

    const auto logSpan = nyquist > minFrequencyHz ? std::log (nyquist / minFrequencyHz) : 0.0;
    const auto minBin = nyquist > 0.0 ? minFrequencyHz / nyquist * (glTextureBins - 1) : 0.0;

    glProgram->setUniform ("uMinBin", static_cast<float> (minBin));
    glProgram->setUniform ("uLogSpan", static_cast<float> (logSpan));

    // Folds the fixed storage range and the display range into one
    // multiply-add per fragment.
    const auto displayRange = juce::jmax (1.0f, stagedDbCeiling - stagedDbFloor);
    glProgram->setUniform ("uLevelScale", -StftAnalyzer::floorDb / displayRange);
    glProgram->setUniform ("uLevelBias", (StftAnalyzer::floorDb - stagedDbFloor) / displayRange);

    glProgram->setUniform ("uScanlineAlpha", scanlineAlpha);
    glProgram->setUniform ("uVignetteAlpha", vignetteAlpha);
    glProgram->setUniform ("uResolution", static_cast<float> (viewportWidth), static_cast<float> (viewportHeight));

    glBindVertexArray (glVertexArray);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray (0);

    glActiveTexture (GL_TEXTURE0);
}

void SpectrogramView::openGLContextClosing()
{
    glProgram.reset();

    if (glHistoryTexture != 0)
    {
        glDeleteTextures (1, &glHistoryTexture);
        glHistoryTexture = 0;
    }

    if (glPaletteTexture != 0)
    {
        glDeleteTextures (1, &glPaletteTexture);
        glPaletteTexture = 0;
    }

    if (glQuadBuffer != 0)
    {
        glDeleteBuffers (1, &glQuadBuffer);
        glQuadBuffer = 0;
    }

    if (glVertexArray != 0)
    {
        glDeleteVertexArrays (1, &glVertexArray);
        glVertexArray = 0;
    }

    glTextureBins = 0;
    glWriteColumn = 0;
}
