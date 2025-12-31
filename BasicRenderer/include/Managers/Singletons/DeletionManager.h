#pragma once
#include <vector>
#include "Managers/Singletons/SettingsManager.h"
#include "rhi_helpers.h"
#include "rhi_allocator.h"
#include "Resources/TrackedAllocation.h"

class DeletionManager {
public:
    static DeletionManager& GetInstance();

	void Initialize() {
		m_numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
		m_deletionQueue.resize(m_numFramesInFlight);
		m_allocationDeletionQueue.resize(m_numFramesInFlight);
		m_trackedAllocationDeletionQueue.resize(m_numFramesInFlight);
	}

	// Move-only
    void MarkForDelete(rhi::helpers::AnyObjectPtr ptr) {
		m_deletionQueue[0].push_back(std::move(ptr));
    }

	// Move-only
	void MarkForDelete(rhi::ma::AllocationPtr ptr) {
		m_allocationDeletionQueue[0].push_back(std::move(ptr));
	}

	void MarkForDelete(TrackedHandle&& alloc) {
		m_trackedAllocationDeletionQueue[0].push_back(std::move(alloc));
	}

	void ProcessDeletions() {
		m_deletionQueue.back().clear();
		for (int i = static_cast<int>(m_deletionQueue.size()) - 1; i >= 1; --i) {
			m_deletionQueue[i].swap(m_deletionQueue[i-1]);
		}
		m_allocationDeletionQueue.back().clear();
		for (int i = static_cast<int>(m_allocationDeletionQueue.size()) - 1; i >= 1; --i) {
			m_allocationDeletionQueue[i].swap(m_allocationDeletionQueue[i-1]);
		}
		m_trackedAllocationDeletionQueue.back().clear();
		for (int i = static_cast<int>(m_trackedAllocationDeletionQueue.size()) - 1; i >= 1; --i) {
			m_trackedAllocationDeletionQueue[i].swap(m_trackedAllocationDeletionQueue[i-1]);
		}
	}

    void Cleanup() {
		m_deletionQueue.clear();
		m_deletionQueue.resize(m_numFramesInFlight);
		m_allocationDeletionQueue.clear();
		m_allocationDeletionQueue.resize(m_numFramesInFlight);
		m_trackedAllocationDeletionQueue.clear();
		m_trackedAllocationDeletionQueue.resize(m_numFramesInFlight);
    }

private:
	uint8_t m_numFramesInFlight;
    DeletionManager() = default;

	std::vector<std::vector<rhi::helpers::AnyObjectPtr>> m_deletionQueue;
	std::vector<std::vector<rhi::ma::AllocationPtr>> m_allocationDeletionQueue;
	std::vector<std::vector<TrackedHandle>> m_trackedAllocationDeletionQueue;
};

inline DeletionManager& DeletionManager::GetInstance() {
    static DeletionManager instance;
    return instance;
}