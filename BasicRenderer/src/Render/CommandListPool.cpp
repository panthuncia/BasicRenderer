#include "Render/CommandListPool.h"

using Microsoft::WRL::ComPtr;

CommandListPool::CommandListPool(rhi::Device& device, rhi::QueueKind type)
    : m_device(device), m_type(type) {
}

CommandListPair CommandListPool::Request() {
    if (!m_available.empty()) {
        CommandListPair pair = std::move(m_available.back());
        m_available.pop_back();
        pair.list->Recycle(pair.allocator.Get());
        return pair;
    }

    CommandListPair pair;
	pair.allocator = m_device.CreateCommandAllocator(m_type);
	pair.list = m_device.CreateCommandList(m_type, pair.allocator.Get());
    pair.list->End();
    pair.allocator->Recycle();
    pair.list->Recycle(pair.allocator.Get());
    return pair;
}

void CommandListPool::Recycle(CommandListPair&& pair, uint64_t fenceValue) {
    if (fenceValue == 0) {
        m_inFlightNoFence.emplace_back(std::move(pair));
    }
    else {
        if (!m_inFlightNoFence.empty()) {
            for (auto& p : m_inFlightNoFence) {
                m_inFlight.emplace_back(fenceValue, std::move(p));
            }
            m_inFlightNoFence.clear();
		}
        m_inFlight.emplace_back(fenceValue, std::move(pair));
    }
}

void CommandListPool::RecycleCompleted(uint64_t completedFenceValue) {
    while (!m_inFlight.empty() && m_inFlight.front().first <= completedFenceValue) {
        auto pair = std::move(m_inFlight.front().second);
        m_inFlight.pop_front();
        pair.allocator->Recycle();
        m_available.push_back(std::move(pair));
    }
}
