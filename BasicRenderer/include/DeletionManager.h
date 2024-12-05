#pragma once
#include <memory>
#include <vector>
#include "SettingsManager.h"

class DeletionManager {
public:
    static DeletionManager& GetInstance();

	void Initialize() {
		uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
		m_deletionQueue.resize(numFramesInFlight);
	}

    template <typename T>
    void MarkForDelete(const std::shared_ptr<T>& ptr) {
		m_deletionQueue[0].push_back(ptr);
    }

	void ProcessDeletions() {
		m_deletionQueue.back().clear();
		for (int i = m_deletionQueue.size() - 2; i >= 0; --i) {
			m_deletionQueue[i + 1].swap(m_deletionQueue[i]);
		}
	}

    void Cleanup() {
		m_deletionQueue.clear();
		m_deletionQueue.push_back(std::vector<std::shared_ptr<void>>());
    }

private:
    DeletionManager() = default;

	std::vector<std::vector<std::shared_ptr<void>>> m_deletionQueue;
};

inline DeletionManager& DeletionManager::GetInstance() {
    static DeletionManager instance;
    return instance;
}