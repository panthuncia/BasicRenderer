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
#include "Render/GraphExtensions/ClusterLOD/ReyesDeepVisibilityRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesCopyCounterPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesCreateDispatchArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesDicePass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesPatchRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesQueueResetPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesSeedPatchesPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesSplitPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationTable.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationTableUploadPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapAllocatePagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapBuildPageListsPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearDirtyBitsPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapGatherStatsPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapDirtyHierarchyPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapMarkPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapSetupPass.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "Render/RenderPhase.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Buffers/PagePool.h"
#include "Resources/components.h"
#include "ShaderBuffers.h"
#include "Utilities/ProportionalBudgetAllocator.h"
#include "BuiltinResources.h"

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
            Engine::Primary::ShadowMapsPass,
            CLodRasterOutputKind::VirtualShadow,
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

TextureDescription CreateVirtualShadowPageTableDescription()
{
    TextureDescription desc;
    ImageDimensions dims;
    dims.width = CLodVirtualShadowDefaultPageTableResolution;
    dims.height = CLodVirtualShadowDefaultPageTableResolution;
    dims.rowPitch = static_cast<uint64_t>(dims.width) * sizeof(uint32_t);
    dims.slicePitch = dims.rowPitch * static_cast<uint64_t>(dims.height);
    desc.imageDimensions.push_back(dims);
    desc.channels = 1;
    desc.format = rhi::Format::R32_UInt;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.hasUAV = true;
    desc.uavFormat = rhi::Format::R32_UInt;
    desc.isArray = true;
    desc.arraySize = CLodVirtualShadowDefaultClipmapCount;
    return desc;
}

TextureDescription CreateVirtualShadowPhysicalPagesDescription()
{
    TextureDescription desc;
    ImageDimensions dims;
    dims.width = CLodVirtualShadowDefaultPhysicalPagesPerAxis * CLodVirtualShadowPhysicalPageSize;
    dims.height = CLodVirtualShadowDefaultPhysicalPagesPerAxis * CLodVirtualShadowPhysicalPageSize;
    dims.rowPitch = static_cast<uint64_t>(dims.width) * sizeof(uint32_t);
    dims.slicePitch = dims.rowPitch * static_cast<uint64_t>(dims.height);
    desc.imageDimensions.push_back(dims);
    desc.channels = 1;
    desc.format = rhi::Format::R32_UInt;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.hasUAV = true;
    desc.uavFormat = rhi::Format::R32_UInt;
    return desc;
}

