#pragma once

#include <juce_core/juce_core.h>
#include <vector>

/**
    Single-producer / single-consumer queue of fixed-width spectrogram columns.

    A column is too big to pass through LockFreeQueue by value, so the samples
    live in one flat buffer and the FIFO only tracks column counts.
*/
class ColumnRing
{
public:
    ColumnRing() = default;

    /** Allocates. Not safe to call while either thread is running. */
    void prepare (int binsPerColumn, int numColumns)
    {
        bins = juce::jmax (1, binsPerColumn);
        const auto columns = juce::nextPowerOfTwo (juce::jmax (8, numColumns));

        storage.assign (static_cast<size_t> (bins) * static_cast<size_t> (columns), 0.0f);
        fifo.setTotalSize (columns);
        fifo.reset();
    }

    /** Producer. Returns false if the consumer has fallen behind. */
    bool push (const float* column) noexcept
    {
        if (fifo.getFreeSpace() < 1)
            return false;

        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        const auto slot = size1 > 0 ? start1 : start2;

        if (size1 + size2 < 1)
            return false;

        std::copy (column, column + bins,
                   storage.begin() + static_cast<std::ptrdiff_t> (slot) * bins);

        fifo.finishedWrite (1);
        return true;
    }

    /** Consumer. Writes up to maxColumns consecutive columns into destination,
        which must hold maxColumns * getBinsPerColumn() floats.
    */
    int pop (float* destination, int maxColumns) noexcept
    {
        const auto numToRead = juce::jmin (maxColumns, fifo.getNumReady());

        if (numToRead <= 0)
            return 0;

        int start1, size1, start2, size2;
        fifo.prepareToRead (numToRead, start1, size1, start2, size2);

        const auto copyRun = [this, destination] (int sourceColumn, int destColumn, int count)
        {
            if (count <= 0)
                return;

            const auto first = static_cast<std::ptrdiff_t> (sourceColumn) * bins;
            const auto total = static_cast<std::ptrdiff_t> (count) * bins;

            std::copy (storage.begin() + first,
                       storage.begin() + first + total,
                       destination + static_cast<std::ptrdiff_t> (destColumn) * bins);
        };

        copyRun (start1, 0, size1);
        copyRun (start2, size1, size2);

        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    void discardPending() noexcept
    {
        if (const auto ready = fifo.getNumReady(); ready > 0)
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (ready, start1, size1, start2, size2);
            fifo.finishedRead (size1 + size2);
        }
    }

    int getBinsPerColumn() const noexcept { return bins; }
    int getNumReady() const noexcept      { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo { 1 };
    std::vector<float> storage;
    int bins = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColumnRing)
};
