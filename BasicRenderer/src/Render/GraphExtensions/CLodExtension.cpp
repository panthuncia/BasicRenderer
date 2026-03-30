#include "Render/GraphExtensions/CLodExtension.h"

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Managers/MeshManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/GraphExtensions/ClusterLOD/ClearDeepVisibilityPass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingSystem.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/DeepVisibilityResolvePass.h"
#include "Render/GraphExtensions/ClusterLOD/HierarchialCullingPass.h"
#include "Render/GraphExtensions/ClusterLOD/PerViewLinearDepthCopyPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockOffsetsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockScanPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCompactAndArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCreateCommandPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketHistogramPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesClassifyPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesBuildRasterWorkPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesCreateDispatchArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesDicePass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesPatchRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesQueueResetPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesSeedPatchesPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesSplitPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationTable.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationTableUploadPass.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "Render/RenderPhase.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "Resources/Buffers/PagePool.h"
#include "Resources/components.h"
#include "ShaderBuffers.h"

namespace {

flecs::entity EnsureVisibilityBufferTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionVisibilityBufferTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionVisibilityBufferTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionVisibilityBufferTag>();
    }
    return ecsWorld.entity<CLodExtensionVisibilityBufferTag>();
}

flecs::entity EnsureAlphaBlendTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionAlphaBlendTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionAlphaBlendTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionAlphaBlendTag>();
    }
    return ecsWorld.entity<CLodExtensionAlphaBlendTag>();
}

flecs::entity EnsureShadowTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionShadowTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionShadowTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionShadowTag>();
    }
    return ecsWorld.entity<CLodExtensionShadowTag>();
}

struct CLodVariantTraits {
    enum class ScheduleMode : uint8_t {
        TwoPassVisibility,
        SinglePassCullOnly,
        SinglePassDeepVisibility
    };

    CLodExtensionType type;
    flecs::entity (*ensureTypeEntity)(flecs::world&);
    std::string_view passPrefix;
    std::string_view resourcePrefix;
    std::string_view renderPhaseName;
    CLodRasterOutputKind rasterOutputKind;
    ScheduleMode scheduleMode = ScheduleMode::TwoPassVisibility;
    bool ownsStreaming = false;
    bool usesPhase2OcclusionReplay = false;
    bool schedulesPerViewDepthCopy = false;
    bool schedulesStructuralPasses = false;
};

const CLodVariantTraits& GetVariantTraits(CLodExtensionType type)
{
    static const std::array<CLodVariantTraits, 3> kTraits = {{
        {
            CLodExtensionType::VisiblityBuffer,
            &EnsureVisibilityBufferTag,
            "CLodOpaque::",
            "CLod[Opaque] ",
            Engine::Primary::GBufferPass,
            CLodRasterOutputKind::VisibilityBuffer,
            CLodVariantTraits::ScheduleMode::TwoPassVisibility,
            true,
            true,
            true,
            true
        },
        {
            CLodExtensionType::AlphaBlend,
            &EnsureAlphaBlendTag,
            "CLodAlpha::",
            "CLod[Alpha] ",
            Engine::Primary::CLodTransparentPass,
            CLodRasterOutputKind::DeepVisibility,
            CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility,
            false,
            false,
            false,
            true
        },
        {
            CLodExtensionType::Shadow,
            &EnsureShadowTag,
            "CLodShadow::",
            "CLod[Shadow] ",
            Engine::Primary::GBufferPass,
            CLodRasterOutputKind::VisibilityBuffer,
            CLodVariantTraits::ScheduleMode::TwoPassVisibility,
            false,
            true,
            false,
            true
        },
    }};

    for (const auto& traits : kTraits) {
        if (traits.type == type) {
            return traits;
        }
    }

    throw std::runtime_error("Unknown CLod extension type.");
}

std::string MakeVariantPassName(const CLodVariantTraits& traits, std::string_view suffix)
{
    return std::string(traits.passPrefix) + std::string(suffix);
}

std::string MakeVariantResourceName(const CLodVariantTraits& traits, std::string_view suffix)
{
    return std::string(traits.resourcePrefix) + std::string(suffix);
}

HierarchialCullingWorkGraphMode GetCullingWorkGraphMode(CLodSoftwareRasterMode softwareRasterMode)
{
    switch (softwareRasterMode) {
    case CLodSoftwareRasterMode::Disabled:
        return HierarchialCullingWorkGraphMode::HardwareOnly;
    case CLodSoftwareRasterMode::Compute:
        return HierarchialCullingWorkGraphMode::SoftwareRasterCompute;
    case CLodSoftwareRasterMode::WorkGraph:
        return HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph;
    }

    return HierarchialCullingWorkGraphMode::HardwareOnly;
}

std::shared_ptr<ResourceGroup> GetSlabResourceGroup()
{
    try {
        auto getter = SettingsManager::GetInstance().getSettingGetter<std::function<MeshManager*()>>(CLodStreamingMeshManagerGetterSettingName);
        if (auto* meshManager = getter()()) {
            if (auto* pool = meshManager->GetCLodPagePool()) {
                return pool->GetSlabResourceGroup();
            }
        }
    } catch (...) {}

    return nullptr;
}

template<typename... Tags>
void SyncTaggedBufferEntity(const std::shared_ptr<Buffer>& buffer, const flecs::entity& typeEntity, bool enabled)
{
    if (!buffer) {
        return;
    }

    auto entity = buffer->GetECSEntity();
    if (enabled) {
        entity.set<Components::Resource>({ buffer });
        entity.add<CLodExtensionTypeTag>(typeEntity);
        (entity.add<Tags>(), ...);
    }
    else if (entity.has<Components::Resource>()) {
        entity.remove<Components::Resource>();
    }
}

} // namespace

CLodExtension::~CLodExtension() = default;

bool CLodExtension::IsReyesTessellationDisabled() const
{
    return SettingsManager::GetInstance().getSettingGetter<bool>(CLodDisableReyesRasterizationSettingName)();
}

