#pragma once

#include <memory>

#include "RenderPasses/Base/ComputePass.h"

class Buffer;

class FixedSliceScalarVBOITAdaptiveFitPass final : public ComputePass {
public:
    FixedSliceScalarVBOITAdaptiveFitPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> fitStateBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_fitStateBuffer;
    PipelineState m_pso;
};