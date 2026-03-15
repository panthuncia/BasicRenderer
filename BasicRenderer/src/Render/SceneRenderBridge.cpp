#include "Render/SceneRenderBridge.h"

#include <unordered_set>
#include <vector>

#include "Managers/LightManager.h"
#include "Managers/ManagerInterface.h"
#include "Managers/ObjectManager.h"
#include "Managers/ViewManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Materials/Material.h"
#include "Mesh/MeshInstance.h"
#include "Resources/Sampler.h"
#include "Scene/Components.h"
#include "Resources/components.h"
#include "Utilities/Utilities.h"

namespace {

struct BridgedSceneEntity {};
struct CameraResourceSignature {
    uint32_t depthResX = 0;
    uint32_t depthResY = 0;
    bool primary = false;
};
struct LightResourceSignature {
    Components::LightType type = Components::LightType::Directional;
    bool shadowCaster = false;
    uint16_t shadowResolution = 0;
    uint8_t directionalCascadeCount = 0;
    bool hasPrimaryCamera = false;
};
struct RenderableSignature {
    std::vector<uint64_t> meshInstanceKeys;
};

bool operator==(const CameraResourceSignature& lhs, const CameraResourceSignature& rhs) {
    return lhs.depthResX == rhs.depthResX
        && lhs.depthResY == rhs.depthResY
        && lhs.primary == rhs.primary;
}

bool operator==(const LightResourceSignature& lhs, const LightResourceSignature& rhs) {
    return lhs.type == rhs.type
        && lhs.shadowCaster == rhs.shadowCaster
        && lhs.shadowResolution == rhs.shadowResolution
        && lhs.directionalCascadeCount == rhs.directionalCascadeCount
        && lhs.hasPrimaryCamera == rhs.hasPrimaryCamera;
}

uint64_t BuildMeshInstanceKey(const std::shared_ptr<MeshInstance>& meshInstance) {
    const auto mesh = meshInstance->GetMesh();
    const auto instanceKey = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(meshInstance.get()));
    const auto meshKey = mesh ? mesh->GetGlobalID() : 0ull;
    const auto materialKey = mesh ? static_cast<uint64_t>(mesh->material->Technique().compileFlags) : 0ull;
    return instanceKey ^ (meshKey << 1) ^ (materialKey << 33);
}

RenderableSignature BuildRenderableSignature(const Components::MeshInstances* meshInstances) {
    RenderableSignature signature;
    if (!meshInstances) {
        return signature;
    }

    signature.meshInstanceKeys.reserve(meshInstances->meshInstances.size());
    for (const auto& meshInstance : meshInstances->meshInstances) {
        signature.meshInstanceKeys.push_back(BuildMeshInstanceKey(meshInstance));
    }
    return signature;
}

Components::PerPassMeshes BuildPerPassMeshes(const Components::MeshInstances* meshInstances) {
    Components::PerPassMeshes perPassMeshes;
    if (!meshInstances) {
        return perPassMeshes;
    }

    for (const auto& meshInstance : meshInstances->meshInstances) {
        const auto mesh = meshInstance->GetMesh();
        for (const auto& pass : mesh->material->Technique().passes) {
            perPassMeshes.meshesByPass[pass.hash].push_back(meshInstance);
        }
    }

    return perPassMeshes;
}

void DestroyRendererObject(flecs::entity entity, ObjectManager& objectManager) {
    if (const auto* drawInfo = entity.try_get<Components::ObjectDrawInfo>()) {
        objectManager.RemoveObject(drawInfo);
        entity.remove<Components::ObjectDrawInfo>();
    }
}

void DestroyRendererCamera(flecs::entity entity, ViewManager& viewManager) {
    if (const auto* renderView = entity.try_get<Components::RenderViewRef>()) {
        viewManager.DestroyView(renderView->viewID);
        entity.remove<Components::RenderViewRef>();
    }
    entity.remove<Components::DepthMap>();
}

void DestroyRendererLight(flecs::entity entity, LightManager& lightManager) {
    if (entity.has<Components::LightViewInfo>()) {
        lightManager.RemoveLight(entity);
        entity.remove<Components::LightViewInfo>();
    }
    entity.remove<Components::DepthMap>();
}

