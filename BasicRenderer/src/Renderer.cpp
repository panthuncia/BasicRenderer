//
// Created by matth on 6/25/2024.
//

#include "Renderer.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <atlbase.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <typeindex>
#include <utility>

#include <rhi_interop_dx12.h>
#include <tracy/Tracy.hpp>
#include <spdlog/spdlog.h>
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Render/RenderContext.h"
#include "OpenRenderGraph/OpenRenderGraph.h"
#include "Render/PassBuilders.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "RenderPasses/Base/RenderPass.h"
#include "RenderPasses/ForwardRenderPass.h"
#include "Managers/Singletons/SettingsManager.h"
#include "RenderPasses/DebugRenderPass.h"
#include "RenderPasses/SkyboxRenderPass.h"
#include "RenderPasses/EnvironmentFilterPass.h"
#include "RenderPasses/ClearUAVsPass.h"
#include "RenderPasses/ObjectCullingPass.h"
#include "RenderPasses/MeshletCullingPass.h"
#include "RenderPasses/DebugSpheresPass.h"
#include "RenderPasses/SkinningPass.h"
#include "RenderPasses/Base/ComputePass.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "RenderPasses/PostProcessing/Tonemapping.h"
#include "RenderPasses/PostProcessing/Upscaling.h"
#include "RenderPasses/PostProcessing/luminanceHistogram.h"
#include "RenderPasses/PostProcessing/luminanceHistogramAverage.h"
#include "RenderPasses/ClearVisibilityBufferPass.h"
#include "RenderPasses/PostProcessing/DebugResolvePass.h"
#include "RenderPasses/MenuRenderPass.h"
#include "Resources/TextureDescription.h"
#include "Menu/Menu.h"
#include "Managers/Singletons/DeletionManager.h"
#include "NsightAftermathGpuCrashTracker.h"
#include "Aftermath/GFSDK_Aftermath.h"
#include "NsightAftermathHelpers.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Utilities/MathUtils.h"
#include "Scene/MovementState.h"
#include "ThirdParty/XeGTAO.h"
#include "Managers/EnvironmentManager.h"
#include "Render/TonemapTypes.h"
#include "../generated/BuiltinResources.h"
#include "Resources/ResourceIdentifier.h"
#include "Render/RenderGraphBuildHelper.h"
#include "Managers/Singletons/UpscalingManager.h"
#include "Managers/Singletons/FFXManager.h"
#include "Render/Runtime/OpenRenderGraphSettings.h"
#include "Render/GraphExtensions/IOExtension.h"
#include "Render/GraphExtensions/CLodExtension.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "RenderPasses/DebugGridPass.h"
#include "Render/GraphExtensions/ReadbackCaptureExtension.h"
#include "Resources/Resource.h"
#include "Resources/DynamicResource.h"
#include "Resources/ExternalTextureResource.h"
#include "Render/MemoryIntrospectionBackend.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Render/Runtime/UploadPolicyServiceAccess.h"
#include "Render/Runtime/DescriptorServiceAccess.h"
#include "Render/TbbTaskService.h"

