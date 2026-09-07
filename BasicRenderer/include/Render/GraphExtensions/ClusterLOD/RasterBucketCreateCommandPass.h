#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

class RasterBucketCreateCommandPass : public org::TypedRenderGraphPass<RasterBucketCreateCommandPass, br::render::PreparedComputeDispatch> {
public:
    RasterBucketCreateCommandPass(
        std::shared_ptr<Buffer> visibleClustersCounterBuffer,
        std::shared_ptr<Buffer> histogramIndirectCommand,
        std::shared_ptr<Buffer> occlusionReplayStateBuffer,
        std::shared_ptr<Buffer> occlusionNodeGpuInputsBuffer,
        uint32_t visibleClustersCapacity,
        bool runWhenComputeSWRasterEnabledOnly = false,
        bool patchReplayNodeInputs = false);

    void Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& context) {
        br::render::RecordPreparedComputeDispatch(data, context);
    }
    void Update(const UpdateExecutionContext& executionContext) override;

private:
    PipelineState m_pso;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<Buffer> m_occlusionReplayStateBuffer;
    std::shared_ptr<Buffer> m_occlusionNodeGpuInputsBuffer;
    uint32_t m_visibleClustersCapacity = 0;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
    bool m_patchReplayNodeInputs = false;
};
