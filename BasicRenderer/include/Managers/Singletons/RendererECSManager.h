#pragma once

#include <flecs.h>
#include <memory>
#include <unordered_map>

#include "Render/RenderPhase.h"

class RendererECSManager {
public:
    static RendererECSManager& GetInstance();

    void Initialize();
    void Cleanup();
    bool IsAlive() const;
    flecs::world& GetWorld();
    flecs::entity GetRenderPhaseEntity(const RenderPhase& phase);
    void CreateRenderPhaseEntity(const RenderPhase& phase);
    const std::unordered_map<RenderPhase, flecs::entity, RenderPhase::Hasher>& GetRenderPhaseEntities() const;

private:
    std::unique_ptr<flecs::world> m_world;
    std::unordered_map<RenderPhase, flecs::entity, RenderPhase::Hasher> m_renderPhaseEntities;
    RendererECSManager() = default;
};

inline RendererECSManager& RendererECSManager::GetInstance() {
    static RendererECSManager instance;
    return instance;
}