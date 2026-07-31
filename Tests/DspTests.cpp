#include <juce_audio_basics/juce_audio_basics.h>

#include "../Source/dsp/SampleRingBuffer.h"
#include "../Source/dsp/LockFreeQueue.h"
#include "../Source/dsp/AnalysisEngine.h"

namespace
{
    juce::AudioBuffer<float> makeBuffer (int numChannels, int numSamples,
                                         const std::function<float (int channel, int index)>& generator)
    {
        juce::AudioBuffer<float> buffer (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, generator (ch, i));

        return buffer;
    }
}

//==============================================================================
class SampleRingBufferTests final : public juce::UnitTest
{
public:
    SampleRingBufferTests() : UnitTest ("SampleRingBuffer", "dsp") {}

    void runTest() override
    {
        beginTest ("round-trips samples in order across a wrap");
        {
            SampleRingBuffer ring;
            ring.prepare (1, 1024);

            // Far more than the capacity in total, forcing several wraps.
            int written = 0;
            juce::AudioBuffer<float> destination (1, 128);

            for (int block = 0; block < 40; ++block)
            {
                auto source = makeBuffer (1, 128, [&] (int, int i) { return static_cast<float> (written + i); });
                ring.write (source);
                written += 128;

                expectEquals (ring.read (destination, 128), 128);

                for (int i = 0; i < 128; ++i)
                    expectEquals (destination.getSample (0, i), static_cast<float> (written - 128 + i));
            }
        }

        beginTest ("reports nothing readable until a full request is available");
        {
            SampleRingBuffer ring;
            ring.prepare (1, 1024);

            ring.write (makeBuffer (1, 100, [] (int, int) { return 1.0f; }));

            juce::AudioBuffer<float> destination (1, 256);
            expectEquals (ring.read (destination, 256), 0);
            expectEquals (ring.getNumReady(), 100);
        }

        beginTest ("drops blocks instead of blocking when the consumer stalls");
        {
            SampleRingBuffer ring;
            ring.prepare (1, 1024);

            for (int i = 0; i < 20; ++i)
                ring.write (makeBuffer (1, 256, [] (int, int) { return 0.5f; }));

            expect (ring.getNumDroppedBlocks() > 0, "a full buffer should drop rather than overrun");
            expect (ring.getNumReady() <= 1024, "readable count must never exceed capacity");

            // Surviving data must still be intact, not a torn mixture.
            juce::AudioBuffer<float> destination (1, 256);
            expectEquals (ring.read (destination, 256), 256);

            for (int i = 0; i < 256; ++i)
                expectEquals (destination.getSample (0, i), 0.5f);
        }

        beginTest ("duplicates a mono source into both stored channels");
        {
            SampleRingBuffer ring;
            ring.prepare (2, 1024);
            ring.write (makeBuffer (1, 64, [] (int, int) { return 0.25f; }));

            juce::AudioBuffer<float> destination (2, 64);
            expectEquals (ring.read (destination, 64), 64);
            expectEquals (destination.getSample (0, 10), 0.25f);
            expectEquals (destination.getSample (1, 10), 0.25f);
        }
    }
};

//==============================================================================
class LockFreeQueueTests final : public juce::UnitTest
{
public:
    LockFreeQueueTests() : UnitTest ("LockFreeQueue", "dsp") {}

    void runTest() override
    {
        beginTest ("preserves order and reports fullness");
        {
            LockFreeQueue<EnvelopePoint> queue;
            queue.prepare (16);

            int pushed = 0;

            while (queue.push (EnvelopePoint { static_cast<float> (pushed), 0.0f, 0.0f }))
                ++pushed;

            expect (pushed > 0, "should accept at least one item");
            expect (! queue.push (EnvelopePoint {}), "a full queue must refuse rather than overwrite");

            std::vector<EnvelopePoint> out (static_cast<size_t> (pushed));
            expectEquals (queue.pop (out.data(), pushed), pushed);

            for (int i = 0; i < pushed; ++i)
                expectEquals (out[static_cast<size_t> (i)].minValue, static_cast<float> (i));

            expectEquals (queue.getNumReady(), 0);
        }

        beginTest ("discardPending empties the queue");
        {
            LockFreeQueue<EnvelopePoint> queue;
            queue.prepare (16);

            for (int i = 0; i < 5; ++i)
                queue.push (EnvelopePoint {});

            expectEquals (queue.getNumReady(), 5);
            queue.discardPending();
            expectEquals (queue.getNumReady(), 0);
        }
    }
};

