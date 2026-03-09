#pragma once

#include <cstdint>
#include <memory>

#include <boost/container_hash/hash.hpp>
#include <rhi.h>

#include "BuiltinRenderPasses.h"
#include "Interfaces/IDynamicDeclaredResources.h"
#include "Render/RenderPhase.h"
#include "RenderPasses/Base/ComputePass.h"
#include "Render/RenderGraph/RenderGraph.h"

class Buffer;

struct HierarchialCullingPassInputs {
    bool isFirstPass;
    unsigned int maxVisibleClusters;

    friend bool operator==(const HierarchialCullingPassInputs&, const HierarchialCullingPassInputs&) = default;
};

inline rg::Hash64 HashValue(const HierarchialCullingPassInputs& i) {
    std::size_t seed = 0;

    boost::hash_combine(seed, i.isFirstPass);
    boost::hash_combine(seed, i.maxVisibleClusters);
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
        std::shared_ptr<Buffer> viewDepthSrvIndicesBuffer);
    ~HierarchialCullingPass();

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Update(const UpdateExecutionContext& executionContext) override;
    bool DeclaredResourcesChanged() const override;
    void Cleanup() override;

private:
    struct ObjectCullRecord
    {
        uint32_t viewDataIndex;
        uint32_t activeDrawSetIndicesSRVIndex;
        uint32_t activeDrawCount;
        uint32_t dispatchGridX;
        uint32_t dispatchGridY;
        uint32_t dispatchGridZ;
    };

    void CreatePipelines(
        rhi::Device device,
        rhi::PipelineLayoutHandle globalRootSignature,
        rhi::WorkGraphPtr& outGraph,
        PipelineState& outCreateCommandPipeline);

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
    unsigned int m_maxVisibleClusters = 0u;
    RenderPhase m_renderPhase = Engine::Primary::GBufferPass;
};
