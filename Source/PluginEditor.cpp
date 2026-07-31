#include "PluginEditor.h"
#include "gui/Theme.h"

namespace
{
    constexpr int outerMargin  = 11;
    constexpr int headerHeight = 38;
    constexpr int labelHeight  = 13;
    constexpr int footerHeight = 16;
    constexpr int screenInset  = 4;
    constexpr int paneGap      = 10;
    constexpr int railHeight   = 18;
    constexpr int switchWidth  = 66;
    constexpr int switchGap    = 4;

    // Relative heights when a pane is enabled, normalised over the enabled set.
    constexpr float paneWeights[] = { 0.62f, 1.0f, 1.0f, 1.0f, 0.75f, 0.75f };
}

SpectroscopeAudioProcessorEditor::SpectroscopeAudioProcessorEditor (SpectroscopeAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      waveformView (p.getAnalysisEngine()),
      spectrogramView (p.getAnalysisEngine()),
      spectrumView (p.getAnalysisEngine()),
      stereoFieldView (p.getAnalysisEngine()),
      loudnessView (p.getAnalysisEngine()),
      oscilloscopeView (p.getAnalysisEngine())
{
    Theme::setCurrent (processor.getThemeIndex());

    // The spectrogram cached its colour table during member construction,
    // before the saved livery was applied above.
    spectrogramView.themeChanged();

    addAndMakeVisible (waveformView);
    addAndMakeVisible (spectrogramView);
    addAndMakeVisible (spectrumView);
    addAndMakeVisible (stereoFieldView);
    addAndMakeVisible (loudnessView);
    addAndMakeVisible (oscilloscopeView);

    // Last, so the glass sits in front of everything behind it.
    addAndMakeVisible (crtOverlay);

    applyPanes (processor.getPanesMask());

    // Cmd+R clears the displays. Hosts usually eat keystrokes before a plugin
    // editor sees them, which is what the CLR switch is for.
    setWantsKeyboardFocus (true);

    setResizable (true, true);
    setResizeLimits (520, 360, 4096, 2400);
    setSize (940, 600);

    startTimerHz (12);
}

juce::Component& SpectroscopeAudioProcessorEditor::paneView (int index) noexcept
{
    juce::Component* const views[numPanes] = { &waveformView, &spectrogramView, &spectrumView,
                                               &stereoFieldView, &loudnessView, &oscilloscopeView };
    return *views[index];
}

void SpectroscopeAudioProcessorEditor::applyPanes (int mask)
{
    // The processor coerces an empty mask to the waveform, so the console
    // can never go dark whatever it was handed.
    processor.setPanesMask (mask);
    mask = processor.getPanesMask();

    for (int i = 0; i < numPanes; ++i)
        paneView (i).setVisible ((mask & (1 << i)) != 0);

    // Each queue owner only drains while active. The waveform samples its
    // ring in place rather than draining, so visibility alone gates it.
    spectrogramView.setActive ((mask & (1 << 1)) != 0);
    spectrumView.setActive ((mask & (1 << 2)) != 0);
    stereoFieldView.setActive ((mask & (1 << 3)) != 0);
    loudnessView.setActive ((mask & (1 << 4)) != 0);
    oscilloscopeView.setActive ((mask & (1 << 5)) != 0);

    resized();
    repaint();
}

void SpectroscopeAudioProcessorEditor::clearAllDisplays()
{
    waveformView.clear();
    spectrogramView.clear();
    spectrumView.clear();
    stereoFieldView.clear();
    loudnessView.clear();
    oscilloscopeView.clear();
}

void SpectroscopeAudioProcessorEditor::toggleFullScreen()
{
    if (! juce::JUCEApplicationBase::isStandaloneApp())
        return;

    auto& desktop = juce::Desktop::getInstance();
    auto* window = getTopLevelComponent();

    desktop.setKioskModeComponent (desktop.getKioskModeComponent() == window ? nullptr : window,
                                   false);
    repaint (headerArea.expanded (2));
}

bool SpectroscopeAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.isKeyCurrentlyDown ('R'))
    {
        clearAllDisplays();
        return true;
    }

    if (key == juce::KeyPress::escapeKey
        && juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent())
    {
        toggleFullScreen();
        return true;
    }

    return false;
}

