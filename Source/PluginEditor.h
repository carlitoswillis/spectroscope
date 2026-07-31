#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "PluginProcessor.h"
#include "gui/WaveformView.h"
#include "gui/SpectrogramView.h"
#include "gui/SpectrumView.h"
#include "gui/StereoFieldView.h"
#include "gui/LoudnessHistoryView.h"
#include "gui/OscilloscopeView.h"
#include "gui/CrtOverlay.h"
#include "gui/InstrumentWindow.h"
#include "gui/BootCard.h"

/**
    A console of instrument panes set into a chassis behind CRT glass.

    Six instruments — waveform, scrolling spectrogram, instantaneous spectrum
    with peak hold, stereo field (Lissajous scope with VU and correlation
    meters), momentary-loudness history chart, and triggered-sweep
    oscilloscope — show in any combination, stacked vertically. The switch
    rail under the header latches each pane in or out; at least one always
    stays lit. The livery switch in the header cycles the palettes; both the
    pane mask and the livery persist in the plugin state.

    Any docked pane can pop out into its own always-on-top InstrumentWindow
    via the glyph at the right of its caption row; closing the window docks
    it back. The same view component moves between console and window — never
    a copy — and the floating set with its window bounds rides the plugin
    state alongside the pane mask.

    Right-justified on the same rail, a bank of utility plates: STORE freezes
    comparison traces on the spectrum and stereo field, MARK stamps a pen tick
    on the level chart, PHOTO saves the console to the desktop with a data
    strip along the bottom, LOG writes a full session report folder, and TONE
    latches the processor's -18 dBFS alignment sine into the meters. The
    processor's trigger parameters (clear / store / marker) are polled on the
    timer so hosts can automate the same actions, and the standalone opens
    behind a boot test card that dismisses itself.
*/
class SpectroscopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SpectroscopeAudioProcessorEditor (SpectroscopeAudioProcessor&);
    ~SpectroscopeAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void paintHeader (juce::Graphics&);
    void paintSwitchRail (juce::Graphics&);
    void paintScreenSurround (juce::Graphics&,
                              juce::Rectangle<int> screen,
                              juce::Rectangle<int> labelArea,
                              juce::StringRef caption,
                              juce::StringRef annotation);

    void applyPanes (int mask);
    void cycleTheme();

    /** Lifts a pane's view out of the stack into its own InstrumentWindow. */
    void floatPane (int index);

    /** Closes a pane's window and returns its view to the console stack. */
    void dockPane (int index);

    /** Re-serialises every live window's bounds into the processor. */
    void storeWindowLayout();

    juce::String paneCaption (int index) const;
    juce::String paneAnnotation (int index) const;

    /** Wipes every instrument's accumulated history — new song, dark glass. */
    void clearAllDisplays();

    /** Standalone only: the whole console takes the display. */
    void toggleFullScreen();

    /** STORE: one latching comparison memory across both trace-holding
        instruments — engaged while either still holds one.
    */
    void toggleStoredTraces();

    /** PHOTO: the console to a desktop PNG with a data strip along the
        bottom of the saved image.
    */
    void capturePhoto();

    /** LOG: SessionReport folder on the desktop, then revealed. */
    void writeSessionLog();

    /** Routes one utility plate press; index in rail order. */
    void railPlatePressed (int index);

    /** Standalone spectrogram renders through GL, which snapshots black:
        wraps an action in the same CPU-raster pause float/dock uses.
    */
    void withGpuPaused (const std::function<void()>& action);

    static constexpr int numPanes = 6;
    juce::Component& paneView (int index) noexcept;

    SpectroscopeAudioProcessor& processor;
    WaveformView waveformView;
    SpectrogramView spectrogramView;
    SpectrumView spectrumView;
    StereoFieldView stereoFieldView;
    LoudnessHistoryView loudnessView;
    OscilloscopeView oscilloscopeView;
    CrtOverlay crtOverlay;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> footerArea;
    juce::Rectangle<int> themeSwitchArea;
    juce::Rectangle<int> clearSwitchArea;
    juce::Rectangle<int> fullScreenSwitchArea;
    std::array<juce::Rectangle<int>, numPanes> switchAreas;
    std::array<juce::Rectangle<int>, numPanes> paneLabelAreas;
    std::array<juce::Rectangle<int>, numPanes> popOutAreas;

    // Utility plates, right-justified on the switch rail. In rail order:
    // STORE, MARK, PHOTO, LOG, TONE. An empty rectangle means the plate was
    // dropped for width.
    static constexpr int numRailPlates = 5;
    juce::Rectangle<int> railArea;
    std::array<juce::Rectangle<int>, numRailPlates> railPlateAreas;

    juce::String readoutText { "STANDBY" };
    bool signalPresent = false;
    bool dropsSeen = false;
    int droppedCount = 0;

    // Engaged lamps cached so the timer repaints the rail only on change.
    bool storePlateEngaged = false;
    bool tonePlateEngaged = false;

    // Trigger-parameter counts last seen by the timer; a diff fires the action.
    int lastClearEvents = 0;
    int lastStoreEvents = 0;
    int lastMarkerEvents = 0;

    // Standalone only: the test card shown over everything at launch.
    std::unique_ptr<BootCard> bootCard;

    // Declared last: the windows borrow views owned above, so they must die
    // before them — the default destructor then needs no help.
    std::array<std::unique_ptr<InstrumentWindow>, numPanes> instrumentWindows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessorEditor)
};
