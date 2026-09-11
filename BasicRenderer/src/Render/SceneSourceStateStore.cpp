#include "Render/SceneSourceStateStore.h"

#include <stdexcept>

namespace br::render {

SceneSourceStateStore::WriteLease::WriteLease(std::unique_lock<std::mutex> lock,
    flecs::world& world, const PhaseMap& phases, std::uint64_t revision) noexcept
    : m_lock(std::move(lock)), m_world(&world), m_phases(&phases), m_revision(revision) {}

void SceneSourceStateStore::Configure(flecs::world& world, const PhaseMap& phases) noexcept {
    std::lock_guard lock(m_mutex);
    m_world = &world;
    m_phases = &phases;
    m_lastRevision = 0;
}

void SceneSourceStateStore::Reset() noexcept {
    std::lock_guard lock(m_mutex);
    m_world = nullptr;
    m_phases = nullptr;
    m_lastRevision = 0;
}

bool SceneSourceStateStore::Available() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_world && m_phases;
}

SceneSourceStateStore::WriteLease SceneSourceStateStore::AcquireWrite(std::uint64_t sourceRevision) {
    std::unique_lock lock(m_mutex);
    if (!m_world || !m_phases) throw std::logic_error("SceneSourceStateStore is not configured");
    if (sourceRevision != 0 && sourceRevision < m_lastRevision) {
        throw std::logic_error("SceneSourceStateStore rejected an out-of-order ingestion batch");
    }
    if (sourceRevision != 0) m_lastRevision = sourceRevision;
    return WriteLease(std::move(lock), *m_world, *m_phases, m_lastRevision);
}

}
