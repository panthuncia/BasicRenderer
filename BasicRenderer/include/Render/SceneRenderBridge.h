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
    SceneFrameSnapshot ExportSnapshot(Scene& scene, uint64_t snapshotSequence, uint64_t sourceFrameNumber) const;
    void IngestSnapshot(const SceneFrameSnapshot& snapshot, const ManagerInterface& managerInterface);
    void Sync(Scene& scene, const ManagerInterface& managerInterface);
    void Clear(const ManagerInterface& managerInterface);

    bool HasPrimaryCamera() const;
    flecs::entity GetPrimaryCameraEntity() const;

private:
    std::unordered_map<uint64_t, uint64_t> m_stableSceneIdToRenderEntityIds;
    uint64_t m_primaryCameraEntityId = 0;
};

} // namespace br::render