TextureDescription CreateVirtualShadowDirtyHierarchyDescription()
{
    TextureDescription desc;
    ImageDimensions dims;
    dims.width = CLodVirtualShadowDefaultPageTableResolution;
    dims.height = CLodVirtualShadowDefaultPageTableResolution;
    dims.rowPitch = static_cast<uint64_t>(dims.width) * sizeof(uint32_t);
    dims.slicePitch = dims.rowPitch * static_cast<uint64_t>(dims.height);
    desc.imageDimensions.push_back(dims);
    desc.channels = 1;
    desc.format = rhi::Format::R32_UInt;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.hasUAV = true;
    desc.uavFormat = rhi::Format::R32_UInt;
    desc.isArray = true;
    desc.arraySize = CLodVirtualShadowDefaultClipmapCount;
    desc.generateMipMaps = true;
    return desc;
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

struct ReyesResourceSizing {
    uint32_t fullClusterOutputCapacity = 0u;
    uint32_t ownedClusterCapacity = 0u;
    uint32_t splitQueueCapacity = 0u;
    uint32_t diceQueueCapacity = 0u;
    uint32_t diceQueuePhysicalCapacity = 0u;
    uint32_t rasterWorkCapacity = 0u;
    uint32_t ownershipBitsetWordCount = 0u;
    uint64_t requestedBudgetBytes = 0u;
    uint64_t allocatedBudgetBytes = 0u;
    bool budgetLimited = false;
};

uint64_t BufferBytes(uint32_t elementCount, size_t elementSize)
{
    return static_cast<uint64_t>(elementCount) * static_cast<uint64_t>(elementSize);
}

uint32_t BytesToElementCount(uint64_t allocatedBytes, size_t elementSize)
{
    if (elementSize == 0u) {
        throw std::runtime_error("Element size must be non-zero when converting budgeted bytes to element count.");
    }

    return static_cast<uint32_t>(allocatedBytes / static_cast<uint64_t>(elementSize));
}

ReyesResourceSizing BuildReyesResourceSizing(const CLodVariantTraits& traits, uint32_t maxVisibleClusters)
{
    const bool usesPhase2ReyesResources = traits.usesPhase2OcclusionReplay;
    const uint32_t ownershipBitsetWordCount = CLodBitsetWordCount(maxVisibleClusters);
    const uint32_t idealFullClusterOutputCapacity = maxVisibleClusters;
    const uint32_t idealOwnedClusterCapacity = maxVisibleClusters;
    const uint32_t idealSplitQueueCapacity = CLodReyesSplitQueueCapacity(maxVisibleClusters);
    const uint32_t idealDiceQueueCapacity = CLodReyesDiceQueueCapacity(maxVisibleClusters);
    const uint32_t idealRasterWorkCapacity = CLodReyesRasterWorkCapacity(maxVisibleClusters);
    const uint32_t diceQueuePhaseMultiplier = usesPhase2ReyesResources ? 2u : 1u;
    const ReyesTessellationTableData& reyesTessellationTableData = GetReyesTessellationTableData();

    std::vector<budget::ProportionalBudgetItem> budgetItems;
    budgetItems.reserve(24);

    auto addFixedItem = [&budgetItems](std::string_view id, uint64_t bytes) {
        budgetItems.push_back(budget::ProportionalBudgetItem{
            .id = id,
            .idealBytes = bytes,
            .minBytes = bytes,
            .maxBytes = bytes,
            .quantumBytes = 1u,
        });
    };

    auto addFlexibleItem = [&budgetItems](std::string_view id, uint64_t idealBytes, uint64_t quantumBytes) {
        budgetItems.push_back(budget::ProportionalBudgetItem{
            .id = id,
            .idealBytes = idealBytes,
            .minBytes = quantumBytes,
            .maxBytes = idealBytes,
            .quantumBytes = quantumBytes,
        });
    };

    addFlexibleItem("fullClusterOutputs", BufferBytes(idealFullClusterOutputCapacity, sizeof(CLodReyesFullClusterOutput)), sizeof(CLodReyesFullClusterOutput));
    addFixedItem("fullClusterOutputCounter", sizeof(uint32_t));
    addFlexibleItem("ownedClusters", BufferBytes(idealOwnedClusterCapacity, sizeof(CLodReyesOwnedClusterEntry)), sizeof(CLodReyesOwnedClusterEntry));
    addFixedItem("ownedClusterCounter", sizeof(uint32_t));
    addFixedItem("ownershipBitset", BufferBytes(ownershipBitsetWordCount, sizeof(uint32_t)));
    if (usesPhase2ReyesResources) {
        addFixedItem("ownershipBitsetPhase2", BufferBytes(ownershipBitsetWordCount, sizeof(uint32_t)));
    }

    addFixedItem("classifyIndirectArgs", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFixedItem("classifyIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }

    addFixedItem("splitIndirectArgs", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFixedItem("splitIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }

    addFlexibleItem("splitQueueA", BufferBytes(idealSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry)), sizeof(CLodReyesSplitQueueEntry));
    addFixedItem("splitQueueCounterA", sizeof(uint32_t));
    addFixedItem("splitQueueOverflowA", sizeof(uint32_t));
    addFlexibleItem("splitQueueB", BufferBytes(idealSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry)), sizeof(CLodReyesSplitQueueEntry));
    addFixedItem("splitQueueCounterB", sizeof(uint32_t));
    addFixedItem("splitQueueOverflowB", sizeof(uint32_t));

    addFlexibleItem(
        "diceQueue",
        BufferBytes(idealDiceQueueCapacity * diceQueuePhaseMultiplier, sizeof(CLodReyesDiceQueueEntry)),
        static_cast<uint64_t>(sizeof(CLodReyesDiceQueueEntry)) * static_cast<uint64_t>(diceQueuePhaseMultiplier));
    addFixedItem("diceQueueCounter", sizeof(uint32_t));
    if (usesPhase2ReyesResources) {
        addFixedItem("diceQueuePhase1Count", sizeof(uint32_t));
    }
    addFixedItem("diceQueueOverflow", sizeof(uint32_t));

    addFlexibleItem("rasterWorkPhase1", BufferBytes(idealRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry)), sizeof(CLodReyesRasterWorkEntry));
    addFixedItem("rasterWorkCounterPhase1", sizeof(uint32_t));
    addFixedItem("rasterWorkIndirectArgsPhase1", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFlexibleItem("rasterWorkPhase2", BufferBytes(idealRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry)), sizeof(CLodReyesRasterWorkEntry));
        addFixedItem("rasterWorkCounterPhase2", sizeof(uint32_t));
        addFixedItem("rasterWorkIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }

    addFixedItem("tessTableConfigs", BufferBytes(static_cast<uint32_t>(reyesTessellationTableData.configs.size()), sizeof(CLodReyesTessTableConfigEntry)));
    addFixedItem("tessTableVertices", BufferBytes(static_cast<uint32_t>(reyesTessellationTableData.vertices.size()), sizeof(uint32_t)));
    addFixedItem("tessTableTriangles", BufferBytes(static_cast<uint32_t>(reyesTessellationTableData.triangles.size()), sizeof(uint32_t)));
    addFixedItem("diceIndirectArgsPhase1", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFixedItem("diceIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }
    addFixedItem("telemetryPhase1", sizeof(CLodReyesTelemetry));
    if (usesPhase2ReyesResources) {
        addFixedItem("telemetryPhase2", sizeof(CLodReyesTelemetry));
    }

    uint64_t idealBudgetBytes = 0u;
    for (const budget::ProportionalBudgetItem& item : budgetItems) {
        idealBudgetBytes += item.idealBytes;
    }

    const uint64_t configuredBudgetBytes = static_cast<uint64_t>(SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodReyesResourceBudgetBytesSettingName)());
    const uint64_t effectiveBudgetBytes = configuredBudgetBytes == 0u ? idealBudgetBytes : configuredBudgetBytes;
    const budget::ProportionalBudgetResult budgetResult = budget::AllocateProportionalBudget(budgetItems, effectiveBudgetBytes);

    ReyesResourceSizing sizing{};
    sizing.fullClusterOutputCapacity = BytesToElementCount(budgetResult.Find("fullClusterOutputs").allocatedBytes, sizeof(CLodReyesFullClusterOutput));
    sizing.ownedClusterCapacity = BytesToElementCount(budgetResult.Find("ownedClusters").allocatedBytes, sizeof(CLodReyesOwnedClusterEntry));
    sizing.splitQueueCapacity = BytesToElementCount(budgetResult.Find("splitQueueA").allocatedBytes, sizeof(CLodReyesSplitQueueEntry));
    sizing.diceQueueCapacity = static_cast<uint32_t>(
        budgetResult.Find("diceQueue").allocatedBytes /
        (static_cast<uint64_t>(sizeof(CLodReyesDiceQueueEntry)) * static_cast<uint64_t>(diceQueuePhaseMultiplier)));
    sizing.diceQueuePhysicalCapacity = sizing.diceQueueCapacity * diceQueuePhaseMultiplier;
    sizing.rasterWorkCapacity = BytesToElementCount(budgetResult.Find("rasterWorkPhase1").allocatedBytes, sizeof(CLodReyesRasterWorkEntry));
    sizing.ownershipBitsetWordCount = ownershipBitsetWordCount;
    sizing.requestedBudgetBytes = budgetResult.requestedTotalBytes;
    sizing.allocatedBudgetBytes = budgetResult.allocatedTotalBytes;
    sizing.budgetLimited = budgetResult.allocatedTotalBytes < budgetResult.requestedTotalBytes;
    return sizing;
}

} // namespace

CLodExtension::~CLodExtension() = default;

bool CLodExtension::IsReyesTessellationDisabled() const
{
    return SettingsManager::GetInstance().getSettingGetter<bool>(CLodDisableReyesRasterizationSettingName)();
}

void CLodExtension::InitializeCoreResources()
{
    const auto& traits = GetVariantTraits(m_type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    m_visibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(m_maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
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

    m_compactedVisibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(m_maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
    m_compactedVisibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Compacted Visible Clusters Buffer"));

    m_visibleClustersBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersBuffer })
        .set<CLodVisibleClusterCapacity>({ m_maxVisibleClusters })
        .add<VisibleClustersBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_rasterBucketsWriteCursorBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor"));

    m_rasterBucketsIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false);
    m_rasterBucketsIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args phase1"));

    m_rasterBucketsIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false);
    m_rasterBucketsIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args phase2"));

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

    m_sortedToUnsortedMappingBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(m_maxVisibleClusters), sizeof(uint32_t), true, false);
    m_sortedToUnsortedMappingBuffer->SetName(MakeVariantResourceName(traits, "Sorted-to-Unsorted Mapping Buffer"));

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName(MakeVariantResourceName(traits, "View Raster Info Buffer"));
}

void CLodExtension::InitializeDeepVisibilityResources()
{
    const auto& traits = GetVariantTraits(m_type);
    if (traits.rasterOutputKind != CLodRasterOutputKind::DeepVisibility) {
        return;
    }

    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

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
}

void CLodExtension::InitializeShadowResources()
{
    const auto& traits = GetVariantTraits(m_type);
    if (traits.type != CLodExtensionType::Shadow) {
        return;
    }

    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    m_shadowPageTableTexture = PixelBuffer::CreateSharedUnmaterialized(CreateVirtualShadowPageTableDescription());
    m_shadowPageTableTexture->SetName(MakeVariantResourceName(traits, "Virtual Shadow Page Table"));
    m_shadowPageTableTexture->GetECSEntity()
        .set<Components::Resource>({ m_shadowPageTableTexture })
        .add<CLodVirtualShadowPageTableTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPhysicalPagesTexture = PixelBuffer::CreateSharedUnmaterialized(CreateVirtualShadowPhysicalPagesDescription());
    m_shadowPhysicalPagesTexture->SetName(MakeVariantResourceName(traits, "Virtual Shadow Physical Pages"));
    m_shadowPhysicalPagesTexture->GetECSEntity()
        .set<Components::Resource>({ m_shadowPhysicalPagesTexture })
        .add<CLodVirtualShadowPhysicalPagesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPageMetadataBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowDefaultPhysicalPageCount,
        sizeof(CLodVirtualShadowPhysicalPageMeta),
        true,
        false,
        false,
        false);
    m_shadowPageMetadataBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Page Metadata Buffer"));
    m_shadowPageMetadataBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPageMetadataBuffer })
        .add<CLodVirtualShadowPageMetadataTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowAllocationRequestsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxAllocationRequests,
        sizeof(CLodVirtualShadowPageAllocationRequest),
        true,
        false,
        false,
        false);
    m_shadowAllocationRequestsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Allocation Requests Buffer"));
    m_shadowAllocationRequestsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowAllocationRequestsBuffer })
        .add<CLodVirtualShadowAllocationRequestsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowAllocationCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_shadowAllocationCountBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Allocation Count Buffer"));
    m_shadowAllocationCountBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowAllocationCountBuffer })
        .add<CLodVirtualShadowAllocationCountTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowAllocationIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_shadowAllocationIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Allocation Indirect Args Buffer"));

    m_shadowFreePhysicalPagesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowDefaultPhysicalPageCount,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowFreePhysicalPagesBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Free Physical Pages Buffer"));
    m_shadowFreePhysicalPagesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowFreePhysicalPagesBuffer })
        .add<CLodVirtualShadowFreePhysicalPagesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowReusablePhysicalPagesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowDefaultPhysicalPageCount,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowReusablePhysicalPagesBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Reusable Physical Pages Buffer"));
    m_shadowReusablePhysicalPagesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowReusablePhysicalPagesBuffer })
        .add<CLodVirtualShadowReusablePhysicalPagesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPageListHeaderBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodVirtualShadowPageListHeader), true, false, false, false);
    m_shadowPageListHeaderBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Page List Header Buffer"));
    m_shadowPageListHeaderBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPageListHeaderBuffer })
        .add<CLodVirtualShadowPageListHeaderTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowDirtyPageFlagsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowDirtyWordCount(CLodVirtualShadowDefaultPhysicalPageCount),
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowDirtyPageFlagsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Dirty Page Flags Buffer"));
    m_shadowDirtyPageFlagsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowDirtyPageFlagsBuffer })
        .add<CLodVirtualShadowDirtyPageFlagsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowDirtyPageHierarchyTexture = PixelBuffer::CreateSharedUnmaterialized(CreateVirtualShadowDirtyHierarchyDescription());
    m_shadowDirtyPageHierarchyTexture->SetName(MakeVariantResourceName(traits, "Virtual Shadow Dirty Hierarchy"));
    m_shadowDirtyPageHierarchyTexture->GetECSEntity()
        .set<Components::Resource>({ m_shadowDirtyPageHierarchyTexture })
        .add<CLodVirtualShadowDirtyHierarchyTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowClipmapInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowDefaultClipmapCount,
        sizeof(CLodVirtualShadowClipmapInfo),
        false,
        false,
        false,
        false);
    m_shadowClipmapInfoBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Clipmap Info Buffer"));
    m_shadowClipmapInfoBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowClipmapInfoBuffer })
        .add<CLodVirtualShadowClipmapInfoTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowRuntimeStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodVirtualShadowRuntimeState), true, false, false, false);
    m_shadowRuntimeStateBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Runtime State Buffer"));
    m_shadowRuntimeStateBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowRuntimeStateBuffer })
        .add<CLodVirtualShadowRuntimeStateTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowStatsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodVirtualShadowStats), true, false, false, false);
    m_shadowStatsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Stats Buffer"));
    m_shadowStatsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowStatsBuffer })
        .add<CLodVirtualShadowStatsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowVirtualResourcesNeedReset = true;
}

