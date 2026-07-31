/*
    Headless preview renderer.

    Builds the editor, drives synthetic audio through the processor, pumps the
    message loop so the views fill with real analysis data, then paints the
    whole thing into a PNG. No display server required.

    This exists so the visual design can be reviewed without a Mac and without
    a DAW — the picture it produces is the same one the plugin draws.

        RenderPreview <output.png> [width] [height]
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
    /** A sweep plus periodic transients: enough structure to show whether the
        spectrogram, the envelope and the time alignment all look right.
    */
    void fillTestSignal (juce::AudioBuffer<float>& buffer, double sampleRate,
                         int64_t startSample, double& sweepPhase)
    {
        const auto numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            const auto n = static_cast<double> (startSample + i);
            const auto seconds = n / sampleRate;

            // Exponential sweep, 80 Hz to 12 kHz over four seconds, looping.
            const auto sweepPosition = std::fmod (seconds, 4.0) / 4.0;
            const auto frequency = 80.0 * std::pow (12000.0 / 80.0, sweepPosition);

            // Phase has to be integrated, not evaluated. Using f(t)*t directly
            // makes the instantaneous frequency d/dt[f(t)t], which races past
            // Nyquist and aliases back down the screen as a second sweep.
            sweepPhase += juce::MathConstants<double>::twoPi * frequency / sampleRate;

            if (sweepPhase > juce::MathConstants<double>::twoPi)
                sweepPhase -= juce::MathConstants<double>::twoPi;

            auto value = 0.35 * std::sin (sweepPhase);

            // A quiet harmonic stack, so there's something in the upper bands.
            value += 0.10 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * seconds);
            value += 0.05 * std::sin (juce::MathConstants<double>::twoPi * 1320.0 * seconds);

            // Transient every half second, to check the two views line up.
            const auto intoBeat = std::fmod (seconds, 0.5);

            if (intoBeat < 0.02)
                value += 0.55 * std::exp (-intoBeat * 220.0)
                              * std::sin (juce::MathConstants<double>::twoPi * 90.0 * seconds);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, static_cast<float> (value));
        }
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File output (argc > 1 ? juce::String (argv[1])
                                      : juce::File::getCurrentWorkingDirectory()
                                            .getChildFile ("preview.png").getFullPathName());

    const auto width  = argc > 2 ? juce::String (argv[2]).getIntValue() : 940;
    const auto height = argc > 3 ? juce::String (argv[3]).getIntValue() : 600;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    SpectroscopeAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "editor could not be created" << std::endl;
        return 1;
    }

    editor->setSize (width, height);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    int64_t sampleCounter = 0;
    double sweepPhase = 0.0;

    // Roughly six seconds of audio, interleaved with message-loop time so the
    // view timers actually run and drain the analysis queues.
    const auto blocksToRun = static_cast<int> (6.0 * sampleRate / blockSize);

    for (int block = 0; block < blocksToRun; ++block)
    {
        fillTestSignal (buffer, sampleRate, sampleCounter, sweepPhase);
        sampleCounter += blockSize;

        processor.processBlock (buffer, midi);

        // One block is ~10.7 ms of audio; give the loop a comparable slice so
        // the analysis thread and the 60 Hz view timers keep pace.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (11);
    }

    // Let the final columns land.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    juce::Image image (juce::Image::ARGB, width, height, true);

    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    editor.reset();
    processor.releaseResources();

    output.deleteFile();

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (output.createOutputStream()))
    {
        juce::PNGImageFormat png;

        if (! png.writeImageToStream (image, *stream))
        {
            std::cerr << "failed to encode PNG" << std::endl;
            return 1;
        }
    }
    else
    {
        std::cerr << "could not open " << output.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "wrote " << output.getFullPathName() << " (" << width << "x" << height << ")"
              << std::endl;
    return 0;
}
