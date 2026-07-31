#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <vector>
#include <array>
#include <atomic>
#include <memory>
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

    With the GPU path enabled, the same ring lives in a GL_R8 texture and the
    log-frequency remap, dB normalisation and palette lookup all happen in the
    fragment shader — so those operations become uniform updates instead of
    full-history re-renders. The context is attached to this component alone
    (never the editor: whole-editor GL is slower than CoreGraphics on modern
    macOS and has a history of host DPI bugs), and the CPU image path stays
    intact underneath as the fallback and for headless rendering.
*/
class SpectrogramView final : public juce::Component,
                              private juce::Timer,
                              private juce::OpenGLRenderer
{
public:
    explicit SpectrogramView (AnalysisEngine&);
    ~SpectrogramView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setDecibelRange (float floorDb, float ceilingDb);

    /** Re-fetches the palette's colour table and repaints history in it. */
    void themeChanged();

    /** Drops all accumulated history — dark glass, as though just powered on. */
    void clear();

    /** The spectrum column queue feeds whichever view is active; an inactive
        view stops its timer so it never steals columns from the other.
    */
    void setActive (bool shouldBeActive);

    /** Switches the raster to a GL ring texture rendered by a context attached
        to this component. Off by default, and must stay off in DAW/headless
        builds — the CPU image path remains the fallback either way.
    */
    void setGpuEnabled (bool shouldUseGpu);

private:
    void timerCallback() override;
    void rebuildImage();
    void rebuildRowMapping();
    void renderColumnIntoImage (const float* column, int imageColumn);
    void reRenderAllHistory();
    void drawFrequencyGrid (juce::Graphics&);
    void drawCursorReadout (juce::Graphics&);

    // juce::OpenGLRenderer — these run on the context's render thread.
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // Message-thread producers for the GL staging area.
    void stageColumnForGl (const float* column);
    void stageFullHistoryForGl();
    void stagePaletteForGl();
    void stageUniformsForGl();

    // GL-thread helper.
    void reallocateHistoryTexture (int bins);

    static constexpr int historyColumns = 4096;
    static constexpr int maxColumnsPerFrame = 128;

    /** The GL ring is capped below the CPU ring: no display is 4096 logical
        pixels wide, and halving the texture halves the reset upload.
    */
    static constexpr int glRingColumns = 2048;

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

    // Component-layer cursor readout. -1 means the pointer is outside the
    // view; this paints above the GL raster on the message thread same as
    // the frequency grid, so the GPU path needs no changes of its own.
    juce::Point<int> cursor { -1, -1 };

    //==========================================================================
    // GPU path.

    bool gpuEnabled = false;
    juce::OpenGLContext glContext;

    /** Hand-off from the message thread to the render thread. Columns are
        normalised to bytes over the fixed StftAnalyzer::floorDb..0 range, so a
        change of display range is a uniform, not a re-upload.
    */
    struct GlStaging
    {
        std::vector<juce::uint8> columns;   // numBins bytes per column, oldest first
        int numColumns = 0;
        int numBins = 0;
        bool reset = false;                 // texture contents are stale; zero before uploading

        std::array<juce::uint8, 256 * 4> palette {};
        bool paletteDirty = false;

        float dbFloor = -75.0f;
        float dbCeiling = 0.0f;
        double nyquist = 0.0;
        float scanlineAlpha = 0.0f;
        float vignetteAlpha = 0.0f;
    };

    juce::CriticalSection glLock;
    GlStaging glStaging;                    // guarded by glLock

    // The component's bounds belong to the message thread; the render thread
    // reads these cached copies instead.
    std::atomic<int> glViewWidth { 1 };
    std::atomic<int> glViewHeight { 1 };

    // A recreated context arrives with empty textures; the timer restages the
    // whole CPU ring when it sees this.
    std::atomic<bool> glContextReset { false };

    // Render-thread-only state — no lock needed.
    std::unique_ptr<juce::OpenGLShaderProgram> glProgram;
    std::vector<juce::uint8> glUploadScratch;
    juce::uint32 glHistoryTexture = 0;
    juce::uint32 glPaletteTexture = 0;
    juce::uint32 glQuadBuffer = 0;
    juce::uint32 glVertexArray = 0;
    int glTextureBins = 0;
    int glWriteColumn = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramView)
};
