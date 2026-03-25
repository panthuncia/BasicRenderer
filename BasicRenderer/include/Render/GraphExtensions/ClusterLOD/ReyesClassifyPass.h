#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;

class ReyesClassifyPass final : public ComputePass {
public:
    ReyesClassifyPass(
        std::shared_ptr<Buffer> visibleClustersBuffer,
        std::shared_ptr<Buffer> visibleClustersCounterBuffer,
        std::shared_ptr<Buffer> fullClusterOutputsBuffer,
        std::shared_ptr<Buffer> fullClusterCounterBuffer,
        std::shared_ptr<Buffer> splitQueueBuffer,
        std::shared_ptr<Buffer> splitQueueCounterBuffer,
        std::shared_ptr<Buffer> diceQueueBuffer,
        std::shared_ptr<Buffer> diceQueueCounterBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<Buffer> telemetryBuffer,
        uint32_t phaseIndex);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Update(const UpdateExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_fullClusterOutputsBuffer;
    std::shared_ptr<Buffer> m_fullClusterCounterBuffer;
    std::shared_ptr<Buffer> m_splitQueueBuffer;
    std::shared_ptr<Buffer> m_splitQueueCounterBuffer;
    std::shared_ptr<Buffer> m_diceQueueBuffer;
    std::shared_ptr<Buffer> m_diceQueueCounterBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    std::shared_ptr<Buffer> m_telemetryBuffer;
    uint32_t m_phaseIndex = 0u;
    PipelineState m_pso;
    rhi::CommandSignaturePtr m_commandSignature;
};