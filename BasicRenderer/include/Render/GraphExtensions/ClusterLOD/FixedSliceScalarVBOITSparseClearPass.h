#pragma once

#include <memory>

#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class FixedSliceScalarVBOITSparseClearPass final : public ComputePass {
public:
    FixedSliceScalarVBOITSparseClearPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<PixelBuffer> extinctionTexture,
        std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<PixelBuffer> m_extinctionTexture;
    std::shared_ptr<PixelBuffer> m_zeroTransmittanceSliceTexture;
    PipelineState m_pso;
};