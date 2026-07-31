#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include "../dsp/AnalysisEngine.h"
#include "ColourMaps.h"

/**
    Scrolling spectrogram, newest column at the right edge, frequency on Y.

    The image is a ring: new columns overwrite the oldest in place and drawing
    is two blits with a shifting offset, rather than moving the whole image left
    every frame. That's the CPU rehearsal of the ring texture Phase 4 uploads to
    the GPU.

    Raw dB columns are kept alongside the image so a resize, a palette switch
    or a sample-rate change can re-render history instead of losing it.
*/
class SpectrogramView final : public juce::Component,
                              private juce::Timer
{
public:
    explicit SpectrogramView (AnalysisEngine&);
    ~SpectrogramView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setDecibelRange (float floorDb, float ceilingDb);

    /** Re-fetches the palette's colour table and repaints history in it. */
    void themeChanged();

    /** Drops all accumulated history — dark glass, as though just powered on. */
    void clear();

    /** The spectrum column queue feeds whichever view is active; an inactive
        view stops its timer so it never steals columns from the other.
    */
    void setActive (bool shouldBeActive);

private:
    void timerCallback() override;
    void rebuildImage();
    void rebuildRowMapping();
    void renderColumnIntoImage (const float* column, int imageColumn);
    void reRenderAllHistory();
    void drawFrequencyGrid (juce::Graphics&);

    static constexpr int historyColumns = 4096;
    static constexpr int maxColumnsPerFrame = 128;

    /** Bottom edge of the log frequency axis. 30 Hz keeps the lowest useful
        octave on screen without spending rows on sub-audio rumble.
    */
    static constexpr double minFrequencyHz = 30.0;

    AnalysisEngine& engine;

    std::vector<float> history;   // numBins * historyColumns, ring
    std::vector<float> scratch;   // maxColumnsPerFrame * numBins
    int historyWrite = 0;
    int numStored = 0;
    int numBins = 0;

    juce::Image image;
    int imageWrite = 0;

    std::vector<int>   rowBinLow;     // per screen row, inclusive
    std::vector<int>   rowBinHigh;
    std::vector<float> rowBinCentre;  // fractional bin at the row's centre frequency

    std::array<juce::PixelARGB, 256> colourTable {};

    // -75 dB sits just under the noise floor of well-recorded material, so the
    // background stays dark instead of glowing with broadband hiss.
    float dbFloor = -75.0f;
    float dbCeiling = 0.0f;

    double lastMappedRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramView)
};
