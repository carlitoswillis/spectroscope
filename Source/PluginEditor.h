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

    juce::String readoutText { "STANDBY" };
    bool signalPresent = false;
    bool dropsSeen = false;
    int droppedCount = 0;

    // Declared last: the windows borrow views owned above, so they must die
    // before them — the default destructor then needs no help.
    std::array<std::unique_ptr<InstrumentWindow>, numPanes> instrumentWindows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroscopeAudioProcessorEditor)
};
