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

    Raw dB columns are kept alongside the image so a resize — or, later, a
    switch to a log frequency axis — can re-render history instead of losing it.
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

private:
    void timerCallback() override;
    void rebuildImage();
    void rebuildRowMapping();
    void renderColumnIntoImage (const float* column, int imageColumn);
    void reRenderAllHistory();

    static constexpr int historyColumns = 4096;
    static constexpr int maxColumnsPerFrame = 128;

    AnalysisEngine& engine;

    std::vector<float> history;   // numBins * historyColumns, ring
    std::vector<float> scratch;   // maxColumnsPerFrame * numBins
    int historyWrite = 0;
    int numStored = 0;
    int numBins = 0;

    juce::Image image;
    int imageWrite = 0;

    std::vector<int> rowBinLow;   // per screen row, inclusive
    std::vector<int> rowBinHigh;

    std::array<juce::PixelARGB, 256> colourTable { ColourMaps::buildTable (ColourMaps::magma) };

    float dbFloor = -90.0f;
    float dbCeiling = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramView)
};
