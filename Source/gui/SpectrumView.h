#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../dsp/AnalysisEngine.h"

/**
    Instantaneous spectrum: magnitude against a log frequency axis, the view a
    channel-strip analyser gives you — but drawn as a phosphor trace, not a bar
    chart.

    Two traces: an averaged curve that moves like the sound, and a peak-hold
    ghost that decays slowly enough to read resonances off. Both come from the
    same STFT columns the spectrogram scrolls, so switching views never changes
    what is being measured.

    A third trace averages the SIDE channel's columns, giving the width-vs-
    frequency reading: where it hugs the mid trace the material is wide, where
    it falls away it is mono.

    On top of the live traces: a stored snapshot for A/B comparison, a
    measurement cursor with a note readout, optional note-grid and tilt-guide
    overlays, and a reference curve averaged offline from a dropped audio file.
*/
class SpectrumView final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer,
                           private juce::AsyncUpdater
{
public:
    explicit SpectrumView (AnalysisEngine&);
    ~SpectrumView() override;

    void paint (juce::Graphics&) override;

    /** The spectrum column queue feeds whichever view is active; an inactive
        view stops its timer so it never steals columns from the other.
    */
    void setActive (bool shouldBeActive);

    /** Drops the averages, the peak-hold ghost and the stored trace back to
        the floor. The reference curve survives — it is a reference, not
        history.
    */
    void clear();

    /** Snapshots the current averaged and side curves as a stored trace drawn
        under the live ones, or drops an existing snapshot — one latching
        comparison memory.
    */
    void toggleStore();
    bool hasStoredTrace() const;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    class ReferenceAnalysisJob;

    void timerCallback() override;
    void handleAsyncUpdate() override;

    void drawGraticule (juce::Graphics&);
    void drawTraces (juce::Graphics&);
    void drawNoteGrid (juce::Graphics&, float width, float height,
                       double logSpan, double nyquist) const;
    void drawTiltGuides (juce::Graphics&, float width, float height, double nyquist) const;
    void drawToggles (juce::Graphics&) const;
    void drawCursor (juce::Graphics&, float width, float height, double logSpan) const;

    /** Averaged level at a fractional bin, max-over-span when a pixel covers
        several bins and interpolated when several pixels cover one bin.
    */
    float levelAt (const std::vector<float>& source, int sourceNumBins,
                   float binLow, float binHigh) const;

    /** Per-pixel dB curve: one sample per x column plus a short moving average
        across frequency, which calms the bin-level jitter that fuzzes a trace
        above 1 kHz, where each pixel spans many bins. Generalised over the
        source's bin density so the stored and reference curves share the exact
        sampling of the live one.
    */
    void buildPixelCurve (const std::vector<float>& source, int sourceNumBins,
                          double binsPerHz, double logSpan, std::vector<float>& dest);

    juce::Rectangle<int> noteToggleBounds() const;
    juce::Rectangle<int> tiltToggleBounds() const;
    juce::Rectangle<int> refTagBounds() const;

    static constexpr int maxColumnsPerFrame = 128;
    static constexpr double minFrequencyHz = 30.0;

    // Per incoming column (~5 ms of audio): the average chases the input, the
    // peak trace falls at a readable rate. Matched to the ~187 columns/s an
    // FFT hop of 256 produces at 48 kHz.
    static constexpr float averageAlpha = 0.18f;
    static constexpr float peakDecayDbPerColumn = 0.08f;

    static constexpr float dbFloor = -75.0f;
    static constexpr float dbCeiling = 0.0f;

    AnalysisEngine& engine;

    std::vector<float> scratch;       // maxColumnsPerFrame * numBins
    std::vector<float> averaged;      // dB per bin, exponential average
    std::vector<float> peakHold;      // dB per bin, slow decay
    std::vector<float> sideScratch;   // maxColumnsPerFrame * numBins
    std::vector<float> averagedSide;  // dB per bin, exponential average
    int numBins = 0;

    // STORE snapshot, dB per bin at the moment of capture. Empty means none.
    std::vector<float> storedAveraged;
    std::vector<float> storedSide;

    // Measurement cursor, view-internal: position tracked on mouseMove, the
    // readout drawn as a paint overlay.
    juce::Point<int> cursorPosition;
    bool cursorVisible = false;

    // Silkscreen overlay toggles.
    bool showNoteGrid = false;
    bool showTiltGuides = false;

    // Reference curve from a dropped file. The job hands its result over
    // under the lock and pokes the AsyncUpdater; the message thread owns
    // referenceDb itself.
    std::unique_ptr<ReferenceAnalysisJob> referenceJob;
    juce::CriticalSection referenceLock;
    std::vector<float> pendingReferenceDb;   // guarded by referenceLock
    double pendingReferenceRate = 0.0;       // guarded by referenceLock
    std::vector<float> referenceDb;          // dB per bin. Empty means none.
    double referenceRate = 0.0;
    bool dragHover = false;

    // Per-pixel paint scratch, one entry per x column. Members rather than
    // paint() locals so the 60 Hz repaint reuses capacity instead of hitting
    // the allocator; resized only when the width changes.
    std::vector<float> avgDbScratch;
    std::vector<float> peakDbScratch;
    std::vector<float> sideDbScratch;
    std::vector<float> overlayDbScratch;
    std::vector<float> smoothScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};
