#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <flecs.h>

#include "Render/RenderPhase.h"

namespace br::render {

// Renderer-scoped mutation boundary for the ECS-backed ingestion adapter.
// Publications never retain this store; it exists only while converting one
// ordered source batch into artifact requests.
class SceneSourceStateStore {
public:
    using PhaseMap = std::unordered_map<RenderPhase, flecs::entity, RenderPhase::Hasher>;

    class WriteLease {
    public:
        WriteLease(std::unique_lock<std::mutex> lock, flecs::world& world,
            const PhaseMap& phases, std::uint64_t revision) noexcept;
        [[nodiscard]] flecs::world& World() const noexcept { return *m_world; }
        [[nodiscard]] const PhaseMap& Phases() const noexcept { return *m_phases; }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_revision; }
    private:
        std::unique_lock<std::mutex> m_lock;
        flecs::world* m_world = nullptr;
        const PhaseMap* m_phases = nullptr;
        std::uint64_t m_revision = 0;
    };

    void Configure(flecs::world& world, const PhaseMap& phases) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] WriteLease AcquireWrite(std::uint64_t sourceRevision = 0);

private:
    mutable std::mutex m_mutex;
    flecs::world* m_world = nullptr;
    const PhaseMap* m_phases = nullptr;
    std::uint64_t m_lastRevision = 0;
};

}
