#pragma once

#include <rhi.h>
#include <rhi_interop_dx12.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <unordered_set>
#include <vector>

#include "Render/RenderGraph/RenderGraph.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MeshManager.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Resources/Buffers/Buffer.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodRootConstants.h"

static constexpr const char* CLodStreamingMeshManagerGetterSettingName = "getMeshManager";
static constexpr const char* CLodStreamingCpuUploadBudgetSettingName = "clodStreamingCpuUploadBudgetRequests";
static constexpr const char* CLodStreamingResidentBudgetSettingName = "clodStreamingResidentBudgetGroups";

struct RasterBucketsHistogramIndirectCommand
{
    unsigned int clusterCount;
    unsigned int dispatchXDimension;
    unsigned int dispatchX, dispatchY, dispatchZ;
};

struct RasterizeClustersCommand
{
    unsigned int baseClusterOffset;
    unsigned int xDim;
    unsigned int rasterBucketID;
    unsigned int dispatchX, dispatchY, dispatchZ;
};

struct CLodViewRasterInfo
{
    uint32_t visibilityUAVDescriptorIndex;
    uint32_t scissorMinX;
    uint32_t scissorMinY;
    uint32_t scissorMaxX;
    uint32_t scissorMaxY;
    float viewportScaleX;
    float viewportScaleY;
    uint32_t pad0;

    friend bool operator==(const CLodViewRasterInfo&, const CLodViewRasterInfo&) = default;
};

static constexpr uint32_t CLodReplayBufferSizeBytes = 8u * 1024u * 1024u;
static constexpr uint32_t CLodReplayBufferNumUints = CLodReplayBufferSizeBytes / sizeof(uint32_t);
static constexpr uint32_t CLodMaxViewDepthIndices = 512u;
static constexpr uint32_t CLodStreamingInitialGroupCapacity = 1024u;
static constexpr uint32_t CLodStreamingRequestCapacity = (1u << 16);

static constexpr uint32_t CLodBitsetWordCount(uint32_t bitCount) {
    return (bitCount + 31u) / 32u;
}

static uint32_t CLodRoundUpCapacity(uint32_t required) {
    uint32_t capacity = CLodStreamingInitialGroupCapacity;
    while (capacity < required) {
        capacity *= 2u;
    }
    return capacity;
}

inline std::shared_ptr<Buffer> CreateAliasedUnmaterializedStructuredBuffer(
    uint32_t numElements,
    uint32_t elementSize,
    bool unorderedAccess = true,
    bool unorderedAccessCounter = false,
    bool createNonShaderVisibleUAV = false,
    bool allowAlias = true)
{
    auto buffer = Buffer::CreateUnmaterializedStructuredBuffer(
        numElements,
        elementSize,
        unorderedAccess,
        unorderedAccessCounter,
        createNonShaderVisibleUAV,
        rhi::HeapType::DeviceLocal);
    buffer->SetAllowAlias(allowAlias);
    return buffer;
}

class CLodExtension final : public RenderGraph::IRenderGraphExtension {
public:
    explicit CLodExtension(CLodExtensionType type, uint64_t maxVisibleClusters) : m_maxVisibleClusters(maxVisibleClusters) {
    m_streamingNonResidentBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingActiveGroupsBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingPinnedGroupsBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingEvictionExemptBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingResidencyInitializedBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingLruCurrentStampByGroup.assign(m_streamingStorageGroupCapacity, 0ull);
    m_streamingRequestsInProgressBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingNonResidentBitsUploadPending = true;

        try {
            auto getter = SettingsManager::GetInstance().getSettingGetter<std::function<MeshManager*()>>(CLodStreamingMeshManagerGetterSettingName);
            m_getMeshManager = getter();
        }
        catch (...) {
        }

        try {
            auto getFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight");
            m_streamingReadbackRingSize = std::max<uint32_t>(getFramesInFlight(), 1u);
        }
        catch (...) {
            m_streamingReadbackRingSize = 2u;
        }

        try {
            auto getBudget = SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodStreamingCpuUploadBudgetSettingName);
            m_streamingCpuUploadBudgetRequests = std::max(getBudget(), 1u);
        }
        catch (...) {
            m_streamingCpuUploadBudgetRequests = 64u;
        }

