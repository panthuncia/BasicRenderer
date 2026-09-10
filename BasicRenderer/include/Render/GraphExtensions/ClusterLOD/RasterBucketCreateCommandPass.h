#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

struct RasterBucketCreateCommandBindings {
    org::ResourceBindingToken visibleCount, indirectCommand, replayState, nodeInputs;
    uint32_t numBuckets = 0;
    uint32_t visibleCapacity = 0;
    bool enabled = false;
    bool patchReplay = false;
};

class RasterBucketCreateCommandPass : public org::TypedRenderGraphPass<RasterBucketCreateCommandPass,
    br::render::PreparedComputeDispatch, RasterBucketCreateCommandBindings> {
public:
    RasterBucketCreateCommandPass(
        std::shared_ptr<Buffer> visibleClustersCounterBuffer,
        std::shared_ptr<Buffer> histogramIndirectCommand,
        std::shared_ptr<Buffer> occlusionReplayStateBuffer,
        std::shared_ptr<Buffer> occlusionNodeGpuInputsBuffer,
        uint32_t visibleClustersCapacity,
        bool runWhenComputeSWRasterEnabledOnly = false,
        bool patchReplayNodeInputs = false);

    RasterBucketCreateCommandBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const RasterBucketCreateCommandBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const RasterBucketCreateCommandBindings&, const br::render::PreparedComputeDispatch& data, org::PassRecordContext& context) {
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
    uint32_t m_numBuckets = 0;
    bool m_enabled = false;
};
