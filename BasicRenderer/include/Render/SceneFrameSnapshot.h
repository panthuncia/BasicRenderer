#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Scene/Components.h"

namespace br::render {

using StableSceneID = uint64_t;

struct SnapshotRenderable {
    StableSceneID stableID = 0;
    Components::Matrix matrix;
    Components::MeshInstances meshInstances;
    std::string name;
    bool skinned = false;
    bool skipShadowPass = false;
};

struct SnapshotCamera {
    StableSceneID stableID = 0;
    Components::Matrix matrix;
    Components::Camera camera;
    std::string name;
    bool primary = false;
};

struct SnapshotLight {
    StableSceneID stableID = 0;
    Components::Matrix matrix;
    Components::Light light;
    std::optional<Components::FrustumPlanes> frustumPlanes;
    std::string name;
    bool skipShadowPass = false;
};

struct SceneFrameSnapshot {
    uint64_t sceneID = 0;
    uint64_t snapshotSequence = 0;
    uint64_t sourceFrameNumber = 0;
    Components::DrawStats drawStats;
    Components::GlobalMeshLibrary meshLibrary;
    std::vector<SnapshotRenderable> renderables;
    std::vector<SnapshotCamera> cameras;
    std::vector<SnapshotLight> lights;
    bool hasPrimaryCamera = false;
    StableSceneID primaryCameraStableID = 0;
};

struct SceneOverlapStatus {
    bool enabled = false;
    bool taskInFlight = false;
    bool hasCommittedSnapshot = false;
    uint64_t committedSnapshotSequence = 0;
    uint64_t pendingSnapshotSequence = 0;
    uint64_t lastCompletedSnapshotSequence = 0;
    uint64_t lastCommittedSourceFrame = 0;
    double lastTaskDurationMs = 0.0;
};

} // namespace br::render