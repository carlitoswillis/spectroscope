#include "CrtOverlay.h"
#include "Theme.h"

namespace
{
    constexpr int   scanlinePitch   = 3;      // pixels between dark lines
    constexpr float scanlineAlpha   = 0.16f;
    constexpr float vignetteAlpha   = 0.55f;
    constexpr float cornerTintAlpha = 0.10f;
}

CrtOverlay::CrtOverlay()
{
    // Decoration only — every click belongs to whatever is underneath.
    setInterceptsMouseClicks (false, false);
}

void CrtOverlay::resized()
{
    rebuild();
}

void CrtOverlay::rebuild()
{
    const auto w = getWidth();
    const auto h = getHeight();

    if (w <= 0 || h <= 0)
    {
        overlay = {};
        return;
    }

    overlay = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (overlay);

    // Scanlines. Dark rather than light, so they read as gaps between glowing
    // lines instead of stripes painted over the picture.
    g.setColour (juce::Colours::black.withAlpha (scanlineAlpha));

    for (int y = 0; y < h; y += scanlinePitch)
        g.fillRect (0, y, w, 1);

    // Vignette: transparent at the centre, dark at the corners. The radius
    // deliberately overshoots the frame so the falloff stays gentle.
    const auto centre = juce::Point<float> (w * 0.5f, h * 0.5f);
    const auto radius = juce::jmax (static_cast<float> (w), static_cast<float> (h)) * 0.78f;

    juce::ColourGradient vignette (juce::Colours::transparentBlack, centre,
                                   juce::Colours::black.withAlpha (vignetteAlpha),
                                   centre.translated (radius, 0.0f), true);

    vignette.addColour (0.55, juce::Colours::transparentBlack);
    vignette.addColour (0.80, juce::Colours::black.withAlpha (vignetteAlpha * 0.35f));

    g.setGradientFill (vignette);
    g.fillRect (0, 0, w, h);

    // A breath of amber in the corners, as though the phosphor coating carries
    // a little colour even where nothing is lit.
    juce::ColourGradient tint (juce::Colours::transparentBlack, centre,
                               Theme::amber.withAlpha (cornerTintAlpha),
                               centre.translated (radius, 0.0f), true);

    tint.addColour (0.7, juce::Colours::transparentBlack);

    g.setGradientFill (tint);
    g.fillRect (0, 0, w, h);
}

void CrtOverlay::paint (juce::Graphics& g)
{
    if (overlay.isValid())
        g.drawImageAt (overlay, 0, 0);
}
