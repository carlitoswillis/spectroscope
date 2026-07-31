#pragma once

#include <juce_core/juce_core.h>
#include <vector>

/**
    Single-producer / single-consumer queue of trivially copyable items.

    Used for the analysis-thread-to-render-thread hop. Phase 2 carries envelope
    points; Phase 3 carries spectrogram columns through the same structure.
*/
template <typename ItemType>
class LockFreeQueue
{
public:
    static_assert (std::is_trivially_copyable_v<ItemType>,
                   "LockFreeQueue items are memcpy'd between threads");

    LockFreeQueue() = default;

    /** Allocates storage. Not safe to call while either thread is running. */
    void prepare (int capacity)
    {
        const auto size = juce::nextPowerOfTwo (juce::jmax (16, capacity));
        storage.assign (static_cast<size_t> (size), ItemType {});
        fifo.setTotalSize (size);
        fifo.reset();
    }

    /** Producer. Returns false if the consumer has fallen behind. */
    bool push (const ItemType& item) noexcept
    {
        if (fifo.getFreeSpace() < 1)
            return false;

        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
            storage[static_cast<size_t> (start1)] = item;
        else if (size2 > 0)
            storage[static_cast<size_t> (start2)] = item;
        else
            return false;

        fifo.finishedWrite (1);
        return true;
    }

    /** Consumer. Returns how many items were written to destination. */
    int pop (ItemType* destination, int maxItems) noexcept
    {
        const auto numToRead = juce::jmin (maxItems, fifo.getNumReady());

        if (numToRead <= 0)
            return 0;

        int start1, size1, start2, size2;
        fifo.prepareToRead (numToRead, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            destination[i] = storage[static_cast<size_t> (start1 + i)];

        for (int i = 0; i < size2; ++i)
            destination[size1 + i] = storage[static_cast<size_t> (start2 + i)];

        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    /** Consumer. Throws away everything pending — used when a view attaches and
        would otherwise render a backlog of stale frames.
    */
    void discardPending() noexcept
    {
        if (const auto ready = fifo.getNumReady(); ready > 0)
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (ready, start1, size1, start2, size2);
            fifo.finishedRead (size1 + size2);
        }
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo { 1 };
    std::vector<ItemType> storage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LockFreeQueue)
};