void CLodExtension::TagCoreResourceUsages()
{
    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };

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
    tagBufferUsage(m_rasterBucketsIndirectArgsBufferPhase2, "Cluster LOD rasterization");
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
    tagBufferUsage(m_deepVisibilityNodesBuffer, "Cluster LOD deep visibility");
    tagBufferUsage(m_deepVisibilityCounterBuffer, "Cluster LOD deep visibility");
    tagBufferUsage(m_deepVisibilityOverflowCounterBuffer, "Cluster LOD deep visibility");
    tagBufferUsage(m_deepVisibilityStatsBuffer, "Cluster LOD deep visibility");
}

void CLodExtension::TagShadowResourceUsages()
{
    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };
    auto tagTextureUsage = [](const std::shared_ptr<PixelBuffer>& texture, std::string_view usage) {
        if (texture) {
            rg::memory::SetResourceUsageHint(*texture, std::string(usage));
        }
    };

    tagTextureUsage(m_shadowPageTableTexture, "Cluster LOD virtual shadow maps");
    tagTextureUsage(m_shadowPhysicalPagesTexture, "Cluster LOD virtual shadow maps");
    tagTextureUsage(m_shadowDirtyPageHierarchyTexture, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPageMetadataBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowAllocationRequestsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowAllocationCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowAllocationIndirectArgsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowFreePhysicalPagesBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowReusablePhysicalPagesBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPageListHeaderBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowDirtyPageFlagsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowClipmapInfoBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowRuntimeStateBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowStatsBuffer, "Cluster LOD virtual shadow maps");
}

void CLodExtension::ReleaseBufferBackings()
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
    releaseBufferBacking(m_rasterBucketsIndirectArgsBufferPhase2);
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
    releaseBufferBacking(m_reyesDiceQueuePhase1CountBuffer);
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
    releaseBufferBacking(m_shadowPageMetadataBuffer);
    releaseBufferBacking(m_shadowAllocationRequestsBuffer);
    releaseBufferBacking(m_shadowAllocationCountBuffer);
    releaseBufferBacking(m_shadowAllocationIndirectArgsBuffer);
    releaseBufferBacking(m_shadowFreePhysicalPagesBuffer);
    releaseBufferBacking(m_shadowReusablePhysicalPagesBuffer);
    releaseBufferBacking(m_shadowPageListHeaderBuffer);
    releaseBufferBacking(m_shadowDirtyPageFlagsBuffer);
    releaseBufferBacking(m_shadowClipmapInfoBuffer);
    releaseBufferBacking(m_shadowRuntimeStateBuffer);
    releaseBufferBacking(m_shadowStatsBuffer);
}