void SpectroscopeAudioProcessorEditor::cycleTheme()
{
    const auto next = (Theme::currentIndex + 1) % static_cast<int> (Theme::palettes.size());

    Theme::setCurrent (next);
    processor.setThemeIndex (next);

    spectrogramView.themeChanged();
    crtOverlay.themeChanged();
    repaint();
}

void SpectroscopeAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (themeSwitchArea.contains (position))
    {
        cycleTheme();
        return;
    }

    if (clearSwitchArea.contains (position))
    {
        clearAllDisplays();
        return;
    }

    if (fullScreenSwitchArea.contains (position))
    {
        toggleFullScreen();
        return;
    }

    for (int i = 0; i < numPanes; ++i)
    {
        if (switchAreas[i].contains (position))
        {
            // Switching off the last lit pane is ignored rather than having
            // the coercion silently re-light a different one.
            const auto mask = processor.getPanesMask() ^ (1 << i);

            if (mask != 0)
                applyPanes (mask);

            return;
        }
    }
}

void SpectroscopeAudioProcessorEditor::mouseMove (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();
    auto clickable = themeSwitchArea.contains (position)
                  || clearSwitchArea.contains (position)
                  || fullScreenSwitchArea.contains (position);

    for (const auto& area : switchAreas)
        clickable = clickable || area.contains (position);

    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor
                              : juce::MouseCursor::NormalCursor);
}

void SpectroscopeAudioProcessorEditor::timerCallback()
{
    auto& engine = processor.getAnalysisEngine();

    const auto rate = processor.getCurrentSampleRate();
    const auto block = processor.getCurrentBlockSize();
    const auto dropped = engine.getNumDroppedBlocks();
    const auto peak = engine.getRecentPeak();

    const auto newSignal = peak > 0.0005f;
    const auto newDropped = dropped > 0;

    auto text = rate > 0.0
        ? juce::String (rate / 1000.0, 1) + "K  " + juce::String (block) + "SMP"
        : juce::String ("STANDBY");

    if (text != readoutText || newSignal != signalPresent || newDropped != dropsSeen)
    {
        readoutText = text;
        signalPresent = newSignal;
        dropsSeen = newDropped;
        droppedCount = dropped;
        repaint (headerArea.expanded (2));
    }
}

void SpectroscopeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (outerMargin);

    crtOverlay.setBounds (getLocalBounds());

    headerArea = area.removeFromTop (headerHeight);
    footerArea = area.removeFromBottom (footerHeight);
    area.removeFromBottom (4);

    // The livery switch sits between the lamp cluster and the readout, with
    // the display-clear and fullscreen switches to its left.
    themeSwitchArea = headerArea.withTrimmedRight (160)
                                .removeFromRight (juce::jmin (128, headerArea.getWidth() / 4))
                                .withTrimmedTop (4).withHeight (20);

    clearSwitchArea = themeSwitchArea.translated (-themeSwitchArea.getWidth(), 0)
                                     .removeFromRight (40).translated (-6, 0);

    fullScreenSwitchArea = juce::JUCEApplicationBase::isStandaloneApp()
        ? clearSwitchArea.translated (-46, 0)
        : juce::Rectangle<int>();

    // The switch rail sits directly under the header hairline: one latching
    // channel switch per pane, left-aligned.
    area.removeFromTop (2);

    auto rail = area.removeFromTop (railHeight);

    for (auto& switchArea : switchAreas)
    {
        switchArea = rail.removeFromLeft (switchWidth);
        rail.removeFromLeft (switchGap);
    }

    area.removeFromTop (paneGap);

    // Enabled panes stack vertically, each a caption row over its screen,
    // heights split by weight. All panes share left and right edges so the
    // time-axis instruments line up pixel for pixel. Disabled panes keep
    // stale bounds — they are invisible, and paint() skips their surrounds.
    const auto mask = processor.getPanesMask();

    auto enabledCount = 0;
    auto totalWeight = 0.0f;

    for (int i = 0; i < numPanes; ++i)
    {
        if ((mask & (1 << i)) != 0)
        {
            ++enabledCount;
            totalWeight += paneWeights[i];
        }
    }

    const auto viewSpace = area.getHeight()
                         - enabledCount * labelHeight
                         - (enabledCount - 1) * paneGap;

    auto placed = 0;

    for (int i = 0; i < numPanes; ++i)
    {
        if ((mask & (1 << i)) == 0)
            continue;

        if (placed > 0)
            area.removeFromTop (paneGap);

        // The last pane absorbs the rounding remainder.
        const auto last = (++placed == enabledCount);
        const auto viewHeight = last
            ? area.getHeight() - labelHeight
            : juce::roundToInt (static_cast<float> (viewSpace) * paneWeights[i] / totalWeight);

        auto pane = area.removeFromTop (labelHeight + juce::jmax (0, viewHeight));
        paneLabelAreas[i] = pane.removeFromTop (labelHeight);
        paneView (i).setBounds (pane.reduced (screenInset, 0));
    }
}

