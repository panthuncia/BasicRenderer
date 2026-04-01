#pragma once

#include <cstdint>
#include <memory>

#include <boost/container_hash/hash.hpp>
#include <rhi.h>

#include "BuiltinRenderPasses.h"
#include "Interfaces/IDynamicDeclaredResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderPhase.h"
#include "RenderPasses/Base/ComputePass.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Resources/PixelBuffer.h"

class Buffer;
class PixelBuffer;
class ResourceGroup;

enum class HierarchialCullingWorkGraphMode : uint8_t {
    HardwareOnly,
    SoftwareRasterCompute,
    SoftwareRasterWorkGraph,
};

struct HierarchialCullingPassInputs {
    bool isFirstPass;
    unsigned int maxVisibleClusters;
    HierarchialCullingWorkGraphMode workGraphMode = HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph;
    RenderPhase renderPhase;
    bool clodOnlyWorkloads = false;
    bool useShadowCascadeViews = false;
    CLodRasterOutputKind rasterOutputKind = CLodRasterOutputKind::VisibilityBuffer;

    RG_DEFINE_PASS_INPUTS(HierarchialCullingPassInputs, &HierarchialCullingPassInputs::isFirstPass, &HierarchialCullingPassInputs::maxVisibleClusters, &HierarchialCullingPassInputs::workGraphMode, &HierarchialCullingPassInputs::renderPhase, &HierarchialCullingPassInputs::clodOnlyWorkloads, &HierarchialCullingPassInputs::useShadowCascadeViews, &HierarchialCullingPassInputs::rasterOutputKind);
};

class HierarchialCullingPass : public ComputePass, public IDynamicDeclaredResources {
public:
    HierarchialCullingPass(
        HierarchialCullingPassInputs inputs,
        std::shared_ptr<Buffer> visibleClustersBuffer,
        std::shared_ptr<Buffer> visibleClustersCounterBuffer,
        std::shared_ptr<Buffer> swVisibleClustersCounterBuffer,
        std::shared_ptr<Buffer> histogramIndirectCommand,
        std::shared_ptr<Buffer> workGraphTelemetryBuffer,
        std::shared_ptr<Buffer> occlusionReplayBuffer,
        std::shared_ptr<Buffer> occlusionReplayStateBuffer,
        std::shared_ptr<Buffer> occlusionNodeGpuInputsBuffer,
        std::shared_ptr<Buffer> viewDepthSrvIndicesBuffer,
        std::shared_ptr<Buffer> viewRasterInfoBuffer,
        std::shared_ptr<ResourceGroup> slabResourceGroup = nullptr,
        std::shared_ptr<Buffer> phase1VisibleClustersCounterBuffer = nullptr,
        std::shared_ptr<Buffer> swWriteBaseCounterBuffer = nullptr);
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
    std::shared_ptr<Buffer> m_swVisibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_scratchBuffer;
    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<Buffer> m_workGraphTelemetryBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayStateBuffer;
    std::shared_ptr<Buffer> m_occlusionNodeGpuInputsBuffer;
    std::shared_ptr<Buffer> m_viewDepthSrvIndicesBuffer;
    std::shared_ptr<Buffer> m_viewRasterInfoBuffer;
    std::shared_ptr<ResourceGroup> m_slabResourceGroup;
    std::shared_ptr<Buffer> m_phase1VisibleClustersCounterBuffer; // Phase 2 only: Phase 1's HW counter for write offset
    std::shared_ptr<Buffer> m_swWriteBaseCounterBuffer; // Phase 2 only: Phase 1's SW counter for top-down write offset
    std::vector<std::shared_ptr<PixelBuffer>> m_visibilityBuffers;
    bool m_isFirstPass = true;
    bool m_declaredResourcesChanged = true;
    unsigned int m_maxVisibleClusters = 0u;
    HierarchialCullingWorkGraphMode m_workGraphMode = HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph;
    CLodRasterOutputKind m_rasterOutputKind = CLodRasterOutputKind::VisibilityBuffer;
    RenderPhase m_renderPhase;
    bool m_clodOnlyWorkloads = false;
    bool m_useShadowCascadeViews = false;
};