void CLodExtension::SyncReyesResourceEntities(bool enabled)
{
    if (!m_reyesFullClusterOutputsBuffer) {
        return;
    }

    const auto& traits = GetVariantTraits(m_type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    SyncTaggedBufferEntity<CLodReyesFullClustersCounterTag>(m_reyesFullClusterOutputsCounterBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueCounterATag>(m_reyesSplitQueueCounterBufferA, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueOverflowATag>(m_reyesSplitQueueOverflowBufferA, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueCounterBTag>(m_reyesSplitQueueCounterBufferB, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueOverflowBTag>(m_reyesSplitQueueOverflowBufferB, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesDiceQueueTag>(m_reyesDiceQueueBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTessTableConfigsTag>(m_reyesTessTableConfigsBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTessTableVerticesTag>(m_reyesTessTableVerticesBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTessTableTrianglesTag>(m_reyesTessTableTrianglesBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesDiceQueueCounterTag>(m_reyesDiceQueueCounterBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesDiceQueueOverflowTag>(m_reyesDiceQueueOverflowBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTelemetryBufferPhase1Tag>(m_reyesTelemetryBufferPhase1, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTelemetryBufferPhase2Tag>(m_reyesTelemetryBufferPhase2, typeEntity, enabled);
}

void CLodExtension::EnsureReyesResourcesInitialized()
{
    if (m_reyesFullClusterOutputsBuffer) {
        SyncReyesResourceEntities(true);
        return;
    }

    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };

    const auto& traits = GetVariantTraits(m_type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);
    const uint32_t reyesOwnershipWordCount = CLodBitsetWordCount(m_maxVisibleClusters);
    const uint32_t reyesSplitQueueCapacity = CLodReyesSplitQueueCapacity(m_maxVisibleClusters);
    const uint32_t reyesDiceQueueCapacity = CLodReyesDiceQueueCapacity(m_maxVisibleClusters);
    const uint32_t reyesRasterWorkCapacity = CLodReyesRasterWorkCapacity(m_maxVisibleClusters);

    m_reyesFullClusterOutputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_maxVisibleClusters, sizeof(CLodReyesFullClusterOutput), true, false);
    m_reyesFullClusterOutputsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Full Cluster Outputs Buffer"));

    m_reyesFullClusterOutputsCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesFullClusterOutputsCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Full Cluster Outputs Counter Buffer"));
    m_reyesFullClusterOutputsCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesFullClusterOutputsCounterBuffer })
        .add<CLodReyesFullClustersCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesOwnedClustersBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_maxVisibleClusters, sizeof(CLodReyesOwnedClusterEntry), true, false);
    m_reyesOwnedClustersBuffer->SetName(MakeVariantResourceName(traits, "Reyes Owned Clusters Buffer"));

    m_reyesOwnedClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesOwnedClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Owned Clusters Counter Buffer"));

    m_reyesOwnershipBitsetBuffer = CreateAliasedUnmaterializedStructuredBuffer(reyesOwnershipWordCount, sizeof(uint32_t), true, false, false, false);
    m_reyesOwnershipBitsetBuffer->SetName(MakeVariantResourceName(traits, "Reyes Ownership Bitset Buffer"));

    m_reyesOwnershipBitsetBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(reyesOwnershipWordCount, sizeof(uint32_t), true, false, false, false);
    m_reyesOwnershipBitsetBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Ownership Bitset Buffer Phase2"));

    m_reyesClassifyIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesClassifyIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Classify Indirect Args Buffer"));

    m_reyesClassifyIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesClassifyIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Classify Indirect Args Buffer Phase2"));

    m_reyesSplitIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesSplitIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Split Indirect Args Buffer"));

    m_reyesSplitIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesSplitIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Split Indirect Args Buffer Phase2"));

    m_reyesSplitQueueBufferA = CreateAliasedUnmaterializedStructuredBuffer(reyesSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry), true, false);
    m_reyesSplitQueueBufferA->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Buffer A"));

    m_reyesSplitQueueCounterBufferA = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesSplitQueueCounterBufferA->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Counter Buffer A"));
    m_reyesSplitQueueCounterBufferA->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueCounterBufferA })
        .add<CLodReyesSplitQueueCounterATag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesSplitQueueOverflowBufferA = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesSplitQueueOverflowBufferA->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Overflow Buffer A"));
    m_reyesSplitQueueOverflowBufferA->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueOverflowBufferA })
        .add<CLodReyesSplitQueueOverflowATag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesSplitQueueBufferB = CreateAliasedUnmaterializedStructuredBuffer(reyesSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry), true, false);
    m_reyesSplitQueueBufferB->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Buffer B"));

    m_reyesSplitQueueCounterBufferB = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesSplitQueueCounterBufferB->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Counter Buffer B"));
    m_reyesSplitQueueCounterBufferB->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueCounterBufferB })
        .add<CLodReyesSplitQueueCounterBTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesSplitQueueOverflowBufferB = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesSplitQueueOverflowBufferB->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Overflow Buffer B"));
    m_reyesSplitQueueOverflowBufferB->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueOverflowBufferB })
        .add<CLodReyesSplitQueueOverflowBTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesDiceQueueBuffer = CreateAliasedUnmaterializedStructuredBuffer(reyesDiceQueueCapacity, sizeof(CLodReyesDiceQueueEntry), true, false);
    m_reyesDiceQueueBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Buffer"));
    m_reyesDiceQueueBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesDiceQueueBuffer })
        .add<CLodReyesDiceQueueTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    const ReyesTessellationTableData& reyesTessellationTableData = GetReyesTessellationTableData();
    m_reyesTessTableConfigsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        static_cast<uint32_t>(reyesTessellationTableData.configs.size()),
        sizeof(CLodReyesTessTableConfigEntry),
        false,
        false,
        false,
        false);
    m_reyesTessTableConfigsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Tess Table Configs Buffer"));
    m_reyesTessTableConfigsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesTessTableConfigsBuffer })
        .add<CLodReyesTessTableConfigsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesTessTableVerticesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        static_cast<uint32_t>(reyesTessellationTableData.vertices.size()),
        sizeof(uint32_t),
        false,
        false,
        false,
        false);
    m_reyesTessTableVerticesBuffer->SetName(MakeVariantResourceName(traits, "Reyes Tess Table Vertices Buffer"));
    m_reyesTessTableVerticesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesTessTableVerticesBuffer })
        .add<CLodReyesTessTableVerticesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesTessTableTrianglesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        static_cast<uint32_t>(reyesTessellationTableData.triangles.size()),
        sizeof(uint32_t),
        false,
        false,
        false,
        false);
    m_reyesTessTableTrianglesBuffer->SetName(MakeVariantResourceName(traits, "Reyes Tess Table Triangles Buffer"));
    m_reyesTessTableTrianglesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesTessTableTrianglesBuffer })
        .add<CLodReyesTessTableTrianglesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesDiceQueueCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesDiceQueueCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Counter Buffer"));
    m_reyesDiceQueueCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesDiceQueueCounterBuffer })
        .add<CLodReyesDiceQueueCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesDiceQueueOverflowBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesDiceQueueOverflowBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Overflow Buffer"));
    m_reyesDiceQueueOverflowBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesDiceQueueOverflowBuffer })
        .add<CLodReyesDiceQueueOverflowTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesRasterWorkBuffer = CreateAliasedUnmaterializedStructuredBuffer(reyesRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry), true, false, false, false);
    m_reyesRasterWorkBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Buffer"));

    m_reyesRasterWorkCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesRasterWorkCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Counter Buffer"));

    m_reyesRasterWorkIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesRasterWorkIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Indirect Args Buffer"));

    m_reyesRasterWorkBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(reyesRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry), true, false, false, false);
    m_reyesRasterWorkBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Buffer Phase2"));

    m_reyesRasterWorkCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesRasterWorkCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Counter Buffer Phase2"));

    m_reyesRasterWorkIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesRasterWorkIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Indirect Args Buffer Phase2"));

    m_reyesDiceIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesDiceIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Indirect Args Buffer"));

    m_reyesDiceIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesDiceIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Dice Indirect Args Buffer Phase2"));

    m_reyesTelemetryBufferPhase1 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesTelemetry), true, false, false, false);
    m_reyesTelemetryBufferPhase1->SetName(MakeVariantResourceName(traits, "Reyes Telemetry Buffer Phase1"));
    m_reyesTelemetryBufferPhase1->GetECSEntity()
        .set<Components::Resource>({ m_reyesTelemetryBufferPhase1 })
        .add<CLodReyesTelemetryBufferPhase1Tag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesTelemetryBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesTelemetry), true, false, false, false);
    m_reyesTelemetryBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Telemetry Buffer Phase2"));
    m_reyesTelemetryBufferPhase2->GetECSEntity()
        .set<Components::Resource>({ m_reyesTelemetryBufferPhase2 })
        .add<CLodReyesTelemetryBufferPhase2Tag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    tagBufferUsage(m_reyesFullClusterOutputsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesFullClusterOutputsCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnedClustersBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnedClustersCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnershipBitsetBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnershipBitsetBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesClassifyIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesClassifyIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueBufferA, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueCounterBufferA, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueOverflowBufferA, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueBufferB, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueCounterBufferB, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueOverflowBufferB, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceQueueBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTessTableConfigsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTessTableVerticesBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTessTableTrianglesBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceQueueCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceQueueOverflowBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkCounterBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTelemetryBufferPhase1, "Cluster LOD telemetry");
    tagBufferUsage(m_reyesTelemetryBufferPhase2, "Cluster LOD telemetry");
}

