#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;

class VirtualShadowMapExpandPredictedPagesPass final : public ComputePass {
public:
    VirtualShadowMapExpandPredictedPagesPass(
        std::shared_ptr<Buffer> predictiveCandidatesBuffer,
        std::shared_ptr<Buffer> predictiveCandidateCountBuffer,
        std::shared_ptr<Buffer> predictiveRawPagesBuffer,
        std::shared_ptr<Buffer> predictiveRawPageCountBuffer,
        std::shared_ptr<Buffer> clipmapInfoBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_pso;
    std::shared_ptr<Buffer> m_predictiveCandidatesBuffer;
    std::shared_ptr<Buffer> m_predictiveCandidateCountBuffer;
    std::shared_ptr<Buffer> m_predictiveRawPagesBuffer;
    std::shared_ptr<Buffer> m_predictiveRawPageCountBuffer;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
};