void D3D12DebugCallback(
    D3D12_MESSAGE_CATEGORY Category,
    D3D12_MESSAGE_SEVERITY Severity,
    D3D12_MESSAGE_ID ID,
    LPCSTR pDescription,
    void* pContext) {
    std::string message(pDescription);

    // Redirect messages to spdlog based on severity
    switch (Severity) {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        spdlog::critical("D3D12 CORRUPTION: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_ERROR:
        spdlog::error("D3D12 ERROR: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        spdlog::warn("D3D12 WARNING: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
        spdlog::info("D3D12 INFO: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        spdlog::debug("D3D12 MESSAGE: {}", message);
        break;
    }
}

namespace {

flecs::entity FindSceneEntityByStableSceneID(flecs::entity node, uint64_t stableSceneID) {
    if (!node.is_alive()) {
        return {};
    }

    if (const auto* currentStableSceneID = node.try_get<Components::StableSceneID>()) {
        if (currentStableSceneID->value == stableSceneID) {
            return node;
        }
    }

    flecs::entity found;
    node.children([&](flecs::entity child) {
        if (found.is_alive()) {
            return;
        }

        auto candidate = FindSceneEntityByStableSceneID(child, stableSceneID);
        if (candidate.is_alive()) {
            found = candidate;
        }
    });
    return found;
}

} // namespace

void Renderer::Initialize(HWND hwnd, UINT x_res, UINT y_res) {
    auto& settingsManager = SettingsManager::GetInstance();
    settingsManager.registerSetting<uint8_t>("numFramesInFlight", m_numFramesInFlight);
    getNumFramesInFlight = settingsManager.getSettingGetter<uint8_t>("numFramesInFlight");
    settingsManager.registerSetting<DirectX::XMUINT2>("renderResolution", { x_res, y_res });
    settingsManager.registerSetting<DirectX::XMUINT2>("outputResolution", { x_res, y_res });
    settingsManager.registerSetting<bool>("enableVisibilityRendering", m_visibilityRendering);
    LoadPipeline(hwnd, x_res, y_res);
    UpscalingManager::GetInstance().InitSL();
    UpscalingManager::GetInstance().InitFFX(); // Needs device
    FFXManager::GetInstance().InitFFX();
    SetSettings();
    RendererECSManager::GetInstance().Initialize();
    TrackedEntityToken::SetHooks({
        .isRuntimeAlive = []() {
            return RendererECSManager::GetInstance().IsAlive();
        },
        .destroyEntity = [](flecs::world& world, flecs::entity_t id) {
            flecs::entity entity{ world, id };
            if (entity.is_alive()) {
                entity.destruct();
            }
        }
        });

    Resource::SetEntityHooks({
        .createEntity = []() -> Resource::ECSEntityHandle {
            auto& world = RendererECSManager::GetInstance().GetWorld();
            auto entity = world.entity();
            return { .world = &world, .id = entity.id() };
        },
        .destroyEntity = [](flecs::world& world, flecs::entity_t id) {
			flecs::entity entity{ world, id };
			if (entity.is_alive()) {
				entity.destruct();
            }
        },
        .isRuntimeAlive = []() {
            return RendererECSManager::GetInstance().IsAlive();
        }
        });

    if (!currentRenderGraph) {
		currentRenderGraph = std::make_unique<RenderGraph>(DeviceManager::GetInstance().GetDevice());
    }

    if (auto* uploadService = currentRenderGraph->GetUploadService()) {
            uploadService->Initialize();
        rg::runtime::SetActiveUploadService(uploadService);
    }
    if (!m_uploadPolicyService) {
        m_uploadPolicyService = rg::runtime::CreateDefaultUploadPolicyService();
    }
    if (m_uploadPolicyService) {
        m_uploadPolicyService->Initialize();
        rg::runtime::SetActiveUploadPolicyService(m_uploadPolicyService.get());
    }
    if (auto* descriptorService = currentRenderGraph->GetDescriptorService()) {
        descriptorService->Initialize();
        rg::runtime::SetActiveDescriptorService(descriptorService);
    }
    ResourceManager::GetInstance().Initialize();
    TaskSchedulerManager::GetInstance().Initialize(16);
    currentRenderGraph->SetTaskService(std::make_shared<br::TbbTaskService>());
    PSOManager::GetInstance().initialize();
    DeletionManager::GetInstance().Initialize();
	CommandSignatureManager::GetInstance().Initialize();
    Menu::GetInstance().Initialize(hwnd, rhi::dx12::get_swapchain(m_swapChain.Get())); // TODO: VK imgui
    if (auto* readbackService = currentRenderGraph->GetReadbackService()) {
        readbackService->Initialize(m_readbackFence.Get());
    }
    m_pReadbackManager = std::make_unique<br::ReadbackManager>();
    
    m_pReadbackManager->Initialize(m_readbackFence.Get());
    if (auto* statisticsService = currentRenderGraph->GetStatisticsService()) {
        statisticsService->Initialize();
    }

    UpscalingManager::GetInstance().Setup();

    CreateTextures();
    CreateGlobalResources();

    // Initialize GPU resource managers
    m_pLightManager = LightManager::CreateUnique();
    m_pMeshManager = MeshManager::CreateUnique();
	m_pObjectManager = ObjectManager::CreateUnique();
	m_pIndirectCommandBufferManager = IndirectCommandBufferManager::CreateUnique();
	m_pViewManager = ViewManager::CreateUnique();
	m_pEnvironmentManager = EnvironmentManager::CreateUnique();
    CreateDefaultEnvironmentResources();
    m_pEnvironmentManager->SetRequestReadbackFn([this](std::shared_ptr<PixelBuffer> texture, std::wstring outputFile, std::function<void()> callback, bool cubemap) {
        if (!m_pReadbackManager) {
            return;
        }

        m_pReadbackManager->RequestReadback(std::move(texture), std::move(outputFile), std::move(callback), cubemap);
    });
	m_pMaterialManager = MaterialManager::CreateUnique();
	//ResourceManager::GetInstance().SetEnvironmentBufferDescriptorIndex(m_pEnvironmentManager->GetEnvironmentBufferSRVDescriptorIndex());
	m_pLightManager->SetViewManager(m_pViewManager.get()); // Light manager needs access to view manager for shadow cameras
	m_pViewManager->SetIndirectCommandBufferManager(m_pIndirectCommandBufferManager.get()); // View manager needs to make indirect command buffers
    m_pMeshManager->SetViewManager(m_pViewManager.get());
	m_pSkeletonManager = SkeletonManager::CreateUnique();
    m_pTextureFactory = TextureFactory::CreateUnique();

	m_managerInterface.SetManagers(
        m_pMeshManager.get(), 
        m_pObjectManager.get(), 
        m_pIndirectCommandBufferManager.get(), 
        m_pViewManager.get(), 
        m_pLightManager.get(), 
        m_pEnvironmentManager.get(), 
        m_pMaterialManager.get(),
        m_pSkeletonManager.get(),
        m_pTextureFactory.get());

    m_warnedNullScene = false;
    m_warnedMissingPrimaryCamera = false;
    m_warnedUsingFallbackEnvironment = false;

	m_isInitialized = true;
}

void Renderer::RunGameUpdateStage(float elapsedSeconds) {
    ZoneScopedN("Renderer::Update::GameUpdate");
    currentScene->Update(elapsedSeconds);
}

void Renderer::RunAnimationUpdateStage(float elapsedSeconds) {
    ZoneScopedN("Renderer::Update::AnimationUpdate");
    m_pSkeletonManager->TickAnimations(elapsedSeconds);
    m_pSkeletonManager->UpdateAllDirtyInstances();
}

void Renderer::RunTransformPropagationStage() {
    ZoneScopedN("Renderer::Update::TransformPropagation");
    currentScene->PropagateTransforms();
}

void Renderer::RunSceneBridgeSyncStage() {
    ZoneScopedN("Renderer::Update::SceneBridgeSync");
    m_sceneRenderBridge.Sync(*currentScene, m_managerInterface);
}

void Renderer::QueueSceneNodePositionEdit(uint64_t stableSceneID, DirectX::XMFLOAT3 position) {
    std::scoped_lock lock(m_pendingSceneExplorerEditsMutex);
    auto& edit = m_pendingSceneExplorerEdits[stableSceneID];
    edit.hasPosition = true;
    edit.position = position;
}

void Renderer::QueueSceneNodeUniformScaleEdit(uint64_t stableSceneID, float uniformScale) {
    std::scoped_lock lock(m_pendingSceneExplorerEditsMutex);
    auto& edit = m_pendingSceneExplorerEdits[stableSceneID];
    edit.hasUniformScale = true;
    edit.uniformScale = uniformScale;
}

void Renderer::FlushPendingSceneExplorerEdits() {
    if (!currentScene || m_sceneTaskInFlight.load()) {
        return;
    }

    std::unordered_map<uint64_t, PendingSceneExplorerEdit> pendingEdits;
    {
        std::scoped_lock lock(m_pendingSceneExplorerEditsMutex);
        if (m_pendingSceneExplorerEdits.empty()) {
            return;
        }
        pendingEdits.swap(m_pendingSceneExplorerEdits);
    }

    bool anyApplied = false;
    auto root = currentScene->GetRoot();
    for (const auto& [stableSceneID, edit] : pendingEdits) {
        auto entity = FindSceneEntityByStableSceneID(root, stableSceneID);
        if (!entity.is_alive()) {
            continue;
        }

        if (edit.hasPosition && entity.has<Components::Position>()) {
            entity.set<Components::Position>(edit.position);
            anyApplied = true;
        }

        if (edit.hasUniformScale && entity.has<Components::Scale>()) {
            entity.set<Components::Scale>({ edit.uniformScale, edit.uniformScale, edit.uniformScale });
            anyApplied = true;
        }
    }

    if (anyApplied) {
        currentScene->PropagateTransforms();

        {
            std::scoped_lock lock(m_sceneSnapshotMutex);
            m_completedSceneSnapshot.reset();
        }
        m_sceneTaskCompleted.store(false);
        BootstrapCommittedSceneSnapshot();
    }
}

void Renderer::BootstrapCommittedSceneSnapshot() {
    if (!currentScene) {
        std::scoped_lock lock(m_sceneSnapshotMutex);
        m_committedSceneSnapshot.reset();
        return;
    }

    auto snapshot = std::make_shared<br::render::SceneFrameSnapshot>(
        m_sceneRenderBridge.ExportSnapshot(*currentScene, m_nextSceneSnapshotSequence++, m_totalFramesRendered));
    m_sceneRenderBridge.IngestSnapshot(*snapshot, m_managerInterface);

    std::scoped_lock lock(m_sceneSnapshotMutex);
    m_committedSceneSnapshot = std::move(snapshot);
    m_lastCommittedSceneSnapshotSequence = m_committedSceneSnapshot->snapshotSequence;
    m_lastCommittedSceneSourceFrame = m_committedSceneSnapshot->sourceFrameNumber;
}

void Renderer::CommitCompletedSceneSnapshot() {
    if (!m_sceneTaskCompleted.exchange(false)) {
        return;
    }

    std::shared_ptr<br::render::SceneFrameSnapshot> completedSnapshot;
    {
        std::scoped_lock lock(m_sceneSnapshotMutex);
        completedSnapshot = std::exchange(m_completedSceneSnapshot, nullptr);
    }

    if (!completedSnapshot) {
        return;
    }

    if (!currentScene || completedSnapshot->sceneID != currentScene->GetSceneID()) {
        return;
    }

    m_sceneRenderBridge.IngestSnapshot(*completedSnapshot, m_managerInterface);
    {
        std::scoped_lock lock(m_sceneSnapshotMutex);
        m_committedSceneSnapshot = std::move(completedSnapshot);
        m_lastCommittedSceneSnapshotSequence = m_committedSceneSnapshot->snapshotSequence;
        m_lastCommittedSceneSourceFrame = m_committedSceneSnapshot->sourceFrameNumber;
    }
}

void Renderer::ScheduleSceneUpdateTask(float elapsedSeconds) {
    if (!m_sceneRenderOverlapEnabled || !currentScene || m_sceneTaskInFlight.exchange(true)) {
        return;
    }

    auto scene = currentScene;
    const auto movementSnapshot = movementState;
    const float verticalAngleSnapshot = verticalAngle;
    const float horizontalAngleSnapshot = horizontalAngle;
    const uint64_t snapshotSequence = m_nextSceneSnapshotSequence++;
    const uint64_t sourceFrameNumber = m_totalFramesRendered + 1;

    verticalAngle = 0.0f;
    horizontalAngle = 0.0f;

    TaskSchedulerManager::GetInstance().RunBackgroundTask("SceneUpdateOverlap", [this, scene, elapsedSeconds, movementSnapshot, verticalAngleSnapshot, horizontalAngleSnapshot, snapshotSequence, sourceFrameNumber]() mutable {
        const auto taskStart = std::chrono::steady_clock::now();

        if (!scene) {
            m_sceneTaskInFlight.store(false);
            return;
        }

        if (scene->HasUsablePrimaryCamera()) {
            Components::Position& cameraPosition = scene->GetPrimaryCameraPosition();
            Components::Rotation& cameraRotation = scene->GetPrimaryCameraRotation();
            ApplyMovement(cameraPosition, cameraRotation, movementSnapshot, elapsedSeconds);
            RotatePitchYaw(cameraRotation, verticalAngleSnapshot, horizontalAngleSnapshot);
            scene->GetPrimaryCamera().modified<Components::Position>();
            scene->GetPrimaryCamera().modified<Components::Rotation>();
        }

        scene->Update(elapsedSeconds);
        scene->PropagateTransforms();

        auto snapshot = std::make_shared<br::render::SceneFrameSnapshot>(
            m_sceneRenderBridge.ExportSnapshot(*scene, snapshotSequence, sourceFrameNumber));

        const auto taskEnd = std::chrono::steady_clock::now();
        const auto durationMs = std::chrono::duration<double, std::milli>(taskEnd - taskStart).count();

        {
            std::scoped_lock lock(m_sceneSnapshotMutex);
            m_completedSceneSnapshot = std::move(snapshot);
            m_lastCompletedSceneSnapshotSequence = snapshotSequence;
            m_lastSceneTaskDurationMs = durationMs;
        }

        m_sceneTaskCompleted.store(true);
        m_sceneTaskInFlight.store(false);
    });
}

bool Renderer::HasCommittedSceneSnapshot() const {
    std::scoped_lock lock(m_sceneSnapshotMutex);
    return static_cast<bool>(m_committedSceneSnapshot);
}

bool Renderer::NeedsSceneSnapshotBootstrap() const {
    if (!m_sceneRenderOverlapEnabled || !currentScene || m_sceneTaskInFlight.load()) {
        return false;
    }

    if (!currentScene->HasUsablePrimaryCamera()) {
        return false;
    }

    return !HasCommittedSceneSnapshot() || !m_sceneRenderBridge.HasPrimaryCamera();
}

br::render::SceneOverlapStatus Renderer::GetSceneOverlapStatus() const {
    br::render::SceneOverlapStatus status;
    status.enabled = m_sceneRenderOverlapEnabled;
    status.taskInFlight = m_sceneTaskInFlight.load();

    std::scoped_lock lock(m_sceneSnapshotMutex);
    status.hasCommittedSnapshot = static_cast<bool>(m_committedSceneSnapshot);
    status.committedSnapshotSequence = m_lastCommittedSceneSnapshotSequence;
    status.lastCompletedSnapshotSequence = m_lastCompletedSceneSnapshotSequence;
    status.lastCommittedSourceFrame = m_lastCommittedSceneSourceFrame;
    status.lastTaskDurationMs = m_lastSceneTaskDurationMs;
    if (m_completedSceneSnapshot) {
        status.pendingSnapshotSequence = m_completedSceneSnapshot->snapshotSequence;
    }

    return status;
}

void Renderer::RunRenderResourceSyncStage() {
    ZoneScopedN("Renderer::Update::RenderResourceSync");

    auto& world = RendererECSManager::GetInstance().GetWorld();

    if (!m_renderSyncQueriesBuilt) {
        m_renderSyncObjectQuery = world.query_builder<Components::Matrix, Components::RenderableObject, Components::ObjectDrawInfo>()
            .with<Components::Active>()
            .build();
        m_renderSyncCameraQuery = world.query_builder<Components::Matrix, Components::Camera, Components::RenderViewRef>()
            .with<Components::Active>()
            .build();
        m_renderSyncLightQuery = world.query_builder<Components::Matrix, Components::Light>()
            .with<Components::Active>()
            .build();
        m_renderTransformUpdatedCleanupQuery = world.query_builder<>()
            .with<Components::RenderTransformUpdated>()
            .build();
        m_renderSyncQueriesBuilt = true;
    }

    // Collect object entity data for parallel processing
    struct ObjectSyncItem {
        Components::Matrix* worldMatrix;
        Components::RenderableObject* object;
        Components::ObjectDrawInfo* drawInfo;
    };
    std::vector<ObjectSyncItem> objectItems;
    m_renderSyncObjectQuery.run([&](flecs::iter& it) {
        while (it.next()) {
            auto matrices = it.field<Components::Matrix>(0);
            auto objects = it.field<Components::RenderableObject>(1);
            auto drawInfos = it.field<Components::ObjectDrawInfo>(2);
            for (auto i : it) {
                objectItems.push_back({ &matrices[i], &objects[i], &drawInfos[i] });
            }
        }
    });

    auto* objectManager = m_managerInterface.GetObjectManager();

    // Pre-size scratch buffers single-threaded so the parallel loop can
    // memcpy into non-overlapping regions without any synchronization.
    auto perObjectHandle = objectManager->BeginPerObjectBulkWrite();
    auto normalMatrixHandle = objectManager->BeginNormalMatrixBulkWrite();

    TaskSchedulerManager::GetInstance().ParallelFor("ObjectSync", objectItems.size(),
        [&objectItems, &perObjectHandle, &normalMatrixHandle](size_t idx) {
            auto& [worldMatrix, object, drawInfo] = objectItems[idx];
            object->perObjectCB.prevModelMatrix = object->perObjectCB.modelMatrix;
            object->perObjectCB.modelMatrix = worldMatrix->matrix;

            const XMVECTOR det = XMMatrixDeterminant(worldMatrix->matrix);
            object->perObjectCB.objectFlags = (XMVectorGetX(det) < 0.0f) ? OBJECT_FLAG_REVERSE_WINDING : 0u;

            // Write per-object data directly into the scratch buffer (lock-free).
            {
                const size_t offset = drawInfo->perObjectCBView->GetOffset();
                const size_t sz = sizeof(PerObjectCB);
                std::memcpy(perObjectHandle.data + offset, &object->perObjectCB, sz);
            }

            const auto& modelMatrix = object->perObjectCB.modelMatrix;
            const XMMATRIX upperLeft3x3 = XMMatrixSet(
                XMVectorGetX(modelMatrix.r[0]), XMVectorGetY(modelMatrix.r[0]), XMVectorGetZ(modelMatrix.r[0]), 0.0f,
                XMVectorGetX(modelMatrix.r[1]), XMVectorGetY(modelMatrix.r[1]), XMVectorGetZ(modelMatrix.r[1]), 0.0f,
                XMVectorGetX(modelMatrix.r[2]), XMVectorGetY(modelMatrix.r[2]), XMVectorGetZ(modelMatrix.r[2]), 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f);
            XMMATRIX normalMat = XMMatrixInverse(nullptr, upperLeft3x3);

            // Write normal matrix directly into the scratch buffer (lock-free).
            {
                const size_t offset = drawInfo->normalMatrixView->GetOffset();
                const size_t sz = sizeof(DirectX::XMFLOAT4X4);
                DirectX::XMFLOAT4X4 stored;
                XMStoreFloat4x4(&stored, normalMat);
                std::memcpy(normalMatrixHandle.data + offset, &stored, sz);
            }
        });

    // Register dirty ranges single-threaded.
    // Mark the entire used portion dirty — the coalescer handles this efficiently.
    if (!objectItems.empty()) {
        objectManager->EndPerObjectBulkWrite(0, perObjectHandle.capacity);
        objectManager->EndNormalMatrixBulkWrite(0, normalMatrixHandle.capacity);
    }

    m_renderSyncCameraQuery.each([&](flecs::entity entity, Components::Matrix& worldMatrix, Components::Camera& camera, Components::RenderViewRef& renderView) {
        const XMMATRIX cameraModel = RemoveScalingFromMatrix(worldMatrix.matrix);
        const XMMATRIX view = XMMatrixInverse(nullptr, cameraModel);
        DirectX::XMMATRIX projection = camera.info.unjitteredProjection;
        camera.info.prevJitteredProjection = camera.info.jitteredProjection;
        camera.info.prevUnjitteredProjection = camera.info.unjitteredProjection;
        if (m_jitter && entity.has<Components::PrimaryCamera>()) {
            const auto jitterPixelSpace = UpscalingManager::GetInstance().GetJitter(m_totalFramesRendered);
            camera.jitterPixelSpace = jitterPixelSpace;
            const auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
            const DirectX::XMFLOAT2 jitterNDC = {
                (2.0f * jitterPixelSpace.x / renderRes.x),
                (-2.0f * jitterPixelSpace.y / renderRes.y)
            };
            camera.jitterNDC = jitterNDC;
            const auto jitterMatrix = DirectX::XMMatrixTranslation(jitterNDC.x, jitterNDC.y, 0.0f);
            projection = XMMatrixMultiply(projection, jitterMatrix);
        }

        camera.info.jitteredProjection = projection;
        camera.info.prevView = camera.info.view;
        camera.info.view = view;
        camera.info.viewInverse = cameraModel;
        camera.info.viewProjection = XMMatrixMultiply(camera.info.view, projection);
        camera.info.projectionInverse = XMMatrixInverse(nullptr, projection);

        const auto pos = GetGlobalPositionFromMatrix(worldMatrix.matrix);
        camera.info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0f };

        m_managerInterface.GetViewManager()->UpdateCamera(renderView.viewID, camera.info);
    });

    m_renderSyncLightQuery.each([&](flecs::entity entity, Components::Matrix& worldMatrix, Components::Light& light) {
        const XMVECTOR worldForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        light.lightInfo.dirWorldSpace = XMVector3Normalize(XMVector3TransformNormal(worldForward, worldMatrix.matrix));
        light.lightInfo.posWorldSpace = XMVectorSet(
            XMVectorGetX(worldMatrix.matrix.r[3]),
            XMVectorGetY(worldMatrix.matrix.r[3]),
            XMVectorGetZ(worldMatrix.matrix.r[3]),
            1.0f);
        switch (light.lightInfo.type) {
        case Components::LightType::Spot:
            light.lightInfo.boundingSphere = ComputeConeBoundingSphere(light.lightInfo.posWorldSpace, light.lightInfo.dirWorldSpace, light.lightInfo.maxRange, acos(light.lightInfo.outerConeAngle));
            break;
        case Components::LightType::Point:
            light.lightInfo.boundingSphere = {{
                XMVectorGetX(worldMatrix.matrix.r[3]),
                XMVectorGetY(worldMatrix.matrix.r[3]),
                XMVectorGetZ(worldMatrix.matrix.r[3]),
                light.lightInfo.maxRange }};
            break;
        default:
            break;
        }

        if (light.lightInfo.shadowCaster && entity.has<Components::LightViewInfo>()) {
            const Components::LightViewInfo& viewInfo = entity.get<Components::LightViewInfo>();
            m_managerInterface.GetLightManager()->UpdateLightBufferView(viewInfo.lightBufferView.get(), light.lightInfo);
            m_managerInterface.GetLightManager()->UpdateLightViewInfo(entity);
        }
    });

    // Clean up RenderTransformUpdated tags so they're fresh for next frame's IngestSnapshot.
    // Defer structural changes to avoid modifying tables while the query has them locked.
    world.defer_begin();
    m_renderTransformUpdatedCleanupQuery.each([](flecs::entity e) {
        e.remove<Components::RenderTransformUpdated>();
    });
    world.defer_end();
}

void Renderer::BeginFrameTaskGraphCapture() {
    br::telemetry::BeginFrameTaskGraphCapture(m_totalFramesRendered, m_frameIndex);
    m_lastFrameTaskNodeIndex = -1;
}

void Renderer::RecordFrameTaskStage(
    const char* stageName,
    br::telemetry::CpuTaskDomain domain,
    const std::chrono::steady_clock::time_point& stageStart,
    const std::chrono::steady_clock::time_point& stageEnd) {
    m_lastFrameTaskNodeIndex = br::telemetry::RecordFrameTaskNode(stageName, domain, m_lastFrameTaskNodeIndex, stageStart, stageEnd);
}

void Renderer::PublishFrameTaskGraphCapture() {
    br::telemetry::PublishFrameTaskGraphSnapshot();
}

void Renderer::CreateGlobalResources() {
}

void Renderer::CreateDefaultEnvironmentResources() {
    auto makeFallbackCubemap = [](uint32_t resolution, bool generateMipMaps, const char* name) {
        TextureDescription desc;
        desc.channels = 4;
        desc.isCubemap = true;
        desc.format = rhi::Format::R8G8B8A8_UNorm;
        desc.hasSRV = true;
        desc.generateMipMaps = generateMipMaps;

        ImageDimensions dims;
        dims.width = resolution;
        dims.height = resolution;
        dims.rowPitch = resolution * 4;
        dims.slicePitch = resolution * resolution * 4;
        for (int i = 0; i < 6; ++i) {
            desc.imageDimensions.push_back(dims);
        }

        auto cubemap = PixelBuffer::CreateShared(desc);
        cubemap->SetName(name);
        return cubemap;
    };

    auto reflectionResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("reflectionCubemapResolution")();
    auto skyboxResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("skyboxResolution")();

    m_defaultEnvironmentCubemap = makeFallbackCubemap(skyboxResolution, false, "Fallback Environment Cubemap");
    m_defaultEnvironmentPrefilteredCubemap = makeFallbackCubemap(reflectionResolution, true, "Fallback Prefiltered Environment Cubemap");

    rg::memory::SetResourceUsageHint(*m_defaultEnvironmentCubemap, "Fallback environment resources");
    rg::memory::SetResourceUsageHint(*m_defaultEnvironmentPrefilteredCubemap, "Fallback environment resources");
}

bool Renderer::IsSceneReadyForFrame(bool logWarnings) {
    if (!currentScene) {
        if (logWarnings && !m_warnedNullScene) {
            spdlog::warn("Renderer: current scene is null. Skipping scene update/render work until a valid scene is set.");
        }
        m_warnedNullScene = true;
        m_warnedMissingPrimaryCamera = false;
        return false;
    }

    m_warnedNullScene = false;

    const bool hasPrimaryCamera = m_sceneRenderOverlapEnabled
        ? (NeedsSceneSnapshotBootstrap() || (HasCommittedSceneSnapshot() && m_sceneRenderBridge.HasPrimaryCamera()))
        : currentScene->HasUsablePrimaryCamera();

    if (!hasPrimaryCamera) {
        if (logWarnings && !m_warnedMissingPrimaryCamera) {
            spdlog::warn("Renderer: primary camera is missing or invalid. Skipping scene update/render work until a valid camera is available.");
        }
        m_warnedMissingPrimaryCamera = true;
        return false;
    }

    m_warnedMissingPrimaryCamera = false;
    return true;
}

flecs::entity Renderer::GetValidatedPrimaryRenderCamera(bool attemptResync) {
    if (!currentScene || !RendererECSManager::GetInstance().IsAlive()) {
        return {};
    }

    if (m_sceneRenderOverlapEnabled && !HasCommittedSceneSnapshot()) {
        return {};
    }

    if (!m_sceneRenderOverlapEnabled && !currentScene->HasUsablePrimaryCamera()) {
        return {};
    }

    if (m_sceneRenderOverlapEnabled && NeedsSceneSnapshotBootstrap()) {
        if (attemptResync) {
            BootstrapCommittedSceneSnapshot();
        }

        if (!HasCommittedSceneSnapshot()) {
            return {};
        }
    }

    auto validateCamera = [](flecs::entity camera) {
        return camera
            && camera.is_alive()
            && camera.has<Components::Camera>()
            && camera.has<Components::RenderViewRef>()
            && camera.has<Components::DepthMap>();
    };

    auto primaryCamera = m_sceneRenderBridge.GetPrimaryCameraEntity();
    if (!validateCamera(primaryCamera) && attemptResync && !m_sceneRenderOverlapEnabled) {
        m_sceneRenderBridge.Sync(*currentScene, m_managerInterface);
        primaryCamera = m_sceneRenderBridge.GetPrimaryCameraEntity();
    }

    if (!validateCamera(primaryCamera)) {
        return {};
    }

    return primaryCamera;
}

void Renderer::SetSettings() {
	auto& settingsManager = SettingsManager::GetInstance();

    uint8_t numDirectionalCascades = 4;
	float maxShadowDistance = 30.0f;
	settingsManager.registerSetting<uint8_t>("numDirectionalLightCascades", numDirectionalCascades);
    settingsManager.registerSetting<float>("maxShadowDistance", maxShadowDistance);
    settingsManager.registerSetting<std::vector<float>>("directionalLightCascadeSplits", calculateCascadeSplits(numDirectionalCascades, 0.1f, 100, maxShadowDistance));
    settingsManager.registerSetting<uint16_t>("shadowResolution", 2048);
    settingsManager.registerSetting<float>("cameraSpeed", 10);
	settingsManager.registerSetting<bool>("enableWireframe", false);
	settingsManager.registerSetting<bool>("enableShadows", false);
	settingsManager.registerSetting<uint16_t>("skyboxResolution", 2048);
    settingsManager.registerSetting<uint16_t>("reflectionCubemapResolution", 512);
	settingsManager.registerSetting<bool>("enableImageBasedLighting", true);
	settingsManager.registerSetting<bool>("enablePunctualLighting", true);
	settingsManager.registerSetting<std::string>("environmentName", "");
	settingsManager.registerSetting<unsigned int>("outputType", OutputType::COLOR);
	settingsManager.registerSetting<unsigned int>("tonemapType", TonemapType::AMD_LPM);
    settingsManager.registerSetting<bool>("allowTearing", false);
	settingsManager.registerSetting<bool>("drawBoundingSpheres", false);
    settingsManager.registerSetting<bool>("enableClusteredLighting", m_clusteredLighting);
    settingsManager.registerSetting<DirectX::XMUINT3>("lightClusterSize", m_lightClusterSize);
    settingsManager.registerSetting<bool>("collectPipelineStatistics", false);
	// This feels like abuse of the settings manager, but it's the easiest way to get the renderable objects to the menu
    settingsManager.registerSetting<std::function<flecs::entity()>>("getSceneRoot", [this]() -> flecs::entity {
        if (!currentScene || m_sceneTaskInFlight.load()) {
            return {};
        }
        return currentScene->GetRoot();
        });
    settingsManager.registerSetting<std::function<void(uint64_t, DirectX::XMFLOAT3)>>("queueSceneNodePositionEdit", [this](uint64_t stableSceneID, DirectX::XMFLOAT3 position) {
        QueueSceneNodePositionEdit(stableSceneID, position);
        });
    settingsManager.registerSetting<std::function<void(uint64_t, float)>>("queueSceneNodeUniformScaleEdit", [this](uint64_t stableSceneID, float uniformScale) {
        QueueSceneNodeUniformScaleEdit(stableSceneID, uniformScale);
        });
    bool meshShaderSupported = DeviceManager::GetInstance().GetMeshShadersSupported();
	settingsManager.registerSetting<bool>("enableMeshShader", meshShaderSupported && m_useMeshShaders);
	settingsManager.registerSetting<bool>("enableIndirectDraws", meshShaderSupported);
	settingsManager.registerSetting<bool>("enableGTAO", m_gtaoEnabled);
	settingsManager.registerSetting<bool>("enableOcclusionCulling", m_occlusionCulling);
	settingsManager.registerSetting<bool>("enableMeshletCulling", m_meshletCulling);
    settingsManager.registerSetting<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName, CLodSoftwareRasterMode::WorkGraph);
    settingsManager.registerSetting<bool>("enableBloom", m_bloom);
    settingsManager.registerSetting<bool>("enableJitter", m_jitter);
    settingsManager.registerSetting<std::function<std::shared_ptr<Scene>(std::shared_ptr<Scene>)>>("appendScene", [this](std::shared_ptr<Scene> scene) -> std::shared_ptr<Scene> {
        return AppendScene(scene);
        });
    settingsManager.registerSetting<std::function<MeshManager*()>>("getMeshManager", [this]() -> MeshManager* {
        return m_pMeshManager.get();
        });
	settingsManager.registerSetting<UpscalingMode>("upscalingMode", UpscalingManager::GetInstance().GetCurrentUpscalingMode());
    settingsManager.registerSetting<UpscaleQualityMode>("upscalingQualityMode", UpscalingManager::GetInstance().GetCurrentUpscalingQualityMode());
	settingsManager.registerSetting<bool>("enableScreenSpaceReflections", m_screenSpaceReflections);
    settingsManager.registerSetting<bool>("useAsyncCompute", true);
	settingsManager.registerSetting<AutoAliasMode>("autoAliasMode", AutoAliasMode::Balanced);
    settingsManager.registerSetting<AutoAliasPackingStrategy>("autoAliasPackingStrategy", AutoAliasPackingStrategy::GreedySweepLine);
    settingsManager.registerSetting<bool>("autoAliasEnableLogging", false);
    settingsManager.registerSetting<bool>("autoAliasLogExclusionReasons", false);
    settingsManager.registerSetting<bool>("queueSchedulingEnableLogging", false);
    settingsManager.registerSetting<float>("queueSchedulingWidthScale", 1.0f);
    settingsManager.registerSetting<float>("queueSchedulingPenaltyBias", 0.0f);
    settingsManager.registerSetting<float>("queueSchedulingMinPenalty", 1.0f);
    settingsManager.registerSetting<float>("queueSchedulingResourcePressureWeight", 1.0f);
    settingsManager.registerSetting<float>("queueSchedulingUavPressureWeight", 0.5f);
	settingsManager.registerSetting<uint32_t>("autoAliasPoolRetireIdleFrames", 120u);
	settingsManager.registerSetting<float>("autoAliasPoolGrowthHeadroom", 1.5f);
    settingsManager.registerSetting<bool>("heavyDebug", false);
    settingsManager.registerSetting<uint32_t>("clodStreamingCpuUploadBudgetRequests", 50u);
    settingsManager.registerSetting<bool>(CLodDisableReyesRasterizationSettingName, false);
	settingsManager.registerSetting<uint32_t>(CLodReyesResourceBudgetBytesSettingName, 512u*1024u*1024u); // 500 MB for reyes
	settingsManager.registerSetting<uint32_t>("usdPointInstancerMaxInstances", 10000u);
    getShadowResolution = settingsManager.getSettingGetter<uint16_t>("shadowResolution");
    setCameraSpeed = settingsManager.getSettingSetter<float>("cameraSpeed");
	getCameraSpeed = settingsManager.getSettingGetter<float>("cameraSpeed");
	setWireframeEnabled = settingsManager.getSettingSetter<bool>("enableWireframe");
	getWireframeEnabled = settingsManager.getSettingGetter<bool>("enableWireframe");
	setShadowsEnabled = settingsManager.getSettingSetter<bool>("enableShadows");
	getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
	getSkyboxResolution = settingsManager.getSettingGetter<uint16_t>("skyboxResolution");
	setImageBasedLightingEnabled = settingsManager.getSettingSetter<bool>("enableImageBasedLighting");
	setEnvironment = settingsManager.getSettingSetter<std::string>("environmentName");
	getMeshShadersEnabled = settingsManager.getSettingGetter<bool>("enableMeshShader");
	getIndirectDrawsEnabled = settingsManager.getSettingGetter<bool>("enableIndirectDraws");
	getDrawBoundingSpheres = settingsManager.getSettingGetter<bool>("drawBoundingSpheres");
	getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
    

    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableShadows", [this](const bool& newValue) {
        // Trigger recompilation of the render graph when setting changes
        rebuildRenderGraph = true;
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<std::string>("environmentName", [this](const std::string& newValue) {
		SetEnvironmentInternal(s2ws(newValue));
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<unsigned int>("outputType", [this](const unsigned int& newValue) {
        ResourceManager::GetInstance().SetOutputType(newValue);
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableMeshShader", [this](const bool& newValue) {
		ToggleMeshShaders(newValue);
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableWireframe", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableIndirectDraws", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("allowTearing", [this](const bool& newValue) {
		m_allowTearing = newValue;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("drawBoundingSpheres", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableClusteredLighting", [this](const bool& newValue) {
		m_clusteredLighting = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableImageBasedLighting", [this](const bool& newValue) {
		m_imageBasedLighting = newValue;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableGTAO", [this](const bool& newValue) {
		m_gtaoEnabled = newValue;
		rebuildRenderGraph = true;
		}));
	m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableVisibilityRendering", [this](const bool& newValue) {
		m_visibilityRendering = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableOcclusionCulling", [this](const bool& newValue) {
		m_occlusionCulling = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName, [this](const CLodSoftwareRasterMode& newValue) {
        (void)newValue;
        rebuildRenderGraph = true;
        }));
        m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>(CLodDisableReyesRasterizationSettingName, [this](const bool& newValue) {
            (void)newValue;
            rebuildRenderGraph = true;
            }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<uint32_t>(CLodReyesResourceBudgetBytesSettingName, [this](const uint32_t& newValue) {
        (void)newValue;
        rebuildRenderGraph = true;
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableMeshletCulling", [this](const bool& newValue) {
		m_meshletCulling = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableBloom", [this](const bool& newValue) {
        m_bloom = newValue;
        rebuildRenderGraph = true;
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableJitter", [this](const bool& newValue) {
        m_jitter = newValue;
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<float>("maxShadowDistance", [](const float& newValue) {
		auto& settingsManager = SettingsManager::GetInstance();
		auto numDirectionalCascades = settingsManager.getSettingGetter<uint8_t>("numDirectionalLightCascades")();
		auto maxShadowDistance = settingsManager.getSettingGetter<float>("maxShadowDistance")();
        settingsManager.getSettingSetter<std::vector<float>>("directionalLightCascadeSplits")(calculateCascadeSplits(numDirectionalCascades, 0.1f, 100, maxShadowDistance));
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<std::vector<float>>("directionalLightCascadeSplits", [this](const std::vector<float>& newValue) {
        ResourceManager::GetInstance().SetDirectionalCascadeSplits(newValue);
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<UpscalingMode>("upscalingMode", [this](const UpscalingMode& newValue) {

        m_preFrameDeferredFunctions.defer([newValue, this]() { // Don't do this during a frame
            StallPipeline(); // Wait for all GPU work before destroying contexts
            UpscalingManager::GetInstance().Shutdown();
            UpscalingManager::GetInstance().InitFFX(); // Needs device
            UpscalingManager::GetInstance().SetUpscalingMode(newValue);
            UpscalingManager::GetInstance().Setup();

            FFXManager::GetInstance().Shutdown();
            FFXManager::GetInstance().InitFFX();

            CreateTextures();
            auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
            m_sceneRenderBridge.ResyncPrimaryCameraDepth(*m_pViewManager, renderRes.x, renderRes.y);
            rebuildRenderGraph = true;
            });
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<UpscaleQualityMode>("upscalingQualityMode", [this](const UpscaleQualityMode& newValue) {

        m_preFrameDeferredFunctions.defer([newValue, this]() { // Don't do this during a frame
            StallPipeline(); // Wait for all GPU work before destroying contexts
            UpscalingManager::GetInstance().SetUpscalingQualityMode(newValue);
            UpscalingManager::GetInstance().Shutdown();
            UpscalingManager::GetInstance().InitFFX(); // Recreate FSR context before Setup queries it
            UpscalingManager::GetInstance().Setup();
            FFXManager::GetInstance().Shutdown();
            FFXManager::GetInstance().InitFFX();
            CreateTextures();
            auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
            m_sceneRenderBridge.ResyncPrimaryCameraDepth(*m_pViewManager, renderRes.x, renderRes.y);
            rebuildRenderGraph = true;
            });
        }));
	m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableScreenSpaceReflections", [this](const bool& newValue) {
		m_screenSpaceReflections = newValue;
		rebuildRenderGraph = true;
		}));


	// Indirect draws require mesh shaders (due to not having implemented indirect draws with traditional pipelines)
	settingsManager.addImplicationConstraint("enableIndirectDraws", "enableMeshShader");

	// Occlusion culling requires meshlet culling (due to object occlusion and meshlet occlusion not being separate yet)
	settingsManager.addImplicationConstraint("enableOcclusionCulling", "enableMeshletCulling");

	// Visibility rendering requires mesh shaders (due to not having implemented visibility VS)
    settingsManager.addImplicationConstraint("enableVisibilityRendering", "enableMeshShader");

    // Visibility rendering requires meshlet culling (due to reliance on visible clusters list)
	settingsManager.addImplicationConstraint("enableVisibilityRendering", "enableMeshletCulling");

    //Visibility rendering requires indirect draws (because of a bug) TODO: fix
	settingsManager.addImplicationConstraint("enableVisibilityRendering", "enableIndirectDraws");
}

void Renderer::ToggleMeshShaders(bool useMeshShaders) {
    // We need to:
    // 1. Remove all meshes in the global mesh library from the mesh manager
	// 2. Re-add them to the mesh manager
    // 3. Get all objects with mesh instances by querying the ECS
	// 4. Remove and re-add all instances to the mesh manager
	// 5. Remove and re-add all objects from the object manager to rebuild indirect draw info

    auto& world = RendererECSManager::GetInstance().GetWorld();
	auto& meshLibrary = world.get_mut<Components::GlobalMeshLibrary>().meshes;

	// Remove all meshes from the mesh manager
	for (auto& meshPair : meshLibrary) {
		auto& mesh = meshPair.second;
		m_pMeshManager->RemoveMesh(mesh.lock().get());
	}
	// Re-add them to the mesh manager
	for (auto& meshPair : meshLibrary) {
		auto& mesh = meshPair.second;
        auto ptr = mesh.lock();
        m_pMeshManager->AddMesh(ptr, useMeshShaders);
	}

	// Get all active objects with mesh instances by querying the ECS
    auto query = world.query_builder<Components::RenderableObject, Components::ObjectDrawInfo>().with<Components::Active>()
        .build();

    world.defer_begin();
    query.each([&](flecs::entity entity, const Components::RenderableObject& object, const Components::ObjectDrawInfo& drawInfo) {
        auto meshInstances = entity.try_get<Components::MeshInstances>();

        if (meshInstances) {
            for (auto& meshInstance : meshInstances->meshInstances) {
                m_pMeshManager->RemoveMeshInstance(meshInstance.get());
                m_pMeshManager->AddMeshInstance(meshInstance.get(), useMeshShaders);
            }
        }

		// Remove and re-add all objects from the object manager to rebuild indirect draw info
		m_pObjectManager->RemoveObject(&drawInfo);
		auto newDrawInfo = m_pObjectManager->AddObject(object.perObjectCB, meshInstances);
		entity.set<Components::ObjectDrawInfo>(newDrawInfo);
            });
    world.defer_end();
}

void Renderer::LoadPipeline(HWND hwnd, UINT x_res, UINT y_res) {
    UINT dxgiFactoryFlags = 0;

#if defined(ENABLE_NSIGHT_AFTERMATH)
    m_gpuCrashTracker.Initialize();
#endif

    DeviceManager::GetInstance().Initialize();

	auto device = DeviceManager::GetInstance().GetDevice();

    UpscalingManager::GetInstance().InitializeAdapter();

    auto result = device.CreateSwapchain(hwnd, x_res, y_res, rhi::Format::R8G8B8A8_UNorm, m_numFramesInFlight, m_allowTearing, m_swapChain);


#if defined(ENABLE_NSIGHT_AFTERMATH)
    const uint32_t aftermathFlags =
        GFSDK_Aftermath_FeatureFlags_EnableMarkers |             // Enable event marker tracking.
        GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |    // Enable tracking of resources.
        GFSDK_Aftermath_FeatureFlags_CallStackCapturing |        // Capture call stacks for all draw calls, compute dispatches, and resource copies.
        GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo;    // Generate debug information for shaders.

    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_DX12_Initialize(
        GFSDK_Aftermath_Version_API,
        aftermathFlags,
        rhi::dx12::get_device(device)));
#endif

    // Create RTV descriptor heap
	rhi::DescriptorHeapDesc rtvHeapDesc = {};
    rtvHeapDesc.capacity = m_numFramesInFlight;
    rtvHeapDesc.type = rhi::DescriptorHeapType::RTV;
	rtvHeapDesc.shaderVisible = false;
	rtvHeapDesc.debugName = "RTV Descriptor Heap";
    result = device.CreateDescriptorHeap(rtvHeapDesc, rtvHeap);

    rtvDescriptorSize = device.GetDescriptorHandleIncrementSize(rhi::DescriptorHeapType::RTV);

    // Create frame resources
    renderTargets.resize(m_numFramesInFlight);
    for (UINT n = 0; n < m_numFramesInFlight; n++) {        
        renderTargets[n] = m_swapChain->Image(n);
    }

    // Wrap swapchain images for render-graph tracking
    m_backbufferResources.resize(m_numFramesInFlight);
    for (UINT n = 0; n < m_numFramesInFlight; n++) {
        m_backbufferResources[n] = std::make_shared<ExternalTextureResource>(
            renderTargets[n], x_res, y_res);
        m_backbufferResources[n]->SetName("Backbuffer " + std::to_string(n));
    }
    m_dynamicBackbuffer = std::make_shared<DynamicResource>(m_backbufferResources[0]);
    m_dynamicBackbuffer->SetName("Backbuffer");

    CreateRTVs();

    // Create command allocator

	m_commandAllocators.resize(m_numFramesInFlight);
	m_commandLists.resize(m_numFramesInFlight);
    for (int i = 0; i < m_numFramesInFlight; i++) {
        rhi::CommandAllocatorPtr commandAllocator;
        rhi::CommandListPtr commandList;
        result = device.CreateCommandAllocator(rhi::QueueKind::Graphics, commandAllocator);
        result = device.CreateCommandList(rhi::QueueKind::Graphics, commandAllocator.Get(), commandList);
		m_commandAllocators[i] = std::move(commandAllocator);
		m_commandLists[i] = std::move(commandList);
        m_commandLists[i]->End();
    }

    // Create per-frame fence information
	m_frameFenceValues.resize(m_numFramesInFlight);
	for (int i = 0; i < m_numFramesInFlight; i++) {
		m_frameFenceValues[i] = 0;
	}

    result = device.CreateTimeline(m_frameFence);
    result = device.CreateTimeline(m_readbackFence);
}

void Renderer::CreateTextures() {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    // Create HDR color target
    TextureDescription hdrDesc;
    hdrDesc.arraySize = 1;
    hdrDesc.channels = 4; // RGBA
    hdrDesc.isCubemap = false;
    hdrDesc.hasRTV = true;
    hdrDesc.hasUAV = true;
    hdrDesc.hasNonShaderVisibleUAV = true;
    hdrDesc.format = rhi::Format::R16G16B16A16_Float; // HDR format
    hdrDesc.generateMipMaps = false; // For bloom downsampling
    ImageDimensions dims;
    dims.height = resolution.y;
    dims.width = resolution.x;
    hdrDesc.imageDimensions.push_back(dims);
    hdrDesc.allowAlias = true;
    auto hdrColorTarget = PixelBuffer::CreateSharedUnmaterialized(hdrDesc);
    hdrColorTarget->SetName("Primary Camera HDR Color Target");
    rg::memory::SetResourceUsageHint(*hdrColorTarget, "Primary color buffers");
	m_coreResourceProvider.m_HDRColorTarget = hdrColorTarget;

    auto outputResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
    hdrDesc.imageDimensions[0].width = outputResolution.x;
    hdrDesc.imageDimensions[0].height = outputResolution.y;
    hdrDesc.generateMipMaps = true;
    hdrDesc.allowAlias = true;
	auto upscaledHDRColorTarget = PixelBuffer::CreateSharedUnmaterialized(hdrDesc);
	upscaledHDRColorTarget->SetName("Upscaled HDR Color Target");
    rg::memory::SetResourceUsageHint(*upscaledHDRColorTarget, "Upscaled color buffers");
	m_coreResourceProvider.m_upscaledHDRColorTarget = upscaledHDRColorTarget;

    TextureDescription motionVectors;
    motionVectors.arraySize = 1;
    motionVectors.channels = 2;
    motionVectors.isCubemap = false;
    motionVectors.hasRTV = true;
    motionVectors.format = rhi::Format::R16G16_Float;
    motionVectors.generateMipMaps = false;
    motionVectors.hasSRV = true;
    motionVectors.hasUAV = true;
    motionVectors.hasNonShaderVisibleUAV = true;
    motionVectors.srvFormat = rhi::Format::R16G16_Float;
    ImageDimensions motionVectorsDims = { resolution.x, resolution.y, 0, 0 };
    motionVectors.imageDimensions.push_back(motionVectorsDims);
	motionVectors.allowAlias = true;
    auto motionVectorsBuffer = PixelBuffer::CreateSharedUnmaterialized(motionVectors);
    motionVectorsBuffer->SetName("Motion Vectors");
    rg::memory::SetResourceUsageHint(*motionVectorsBuffer, "GBuffer");
	m_coreResourceProvider.m_gbufferMotionVectors = motionVectorsBuffer;
}

void Renderer::CreateRTVs() {
    auto device = DeviceManager::GetInstance().GetDevice();
    // Recreate the render target views
    for (UINT n = 0; n < m_numFramesInFlight; n++) {
        renderTargets[n] = m_swapChain->Image(n);
        rhi::RtvDesc rtvDesc = {};
        rtvDesc.dimension = rhi::RtvDim::Texture2D;
        rtvDesc.formatOverride = rhi::Format::R8G8B8A8_UNorm;
        rtvDesc.range = { 0, 1, 0, 1 };
        device.CreateRenderTargetView({ rtvHeap->GetHandle(), n }, renderTargets[n], rtvDesc);

        // Keep external texture wrappers in sync after resize
        if (n < m_backbufferResources.size() && m_backbufferResources[n]) {
            m_backbufferResources[n]->SetHandle(renderTargets[n]);
        }
    }
}

void Renderer::OnResize(UINT newWidth, UINT newHeight) {
    // Wait for all in-flight GPU work before destroying resources
	StallPipeline();

    // Release the resources tied to the swap chain
    auto numFramesInFlight = getNumFramesInFlight();

    // Resize the swap chain
	m_swapChain->ResizeBuffers(m_numFramesInFlight, newWidth, newHeight, rhi::Format::R8G8B8A8_UNorm, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH); // TODO: Port flags to RHI

    m_frameIndex = static_cast<uint8_t>(m_swapChain->CurrentImageIndex());

    CreateRTVs();

	SettingsManager::GetInstance().getSettingSetter<DirectX::XMUINT2>("outputResolution")({ newWidth, newHeight });

    UpscalingManager::GetInstance().Shutdown();
    UpscalingManager::GetInstance().Setup();
    FFXManager::GetInstance().Shutdown();
    FFXManager::GetInstance().InitFFX();

    CreateTextures();

    auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    m_sceneRenderBridge.ResyncPrimaryCameraDepth(*m_pViewManager, renderRes.x, renderRes.y);

	//Rebuild the render graph
	rebuildRenderGraph = true;
}


void Renderer::WaitForFrame(uint8_t currentFrameIndex) {
	// Wait until the GPU has completed commands up to this fence point.
	auto device = DeviceManager::GetInstance().GetDevice();
	auto completedValue = m_frameFence->GetCompletedValue();
    if (completedValue < m_frameFenceValues[currentFrameIndex]) {
        m_frameFence->HostWait(m_frameFenceValues[currentFrameIndex]);
    }
}

void Renderer::Update(float elapsedSeconds) {
    ZoneScopedN("Renderer::Update");

    BeginFrameTaskGraphCapture();

    const auto runCapturedStage = [this](const char* stageName, auto&& stageFn) {
        const auto stageStart = std::chrono::steady_clock::now();
        stageFn();
        const auto stageEnd = std::chrono::steady_clock::now();
        RecordFrameTaskStage(stageName, br::telemetry::CpuTaskDomain::MainThread, stageStart, stageEnd);
    };

    if (!IsSceneReadyForFrame()) {
        return;
    }
    runCapturedStage("SceneExplorerEdits", [&]() {
        ZoneScopedN("Renderer::Update::SceneExplorerEdits");
        FlushPendingSceneExplorerEdits();
    });
    if (NeedsSceneSnapshotBootstrap()) {
        runCapturedStage("BootstrapSceneSnapshot", [&]() {
            ZoneScopedN("Renderer::Update::BootstrapSceneSnapshot");
            if (currentScene->HasUsablePrimaryCamera()) {
                Components::Position& cameraPosition = currentScene->GetPrimaryCameraPosition();
                Components::Rotation& cameraRotation = currentScene->GetPrimaryCameraRotation();
                ApplyMovement(cameraPosition, cameraRotation, movementState, elapsedSeconds);
                RotatePitchYaw(cameraRotation, verticalAngle, horizontalAngle);
                currentScene->GetPrimaryCamera().modified<Components::Position>();
                currentScene->GetPrimaryCamera().modified<Components::Rotation>();
                verticalAngle = 0.0f;
                horizontalAngle = 0.0f;
            }
            RunGameUpdateStage(elapsedSeconds);
            RunTransformPropagationStage();
            BootstrapCommittedSceneSnapshot();
        });
    } else {
        runCapturedStage("CommitSceneSnapshot", [&]() {
            ZoneScopedN("Renderer::Update::CommitSceneSnapshot");
            CommitCompletedSceneSnapshot();
        });
    }

    runCapturedStage("AnimationUpdate", [&]() {
        RunAnimationUpdateStage(elapsedSeconds);
    });
    // Flush deferred functions before rebuilding the render graph so that
    // deferred state changes (e.g. environment creation from SetEnvironmentInternal)
    // are visible when the graph is constructed.
    if (!m_preFrameDeferredFunctions.empty()) {
        runCapturedStage("DeferredWorkEarly", [&]() {
            ZoneScopedN("Renderer::Update::DeferredWorkEarly");
            m_preFrameDeferredFunctions.flush();
        });
    }
    if (rebuildRenderGraph) {
        runCapturedStage("RenderGraphBuild", [&]() {
            ZoneScopedN("Renderer::Update::RenderGraphBuild");
		    CreateRenderGraph();
        });
    }
    runCapturedStage("RenderResourceSync", [&]() {
        RunRenderResourceSyncStage();
    });

    auto& world = RendererECSManager::GetInstance().GetWorld();

    auto camera = GetValidatedPrimaryRenderCamera(false);
    if (!camera) {
        rebuildRenderGraph = true;
        spdlog::warn("Renderer: bridged primary camera is unavailable after scene sync. Skipping frame update work.");
        return;
    }
    unsigned int cameraIndex = m_pViewManager->Get(camera.get<Components::RenderViewRef>().viewID)->gpu.cameraBufferIndex;
	auto& commandAllocator = m_commandAllocators[m_frameIndex];
	auto& commandList = m_commandLists[m_frameIndex];


    commandAllocator->Recycle();
    auto& resourceManager = ResourceManager::GetInstance();
    auto res = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    runCapturedStage("PerFrameBuffer", [&]() {
        ZoneScopedN("Renderer::Update::PerFrameBuffer");
        resourceManager.UpdatePerFrameBuffer(cameraIndex, m_pLightManager->GetNumLights(), { res.x, res.y }, m_lightClusterSize, static_cast<uint32_t>(m_totalFramesRendered));
    });

    const Components::DrawStats& drawStats = world.get<Components::DrawStats>();
    auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    auto outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
    UpdateContext updateData{};
    updateData.drawStats = drawStats;
    updateData.objectManager = m_pObjectManager.get();
    updateData.meshManager = m_pMeshManager.get();
    updateData.indirectCommandBufferManager = m_pIndirectCommandBufferManager.get();
    updateData.viewManager = m_pViewManager.get();
    updateData.lightManager = m_pLightManager.get();
    updateData.environmentManager = m_pEnvironmentManager.get();
    updateData.materialManager = m_pMaterialManager.get();
    updateData.currentScene = m_sceneRenderOverlapEnabled ? nullptr : currentScene.get();
    updateData.frameIndex = m_frameIndex;
    updateData.frameFenceValue = m_currentFrameFenceValue;
    updateData.renderResolution = renderRes;
    updateData.outputResolution = outputRes;
    updateData.deltaTime = elapsedSeconds;

    struct RendererUpdateHostData : IHostExecutionData {
        const UpdateContext* data = nullptr;

        const void* TryGet(std::type_index t) const noexcept override {
            if (t == std::type_index(typeid(UpdateContext))) {
                return data;
            }
            return nullptr;
        }
    };

    RendererUpdateHostData updateHostData;
    updateHostData.data = &updateData;

    runCapturedStage("FlushUploadPolicies", [&]() {
        ZoneScopedN("Renderer::Update::FlushUploadPolicies");
        rg::runtime::FlushUploadPolicies();
    });

    UpdateExecutionContext context{};
    context.frameIndex = m_frameIndex;
    context.frameFenceValue = m_currentFrameFenceValue;
    context.deltaTime = elapsedSeconds;
    context.hostData = &updateHostData;

	auto& deviceManager = DeviceManager::GetInstance();

    runCapturedStage("RenderGraphUpdate", [&]() {
        ZoneScopedN("Renderer::Update::RenderGraphUpdate");
        currentRenderGraph->Update(context, deviceManager.GetDevice());
    });

    runCapturedStage("ScheduleSceneUpdate", [&]() {
        ZoneScopedN("Renderer::Update::ScheduleSceneUpdate");
        ScheduleSceneUpdateTask(elapsedSeconds);
    });

    runCapturedStage("WaitForFrame", [&]() {
        ZoneScopedN("Renderer::Update::WaitForFrame");
        WaitForFrame(m_frameIndex); // Wait for the previous iteration of the frame to finish
        });
    rg::runtime::BeginUploadPolicyFrame();

    auto graphicsQueue = deviceManager.GetGraphicsQueue();
    auto computeQueue = deviceManager.GetComputeQueue();
    runCapturedStage("FrameMaintenance", [&]() {
        if (currentRenderGraph) {
            ZoneScopedN("Renderer::Update::FrameStatistics");
            if (auto* statisticsService = currentRenderGraph->GetStatisticsService()) {
                statisticsService->OnFrameComplete(m_frameIndex, computeQueue); // Gather statistics for the last iteration of the frame
                statisticsService->OnFrameComplete(m_frameIndex, graphicsQueue); // Gather statistics for the last iteration of the frame
            }
        }

        if (currentRenderGraph) {
            ZoneScopedN("Renderer::Update::DeferredReleases");
            if (auto* uploadService = currentRenderGraph->GetUploadService()) {
                uploadService->ProcessDeferredReleases(m_frameIndex);
            }
        }
        });

    commandList->Recycle(commandAllocator.Get());
}

void Renderer::PostUpdate() {
	if (!currentScene) {
        return;
    }
	currentScene->PostUpdate();
}

void Renderer::Render() {
    ZoneScopedN("Renderer::Render");

    const auto runCapturedStage = [this](const char* stageName, auto&& stageFn) {
        const auto stageStart = std::chrono::steady_clock::now();
        stageFn();
        const auto stageEnd = std::chrono::steady_clock::now();
        RecordFrameTaskStage(stageName, br::telemetry::CpuTaskDomain::MainThread, stageStart, stageEnd);
    };

    auto deltaTime = m_frameTimer.tick();
    if (!IsSceneReadyForFrame()) {
        return;
    }

    if (!currentRenderGraph) {
        return;
    }

    // Record all the commands we need to render the scene into the command list
    auto& commandAllocator = m_commandAllocators[m_frameIndex];
    auto& commandList = m_commandLists[m_frameIndex];

    auto& world = RendererECSManager::GetInstance().GetWorld();
	const Components::DrawStats& drawStats = world.get<Components::DrawStats>();
    auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    auto outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();

    auto& deviceManager = DeviceManager::GetInstance();

    runCapturedStage("PrepareRenderContext", [&]() {
        m_context.currentScene = m_sceneRenderOverlapEnabled ? nullptr : currentScene.get();
        m_context.hasPrimaryCamera = false;
        m_context.primaryViewID = 0;
        m_context.textureDescriptorHeap = rg::runtime::GetActiveSRVDescriptorHeap();
        m_context.samplerDescriptorHeap = rg::runtime::GetActiveSamplerDescriptorHeap();
        m_context.rtvHeap = rtvHeap.Get();
        m_context.rtvDescriptorSize = rtvDescriptorSize;
        m_context.dsvDescriptorSize = dsvDescriptorSize;
        m_context.frameIndex = m_frameIndex;
		m_context.frameNumber = m_totalFramesRendered;
        m_context.frameFenceValue = m_currentFrameFenceValue;
        m_context.renderResolution = { renderRes.x, renderRes.y };
	    m_context.outputResolution = { outputRes.x, outputRes.y };
	    m_context.viewManager = m_pViewManager.get();
	    m_context.objectManager = m_pObjectManager.get();
	    m_context.meshManager = m_pMeshManager.get();
	    m_context.indirectCommandBufferManager = m_pIndirectCommandBufferManager.get();
	    m_context.lightManager = m_pLightManager.get();
	    m_context.environmentManager = m_pEnvironmentManager.get();
	    m_context.materialManager = m_pMaterialManager.get();
	    m_context.drawStats = drawStats;
	    m_context.deltaTime = deltaTime;
        m_context.sceneOverlapStatus = GetSceneOverlapStatus();

        auto primaryCamera = GetValidatedPrimaryRenderCamera(false);
        if (primaryCamera) {
            m_context.hasPrimaryCamera = true;
            m_context.primaryViewID = primaryCamera.get<Components::RenderViewRef>().viewID;
            m_context.primaryCamera = primaryCamera.get<Components::Camera>();
            if (auto depthMap = primaryCamera.try_get<Components::DepthMap>()) {
                m_context.primaryDepthMap = *depthMap;
            }
        }

        unsigned int globalPSOFlags = 0;
        if (m_imageBasedLighting) {
            globalPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
        }
	    if (m_clusteredLighting) {
		    globalPSOFlags |= PSOFlags::PSO_CLUSTERED_LIGHTING;
	    }
        if (m_screenSpaceReflections) {
            globalPSOFlags |= PSOFlags::PSO_SCREENSPACE_REFLECTIONS;
        }
	    m_context.globalPSOFlags = globalPSOFlags;
    });

    struct RendererHostFrameData : IHostExecutionData {
        rhi::DescriptorHeap textureDescriptorHeap;
        rhi::DescriptorHeap samplerDescriptorHeap;
        DirectX::XMUINT2 renderResolution{};
        DirectX::XMUINT2 outputResolution{};
        const RenderContext* renderContext = nullptr;

        const void* TryGet(std::type_index t) const noexcept override {
            if (t == std::type_index(typeid(RenderContext))) {
                return renderContext;
            }
            if (t == std::type_index(typeid(RendererHostFrameData))) {
                return this;
            }
            return nullptr;
        }
    };

    RendererHostFrameData hostFrameData{};
    hostFrameData.textureDescriptorHeap = m_context.textureDescriptorHeap;
    hostFrameData.samplerDescriptorHeap = m_context.samplerDescriptorHeap;
    hostFrameData.renderResolution = m_context.renderResolution;
    hostFrameData.outputResolution = m_context.outputResolution;
    hostFrameData.renderContext = &m_context;

    PassExecutionContext passExecutionContext{};
    passExecutionContext.device = deviceManager.GetDevice();
    passExecutionContext.frameIndex = m_context.frameIndex;
    passExecutionContext.frameFenceValue = m_context.frameFenceValue;
    passExecutionContext.deltaTime = m_context.deltaTime;
    passExecutionContext.hostData = &hostFrameData;
   
    commandList->End();

    // Execute the command list
    auto graphicsQueue = deviceManager.GetGraphicsQueue();
    runCapturedStage("SubmitSetup", [&]() {
        ZoneScopedN("Renderer::Render::SubmitSetup");
        graphicsQueue.Submit({ &commandList.Get() });
    });

    // Sync SettingsManager values into OpenRenderGraphSettings so the
    // DefaultRenderGraphSettingsService reads up-to-date values.
    {
        auto& sm = SettingsManager::GetInstance();
        rg::runtime::OpenRenderGraphSettings orgSettings{};
        orgSettings.numFramesInFlight        = m_numFramesInFlight;
        orgSettings.collectPipelineStatistics = false;
        orgSettings.useAsyncCompute           = sm.getSettingGetter<bool>("useAsyncCompute")();
        orgSettings.autoAliasMode             = static_cast<uint8_t>(sm.getSettingGetter<AutoAliasMode>("autoAliasMode")());
        orgSettings.autoAliasPackingStrategy  = static_cast<uint8_t>(sm.getSettingGetter<AutoAliasPackingStrategy>("autoAliasPackingStrategy")());
        orgSettings.autoAliasEnableLogging    = sm.getSettingGetter<bool>("autoAliasEnableLogging")();
        orgSettings.autoAliasLogExclusionReasons = sm.getSettingGetter<bool>("autoAliasLogExclusionReasons")();
        orgSettings.queueSchedulingEnableLogging = sm.getSettingGetter<bool>("queueSchedulingEnableLogging")();
        orgSettings.queueSchedulingWidthScale = sm.getSettingGetter<float>("queueSchedulingWidthScale")();
        orgSettings.queueSchedulingPenaltyBias = sm.getSettingGetter<float>("queueSchedulingPenaltyBias")();
        orgSettings.queueSchedulingMinPenalty = sm.getSettingGetter<float>("queueSchedulingMinPenalty")();
        orgSettings.queueSchedulingResourcePressureWeight = sm.getSettingGetter<float>("queueSchedulingResourcePressureWeight")();
        orgSettings.queueSchedulingUavPressureWeight = sm.getSettingGetter<float>("queueSchedulingUavPressureWeight")();
        orgSettings.autoAliasPoolRetireIdleFrames = sm.getSettingGetter<uint32_t>("autoAliasPoolRetireIdleFrames")();
        orgSettings.autoAliasPoolGrowthHeadroom   = sm.getSettingGetter<float>("autoAliasPoolGrowthHeadroom")();
        orgSettings.heavyDebug                = sm.getSettingGetter<bool>("heavyDebug")();
        rg::runtime::SetOpenRenderGraphSettings(orgSettings);
    }

    runCapturedStage("RenderGraphExecute", [&]() {
        ZoneScopedN("Renderer::Render::RenderGraphExecute");
        m_dynamicBackbuffer->SetResource(m_backbufferResources[m_frameIndex]);
        currentRenderGraph->Execute(passExecutionContext); // Main render graph execution
    });

	// Transition backbuffer to Common for present
	commandList->Recycle(commandAllocator.Get());
	rhi::TextureBarrier rtvBarrier = {};
	rtvBarrier.afterAccess = rhi::ResourceAccessType::Common;
	rtvBarrier.afterLayout = rhi::ResourceLayout::Common;
	rtvBarrier.afterSync = rhi::ResourceSyncState::All;
	rtvBarrier.beforeAccess = rhi::ResourceAccessType::RenderTarget;
	rtvBarrier.beforeLayout = rhi::ResourceLayout::RenderTarget;
	rtvBarrier.beforeSync = rhi::ResourceSyncState::All;
	rtvBarrier.texture = renderTargets[m_frameIndex];
	rhi::BarrierBatch batch = {};
	batch.textures = { &rtvBarrier };
    runCapturedStage("TransitionForPresent", [&]() {
        ZoneScopedN("Renderer::Render::TransitionForPresent");
        commandList->Barriers(batch);
    });

    // Keep the symbolic tracker in sync with the manual barrier above so the
    // graph emits a Common->RenderTarget transition on the next frame.
    m_backbufferResources[m_frameIndex]->ResetToCommon();

    commandList->End();

    // Execute the command list
    runCapturedStage("SubmitPresent", [&]() {
        ZoneScopedN("Renderer::Render::SubmitPresent");
        graphicsQueue.Submit({ &commandList.Get() });
    });

    // Present the frame
    runCapturedStage("Present", [&]() {
        ZoneScopedN("Renderer::Render::Present");
        m_swapChain->Present(!m_allowTearing);
    });

    AdvanceFrameIndex();

    runCapturedStage("SignalFence", [&]() {
        ZoneScopedN("Renderer::Render::SignalFence");
        SignalFence(graphicsQueue, m_frameIndex);
    });

    runCapturedStage("ReadbackRequests", [&]() {
        if (currentRenderGraph) {
            ZoneScopedN("Renderer::Render::ReadbackRequests");
            if (auto* readbackService = currentRenderGraph->GetReadbackService()) {
                readbackService->ProcessReadbackRequests(); // Process readback captures
            }
        }
        if (m_pReadbackManager) {
            m_pReadbackManager->ProcessReadbackRequests(); // Save images to disk if requested
        }
    });

    runCapturedStage("DeletionProcessing", [&]() {
        DeletionManager::GetInstance().ProcessDeletions();
    });

    PublishFrameTaskGraphCapture();
    FrameMark;
}

void Renderer::SignalFence(rhi::Queue commandQueue, uint8_t frameIndexToSignal) {
    // Signal the fence
    m_currentFrameFenceValue++;
	commandQueue.Signal({ m_frameFence->GetHandle(), m_currentFrameFenceValue });

    // Store the fence value for the current frame
    m_frameFenceValues[frameIndexToSignal] = m_currentFrameFenceValue;
}

void Renderer::AdvanceFrameIndex() {
    m_frameIndex = (m_frameIndex + 1) % m_numFramesInFlight;
    m_totalFramesRendered += 1;
}

void Renderer::FlushCommandQueue() {
	auto device = DeviceManager::GetInstance().GetDevice();
    auto flushQueue = [&](rhi::Queue queue, const char* debugName) {
        rhi::TimelinePtr flushFence;
        auto result = device.CreateTimeline(flushFence, 0, debugName);
        if (result != rhi::Result::Ok || !flushFence) {
            throw std::runtime_error("Failed to create queue flush timeline");
        }

        queue.Signal({ flushFence->GetHandle(), 1 });
        flushFence->HostWait(1);
    };

    auto& deviceManager = DeviceManager::GetInstance();
    flushQueue(deviceManager.GetGraphicsQueue(), "RendererFlushGraphics");
    flushQueue(deviceManager.GetComputeQueue(), "RendererFlushCompute");
    flushQueue(deviceManager.GetCopyQueue(), "RendererFlushCopy");

    if (currentRenderGraph) {
        auto& registry = currentRenderGraph->GetQueueRegistry();
        for (size_t i = 0; i < registry.SlotCount(); ++i) {
            auto slot = static_cast<QueueSlotIndex>(static_cast<uint8_t>(i));
            auto queue = registry.GetQueue(slot);
            auto& fence = registry.GetFence(slot);
            const uint64_t fenceValue = registry.GetNextFenceValue(slot);
            queue.Signal({ fence.GetHandle(), fenceValue });
            fence.HostWait(fenceValue);
        }
    }
}

void Renderer::StallPipeline() {
    for (uint8_t i = 0; i < m_numFramesInFlight; ++i) {
        WaitForFrame(i);
    }
    FlushCommandQueue();
}

void Renderer::Cleanup() {
    spdlog::info("In cleanup");
    // Wait for all GPU frames to complete
	spdlog::info("Stalling pipeline for cleanup");
	StallPipeline();
	m_sceneRenderBridge.Clear(m_managerInterface);
	m_renderSyncObjectQuery = {};
	m_renderSyncCameraQuery = {};
	m_renderSyncLightQuery = {};
	m_renderTransformUpdatedCleanupQuery = {};
	m_renderSyncQueriesBuilt = false;
	spdlog::info("Cleaning up resources");
    if (currentRenderGraph) {
        if (auto* uploadService = currentRenderGraph->GetUploadService()) {
            uploadService->Cleanup();
        }
        if (m_uploadPolicyService) {
            m_uploadPolicyService->Cleanup();
        }
        if (auto* readbackService = currentRenderGraph->GetReadbackService()) {
            readbackService->Cleanup();
        }
        if (auto* descriptorService = currentRenderGraph->GetDescriptorService()) {
            descriptorService->Cleanup();
        }
    }
    if (m_pReadbackManager) {
        m_pReadbackManager->Cleanup();
    }
    ResourceManager::GetInstance().Cleanup();
    TaskSchedulerManager::GetInstance().Cleanup();
    m_coreResourceProvider.Cleanup();
    currentRenderGraph.reset();
    rg::runtime::SetActiveUploadService(nullptr);
    rg::runtime::SetActiveUploadPolicyService(nullptr);
    rg::runtime::SetActiveDescriptorService(nullptr);
    m_uploadPolicyService.reset();
    m_renderGraphRuntimeInitialized = false;
    m_currentEnvironment.reset();
    m_defaultEnvironmentCubemap.reset();
    m_defaultEnvironmentPrefilteredCubemap.reset();
	currentScene.reset();
	m_pIndirectCommandBufferManager.reset();
	m_pViewManager.reset();
	m_pLightManager.reset();
	m_pMeshManager.reset();
	m_pObjectManager.reset();
    m_pMaterialManager.reset();
    m_pEnvironmentManager.reset();
	m_pSkeletonManager.reset();
    m_pReadbackManager.reset();
    m_pTextureFactory.reset();
    m_settingsSubscriptions.clear();
    m_warnedUsingFallbackEnvironment = false;
    m_warnedNullScene = false;
    m_warnedMissingPrimaryCamera = false;
    m_readbackFence.Reset();
	spdlog::info("Cleaning up singletons");
    Material::DestroyDefaultMaterial();
    Menu::GetInstance().Cleanup();
    PSOManager::GetInstance().Cleanup();
	FFXManager::GetInstance().Shutdown();
	UpscalingManager::GetInstance().Shutdown();
    TrackedEntityToken::ResetHooks();
    Resource::ResetEntityHooks();
    RendererECSManager::GetInstance().Cleanup();
    DeletionManager::GetInstance().Cleanup();
	spdlog::info("Cleaning up swap chain");
    m_swapChain.Reset();
	spdlog::info("Cleaning up device manager");
    DeviceManager::GetInstance().Cleanup();
	spdlog::info("Cleanup complete");
}

void Renderer::CheckDebugMessages() {
    auto device = DeviceManager::GetInstance().GetDevice();
    if (device) {
        device.CheckDebugMessages();
    }
}

void Renderer::SetEnvironment(std::string environmentName) {
	setEnvironment(environmentName);
}

std::shared_ptr<Scene>& Renderer::GetCurrentScene() {
    return currentScene;
}

void Renderer::SetCurrentScene(std::shared_ptr<Scene> newScene) {
	if (!newScene) {
    m_sceneTaskCompleted.store(false);
    {
        std::scoped_lock lock(m_sceneSnapshotMutex);
        m_committedSceneSnapshot.reset();
        m_completedSceneSnapshot.reset();
    }
        m_sceneRenderBridge.Clear(m_managerInterface);
        currentScene.reset();
        rebuildRenderGraph = true;
        IsSceneReadyForFrame(true);
        return;
    }

	if (currentScene != newScene) {
		m_sceneRenderBridge.Clear(m_managerInterface);
	}

    m_sceneTaskCompleted.store(false);
    {
        std::scoped_lock lock(m_sceneSnapshotMutex);
        m_committedSceneSnapshot.reset();
        m_completedSceneSnapshot.reset();
    }

	newScene->GetRoot().add<Components::ActiveScene>();
    currentScene = newScene;
    //currentScene->SetDepthMap(m_depthMap);
    currentScene->Activate(m_managerInterface);
    currentScene->PropagateTransforms();
    BootstrapCommittedSceneSnapshot();
	m_warnedNullScene = false;
	m_warnedMissingPrimaryCamera = false;
	rebuildRenderGraph = true;
}

std::shared_ptr<Scene> Renderer::AppendScene(std::shared_ptr<Scene> scene) {
	if (!scene) {
        spdlog::warn("Renderer: attempted to append a null scene. Ignoring append request.");
        return nullptr;
    }

	if (m_sceneTaskInFlight.load()) {
        spdlog::warn("Renderer: attempted to append a scene while async scene overlap work is running. Ignoring append request for v1 overlap safety.");
        return nullptr;
    }

	if (!currentScene) {
        spdlog::warn("Renderer: attempted to append a scene while no current scene exists. Ignoring append request.");
        return nullptr;
    }

	return GetCurrentScene()->AppendScene(scene);
}

InputManager& Renderer::GetInputManager() {
    return inputManager;
}

void Renderer::SetInputMode(InputMode mode) {
    static WASDContext wasdContext;
    static OrbitalCameraContext orbitalContext;
    switch (mode) {
    case InputMode::wasd:
        inputManager.SetInputContext(&wasdContext);
        break;
    case InputMode::orbital:
        inputManager.SetInputContext(&orbitalContext);
        break;
    }
    SetupInputHandlers();
}

void Renderer::MoveForward() {
    spdlog::info("Moving forward!");
}

void Renderer::SetupInputHandlers() {
	auto& context = *inputManager.GetCurrentContext();
    context.SetActionHandler(InputAction::MoveForward, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving forward!");
        movementState.forwardMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveBackward, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving forward!");
        movementState.backwardMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveRight, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving right!");
        movementState.rightMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveLeft, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving right!");
        movementState.leftMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveUp, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving up!");
        movementState.upMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveDown, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving up!");
        movementState.downMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::RotateCamera, [this](float magnitude, const InputData& inputData) {
        horizontalAngle -= static_cast<float>(inputData.mouseDeltaX) * 0.005f;
        verticalAngle -= static_cast<float>(inputData.mouseDeltaY) * 0.005f;
        });

    context.SetActionHandler(InputAction::ZoomIn, [](float magnitude, const InputData& inputData) {
        // TODO
        });

    context.SetActionHandler(InputAction::ZoomOut, [](float magnitude, const InputData& inputData) {
        // TODO
        });

	context.SetActionHandler(InputAction::Reset, [](float magnitude, const InputData& inputData) {
        PSOManager::GetInstance().ReloadShaders();
		});

    context.SetActionHandler(InputAction::X, [](float magnitude, const InputData& inputData) {
        });

    context.SetActionHandler(InputAction::Z, [](float magnitude, const InputData& inputData) {
        });
}

void Renderer::CreateRenderGraph() {
    if (!IsSceneReadyForFrame()) {
        rebuildRenderGraph = true;
        return;
    }

    auto primaryCameraEntity = GetValidatedPrimaryRenderCamera(true);
    if (!primaryCameraEntity) {
        spdlog::warn("Renderer: primary camera bridge is not ready during render graph creation. Deferring rebuild.");
        rebuildRenderGraph = true;
        return;
    }

    StallPipeline();

    // TODO: Find a better way to handle resources like this
    // TODO: this access pattern is stupid
    auto primaryViewID = primaryCameraEntity.get<Components::RenderViewRef>().viewID;
    auto primaryCamera = m_pViewManager->Get(primaryViewID);

    // TODO: Primary camera and current environment will change, and I'd rather not recompile the graph every time that happens.
    // How should we manage swapping out their resources? DynamicResource could work, but the ResourceGroup/independantly managed resource
    // part of the compiler would need to become aware of DynamicResource.

    // TODO: Some of these resources don't really need to be recreated (GTAO, etc.)
    // Instead, just create them externally and register them

        if (!currentRenderGraph)
        {
		currentRenderGraph = std::make_unique<RenderGraph>(DeviceManager::GetInstance().GetDevice());
        if (auto* uploadService = currentRenderGraph->GetUploadService()) {
            uploadService->Initialize();
            rg::runtime::SetActiveUploadService(uploadService);
        }
        if (m_uploadPolicyService) {
            rg::runtime::SetActiveUploadPolicyService(m_uploadPolicyService.get());
        }
        if (auto* descriptorService = currentRenderGraph->GetDescriptorService()) {
            descriptorService->Initialize();
            rg::runtime::SetActiveDescriptorService(descriptorService);
        }
        }

        if (!m_renderGraphRuntimeInitialized) {
        currentRenderGraph->GetMemorySnapshotProvider().SetProvider(
            rg::memory::CreateECSMemorySnapshotProvider());
        Menu::GetInstance().SetRenderGraph(currentRenderGraph.get());

        RendererECSManager::GetInstance().CreateRenderPhaseEntity(Engine::Primary::CLodTransparentPass);

        currentRenderGraph->RegisterExtension(std::make_unique<RenderGraphIOExtension>(
            m_managerInterface.GetTextureFactory(),
            currentRenderGraph->GetUploadService(),
            m_pReadbackManager.get()));
        currentRenderGraph->RegisterExtension(std::make_unique<ReadbackCaptureExtension>(
            currentRenderGraph->GetReadbackService()));
        uint maxClusters = 100000; // TODO: make this configurable based on scene content   
        currentRenderGraph->RegisterExtension(
            std::make_unique<CLodExtension>(CLodExtensionType::VisiblityBuffer, static_cast<uint32_t>(maxClusters)),
            "CLodOpaque");
        constexpr bool kEnableAlphaBlendCLodVariant = true;
        if (kEnableAlphaBlendCLodVariant) {
            currentRenderGraph->RegisterExtension(
                std::make_unique<CLodExtension>(CLodExtensionType::AlphaBlend, static_cast<uint32_t>(maxClusters)),
                "CLodAlpha");
        }
		m_renderGraphRuntimeInitialized = true;
    }

    auto& newGraph = currentRenderGraph;

    newGraph->ResetForRebuild();
    DeletionManager::GetInstance().DrainAll();

    newGraph->RegisterProvider(m_pMeshManager.get());
    newGraph->RegisterProvider(m_pObjectManager.get());
    newGraph->RegisterProvider(m_pViewManager.get());
    newGraph->RegisterProvider(m_pLightManager.get());
    newGraph->RegisterProvider(m_pEnvironmentManager.get());
    newGraph->RegisterProvider(m_pIndirectCommandBufferManager.get());
	newGraph->RegisterProvider(m_pMaterialManager.get());
	newGraph->RegisterProvider(m_pSkeletonManager.get());
    newGraph->RegisterProvider(&m_coreResourceProvider);

    auto& depth = primaryCameraEntity.get<Components::DepthMap>();
    std::shared_ptr<PixelBuffer> depthTexture = depth.depthMap;

    newGraph->RegisterResource(Builtin::PrimaryCamera::DepthTexture, depthTexture);
    newGraph->RegisterResource(Builtin::PrimaryCamera::LinearDepthMap, depth.linearDepthMap);
    // In visibility rendering (CLod), projected depth is computed by the depth copy pass.
    // In standard rasterization, the hardware DSV already contains projected depth.
    if (m_visibilityRendering) {
        newGraph->RegisterResource(Builtin::PrimaryCamera::ProjectedDepthTexture, depth.projectedDepthMap);
    } else {
        newGraph->RegisterResource(Builtin::PrimaryCamera::ProjectedDepthTexture, depthTexture);
    }
    newGraph->RegisterResource(Builtin::Backbuffer, m_dynamicBackbuffer);

    bool useMeshShaders = getMeshShadersEnabled();
    if (!DeviceManager::GetInstance().GetMeshShadersSupported()) {
        useMeshShaders = false;
    }

    BuildBRDFIntegrationPass(newGraph.get());

    BuildEnvironmentPipeline(newGraph.get());

    // Skinning comes before Z prepass
    newGraph->BuildComputePass<SkinningPass>("SkinningPass");
    
    bool indirect = getIndirectDrawsEnabled();
    if (!useMeshShaders) { // Indirect draws only supported with mesh shaders
        indirect = false;
    }

    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    TextureDescription visibilityDesc;
    visibilityDesc.channels = 2;
    visibilityDesc.format = rhi::Format::R32G32_UInt;
    visibilityDesc.hasRTV = true;
    visibilityDesc.hasSRV = true;
    visibilityDesc.hasUAV = true; // For clearing
    visibilityDesc.hasNonShaderVisibleUAV = true; // For clearing with ClearUnorderedAccessViewUint
    visibilityDesc.imageDimensions.emplace_back(resolution.x, resolution.y, 0, 0);
	visibilityDesc.allowAlias = true;
    auto visibilityBuffer = PixelBuffer::CreateSharedUnmaterialized(visibilityDesc);
    visibilityBuffer->SetName("Visibility Buffer");
    rg::memory::SetResourceUsageHint(*visibilityBuffer, "GBuffer");
    newGraph->RegisterResource(Builtin::PrimaryCamera::VisibilityTexture, visibilityBuffer);

	m_pViewManager->AttachVisibilityBuffer(primaryViewID, visibilityBuffer);

    CreateGBufferResources(newGraph.get());

    CreateDebugVisualizationResources(newGraph.get());

    if (m_visibilityRendering) {
        newGraph->BuildRenderPass<ClearVisibilityBufferPass>("ClearVisibilityBufferPass");
    }

	// Either visibility or standard GBuffer pass
    BuildGBufferPipeline(newGraph.get());

    // GTAO pass
    if (m_gtaoEnabled) {
		RegisterGTAOResources(newGraph.get());
        BuildGTAOPipeline(newGraph.get(), primaryCameraEntity.try_get<Components::Camera>());
    }

	if (m_clusteredLighting) {  // TODO: active cluster determination using Z prepass
        BuildLightClusteringPipeline(newGraph.get());
    }

    auto& debugPassBuilder = newGraph->BuildRenderPass<DebugRenderPass>("DebugPass");

    auto drawShadows = getShadowsEnabled();
    if (drawShadows) {
        BuildMainShadowPass(newGraph.get());
        debugPassBuilder.WithShaderResource(Builtin::PrimaryCamera::LinearDepthMap);
    }

    // Linear depth downsample is scheduled by CLodExtension between phase-1 and phase-2.
	
    auto currentEnvironmentCubemap = m_defaultEnvironmentCubemap;
    auto currentEnvironmentPrefilteredCubemap = m_defaultEnvironmentPrefilteredCubemap;
    if (m_currentEnvironment != nullptr
        && m_currentEnvironment->GetEnvironmentCubemap() != nullptr
        && m_currentEnvironment->GetEnvironmentCubemap()->ImagePtr() != nullptr
        && m_currentEnvironment->GetEnvironmentPrefilteredCubemap() != nullptr) {
        currentEnvironmentCubemap = m_currentEnvironment->GetEnvironmentCubemap()->ImagePtr();
        currentEnvironmentPrefilteredCubemap = m_currentEnvironment->GetEnvironmentPrefilteredCubemap();
        m_warnedUsingFallbackEnvironment = false;
    }
    else if (!m_warnedUsingFallbackEnvironment) {
        spdlog::warn("Renderer: no valid environment is active. Using fallback blank cubemaps.");
        m_warnedUsingFallbackEnvironment = true;
    }

    newGraph->RegisterResource(Builtin::Environment::CurrentCubemap, currentEnvironmentCubemap);
    newGraph->RegisterResource(Builtin::Environment::CurrentPrefilteredCubemap, currentEnvironmentPrefilteredCubemap);

    if (m_currentEnvironment != nullptr) {
        newGraph->BuildComputePass<SkyboxRenderPass>("SkyboxPass");
    }

    BuildPrimaryPass(newGraph.get(), m_currentEnvironment.get());

    BuildPPLLPipeline(newGraph.get());

	// Start of post-processing passes

	if (m_screenSpaceReflections) {
        BuildSSRPasses(newGraph.get());
    }

	auto adaptedLuminanceBuffer = CreateIndexedStructuredBuffer(1, sizeof(float), true, false);
    adaptedLuminanceBuffer->SetName("Adapted Luminance");
    rg::memory::SetResourceUsageHint(*adaptedLuminanceBuffer, "Post-Processing resources");
	newGraph->RegisterResource(Builtin::PostProcessing::AdaptedLuminance, adaptedLuminanceBuffer);
	auto histogramBuffer = CreateIndexedStructuredBuffer(255, sizeof(uint32_t), true, false);
	histogramBuffer->SetName("Luminance Histogram Buffer");
    rg::memory::SetResourceUsageHint(*histogramBuffer, "Post-Processing resources");
	newGraph->RegisterResource(Builtin::PostProcessing::LuminanceHistogram, histogramBuffer);

        newGraph->BuildComputePass<LuminanceHistogramPass>("luminanceHistogramPass");
        newGraph->BuildComputePass<LuminanceHistogramAveragePass>("LuminanceAveragePass");

        newGraph->BuildRenderPass<UpscalingPass>("UpscalingPass");

    if (m_bloom) {
        BuildBloomPipeline(newGraph.get());
    }

    DebugGridPass::Params params;
    params.planeY = 0.0f;
    params.minorCellSize = 1.0;
    params.majorCellSize = 10;
    params.axisHalfWidthWorld = 0.5f * 0.04f * params.minorCellSize;
    params.minorLineWidth = 0.01f;
    params.majorLineWidth = 0.02f;
    params.minorOpacity = 0.3f;
	params.majorOpacity = 0.55f;
	params.axisOpacity = 0.85f;
    params.overallOpacity = 1.0f;

	newGraph->BuildComputePass<DebugGridPass>("DebugGridPass", params);

    newGraph->BuildRenderPass<TonemappingPass>("TonemappingPass");

    newGraph->BuildRenderPass<DebugResolvePass>("DebugResolvePass");

    newGraph->BuildRenderPass<MenuRenderPass>("MenuRenderPass");
	if (m_coreResourceProvider.m_currentDebugTexture != nullptr) {
		auto debugRenderPass = newGraph->GetRenderPassByName("DebugPass");
		std::shared_ptr<DebugRenderPass> debugPass = std::dynamic_pointer_cast<DebugRenderPass>(debugRenderPass);
        if (debugPass) {
            debugPass->SetTexture(m_coreResourceProvider.m_currentDebugTexture.get());
        }
	}

    if (getDrawBoundingSpheres()) {
        newGraph->BuildRenderPass<DebugSpherePass>("DebugSpherePass");
    }

	BuildLinearDepthHistoryCopyPass(newGraph.get());

    newGraph->SetMinimumAutomaticSchedulingQueues(QueueKind::Compute, 3);

    newGraph->CompileStructural();
    newGraph->Setup();

	rebuildRenderGraph = false;
}

void Renderer::SetEnvironmentInternal(std::wstring name) {

    std::filesystem::path envpath = std::filesystem::path(GetExePath()) / L"textures" / L"environment" / (name+L".hdr");

    if (std::filesystem::exists(envpath)) {
		m_warnedUsingFallbackEnvironment = false;
		m_preFrameDeferredFunctions.defer([envpath, name, this]() { // Don't change this during rendering
            m_currentEnvironment = m_pEnvironmentManager->CreateEnvironment(name);
            m_pEnvironmentManager->SetFromHDRI(m_currentEnvironment.get(), envpath.string());
			ResourceManager::GetInstance().SetActiveEnvironmentIndex(m_currentEnvironment->GetEnvironmentIndex());
			});
    }
    else {
        m_currentEnvironment.reset();
        ResourceManager::GetInstance().SetActiveEnvironmentIndex(0);
        rebuildRenderGraph = true;
        if (!m_warnedUsingFallbackEnvironment) {
            spdlog::warn("Environment file not found: {}. Falling back to blank environment resources.", envpath.string());
            m_warnedUsingFallbackEnvironment = true;
        }
    }
}

void Renderer::SetDebugTexture(std::shared_ptr<PixelBuffer> texture) {
    m_coreResourceProvider.m_currentDebugTexture = texture;
	if (currentRenderGraph == nullptr) {
		return;
	}
    auto pPass = currentRenderGraph->GetRenderPassByName("DebugPass");
    if (pPass != nullptr) {
        auto pDebugPass = std::dynamic_pointer_cast<DebugRenderPass>(pPass);
        pDebugPass->SetTexture(texture.get());
    }
    else {
        spdlog::warn("Debug pass does not exist");
    }
}