        try {
            auto getResidentBudget = SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodStreamingResidentBudgetSettingName);
            m_streamingResidentBudgetGroups = std::max(getResidentBudget(), 1u);
        }
        catch (...) {
            m_streamingResidentBudgetGroups = std::numeric_limits<uint32_t>::max();
        }

        m_streamingLoadReadbackSlots.assign(m_streamingReadbackRingSize, {});

        // Initialize global entity for extension type
        auto ecsWorld = ECSManager::GetInstance().GetWorld();
        // TODO: Better way to do this? Weird global initialization
        switch (type) {
        case CLodExtensionType::VisiblityBuffer:
            if (ecsWorld.component<CLodExtensionVisibilityBufferTag>().has(flecs::Exclusive)) {
                // Already initialized
                break;
            }
            ecsWorld.component<CLodExtensionVisibilityBufferTag>().add(flecs::Exclusive);
            ecsWorld.add<CLodExtensionVisibilityBufferTag>();
            break;
        case CLodExtensionType::Shadow:
            if (ecsWorld.component<CLodExtensionShadowTag>().has(flecs::Exclusive)) {
                // Already initialized
                break;
            }
            ecsWorld.component<CLodExtensionShadowTag>().add(flecs::Exclusive);
            ecsWorld.add<CLodExtensionShadowTag>();
            break;
        }

        m_visibleClustersBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(maxVisibleClusters), sizeof(VisibleCluster), true, false);
        m_visibleClustersBuffer->SetName("CLod Visible Clusters Buffer (uncompacted)");
        m_histogramIndirectCommand = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterBucketsHistogramIndirectCommand), true, false);
        m_histogramIndirectCommand->SetName("CLod Raster Buckets Histogram Indirect Command Buffer");
        m_rasterBucketsHistogramBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsHistogramBuffer->SetName("Raster bucket histogram");

        flecs::entity typeEntity;
        switch (type) {
        case CLodExtensionType::VisiblityBuffer:
            typeEntity = ecsWorld.entity<CLodExtensionVisibilityBufferTag>();
            break;
        case CLodExtensionType::Shadow:
            typeEntity = ecsWorld.entity<CLodExtensionShadowTag>();
            break;
        }

        m_visibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
        m_visibleClustersCounterBuffer->SetName("CLod Visible Clusters Counter Buffer");
        m_visibleClustersCounterBuffer->GetECSEntity()
            .set<Components::Resource>({ m_visibleClustersCounterBuffer })
            .add<VisibleClustersCounterTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_workGraphTelemetryBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodWorkGraphCounterCount, sizeof(uint32_t), true, false, false, false);
        m_workGraphTelemetryBuffer->SetName("CLod Work Graph Telemetry Buffer");
        m_workGraphTelemetryBuffer->GetECSEntity()
            .set<Components::Resource>({ m_workGraphTelemetryBuffer })
            .add<CLodWorkGraphTelemetryBufferTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_occlusionReplayBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodReplayBufferNumUints, sizeof(uint32_t), true, false, false, false);
        m_occlusionReplayBuffer->SetName("CLod Occlusion Replay Buffer");
        m_occlusionReplayStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReplayBufferState), true, false, false, false);
        m_occlusionReplayStateBuffer->SetName("CLod Occlusion Replay State Buffer");
        m_occlusionNodeGpuInputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(3, sizeof(CLodNodeGpuInput), true, false, false, false);
        m_occlusionNodeGpuInputsBuffer->SetName("CLod Occlusion Node GPU Inputs Buffer");
        m_viewDepthSrvIndicesBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodMaxViewDepthIndices, sizeof(CLodViewDepthSRVIndex), true, false, false, false);
        m_viewDepthSrvIndicesBuffer->SetName("CLod View Depth SRV Indices Buffer");

        m_rasterBucketsOffsetsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsOffsetsBuffer->SetName("CLod Raster bucket offsets");
        m_rasterBucketsBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsBlockSumsBuffer->SetName("CLod Raster bucket block sums");
        m_rasterBucketsScannedBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsScannedBlockSumsBuffer->SetName("CLod Raster bucket scanned block sums");
        m_rasterBucketsTotalCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsTotalCountBuffer->SetName("CLod Raster bucket total count");

        m_compactedVisibleClustersBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(maxVisibleClusters), sizeof(VisibleCluster), true, false);
        m_compactedVisibleClustersBuffer->SetName("CLod Compacted Visible Clusters Buffer");
        // This tags the buffer with the extension type so passes can query for it with ECSResourceResolver
        m_compactedVisibleClustersBuffer->GetECSEntity()
            .set<Components::Resource>({ m_compactedVisibleClustersBuffer })
            .add<VisibleClustersBufferTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_rasterBucketsWriteCursorBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsWriteCursorBuffer->SetName("CLod Raster bucket write cursor");
        m_rasterBucketsIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false);
        m_rasterBucketsIndirectArgsBuffer->SetName("CLod Raster bucket indirect args");

        m_visibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
        m_visibleClustersCounterBufferPhase2->SetName("CLod Visible Clusters Counter Buffer Phase2");

        m_rasterBucketsHistogramBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsHistogramBufferPhase2->SetName("Raster bucket histogram phase2");

        m_rasterBucketsWriteCursorBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsWriteCursorBufferPhase2->SetName("CLod Raster bucket write cursor phase2");

        m_streamingNonResidentBits = CreateAliasedUnmaterializedStructuredBuffer(
            CLodBitsetWordCount(m_streamingStorageGroupCapacity),
            sizeof(uint32_t),
            true,
            false,
            false,
            false);
        m_streamingNonResidentBits->SetName("CLod Streaming NonResident Bits");

        m_streamingActiveGroupsBits = CreateAliasedUnmaterializedStructuredBuffer(
            CLodBitsetWordCount(m_streamingStorageGroupCapacity),
            sizeof(uint32_t),
            true,
            false,
            false,
            false);
        m_streamingActiveGroupsBits->SetName("CLod Streaming Active Groups Bits");

        m_streamingLoadRequestBits = CreateAliasedUnmaterializedStructuredBuffer(
            CLodBitsetWordCount(m_streamingStorageGroupCapacity),
            sizeof(uint32_t),
            true,
            false,
            false,
            false);
        m_streamingLoadRequestBits->SetName("CLod Streaming Load Request Bits");

        m_streamingLoadRequests = CreateAliasedUnmaterializedStructuredBuffer(
            CLodStreamingRequestCapacity,
            sizeof(CLodStreamingRequest),
            true,
            false,
            false,
            false);
        m_streamingLoadRequests->SetName("CLod Streaming Load Requests");

        m_streamingLoadCounter = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_streamingLoadCounter->SetName("CLod Streaming Load Counter");

        m_streamingRuntimeState = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodStreamingRuntimeState), true, false, false, false);
        m_streamingRuntimeState->SetName("CLod Streaming Runtime State");

        m_type = type;
    }

    void OnRegistryReset(ResourceRegistry* reg) override {
        (void)reg;
        for (auto& slot : m_streamingLoadReadbackSlots) {
            slot = {};
        }
        m_pendingStreamingRequests.clear();
        m_streamingLruEntries.clear();
        m_streamingLruSerial = 0u;
        m_streamingResidentGroupsCount = 0u;
        std::fill(m_streamingNonResidentBitsCpu.begin(), m_streamingNonResidentBitsCpu.end(), 0u);
        std::fill(m_streamingActiveGroupsBitsCpu.begin(), m_streamingActiveGroupsBitsCpu.end(), 0u);
        std::fill(m_streamingPinnedGroupsBitsCpu.begin(), m_streamingPinnedGroupsBitsCpu.end(), 0u);
        std::fill(m_streamingEvictionExemptBitsCpu.begin(), m_streamingEvictionExemptBitsCpu.end(), 0u);
        std::fill(m_streamingResidencyInitializedBitsCpu.begin(), m_streamingResidencyInitializedBitsCpu.end(), 0u);
        std::fill(m_streamingLruCurrentStampByGroup.begin(), m_streamingLruCurrentStampByGroup.end(), 0ull);
        std::fill(m_streamingRequestsInProgressBitsCpu.begin(), m_streamingRequestsInProgressBitsCpu.end(), 0u);
        m_streamingActiveGroupScanCount = 0u;
        m_streamingReadbackScheduleCursor = 0u;
        m_streamingNonResidentBitsUploadPending = true;
    }

    void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {
        m_streamingReadbackService = rg.GetReadbackService();

        rg.RegisterResource(Builtin::CLod::StreamingNonResidentBits, m_streamingNonResidentBits);
        rg.RegisterResource(Builtin::CLod::StreamingActiveGroupsBits, m_streamingActiveGroupsBits);
        rg.RegisterResource(Builtin::CLod::StreamingLoadRequestBits, m_streamingLoadRequestBits);
        rg.RegisterResource(Builtin::CLod::StreamingLoadRequests, m_streamingLoadRequests);
        rg.RegisterResource(Builtin::CLod::StreamingLoadCounter, m_streamingLoadCounter);
        rg.RegisterResource(Builtin::CLod::StreamingRuntimeState, m_streamingRuntimeState);

        RenderGraph::ExternalPassDesc streamingBeginPassDesc;
        streamingBeginPassDesc.type = RenderGraph::PassType::Compute;
        streamingBeginPassDesc.name = "CLod::StreamingBeginFramePass";
        streamingBeginPassDesc.where = RenderGraph::ExternalInsertPoint::After("SkinningPass");

        streamingBeginPassDesc.pass = std::make_shared<CLodStreamingBeginFramePass>(
            m_streamingLoadCounter,
            m_streamingLoadRequestBits,
            m_streamingNonResidentBits,
            m_streamingActiveGroupsBits,
            m_streamingRuntimeState,
            [this](std::vector<uint32_t>& outBits) {
                if (!m_streamingNonResidentBitsUploadPending) {
                    return false;
                }

                outBits = m_streamingNonResidentBitsCpu;
                m_streamingNonResidentBitsUploadPending = false;
                return true;
            },
            [this](std::vector<uint32_t>& outBits, uint32_t& outActiveScanCount) {
                outBits = m_streamingActiveGroupsBitsCpu;
                outActiveScanCount = m_streamingActiveGroupScanCount;
            },
            [this]() {
                return static_cast<uint32_t>(m_streamingNonResidentBitsCpu.size());
            },
            [this]() {
                ScheduleStreamingReadbacks();
            },
            [this]() {
                ProcessStreamingRequestsBudgeted();
            });
        outPasses.push_back(std::move(streamingBeginPassDesc));

        // First hierarchial culling pass
        // Occlusion culls based on last frame's depth buffer
        // Anything that fails only occlusion test gets sent to phase 2 
        // Any clusters that pass gets written out as a visible cluster
        RenderGraph::ExternalPassDesc cullPassDesc;
        cullPassDesc.type = RenderGraph::PassType::Compute;
        cullPassDesc.name = "CLod::HierarchialCullingPass1";
        HierarchialCullingPassInputs cullPassInputs;
        cullPassInputs.isFirstPass = true;
        cullPassDesc.pass = std::make_shared<HierarchialCullingPass>(
            cullPassInputs,
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_workGraphTelemetryBuffer,
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            m_viewDepthSrvIndicesBuffer);
        cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass");
        outPasses.push_back(std::move(cullPassDesc));

		// Histogram + prefix sum passes to prepare for indirect dispatch rasterization
        RenderGraph::ExternalPassDesc histogramPassDesc;
        histogramPassDesc.type = RenderGraph::PassType::Compute;
        histogramPassDesc.name = "CLod::RasterBucketsHistogramPass1";
        histogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBuffer);
        outPasses.push_back(std::move(histogramPassDesc));

        RenderGraph::ExternalPassDesc prefixScanPassDesc;
        prefixScanPassDesc.type = RenderGraph::PassType::Compute;
        prefixScanPassDesc.name = "CLod::RasterBucketsPrefixScanPass1";
        prefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer);
        outPasses.push_back(std::move(prefixScanPassDesc));

        RenderGraph::ExternalPassDesc prefixOffsetsPassDesc;
        prefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
        prefixOffsetsPassDesc.name = "CLod::RasterBucketsPrefixOffsetsPass1";
        prefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer);
        outPasses.push_back(std::move(prefixOffsetsPassDesc));

        RenderGraph::ExternalPassDesc compactPassDesc;
        compactPassDesc.type = RenderGraph::PassType::Compute;
        compactPassDesc.name = "CLod::RasterBucketsCompactAndArgsPass1";
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
            m_maxVisibleClusters,
            false);
        outPasses.push_back(std::move(compactPassDesc));

		// First rasterization pass, using indirect args from histogram compaction
        RenderGraph::ExternalPassDesc rasterizePassDesc;
        rasterizePassDesc.type = RenderGraph::PassType::Render;
        rasterizePassDesc.name = "CLod::RasterizeClustersPass1";
        ClusterRasterizationPassInputs rasterizePassInputs;
        rasterizePassInputs.clearGbuffer = true;
        rasterizePassInputs.wireframe = false;
        rasterizePassDesc.pass = std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsIndirectArgsBuffer);
        rasterizePassDesc.isGeometryPass = true;
        outPasses.push_back(std::move(rasterizePassDesc));

        // If we are writing a visibility buffer, we need to extract depth before downsample
        if (m_type != CLodExtensionType::Shadow) {
            RenderGraph::ExternalPassDesc depthCopyPassDesc;
            depthCopyPassDesc.type = RenderGraph::PassType::Compute;
            depthCopyPassDesc.name = "CLod::LinearDepthCopyPass1";
            depthCopyPassDesc.pass = std::make_shared<PerViewLinearDepthCopyPass>();
            outPasses.push_back(std::move(depthCopyPassDesc));
        }

		// Build HZB for occlusion culling in phase 2
        RenderGraph::ExternalPassDesc downsamplePassDesc;
        downsamplePassDesc.type = RenderGraph::PassType::Compute;
        downsamplePassDesc.name = "CLod::LinearDepthDownsamplePass1";
        downsamplePassDesc.pass = std::make_shared<DownsamplePass>();
        outPasses.push_back(std::move(downsamplePassDesc));

		// Second hierarchial culling pass
		// Anything that failed only occlusion in the first pass gets another chance,
		// using the HZB built from this frame's depth buffer
        RenderGraph::ExternalPassDesc cullPassDesc2;
        cullPassDesc2.type = RenderGraph::PassType::Compute;
        cullPassDesc2.name = "CLod::HierarchialCullingPass2";
        HierarchialCullingPassInputs cullPassInputs2;
        cullPassInputs2.isFirstPass = false;
        cullPassDesc2.pass = std::make_shared<HierarchialCullingPass>(
            cullPassInputs2,
            m_visibleClustersBuffer,
            m_visibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_workGraphTelemetryBuffer,
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            m_viewDepthSrvIndicesBuffer);
        outPasses.push_back(std::move(cullPassDesc2));

		// Histogram + prefix sum passes again
        RenderGraph::ExternalPassDesc histogramPassDesc2;
        histogramPassDesc2.type = RenderGraph::PassType::Compute;
        histogramPassDesc2.name = "CLod::RasterBucketsHistogramPass2";
        histogramPassDesc2.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2);
        outPasses.push_back(std::move(histogramPassDesc2));

        RenderGraph::ExternalPassDesc prefixScanPassDesc2;
        prefixScanPassDesc2.type = RenderGraph::PassType::Compute;
        prefixScanPassDesc2.name = "CLod::RasterBucketsPrefixScanPass2";
        prefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferPhase2,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer);
        outPasses.push_back(std::move(prefixScanPassDesc2));

        RenderGraph::ExternalPassDesc prefixOffsetsPassDesc2;
        prefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
        prefixOffsetsPassDesc2.name = "CLod::RasterBucketsPrefixOffsetsPass2";
        prefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer);
        outPasses.push_back(std::move(prefixOffsetsPassDesc2));

        RenderGraph::ExternalPassDesc compactPassDesc2;
        compactPassDesc2.type = RenderGraph::PassType::Compute;
        compactPassDesc2.name = "CLod::RasterBucketsCompactAndArgsPass2";
        compactPassDesc2.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBufferPhase2,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBufferPhase2,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_maxVisibleClusters,
            true);
        outPasses.push_back(std::move(compactPassDesc2));

		// Second rasterization pass, using indirect args from phase 2 histogram compaction
        RenderGraph::ExternalPassDesc rasterizePassDesc2;
        rasterizePassDesc2.type = RenderGraph::PassType::Render;
        rasterizePassDesc2.name = "CLod::RasterizeClustersPass2";
        rasterizePassDesc2.where = RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass");
        ClusterRasterizationPassInputs rasterizePassInputs2;
        rasterizePassInputs2.clearGbuffer = false;
        rasterizePassInputs2.wireframe = false;
        rasterizePassDesc2.pass = std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs2,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBufferPhase2,
            m_rasterBucketsIndirectArgsBuffer);
        rasterizePassDesc2.isGeometryPass = true;
        outPasses.push_back(std::move(rasterizePassDesc2));

        // Ensure linear depth contains any updates introduced by the second CLod raster pass
        if (m_type != CLodExtensionType::Shadow) {
            RenderGraph::ExternalPassDesc depthCopyPassDesc2;
            depthCopyPassDesc2.type = RenderGraph::PassType::Compute;
            depthCopyPassDesc2.name = "CLod::LinearDepthCopyPass2";
			depthCopyPassDesc2.where = RenderGraph::ExternalInsertPoint::Before("DeferredShadingPass");
            depthCopyPassDesc2.pass = std::make_shared<PerViewLinearDepthCopyPass>();
            outPasses.push_back(std::move(depthCopyPassDesc2));
        }

        // Build HZB for next frame
        RenderGraph::ExternalPassDesc downsamplePassDesc2;
        downsamplePassDesc2.type = RenderGraph::PassType::Compute;
        downsamplePassDesc2.name = "CLod::LinearDepthDownsamplePass2";
        downsamplePassDesc2.pass = std::make_shared<DownsamplePass>();
        outPasses.push_back(std::move(downsamplePassDesc2));
    }

    void GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {
        (void)outPasses;
        RefreshStreamingActiveGroupDomain();
        m_streamingReadbackService = rg.GetReadbackService();
    }