CLodExtension::CLodExtension(CLodExtensionType type, uint32_t maxVisibleClusters)
    : m_type(type)
    , m_maxVisibleClusters(maxVisibleClusters) {
    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };

    const auto& traits = GetVariantTraits(type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    if (traits.ownsStreaming) {
        m_streamingSystem = std::make_unique<CLodStreamingSystem>();
    }

    m_visibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
    m_visibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Visible Clusters Buffer (uncompacted)"));

    m_histogramIndirectCommand = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterBucketsHistogramIndirectCommand), true, false);
    m_histogramIndirectCommand->SetName(MakeVariantResourceName(traits, "Raster Buckets Histogram Indirect Command Buffer"));

    m_rasterBucketsHistogramBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket histogram"));

    m_visibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "Visible Clusters Counter Buffer"));
    m_visibleClustersCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersCounterBuffer })
        .add<VisibleClustersCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_workGraphTelemetryBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodWorkGraphCounterCount, sizeof(uint32_t), true, false, false, false);
    m_workGraphTelemetryBuffer->SetName(MakeVariantResourceName(traits, "Work Graph Telemetry Buffer"));
    m_workGraphTelemetryBuffer->GetECSEntity()
        .set<Components::Resource>({ m_workGraphTelemetryBuffer })
        .add<CLodWorkGraphTelemetryBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_occlusionReplayBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodReplayBufferNumUints, sizeof(uint32_t), true, false, false, false);
    m_occlusionReplayBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Replay Buffer"));

    m_occlusionReplayStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReplayBufferState), true, false, false, false);
    m_occlusionReplayStateBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Replay State Buffer"));

    m_occlusionNodeGpuInputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(3, sizeof(CLodNodeGpuInput), true, false, false, false);
    m_occlusionNodeGpuInputsBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Node GPU Inputs Buffer"));

    m_viewDepthSrvIndicesBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodMaxViewDepthIndices, sizeof(CLodViewDepthSRVIndex), true, false, false, false);
    m_viewDepthSrvIndicesBuffer->SetName(MakeVariantResourceName(traits, "View Depth SRV Indices Buffer"));

    m_rasterBucketsOffsetsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsOffsetsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket offsets"));

    m_rasterBucketsBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsBlockSumsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket block sums"));

    m_rasterBucketsScannedBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsScannedBlockSumsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket scanned block sums"));

    m_rasterBucketsTotalCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsTotalCountBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket total count"));

    m_rasterBucketsTotalCountBufferPhase1 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsTotalCountBufferPhase1->SetName(MakeVariantResourceName(traits, "Raster bucket total count phase1"));

    m_rasterBucketsTotalCountBufferPhase1Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsTotalCountBufferPhase1Sw->SetName(MakeVariantResourceName(traits, "Raster bucket total count phase1 SW"));

    m_compactedVisibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
    m_compactedVisibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Compacted Visible Clusters Buffer"));

    m_visibleClustersBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersBuffer })
        .set<CLodVisibleClusterCapacity>({ maxVisibleClusters })
        .add<VisibleClustersBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_rasterBucketsWriteCursorBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor"));

    m_rasterBucketsIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false);
    m_rasterBucketsIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args"));

    if (!IsReyesTessellationDisabled()) {
        EnsureReyesResourcesInitialized();
    }

    m_visibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "Visible Clusters Counter Buffer Phase2"));

    m_rasterBucketsHistogramBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket histogram phase2"));

    m_rasterBucketsHistogramBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferSw->SetName(MakeVariantResourceName(traits, "Raster bucket histogram SW phase1"));

    m_rasterBucketsHistogramBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferPhase2Sw->SetName(MakeVariantResourceName(traits, "Raster bucket histogram SW phase2"));

    m_rasterBucketsWriteCursorBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor phase2"));

    m_rasterBucketsWriteCursorBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferSw->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor SW phase1"));

    m_rasterBucketsWriteCursorBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferPhase2Sw->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor SW phase2"));

    m_swVisibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "SW Visible Clusters Counter Buffer Phase1"));

    m_swVisibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "SW Visible Clusters Counter Buffer Phase2"));

    m_sortedToUnsortedMappingBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(maxVisibleClusters), sizeof(uint32_t), true, false);
    m_sortedToUnsortedMappingBuffer->SetName(MakeVariantResourceName(traits, "Sorted-to-Unsorted Mapping Buffer"));

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName(MakeVariantResourceName(traits, "View Raster Info Buffer"));

    tagBufferUsage(m_visibleClustersBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_histogramIndirectCommand, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsHistogramBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_visibleClustersCounterBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_workGraphTelemetryBuffer, "Cluster LOD telemetry");
    tagBufferUsage(m_occlusionReplayBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_occlusionReplayStateBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_occlusionNodeGpuInputsBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_viewDepthSrvIndicesBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_rasterBucketsOffsetsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsBlockSumsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsScannedBlockSumsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsTotalCountBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsTotalCountBufferPhase1, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsTotalCountBufferPhase1Sw, "Cluster LOD rasterization");
    tagBufferUsage(m_compactedVisibleClustersBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_rasterBucketsWriteCursorBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsIndirectArgsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_visibleClustersCounterBufferPhase2, "Cluster LOD visibility");
    tagBufferUsage(m_rasterBucketsHistogramBufferPhase2, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsHistogramBufferSw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsHistogramBufferPhase2Sw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsWriteCursorBufferPhase2, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsWriteCursorBufferSw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsWriteCursorBufferPhase2Sw, "Cluster LOD rasterization");
    tagBufferUsage(m_swVisibleClustersCounterBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_swVisibleClustersCounterBufferPhase2, "Cluster LOD visibility");
    tagBufferUsage(m_sortedToUnsortedMappingBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_viewRasterInfoBuffer, "Cluster LOD rasterization");

    if (traits.rasterOutputKind == CLodRasterOutputKind::DeepVisibility) {
        m_deepVisibilityNodesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
            1,
            sizeof(CLodDeepVisibilityNode),
            true,
            false,
            false,
            false);
        m_deepVisibilityNodesBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Nodes Buffer"));

        m_deepVisibilityCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
        m_deepVisibilityCounterBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Counter Buffer"));
        m_deepVisibilityCounterBuffer->GetECSEntity()
            .set<Components::Resource>({ m_deepVisibilityCounterBuffer })
            .add<CLodDeepVisibilityCounterTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_deepVisibilityOverflowCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
        m_deepVisibilityOverflowCounterBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Overflow Counter Buffer"));
        m_deepVisibilityOverflowCounterBuffer->GetECSEntity()
            .set<Components::Resource>({ m_deepVisibilityOverflowCounterBuffer })
            .add<CLodDeepVisibilityOverflowCounterTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_deepVisibilityStatsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
            1,
            sizeof(CLodDeepVisibilityStats),
            true,
            false,
            true,
            false);
        m_deepVisibilityStatsBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Stats Buffer"));
        m_deepVisibilityStatsBuffer->GetECSEntity()
            .set<Components::Resource>({ m_deepVisibilityStatsBuffer })
            .add<CLodDeepVisibilityStatsTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        tagBufferUsage(m_deepVisibilityNodesBuffer, "Cluster LOD deep visibility");
        tagBufferUsage(m_deepVisibilityCounterBuffer, "Cluster LOD deep visibility");
        tagBufferUsage(m_deepVisibilityOverflowCounterBuffer, "Cluster LOD deep visibility");
        tagBufferUsage(m_deepVisibilityStatsBuffer, "Cluster LOD deep visibility");
    }
}

void CLodExtension::Initialize(RenderGraph& rg)
{
    if (m_streamingSystem) {
        m_streamingSystem->Initialize(rg);
    }
}