void CopyCommonComponents(flecs::entity dst, flecs::entity src) {
    dst.add<BridgedSceneEntity>();
    dst.add<Components::Active>();

    if (auto name = src.try_get<Components::Name>()) {
        dst.set<Components::Name>(*name);
    }

    if (auto matrix = src.try_get<Components::Matrix>()) {
        dst.set<Components::Matrix>(*matrix);
    }

    if (auto stableSceneID = src.try_get<Components::StableSceneID>()) {
        dst.set<Components::StableSceneID>(*stableSceneID);
    }
}

void CopyCommonComponents(flecs::entity dst, uint64_t stableSceneID, const std::string& name, const Components::Matrix& matrix) {
    dst.add<BridgedSceneEntity>();
    dst.add<Components::Active>();
    dst.set<Components::StableSceneID>({ stableSceneID });
    dst.set<Components::Matrix>(matrix);

    if (!name.empty()) {
        dst.set<Components::Name>(name);
    } else {
        dst.remove<Components::Name>();
    }
}

void SyncPassMembership(flecs::entity dst, const Components::MeshInstances* meshInstances) {
    dst.remove<Components::ParticipatesInPass>(flecs::Wildcard);

    if (!meshInstances) {
        return;
    }

    const auto& renderPhaseEntities = RendererECSManager::GetInstance().GetRenderPhaseEntities();
    std::unordered_set<uint64_t> passHashes;
    for (const auto& meshInstance : meshInstances->meshInstances) {
        for (const auto& pass : meshInstance->GetMesh()->material->Technique().passes) {
            passHashes.insert(pass.hash);
        }
    }

    for (const auto& [phase, phaseEntity] : renderPhaseEntities) {
        if (passHashes.contains(phase.hash)) {
            dst.add<Components::ParticipatesInPass>(phaseEntity);
        }
    }
}

void SyncRenderableDerivedState(flecs::entity dst, const Components::MeshInstances* meshInstances, ObjectManager& objectManager) {
    const auto newSignature = BuildRenderableSignature(meshInstances);
    const auto perPassMeshes = BuildPerPassMeshes(meshInstances);
    const auto* oldSignature = dst.try_get<RenderableSignature>();
    const bool signatureChanged = oldSignature == nullptr || oldSignature->meshInstanceKeys != newSignature.meshInstanceKeys;

    if (meshInstances) {
        dst.set<Components::MeshInstances>(*meshInstances);
        dst.set<Components::PerPassMeshes>(perPassMeshes);
    } else {
        dst.remove<Components::MeshInstances>();
        dst.remove<Components::PerPassMeshes>();
    }

    if (!dst.has<Components::RenderableObject>()) {
        dst.set<Components::RenderableObject>({});
    }

    if (signatureChanged) {
        DestroyRendererObject(dst, objectManager);
        auto renderable = dst.get<Components::RenderableObject>();
        auto drawInfo = objectManager.AddObject(renderable.perObjectCB, meshInstances);
        renderable.perObjectCB.normalMatrixBufferIndex = drawInfo.normalMatrixIndex;
        dst.set<Components::RenderableObject>(renderable);
        dst.set<Components::ObjectDrawInfo>(drawInfo);
        dst.set<RenderableSignature>(newSignature);
    }

    SyncPassMembership(dst, meshInstances);
}

Components::Camera BuildRendererCamera(const Components::Camera& sceneCamera, const Components::DepthMap& depthMap, uint32_t width, uint32_t height) {
    auto rendererCamera = sceneCamera;
    rendererCamera.info.numDepthMips = NumMips(width, height);
    rendererCamera.info.depthResX = width;
    rendererCamera.info.depthResY = height;
    const auto paddedLinearDepthX = depthMap.linearDepthMap->GetInternalWidth();
    const auto paddedLinearDepthY = depthMap.linearDepthMap->GetInternalHeight();
    rendererCamera.info.uvScaleToNextPowerOfTwo = {
        static_cast<float>(width) / static_cast<float>(paddedLinearDepthX),
        static_cast<float>(height) / static_cast<float>(paddedLinearDepthY)
    };
    return rendererCamera;
}

