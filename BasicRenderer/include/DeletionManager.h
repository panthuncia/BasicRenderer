#pragma once
#include <memory>
#include <vector>

class DeletionManager {
public:
    static DeletionManager& GetInstance();

    template <typename T>
    void MarkForDelete(const std::shared_ptr<T>& ptr) {
        m_deferredDeletion.push_back(std::static_pointer_cast<void>(ptr));
    }

	void ProcessDeletions() {
		m_stuffToDelete.clear();
        for (auto& ptr : m_deferredDeletion) {
			m_stuffToDelete.push_back(ptr);
        }
        m_deferredDeletion.clear();
	}

    void Cleanup() {
		m_deferredDeletion.clear();
		m_stuffToDelete.clear();
    }

private:
    DeletionManager() = default;
    // TODO: Currently, resource transitions for the initial states of resources of new objects are handled at the start of each frame.
    // This means that if an object is loaded mid-frame, and one of its resources is queued for deletion (buffer resizing, for example),
    // it will be added to the transition queue, then deleted, and then its transition will be processed, which is invalid.
    // As a hack solution, this does double-frame deferred deletion. There is probably a better solution. My initial resource transitions
    // should probably be reworked- I wrote them before I understood how resource states worked.
	std::vector<std::shared_ptr<void>> m_deferredDeletion; // double deferred deletion to avoid deleting objects while transitioning them
    std::vector<std::shared_ptr<void>> m_stuffToDelete;
};

inline DeletionManager& DeletionManager::GetInstance() {
    static DeletionManager instance;
    return instance;
}