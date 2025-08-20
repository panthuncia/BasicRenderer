#pragma once

#include <wrl/client.h>
#include <d3d12.h>
#include <deque>
#include <vector>
#include <cstdint>

#include "Utilities/Utilities.h"

struct CommandListPair {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList10> list;
};

class CommandListPool {
public:
    CommandListPool(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

    // Acquire a command allocator / list pair ready for recording
    CommandListPair Request();

    // Recycle a pair after execution. If fenceValue is 0 the pair becomes
    // immediately available. Otherwise it will be returned to the available
    // pool once RecycleCompleted is called with a sufficiently large fence value.
    void Recycle(CommandListPair&& pair, uint64_t fenceValue);

    // Return any completed command lists to the available pool.
    void RecycleCompleted(uint64_t completedFenceValue);

private:
    ID3D12Device* m_device = nullptr;
    D3D12_COMMAND_LIST_TYPE m_type;

    std::vector<CommandListPair> m_available;
    std::deque<std::pair<uint64_t, CommandListPair>> m_inFlight;
	std::vector<CommandListPair> m_inFlightNoFence;
};