void SyncCameraDerivedState(
    flecs::entity dst,
    const Components::Camera& sceneCamera,
    bool isPrimary,
    ViewManager& viewManager,
    uint32_t renderWidth,
    uint32_t renderHeight) {
    const CameraResourceSignature newSignature{ renderWidth, renderHeight, isPrimary };
    const auto* oldSignature = dst.try_get<CameraResourceSignature>();
    const bool signatureChanged = oldSignature == nullptr || !(*oldSignature == newSignature);

    if (signatureChanged) {
        DestroyRendererCamera(dst, viewManager);

        auto depthMap = CreateDepthMapComponent(renderWidth, renderHeight, 1, false);
        auto rendererCamera = BuildRendererCamera(sceneCamera, depthMap, renderWidth, renderHeight);
        const auto viewFlags = isPrimary ? ViewFlags::PrimaryCamera() : ViewFlags::Generic();
        const auto viewID = viewManager.CreateView(rendererCamera.info, viewFlags);
        viewManager.AttachDepth(viewID, depthMap.depthMap, depthMap.linearDepthMap);

        dst.set<Components::Camera>(rendererCamera);
        dst.set<Components::RenderViewRef>({ viewID });
        dst.set<Components::DepthMap>(depthMap);
        dst.set<CameraResourceSignature>(newSignature);
    } else {
        const auto depthMap = dst.get<Components::DepthMap>();
        dst.set<Components::Camera>(BuildRendererCamera(sceneCamera, depthMap, renderWidth, renderHeight));
    }
}

LightResourceSignature BuildLightSignature(const Components::Light& light, uint16_t shadowResolution, uint8_t directionalCascadeCount, bool hasPrimaryCamera) {
    return LightResourceSignature{
        light.type,
        light.lightInfo.shadowCaster,
        shadowResolution,
        directionalCascadeCount,
        hasPrimaryCamera
    };
}

void ApplyLightRendererBindings(Components::Light& light, flecs::entity dst) {
    if (const auto* viewInfo = dst.try_get<Components::LightViewInfo>()) {
        light.lightInfo.shadowViewInfoIndex = viewInfo->viewInfoBufferIndex;
        if (const auto* depthMap = dst.try_get<Components::DepthMap>()) {
            if (depthMap->depthMap) {
                light.lightInfo.shadowMapIndex = depthMap->depthMap->GetSRVInfo(0).slot.index;
                light.lightInfo.shadowSamplerIndex = Sampler::GetDefaultShadowSampler()->GetDescriptorIndex();
            }
        }
    }
}

void SyncLightDerivedState(
    flecs::entity dst,
    const Components::Light& sceneLight,
    const Components::FrustumPlanes* sceneFrustumPlanes,
    LightManager& lightManager,
    uint16_t shadowResolution,
    uint8_t directionalCascadeCount,
    bool hasPrimaryCamera) {
    const auto newSignature = BuildLightSignature(sceneLight, shadowResolution, directionalCascadeCount, hasPrimaryCamera);
    const auto* oldSignature = dst.try_get<LightResourceSignature>();
    const bool signatureChanged = oldSignature == nullptr || !(*oldSignature == newSignature);

    Components::Light rendererLight = sceneLight;

    if (signatureChanged) {
        DestroyRendererLight(dst, lightManager);

        AddLightReturn addInfo = lightManager.AddLight(&rendererLight.lightInfo, dst.id());
        dst.set<Components::LightViewInfo>(addInfo.lightViewInfo);

        if (addInfo.shadowMap.has_value()) {
            dst.set<Components::DepthMap>(*addInfo.shadowMap);
        } else {
            dst.remove<Components::DepthMap>();
        }

        if (sceneFrustumPlanes) {
            dst.set<Components::FrustumPlanes>(*sceneFrustumPlanes);
        } else {
            dst.remove<Components::FrustumPlanes>();
        }
        if (addInfo.frustumPlanes.has_value()) {
            dst.set<Components::FrustumPlanes>(*addInfo.frustumPlanes);
        }

        dst.set<LightResourceSignature>(newSignature);
    } else if (sceneFrustumPlanes) {
        dst.set<Components::FrustumPlanes>(*sceneFrustumPlanes);
    }

    ApplyLightRendererBindings(rendererLight, dst);
    if (const auto* viewInfo = dst.try_get<Components::LightViewInfo>()) {
        lightManager.UpdateLightBufferView(viewInfo->lightBufferView.get(), rendererLight.lightInfo);
    }
    dst.set<Components::Light>(rendererLight);
}

flecs::entity GetOrCreateBridgedEntity(
    flecs::world& renderWorld,
    std::unordered_map<uint64_t, br::render::SceneRenderBridge::BridgedEntityState>& bridgedEntities,
    uint64_t stableSceneID,
    uint64_t currentFrame) {
    if (auto it = bridgedEntities.find(stableSceneID); it != bridgedEntities.end()) {
        it->second.lastSeenFrame = currentFrame;
        flecs::entity existing{ renderWorld, it->second.renderEntityId };
        if (existing.is_alive()) {
            return existing;
        }
    }

    auto dst = renderWorld.entity();
    bridgedEntities[stableSceneID] = { dst.id(), currentFrame, 0, DirectX::XMMatrixIdentity() };
    return dst;
}