//==============================================================================
class AnalysisEngineTests final : public juce::UnitTest
{
public:
    AnalysisEngineTests() : UnitTest ("AnalysisEngine", "dsp") {}

    void runTest() override
    {
        beginTest ("summarises a known sine into correct peak and RMS");
        {
            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 512;
            constexpr float amplitude = 0.5f;
            constexpr double frequency = 1000.0;

            AnalysisEngine engine;
            engine.prepare (sampleRate, blockSize, 2);

            expectEquals (engine.getHopSize(), 256);
            expect (std::abs (engine.getSecondsPerPoint() - 256.0 / 48000.0) < 1.0e-9);

            double phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

            // Feed a couple of seconds' worth, draining as we go so the queue
            // never fills.
            std::vector<EnvelopePoint> collected;
            std::vector<EnvelopePoint> scratch (512);

            for (int block = 0; block < 100; ++block)
            {
                auto buffer = makeBuffer (2, blockSize, [&] (int ch, int i)
                {
                    return amplitude * static_cast<float> (std::sin (phase + increment * i))
                           * (ch == 0 ? 1.0f : 1.0f);
                });

                phase += increment * blockSize;
                engine.pushAudio (buffer);

                juce::Thread::sleep (2);

                const auto numRead = engine.getEnvelopeQueue().pop (scratch.data(), 512);
                collected.insert (collected.end(), scratch.begin(), scratch.begin() + numRead);
            }

            engine.release();

            expect (collected.size() > 100,
                    "expected many envelope points, got " + juce::String (collected.size()));

            // Skip the first few: the very first hop can straddle the start.
            auto peak = 0.0f;
            auto rmsSum = 0.0;
            int counted = 0;

            for (size_t i = 4; i < collected.size(); ++i)
            {
                peak = juce::jmax (peak, collected[i].maxValue, -collected[i].minValue);
                rmsSum += collected[i].rms;
                ++counted;
            }

            expect (counted > 0);

            // A 1 kHz sine at 48 kHz gives ~48 samples per cycle, so every
            // 256-sample hop contains whole cycles: peak should reach amplitude.
            expect (std::abs (peak - amplitude) < 0.01f,
                    "peak was " + juce::String (peak) + ", expected " + juce::String (amplitude));

            const auto meanRms = static_cast<float> (rmsSum / counted);
            const auto expectedRms = amplitude / std::sqrt (2.0f);

            expect (std::abs (meanRms - expectedRms) < 0.01f,
                    "mean RMS was " + juce::String (meanRms) + ", expected " + juce::String (expectedRms));
        }

        beginTest ("drops no blocks when the consumer keeps up");
        {
            AnalysisEngine engine;
            engine.prepare (48000.0, 512, 2);

            std::vector<EnvelopePoint> scratch (512);

            for (int block = 0; block < 50; ++block)
            {
                engine.pushAudio (makeBuffer (2, 512, [] (int, int) { return 0.1f; }));
                juce::Thread::sleep (2);
                engine.getEnvelopeQueue().pop (scratch.data(), 512);
            }

            expectEquals (engine.getNumDroppedBlocks(), 0);
            engine.release();
        }
    }
};

//==============================================================================
static SampleRingBufferTests sampleRingBufferTests;
static LockFreeQueueTests    lockFreeQueueTests;
static AnalysisEngineTests   analysisEngineTests;

int main (int, char**)
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    // Only our own tests. Linking juce_core also registers JUCE's suite, and
    // its filesystem tests fail in sandboxed containers for reasons that have
    // nothing to do with this project.
    runner.runTestsInCategory ("dsp");

    int numFailures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult (i);
        numFailures += result->failures;

        std::cout << (result->failures > 0 ? "FAIL  " : "ok    ")
                  << result->unitTestName << " / " << result->subcategoryName
                  << "  (" << result->passes << " passed, "
                  << result->failures << " failed)" << std::endl;

        for (const auto& message : result->messages)
            std::cout << "      " << message << std::endl;
    }

    std::cout << (numFailures == 0 ? "\nAll DSP tests passed.\n"
                                   : "\n" + juce::String (numFailures).toStdString() + " failure(s).\n");

    return numFailures == 0 ? 0 : 1;
}
