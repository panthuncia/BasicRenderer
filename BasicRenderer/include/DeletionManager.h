#pragma once
#include <memory>
#include <vector>

class DeletionManager {
public:
    static DeletionManager& GetInstance();

    template <typename T>
    void MarkForDelete(const std::shared_ptr<T>& ptr) {
        m_stuffToDelete.push_back(std::static_pointer_cast<void>(ptr));
    }

	void ProcessDeletions() {
		m_stuffToDelete.clear();
	}

private:
    DeletionManager() = default;
    std::vector<std::shared_ptr<void>> m_stuffToDelete; // Some things need deferred deletion
};

inline DeletionManager& DeletionManager::GetInstance() {
    static DeletionManager instance;
    return instance;
}