void CLodExtension::OnRegistryReset(ResourceRegistry* reg)
{
    auto releaseBufferBacking = [](const std::shared_ptr<Buffer>& buffer) {
        if (buffer) {
            buffer->Dematerialize();
        }
    };

    releaseBufferBacking(m_visibleClustersBuffer);
    releaseBufferBacking(m_visibleClustersCounterBuffer);
    releaseBufferBacking(m_workGraphTelemetryBuffer);
    releaseBufferBacking(m_occlusionReplayBuffer);
    releaseBufferBacking(m_occlusionReplayStateBuffer);
    releaseBufferBacking(m_occlusionNodeGpuInputsBuffer);
    releaseBufferBacking(m_viewDepthSrvIndicesBuffer);
    releaseBufferBacking(m_histogramIndirectCommand);
    releaseBufferBacking(m_rasterBucketsHistogramBuffer);
    releaseBufferBacking(m_rasterBucketsOffsetsBuffer);
    releaseBufferBacking(m_rasterBucketsBlockSumsBuffer);
    releaseBufferBacking(m_rasterBucketsScannedBlockSumsBuffer);
    releaseBufferBacking(m_rasterBucketsTotalCountBuffer);
    releaseBufferBacking(m_rasterBucketsTotalCountBufferPhase1);
    releaseBufferBacking(m_rasterBucketsTotalCountBufferPhase1Sw);
    releaseBufferBacking(m_visibleClustersCounterBufferPhase2);
    releaseBufferBacking(m_rasterBucketsHistogramBufferPhase2);
    releaseBufferBacking(m_rasterBucketsWriteCursorBufferPhase2);
    releaseBufferBacking(m_rasterBucketsHistogramBufferSw);
    releaseBufferBacking(m_rasterBucketsHistogramBufferPhase2Sw);
    releaseBufferBacking(m_rasterBucketsWriteCursorBufferSw);
    releaseBufferBacking(m_rasterBucketsWriteCursorBufferPhase2Sw);
    releaseBufferBacking(m_compactedVisibleClustersBuffer);
    releaseBufferBacking(m_rasterBucketsWriteCursorBuffer);
    releaseBufferBacking(m_rasterBucketsIndirectArgsBuffer);
    releaseBufferBacking(m_reyesFullClusterOutputsBuffer);
    releaseBufferBacking(m_reyesFullClusterOutputsCounterBuffer);
    releaseBufferBacking(m_reyesOwnedClustersBuffer);
    releaseBufferBacking(m_reyesOwnedClustersCounterBuffer);
    releaseBufferBacking(m_reyesOwnershipBitsetBuffer);
    releaseBufferBacking(m_reyesOwnershipBitsetBufferPhase2);
    releaseBufferBacking(m_reyesClassifyIndirectArgsBuffer);
    releaseBufferBacking(m_reyesClassifyIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesSplitIndirectArgsBuffer);
    releaseBufferBacking(m_reyesSplitIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesSplitQueueBufferA);
    releaseBufferBacking(m_reyesSplitQueueCounterBufferA);
    releaseBufferBacking(m_reyesSplitQueueOverflowBufferA);
    releaseBufferBacking(m_reyesSplitQueueBufferB);
    releaseBufferBacking(m_reyesSplitQueueCounterBufferB);
    releaseBufferBacking(m_reyesSplitQueueOverflowBufferB);
    releaseBufferBacking(m_reyesDiceQueueBuffer);
    releaseBufferBacking(m_reyesDiceQueueCounterBuffer);
    releaseBufferBacking(m_reyesDiceQueueOverflowBuffer);
    releaseBufferBacking(m_reyesRasterWorkBuffer);
    releaseBufferBacking(m_reyesRasterWorkCounterBuffer);
    releaseBufferBacking(m_reyesRasterWorkIndirectArgsBuffer);
    releaseBufferBacking(m_reyesTessTableConfigsBuffer);
    releaseBufferBacking(m_reyesTessTableVerticesBuffer);
    releaseBufferBacking(m_reyesTessTableTrianglesBuffer);
    releaseBufferBacking(m_reyesDiceIndirectArgsBuffer);
    releaseBufferBacking(m_reyesDiceIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesRasterWorkBufferPhase2);
    releaseBufferBacking(m_reyesRasterWorkCounterBufferPhase2);
    releaseBufferBacking(m_reyesRasterWorkIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesTelemetryBufferPhase1);
    releaseBufferBacking(m_reyesTelemetryBufferPhase2);
    releaseBufferBacking(m_swVisibleClustersCounterBuffer);
    releaseBufferBacking(m_swVisibleClustersCounterBufferPhase2);
    releaseBufferBacking(m_sortedToUnsortedMappingBuffer);
    releaseBufferBacking(m_viewRasterInfoBuffer);
    releaseBufferBacking(m_deepVisibilityNodesBuffer);
    releaseBufferBacking(m_deepVisibilityCounterBuffer);
    releaseBufferBacking(m_deepVisibilityOverflowCounterBuffer);
    releaseBufferBacking(m_deepVisibilityStatsBuffer);

    SyncReyesResourceEntities(!IsReyesTessellationDisabled());

    if (m_streamingSystem) {
        m_streamingSystem->OnRegistryReset(reg);
    }
}