void SpectroscopeAudioProcessorEditor::paintScreenSurround (juce::Graphics& g,
                                                            juce::Rectangle<int> screen,
                                                            juce::Rectangle<int> labelArea,
                                                            juce::StringRef caption,
                                                            juce::StringRef annotation)
{
    Theme::drawRecessedScreen (g, screen);
    Theme::drawCornerBrackets (g, screen.expanded (6), Theme::palette().amberDim.withAlpha (0.75f), 12, 1.4f);

    auto label = labelArea.reduced (10, 0);

    g.setColour (Theme::palette().bone.withAlpha (0.85f));
    g.setFont (Theme::mono (9.5f, true));
    g.drawText (Theme::spaced (caption), label.removeFromLeft (label.getWidth() / 2),
                juce::Justification::centredLeft);

    g.setColour (Theme::palette().boneDim.withAlpha (0.65f));
    g.setFont (Theme::mono (9.0f));
    g.drawText (annotation, label, juce::Justification::centredRight);
}

void SpectroscopeAudioProcessorEditor::paintSwitchRail (juce::Graphics& g)
{
    const juce::String labels[numPanes] = { "WAVE", "DENSITY", "SPECTRUM",
                                            "FIELD", "CHART", "SCOPE" };
    const auto mask = processor.getPanesMask();

    for (int i = 0; i < numPanes; ++i)
    {
        const auto engaged = (mask & (1 << i)) != 0;
        auto plate = switchAreas[i];

        g.setColour (engaged ? Theme::palette().amber.withAlpha (0.7f)
                             : Theme::palette().boneDim.withAlpha (0.3f));
        g.drawRect (plate, 1);

        Theme::drawLamp (g, plate.removeFromLeft (14).toFloat().withSizeKeepingCentre (5.0f, 5.0f),
                         Theme::palette().amber, engaged);

        g.setColour (engaged ? Theme::palette().bone
                             : Theme::palette().boneDim.withAlpha (0.5f));
        g.setFont (Theme::mono (8.0f, true));
        g.drawText (labels[i], plate.withTrimmedRight (3), juce::Justification::centredLeft);
    }
}

