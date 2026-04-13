#pragma once

#include <memory>

#include "Managers/Singletons/PSOManager.h"
#include "RenderPasses/Base/RenderPass.h"

class Buffer;
class PixelBuffer;

class FixedSliceScalarVBOITEarlyDepthPass final : public RenderPass {
public:
    FixedSliceScalarVBOITEarlyDepthPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture,
        std::shared_ptr<PixelBuffer> earlyDepthTexture);

    void DeclareResourceUsages(RenderPassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_zeroTransmittanceSliceTexture;
    std::shared_ptr<PixelBuffer> m_earlyDepthTexture;
    PipelineState m_pso;
};