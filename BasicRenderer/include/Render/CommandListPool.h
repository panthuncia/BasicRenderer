#pragma once

#include <deque>
#include <vector>
#include <cstdint>
#include <rhi.h>

#include "Utilities/Utilities.h"

struct CommandListPair {
    rhi::CommandAllocatorPtr allocator;
    rhi::CommandListPtr list;
};

class CommandListPool {
public:
    CommandListPool(rhi::Device& device, rhi::QueueKind type);

    // Acquire a command allocator / list pair ready for recording
    CommandListPair Request();

    // Recycle a pair after execution. If fenceValue is 0 the pair becomes
    // immediately available. Otherwise it will be returned to the available
    // pool once RecycleCompleted is called with a sufficiently large fence value.
    void Recycle(CommandListPair&& pair, uint64_t fenceValue);

    // Return any completed command lists to the available pool.
    void RecycleCompleted(uint64_t completedFenceValue);

private:
    rhi::Device m_device;
    rhi::QueueKind m_type;

    std::vector<CommandListPair> m_available;
    std::deque<std::pair<uint64_t, CommandListPair>> m_inFlight;
	std::vector<CommandListPair> m_inFlightNoFence;
};