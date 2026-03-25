#pragma once

#include <memory>
#include <vector>

#include "Render/RenderGraph/RenderGraph.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"

class Buffer;
class CLodStreamingSystem;

class CLodExtension final : public RenderGraph::IRenderGraphExtension {
public:
    explicit CLodExtension(CLodExtensionType type, uint32_t maxVisibleClusters);
    ~CLodExtension();

    void Initialize(RenderGraph& rg) override;
    void OnRegistryReset(ResourceRegistry* reg) override;
    void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override;
    void GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override;

private:
    CLodExtensionType m_type;
    uint32_t m_maxVisibleClusters = 0u;

    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_workGraphTelemetryBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayStateBuffer;
    std::shared_ptr<Buffer> m_occlusionNodeGpuInputsBuffer;
    std::shared_ptr<Buffer> m_viewDepthSrvIndicesBuffer;

    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBuffer;

    std::shared_ptr<Buffer> m_rasterBucketsOffsetsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsBlockSumsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsScannedBlockSumsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsTotalCountBuffer;

    std::shared_ptr<Buffer> m_visibleClustersCounterBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferSw;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferPhase2Sw;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferSw;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferPhase2Sw;

    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;

    std::shared_ptr<Buffer> m_reyesFullClusterOutputsBuffer;
    std::shared_ptr<Buffer> m_reyesFullClusterOutputsCounterBuffer;
    std::shared_ptr<Buffer> m_reyesOwnedClustersBuffer;
    std::shared_ptr<Buffer> m_reyesOwnedClustersCounterBuffer;
    std::shared_ptr<Buffer> m_reyesOwnershipBitsetBuffer;
    std::shared_ptr<Buffer> m_reyesOwnershipBitsetBufferPhase2;
    std::shared_ptr<Buffer> m_reyesClassifyIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_reyesClassifyIndirectArgsBufferPhase2;
    std::shared_ptr<Buffer> m_reyesSplitIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_reyesSplitIndirectArgsBufferPhase2;
    std::shared_ptr<Buffer> m_reyesSplitQueueBufferA;
    std::shared_ptr<Buffer> m_reyesSplitQueueCounterBufferA;
    std::shared_ptr<Buffer> m_reyesSplitQueueOverflowBufferA;
    std::shared_ptr<Buffer> m_reyesSplitQueueBufferB;
    std::shared_ptr<Buffer> m_reyesSplitQueueCounterBufferB;
    std::shared_ptr<Buffer> m_reyesSplitQueueOverflowBufferB;
    std::shared_ptr<Buffer> m_reyesDiceQueueBuffer;
    std::shared_ptr<Buffer> m_reyesDiceQueueCounterBuffer;
    std::shared_ptr<Buffer> m_reyesDiceQueueOverflowBuffer;
    std::shared_ptr<Buffer> m_reyesDiceIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_reyesDiceIndirectArgsBufferPhase2;
    std::shared_ptr<Buffer> m_reyesTelemetryBufferPhase1;
    std::shared_ptr<Buffer> m_reyesTelemetryBufferPhase2;

    std::shared_ptr<Buffer> m_swVisibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_swVisibleClustersCounterBufferPhase2;
    std::shared_ptr<Buffer> m_sortedToUnsortedMappingBuffer;
    std::shared_ptr<Buffer> m_viewRasterInfoBuffer;
    std::shared_ptr<Buffer> m_deepVisibilityNodesBuffer;
    std::shared_ptr<Buffer> m_deepVisibilityCounterBuffer;
    std::shared_ptr<Buffer> m_deepVisibilityOverflowCounterBuffer;
    std::shared_ptr<Buffer> m_deepVisibilityStatsBuffer;

    std::unique_ptr<CLodStreamingSystem> m_streamingSystem;
};
