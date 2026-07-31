#include "SessionReport.h"
#include "LoudnessHistoryView.h"
#include "../dsp/LoudnessMeter.h"

namespace
{
    // LUFS at or below this reads as no data — see LoudnessMeter's own
    // contract. Max true peak shares the same sentinel family.
    constexpr float noDataThreshold = -100.0f;

    juce::String formatFigure (float value, juce::StringRef unit)
    {
        if (value <= noDataThreshold)
            return "---";

        return juce::String (value, 1) + " " + unit;
    }
}

juce::File SessionReport::write (juce::Component& console, LoudnessHistoryView& chart,
                                 LoudnessMeter& meter, juce::StringRef liveryName)
{
    const auto now = juce::Time::getCurrentTime();

    const auto folder = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                            .getChildFile ("Spectroscope Log " + now.formatted ("%Y-%m-%d %H%M%S"));

    folder.createDirectory();

    // loudness.csv — the chart hands back the same rows it paints, oldest
    // first, seconds counted back from now.
    {
        juce::String csv;
        chart.appendCsv (csv);
        folder.getChildFile ("loudness.csv").replaceWithText (csv);
    }

    // console.png — a snapshot of whatever the console looked like at the
    // moment of writing, chrome and all.
    {
        const auto snapshot = console.createComponentSnapshot (console.getLocalBounds());
        auto pngFile = folder.getChildFile ("console.png");

        if (auto stream = pngFile.createOutputStream())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (snapshot, *stream);
        }
    }

    // report.txt — the headline figures read straight off the meter, not the
    // chart's cached copies, so the numbers are current to the write, not to
    // the chart's last 60 Hz tick.
    {
        // Integrated gates loudness range exactly as the chart's own readout
        // does: LRA shares the gated block history, so it has nothing to say
        // until integrated does either.
        const auto integrated = meter.getIntegratedLufs();
        const auto hasIntegrated = integrated > noDataThreshold;

        juce::String report;
        report << "SPECTROSCOPE SESSION REPORT" << juce::newLine;
        report << liveryName << juce::newLine;
        report << now.toISO8601 (true) << juce::newLine;
        report << "MOMENTARY " << formatFigure (meter.getMomentaryLufs(), "LUFS") << juce::newLine;
        report << "SHORT-TERM " << formatFigure (meter.getShortTermLufs(), "LUFS") << juce::newLine;
        report << "INTEGRATED " << formatFigure (integrated, "LUFS") << juce::newLine;
        report << "LRA " << (hasIntegrated ? juce::String (meter.getLoudnessRange(), 1) + " LU" : juce::String ("---")) << juce::newLine;
        report << "MAX TRUE PEAK " << formatFigure (meter.getMaxTruePeakDb(), "DBTP") << juce::newLine;

        folder.getChildFile ("report.txt").replaceWithText (report);
    }

    return folder;
}
