#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

enum class ReyesReplayMergeKind
{
    Split,
    Dice
};

class ReyesReplayMergePass final : public org::TypedRenderGraphPass<ReyesReplayMergePass, br::render::PreparedComputeIndirect> {
public:
    ReyesReplayMergePass(
        ReyesReplayMergeKind kind,
        std::shared_ptr<Buffer> sourceQueueBuffer,
        std::shared_ptr<Buffer> sourceQueueCounterBuffer,
        std::shared_ptr<Buffer> destQueueBuffer,
        std::shared_ptr<Buffer> destQueueCounterBuffer,
        std::shared_ptr<Buffer> destQueueOverflowBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<Buffer> telemetryBuffer,
        uint32_t destQueueCapacity);

    void Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    br::render::PreparedComputeIndirect Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeIndirect&, org::PassRecordContext&);

private:
    ReyesReplayMergeKind m_kind = ReyesReplayMergeKind::Split;
    std::shared_ptr<Buffer> m_sourceQueueBuffer;
    std::shared_ptr<Buffer> m_sourceQueueCounterBuffer;
    std::shared_ptr<Buffer> m_destQueueBuffer;
    std::shared_ptr<Buffer> m_destQueueCounterBuffer;
    std::shared_ptr<Buffer> m_destQueueOverflowBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    ResourceBindingToken m_indirectArgumentsBinding{};
    std::shared_ptr<Buffer> m_telemetryBuffer;
    uint32_t m_destQueueCapacity = 0u;
    PipelineState m_pso;
    std::shared_ptr<rhi::CommandSignaturePtr> m_commandSignature;
};
