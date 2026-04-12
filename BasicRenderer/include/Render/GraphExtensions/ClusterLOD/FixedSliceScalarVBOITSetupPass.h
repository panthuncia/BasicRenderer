#pragma once

#include <memory>

#include "RenderPasses/Base/RenderPass.h"

class Buffer;
class PixelBuffer;

class FixedSliceScalarVBOITSetupPass final : public RenderPass {
public:
    FixedSliceScalarVBOITSetupPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> extinctionTexture,
        std::shared_ptr<PixelBuffer> integratedTransmittanceTexture);

    void DeclareResourceUsages(RenderPassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_extinctionTexture;
    std::shared_ptr<PixelBuffer> m_integratedTransmittanceTexture;
};