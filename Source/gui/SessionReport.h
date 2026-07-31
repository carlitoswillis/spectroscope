#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class LoudnessHistoryView;
class LoudnessMeter;

/**
    Everything a session leaves behind, written in one pass: the loudness
    chart as CSV, a snapshot of the whole console as it looked at the moment
    of writing, and a plain-text report of the headline figures.

    A namespace rather than a class — there is no state to hold between
    calls, just a folder to build.
*/
namespace SessionReport
{
    /** Creates "~/Desktop/Spectroscope Log <yyyy-mm-dd hhmmss>/" holding
        loudness.csv, console.png and report.txt, and returns that folder.
        Silent about failure — no dialogs, this is a background convenience,
        not a save operation the operator waits on.
    */
    juce::File write (juce::Component& console, LoudnessHistoryView& chart,
                      LoudnessMeter& meter, juce::StringRef liveryName);
}