uint64_t GetStableSceneID(flecs::entity entity) {
    const auto* stableSceneID = entity.try_get<Components::StableSceneID>();
    if (!stableSceneID) {
        throw std::runtime_error("SceneRenderBridge requires Components::StableSceneID on exported entities");
    }

    return stableSceneID->value;
}

void DestroyBridgedEntity(
    flecs::world& renderWorld,
    uint64_t renderEntityId,
    const ManagerInterface& managerInterface) {
    flecs::entity entity{ renderWorld, renderEntityId };
    if (!entity.is_alive()) {
        return;
    }

    if (auto* objectManager = managerInterface.GetObjectManager()) {
        DestroyRendererObject(entity, *objectManager);
    }
    if (auto* viewManager = managerInterface.GetViewManager()) {
        DestroyRendererCamera(entity, *viewManager);
    }
    if (auto* lightManager = managerInterface.GetLightManager()) {
        DestroyRendererLight(entity, *lightManager);
    }
    entity.destruct();
}

bool MatricesEqual(const DirectX::XMMATRIX& a, const DirectX::XMMATRIX& b) {
    for (int i = 0; i < 4; ++i) {
        if (DirectX::XMVector4NotEqual(a.r[i], b.r[i]))
            return false;
    }
    return true;
}

} // namespace

