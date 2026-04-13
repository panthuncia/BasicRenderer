#pragma once

#include <cstdint>
#include <memory>

#include "Interfaces/IDynamicDeclaredResources.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class FixedSliceScalarVBOITIntegratePass final : public ComputePass, public IDynamicDeclaredResources {
public:
    FixedSliceScalarVBOITIntegratePass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> fitStateBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> coverageTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<PixelBuffer> extinctionTexture,
        std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
        std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    bool DeclaredResourcesChanged() const override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_fitStateBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_coverageTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<PixelBuffer> m_extinctionTexture;
    std::shared_ptr<PixelBuffer> m_integratedTransmittanceTexture;
    std::shared_ptr<PixelBuffer> m_zeroTransmittanceSliceTexture;
    bool m_declaredResourcesChanged = true;
    PipelineState m_pso;
};