void CLodExtension::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    const auto& traits = GetVariantTraits(m_type);
    if (m_streamingSystem) {
        m_streamingSystem->GatherStructuralPasses(rg, outPasses);
    }

    if (!traits.schedulesStructuralPasses) {
        return;
    }

    const auto softwareRasterMode =
        SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)();
    const bool disableReyesTessellation =
        SettingsManager::GetInstance().getSettingGetter<bool>(CLodDisableReyesRasterizationSettingName)();
    if (!disableReyesTessellation) {
        EnsureReyesResourcesInitialized();
    }
    SyncReyesResourceEntities(!disableReyesTessellation);
    const bool forceHardwareOnly =
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly ||
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility;
    const bool useComputeSWRaster = !forceHardwareOnly && CLodSoftwareRasterUsesCompute(softwareRasterMode);
    const uint32_t reyesSplitQueueCapacity = CLodReyesSplitQueueCapacity(m_maxVisibleClusters);
    const uint32_t reyesDiceQueueCapacity = CLodReyesDiceQueueCapacity(m_maxVisibleClusters);
    const uint32_t reyesRasterWorkCapacity = CLodReyesRasterWorkCapacity(m_maxVisibleClusters);
    const auto workGraphMode = forceHardwareOnly
        ? HierarchialCullingWorkGraphMode::HardwareOnly
        : GetCullingWorkGraphMode(softwareRasterMode);
    const auto renderPhase = RenderPhase(traits.renderPhaseName.data());
    const std::shared_ptr<Buffer> reyesOwnershipBitsetBuffer = disableReyesTessellation ? nullptr : m_reyesOwnershipBitsetBuffer;
    const std::shared_ptr<Buffer> reyesOwnershipBitsetBufferPhase2 = disableReyesTessellation ? nullptr : m_reyesOwnershipBitsetBufferPhase2;

    std::shared_ptr<ResourceGroup> slabGroup = GetSlabResourceGroup();

    RenderGraph::ExternalPassDesc cullPassDesc;
    cullPassDesc.type = RenderGraph::PassType::Compute;
    //cullPassDesc.computeQueueSelection = ComputeQueueSelection::Graphics;
    cullPassDesc.name = MakeVariantPassName(traits, "HierarchialCullingPass1");
    HierarchialCullingPassInputs cullPassInputs;
    cullPassInputs.isFirstPass = true;
    cullPassInputs.maxVisibleClusters = m_maxVisibleClusters;
    cullPassInputs.workGraphMode = workGraphMode;
    cullPassInputs.renderPhase = renderPhase;
    cullPassInputs.clodOnlyWorkloads = true;
    cullPassInputs.rasterOutputKind = traits.rasterOutputKind;
    cullPassDesc.pass = std::make_shared<HierarchialCullingPass>(
        cullPassInputs,
        m_visibleClustersBuffer,
        m_visibleClustersCounterBuffer,
        m_swVisibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_workGraphTelemetryBuffer,
        m_occlusionReplayBuffer,
        m_occlusionReplayStateBuffer,
        m_occlusionNodeGpuInputsBuffer,
        m_viewDepthSrvIndicesBuffer,
        m_viewRasterInfoBuffer,
        slabGroup);
    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly ||
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("CLodOpaque::LinearDepthDownsamplePass2");
    }
    else {
        cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass");
    }
    outPasses.push_back(std::move(cullPassDesc));

        if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::TwoPassVisibility && !disableReyesTessellation) {
            RenderGraph::ExternalPassDesc reyesTessellationTableUploadPassDesc;
            reyesTessellationTableUploadPassDesc.type = RenderGraph::PassType::Compute;
            reyesTessellationTableUploadPassDesc.name = MakeVariantPassName(traits, "ReyesTessellationTableUploadPass");
            reyesTessellationTableUploadPassDesc.pass = std::make_shared<ReyesTessellationTableUploadPass>(
                m_reyesTessTableConfigsBuffer,
                m_reyesTessTableVerticesBuffer,
                m_reyesTessTableTrianglesBuffer);
            outPasses.push_back(std::move(reyesTessellationTableUploadPassDesc));

            RenderGraph::ExternalPassDesc reyesResetPassDesc;
            reyesResetPassDesc.type = RenderGraph::PassType::Compute;
            reyesResetPassDesc.name = MakeVariantPassName(traits, "ReyesQueueResetPass1");
            reyesResetPassDesc.pass = std::make_shared<ReyesQueueResetPass>(
                m_reyesFullClusterOutputsCounterBuffer,
                m_reyesOwnedClustersCounterBuffer,
                std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
                std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
                m_reyesDiceQueueCounterBuffer,
                m_reyesDiceQueueOverflowBuffer,
                reyesOwnershipBitsetBuffer,
                m_reyesTelemetryBufferPhase1,
                1u);
            outPasses.push_back(std::move(reyesResetPassDesc));

            RenderGraph::ExternalPassDesc reyesCreateClassifyArgsPassDesc;
            reyesCreateClassifyArgsPassDesc.type = RenderGraph::PassType::Compute;
            reyesCreateClassifyArgsPassDesc.name = MakeVariantPassName(traits, "ReyesCreateClassifyDispatchArgsPass1");
            reyesCreateClassifyArgsPassDesc.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
                m_visibleClustersCounterBuffer,
                m_reyesClassifyIndirectArgsBuffer);
            outPasses.push_back(std::move(reyesCreateClassifyArgsPassDesc));

            RenderGraph::ExternalPassDesc reyesClassifyPassDesc;
            reyesClassifyPassDesc.type = RenderGraph::PassType::Compute;
            reyesClassifyPassDesc.name = MakeVariantPassName(traits, "ReyesClassifyPass1");
            reyesClassifyPassDesc.pass = std::make_shared<ReyesClassifyPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                nullptr,
                m_reyesFullClusterOutputsBuffer,
                m_reyesFullClusterOutputsCounterBuffer,
                m_reyesOwnedClustersBuffer,
                m_reyesOwnedClustersCounterBuffer,
                reyesOwnershipBitsetBuffer,
                m_reyesClassifyIndirectArgsBuffer,
                m_reyesTelemetryBufferPhase1,
                1u);
            outPasses.push_back(std::move(reyesClassifyPassDesc));

            RenderGraph::ExternalPassDesc reyesCreateSeedArgsPassDesc;
            reyesCreateSeedArgsPassDesc.type = RenderGraph::PassType::Compute;
            reyesCreateSeedArgsPassDesc.name = MakeVariantPassName(traits, "ReyesCreateSeedDispatchArgsPass1");
            reyesCreateSeedArgsPassDesc.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
                m_reyesOwnedClustersCounterBuffer,
                m_reyesSplitIndirectArgsBuffer);
            outPasses.push_back(std::move(reyesCreateSeedArgsPassDesc));

            RenderGraph::ExternalPassDesc reyesSeedPassDesc;
            reyesSeedPassDesc.type = RenderGraph::PassType::Compute;
            reyesSeedPassDesc.name = MakeVariantPassName(traits, "ReyesSeedPatchesPass1");
            reyesSeedPassDesc.pass = std::make_shared<ReyesSeedPatchesPass>(
                m_visibleClustersBuffer,
                m_reyesOwnedClustersBuffer,
                m_reyesOwnedClustersCounterBuffer,
                m_reyesSplitQueueBufferA,
                m_reyesSplitQueueCounterBufferA,
                m_reyesSplitQueueOverflowBufferA,
                m_reyesSplitIndirectArgsBuffer,
                reyesSplitQueueCapacity,
                1u);
            outPasses.push_back(std::move(reyesSeedPassDesc));

            const std::shared_ptr<Buffer> reyesSplitBuffers[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
            const std::shared_ptr<Buffer> reyesSplitCounters[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
            const std::shared_ptr<Buffer> reyesSplitOverflows[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
            for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
                const uint32_t inputIndex = splitPassIndex & 1u;
                const uint32_t outputIndex = inputIndex ^ 1u;

                RenderGraph::ExternalPassDesc reyesCreateSplitArgsPassDesc;
                reyesCreateSplitArgsPassDesc.type = RenderGraph::PassType::Compute;
                reyesCreateSplitArgsPassDesc.name = MakeVariantPassName(traits, "ReyesCreateSplitDispatchArgsPass1_" + std::to_string(splitPassIndex));
                reyesCreateSplitArgsPassDesc.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
                    reyesSplitCounters[inputIndex],
                    m_reyesSplitIndirectArgsBuffer);
                outPasses.push_back(std::move(reyesCreateSplitArgsPassDesc));

                RenderGraph::ExternalPassDesc reyesSplitPassDesc;
                reyesSplitPassDesc.type = RenderGraph::PassType::Compute;
                reyesSplitPassDesc.name = MakeVariantPassName(traits, "ReyesSplitPass1_" + std::to_string(splitPassIndex));
                reyesSplitPassDesc.pass = std::make_shared<ReyesSplitPass>(
                    m_visibleClustersBuffer,
                    reyesSplitBuffers[inputIndex],
                    reyesSplitCounters[inputIndex],
                    reyesSplitBuffers[outputIndex],
                    reyesSplitCounters[outputIndex],
                    reyesSplitOverflows[outputIndex],
                    m_reyesDiceQueueBuffer,
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesDiceQueueOverflowBuffer,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesTessTableVerticesBuffer,
                    m_reyesTessTableTrianglesBuffer,
                    m_reyesSplitIndirectArgsBuffer,
                    m_reyesTelemetryBufferPhase1,
                    reyesSplitQueueCapacity,
                    splitPassIndex,
                    CLodReyesMaxSplitPassCount,
                    1u);
                outPasses.push_back(std::move(reyesSplitPassDesc));
            }

            RenderGraph::ExternalPassDesc reyesCreateDiceArgsPassDesc;
            reyesCreateDiceArgsPassDesc.type = RenderGraph::PassType::Compute;
            reyesCreateDiceArgsPassDesc.name = MakeVariantPassName(traits, "ReyesCreateDiceDispatchArgsPass1");
            reyesCreateDiceArgsPassDesc.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
                m_reyesDiceQueueCounterBuffer,
                m_reyesDiceIndirectArgsBuffer);
            outPasses.push_back(std::move(reyesCreateDiceArgsPassDesc));

            RenderGraph::ExternalPassDesc reyesDicePassDesc;
            reyesDicePassDesc.type = RenderGraph::PassType::Compute;
            reyesDicePassDesc.name = MakeVariantPassName(traits, "ReyesDicePass1");
            reyesDicePassDesc.pass = std::make_shared<ReyesDicePass>(
                m_reyesDiceQueueBuffer,
                m_reyesDiceQueueCounterBuffer,
                m_reyesTessTableConfigsBuffer,
                m_reyesDiceIndirectArgsBuffer,
                m_reyesTelemetryBufferPhase1,
                reyesDiceQueueCapacity,
                1u);
            outPasses.push_back(std::move(reyesDicePassDesc));

            if (traits.type == CLodExtensionType::VisiblityBuffer) {
                RenderGraph::ExternalPassDesc reyesBuildRasterWorkPassDesc;
                reyesBuildRasterWorkPassDesc.type = RenderGraph::PassType::Compute;
                reyesBuildRasterWorkPassDesc.name = MakeVariantPassName(traits, "ReyesBuildRasterWorkPass1");
                reyesBuildRasterWorkPassDesc.pass = std::make_shared<ReyesBuildRasterWorkPass>(
                    m_reyesDiceQueueBuffer,
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesRasterWorkBuffer,
                    m_reyesRasterWorkCounterBuffer,
                    m_reyesDiceIndirectArgsBuffer,
                    m_reyesTelemetryBufferPhase1,
                    reyesRasterWorkCapacity);
                outPasses.push_back(std::move(reyesBuildRasterWorkPassDesc));

                RenderGraph::ExternalPassDesc reyesCreateRasterWorkArgsPassDesc;
                reyesCreateRasterWorkArgsPassDesc.type = RenderGraph::PassType::Compute;
                reyesCreateRasterWorkArgsPassDesc.name = MakeVariantPassName(traits, "ReyesCreateRasterWorkDispatchArgsPass1");
                reyesCreateRasterWorkArgsPassDesc.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_reyesRasterWorkCounterBuffer,
                    m_reyesRasterWorkIndirectArgsBuffer);
                outPasses.push_back(std::move(reyesCreateRasterWorkArgsPassDesc));

                RenderGraph::ExternalPassDesc reyesPatchRasterPassDesc;
                reyesPatchRasterPassDesc.type = RenderGraph::PassType::Compute;
                reyesPatchRasterPassDesc.name = MakeVariantPassName(traits, "ReyesPatchRasterPass1");
                reyesPatchRasterPassDesc.pass = std::make_shared<ReyesPatchRasterizationPass>(
                    m_visibleClustersBuffer,
                    m_reyesDiceQueueBuffer,
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesRasterWorkBuffer,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesTessTableVerticesBuffer,
                    m_reyesTessTableTrianglesBuffer,
                    m_viewRasterInfoBuffer,
                    m_reyesRasterWorkIndirectArgsBuffer,
                    m_reyesTelemetryBufferPhase1,
                    m_maxVisibleClusters,
                    1u,
                    CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters));
                outPasses.push_back(std::move(reyesPatchRasterPassDesc));
            }
        }

    RenderGraph::ExternalPassDesc histogramPassDesc;
    histogramPassDesc.type = RenderGraph::PassType::Compute;
    histogramPassDesc.name = MakeVariantPassName(traits, "RasterBucketsHistogramPass1");
    histogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBuffer,
        reyesOwnershipBitsetBuffer);
    outPasses.push_back(std::move(histogramPassDesc));

    RenderGraph::ExternalPassDesc prefixScanPassDesc;
    prefixScanPassDesc.type = RenderGraph::PassType::Compute;
    prefixScanPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPass1");
    prefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer);
    outPasses.push_back(std::move(prefixScanPassDesc));

    RenderGraph::ExternalPassDesc prefixOffsetsPassDesc;
    prefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
    prefixOffsetsPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass1");
    prefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer,
        m_rasterBucketsScannedBlockSumsBuffer,
        m_rasterBucketsTotalCountBufferPhase1);
    outPasses.push_back(std::move(prefixOffsetsPassDesc));

    RenderGraph::ExternalPassDesc compactPassDesc;
    compactPassDesc.type = RenderGraph::PassType::Compute;
    compactPassDesc.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass1");
    compactPassDesc.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBuffer,
        m_visibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsWriteCursorBuffer,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        reyesOwnershipBitsetBuffer,
        m_maxVisibleClusters,
        false);
    outPasses.push_back(std::move(compactPassDesc));

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly) {
        return;
    }

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        RenderGraph::ExternalPassDesc clearDeepVisibilityPassDesc;
        clearDeepVisibilityPassDesc.type = RenderGraph::PassType::Render;
        clearDeepVisibilityPassDesc.name = MakeVariantPassName(traits, "ClearDeepVisibilityPass");
        clearDeepVisibilityPassDesc.pass = std::make_shared<ClearDeepVisibilityPass>(
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer,
            m_deepVisibilityStatsBuffer);
        outPasses.push_back(std::move(clearDeepVisibilityPassDesc));

        RenderGraph::ExternalPassDesc rasterizeDeepVisibilityPassDesc;
        rasterizeDeepVisibilityPassDesc.type = RenderGraph::PassType::Render;
        rasterizeDeepVisibilityPassDesc.name = MakeVariantPassName(traits, "RasterizeClustersPass1");
        ClusterRasterizationPassInputs rasterizePassInputs;
        rasterizePassInputs.clearGbuffer = false;
        rasterizePassInputs.wireframe = false;
        rasterizePassInputs.renderPhase = renderPhase;
        rasterizePassInputs.outputKind = traits.rasterOutputKind;
        rasterizeDeepVisibilityPassDesc.pass = std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_deepVisibilityNodesBuffer,
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer,
            slabGroup);
        rasterizeDeepVisibilityPassDesc.isGeometryPass = true;
        outPasses.push_back(std::move(rasterizeDeepVisibilityPassDesc));

        RenderGraph::ExternalPassDesc resolveDeepVisibilityPassDesc;
        resolveDeepVisibilityPassDesc.type = RenderGraph::PassType::Compute;
        resolveDeepVisibilityPassDesc.name = MakeVariantPassName(traits, "DeepVisibilityResolvePass");
        resolveDeepVisibilityPassDesc.where = RenderGraph::ExternalInsertPoint::Before("PPLLResolvePass");
        resolveDeepVisibilityPassDesc.pass = std::make_shared<DeepVisibilityResolvePass>(
            m_visibleClustersBuffer,
            nullptr,
            m_deepVisibilityNodesBuffer,
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer,
            m_deepVisibilityStatsBuffer,
            CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters));
        outPasses.push_back(std::move(resolveDeepVisibilityPassDesc));
        return;
    }

    RenderGraph::ExternalPassDesc rasterizePassDesc;
    rasterizePassDesc.type = RenderGraph::PassType::Render;
    rasterizePassDesc.name = MakeVariantPassName(traits, "RasterizeClustersPass1");
    ClusterRasterizationPassInputs rasterizePassInputs;
    rasterizePassInputs.clearGbuffer = true;
    rasterizePassInputs.wireframe = false;
    rasterizePassInputs.renderPhase = renderPhase;
    rasterizePassInputs.outputKind = traits.rasterOutputKind;
    rasterizePassDesc.pass = std::make_shared<ClusterRasterizationPass>(
        rasterizePassInputs,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        nullptr,
        nullptr,
        nullptr,
        slabGroup);
    rasterizePassDesc.isGeometryPass = true;
    outPasses.push_back(std::move(rasterizePassDesc));

    if (useComputeSWRaster) {
        RenderGraph::ExternalPassDesc swCreateCommandPassDesc;
        swCreateCommandPassDesc.type = RenderGraph::PassType::Compute;
        swCreateCommandPassDesc.name = MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW1");
        swCreateCommandPassDesc.pass = std::make_shared<RasterBucketCreateCommandPass>(
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            true);
        outPasses.push_back(std::move(swCreateCommandPassDesc));

        RenderGraph::ExternalPassDesc swHistogramPassDesc;
        swHistogramPassDesc.type = RenderGraph::PassType::Compute;
        swHistogramPassDesc.name = MakeVariantPassName(traits, "RasterBucketsHistogramPassSW1");
        swHistogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferSw,
            reyesOwnershipBitsetBuffer,
            nullptr,
            true,
            m_maxVisibleClusters,
            true);
        outPasses.push_back(std::move(swHistogramPassDesc));

        RenderGraph::ExternalPassDesc swPrefixScanPassDesc;
        swPrefixScanPassDesc.type = RenderGraph::PassType::Compute;
        swPrefixScanPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW1");
        swPrefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferSw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            true);
        outPasses.push_back(std::move(swPrefixScanPassDesc));

        RenderGraph::ExternalPassDesc swPrefixOffsetsPassDesc;
        swPrefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
        swPrefixOffsetsPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW1");
        swPrefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBufferPhase1Sw,
            true);
        outPasses.push_back(std::move(swPrefixOffsetsPassDesc));

        RenderGraph::ExternalPassDesc swCompactPassDesc;
        swCompactPassDesc.type = RenderGraph::PassType::Compute;
        swCompactPassDesc.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW1");
        swCompactPassDesc.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBuffer,
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferSw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBufferSw,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            reyesOwnershipBitsetBuffer,
            m_maxVisibleClusters,
            false,
            true,
            true,
            true);
        outPasses.push_back(std::move(swCompactPassDesc));

        RenderGraph::ExternalPassDesc swRasterizePassDesc;
        swRasterizePassDesc.type = RenderGraph::PassType::Compute;
        swRasterizePassDesc.name = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass1");
        swRasterizePassDesc.pass = std::make_shared<ClusterSoftwareRasterizationPass>(
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBufferSw,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_viewRasterInfoBuffer,
            slabGroup,
            true);
        outPasses.push_back(std::move(swRasterizePassDesc));
    }

    if (traits.schedulesPerViewDepthCopy) {
        RenderGraph::ExternalPassDesc depthCopyPassDesc;
        depthCopyPassDesc.type = RenderGraph::PassType::Compute;
        depthCopyPassDesc.name = MakeVariantPassName(traits, "LinearDepthCopyPass1");
        depthCopyPassDesc.pass = std::make_shared<PerViewLinearDepthCopyPass>();
        outPasses.push_back(std::move(depthCopyPassDesc));
    }

    RenderGraph::ExternalPassDesc downsamplePassDesc;
    downsamplePassDesc.type = RenderGraph::PassType::Compute;
    downsamplePassDesc.name = MakeVariantPassName(traits, "LinearDepthDownsamplePass1");
    downsamplePassDesc.pass = std::make_shared<DownsamplePass>();
    outPasses.push_back(std::move(downsamplePassDesc));

    if (!traits.usesPhase2OcclusionReplay) {
        return;
    }

    RenderGraph::ExternalPassDesc cullPassDesc2;
    cullPassDesc2.type = RenderGraph::PassType::Compute;
    //cullPassDesc2.computeQueueSelection = ComputeQueueSelection::Graphics;
    cullPassDesc2.name = MakeVariantPassName(traits, "HierarchialCullingPass2");
    HierarchialCullingPassInputs cullPassInputs2;
    cullPassInputs2.isFirstPass = false;
    cullPassInputs2.maxVisibleClusters = m_maxVisibleClusters;
    cullPassInputs2.workGraphMode = workGraphMode;
    cullPassInputs2.renderPhase = renderPhase;
    cullPassInputs2.clodOnlyWorkloads = true;
    cullPassInputs2.rasterOutputKind = traits.rasterOutputKind;
    cullPassDesc2.pass = std::make_shared<HierarchialCullingPass>(
        cullPassInputs2,
        m_visibleClustersBuffer,
        m_visibleClustersCounterBufferPhase2,
        m_swVisibleClustersCounterBufferPhase2,
        m_histogramIndirectCommand,
        m_workGraphTelemetryBuffer,
        m_occlusionReplayBuffer,
        m_occlusionReplayStateBuffer,
        m_occlusionNodeGpuInputsBuffer,
        m_viewDepthSrvIndicesBuffer,
        m_viewRasterInfoBuffer,
        slabGroup,
        m_visibleClustersCounterBuffer,
        m_swVisibleClustersCounterBuffer);
    outPasses.push_back(std::move(cullPassDesc2));

    if (!disableReyesTessellation) {
        RenderGraph::ExternalPassDesc reyesResetPassDesc2;
        reyesResetPassDesc2.type = RenderGraph::PassType::Compute;
        reyesResetPassDesc2.name = MakeVariantPassName(traits, "ReyesQueueResetPass2");
        reyesResetPassDesc2.pass = std::make_shared<ReyesQueueResetPass>(
            m_reyesFullClusterOutputsCounterBuffer,
            m_reyesOwnedClustersCounterBuffer,
            std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
            std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
            m_reyesDiceQueueCounterBuffer,
            m_reyesDiceQueueOverflowBuffer,
            reyesOwnershipBitsetBufferPhase2,
            m_reyesTelemetryBufferPhase2,
            2u);
        outPasses.push_back(std::move(reyesResetPassDesc2));

        RenderGraph::ExternalPassDesc reyesCreateClassifyArgsPassDesc2;
        reyesCreateClassifyArgsPassDesc2.type = RenderGraph::PassType::Compute;
        reyesCreateClassifyArgsPassDesc2.name = MakeVariantPassName(traits, "ReyesCreateClassifyDispatchArgsPass2");
        reyesCreateClassifyArgsPassDesc2.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
            m_visibleClustersCounterBufferPhase2,
            m_reyesClassifyIndirectArgsBufferPhase2);
        outPasses.push_back(std::move(reyesCreateClassifyArgsPassDesc2));

        RenderGraph::ExternalPassDesc reyesClassifyPassDesc2;
        reyesClassifyPassDesc2.type = RenderGraph::PassType::Compute;
        reyesClassifyPassDesc2.name = MakeVariantPassName(traits, "ReyesClassifyPass2");
        reyesClassifyPassDesc2.pass = std::make_shared<ReyesClassifyPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBufferPhase2,
            m_visibleClustersCounterBuffer,
            m_reyesFullClusterOutputsBuffer,
            m_reyesFullClusterOutputsCounterBuffer,
            m_reyesOwnedClustersBuffer,
            m_reyesOwnedClustersCounterBuffer,
            reyesOwnershipBitsetBufferPhase2,
            m_reyesClassifyIndirectArgsBufferPhase2,
            m_reyesTelemetryBufferPhase2,
            2u);
        outPasses.push_back(std::move(reyesClassifyPassDesc2));

        RenderGraph::ExternalPassDesc reyesCreateSeedArgsPassDesc2;
        reyesCreateSeedArgsPassDesc2.type = RenderGraph::PassType::Compute;
        reyesCreateSeedArgsPassDesc2.name = MakeVariantPassName(traits, "ReyesCreateSeedDispatchArgsPass2");
        reyesCreateSeedArgsPassDesc2.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
            m_reyesOwnedClustersCounterBuffer,
            m_reyesSplitIndirectArgsBufferPhase2);
        outPasses.push_back(std::move(reyesCreateSeedArgsPassDesc2));

        RenderGraph::ExternalPassDesc reyesSeedPassDesc2;
        reyesSeedPassDesc2.type = RenderGraph::PassType::Compute;
        reyesSeedPassDesc2.name = MakeVariantPassName(traits, "ReyesSeedPatchesPass2");
        reyesSeedPassDesc2.pass = std::make_shared<ReyesSeedPatchesPass>(
            m_visibleClustersBuffer,
            m_reyesOwnedClustersBuffer,
            m_reyesOwnedClustersCounterBuffer,
            m_reyesSplitQueueBufferA,
            m_reyesSplitQueueCounterBufferA,
            m_reyesSplitQueueOverflowBufferA,
            m_reyesSplitIndirectArgsBufferPhase2,
            reyesSplitQueueCapacity,
            2u);
        outPasses.push_back(std::move(reyesSeedPassDesc2));

        const std::shared_ptr<Buffer> reyesSplitBuffers[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
        const std::shared_ptr<Buffer> reyesSplitCounters[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
        const std::shared_ptr<Buffer> reyesSplitOverflows[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
        for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
            const uint32_t inputIndex = splitPassIndex & 1u;
            const uint32_t outputIndex = inputIndex ^ 1u;

            RenderGraph::ExternalPassDesc reyesCreateSplitArgsPassDesc2;
            reyesCreateSplitArgsPassDesc2.type = RenderGraph::PassType::Compute;
            reyesCreateSplitArgsPassDesc2.name = MakeVariantPassName(traits, "ReyesCreateSplitDispatchArgsPass2_" + std::to_string(splitPassIndex));
            reyesCreateSplitArgsPassDesc2.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
                reyesSplitCounters[inputIndex],
                m_reyesSplitIndirectArgsBufferPhase2);
            outPasses.push_back(std::move(reyesCreateSplitArgsPassDesc2));

            RenderGraph::ExternalPassDesc reyesSplitPassDesc2;
            reyesSplitPassDesc2.type = RenderGraph::PassType::Compute;
            reyesSplitPassDesc2.name = MakeVariantPassName(traits, "ReyesSplitPass2_" + std::to_string(splitPassIndex));
            reyesSplitPassDesc2.pass = std::make_shared<ReyesSplitPass>(
                m_visibleClustersBuffer,
                reyesSplitBuffers[inputIndex],
                reyesSplitCounters[inputIndex],
                reyesSplitBuffers[outputIndex],
                reyesSplitCounters[outputIndex],
                reyesSplitOverflows[outputIndex],
                m_reyesDiceQueueBuffer,
                m_reyesDiceQueueCounterBuffer,
                m_reyesDiceQueueOverflowBuffer,
                m_reyesTessTableConfigsBuffer,
                m_reyesTessTableVerticesBuffer,
                m_reyesTessTableTrianglesBuffer,
                m_reyesSplitIndirectArgsBufferPhase2,
                m_reyesTelemetryBufferPhase2,
                reyesSplitQueueCapacity,
                splitPassIndex,
                CLodReyesMaxSplitPassCount,
                2u);
            outPasses.push_back(std::move(reyesSplitPassDesc2));
        }

        RenderGraph::ExternalPassDesc reyesCreateDiceArgsPassDesc2;
        reyesCreateDiceArgsPassDesc2.type = RenderGraph::PassType::Compute;
        reyesCreateDiceArgsPassDesc2.name = MakeVariantPassName(traits, "ReyesCreateDiceDispatchArgsPass2");
        reyesCreateDiceArgsPassDesc2.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
            m_reyesDiceQueueCounterBuffer,
            m_reyesDiceIndirectArgsBufferPhase2);
        outPasses.push_back(std::move(reyesCreateDiceArgsPassDesc2));

        RenderGraph::ExternalPassDesc reyesDicePassDesc2;
        reyesDicePassDesc2.type = RenderGraph::PassType::Compute;
        reyesDicePassDesc2.name = MakeVariantPassName(traits, "ReyesDicePass2");
        reyesDicePassDesc2.pass = std::make_shared<ReyesDicePass>(
            m_reyesDiceQueueBuffer,
            m_reyesDiceQueueCounterBuffer,
            m_reyesTessTableConfigsBuffer,
            m_reyesDiceIndirectArgsBufferPhase2,
            m_reyesTelemetryBufferPhase2,
            reyesDiceQueueCapacity,
            2u);
        outPasses.push_back(std::move(reyesDicePassDesc2));

        if (traits.type == CLodExtensionType::VisiblityBuffer) {
            RenderGraph::ExternalPassDesc reyesBuildRasterWorkPassDesc2;
            reyesBuildRasterWorkPassDesc2.type = RenderGraph::PassType::Compute;
            reyesBuildRasterWorkPassDesc2.name = MakeVariantPassName(traits, "ReyesBuildRasterWorkPass2");
            reyesBuildRasterWorkPassDesc2.pass = std::make_shared<ReyesBuildRasterWorkPass>(
                m_reyesDiceQueueBuffer,
                m_reyesDiceQueueCounterBuffer,
                m_reyesTessTableConfigsBuffer,
                m_reyesRasterWorkBufferPhase2,
                m_reyesRasterWorkCounterBufferPhase2,
                m_reyesDiceIndirectArgsBufferPhase2,
                m_reyesTelemetryBufferPhase2,
                reyesRasterWorkCapacity);
            outPasses.push_back(std::move(reyesBuildRasterWorkPassDesc2));

            RenderGraph::ExternalPassDesc reyesCreateRasterWorkArgsPassDesc2;
            reyesCreateRasterWorkArgsPassDesc2.type = RenderGraph::PassType::Compute;
            reyesCreateRasterWorkArgsPassDesc2.name = MakeVariantPassName(traits, "ReyesCreateRasterWorkDispatchArgsPass2");
            reyesCreateRasterWorkArgsPassDesc2.pass = std::make_shared<ReyesCreateDispatchArgsPass>(
                m_reyesRasterWorkCounterBufferPhase2,
                m_reyesRasterWorkIndirectArgsBufferPhase2);
            outPasses.push_back(std::move(reyesCreateRasterWorkArgsPassDesc2));

            RenderGraph::ExternalPassDesc reyesPatchRasterPassDesc2;
            reyesPatchRasterPassDesc2.type = RenderGraph::PassType::Compute;
            reyesPatchRasterPassDesc2.name = MakeVariantPassName(traits, "ReyesPatchRasterPass2");
            reyesPatchRasterPassDesc2.pass = std::make_shared<ReyesPatchRasterizationPass>(
                m_visibleClustersBuffer,
                m_reyesDiceQueueBuffer,
                m_reyesDiceQueueCounterBuffer,
                m_reyesRasterWorkBufferPhase2,
                m_reyesTessTableConfigsBuffer,
                m_reyesTessTableVerticesBuffer,
                m_reyesTessTableTrianglesBuffer,
                m_viewRasterInfoBuffer,
                m_reyesRasterWorkIndirectArgsBufferPhase2,
                m_reyesTelemetryBufferPhase2,
                m_maxVisibleClusters,
                2u,
                CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters));
            outPasses.push_back(std::move(reyesPatchRasterPassDesc2));
        }
    }

    RenderGraph::ExternalPassDesc histogramPassDesc2;
    histogramPassDesc2.type = RenderGraph::PassType::Compute;
    histogramPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsHistogramPass2");
    histogramPassDesc2.pass = std::make_shared<RasterBucketHistogramPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBufferPhase2,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBufferPhase2,
        reyesOwnershipBitsetBufferPhase2,
        m_visibleClustersCounterBuffer);
    outPasses.push_back(std::move(histogramPassDesc2));

    RenderGraph::ExternalPassDesc prefixScanPassDesc2;
    prefixScanPassDesc2.type = RenderGraph::PassType::Compute;
    prefixScanPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPass2");
    prefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer);
    outPasses.push_back(std::move(prefixScanPassDesc2));

    RenderGraph::ExternalPassDesc prefixOffsetsPassDesc2;
    prefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
    prefixOffsetsPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass2");
    prefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer,
        m_rasterBucketsScannedBlockSumsBuffer,
        m_rasterBucketsTotalCountBuffer);
    outPasses.push_back(std::move(prefixOffsetsPassDesc2));

    RenderGraph::ExternalPassDesc compactPassDesc2;
    compactPassDesc2.type = RenderGraph::PassType::Compute;
    compactPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass2");
    compactPassDesc2.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBufferPhase2,
        m_rasterBucketsTotalCountBufferPhase1,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsWriteCursorBufferPhase2,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        reyesOwnershipBitsetBufferPhase2,
        m_maxVisibleClusters,
        true);
    outPasses.push_back(std::move(compactPassDesc2));

    RenderGraph::ExternalPassDesc rasterizePassDesc2;
    rasterizePassDesc2.type = RenderGraph::PassType::Render;
    rasterizePassDesc2.name = MakeVariantPassName(traits, "RasterizeClustersPass2");
    rasterizePassDesc2.where = RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass");
    ClusterRasterizationPassInputs rasterizePassInputs2;
    rasterizePassInputs2.clearGbuffer = false;
    rasterizePassInputs2.wireframe = false;
    rasterizePassInputs2.renderPhase = renderPhase;
    rasterizePassInputs2.outputKind = traits.rasterOutputKind;
    rasterizePassDesc2.pass = std::make_shared<ClusterRasterizationPass>(
        rasterizePassInputs2,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        nullptr,
        nullptr,
        nullptr,
        slabGroup);
    rasterizePassDesc2.isGeometryPass = true;
    outPasses.push_back(std::move(rasterizePassDesc2));

    if (useComputeSWRaster) {
        RenderGraph::ExternalPassDesc swCreateCommandPassDesc2;
        swCreateCommandPassDesc2.type = RenderGraph::PassType::Compute;
        swCreateCommandPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW2");
        swCreateCommandPassDesc2.pass = std::make_shared<RasterBucketCreateCommandPass>(
            m_swVisibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            true);
        outPasses.push_back(std::move(swCreateCommandPassDesc2));

        RenderGraph::ExternalPassDesc swHistogramPassDesc2;
        swHistogramPassDesc2.type = RenderGraph::PassType::Compute;
        swHistogramPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsHistogramPassSW2");
        swHistogramPassDesc2.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2Sw,
            reyesOwnershipBitsetBufferPhase2,
            m_swVisibleClustersCounterBuffer,
            true,
            m_maxVisibleClusters,
            true);
        outPasses.push_back(std::move(swHistogramPassDesc2));

        RenderGraph::ExternalPassDesc swPrefixScanPassDesc2;
        swPrefixScanPassDesc2.type = RenderGraph::PassType::Compute;
        swPrefixScanPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW2");
        swPrefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            true);
        outPasses.push_back(std::move(swPrefixScanPassDesc2));

        RenderGraph::ExternalPassDesc swPrefixOffsetsPassDesc2;
        swPrefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
        swPrefixOffsetsPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW2");
        swPrefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer,
            true);
        outPasses.push_back(std::move(swPrefixOffsetsPassDesc2));

        RenderGraph::ExternalPassDesc swCompactPassDesc2;
        swCompactPassDesc2.type = RenderGraph::PassType::Compute;
        swCompactPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW2");
        swCompactPassDesc2.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBufferPhase2,
            m_rasterBucketsTotalCountBufferPhase1Sw,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBufferPhase2Sw,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            reyesOwnershipBitsetBufferPhase2,
            m_maxVisibleClusters,
            true,
            true,
            true,
            true);
        outPasses.push_back(std::move(swCompactPassDesc2));

        RenderGraph::ExternalPassDesc swRasterizePassDesc2;
        swRasterizePassDesc2.type = RenderGraph::PassType::Compute;
        swRasterizePassDesc2.name = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass2");
        swRasterizePassDesc2.pass = std::make_shared<ClusterSoftwareRasterizationPass>(
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_viewRasterInfoBuffer,
            slabGroup,
            true);
        outPasses.push_back(std::move(swRasterizePassDesc2));
    }

    if (traits.schedulesPerViewDepthCopy) {
        RenderGraph::ExternalPassDesc depthCopyPassDesc2;
        depthCopyPassDesc2.type = RenderGraph::PassType::Compute;
        depthCopyPassDesc2.name = MakeVariantPassName(traits, "LinearDepthCopyPass2");
        depthCopyPassDesc2.where = RenderGraph::ExternalInsertPoint::Before("DeferredShadingPass");
        depthCopyPassDesc2.pass = std::make_shared<PerViewLinearDepthCopyPass>();
        outPasses.push_back(std::move(depthCopyPassDesc2));
    }

    RenderGraph::ExternalPassDesc downsamplePassDesc2;
    downsamplePassDesc2.type = RenderGraph::PassType::Compute;
    downsamplePassDesc2.name = MakeVariantPassName(traits, "LinearDepthDownsamplePass2");
    downsamplePassDesc2.pass = std::make_shared<DownsamplePass>();
    outPasses.push_back(std::move(downsamplePassDesc2));
}

void CLodExtension::GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    if (m_streamingSystem) {
        m_streamingSystem->GatherFramePasses(rg, outPasses);
    }
}