namespace br::render {

void SceneRenderBridge::EnsureExportQueries(flecs::world& sceneWorld) const {
    const auto sceneID = reinterpret_cast<uint64_t>(sceneWorld.c_ptr()); // use world pointer as cache key
    if (m_cachedExportSceneID == sceneID && m_exportRenderableQuery) {
        return;
    }

    m_exportRenderableQuery = sceneWorld.query_builder<Components::StableSceneID, Components::Matrix, Components::MeshInstances>()
        .with<Components::Active>()
        .build();
    m_exportCameraQuery = sceneWorld.query_builder<Components::StableSceneID, Components::Matrix, Components::Camera>()
        .with<Components::Active>()
        .build();
    m_exportLightQuery = sceneWorld.query_builder<Components::StableSceneID, Components::Matrix, Components::Light>()
        .with<Components::Active>()
        .build();
    m_cachedExportSceneID = sceneID;
}

void SceneRenderBridge::InvalidateExportQueries() {
    m_exportRenderableQuery = {};
    m_exportCameraQuery = {};
    m_exportLightQuery = {};
    m_cachedExportSceneID = 0;
}

SceneFrameSnapshot SceneRenderBridge::ExportSnapshot(Scene& scene, uint64_t snapshotSequence, uint64_t sourceFrameNumber) const {
    SceneFrameSnapshot snapshot;
    snapshot.sceneID = scene.GetSceneID();
    snapshot.snapshotSequence = snapshotSequence;
    snapshot.sourceFrameNumber = sourceFrameNumber;

    auto sceneWorld = scene.GetRoot().world();

    if (const auto* drawStats = sceneWorld.try_get<Components::DrawStats>()) {
        snapshot.drawStats = *drawStats;
    }
    if (const auto* meshLibrary = sceneWorld.try_get<Components::GlobalMeshLibrary>()) {
        snapshot.meshLibrary = *meshLibrary;
    }

    EnsureExportQueries(sceneWorld);

    snapshot.aliveRenderableIDs.reserve(m_lastRenderableCount);
    snapshot.changedRenderables.reserve(m_lastChangedRenderableCount);
    m_exportRenderableQuery.each([&](flecs::entity src, const Components::StableSceneID& stableSceneID, const Components::Matrix& matrix, const Components::MeshInstances& meshInstances) {
        snapshot.aliveRenderableIDs.insert(stableSceneID.value);

        const bool transformChanged = src.has<Components::TransformUpdatedThisFrame>();
        auto genIt = m_lastExportedMeshGeneration.find(stableSceneID.value);
        const bool meshChanged = genIt == m_lastExportedMeshGeneration.end() || genIt->second != meshInstances.generation;
        const bool isNew = genIt == m_lastExportedMeshGeneration.end();

        if (transformChanged || meshChanged || isNew) {
            SnapshotRenderable renderable;
            renderable.stableID = stableSceneID.value;
            renderable.matrix = matrix;
            renderable.meshInstances = meshInstances;
            if (const auto* name = src.try_get<Components::Name>()) {
                renderable.name = name->name;
            }
            renderable.skinned = src.has<Components::Skinned>();
            renderable.skipShadowPass = src.has<Components::SkipShadowPass>();
            snapshot.changedRenderables.push_back(std::move(renderable));
            m_lastExportedMeshGeneration[stableSceneID.value] = meshInstances.generation;
        }
    });
    m_lastRenderableCount = snapshot.aliveRenderableIDs.size();
    m_lastChangedRenderableCount = snapshot.changedRenderables.size();

    snapshot.aliveCameraIDs.reserve(m_lastCameraCount);
    m_exportCameraQuery.each([&](flecs::entity src, const Components::StableSceneID& stableSceneID, const Components::Matrix& matrix, const Components::Camera& camera) {
        snapshot.aliveCameraIDs.insert(stableSceneID.value);

        // Always export cameras — they're few and often change (jitter, movement)
        SnapshotCamera snapshotCamera;
        snapshotCamera.stableID = stableSceneID.value;
        snapshotCamera.matrix = matrix;
        snapshotCamera.camera = camera;
        snapshotCamera.primary = src.has<Components::PrimaryCamera>();
        if (const auto* name = src.try_get<Components::Name>()) {
            snapshotCamera.name = name->name;
        }
        if (snapshotCamera.primary) {
            snapshot.hasPrimaryCamera = true;
            snapshot.primaryCameraStableID = snapshotCamera.stableID;
        }
        snapshot.changedCameras.push_back(std::move(snapshotCamera));
    });
    m_lastCameraCount = snapshot.aliveCameraIDs.size();

    snapshot.aliveLightIDs.reserve(m_lastLightCount);
    m_exportLightQuery.each([&](flecs::entity src, const Components::StableSceneID& stableSceneID, const Components::Matrix& matrix, const Components::Light& light) {
        snapshot.aliveLightIDs.insert(stableSceneID.value);

        const bool transformChanged = src.has<Components::TransformUpdatedThisFrame>();
        const bool isNew = !m_bridgedEntities.contains(stableSceneID.value);

        if (transformChanged || isNew) {
            SnapshotLight snapshotLight;
            snapshotLight.stableID = stableSceneID.value;
            snapshotLight.matrix = matrix;
            snapshotLight.light = light;
            snapshotLight.skipShadowPass = src.has<Components::SkipShadowPass>();
            if (const auto* frustumPlanes = src.try_get<Components::FrustumPlanes>()) {
                snapshotLight.frustumPlanes = *frustumPlanes;
            }
            if (const auto* name = src.try_get<Components::Name>()) {
                snapshotLight.name = name->name;
            }
            snapshot.changedLights.push_back(std::move(snapshotLight));
        }
    });
    m_lastLightCount = snapshot.aliveLightIDs.size();

    return snapshot;
}

void SceneRenderBridge::Clear(const ManagerInterface& managerInterface) {
    if (!RendererECSManager::GetInstance().IsAlive()) {
        m_bridgedEntities.clear();
        m_primaryCameraEntityId = 0;
    m_lastExportedMeshGeneration.clear();
    InvalidateExportQueries();
    return;
    }

    auto& renderWorld = RendererECSManager::GetInstance().GetWorld();

    for (const auto& [stableSceneID, state] : m_bridgedEntities) {
        DestroyBridgedEntity(renderWorld, state.renderEntityId, managerInterface);
    }

    m_bridgedEntities.clear();
    m_primaryCameraEntityId = 0;
    m_lastExportedMeshGeneration.clear();
    InvalidateExportQueries();
}

void SceneRenderBridge::IngestSnapshot(const SceneFrameSnapshot& snapshot, const ManagerInterface& managerInterface) {
    auto& renderWorld = RendererECSManager::GetInstance().GetWorld();
    auto* objectManager = managerInterface.GetObjectManager();
    auto* viewManager = managerInterface.GetViewManager();
    auto* lightManager = managerInterface.GetLightManager();

    if (!objectManager || !viewManager || !lightManager) {
        throw std::runtime_error("SceneRenderBridge requires object, view, and light managers");
    }

    const auto renderResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    const auto shadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution")();
    const auto directionalCascadeCount = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();

    ++m_currentIngestionFrame;
    m_primaryCameraEntityId = 0;

    renderWorld.set<Components::DrawStats>(snapshot.drawStats);
    renderWorld.set<Components::GlobalMeshLibrary>(snapshot.meshLibrary);

    // Process only renderables that actually changed (transform, mesh, or new)
    for (const auto& renderable : snapshot.changedRenderables) {
        auto dst = GetOrCreateBridgedEntity(renderWorld, m_bridgedEntities, renderable.stableID, m_currentIngestionFrame);

        auto& entityState = m_bridgedEntities[renderable.stableID];
        const bool meshChanged = entityState.meshGeneration != renderable.meshInstances.generation;
        const bool isNew = !dst.has<BridgedSceneEntity>();

        // Entity is in the changed list, so always update common components
        CopyCommonComponents(dst, renderable.stableID, renderable.name, renderable.matrix);
        entityState.lastMatrix = renderable.matrix.matrix;
        dst.add<Components::RenderTransformUpdated>();

        if (isNew || meshChanged) {
            SyncRenderableDerivedState(dst, &renderable.meshInstances, *objectManager);
            entityState.meshGeneration = renderable.meshInstances.generation;
        }

        if (renderable.skinned) {
            dst.add<Components::Skinned>();
        } else {
            dst.remove<Components::Skinned>();
        }
        if (renderable.skipShadowPass) {
            dst.add<Components::SkipShadowPass>();
        } else {
            dst.remove<Components::SkipShadowPass>();
        }
    }

    // Cameras are always exported (few entities, frequently change)
    for (const auto& camera : snapshot.changedCameras) {
        auto dst = GetOrCreateBridgedEntity(renderWorld, m_bridgedEntities, camera.stableID, m_currentIngestionFrame);
        CopyCommonComponents(dst, camera.stableID, camera.name, camera.matrix);
        dst.add<Components::RenderTransformUpdated>();
        SyncCameraDerivedState(dst, camera.camera, camera.primary, *viewManager, renderResolution.x, renderResolution.y);
        if (camera.primary) {
            dst.add<Components::PrimaryCamera>();
            m_primaryCameraEntityId = dst.id();
            lightManager->SetCurrentCamera(dst);
        } else {
            dst.remove<Components::PrimaryCamera>();
        }
    }

    // Process only lights that actually changed
    for (const auto& light : snapshot.changedLights) {
        auto dst = GetOrCreateBridgedEntity(renderWorld, m_bridgedEntities, light.stableID, m_currentIngestionFrame);
        CopyCommonComponents(dst, light.stableID, light.name, light.matrix);
        dst.add<Components::RenderTransformUpdated>();
        const auto* frustumPlanes = light.frustumPlanes ? &light.frustumPlanes.value() : nullptr;
        SyncLightDerivedState(dst, light.light, frustumPlanes, *lightManager, shadowResolution, directionalCascadeCount, m_primaryCameraEntityId != 0);
        if (light.skipShadowPass) {
            dst.add<Components::SkipShadowPass>();
        } else {
            dst.remove<Components::SkipShadowPass>();
        }
    }

    // Remove stale entities using alive sets from the snapshot
    std::vector<uint64_t> staleStableSceneIDs;
    for (const auto& [stableSceneID, state] : m_bridgedEntities) {
        if (!snapshot.aliveRenderableIDs.contains(stableSceneID) &&
            !snapshot.aliveCameraIDs.contains(stableSceneID) &&
            !snapshot.aliveLightIDs.contains(stableSceneID)) {
            DestroyBridgedEntity(renderWorld, state.renderEntityId, managerInterface);
            staleStableSceneIDs.push_back(stableSceneID);
        }
    }

    for (const auto stableSceneID : staleStableSceneIDs) {
        m_bridgedEntities.erase(stableSceneID);
    }
}

void SceneRenderBridge::Sync(Scene& scene, const ManagerInterface& managerInterface) {
    IngestSnapshot(ExportSnapshot(scene, 0, 0), managerInterface);
}

bool SceneRenderBridge::HasPrimaryCamera() const {
    if (m_primaryCameraEntityId == 0) {
        return false;
    }

    auto& renderWorld = RendererECSManager::GetInstance().GetWorld();
    flecs::entity entity{ renderWorld, m_primaryCameraEntityId };
    return entity.is_alive();
}

flecs::entity SceneRenderBridge::GetPrimaryCameraEntity() const {
    auto& renderWorld = RendererECSManager::GetInstance().GetWorld();
    return flecs::entity{ renderWorld, m_primaryCameraEntityId };
}

} // namespace br::render