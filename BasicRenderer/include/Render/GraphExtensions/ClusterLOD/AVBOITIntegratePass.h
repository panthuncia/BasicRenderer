#pragma once

#include <cstdint>
#include <memory>

#include "Interfaces/IDynamicDeclaredResources.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct AVBOITIntegrateBindings {
    org::ResourceBindingToken config, state, occupancy;
};

class AVBOITIntegratePass final : public org::TypedRenderGraphPass<AVBOITIntegratePass,
    br::render::PreparedComputeDispatch, AVBOITIntegrateBindings>, public IDynamicDeclaredResources {
public:
    AVBOITIntegratePass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> fitStateBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> coverageTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<PixelBuffer> scalarExtinctionTexture,
        std::shared_ptr<PixelBuffer> chromaticExtinctionTexture,
        std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
        std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture);

    AVBOITIntegrateBindings Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    bool DeclaredResourcesChanged() const override;
    br::render::PreparedComputeDispatch Prepare(const AVBOITIntegrateBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const AVBOITIntegrateBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_fitStateBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_coverageTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<PixelBuffer> m_scalarExtinctionTexture;
    std::shared_ptr<PixelBuffer> m_chromaticExtinctionTexture;
    std::shared_ptr<PixelBuffer> m_integratedTransmittanceTexture;
    std::shared_ptr<PixelBuffer> m_zeroTransmittanceSliceTexture;
    bool m_declaredResourcesChanged = true;
    PipelineState m_pso;
};