void CLodExtension::ReleaseShadowResourceBackings()
{
    auto releaseTextureBacking = [](const std::shared_ptr<PixelBuffer>& texture) {
        if (texture) {
            texture->Dematerialize();
        }
    };

    releaseTextureBacking(m_shadowPageTableTexture);
    releaseTextureBacking(m_shadowPhysicalPagesTexture);
    releaseTextureBacking(m_shadowDirtyPageHierarchyTexture);
    m_shadowVirtualResourcesNeedReset = true;
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
    const ReyesResourceSizing reyesResourceSizing = BuildReyesResourceSizing(traits, m_maxVisibleClusters);
    const bool usesPhase2ReyesResources = traits.usesPhase2OcclusionReplay;
    m_reyesFullClusterOutputCapacity = reyesResourceSizing.fullClusterOutputCapacity;
    m_reyesOwnedClusterCapacity = reyesResourceSizing.ownedClusterCapacity;
    m_reyesSplitQueueCapacity = reyesResourceSizing.splitQueueCapacity;
    m_reyesDiceQueueCapacity = reyesResourceSizing.diceQueueCapacity;
    m_reyesDiceQueuePhysicalCapacity = reyesResourceSizing.diceQueuePhysicalCapacity;
    m_reyesRasterWorkCapacity = reyesResourceSizing.rasterWorkCapacity;
    m_reyesOwnershipBitsetWordCount = reyesResourceSizing.ownershipBitsetWordCount;
    m_reyesRequestedBudgetBytes = reyesResourceSizing.requestedBudgetBytes;
    m_reyesAllocatedBudgetBytes = reyesResourceSizing.allocatedBudgetBytes;
    m_reyesBudgetLimited = reyesResourceSizing.budgetLimited;

    m_reyesFullClusterOutputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesFullClusterOutputCapacity, sizeof(CLodReyesFullClusterOutput), true, false, false, false);
    m_reyesFullClusterOutputsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Full Cluster Outputs Buffer"));

    m_reyesFullClusterOutputsCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesFullClusterOutputsCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Full Cluster Outputs Counter Buffer"));
    m_reyesFullClusterOutputsCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesFullClusterOutputsCounterBuffer })
        .add<CLodReyesFullClustersCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesOwnedClustersBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesOwnedClusterCapacity, sizeof(CLodReyesOwnedClusterEntry), true, false, false, false);
    m_reyesOwnedClustersBuffer->SetName(MakeVariantResourceName(traits, "Reyes Owned Clusters Buffer"));

    m_reyesOwnedClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesOwnedClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Owned Clusters Counter Buffer"));

    m_reyesOwnershipBitsetBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesOwnershipBitsetWordCount, sizeof(uint32_t), true, false, false, false);
    m_reyesOwnershipBitsetBuffer->SetName(MakeVariantResourceName(traits, "Reyes Ownership Bitset Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesOwnershipBitsetBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(m_reyesOwnershipBitsetWordCount, sizeof(uint32_t), true, false, false, false);
        m_reyesOwnershipBitsetBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Ownership Bitset Buffer Phase2"));
    }

    m_reyesClassifyIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesClassifyIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Classify Indirect Args Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesClassifyIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
        m_reyesClassifyIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Classify Indirect Args Buffer Phase2"));
    }

    m_reyesSplitIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesSplitIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Split Indirect Args Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesSplitIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
        m_reyesSplitIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Split Indirect Args Buffer Phase2"));
    }

    m_reyesSplitQueueBufferA = CreateAliasedUnmaterializedStructuredBuffer(m_reyesSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry), true, false);
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

    m_reyesSplitQueueBufferB = CreateAliasedUnmaterializedStructuredBuffer(m_reyesSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry), true, false);
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

    m_reyesDiceQueueBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesDiceQueuePhysicalCapacity, sizeof(CLodReyesDiceQueueEntry), true, false);
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

    if (usesPhase2ReyesResources) {
        m_reyesDiceQueuePhase1CountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), false, false, false, false);
        m_reyesDiceQueuePhase1CountBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Phase1 Count Buffer"));
    }

    m_reyesDiceQueueOverflowBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesDiceQueueOverflowBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Overflow Buffer"));
    m_reyesDiceQueueOverflowBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesDiceQueueOverflowBuffer })
        .add<CLodReyesDiceQueueOverflowTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesRasterWorkBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry), true, false, false, false);
    m_reyesRasterWorkBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Buffer"));

    m_reyesRasterWorkCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_reyesRasterWorkCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Counter Buffer"));

    m_reyesRasterWorkIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesRasterWorkIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Indirect Args Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesRasterWorkBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry), true, false, false, false);
        m_reyesRasterWorkBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Buffer Phase2"));

        m_reyesRasterWorkCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_reyesRasterWorkCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Counter Buffer Phase2"));

        m_reyesRasterWorkIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
        m_reyesRasterWorkIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Indirect Args Buffer Phase2"));
    }

    m_reyesDiceIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
    m_reyesDiceIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Indirect Args Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesDiceIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false, false);
        m_reyesDiceIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Dice Indirect Args Buffer Phase2"));
    }

    m_reyesTelemetryBufferPhase1 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesTelemetry), true, false, false, false);
    m_reyesTelemetryBufferPhase1->SetName(MakeVariantResourceName(traits, "Reyes Telemetry Buffer Phase1"));
    m_reyesTelemetryBufferPhase1->GetECSEntity()
        .set<Components::Resource>({ m_reyesTelemetryBufferPhase1 })
        .add<CLodReyesTelemetryBufferPhase1Tag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    if (usesPhase2ReyesResources) {
        m_reyesTelemetryBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesTelemetry), true, false, false, false);
        m_reyesTelemetryBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Telemetry Buffer Phase2"));
        m_reyesTelemetryBufferPhase2->GetECSEntity()
            .set<Components::Resource>({ m_reyesTelemetryBufferPhase2 })
            .add<CLodReyesTelemetryBufferPhase2Tag>()
            .add<CLodExtensionTypeTag>(typeEntity);
    }

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
    tagBufferUsage(m_reyesDiceQueuePhase1CountBuffer, "Cluster LOD Reyes");
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
    const auto& traits = GetVariantTraits(type);

    if (traits.ownsStreaming) {
        m_streamingSystem = std::make_unique<CLodStreamingSystem>();
    }

    InitializeCoreResources();

    if (!IsReyesTessellationDisabled()) {
        EnsureReyesResourcesInitialized();
    }

    InitializeDeepVisibilityResources();
    InitializeShadowResources();
    TagCoreResourceUsages();
    TagShadowResourceUsages();
}

void CLodExtension::PrepareForBuild(RenderGraph& rg)
{
    if (m_type == CLodExtensionType::Shadow && !m_providerRegisteredForCurrentRegistry) {
        rg.RegisterProvider(this);
        m_providerRegisteredForCurrentRegistry = true;
    }
}

void CLodExtension::Initialize(RenderGraph& rg)
{
    PrepareForBuild(rg);

    if (m_streamingSystem) {
        m_streamingSystem->Initialize(rg);
    }
}

void CLodExtension::OnRegistryReset(ResourceRegistry* reg)
{
    m_providerRegisteredForCurrentRegistry = false;
    ReleaseBufferBackings();
    ReleaseShadowResourceBackings();
    m_shadowVirtualResourcesNeedReset = true;

    SyncReyesResourceEntities(!IsReyesTessellationDisabled());

    if (m_streamingSystem) {
        m_streamingSystem->OnRegistryReset(reg);
    }
}

std::shared_ptr<Resource> CLodExtension::ProvideResource(ResourceIdentifier const& key)
{
    if (m_type != CLodExtensionType::Shadow) {
        return nullptr;
    }

    if (key == Builtin::Shadows::CLodPageTable) {
        return m_shadowPageTableTexture;
    }

    if (key == Builtin::Shadows::CLodPhysicalPages) {
        return m_shadowPhysicalPagesTexture;
    }

    if (key == Builtin::Shadows::CLodClipmapInfo) {
        return m_shadowClipmapInfoBuffer;
    }

    return nullptr;
}

std::vector<ResourceIdentifier> CLodExtension::GetSupportedKeys()
{
    if (m_type != CLodExtensionType::Shadow) {
        return {};
    }

    return {
        Builtin::Shadows::CLodPageTable,
        Builtin::Shadows::CLodPhysicalPages,
        Builtin::Shadows::CLodClipmapInfo,
    };
}

