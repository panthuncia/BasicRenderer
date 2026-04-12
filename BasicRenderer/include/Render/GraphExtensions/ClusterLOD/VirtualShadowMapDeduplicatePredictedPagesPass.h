#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;

class VirtualShadowMapDeduplicatePredictedPagesPass final : public ComputePass {
public:
    VirtualShadowMapDeduplicatePredictedPagesPass(
        std::shared_ptr<Buffer> predictiveRawPagesBuffer,
        std::shared_ptr<Buffer> predictiveRawPageCountBuffer,
        std::shared_ptr<Buffer> predictedScratchBitsetBuffer,
        std::shared_ptr<Buffer> predictedPagesBuffer,
        std::shared_ptr<Buffer> predictedPageCountBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_clearStatePso;
    PipelineState m_deduplicatePso;
    std::shared_ptr<Buffer> m_predictiveRawPagesBuffer;
    std::shared_ptr<Buffer> m_predictiveRawPageCountBuffer;
    std::shared_ptr<Buffer> m_predictedScratchBitsetBuffer;
    std::shared_ptr<Buffer> m_predictedPagesBuffer;
    std::shared_ptr<Buffer> m_predictedPageCountBuffer;
};