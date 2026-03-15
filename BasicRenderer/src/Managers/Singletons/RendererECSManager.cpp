#include "Managers/Singletons/RendererECSManager.h"

#include <stdexcept>

#include "Scene/Components.h"

void RendererECSManager::Initialize() {
    if (m_world) {
        return;
    }

    m_world = std::make_unique<flecs::world>();
    auto& world = *m_world;
    world.component<Components::GlobalMeshLibrary>().add(flecs::Exclusive);
    world.component<Components::DrawStats>("DrawStats").add(flecs::Exclusive);
    world.set<Components::DrawStats>({ 0, {} });
}

void RendererECSManager::Cleanup() {
    m_renderPhaseEntities.clear();
    m_world.reset();
}

bool RendererECSManager::IsAlive() const {
    return m_world != nullptr;
}

flecs::world& RendererECSManager::GetWorld() {
    if (!m_world) {
        throw std::runtime_error("RendererECSManager::GetWorld called before Initialize");
    }
    return *m_world;
}

flecs::entity RendererECSManager::GetRenderPhaseEntity(const RenderPhase& phase) {
    auto it = m_renderPhaseEntities.find(phase);
    if (it != m_renderPhaseEntities.end()) {
        return it->second;
    }

    auto entity = GetWorld().entity(phase.name.c_str());
    m_renderPhaseEntities[phase] = entity;
    return entity;
}

void RendererECSManager::CreateRenderPhaseEntity(const RenderPhase& phase) {
    if (m_renderPhaseEntities.contains(phase)) {
        return;
    }

    m_renderPhaseEntities[phase] = GetWorld().entity(phase.name.c_str());
}

const std::unordered_map<RenderPhase, flecs::entity, RenderPhase::Hasher>& RendererECSManager::GetRenderPhaseEntities() const {
    return m_renderPhaseEntities;
}