private:

    struct StreamingRequestReadbackState {
        bool pending = false;
        uint64_t captureId = 0;
        uint64_t sampleId = 0;
        bool hasCounter = false;
        bool hasRequests = false;
        uint32_t requestCount = 0;
        std::vector<CLodStreamingRequest> requests;
    };

    struct StreamingServiceSummary {
        uint32_t requested = 0;
        uint32_t unique = 0;
        uint32_t applied = 0;
        uint32_t failed = 0;
    };

    struct PendingStreamingRequest {
        bool isLoad = true;
        CLodStreamingRequest request{};
    };

    struct StreamingLruEntry {
        uint32_t groupIndex = 0u;
        uint64_t stamp = 0u;
    };

    static uint32_t BitWordAddress(uint32_t key) {
        return key >> 5u;
    }

    static uint32_t BitMask(uint32_t key) {
        return 1u << (key & 31u);
    }

    bool IsGroupPinned(uint32_t groupIndex) const {
        const uint32_t wordAddress = BitWordAddress(groupIndex);
        if (wordAddress >= m_streamingPinnedGroupsBitsCpu.size()) {
            return false;
        }

        return (m_streamingPinnedGroupsBitsCpu[wordAddress] & BitMask(groupIndex)) != 0u;
    }

    bool IsGroupActive(uint32_t groupIndex) const {
        const uint32_t wordAddress = BitWordAddress(groupIndex);
        if (wordAddress >= m_streamingActiveGroupsBitsCpu.size()) {
            return false;
        }

        return (m_streamingActiveGroupsBitsCpu[wordAddress] & BitMask(groupIndex)) != 0u;
    }

    bool IsGroupResident(uint32_t groupIndex) const {
        const uint32_t wordAddress = BitWordAddress(groupIndex);
        if (wordAddress >= m_streamingNonResidentBitsCpu.size()) {
            return false;
        }

        return (m_streamingNonResidentBitsCpu[wordAddress] & BitMask(groupIndex)) == 0u;
    }

    void TouchStreamingLru(uint32_t groupIndex) {
        if (groupIndex >= m_streamingLruCurrentStampByGroup.size()) {
            return;
        }

        const uint64_t stamp = ++m_streamingLruSerial;
        m_streamingLruCurrentStampByGroup[groupIndex] = stamp;
        m_streamingLruEntries.push_back({ groupIndex, stamp });
    }

    bool TryQueuePendingLoadRequest(const CLodStreamingRequest& req) {
        const uint32_t groupIndex = req.groupGlobalIndex;
        if (groupIndex >= m_streamingStorageGroupCapacity) {
            EnsureStreamingStorageCapacity(groupIndex + 1u);
        }

        if (!IsGroupActive(groupIndex)) {
            return false;
        }

        if (IsGroupResident(groupIndex)) {
            return false;
        }

        if (IsStreamingRequestInProgress(groupIndex)) {
            return false;
        }

        MarkStreamingRequestInProgress(groupIndex);
        PendingStreamingRequest pending{};
        pending.isLoad = true;
        pending.request = req;
        m_pendingStreamingRequests.push_back(pending);
        return true;
    }

    uint32_t QueueLoadRequestWithParents(const CLodStreamingRequest& requestedLoad) {
        if (requestedLoad.groupGlobalIndex >= m_streamingStorageGroupCapacity) {
            EnsureStreamingStorageCapacity(requestedLoad.groupGlobalIndex + 1u);
        }

        uint32_t queuedCount = 0u;
        std::vector<uint32_t> parentChain;
        uint32_t currentGroup = requestedLoad.groupGlobalIndex;
        const size_t maxHops = m_streamingParentGroupByGlobal.size();
        for (size_t hop = 0; hop < maxHops; ++hop) {
            if (currentGroup >= m_streamingParentGroupByGlobal.size()) {
                break;
            }

            const int32_t parent = m_streamingParentGroupByGlobal[currentGroup];
            if (parent < 0) {
                break;
            }

            const uint32_t parentGroup = static_cast<uint32_t>(parent);
            if (parentGroup == currentGroup) {
                break;
            }

            parentChain.push_back(parentGroup);
            currentGroup = parentGroup;
        }

        for (auto it = parentChain.rbegin(); it != parentChain.rend(); ++it) {
            const uint32_t parentGroup = *it;
            if (!IsGroupActive(parentGroup)) {
                continue;
            }
            if (IsGroupResident(parentGroup)) {
                continue;
            }

            CLodStreamingRequest parentLoad = requestedLoad;
            parentLoad.groupGlobalIndex = parentGroup;
            if (TryQueuePendingLoadRequest(parentLoad)) {
                queuedCount++;
            }
        }

        if (TryQueuePendingLoadRequest(requestedLoad)) {
            queuedCount++;
        }

        return queuedCount;
    }

    bool TryEvictLruVictim(uint32_t avoidGroup, CLodStreamingOperationStats& frameStats) {
        while (!m_streamingLruEntries.empty()) {
            const StreamingLruEntry entry = m_streamingLruEntries.front();
            m_streamingLruEntries.pop_front();

            if (entry.groupIndex >= m_streamingLruCurrentStampByGroup.size()) {
                continue;
            }
            if (m_streamingLruCurrentStampByGroup[entry.groupIndex] != entry.stamp) {
                continue;
            }
            if (entry.groupIndex == avoidGroup) {
                continue;
            }
            if (!IsGroupActive(entry.groupIndex)) {
                continue;
            }
            if (IsGroupPinned(entry.groupIndex)) {
                continue;
            }
            if (!IsGroupResident(entry.groupIndex)) {
                continue;
            }

            CLodStreamingRequest evictReq{};
            evictReq.groupGlobalIndex = entry.groupIndex;

            bool residencyBitChanged = false;
            const bool serviced = ApplyStreamingRequest(evictReq, residencyBitChanged);

            frameStats.unloadRequested++;
            frameStats.unloadUnique++;
            if (!serviced) {
                frameStats.unloadFailed++;
                continue;
            }

            if (residencyBitChanged) {
                frameStats.unloadApplied++;
                if (m_streamingResidentGroupsCount > 0u) {
                    m_streamingResidentGroupsCount--;
                }
                return true;
            }
        }

        return false;
    }

    void EnsureStreamingStorageCapacity(uint32_t requiredGroupCount) {
        if (requiredGroupCount <= m_streamingStorageGroupCapacity) {
            return;
        }

        const uint32_t newCapacity = CLodRoundUpCapacity(requiredGroupCount);
        const uint32_t newWordCount = CLodBitsetWordCount(newCapacity);

        m_streamingNonResidentBits->ResizeStructured(newWordCount);
        m_streamingActiveGroupsBits->ResizeStructured(newWordCount);
        m_streamingLoadRequestBits->ResizeStructured(newWordCount);

        m_streamingNonResidentBitsCpu.resize(newWordCount, 0u);
        m_streamingActiveGroupsBitsCpu.resize(newWordCount, 0u);
        m_streamingPinnedGroupsBitsCpu.resize(newWordCount, 0u);
        m_streamingEvictionExemptBitsCpu.resize(newWordCount, 0u);
        m_streamingResidencyInitializedBitsCpu.resize(newWordCount, 0u);
        m_streamingRequestsInProgressBitsCpu.resize(newWordCount, 0u);
        m_streamingLruCurrentStampByGroup.resize(newCapacity, 0ull);
        m_streamingParentGroupByGlobal.resize(newCapacity, -1);
        m_streamingStorageGroupCapacity = newCapacity;

        m_streamingNonResidentBitsUploadPending = true;
    }

    void RefreshStreamingActiveGroupDomain() {
        std::fill(m_streamingActiveGroupsBitsCpu.begin(), m_streamingActiveGroupsBitsCpu.end(), 0u);
        std::fill(m_streamingPinnedGroupsBitsCpu.begin(), m_streamingPinnedGroupsBitsCpu.end(), 0u);
        m_streamingActiveGroupScanCount = 0u;

        MeshManager* meshManager = nullptr;
        if (m_getMeshManager) {
            meshManager = m_getMeshManager();
        }

        if (meshManager == nullptr) {
            return;
        }

        std::vector<MeshManager::CLodActiveGroupRange> ranges;
        uint32_t maxGroupIndex = 0u;
        meshManager->GetCLodActiveUniqueAssetGroupRanges(ranges, maxGroupIndex);

        EnsureStreamingStorageCapacity(maxGroupIndex);
        std::fill(m_streamingParentGroupByGlobal.begin(), m_streamingParentGroupByGlobal.end(), -1);

        std::vector<int32_t> parentGroupByGlobal;
        uint32_t parentMapMaxGroupIndex = 0u;
        meshManager->GetCLodUniqueAssetParentMap(parentGroupByGlobal, parentMapMaxGroupIndex);
        if (parentMapMaxGroupIndex > 0u) {
            EnsureStreamingStorageCapacity(parentMapMaxGroupIndex);
            if (!parentGroupByGlobal.empty()) {
                const size_t copyCount = std::min(parentGroupByGlobal.size(), m_streamingParentGroupByGlobal.size());
                std::copy_n(parentGroupByGlobal.begin(), copyCount, m_streamingParentGroupByGlobal.begin());
            }
        }

        m_streamingActiveGroupScanCount = maxGroupIndex;

        for (const auto& range : ranges) {
            const uint32_t rangeBegin = std::min(range.groupsBase, m_streamingStorageGroupCapacity);
            const uint32_t rangeEnd = std::min(range.groupsBase + range.groupCount, m_streamingStorageGroupCapacity);
            for (uint32_t groupIndex = rangeBegin; groupIndex < rangeEnd; ++groupIndex) {
                const uint32_t wordAddress = BitWordAddress(groupIndex);
                m_streamingActiveGroupsBitsCpu[wordAddress] |= BitMask(groupIndex);
            }
        }

        std::vector<MeshManager::CLodActiveGroupRange> coarsestRanges;
        meshManager->GetCLodCoarsestUniqueAssetGroupRanges(coarsestRanges);
        for (const auto& range : coarsestRanges) {
            const uint32_t rangeBegin = std::min(range.groupsBase, m_streamingStorageGroupCapacity);
            const uint32_t rangeEnd = std::min(range.groupsBase + range.groupCount, m_streamingStorageGroupCapacity);
            for (uint32_t groupIndex = rangeBegin; groupIndex < rangeEnd; ++groupIndex) {
                const uint32_t wordAddress = BitWordAddress(groupIndex);
                m_streamingPinnedGroupsBitsCpu[wordAddress] |= BitMask(groupIndex);
                m_streamingEvictionExemptBitsCpu[wordAddress] |= BitMask(groupIndex);
            }
        }

        meshManager->ProcessCLodDiskStreamingIO();

        std::vector<MeshManager::CLodGlobalResidencyRequest> initRequests;
        initRequests.reserve(m_streamingActiveGroupScanCount);

        std::vector<uint32_t> initRequestWordAddresses;
        initRequestWordAddresses.reserve(m_streamingActiveGroupScanCount);

        std::vector<uint32_t> initRequestBitMasks;
        initRequestBitMasks.reserve(m_streamingActiveGroupScanCount);

        std::vector<uint8_t> initRequestPinnedFlags;
        initRequestPinnedFlags.reserve(m_streamingActiveGroupScanCount);

        for (uint32_t groupIndex = 0u; groupIndex < m_streamingActiveGroupScanCount; ++groupIndex) {
            const uint32_t wordAddress = BitWordAddress(groupIndex);
            const uint32_t bitMask = BitMask(groupIndex);
            const bool isActive = (m_streamingActiveGroupsBitsCpu[wordAddress] & bitMask) != 0u;
            if (!isActive) {
                continue;
            }

            const bool initialized = (m_streamingResidencyInitializedBitsCpu[wordAddress] & bitMask) != 0u;
            if (initialized) {
                continue;
            }

            const bool pinned = (m_streamingPinnedGroupsBitsCpu[wordAddress] & bitMask) != 0u;
            m_streamingResidencyInitializedBitsCpu[wordAddress] |= bitMask;

            MeshManager::CLodGlobalResidencyRequest initRequest{};
            initRequest.groupGlobalIndex = groupIndex;
            initRequest.resident = pinned;
            initRequests.push_back(initRequest);
            initRequestWordAddresses.push_back(wordAddress);
            initRequestBitMasks.push_back(bitMask);
            initRequestPinnedFlags.push_back(pinned ? 1u : 0u);
        }

        if (!initRequests.empty()) {
            std::vector<uint32_t> appliedCounts;
            meshManager->SetCLodGroupResidencyForGlobalBatch(initRequests, appliedCounts);

            const size_t appliedCount = std::min(appliedCounts.size(), initRequests.size());
            for (size_t i = 0; i < appliedCount; ++i) {
                const uint32_t wordAddress = initRequestWordAddresses[i];
                const uint32_t bitMask = initRequestBitMasks[i];
                const bool pinned = initRequestPinnedFlags[i] != 0u;

                if (pinned) {
                    if (appliedCounts[i] == 0u) {
                        m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                        m_streamingNonResidentBitsUploadPending = true;
                    }
                    else {
                        m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
                    }
                }
                else {
                    m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                    m_streamingNonResidentBitsUploadPending = true;
                }
            }
        }

        uint32_t residentCount = 0u;
        for (uint32_t groupIndex = 0u; groupIndex < m_streamingActiveGroupScanCount; ++groupIndex) {
            const uint32_t wordAddress = BitWordAddress(groupIndex);
            const uint32_t bitMask = BitMask(groupIndex);
            const bool isActive = (m_streamingActiveGroupsBitsCpu[wordAddress] & bitMask) != 0u;
            if (!isActive) {
                continue;
            }

            const bool resident = (m_streamingNonResidentBitsCpu[wordAddress] & bitMask) == 0u;
            if (resident) {
                residentCount++;
            }
        }

        m_streamingResidentGroupsCount = residentCount;
    }

    bool IsStreamingRequestInProgress(uint32_t groupIndex) const {
        const uint32_t wordAddress = BitWordAddress(groupIndex);
        if (wordAddress >= m_streamingRequestsInProgressBitsCpu.size()) {
            return false;
        }

        const uint32_t bitMask = BitMask(groupIndex);
        return (m_streamingRequestsInProgressBitsCpu[wordAddress] & bitMask) != 0u;
    }

    void MarkStreamingRequestInProgress(uint32_t groupIndex) {
        const uint32_t wordAddress = BitWordAddress(groupIndex);
        const uint32_t bitMask = BitMask(groupIndex);
        m_streamingRequestsInProgressBitsCpu[wordAddress] |= bitMask;
    }

    void ClearStreamingRequestInProgress(uint32_t groupIndex) {
        const uint32_t wordAddress = BitWordAddress(groupIndex);
        if (wordAddress >= m_streamingRequestsInProgressBitsCpu.size()) {
            return;
        }

        const uint32_t bitMask = BitMask(groupIndex);
        m_streamingRequestsInProgressBitsCpu[wordAddress] &= ~bitMask;
    }

    bool ApplyStreamingRequest(const CLodStreamingRequest& req, bool& outResidencyBitChanged) {
        outResidencyBitChanged = false;

        const uint32_t groupIndex = req.groupGlobalIndex;
        if (groupIndex >= m_streamingStorageGroupCapacity) {
            EnsureStreamingStorageCapacity(groupIndex + 1u);
        }

        const uint32_t wordAddress = BitWordAddress(groupIndex);
        const uint32_t bitMask = BitMask(groupIndex);
        const uint32_t oldWord = m_streamingNonResidentBitsCpu[wordAddress];

        MeshManager* meshManager = nullptr;
        if (m_getMeshManager) {
            meshManager = m_getMeshManager();
        }

        bool serviced = false;
        if (meshManager == nullptr) {
            serviced = true;
        }
        else {
            const uint32_t globalApplied = meshManager->SetCLodGroupResidencyForGlobal(req.groupGlobalIndex, false);
            serviced = (globalApplied > 0);
        }

        if (!serviced) {
            return false;
        }

        m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;

        outResidencyBitChanged = (m_streamingNonResidentBitsCpu[wordAddress] != oldWord);
        if (outResidencyBitChanged) {
            m_streamingNonResidentBitsUploadPending = true;
        }

        return true;
    }

    StreamingRequestReadbackState* FindStreamingLoadReadbackStateByCaptureId(uint64_t captureId) {
        auto& slots = m_streamingLoadReadbackSlots;
        for (auto& slot : slots) {
            if (slot.pending && slot.captureId == captureId) {
                return &slot;
            }
        }

        return nullptr;
    }

    void TryFinalizeStreamingLoadReadback(StreamingRequestReadbackState& state) {
        if (!state.pending || !state.hasCounter || !state.hasRequests) {
            return;
        }

        const size_t decodeCount = std::min<size_t>(state.requestCount, state.requests.size());
        uint32_t queuedCount = 0;
        std::unordered_set<uint32_t> seen;
        seen.reserve(decodeCount);

        for (size_t i = 0; i < decodeCount; ++i) {
            const CLodStreamingRequest& req = state.requests[i];
            if (!seen.insert(req.groupGlobalIndex).second) {
                continue;
            }

            queuedCount += QueueLoadRequestWithParents(req);
        }

        spdlog::info(
            "CLod streaming: observed {} load requests ({} unique groups, {} queued, {} deduped/in-progress)",
            static_cast<uint32_t>(decodeCount),
            static_cast<uint32_t>(seen.size()),
            queuedCount,
            static_cast<uint32_t>(seen.size()) - queuedCount);

        state = {};
    }

    void ScheduleStreamingReadbacks() {
        if (m_streamingReadbackService == nullptr || m_streamingReadbackRingSize == 0u) {
            return;
        }

        if (m_streamingLoadReadbackSlots.size() != m_streamingReadbackRingSize) {
            m_streamingLoadReadbackSlots.assign(m_streamingReadbackRingSize, {});
        }

        int32_t selectedSlot = -1;
        for (uint32_t i = 0; i < m_streamingReadbackRingSize; ++i) {
            const uint32_t slotIndex = (m_streamingReadbackScheduleCursor + i) % m_streamingReadbackRingSize;
            if (!m_streamingLoadReadbackSlots[slotIndex].pending) {
                selectedSlot = static_cast<int32_t>(slotIndex);
                break;
            }
        }

        if (selectedSlot < 0) {
            return;
        }

        const uint32_t slotIndex = static_cast<uint32_t>(selectedSlot);
        m_streamingReadbackScheduleCursor = (slotIndex + 1u) % m_streamingReadbackRingSize;

        auto& loadSlot = m_streamingLoadReadbackSlots[slotIndex];

        loadSlot = {};
        loadSlot.pending = true;
        loadSlot.captureId = ++m_streamingReadbackCaptureSerial;

        const uint64_t loadCaptureId = loadSlot.captureId;
        m_streamingReadbackService->RequestReadbackCapture(
            "CLod::HierarchialCullingPass2",
            m_streamingLoadCounter.get(),
            RangeSpec{},
            [this, loadCaptureId](ReadbackCaptureResult&& result) {
                auto* state = FindStreamingLoadReadbackStateByCaptureId(loadCaptureId);
                if (state == nullptr) {
                    return;
                }

                uint32_t requestCount = 0;
                if (result.data.size() >= sizeof(uint32_t)) {
                    std::memcpy(&requestCount, result.data.data(), sizeof(uint32_t));
                }

                state->requestCount = std::min<uint32_t>(requestCount, CLodStreamingRequestCapacity);
                state->hasCounter = true;
                TryFinalizeStreamingLoadReadback(*state);
            });

        m_streamingReadbackService->RequestReadbackCapture(
            "CLod::HierarchialCullingPass2",
            m_streamingLoadRequests.get(),
            RangeSpec{},
            [this, loadCaptureId](ReadbackCaptureResult&& result) {
                auto* state = FindStreamingLoadReadbackStateByCaptureId(loadCaptureId);
                if (state == nullptr) {
                    return;
                }

                const size_t stride = sizeof(CLodStreamingRequest);
                const size_t decodedCount = std::min<size_t>(result.data.size() / stride, CLodStreamingRequestCapacity);
                state->requests.resize(decodedCount);
                if (decodedCount > 0) {
                    std::memcpy(state->requests.data(), result.data.data(), decodedCount * stride);
                }

                state->hasRequests = true;
                TryFinalizeStreamingLoadReadback(*state);
            });
    }

    void ProcessStreamingRequestsBudgeted() {
        const uint32_t budget = std::max(m_streamingCpuUploadBudgetRequests, 1u);
        CLodStreamingOperationStats frameStats{};

        MeshManager* meshManager = nullptr;
        if (m_getMeshManager) {
            meshManager = m_getMeshManager();
        }

        if (meshManager != nullptr) {
            meshManager->ProcessCLodDiskStreamingIO(budget);
        }

        struct PendingLoadBatchEntry {
            uint32_t groupIndex = 0u;
            uint32_t wordAddress = 0u;
            uint32_t bitMask = 0u;
            uint32_t oldWord = 0u;
        };

        std::vector<MeshManager::CLodGlobalResidencyRequest> loadBatchRequests;
        loadBatchRequests.reserve(budget);

        std::vector<PendingLoadBatchEntry> loadBatchEntries;
        loadBatchEntries.reserve(budget);

        uint32_t processed = 0;
        while (processed < budget && !m_pendingStreamingRequests.empty()) {
            const PendingStreamingRequest pending = m_pendingStreamingRequests.front();
            m_pendingStreamingRequests.pop_front();

            if (pending.request.groupGlobalIndex >= m_streamingStorageGroupCapacity) {
                EnsureStreamingStorageCapacity(pending.request.groupGlobalIndex + 1u);
            }

            if (!IsGroupResident(pending.request.groupGlobalIndex)) {
                while (m_streamingResidentGroupsCount >= m_streamingResidentBudgetGroups) {
                    const bool evicted = TryEvictLruVictim(pending.request.groupGlobalIndex, frameStats);
                    if (!evicted) {
                        break;
                    }
                }
            }

            if (!pending.isLoad) {
                bool residencyBitChanged = false;
                const bool serviced = ApplyStreamingRequest(pending.request, residencyBitChanged);
                frameStats.unloadRequested++;
                frameStats.unloadUnique++;
                if (!serviced) {
                    frameStats.unloadFailed++;
                }
                else if (residencyBitChanged) {
                    frameStats.unloadApplied++;
                    if (m_streamingResidentGroupsCount > 0u) {
                        m_streamingResidentGroupsCount--;
                    }
                }

                TouchStreamingLru(pending.request.groupGlobalIndex);
                ClearStreamingRequestInProgress(pending.request.groupGlobalIndex);
                processed++;
                continue;
            }

            frameStats.loadRequested++;
            frameStats.loadUnique++;

            const uint32_t groupIndex = pending.request.groupGlobalIndex;
            const uint32_t wordAddress = BitWordAddress(groupIndex);
            const uint32_t bitMask = BitMask(groupIndex);
            const uint32_t oldWord = m_streamingNonResidentBitsCpu[wordAddress];

            if (meshManager == nullptr) {
                m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
                const bool residencyBitChanged = (m_streamingNonResidentBitsCpu[wordAddress] != oldWord);
                if (residencyBitChanged) {
                    frameStats.loadApplied++;
                    m_streamingResidentGroupsCount++;
                    m_streamingNonResidentBitsUploadPending = true;
                }

                TouchStreamingLru(groupIndex);
                ClearStreamingRequestInProgress(groupIndex);
                processed++;
                continue;
            }

            MeshManager::CLodGlobalResidencyRequest req{};
            req.groupGlobalIndex = groupIndex;
            req.resident = true;
            loadBatchRequests.push_back(req);

            PendingLoadBatchEntry entry{};
            entry.groupIndex = groupIndex;
            entry.wordAddress = wordAddress;
            entry.bitMask = bitMask;
            entry.oldWord = oldWord;
            loadBatchEntries.push_back(entry);

            processed++;
        }

        if (meshManager != nullptr && !loadBatchRequests.empty()) {
            std::vector<uint32_t> appliedCounts;
            meshManager->SetCLodGroupResidencyForGlobalBatch(loadBatchRequests, appliedCounts);

            const size_t count = std::min(loadBatchEntries.size(), appliedCounts.size());
            for (size_t i = 0; i < count; ++i) {
                const auto& entry = loadBatchEntries[i];
                const bool serviced = appliedCounts[i] > 0u;
                if (!serviced) {
                    frameStats.loadFailed++;
                }
                else {
                    m_streamingNonResidentBitsCpu[entry.wordAddress] &= ~entry.bitMask;
                    const bool residencyBitChanged = (m_streamingNonResidentBitsCpu[entry.wordAddress] != entry.oldWord);
                    if (residencyBitChanged) {
                        frameStats.loadApplied++;
                        m_streamingResidentGroupsCount++;
                        m_streamingNonResidentBitsUploadPending = true;
                    }
                }

                TouchStreamingLru(entry.groupIndex);
                ClearStreamingRequestInProgress(entry.groupIndex);
            }

            for (size_t i = count; i < loadBatchEntries.size(); ++i) {
                const auto& entry = loadBatchEntries[i];
                frameStats.loadFailed++;
                TouchStreamingLru(entry.groupIndex);
                ClearStreamingRequestInProgress(entry.groupIndex);
            }
        }

        PublishCLodStreamingOperationStats(frameStats);
    }

    CLodExtensionType m_type;
    uint64_t m_maxVisibleClusters;

    // Buffers used across CLod passes
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_workGraphTelemetryBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayStateBuffer;
    std::shared_ptr<Buffer> m_occlusionNodeGpuInputsBuffer;
    std::shared_ptr<Buffer> m_viewDepthSrvIndicesBuffer;

    // Histogram Pass Buffers
    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBuffer;

    // prefix scan buffers
    std::shared_ptr<Buffer> m_rasterBucketsOffsetsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsBlockSumsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsScannedBlockSumsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsTotalCountBuffer;

    // Phase-2 CPU-reset resources (duplicated to avoid pre-execute Update clobbering)
    std::shared_ptr<Buffer> m_visibleClustersCounterBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferPhase2;

    // Streaming runtime buffers
    std::shared_ptr<Buffer> m_streamingNonResidentBits;
    std::shared_ptr<Buffer> m_streamingActiveGroupsBits;
    std::shared_ptr<Buffer> m_streamingLoadRequestBits;
    std::shared_ptr<Buffer> m_streamingLoadRequests;
    std::shared_ptr<Buffer> m_streamingLoadCounter;
    std::shared_ptr<Buffer> m_streamingRuntimeState;

    std::vector<uint32_t> m_streamingNonResidentBitsCpu;
    std::vector<uint32_t> m_streamingActiveGroupsBitsCpu;
    std::vector<uint32_t> m_streamingPinnedGroupsBitsCpu;
    std::vector<uint32_t> m_streamingEvictionExemptBitsCpu;
    std::vector<uint32_t> m_streamingResidencyInitializedBitsCpu;
    std::vector<uint32_t> m_streamingRequestsInProgressBitsCpu;
    std::vector<int32_t> m_streamingParentGroupByGlobal;
    std::vector<uint64_t> m_streamingLruCurrentStampByGroup;
    std::deque<StreamingLruEntry> m_streamingLruEntries;
    uint64_t m_streamingLruSerial = 0u;
    uint32_t m_streamingResidentGroupsCount = 0u;
    uint32_t m_streamingActiveGroupScanCount = 0u;
    uint32_t m_streamingStorageGroupCapacity = CLodStreamingInitialGroupCapacity;
    bool m_streamingNonResidentBitsUploadPending = false;
    uint32_t m_streamingReadbackRingSize = 2u;
    uint32_t m_streamingReadbackScheduleCursor = 0u;
    uint32_t m_streamingCpuUploadBudgetRequests = 640u;
    uint32_t m_streamingResidentBudgetGroups = 50000u;
    rg::runtime::IReadbackService* m_streamingReadbackService = nullptr;
    std::function<MeshManager*()> m_getMeshManager = []() { return nullptr; };

    uint64_t m_streamingReadbackCaptureSerial = 0;
    uint64_t m_streamingOperationSampleSerial = 0;
    std::vector<StreamingRequestReadbackState> m_streamingLoadReadbackSlots;
    std::deque<PendingStreamingRequest> m_pendingStreamingRequests;

    // Compaction Pass Buffers
    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;

    class PerViewLinearDepthCopyPass : public ComputePass {
    public:
        PerViewLinearDepthCopyPass() {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"shaders/gbuffer.hlsl",
                L"PerViewPrimaryDepthCopyCS",
                {},
                "PerViewPrimaryDepthCopyPSO");
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(Builtin::PrimaryCamera::VisibilityTexture)
                .WithUnorderedAccess(Builtin::PrimaryCamera::LinearDepthMap, Builtin::Shadows::LinearShadowMaps);
        }

        void Setup() override {}

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
            auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());

            uint32_t rootConstants[NumMiscUintRootConstants] = {};

            context.viewManager->ForEachView([&](uint64_t viewID) {
                const auto* view = context.viewManager->Get(viewID);
                if (!view || !view->gpu.visibilityBuffer || !view->gpu.linearDepthMap) {
                    return;
                }

                rootConstants[UintRootConstant0] = view->gpu.visibilityBuffer->GetSRVInfo(0).slot.index;
                rootConstants[UintRootConstant1] = view->gpu.linearDepthMap->GetUAVShaderVisibleInfo(0).slot.index;
                rootConstants[UintRootConstant2] = view->gpu.visibilityBuffer->GetWidth();
                rootConstants[UintRootConstant3] = view->gpu.visibilityBuffer->GetHeight();

                commandList.PushConstants(
                    rhi::ShaderStage::Compute,
                    0,
                    MiscUintRootSignatureIndex,
                    0,
                    NumMiscUintRootConstants,
                    rootConstants);

                const uint32_t groupsX = (rootConstants[UintRootConstant2] + 7u) / 8u;
                const uint32_t groupsY = (rootConstants[UintRootConstant3] + 7u) / 8u;
                commandList.Dispatch(groupsX, groupsY, 1);
            });

            return {};
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
    };

    class CLodStreamingBeginFramePass : public ComputePass {
    public:
        CLodStreamingBeginFramePass(
            std::shared_ptr<Buffer> loadCounter,
            std::shared_ptr<Buffer> loadRequestBits,
            std::shared_ptr<Buffer> nonResidentBits,
            std::shared_ptr<Buffer> activeGroupsBits,
            std::shared_ptr<Buffer> runtimeState,
            std::function<bool(std::vector<uint32_t>&)> tryConsumeNonResidentBitsUpload,
            std::function<void(std::vector<uint32_t>&, uint32_t&)> getActiveGroupsBitsUpload,
            std::function<uint32_t()> getBitsetWordCount,
            std::function<void()> scheduleStreamingReadbacks,
            std::function<void()> processStreamingRequests)
            : m_loadCounter(std::move(loadCounter))
            , m_loadRequestBits(std::move(loadRequestBits))
            , m_nonResidentBits(std::move(nonResidentBits))
            , m_activeGroupsBits(std::move(activeGroupsBits))
            , m_runtimeState(std::move(runtimeState))
            , m_tryConsumeNonResidentBitsUpload(std::move(tryConsumeNonResidentBitsUpload))
            , m_getActiveGroupsBitsUpload(std::move(getActiveGroupsBitsUpload))
            , m_getBitsetWordCount(std::move(getBitsetWordCount))
            , m_scheduleStreamingReadbacks(std::move(scheduleStreamingReadbacks))
            , m_processStreamingRequests(std::move(processStreamingRequests)) {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithUnorderedAccess(m_loadCounter, m_nonResidentBits, m_activeGroupsBits, m_runtimeState)
                .WithUnorderedAccess(Builtin::CLod::StreamingLoadRequestBits);
        }

        void Setup() override {}

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
            auto& context = *renderContext;
            auto& commandList = executionContext.commandList;
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
            if (!updateContext) {
                return;
            }

            if (m_scheduleStreamingReadbacks) {
                m_scheduleStreamingReadbacks();
            }
            if (m_processStreamingRequests) {
                m_processStreamingRequests();
            }

            const uint32_t zero = 0u;
            BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_loadCounter), 0);

            const uint32_t bitsetWordCount = std::max(m_getBitsetWordCount ? m_getBitsetWordCount() : 1u, 1u);
            std::vector<uint32_t> zeroBits(bitsetWordCount, 0u);
            BUFFER_UPLOAD(
                zeroBits.data(),
                static_cast<uint32_t>(zeroBits.size() * sizeof(uint32_t)),
                rg::runtime::UploadTarget::FromShared(m_loadRequestBits),
                0);

            std::vector<uint32_t> nonResidentBitsUpload;
            if (m_tryConsumeNonResidentBitsUpload && m_tryConsumeNonResidentBitsUpload(nonResidentBitsUpload)) {
                BUFFER_UPLOAD(
                    nonResidentBitsUpload.data(),
                    static_cast<uint32_t>(nonResidentBitsUpload.size() * sizeof(uint32_t)),
                    rg::runtime::UploadTarget::FromShared(m_nonResidentBits),
                    0);
            }

            std::vector<uint32_t> activeGroupsBitsUpload;
            uint32_t activeGroupScanCount = 0u;
            if (m_getActiveGroupsBitsUpload) {
                m_getActiveGroupsBitsUpload(activeGroupsBitsUpload, activeGroupScanCount);
            }
            if (!activeGroupsBitsUpload.empty()) {
                BUFFER_UPLOAD(
                    activeGroupsBitsUpload.data(),
                    static_cast<uint32_t>(activeGroupsBitsUpload.size() * sizeof(uint32_t)),
                    rg::runtime::UploadTarget::FromShared(m_activeGroupsBits),
                    0);
            }

            CLodStreamingRuntimeState state{};
            state.activeGroupScanCount = activeGroupScanCount;
            state.unloadAfterFrames = 0u;
            state.activeGroupsBitsetWordCount = CLodBitsetWordCount(activeGroupScanCount);
            BUFFER_UPLOAD(
                &state,
                sizeof(CLodStreamingRuntimeState),
                rg::runtime::UploadTarget::FromShared(m_runtimeState),
                0);
        }

        void Cleanup() override {}

    private:
        std::shared_ptr<Buffer> m_loadCounter;
        std::shared_ptr<Buffer> m_loadRequestBits;
        std::shared_ptr<Buffer> m_nonResidentBits;
        std::shared_ptr<Buffer> m_activeGroupsBits;
        std::shared_ptr<Buffer> m_runtimeState;
        std::function<bool(std::vector<uint32_t>&)> m_tryConsumeNonResidentBitsUpload;
        std::function<void(std::vector<uint32_t>&, uint32_t&)> m_getActiveGroupsBitsUpload;
        std::function<uint32_t()> m_getBitsetWordCount;
        std::function<void()> m_scheduleStreamingReadbacks;
        std::function<void()> m_processStreamingRequests;
    };

    // ---------------------------------------------------------------
    // Hierarchial Culling Pass
    // ---------------------------------------------------------------

    struct ObjectCullRecord
    {
        uint viewDataIndex; // One record per view *...
        uint activeDrawSetIndicesSRVIndex; // One record per draw set
        uint activeDrawCount;
        uint dispatchGridX; // Drives dispatch size
        uint dispatchGridY;
        uint dispatchGridZ;
    };

    struct HierarchialCullingPassInputs {
        bool isFirstPass;

        friend bool operator==(const HierarchialCullingPassInputs&, const HierarchialCullingPassInputs&) = default;
    };

    inline rg::Hash64 HashValue(const HierarchialCullingPassInputs& i) {
        std::size_t seed = 0;

        boost::hash_combine(seed, i.isFirstPass);
        return seed;
    }

    class HierarchialCullingPass : public ComputePass, public IDynamicDeclaredResources {
    public:
        HierarchialCullingPass(
            HierarchialCullingPassInputs inputs,
            std::shared_ptr<Buffer> visibleClustersBuffer,
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> histogramIndirectCommand,
            std::shared_ptr<Buffer> workGraphTelemetryBuffer,
            std::shared_ptr<Buffer> occlusionReplayBuffer,
            std::shared_ptr<Buffer> occlusionReplayStateBuffer,
            std::shared_ptr<Buffer> occlusionNodeGpuInputsBuffer,
            std::shared_ptr<Buffer> viewDepthSrvIndicesBuffer) {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_workGraph,
                m_createCommandPipelineState);
            m_isFirstPass = inputs.isFirstPass;
            auto memSize = m_workGraph->GetRequiredScratchMemorySize();
            m_scratchBuffer = Buffer::CreateShared( // TODO: Make a way for the graph to provide things like this, to allow for aliasing
                rhi::HeapType::DeviceLocal,
                memSize,
                true);
            m_scratchBuffer->SetMemoryUsageHint("Work graph scratch buffer");
            m_visibleClustersBuffer = visibleClustersBuffer;
            m_visibleClustersCounterBuffer = visibleClustersCounterBuffer;
            m_histogramIndirectCommand = histogramIndirectCommand;
            m_workGraphTelemetryBuffer = workGraphTelemetryBuffer;
            m_occlusionReplayBuffer = occlusionReplayBuffer;
            m_occlusionReplayStateBuffer = occlusionReplayStateBuffer;
            m_occlusionNodeGpuInputsBuffer = occlusionNodeGpuInputsBuffer;
            m_viewDepthSrvIndicesBuffer = viewDepthSrvIndicesBuffer;
        }

        ~HierarchialCullingPass() {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            auto ecsWorld = ECSManager::GetInstance().GetWorld();
            flecs::query<> drawSetIndicesQuery = ecsWorld.query_builder<>()
                .with<Components::IsActiveDrawSetIndices>()
                .with<Components::ParticipatesInPass>(flecs::Wildcard)
                .build();
            builder->WithUnorderedAccess(m_scratchBuffer,
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_histogramIndirectCommand,
                m_workGraphTelemetryBuffer,
                m_occlusionReplayBuffer,
                m_occlusionReplayStateBuffer,
                m_occlusionNodeGpuInputsBuffer,
                m_viewDepthSrvIndicesBuffer)
                .WithUnorderedAccess(
                    Builtin::CLod::StreamingLoadRequestBits,
                    Builtin::CLod::StreamingLoadRequests,
                    Builtin::CLod::StreamingLoadCounter,
                    Builtin::CLod::StreamingRuntimeState)
                .WithShaderResource(Builtin::IndirectCommandBuffers::Master,
                    Builtin::CLod::Offsets,
                    Builtin::CLod::GroupChunks,
                    Builtin::CLod::Groups,
                    Builtin::CLod::Children,
                    Builtin::CLod::Nodes,
                    Builtin::CLod::MeshletBounds,
                    Builtin::CLod::StreamingActiveGroupsBits,
                    Builtin::CLod::StreamingNonResidentBits,
                    Builtin::CLod::MeshMetadata,
                    Builtin::CullingCameraBuffer,
                    Builtin::PerMeshInstanceBuffer,
                    Builtin::PerObjectBuffer,
                    Builtin::CameraBuffer,
                    Builtin::PerMeshBuffer,
                    Builtin::PrimaryCamera::LinearDepthMap, 
                    Builtin::Shadows::LinearShadowMaps)
                .WithShaderResource(ECSResourceResolver(drawSetIndicesQuery));

        }

        void Setup() override {
            RegisterSRV(Builtin::IndirectCommandBuffers::Master);
            RegisterSRV(Builtin::CLod::Offsets);
            RegisterSRV(Builtin::CLod::GroupChunks);
            RegisterSRV(Builtin::CLod::Groups);
            RegisterSRV(Builtin::CLod::Children);
            RegisterSRV(Builtin::CLod::StreamingActiveGroupsBits);
            RegisterSRV(Builtin::CLod::StreamingNonResidentBits);
            RegisterSRV(Builtin::CLod::StreamingLoadRequestBits);
            RegisterSRV(Builtin::CLod::StreamingLoadRequests);
            RegisterSRV(Builtin::CLod::StreamingLoadCounter);
            RegisterSRV(Builtin::CLod::StreamingRuntimeState);
			RegisterSRV(Builtin::CLod::MeshMetadata);
            RegisterSRV(Builtin::CullingCameraBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerObjectBuffer);
            RegisterSRV(Builtin::CLod::Nodes);
            RegisterSRV(Builtin::CLod::MeshletBounds);
            RegisterSRV(Builtin::CameraBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);

            //RegisterUAV(Builtin::VisibleClusterBuffer);
            //RegisterUAV(Builtin::VisibleClusterCounter);

            //m_visibleClusterCounter = m_resourceRegistryView->RequestHandle(Builtin::VisibleClusterCounter);
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
            auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            // Set the descriptor heaps
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

            commandList.SetWorkGraph(m_workGraph->GetHandle(), m_scratchBuffer->GetAPIResource().GetHandle(), true); // Reset every time for now

            BindResourceDescriptorIndices(commandList, m_pipelineResources);

            uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX] = m_histogramIndirectCommand->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_WORKGRAPH_TELEMETRY_DESCRIPTOR_INDEX] = m_workGraphTelemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_WORKGRAPH_TELEMETRY_ENABLED] = IsCLodWorkGraphTelemetryEnabled() ? 1u : 0u;
            uintRootConstants[CLOD_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX] = m_occlusionReplayBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX] = m_occlusionReplayStateBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX] = m_occlusionNodeGpuInputsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX] = m_viewDepthSrvIndicesBuffer->GetSRVInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                uintRootConstants);

            if (m_isFirstPass) {
                std::vector<ObjectCullRecord> cullRecords;

                ViewFilter filter = ViewFilter::PrimaryCameras();
                context.viewManager->ForEachFiltered(filter, [&](uint64_t view) {
                    auto viewInfo = context.viewManager->Get(view);
                    auto cameraBufferIndex = viewInfo->gpu.cameraBufferIndex;
                    auto workloads = context.indirectCommandBufferManager->GetViewIndirectBuffersForRenderPhase(view, m_renderPhase);
                    for (auto& wl : workloads) {
                        auto count = wl.workload.count;
                        if (count == 0) {
                            continue;
                        }
                        ObjectCullRecord record{};
                        record.viewDataIndex = cameraBufferIndex;
                        record.activeDrawSetIndicesSRVIndex = context.objectManager->GetActiveDrawSetIndices(wl.flags)->GetSRVInfo(0).slot.index;
                        record.activeDrawCount = count;
                        record.dispatchGridX = static_cast<uint>((count + 63) / 64);
                        record.dispatchGridY = 1;
                        record.dispatchGridZ = 1;
                        cullRecords.push_back(record);
                    }
                    });

                rhi::WorkGraphDispatchDesc dispatchDesc{};
                dispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::NodeCpuInput;
                dispatchDesc.nodeCpuInput.entryPointIndex = 0;
                dispatchDesc.nodeCpuInput.pRecords = cullRecords.data();
                dispatchDesc.nodeCpuInput.numRecords = static_cast<uint32_t>(cullRecords.size());
                dispatchDesc.nodeCpuInput.recordByteStride = sizeof(ObjectCullRecord);
                commandList.DispatchWorkGraph(dispatchDesc);

                rhi::BufferBarrier barrier{};
                barrier.buffer = m_visibleClustersCounterBuffer->GetAPIResource().GetHandle();
                barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
                barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
                barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
                barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
                rhi::BarrierBatch bufferBarriers{};
                bufferBarriers.buffers = rhi::Span<rhi::BufferBarrier>(&barrier, 1);
                commandList.Barriers(bufferBarriers);

            }
            else {
                rhi::BufferBarrier replayDispatchBarriers[2] = {};
                replayDispatchBarriers[0].buffer = m_occlusionReplayBuffer->GetAPIResource().GetHandle();
                replayDispatchBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
                replayDispatchBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;

                replayDispatchBarriers[1].buffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
                replayDispatchBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
                replayDispatchBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;

                rhi::BarrierBatch replayBarrierBatch{};
                replayBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(replayDispatchBarriers, 2);
                commandList.Barriers(replayBarrierBatch);

                rhi::WorkGraphDispatchDesc replayDispatchDesc{};
                replayDispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::MultiNodeGpuInput;
                replayDispatchDesc.multiNodeGpuInput.inputBuffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
                replayDispatchDesc.multiNodeGpuInput.inputAddressOffset = 0;
                commandList.DispatchWorkGraph(replayDispatchDesc);
            }

            rhi::BufferBarrier counterBarrier{};
            counterBarrier.buffer = m_visibleClustersCounterBuffer->GetAPIResource().GetHandle();
            counterBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            counterBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            counterBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
            counterBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            rhi::BarrierBatch counterBarrierBatch{};
            counterBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(&counterBarrier, 1);
            commandList.Barriers(counterBarrierBatch);

            BindResourceDescriptorIndices(commandList, m_createCommandPipelineState.GetResourceDescriptorSlots());
            uintRootConstants[CLOD_NUM_RASTER_BUCKETS] = context.materialManager->GetRasterBucketCount();
            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                uintRootConstants);

            commandList.BindPipeline(m_createCommandPipelineState.GetAPIPipelineState().GetHandle());
            commandList.Dispatch(1, 1, 1);

            // TODO: Move to post-raster pass
            //rhi::BufferBarrier replayDispatchBarriers[2] = {};
            //replayDispatchBarriers[0].buffer = m_occlusionReplayBuffer->GetAPIResource().GetHandle();
            //replayDispatchBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
            //replayDispatchBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;

            //replayDispatchBarriers[1].buffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
            //replayDispatchBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
            //replayDispatchBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;

            //rhi::BarrierBatch replayBarrierBatch{};
            //replayBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(replayDispatchBarriers, 2);
            //commandList.Barriers(replayBarrierBatch);

            //rhi::WorkGraphDispatchDesc replayDispatchDesc{};
            //replayDispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::MultiNodeGpuInput;
            //replayDispatchDesc.multiNodeGpuInput.inputBuffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
            //replayDispatchDesc.multiNodeGpuInput.inputAddressOffset = 0;
            //commandList.DispatchWorkGraph(replayDispatchDesc);

            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
			auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
			if (!updateContext) return;
			auto& context = *updateContext;
            m_declaredResourcesChanged = false;

            uint32_t zero = 0u;
            BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_visibleClustersCounterBuffer), 0);

            if (!m_isFirstPass) {
                return;
            }

            CLodReplayBufferState replayState{};
            replayState.nodeGroupWriteOffsetBytes = 0;
            replayState.meshletWriteOffsetBytes = CLodReplayBufferSizeBytes;
            replayState.nodeGroupDroppedRecords = 0;
            replayState.meshletDroppedRecords = 0;
            BUFFER_UPLOAD(
                &replayState,
                sizeof(CLodReplayBufferState),
                rg::runtime::UploadTarget::FromShared(m_occlusionReplayStateBuffer),
                0);

            std::vector<CLodViewDepthSRVIndex> viewDepthSrvIndices(CLodMaxViewDepthIndices);
            for (uint32_t i = 0; i < CLodMaxViewDepthIndices; ++i) {
                viewDepthSrvIndices[i].cameraBufferIndex = i;
                viewDepthSrvIndices[i].linearDepthSRVIndex = 0;
            }

            context.viewManager->ForEachView([&](uint64_t viewID) {
                const auto* view = context.viewManager->Get(viewID);
                if (!view || !view->gpu.linearDepthMap) {
                    return;
                }

                const uint32_t cameraBufferIndex = view->gpu.cameraBufferIndex;
                if (cameraBufferIndex >= CLodMaxViewDepthIndices) {
                    return;
                }

                const auto linearDepthMap = view->gpu.linearDepthMap;
                uint32_t slice = 0;
                if (view->cameraInfo.depthBufferArrayIndex >= 0) {
                    slice = static_cast<uint32_t>(view->cameraInfo.depthBufferArrayIndex);
                }

                const uint32_t maxSlices = linearDepthMap->GetNumSRVSlices();
                if (maxSlices == 0) {
                    return;
                }

                slice = (std::min)(slice, maxSlices - 1);
                viewDepthSrvIndices[cameraBufferIndex].cameraBufferIndex = cameraBufferIndex;
                viewDepthSrvIndices[cameraBufferIndex].linearDepthSRVIndex = linearDepthMap->GetSRVInfo(0, slice).slot.index;
            });

            BUFFER_UPLOAD(
                viewDepthSrvIndices.data(),
                static_cast<uint32_t>(viewDepthSrvIndices.size() * sizeof(CLodViewDepthSRVIndex)),
                rg::runtime::UploadTarget::FromShared(m_viewDepthSrvIndicesBuffer),
                0);

            CLodNodeGpuInput nodeGpuInputs[3] = {};
            CLodMultiNodeGpuInput multiNodeGpuInput{};
            multiNodeGpuInput.numNodeInputs = 2;
            multiNodeGpuInput.pad0 = 0;
            multiNodeGpuInput.nodeInputStride = sizeof(CLodNodeGpuInput);

            if (ID3D12Resource* nodeInputResource = rhi::dx12::get_resource(m_occlusionNodeGpuInputsBuffer->GetAPIResource())) {
                const uint64_t nodeInputBufferAddress = nodeInputResource->GetGPUVirtualAddress();
                multiNodeGpuInput.nodeInputsAddress = nodeInputBufferAddress + sizeof(CLodNodeGpuInput);
            }

            if (ID3D12Resource* replayResource = rhi::dx12::get_resource(m_occlusionReplayBuffer->GetAPIResource())) {
                const uint64_t replayAddress = replayResource->GetGPUVirtualAddress();
                nodeGpuInputs[1].entrypointIndex = 1;
                nodeGpuInputs[1].numRecords = 0;
                nodeGpuInputs[1].recordsAddress = replayAddress;
                nodeGpuInputs[1].recordStride = sizeof(CLodNodeGroupReplayRecord);

                nodeGpuInputs[2].entrypointIndex = 2;
                nodeGpuInputs[2].numRecords = 0;
                nodeGpuInputs[2].recordsAddress = replayAddress + CLodReplayBufferSizeBytes;
                nodeGpuInputs[2].recordStride = sizeof(CLodMeshletReplayRecord);
            }

            static_assert(sizeof(CLodMultiNodeGpuInput) == sizeof(CLodNodeGpuInput));
            std::memcpy(&nodeGpuInputs[0], &multiNodeGpuInput, sizeof(CLodMultiNodeGpuInput));

            BUFFER_UPLOAD(
                nodeGpuInputs,
                sizeof(nodeGpuInputs),
                rg::runtime::UploadTarget::FromShared(m_occlusionNodeGpuInputsBuffer),
                0);

            if (IsCLodWorkGraphTelemetryEnabled()) {
                std::vector<uint32_t> zeroTelemetry(CLodWorkGraphCounterCount, 0u);
                BUFFER_UPLOAD(
                    zeroTelemetry.data(),
                    static_cast<uint32_t>(zeroTelemetry.size() * sizeof(uint32_t)),
                    rg::runtime::UploadTarget::FromShared(m_workGraphTelemetryBuffer),
                    0);
            }
        }

        bool DeclaredResourcesChanged() const override {
            return m_declaredResourcesChanged;
        }

        void Cleanup() override {

        }

    private:
        PipelineResources m_pipelineResources;
        rhi::WorkGraphPtr m_workGraph;
        PipelineState m_createCommandPipelineState;
        std::shared_ptr<Buffer> m_visibleClustersBuffer;
        std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
        std::shared_ptr<Buffer> m_scratchBuffer;
        std::shared_ptr<Buffer> m_histogramIndirectCommand;
        std::shared_ptr<Buffer> m_workGraphTelemetryBuffer;
        std::shared_ptr<Buffer> m_occlusionReplayBuffer;
        std::shared_ptr<Buffer> m_occlusionReplayStateBuffer;
        std::shared_ptr<Buffer> m_occlusionNodeGpuInputsBuffer;
        std::shared_ptr<Buffer> m_viewDepthSrvIndicesBuffer;
        bool m_isFirstPass = true;
        bool m_declaredResourcesChanged = true;
        RenderPhase m_renderPhase = Engine::Primary::GBufferPass;

        void CreatePipelines(
            rhi::Device device,
            rhi::PipelineLayoutHandle globalRootSignature,
            rhi::WorkGraphPtr& outGraph,
            PipelineState& outCreateCommandPipeline)
        {
            // Compile the work-graph library
            ShaderLibraryInfo libInfo(L"shaders/ClusterLOD/workGraphCulling.hlsl", L"lib_6_8");
            auto compiled = PSOManager::GetInstance().CompileShaderLibrary(libInfo);
            m_pipelineResources = compiled.resourceDescriptorSlots;

            rhi::ShaderBinary libDxil{
                compiled.libraryBlob->GetBufferPointer(),
                static_cast<uint32_t>(compiled.libraryBlob->GetBufferSize())
            };

            // Export the node shader symbols from the library
            // These are the *export names* (function symbols), not NodeID strings.
            std::array<rhi::ShaderExportDesc, 6> exports = { {
                { "WG_ObjectCull",   nullptr },
                { "WG_ReplayNodeGroup", nullptr },
                { "WG_ReplayMeshlet", nullptr },
                { "WG_TraverseNodes",     nullptr },
                { "WG_GroupEvaluate",     nullptr },
                { "WG_ClusterCullBuckets",  nullptr },
            } };

            rhi::ShaderLibraryDesc library{};
            library.dxil = libDxil;
            library.exports = rhi::Span<rhi::ShaderExportDesc>(exports.data(), static_cast<uint32_t>(exports.size()));

            std::array<rhi::ShaderLibraryDesc, 1> libraries = { library };

            // Entry point is by NodeID (the [NodeID("ObjectCull")] in HLSL)
            std::array<rhi::NodeIDDesc, 3> entrypoints = { {
                { "ObjectCull", 0 },
                { "ReplayNodeGroup", 0 },
                { "ReplayMeshlet", 0 }
            } };

            // Build the work graph desc
            rhi::WorkGraphDesc wg{};
            wg.programName = "HierarchialCulling";
            wg.flags = rhi::WorkGraphFlags::WorkGraphFlagsIncludeAllAvailableNodes;
            wg.globalRootSignature = globalRootSignature;
            wg.libraries = rhi::Span<rhi::ShaderLibraryDesc>(libraries.data(), static_cast<uint32_t>(libraries.size()));
            wg.entrypoints = rhi::Span<rhi::NodeIDDesc>(entrypoints.data(), static_cast<uint32_t>(entrypoints.size()));
            wg.allowStateObjectAdditions = false;
            wg.debugName = "HierarchialCullingWG";

            // Create
            device.CreateWorkGraph(wg, outGraph);

            // Pipeline to create indirect command
            outCreateCommandPipeline = PSOManager::GetInstance().MakeComputePipeline(
                globalRootSignature,
                L"shaders/ClusterLOD/clodUtil.hlsl",
                L"CreateRasterBucketsHistogramCommandCSMain",
                {},
                "HierarchialLODCommandCreation");
        }
    };

    // --------------------------------------------------------------
    // Raster Bucket Histogram Pass
    // --------------------------------------------------------------

    class RasterBucketHistogramPass : public ComputePass {
    public:
        RasterBucketHistogramPass(
            std::shared_ptr<Buffer> visibleClustersBuffer,
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> histogramIndirectCommand,
            std::shared_ptr<Buffer> histogramBuffer) {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_histogramPipeline);

            // Used by the cluster rasterization pass
            rhi::IndirectArg rasterizeClustersArgs[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
                {.kind = rhi::IndirectArgKind::Dispatch }
            };

            auto device = DeviceManager::GetInstance().GetDevice();

            auto result = device.CreateCommandSignature(
                rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(rasterizeClustersArgs, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(), m_histogramCommandSignature);

            m_visibleClustersBuffer = visibleClustersBuffer;
            m_visibleClustersCounterBuffer = visibleClustersCounterBuffer;
            m_histogramIndirectCommand = histogramIndirectCommand;
            m_histogramBuffer = histogramBuffer;
        }

        ~RasterBucketHistogramPass() {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMaterialDataBuffer)
                .WithIndirectArguments(m_histogramIndirectCommand)
                .WithUnorderedAccess(m_histogramBuffer);
        }

        void Setup() override {
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
            auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            // Set the descriptor heaps
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

            commandList.BindPipeline(m_histogramPipeline.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_histogramPipeline.GetResourceDescriptorSlots());

            uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
            uintRootConstants[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                uintRootConstants);

            // Single-dispatch ExecuteIndirect
            commandList.ExecuteIndirect(m_histogramCommandSignature->GetHandle(), m_histogramIndirectCommand->GetAPIResource().GetHandle(), 0, {}, 0, 1);

            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;

            auto numRasterBuckets = context.materialManager->GetRasterBucketCount();

            // Resize histogram buffer if needed
            if (m_histogramBuffer->GetSize() < static_cast<size_t>(numRasterBuckets) * sizeof(uint32_t)) {
                m_histogramBuffer->ResizeStructured(numRasterBuckets);
            }

            // Clear the histogram buffer
            std::vector<uint32_t> zeroData(numRasterBuckets, 0);
            BUFFER_UPLOAD(zeroData.data(), static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)), rg::runtime::UploadTarget::FromShared(m_histogramBuffer), 0);
        }

        void Cleanup() override {

        }

    private:
        PipelineState m_histogramPipeline;
        rhi::CommandSignaturePtr m_histogramCommandSignature;
        std::shared_ptr<Buffer> m_visibleClustersBuffer;
        std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
        std::shared_ptr<Buffer> m_histogramIndirectCommand;
        std::shared_ptr<Buffer> m_histogramBuffer;

        void CreatePipelines(
            rhi::Device device,
            rhi::PipelineLayoutHandle globalRootSignature,
            PipelineState& outHistogramPipeline)
        {
            outHistogramPipeline = PSOManager::GetInstance().MakeComputePipeline(
                globalRootSignature,
                L"Shaders/ClusterLOD/clodUtil.hlsl",
                L"ClusterRasterBucketsHistogramCSMain");
        }
    };

    // --------------------------------------------------------------
    // Raster Bucket Prefix Sum Passes
    // --------------------------------------------------------------

    class RasterBucketBlockScanPass : public ComputePass {
    public:
        RasterBucketBlockScanPass(
            std::shared_ptr<Buffer> histogramBuffer,
            std::shared_ptr<Buffer> offsetsBuffer,
            std::shared_ptr<Buffer> blockSumsBuffer)
            : m_histogramBuffer(std::move(histogramBuffer)),
            m_offsetsBuffer(std::move(offsetsBuffer)),
            m_blockSumsBuffer(std::move(blockSumsBuffer)) {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"Shaders/ClusterLOD/clodUtil.hlsl",
                L"RasterBucketsBlockScanCS",
                {},
                "CLod_RasterBucketsBlockScanPSO");
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(m_histogramBuffer)
                .WithUnorderedAccess(m_offsetsBuffer, m_blockSumsBuffer);
        }

        void Setup() override {}

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;
            auto& commandList = executionContext.commandList;
            auto& pm = PSOManager::GetInstance();

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

            uint32_t rc[NumMiscUintRootConstants] = {};
            rc[UintRootConstant0] = numBuckets;
            rc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_blockSumsBuffer->GetUAVShaderVisibleInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                rc);

            commandList.Dispatch(numBlocks, 1, 1);
            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;
            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            if (m_offsetsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
                m_offsetsBuffer->ResizeStructured(numBuckets);
            }
            if (m_blockSumsBuffer->GetSize() < static_cast<size_t>(numBlocks) * sizeof(uint32_t)) {
                m_blockSumsBuffer->ResizeStructured(numBlocks);
            }
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        uint32_t m_blockSize = 1024;
        std::shared_ptr<Buffer> m_histogramBuffer;
        std::shared_ptr<Buffer> m_offsetsBuffer;
        std::shared_ptr<Buffer> m_blockSumsBuffer;
    };

    class RasterBucketBlockOffsetsPass : public ComputePass {
    public:
        RasterBucketBlockOffsetsPass(
            std::shared_ptr<Buffer> offsetsBuffer,
            std::shared_ptr<Buffer> blockSumsBuffer,
            std::shared_ptr<Buffer> scannedBlockSumsBuffer,
            std::shared_ptr<Buffer> totalCountBuffer)
            : m_offsetsBuffer(std::move(offsetsBuffer)),
            m_blockSumsBuffer(std::move(blockSumsBuffer)),
            m_scannedBlockSumsBuffer(std::move(scannedBlockSumsBuffer)),
            m_totalCountBuffer(std::move(totalCountBuffer)) {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"Shaders/ClusterLOD/clodUtil.hlsl",
                L"RasterBucketsBlockOffsetsCS",
                {},
                "CLod_RasterBucketsBlockOffsetsPSO");
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(m_blockSumsBuffer)
                .WithUnorderedAccess(m_offsetsBuffer, m_scannedBlockSumsBuffer, m_totalCountBuffer);
        }

        void Setup() override {}

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;

            auto& commandList = executionContext.commandList;
            auto& pm = PSOManager::GetInstance();

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

            uint32_t rc[NumMiscUintRootConstants] = {};
            rc[UintRootConstant0] = numBuckets;
            rc[UintRootConstant1] = numBlocks;
            rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_blockSumsBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_SCANNED_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_scannedBlockSumsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_TOTAL_COUNT_DESCRIPTOR_INDEX] = m_totalCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                rc);

            commandList.Dispatch(1, 1, 1);
            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;
            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            if (m_scannedBlockSumsBuffer->GetSize() < static_cast<size_t>(numBlocks) * sizeof(uint32_t)) {
                m_scannedBlockSumsBuffer->ResizeStructured(numBlocks);
            }
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        uint32_t m_blockSize = 1024;
        std::shared_ptr<Buffer> m_offsetsBuffer;
        std::shared_ptr<Buffer> m_blockSumsBuffer;
        std::shared_ptr<Buffer> m_scannedBlockSumsBuffer;
        std::shared_ptr<Buffer> m_totalCountBuffer;
    };

    class RasterBucketCompactAndArgsPass : public ComputePass {
    public:
        RasterBucketCompactAndArgsPass(
            std::shared_ptr<Buffer> visibleClustersBuffer,
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> compactedBaseCounterBuffer,
            std::shared_ptr<Buffer> indirectCommand,
            std::shared_ptr<Buffer> histogramBuffer,
            std::shared_ptr<Buffer> offsetsBuffer,
            std::shared_ptr<Buffer> writeCursorBuffer,
            std::shared_ptr<Buffer> compactedClustersBuffer,
            std::shared_ptr<Buffer> indirectArgsBuffer,
            uint64_t maxVisibleClusters,
            bool appendToExisting)
            : m_visibleClustersBuffer(std::move(visibleClustersBuffer)),
            m_visibleClustersCounterBuffer(std::move(visibleClustersCounterBuffer)),
            m_compactedBaseCounterBuffer(std::move(compactedBaseCounterBuffer)),
            m_indirectCommand(std::move(indirectCommand)),
            m_histogramBuffer(std::move(histogramBuffer)),
            m_offsetsBuffer(std::move(offsetsBuffer)),
            m_writeCursorBuffer(std::move(writeCursorBuffer)),
            m_compactedClustersBuffer(std::move(compactedClustersBuffer)),
            m_indirectArgsBuffer(std::move(indirectArgsBuffer)),
            m_maxVisibleClusters(maxVisibleClusters),
            m_appendToExisting(appendToExisting)
        {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"shaders/ClusterLOD/clodUtil.hlsl",
                L"CompactClustersAndBuildIndirectArgsCS",
                {},
                "CLod_RasterBucketsCompactAndArgsPSO");

            rhi::IndirectArg args[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
                {.kind = rhi::IndirectArgKind::Dispatch }
            };

            auto device = DeviceManager::GetInstance().GetDevice();
            device.CreateCommandSignature(
                rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_compactionCommandSignature);
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_compactedBaseCounterBuffer,
                m_histogramBuffer,
                m_offsetsBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMaterialDataBuffer)
                .WithUnorderedAccess(
                    m_writeCursorBuffer,
                    m_compactedClustersBuffer,
                    m_indirectArgsBuffer)
                .WithIndirectArguments(m_indirectCommand);
        }

        void Setup() override {
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;
            auto& commandList = executionContext.commandList;
            auto& pm = PSOManager::GetInstance();

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t kThreads = 64;
            const uint64_t maxItems = std::max<uint64_t>(m_maxVisibleClusters, numBuckets);
            const uint32_t groups = static_cast<uint32_t>((maxItems + kThreads - 1u) / kThreads);

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

            unsigned int rc[NumMiscUintRootConstants] = {};
            rc[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_COMPACTED_APPEND_BASE_COUNTER_DESCRIPTOR_INDEX] = m_compactedBaseCounterBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_NUM_RASTER_BUCKETS] = numBuckets | (m_appendToExisting ? 0x80000000u : 0u);
            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                rc);

            commandList.ExecuteIndirect(
                m_compactionCommandSignature->GetHandle(),
                m_indirectCommand->GetAPIResource().GetHandle(),
                0,
                {},
                0,
                1);

            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;
            auto numBuckets = context.materialManager->GetRasterBucketCount();

            if (m_writeCursorBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
                m_writeCursorBuffer->ResizeStructured(numBuckets);
            }
            if (m_indirectArgsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(RasterizeClustersCommand)) {
                m_indirectArgsBuffer->ResizeStructured(numBuckets);
            }

            std::vector<uint32_t> zeroData(numBuckets, 0u);
            BUFFER_UPLOAD(zeroData.data(),
                static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)),
                rg::runtime::UploadTarget::FromShared(m_writeCursorBuffer),
                0);
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        rhi::CommandSignaturePtr m_compactionCommandSignature;

        std::shared_ptr<Buffer> m_visibleClustersBuffer;
        std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
        std::shared_ptr<Buffer> m_compactedBaseCounterBuffer;
        std::shared_ptr<Buffer> m_indirectCommand;
        std::shared_ptr<Buffer> m_histogramBuffer;
        std::shared_ptr<Buffer> m_offsetsBuffer;
        std::shared_ptr<Buffer> m_writeCursorBuffer;
        std::shared_ptr<Buffer> m_compactedClustersBuffer;
        std::shared_ptr<Buffer> m_indirectArgsBuffer;

        uint64_t m_maxVisibleClusters = 0;
        bool m_appendToExisting = false;
    };

    // --------------------------------------------------------------
    // Cluster Rasterization Pass
    // --------------------------------------------------------------

    struct ClusterRasterizationPassInputs {
        bool wireframe;
        bool clearGbuffer;

        friend bool operator==(const ClusterRasterizationPassInputs&, const ClusterRasterizationPassInputs&) = default;
    };

    inline rg::Hash64 HashValue(const ClusterRasterizationPassInputs& i) {
        std::size_t seed = 0;

        boost::hash_combine(seed, i.wireframe);
        boost::hash_combine(seed, i.clearGbuffer);
        return seed;
    }

    class ClusterRasterizationPass : public RenderPass, public IDynamicDeclaredResources {
    public:
        ClusterRasterizationPass(
            ClusterRasterizationPassInputs inputs,
            std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
            std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
            std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer)
            : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer)),
            m_rasterBucketsHistogramBuffer(std::move(rasterBucketsHistogramBuffer)),
            m_rasterBucketsIndirectArgsBuffer(std::move(rasterBucketsIndirectArgsBuffer)) {
            m_wireframe = inputs.wireframe;
            m_clearGbuffer = inputs.clearGbuffer;

            m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
            m_viewRasterInfoBuffer->SetName("CLodViewRasterInfoBuffer");

            // Create rasterization indirect command signature
            rhi::IndirectArg args[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
                {.kind = rhi::IndirectArgKind::DispatchMesh }
            };
            auto device = DeviceManager::GetInstance().GetDevice();
            device.CreateCommandSignature(
                rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterizeClustersCommand) },
                PSOManager::GetInstance().GetRootSignature().GetHandle(),
                m_rasterizationCommandSignature);
        }

        ~ClusterRasterizationPass() {
        }

        void DeclareResourceUsages(RenderPassBuilder* builder) {

            builder->WithShaderResource(
                //Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer,
                Builtin::PerObjectBuffer,
                Builtin::NormalMatrixBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMaterialDataBuffer,
                Builtin::PostSkinningVertices,
                Builtin::CameraBuffer,
                Builtin::CLod::Offsets,
                Builtin::CLod::GroupChunks,
                Builtin::CLod::Groups,
                Builtin::CLod::MeshMetadata,
                Builtin::MeshResources::MeshletTriangles,
                Builtin::MeshResources::MeshletVertexIndices,
                Builtin::MeshResources::MeshletOffsets,
                Builtin::CLod::CompressedMeshletVertexIndices,
                Builtin::CLod::CompressedPositions,
                Builtin::CLod::CompressedNormals,
                m_compactedVisibleClustersBuffer,
                m_rasterBucketsHistogramBuffer,
                m_viewRasterInfoBuffer)
                .WithIndirectArguments(m_rasterBucketsIndirectArgsBuffer)
                .IsGeometryPass();

            for (auto& vb : m_visibilityBuffers) { // Dynamic per-frame
                builder->WithUnorderedAccess(vb);
            }
        }

        void Setup() override {

            //RegisterSRV(Builtin::MeshResources::MeshletOffsets);
            RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
            RegisterSRV(Builtin::MeshResources::MeshletTriangles);
			RegisterSRV(Builtin::CLod::Offsets);
            RegisterSRV(Builtin::CLod::GroupChunks);
			RegisterSRV(Builtin::CLod::Groups);
            RegisterSRV(Builtin::NormalMatrixBuffer);
            RegisterSRV(Builtin::PostSkinningVertices);
            RegisterSRV(Builtin::PerObjectBuffer);
            RegisterSRV(Builtin::CameraBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
            RegisterSRV(Builtin::MeshResources::MeshletOffsets);
            RegisterSRV(Builtin::CLod::CompressedMeshletVertexIndices);
			RegisterSRV(Builtin::CLod::CompressedPositions);
			RegisterSRV(Builtin::CLod::CompressedNormals);
			RegisterSRV(Builtin::CLod::MeshMetadata);

            //RegisterSRV(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer);
        }

        // Note: relies on Update() running before DeclareResourceUsages(). If this ever changes, we may need a new approach.
        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
            auto& context = *updateContext;

            // Build per-view raster metadata used by CLod mesh/pixel shaders.
            auto numViews = context.viewManager->GetCameraBufferSize();

            m_visibilityBuffers.clear();

            uint32_t maxViewWidth = 1;
            uint32_t maxViewHeight = 1;

            context.viewManager->ForEachView([&](uint64_t v) {
                auto viewInfo = context.viewManager->Get(v);
                if (viewInfo->gpu.visibilityBuffer != nullptr) {
                    maxViewWidth = std::max(maxViewWidth, viewInfo->gpu.visibilityBuffer->GetWidth());
                    maxViewHeight = std::max(maxViewHeight, viewInfo->gpu.visibilityBuffer->GetHeight());
                }
                });

            std::vector<CLodViewRasterInfo> viewRasterInfo(numViews);
            context.viewManager->ForEachView([&](uint64_t v) {
                auto viewInfo = context.viewManager->Get(v);
                if (viewInfo->gpu.visibilityBuffer != nullptr) {
                    auto cameraIndex = viewInfo->gpu.cameraBufferIndex;

                    CLodViewRasterInfo info{};
                    info.visibilityUAVDescriptorIndex = viewInfo->gpu.visibilityBuffer->GetUAVShaderVisibleInfo(0).slot.index;
                    info.scissorMinX = 0;
                    info.scissorMinY = 0;
                    info.scissorMaxX = viewInfo->gpu.visibilityBuffer->GetWidth();
                    info.scissorMaxY = viewInfo->gpu.visibilityBuffer->GetHeight();
                    info.viewportScaleX = static_cast<float>(info.scissorMaxX) / static_cast<float>(maxViewWidth);
                    info.viewportScaleY = static_cast<float>(info.scissorMaxY) / static_cast<float>(maxViewHeight);
                    viewRasterInfo[cameraIndex] = info;

                    m_visibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
                }
                });

            m_passWidth = maxViewWidth;
            m_passHeight = maxViewHeight;

            // Check if per-view raster metadata changed
            if (m_viewRasterInfos != viewRasterInfo) {
                m_viewRasterInfos = viewRasterInfo;
                // Update the buffer
                m_viewRasterInfoBuffer->ResizeStructured(static_cast<uint32_t>(m_viewRasterInfos.size()));
                BUFFER_UPLOAD(
                    m_viewRasterInfos.data(),
                    static_cast<uint32_t>(m_viewRasterInfos.size() * sizeof(CLodViewRasterInfo)),
                    rg::runtime::UploadTarget::FromShared(m_viewRasterInfoBuffer),
                    0);
                m_declaredResourcesChanged = true;
            }
            else {
                m_declaredResourcesChanged = false;
            }
        }

        bool DeclaredResourcesChanged() const override {
            return m_declaredResourcesChanged;
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            rhi::PassBeginInfo p{};
            p.width = m_passWidth;
            p.height = m_passHeight;
            p.debugName = "CLod raster pass";

            executionContext.commandList.BeginPass(p);

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

            // Root signature
            commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());

            auto& psoManager = PSOManager::GetInstance();

            uint32_t misc[NumMiscUintRootConstants] = {};
            misc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_rasterBucketsHistogramBuffer->GetSRVInfo(0).slot.index;
            misc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
            misc[CLOD_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
            commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            if (numBuckets == 0) {
                return {};
            }

            auto apiResource = m_rasterBucketsIndirectArgsBuffer->GetAPIResource();
            // For each raster bucket, we have one ExecuteIndirect into a DispatchMesh command
            auto stride = sizeof(RasterizeClustersCommand);
            for (uint32_t i = 0; i < numBuckets; ++i) { // TODO: Compaction for zero-count buckets?

                auto flags = context.materialManager->GetRasterFlagsForBucket(i);
                auto& pso = psoManager.GetClusterLODRasterPSO(
                    flags,
                    m_wireframe);

                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                const uint64_t argOffset = static_cast<uint64_t>(i) * stride;
                commandList.ExecuteIndirect(
                    m_rasterizationCommandSignature->GetHandle(),
                    apiResource.GetHandle(),
                    argOffset,
                    {},
                    0,
                    1);
            }

            return {};
        }

        void Cleanup() override {
        }

    private:

        bool m_wireframe;
        bool m_meshShaders;
        bool m_clearGbuffer = true;

        std::vector<CLodViewRasterInfo> m_viewRasterInfos;
        std::vector<std::shared_ptr<PixelBuffer>> m_visibilityBuffers;

        std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
        std::shared_ptr<Buffer> m_rasterBucketsHistogramBuffer;
        std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;

        rhi::CommandSignaturePtr m_rasterizationCommandSignature;

        std::shared_ptr<Buffer> m_viewRasterInfoBuffer;
        uint32_t m_passWidth = 1;
        uint32_t m_passHeight = 1;
        bool m_declaredResourcesChanged = true;

        RenderPhase m_renderPhase = Engine::Primary::GBufferPass;
    };

};