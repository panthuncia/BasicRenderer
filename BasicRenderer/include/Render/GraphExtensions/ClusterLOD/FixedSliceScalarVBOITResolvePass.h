#pragma once

#include <memory>

#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class FixedSliceScalarVBOITResolvePass final : public ComputePass {
public:
    FixedSliceScalarVBOITResolvePass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
        std::shared_ptr<PixelBuffer> accumulationTexture);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_integratedTransmittanceTexture;
    std::shared_ptr<PixelBuffer> m_accumulationTexture;
    PipelineState m_pso;
};