void CLodExtension::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    PrepareForBuild(rg);

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
    const uint32_t reyesSplitQueueCapacity = m_reyesSplitQueueCapacity;
    const uint32_t reyesDiceQueueCapacity = m_reyesDiceQueueCapacity;
    const uint32_t reyesRasterWorkCapacity = m_reyesRasterWorkCapacity;
    const auto workGraphMode = forceHardwareOnly
        ? HierarchialCullingWorkGraphMode::HardwareOnly
        : GetCullingWorkGraphMode(softwareRasterMode);
    const auto renderPhase = RenderPhase(traits.renderPhaseName.data());
    const std::shared_ptr<Buffer> reyesOwnershipBitsetBuffer = disableReyesTessellation ? nullptr : m_reyesOwnershipBitsetBuffer;
    const std::shared_ptr<Buffer> reyesOwnershipBitsetBufferPhase2 = disableReyesTessellation ? nullptr : m_reyesOwnershipBitsetBufferPhase2;

    std::string shadowAllocationPassName;
    std::string shadowClearDirtyBitsAfterPassName;
    if (traits.type == CLodExtensionType::Shadow) {
        const std::string shadowSetupPassName = MakeVariantPassName(traits, "VirtualShadowSetupPass");
        auto shadowSetupPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowSetupPassName,
            std::make_shared<VirtualShadowMapSetupPass>(
                m_shadowPageTableTexture,
                m_shadowPageMetadataBuffer,
                m_shadowAllocationCountBuffer,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowStatsBuffer,
                m_shadowRuntimeStateBuffer,
                m_shadowVirtualResourcesNeedReset));
        shadowSetupPassDesc.At(RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass"));
        outPasses.push_back(std::move(shadowSetupPassDesc));
        m_shadowVirtualResourcesNeedReset = false;

        const std::string shadowMarkPagesPassName = MakeVariantPassName(traits, "VirtualShadowMarkPagesPass");
        auto shadowMarkPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowMarkPagesPassName,
            std::make_shared<VirtualShadowMapMarkPagesPass>(
                m_shadowAllocationRequestsBuffer,
                m_shadowAllocationCountBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowPageTableTexture,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowPageMetadataBuffer,
                m_shadowStatsBuffer));
        shadowMarkPagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowSetupPassName));
        outPasses.push_back(std::move(shadowMarkPagesPassDesc));

        const std::string shadowBuildPageListsPassName = MakeVariantPassName(traits, "VirtualShadowBuildPageListsPass");
        auto shadowBuildPageListsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowBuildPageListsPassName,
            std::make_shared<VirtualShadowMapBuildPageListsPass>(
                m_shadowPageMetadataBuffer,
                m_shadowFreePhysicalPagesBuffer,
                m_shadowReusablePhysicalPagesBuffer,
                m_shadowPageListHeaderBuffer));
        shadowBuildPageListsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowMarkPagesPassName));
        outPasses.push_back(std::move(shadowBuildPageListsPassDesc));

        const std::string shadowBuildDispatchArgsPassName = MakeVariantPassName(traits, "VirtualShadowBuildAllocationDispatchArgsPass");
        auto shadowBuildDispatchArgsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowBuildDispatchArgsPassName,
            std::make_shared<ReyesCreateDispatchArgsPass>(
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                nullptr,
                64u,
                CLodVirtualShadowDefaultPhysicalPageCount));
        shadowBuildDispatchArgsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowBuildPageListsPassName));
        outPasses.push_back(std::move(shadowBuildDispatchArgsPassDesc));

        const std::string shadowPreAllocateStatsPassName = MakeVariantPassName(traits, "VirtualShadowPreAllocateStatsPass");
        auto shadowPreAllocateStatsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowPreAllocateStatsPassName,
            std::make_shared<VirtualShadowMapGatherStatsPass>(
                m_shadowPageTableTexture,
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                m_shadowPageListHeaderBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowStatsBuffer,
                true));
        shadowPreAllocateStatsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowBuildDispatchArgsPassName));
        outPasses.push_back(std::move(shadowPreAllocateStatsPassDesc));

        shadowAllocationPassName = MakeVariantPassName(traits, "VirtualShadowAllocatePagesPass");
        auto shadowAllocationPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowAllocationPassName,
            std::make_shared<VirtualShadowMapAllocatePagesPass>(
                m_shadowAllocationRequestsBuffer,
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                m_shadowPageTableTexture,
                m_shadowPageMetadataBuffer,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowFreePhysicalPagesBuffer,
                m_shadowReusablePhysicalPagesBuffer,
                m_shadowPageListHeaderBuffer));
            shadowAllocationPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowPreAllocateStatsPassName));
        outPasses.push_back(std::move(shadowAllocationPassDesc));

        const std::string shadowGatherStatsPassName = MakeVariantPassName(traits, "VirtualShadowGatherStatsPass");
        auto shadowGatherStatsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowGatherStatsPassName,
            std::make_shared<VirtualShadowMapGatherStatsPass>(
                m_shadowPageTableTexture,
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                m_shadowPageListHeaderBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowStatsBuffer,
                false));
        shadowGatherStatsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowAllocationPassName));
        outPasses.push_back(std::move(shadowGatherStatsPassDesc));

        const std::string shadowClearPagesPassName = MakeVariantPassName(traits, "VirtualShadowClearPagesPass");
        auto shadowClearPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowClearPagesPassName,
            std::make_shared<VirtualShadowMapClearPagesPass>(
                m_shadowPhysicalPagesTexture,
                m_shadowDirtyPageFlagsBuffer));
        shadowClearPagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowGatherStatsPassName));
        outPasses.push_back(std::move(shadowClearPagesPassDesc));

        const std::string shadowDirtyHierarchyPassName = MakeVariantPassName(traits, "VirtualShadowDirtyHierarchyPass");
        auto shadowDirtyHierarchyPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowDirtyHierarchyPassName,
            std::make_shared<VirtualShadowMapDirtyHierarchyPass>(
                m_shadowPageTableTexture,
                m_shadowDirtyPageHierarchyTexture));
        shadowDirtyHierarchyPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowClearPagesPassName));
        outPasses.push_back(std::move(shadowDirtyHierarchyPassDesc));

    }

    std::shared_ptr<ResourceGroup> slabGroup = GetSlabResourceGroup();

    HierarchialCullingPassInputs cullPassInputs;
    cullPassInputs.isFirstPass = true;
    cullPassInputs.maxVisibleClusters = m_maxVisibleClusters;
    cullPassInputs.workGraphMode = workGraphMode;
    cullPassInputs.renderPhase = renderPhase;
    cullPassInputs.clodOnlyWorkloads = true;
    cullPassInputs.useShadowCascadeViews = (traits.type == CLodExtensionType::Shadow);
    cullPassInputs.rasterOutputKind = traits.rasterOutputKind;
    auto cullPassDesc = RenderGraph::ExternalPassDesc::Compute(
        MakeVariantPassName(traits, "HierarchialCullingPass1"),
        std::make_shared<HierarchialCullingPass>(
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
            traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
            slabGroup));
    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly ||
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        cullPassDesc.At(RenderGraph::ExternalInsertPoint::After("CLodOpaque::LinearDepthDownsamplePass2"));
    }
    else {
        if (traits.type == CLodExtensionType::Shadow) {
            cullPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowAllocationPassName));
        }
        else {
            cullPassDesc.At(RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass"));
        }
    }
    outPasses.push_back(std::move(cullPassDesc));

        if (traits.scheduleMode != CLodVariantTraits::ScheduleMode::SinglePassCullOnly && !disableReyesTessellation) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesTessellationTableUploadPass"),
                    std::make_shared<ReyesTessellationTableUploadPass>(
                        m_reyesTessTableConfigsBuffer,
                        m_reyesTessTableVerticesBuffer,
                        m_reyesTessTableTrianglesBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesQueueResetPass1"),
                    std::make_shared<ReyesQueueResetPass>(
                        m_reyesFullClusterOutputsCounterBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
                        std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceQueueOverflowBuffer,
                        reyesOwnershipBitsetBuffer,
                        m_reyesTelemetryBufferPhase1,
                        1u,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateClassifyDispatchArgsPass1"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_visibleClustersCounterBuffer,
                        m_reyesClassifyIndirectArgsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesClassifyPass1"),
                    std::make_shared<ReyesClassifyPass>(
                        m_visibleClustersBuffer,
                        m_visibleClustersCounterBuffer,
                        nullptr,
                        m_reyesFullClusterOutputsBuffer,
                        m_reyesFullClusterOutputsCounterBuffer,
                        m_reyesFullClusterOutputCapacity,
                        m_reyesOwnedClustersBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesOwnedClusterCapacity,
                        reyesOwnershipBitsetBuffer,
                        m_reyesClassifyIndirectArgsBuffer,
                        m_reyesTelemetryBufferPhase1,
                        1u)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateSeedDispatchArgsPass1"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesSplitIndirectArgsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesSeedPatchesPass1"),
                    std::make_shared<ReyesSeedPatchesPass>(
                        m_visibleClustersBuffer,
                        m_reyesOwnedClustersBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesSplitQueueBufferA,
                        m_reyesSplitQueueCounterBufferA,
                        m_reyesSplitQueueOverflowBufferA,
                        m_reyesSplitIndirectArgsBuffer,
                        slabGroup,
                        reyesSplitQueueCapacity,
                        1u)));

            const std::shared_ptr<Buffer> reyesSplitBuffers[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
            const std::shared_ptr<Buffer> reyesSplitCounters[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
            const std::shared_ptr<Buffer> reyesSplitOverflows[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
            for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
                const uint32_t inputIndex = splitPassIndex & 1u;
                const uint32_t outputIndex = inputIndex ^ 1u;

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesCreateSplitDispatchArgsPass1_" + std::to_string(splitPassIndex)),
                        std::make_shared<ReyesCreateDispatchArgsPass>(
                            reyesSplitCounters[inputIndex],
                            m_reyesSplitIndirectArgsBuffer)));

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesSplitPass1_" + std::to_string(splitPassIndex)),
                        std::make_shared<ReyesSplitPass>(
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
                            1u)));
            }

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateDiceDispatchArgsPass1"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceIndirectArgsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesDicePass1"),
                    std::make_shared<ReyesDicePass>(
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        nullptr,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesDiceIndirectArgsBuffer,
                        m_reyesTelemetryBufferPhase1,
                        reyesDiceQueueCapacity,
                        1u)));

            if (traits.usesPhase2OcclusionReplay) {
                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Copy(
                        MakeVariantPassName(traits, "ReyesCopyDiceCountPass1"),
                        std::make_shared<ReyesCopyCounterPass>(
                            m_reyesDiceQueueCounterBuffer,
                            m_reyesDiceQueuePhase1CountBuffer)));
            }

            if (traits.type == CLodExtensionType::VisiblityBuffer || traits.type == CLodExtensionType::AlphaBlend) {
                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesBuildRasterWorkPass1"),
                        std::make_shared<ReyesBuildRasterWorkPass>(
                            m_reyesDiceQueueBuffer,
                            m_reyesDiceQueueCounterBuffer,
                            nullptr,
                            m_reyesTessTableConfigsBuffer,
                            m_reyesRasterWorkBuffer,
                            m_reyesRasterWorkCounterBuffer,
                            m_reyesDiceIndirectArgsBuffer,
                            m_reyesTelemetryBufferPhase1,
                            reyesRasterWorkCapacity)));

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesCreateRasterWorkDispatchArgsPass1"),
                        std::make_shared<ReyesCreateDispatchArgsPass>(
                            m_reyesRasterWorkCounterBuffer,
                            m_reyesRasterWorkIndirectArgsBuffer)));

                if (traits.type == CLodExtensionType::VisiblityBuffer) {
                    outPasses.push_back(
                        RenderGraph::ExternalPassDesc::Compute(
                            MakeVariantPassName(traits, "ReyesPatchRasterPass1"),
                            std::make_shared<ReyesPatchRasterizationPass>(
                                m_visibleClustersBuffer,
                                m_reyesDiceQueueBuffer,
                                m_reyesDiceQueueCounterBuffer,
                                m_reyesRasterWorkBuffer,
                                m_reyesRasterWorkCounterBuffer,
                                m_reyesTessTableConfigsBuffer,
                                m_reyesTessTableVerticesBuffer,
                                m_reyesTessTableTrianglesBuffer,
                                m_viewRasterInfoBuffer,
                                m_reyesRasterWorkIndirectArgsBuffer,
                                m_reyesTelemetryBufferPhase1,
                                slabGroup,
                                m_maxVisibleClusters,
                                1u,
                                CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters))));
                }
            }
        }

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsHistogramPass1"),
            std::make_shared<RasterBucketHistogramPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_histogramIndirectCommand,
                m_rasterBucketsHistogramBuffer,
                reyesOwnershipBitsetBuffer)));

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsPrefixScanPass1"),
            std::make_shared<RasterBucketBlockScanPass>(
                m_rasterBucketsHistogramBuffer,
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer)));

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass1"),
            std::make_shared<RasterBucketBlockOffsetsPass>(
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer,
                m_rasterBucketsScannedBlockSumsBuffer,
                m_rasterBucketsTotalCountBufferPhase1)));

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass1"),
            std::make_shared<RasterBucketCompactAndArgsPass>(
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
                false)));

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly) {
        return;
    }

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Render(
                MakeVariantPassName(traits, "ClearDeepVisibilityPass"),
                std::make_shared<ClearDeepVisibilityPass>(
                    m_deepVisibilityCounterBuffer,
                    m_deepVisibilityOverflowCounterBuffer,
                    m_deepVisibilityStatsBuffer)));

        if (!disableReyesTessellation) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesPatchRasterPass1"),
                    std::make_shared<ReyesDeepVisibilityRasterizationPass>(
                        m_visibleClustersBuffer,
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesRasterWorkBuffer,
                        m_reyesRasterWorkCounterBuffer,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesTessTableVerticesBuffer,
                        m_reyesTessTableTrianglesBuffer,
                        m_reyesRasterWorkIndirectArgsBuffer,
                        m_reyesTelemetryBufferPhase1,
                        m_deepVisibilityNodesBuffer,
                        m_deepVisibilityCounterBuffer,
                        m_deepVisibilityOverflowCounterBuffer,
                        slabGroup,
                        MakeVariantResourceName(traits, "Reyes Deep Visibility View Raster Info Buffer"),
                        CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters))));
        }

        ClusterRasterizationPassInputs rasterizePassInputs;
        rasterizePassInputs.clearGbuffer = false;
        rasterizePassInputs.wireframe = false;
        rasterizePassInputs.renderPhase = renderPhase;
        rasterizePassInputs.outputKind = traits.rasterOutputKind;
        auto rasterizeDeepVisibilityPassDesc = RenderGraph::ExternalPassDesc::Render(
            MakeVariantPassName(traits, "RasterizeClustersPass1"),
            std::make_shared<ClusterRasterizationPass>(
                rasterizePassInputs,
                m_compactedVisibleClustersBuffer,
                m_rasterBucketsHistogramBuffer,
                m_rasterBucketsIndirectArgsBuffer,
                m_sortedToUnsortedMappingBuffer,
                m_deepVisibilityNodesBuffer,
                m_deepVisibilityCounterBuffer,
                m_deepVisibilityOverflowCounterBuffer,
                slabGroup));
        rasterizeDeepVisibilityPassDesc.GeometryPass();
        outPasses.push_back(std::move(rasterizeDeepVisibilityPassDesc));

        auto resolveDeepVisibilityPassDesc = RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "DeepVisibilityResolvePass"),
            std::make_shared<DeepVisibilityResolvePass>(
                m_visibleClustersBuffer,
                disableReyesTessellation ? nullptr : m_reyesDiceQueueBuffer,
                disableReyesTessellation ? nullptr : m_reyesTessTableConfigsBuffer,
                disableReyesTessellation ? nullptr : m_reyesTessTableVerticesBuffer,
                disableReyesTessellation ? nullptr : m_reyesTessTableTrianglesBuffer,
                m_deepVisibilityNodesBuffer,
                m_deepVisibilityCounterBuffer,
                m_deepVisibilityOverflowCounterBuffer,
                m_deepVisibilityStatsBuffer,
                CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters)));
            auto resolveInsertPoint = RenderGraph::ExternalInsertPoint::After("LightCullingPass");
            resolveInsertPoint.before.push_back("PPLLResolvePass");
            resolveDeepVisibilityPassDesc.At(std::move(resolveInsertPoint));
        outPasses.push_back(std::move(resolveDeepVisibilityPassDesc));
        return;
    }

    ClusterRasterizationPassInputs rasterizePassInputs;
    rasterizePassInputs.clearGbuffer = true;
    rasterizePassInputs.wireframe = false;
    rasterizePassInputs.renderPhase = renderPhase;
    rasterizePassInputs.outputKind = traits.rasterOutputKind;
    auto shadowPageTable = traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_shadowPageTableTexture : nullptr;
    auto shadowPhysicalPages = traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_shadowPhysicalPagesTexture : nullptr;
    auto shadowClipmapInfo = traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_shadowClipmapInfoBuffer : nullptr;
    auto rasterizePassDesc = RenderGraph::ExternalPassDesc::Render(
        MakeVariantPassName(traits, "RasterizeClustersPass1"),
        std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            nullptr,
            nullptr,
            nullptr,
            slabGroup,
            shadowPageTable,
            shadowPhysicalPages,
            shadowClipmapInfo));
    rasterizePassDesc.GeometryPass();
            shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "RasterizeClustersPass1");
    outPasses.push_back(std::move(rasterizePassDesc));

    if (useComputeSWRaster) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW1"),
                std::make_shared<RasterBucketCreateCommandPass>(
                    m_swVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsHistogramPassSW1"),
                std::make_shared<RasterBucketHistogramPass>(
                    m_visibleClustersBuffer,
                    m_swVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferSw,
                    reyesOwnershipBitsetBuffer,
                    nullptr,
                    true,
                    m_maxVisibleClusters,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW1"),
                std::make_shared<RasterBucketBlockScanPass>(
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW1"),
                std::make_shared<RasterBucketBlockOffsetsPass>(
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    m_rasterBucketsScannedBlockSumsBuffer,
                    m_rasterBucketsTotalCountBufferPhase1Sw,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW1"),
                std::make_shared<RasterBucketCompactAndArgsPass>(
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
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "SoftwareRasterizeClustersPass1"),
                std::make_shared<ClusterSoftwareRasterizationPass>(
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsIndirectArgsBuffer,
                    m_sortedToUnsortedMappingBuffer,
                    m_viewRasterInfoBuffer,
                    traits.rasterOutputKind,
                    slabGroup,
                    true)));
                shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass1");
    }

    if (traits.schedulesPerViewDepthCopy) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "LinearDepthCopyPass1"),
                std::make_shared<PerViewLinearDepthCopyPass>()));
    }

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "LinearDepthDownsamplePass1"),
            std::make_shared<DownsamplePass>()));

    if (!traits.usesPhase2OcclusionReplay) {
        return;
    }

    HierarchialCullingPassInputs cullPassInputs2;
    cullPassInputs2.isFirstPass = false;
    cullPassInputs2.maxVisibleClusters = m_maxVisibleClusters;
    cullPassInputs2.workGraphMode = workGraphMode;
    cullPassInputs2.renderPhase = renderPhase;
    cullPassInputs2.clodOnlyWorkloads = true;
    cullPassInputs2.useShadowCascadeViews = (traits.type == CLodExtensionType::Shadow);
    cullPassInputs2.rasterOutputKind = traits.rasterOutputKind;
    auto cullPassDesc2 = RenderGraph::ExternalPassDesc::Compute(
        MakeVariantPassName(traits, "HierarchialCullingPass2"),
        std::make_shared<HierarchialCullingPass>(
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
            traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
            slabGroup,
            m_visibleClustersCounterBuffer,
            m_swVisibleClustersCounterBuffer));
    outPasses.push_back(std::move(cullPassDesc2));

    if (!disableReyesTessellation) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesQueueResetPass2"),
                std::make_shared<ReyesQueueResetPass>(
                    m_reyesFullClusterOutputsCounterBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
                    std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesDiceQueueOverflowBuffer,
                    reyesOwnershipBitsetBufferPhase2,
                    m_reyesTelemetryBufferPhase2,
                    2u,
                    false)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesCreateClassifyDispatchArgsPass2"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_visibleClustersCounterBufferPhase2,
                    m_reyesClassifyIndirectArgsBufferPhase2)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesClassifyPass2"),
                std::make_shared<ReyesClassifyPass>(
                    m_visibleClustersBuffer,
                    m_visibleClustersCounterBufferPhase2,
                    m_visibleClustersCounterBuffer,
                    m_reyesFullClusterOutputsBuffer,
                    m_reyesFullClusterOutputsCounterBuffer,
                    m_reyesFullClusterOutputCapacity,
                    m_reyesOwnedClustersBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesOwnedClusterCapacity,
                    reyesOwnershipBitsetBufferPhase2,
                    m_reyesClassifyIndirectArgsBufferPhase2,
                    m_reyesTelemetryBufferPhase2,
                    2u)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesCreateSeedDispatchArgsPass2"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesSplitIndirectArgsBufferPhase2)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesSeedPatchesPass2"),
                std::make_shared<ReyesSeedPatchesPass>(
                    m_visibleClustersBuffer,
                    m_reyesOwnedClustersBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesSplitQueueBufferA,
                    m_reyesSplitQueueCounterBufferA,
                    m_reyesSplitQueueOverflowBufferA,
                    m_reyesSplitIndirectArgsBufferPhase2,
                    slabGroup,
                    reyesSplitQueueCapacity,
                    2u)));

        const std::shared_ptr<Buffer> reyesSplitBuffers[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
        const std::shared_ptr<Buffer> reyesSplitCounters[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
        const std::shared_ptr<Buffer> reyesSplitOverflows[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
        for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
            const uint32_t inputIndex = splitPassIndex & 1u;
            const uint32_t outputIndex = inputIndex ^ 1u;

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateSplitDispatchArgsPass2_" + std::to_string(splitPassIndex)),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        reyesSplitCounters[inputIndex],
                        m_reyesSplitIndirectArgsBufferPhase2)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesSplitPass2_" + std::to_string(splitPassIndex)),
                    std::make_shared<ReyesSplitPass>(
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
                        2u)));
        }

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesCreateDiceDispatchArgsPass2"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceIndirectArgsBufferPhase2,
                        m_reyesDiceQueuePhase1CountBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesDicePass2"),
                std::make_shared<ReyesDicePass>(
                    m_reyesDiceQueueBuffer,
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesDiceQueuePhase1CountBuffer,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesDiceIndirectArgsBufferPhase2,
                    m_reyesTelemetryBufferPhase2,
                    reyesDiceQueueCapacity,
                    2u)));

        if (traits.type == CLodExtensionType::VisiblityBuffer) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesBuildRasterWorkPass2"),
                    std::make_shared<ReyesBuildRasterWorkPass>(
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceQueuePhase1CountBuffer,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesRasterWorkBufferPhase2,
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_reyesDiceIndirectArgsBufferPhase2,
                        m_reyesTelemetryBufferPhase2,
                        reyesRasterWorkCapacity)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateRasterWorkDispatchArgsPass2"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_reyesRasterWorkIndirectArgsBufferPhase2)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesPatchRasterPass2"),
                    std::make_shared<ReyesPatchRasterizationPass>(
                        m_visibleClustersBuffer,
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesRasterWorkBufferPhase2,
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesTessTableVerticesBuffer,
                        m_reyesTessTableTrianglesBuffer,
                        m_viewRasterInfoBuffer,
                        m_reyesRasterWorkIndirectArgsBufferPhase2,
                        m_reyesTelemetryBufferPhase2,
                        slabGroup,
                        m_maxVisibleClusters,
                        2u,
                        CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters))));
        }
    }

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsHistogramPass2"),
            std::make_shared<RasterBucketHistogramPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBufferPhase2,
                m_histogramIndirectCommand,
                m_rasterBucketsHistogramBufferPhase2,
                reyesOwnershipBitsetBufferPhase2,
                m_visibleClustersCounterBuffer)));

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsPrefixScanPass2"),
            std::make_shared<RasterBucketBlockScanPass>(
                m_rasterBucketsHistogramBufferPhase2,
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer)));

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass2"),
            std::make_shared<RasterBucketBlockOffsetsPass>(
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer,
                m_rasterBucketsScannedBlockSumsBuffer,
                m_rasterBucketsTotalCountBuffer)));

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass2"),
            std::make_shared<RasterBucketCompactAndArgsPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBufferPhase2,
                m_rasterBucketsTotalCountBufferPhase1,
                m_histogramIndirectCommand,
                m_rasterBucketsHistogramBufferPhase2,
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsWriteCursorBufferPhase2,
                m_compactedVisibleClustersBuffer,
                m_rasterBucketsIndirectArgsBufferPhase2,
                m_sortedToUnsortedMappingBuffer,
                reyesOwnershipBitsetBufferPhase2,
                m_maxVisibleClusters,
                true)));

    ClusterRasterizationPassInputs rasterizePassInputs2;
    rasterizePassInputs2.clearGbuffer = false;
    rasterizePassInputs2.wireframe = false;
    rasterizePassInputs2.renderPhase = renderPhase;
    rasterizePassInputs2.outputKind = traits.rasterOutputKind;
    auto rasterizePassDesc2 = RenderGraph::ExternalPassDesc::Render(
        MakeVariantPassName(traits, "RasterizeClustersPass2"),
        std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs2,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBufferPhase2,
            m_rasterBucketsIndirectArgsBufferPhase2,
            m_sortedToUnsortedMappingBuffer,
            nullptr,
            nullptr,
            nullptr,
            slabGroup,
            shadowPageTable,
            shadowPhysicalPages,
            shadowClipmapInfo));
    rasterizePassDesc2.At(RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass"));
    rasterizePassDesc2.GeometryPass();
            shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "RasterizeClustersPass2");
    outPasses.push_back(std::move(rasterizePassDesc2));

    if (useComputeSWRaster) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW2"),
                std::make_shared<RasterBucketCreateCommandPass>(
                    m_swVisibleClustersCounterBufferPhase2,
                    m_histogramIndirectCommand,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsHistogramPassSW2"),
                std::make_shared<RasterBucketHistogramPass>(
                    m_visibleClustersBuffer,
                    m_swVisibleClustersCounterBufferPhase2,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    reyesOwnershipBitsetBufferPhase2,
                    m_swVisibleClustersCounterBuffer,
                    true,
                    m_maxVisibleClusters,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW2"),
                std::make_shared<RasterBucketBlockScanPass>(
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW2"),
                std::make_shared<RasterBucketBlockOffsetsPass>(
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    m_rasterBucketsScannedBlockSumsBuffer,
                    m_rasterBucketsTotalCountBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW2"),
                std::make_shared<RasterBucketCompactAndArgsPass>(
                    m_visibleClustersBuffer,
                    m_swVisibleClustersCounterBufferPhase2,
                    m_rasterBucketsTotalCountBufferPhase1Sw,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsWriteCursorBufferPhase2Sw,
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsIndirectArgsBufferPhase2,
                    m_sortedToUnsortedMappingBuffer,
                    reyesOwnershipBitsetBufferPhase2,
                    m_maxVisibleClusters,
                    true,
                    true,
                    true,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "SoftwareRasterizeClustersPass2"),
                std::make_shared<ClusterSoftwareRasterizationPass>(
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    m_rasterBucketsIndirectArgsBufferPhase2,
                    m_sortedToUnsortedMappingBuffer,
                    m_viewRasterInfoBuffer,
                    traits.rasterOutputKind,
                    slabGroup,
                    true)));
                shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass2");
    }

    if (traits.schedulesPerViewDepthCopy) {
        auto depthCopyPassDesc2 = RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "LinearDepthCopyPass2"),
            std::make_shared<PerViewLinearDepthCopyPass>());
        depthCopyPassDesc2.At(RenderGraph::ExternalInsertPoint::Before("DeferredShadingPass"));
        outPasses.push_back(std::move(depthCopyPassDesc2));
    }

    if (traits.type == CLodExtensionType::Shadow && !shadowClearDirtyBitsAfterPassName.empty()) {
        const std::string shadowClearDirtyBitsPassName = MakeVariantPassName(traits, "VirtualShadowClearDirtyBitsPass");
        auto shadowClearDirtyBitsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowClearDirtyBitsPassName,
            std::make_shared<VirtualShadowMapClearDirtyBitsPass>(m_shadowPageTableTexture));
        shadowClearDirtyBitsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowClearDirtyBitsAfterPassName));
        outPasses.push_back(std::move(shadowClearDirtyBitsPassDesc));
    }

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "LinearDepthDownsamplePass2"),
            std::make_shared<DownsamplePass>()));
}

void CLodExtension::GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    if (m_streamingSystem) {
        m_streamingSystem->GatherFramePasses(rg, outPasses);
    }
}
