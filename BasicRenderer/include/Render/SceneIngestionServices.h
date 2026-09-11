#pragma once

#include <unordered_map>
#include <DirectXMath.h>
#include <flecs.h>
#include "Render/RenderPhase.h"

class LightManager;
class ObjectManager;
class ViewManager;
class MeshManager;
class MaterialManager;
class TerrainManager;
class ShaderVariantRequestService;
namespace br::render { class SceneAssetRequestService; class StaticGeometryRequestService; class StaticMaterialRequestService; class StaticObjectRequestService; class StaticWorkloadRequestService; }
namespace br::render { class SceneSourceStateStore; }
namespace br::render { class SceneEntityMaterializationService; }
namespace br::render { class PoseInstanceRegistrationService; }
namespace br::render { class SceneRenderableResidencyService; }
namespace org::runtime { class IDescriptorService; class IUploadService; }
namespace br::render { class RendererStateRequestService; }

namespace br::render {
struct SceneIngestionConfiguration {
    DirectX::XMUINT2 renderResolution{};
    DirectX::XMUINT2 outputResolution{};
    std::uint16_t shadowResolution = 0;
    std::uint8_t directionalCascadeCount = 0;
    float maxShadowDistance = 0.0f;
    float directionalShadowDistanceLowerBound = 0.0f;
    float directionalShadowSceneExtent = 0.0f;
    bool primaryCameraUsesOutputHeight = false;
};

// Renderer-owned mutable source store. It is only touched by the ordered
// ingestion owner; frame work consumes immutable publications produced from it.
// Persistent identity/allocation stores used while materializing artifact
// requests. These are not frame inputs and must never escape in a publication.
// Services used to turn immutable artifact descriptions into publishable GPU
// versions. Reservations created here are owned by the resulting transaction.
struct ArtifactExecutionAccess {
    org::runtime::IUploadService* uploads = nullptr;
    org::runtime::IDescriptorService* descriptors = nullptr;
    RendererStateRequestService* stateRequests = nullptr;
};

// Capability set retained by the host's asynchronous static-scene producer.
// It intentionally excludes the renderer ECS world, views, lights, skeletons,
// and every frame-facing object.
struct StaticSceneIngestionAccess {
    StaticGeometryRequestService* meshes = nullptr;
    StaticObjectRequestService* objects = nullptr;
    StaticWorkloadRequestService* workloads = nullptr;
    StaticMaterialRequestService* materials = nullptr;
    SceneAssetRequestService* assetRequests = nullptr;
    ShaderVariantRequestService* shaderVariants = nullptr;
    ArtifactExecutionAccess execution;
};

// Temporary composition root used only at scene-ingestion boundaries. The
// nested capabilities make dependencies explicit while callers are migrated to
// dedicated request sinks; no pass, frame input, or producer payload may retain it.
struct SceneIngestionServices {
    SceneSourceStateStore* source = nullptr;
    SceneEntityMaterializationService* sceneEntities = nullptr;
    PoseInstanceRegistrationService* poseInstances = nullptr;
    SceneRenderableResidencyService* renderables = nullptr;
    ShaderVariantRequestService* shaderVariants = nullptr;
    ArtifactExecutionAccess execution;

    [[nodiscard]] StaticSceneIngestionAccess StaticScene() const noexcept {
        return {
            .meshes = geometryRequests,
            .objects = objectRequests,
            .workloads = workloadRequests,
            .materials = materialRequests,
            .assetRequests = sceneAssetRequests,
            .shaderVariants = shaderVariants,
            .execution = execution
        };
    }

    SceneAssetRequestService* sceneAssetRequests = nullptr;
    StaticGeometryRequestService* geometryRequests = nullptr;
    StaticMaterialRequestService* materialRequests = nullptr;
    StaticObjectRequestService* objectRequests = nullptr;
    StaticWorkloadRequestService* workloadRequests = nullptr;
};
}
