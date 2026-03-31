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
    bool IsReyesTessellationDisabled() const;
    void EnsureReyesResourcesInitialized();
    void SyncReyesResourceEntities(bool enabled);

    CLodExtensionType m_type;
    uint32_t m_maxVisibleClusters = 0u;
    uint32_t m_reyesFullClusterOutputCapacity = 0u;
    uint32_t m_reyesOwnedClusterCapacity = 0u;
    uint32_t m_reyesSplitQueueCapacity = 0u;
    uint32_t m_reyesDiceQueueCapacity = 0u;
    uint32_t m_reyesDiceQueuePhysicalCapacity = 0u;
    uint32_t m_reyesRasterWorkCapacity = 0u;
    uint32_t m_reyesOwnershipBitsetWordCount = 0u;
    uint64_t m_reyesRequestedBudgetBytes = 0u;
    uint64_t m_reyesAllocatedBudgetBytes = 0u;
    bool m_reyesBudgetLimited = false;

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
    std::shared_ptr<Buffer> m_rasterBucketsTotalCountBufferPhase1;
    std::shared_ptr<Buffer> m_rasterBucketsTotalCountBufferPhase1Sw;

    std::shared_ptr<Buffer> m_visibleClustersCounterBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferSw;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferPhase2Sw;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferSw;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferPhase2Sw;

    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBuffer;
    // TODO: There was a strange bug here where CLod raster indirect command buffers ended up with invalid data, even immediately
    // after the data was written, if that buffer was shared between phase 1 and phase 2 of the CLod rasterization process. 
    // The root cause is currently unknown, but it may be related to synchronization issues around indirect command consumption 
    // or an underlying issue with shared CLod raster resources. For now, the workaround is to duplicate the raster indirect args 
    // buffer for phase 1 and phase 2, even though they should be mutually-exclusive with correct synchronization
    //
    // Further testing is required to determine whether the root cause is a CLod usage bug, a synchronization bug
    // specific to indirect command consumption, or a larger underlying issue affecting resources that are explicitly reused 
    // within the same frame (though no other passes exhibit similar behavior)
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBufferPhase2;

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
    std::shared_ptr<Buffer> m_reyesDiceQueuePhase1CountBuffer;
    std::shared_ptr<Buffer> m_reyesDiceQueueOverflowBuffer;
    std::shared_ptr<Buffer> m_reyesRasterWorkBuffer;
    std::shared_ptr<Buffer> m_reyesRasterWorkCounterBuffer;
    std::shared_ptr<Buffer> m_reyesRasterWorkIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_reyesTessTableConfigsBuffer;
    std::shared_ptr<Buffer> m_reyesTessTableVerticesBuffer;
    std::shared_ptr<Buffer> m_reyesTessTableTrianglesBuffer;
    std::shared_ptr<Buffer> m_reyesDiceIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_reyesDiceIndirectArgsBufferPhase2;
    std::shared_ptr<Buffer> m_reyesRasterWorkBufferPhase2;
    std::shared_ptr<Buffer> m_reyesRasterWorkCounterBufferPhase2;
    std::shared_ptr<Buffer> m_reyesRasterWorkIndirectArgsBufferPhase2;
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
