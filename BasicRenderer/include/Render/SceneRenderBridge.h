#pragma once

#include <cstdint>
#include <unordered_map>

#include <flecs.h>

#include "Render/SceneFrameSnapshot.h"
#include "Scene/Scene.h"

class ManagerInterface;

namespace br::render {

class SceneRenderBridge {
public:
    struct BridgedEntityState {
        uint64_t renderEntityId = 0;
        uint64_t lastSeenFrame = 0;
        uint64_t meshGeneration = 0;
        DirectX::XMMATRIX lastMatrix = DirectX::XMMatrixIdentity();
    };

    SceneFrameSnapshot ExportSnapshot(Scene& scene, uint64_t snapshotSequence, uint64_t sourceFrameNumber) const;
    void IngestSnapshot(const SceneFrameSnapshot& snapshot, const ManagerInterface& managerInterface);
    void Sync(Scene& scene, const ManagerInterface& managerInterface);
    void Clear(const ManagerInterface& managerInterface);

    bool HasPrimaryCamera() const;
    flecs::entity GetPrimaryCameraEntity() const;

private:
    void EnsureExportQueries(flecs::world& sceneWorld) const;
    void InvalidateExportQueries();

    std::unordered_map<uint64_t, BridgedEntityState> m_bridgedEntities;
    uint64_t m_primaryCameraEntityId = 0;
    uint64_t m_currentIngestionFrame = 0;

    // Cached export queries (mutable because ExportSnapshot is const)
    mutable flecs::query<Components::StableSceneID, Components::Matrix, Components::MeshInstances> m_exportRenderableQuery;
    mutable flecs::query<Components::StableSceneID, Components::Matrix, Components::Camera> m_exportCameraQuery;
    mutable flecs::query<Components::StableSceneID, Components::Matrix, Components::Light> m_exportLightQuery;
    mutable uint64_t m_cachedExportSceneID = 0;

    // Hints for vector pre-reservation
    mutable size_t m_lastRenderableCount = 0;
    mutable size_t m_lastCameraCount = 0;
    mutable size_t m_lastLightCount = 0;
};

} // namespace br::render