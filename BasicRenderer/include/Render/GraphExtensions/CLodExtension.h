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

    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;

    std::unique_ptr<CLodStreamingSystem> m_streamingSystem;
};
