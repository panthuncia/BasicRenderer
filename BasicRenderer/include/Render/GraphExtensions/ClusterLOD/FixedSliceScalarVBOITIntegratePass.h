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
        std::shared_ptr<Buffer> reyesDiceQueueBuffer,
        std::shared_ptr<Buffer> reyesTessTableConfigsBuffer,
        std::shared_ptr<Buffer> reyesTessTableVerticesBuffer,
        std::shared_ptr<Buffer> reyesTessTableTrianglesBuffer,
        std::shared_ptr<Buffer> deepVisibilityNodesBuffer,
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> extinctionTexture,
        std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
        uint32_t patchVisibilityIndexBase);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    bool DeclaredResourcesChanged() const override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_reyesDiceQueueBuffer;
    std::shared_ptr<Buffer> m_reyesTessTableConfigsBuffer;
    std::shared_ptr<Buffer> m_reyesTessTableVerticesBuffer;
    std::shared_ptr<Buffer> m_reyesTessTableTrianglesBuffer;
    std::shared_ptr<Buffer> m_deepVisibilityNodesBuffer;
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_extinctionTexture;
    std::shared_ptr<PixelBuffer> m_integratedTransmittanceTexture;
    uint32_t m_patchVisibilityIndexBase = 0u;
    std::shared_ptr<PixelBuffer> m_primaryHeadPointerTexture;
    bool m_declaredResourcesChanged = true;
    PipelineState m_pso;
};