void SpectroscopeAudioProcessorEditor::paintHeader (juce::Graphics& g)
{
    auto header = headerArea;

    // Title block. Position and metrics stay identical whatever the livery's
    // title style, so switching never reflows the header.
    auto titleArea = header.removeFromLeft (juce::jmin (330, header.getWidth() / 2));
    const auto titleRow = titleArea.removeFromTop (21);
    const auto titleText = Theme::spaced ("SPECTROSCOPE");
    const juce::Font titleFont (Theme::mono (17.0f, true));

    switch (Theme::palette().titleStyle)
    {
        case Theme::TitleStyle::filled:
            g.setColour (Theme::palette().amber);
            g.setFont (titleFont);
            g.drawText (titleText, titleRow, juce::Justification::topLeft);
            break;

        case Theme::TitleStyle::outline:
        {
            // Hollow boot-screen lettering: stroked glyph outlines, no fill.
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (titleFont, titleText,
                                  static_cast<float> (titleRow.getX()),
                                  static_cast<float> (titleRow.getY()) + titleFont.getAscent());

            juce::Path outline;
            glyphs.createPath (outline);

            g.setColour (Theme::palette().amber);
            g.strokePath (outline, juce::PathStrokeType (1.1f));
            break;
        }

        case Theme::TitleStyle::plate:
        {
            // Signage placard: a filled rounded plate with the text knocked
            // out. Vertical padding is tighter so it clears the subtitle.
            const auto textWidth = juce::GlyphArrangement::getStringWidth (titleFont, titleText);
            const auto plate = juce::Rectangle<float> (textWidth, titleFont.getHeight())
                                   .withPosition (titleRow.toFloat().getPosition())
                                   .expanded (6.0f, 1.5f);

            g.setColour (Theme::palette().amber);
            g.fillRoundedRectangle (plate, 3.0f);

            g.setColour (Theme::palette().shellMid);
            g.setFont (titleFont);
            g.drawText (titleText, titleRow, juce::Justification::topLeft);
            break;
        }
    }

    g.setColour (Theme::palette().boneDim.withAlpha (0.7f));
    g.setFont (Theme::mono (8.5f));
    g.drawText (Theme::spaced (juce::String (Theme::palette().subtitle)),
                titleArea, juce::Justification::topLeft);

    // Readout, right-aligned.
    auto readout = header.removeFromRight (juce::jmin (150, header.getWidth()));

    g.setColour (Theme::palette().amberBright.withAlpha (0.9f));
    g.setFont (Theme::mono (13.0f, true));
    g.drawText (readoutText, readout.removeFromTop (18), juce::Justification::topRight);

    g.setColour (Theme::palette().boneDim.withAlpha (0.55f));
    g.setFont (Theme::mono (8.5f));
    g.drawText (dropsSeen ? juce::String (droppedCount) + " FRAMES LOST"
                          : juce::String ("SIGNAL PATH NOMINAL"),
                readout, juce::Justification::topRight);

    // The livery switch: a silkscreened toggle showing the current unit.
    if (! themeSwitchArea.isEmpty())
    {
        g.setColour (Theme::palette().boneDim.withAlpha (0.5f));
        g.drawRect (themeSwitchArea, 1);

        g.setColour (Theme::palette().bone.withAlpha (0.8f));
        g.setFont (Theme::mono (8.5f, true));
        g.drawText (Theme::spaced (juce::String (Theme::palette().unit) + " " + Theme::palette().name),
                    themeSwitchArea, juce::Justification::centred);
    }

    // Momentary switches beside it: display clear, and — standalone only —
    // fullscreen. Same silkscreen as the livery switch, so the header reads
    // as one bank of controls.
    const auto drawMomentary = [&g] (juce::Rectangle<int> area, juce::StringRef label)
    {
        if (area.isEmpty())
            return;

        g.setColour (Theme::palette().boneDim.withAlpha (0.5f));
        g.drawRect (area, 1);

        g.setColour (Theme::palette().bone.withAlpha (0.8f));
        g.setFont (Theme::mono (8.5f, true));
        g.drawText (Theme::spaced (label), area, juce::Justification::centred);
    };

    drawMomentary (clearSwitchArea, "CLR");
    drawMomentary (fullScreenSwitchArea,
                   juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent()
                       ? "EXIT" : "FULL");

    // Lamp cluster between the title and the switch.
    const juce::String lampNames[] = { "PWR", "SIG", "ERR" };
    const juce::Colour lampColours[] = { Theme::palette().amber, Theme::palette().phosphor, Theme::palette().rust };
    const bool lampStates[] = { true, signalPresent, dropsSeen };

    auto lamps = header.removeFromLeft (juce::jmin (140, header.getWidth()))
                       .withTrimmedTop (3).withHeight (26);

    for (int i = 0; i < 3; ++i)
    {
        auto slot = lamps.removeFromLeft (44);

        Theme::drawLamp (g, slot.removeFromTop (13).toFloat().withSizeKeepingCentre (7.0f, 7.0f),
                         lampColours[i], lampStates[i]);

        g.setColour (lampStates[i] ? Theme::palette().bone.withAlpha (0.8f) : Theme::palette().boneDim.withAlpha (0.4f));
        g.setFont (Theme::mono (8.0f, true));
        g.drawText (lampNames[i], slot, juce::Justification::centredTop);
    }
}

void SpectroscopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Chassis with a slight gradient, as though lit from above.
    g.setGradientFill (juce::ColourGradient (Theme::palette().shellMid, 0.0f, 0.0f,
                                             Theme::palette().shellDark, 0.0f, static_cast<float> (getHeight()),
                                             false));
    g.fillAll();

    paintHeader (g);

    // Hairline under the header, broken where the title sits — a panel seam.
    g.setColour (Theme::palette().bezelHi.withAlpha (0.6f));
    g.drawHorizontalLine (headerArea.getBottom() - 1,
                          static_cast<float> (headerArea.getX()),
                          static_cast<float> (headerArea.getRight()));

    paintSwitchRail (g);

    const auto nyquist = processor.getCurrentSampleRate() * 0.5;
    const auto kHz = nyquist > 0.0 ? juce::String (nyquist / 1000.0, 1) + " KHZ" : juce::String ("-- KHZ");

    const juce::String captions[numPanes] = { "WAVEFORM", "SPECTRAL DENSITY", "SPECTRUM",
                                              "STEREO FIELD", "LEVEL CHART", "OSCILLOSCOPE" };
    const juce::String annotations[numPanes] = { "CHANNEL SUM / PEAK + RMS",
                                                 "LOG 30 HZ-" + kHz,
                                                 "MID + SIDE + PEAK HOLD",
                                                 "LISSAJOUS + VU + CORR",
                                                 "MOMENTARY / 60 S",
                                                 "TRIGGERED SWEEP" };

    // Hidden panes keep stale bounds, so only the visible ones get surrounds.
    const auto mask = processor.getPanesMask();

    for (int i = 0; i < numPanes; ++i)
        if ((mask & (1 << i)) != 0)
            paintScreenSurround (g, paneView (i).getBounds(), paneLabelAreas[i],
                                 captions[i], annotations[i]);

    // Footer readouts.
    auto footer = footerArea;

    g.setFont (Theme::mono (9.0f));
    g.setColour (Theme::palette().boneDim.withAlpha (0.75f));

    g.drawText ("FFT " + juce::String (1 << AnalysisEngine::fftOrder)
                       + " / HOP " + juce::String (AnalysisEngine::hopSize),
                footer.removeFromLeft (170), juce::Justification::centredLeft);

    g.drawText ("LATENCY 0 MS", footer.removeFromRight (120), juce::Justification::centredRight);

    const auto span = waveformView.getVisibleTimeSpan();

    g.setColour (Theme::palette().amber.withAlpha (0.7f));
    g.drawText (span > 0.0 ? juce::String (span, 2) + " S WINDOW" : juce::String ("-- S WINDOW"),
                footer, juce::Justification::centred);

    // Chassis fasteners.
    const auto inset = static_cast<float> (outerMargin) - 4.0f;
    const auto right = static_cast<float> (getWidth()) - inset;
    const auto bottom = static_cast<float> (getHeight()) - inset;

    for (const auto& corner : { juce::Point<float> (inset, inset),
                                juce::Point<float> (right, inset),
                                juce::Point<float> (inset, bottom),
                                juce::Point<float> (right, bottom) })
        Theme::drawRivet (g, corner);
}
