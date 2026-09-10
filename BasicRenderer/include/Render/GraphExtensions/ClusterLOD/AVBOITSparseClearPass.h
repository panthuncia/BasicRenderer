#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct AVBOITSparseClearBindings { org::ResourceBindingToken config, occupancy; };
class AVBOITSparseClearPass final : public org::TypedRenderGraphPass<AVBOITSparseClearPass, br::render::PreparedComputeDispatch, AVBOITSparseClearBindings> {
public:
    AVBOITSparseClearPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<PixelBuffer> scalarExtinctionTexture,
        std::shared_ptr<PixelBuffer> chromaticExtinctionTexture,
        std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture);

    AVBOITSparseClearBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const AVBOITSparseClearBindings&, const org::PassPrepareContext&) const;
    static void Record(const AVBOITSparseClearBindings&, const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<PixelBuffer> m_scalarExtinctionTexture;
    std::shared_ptr<PixelBuffer> m_chromaticExtinctionTexture;
    std::shared_ptr<PixelBuffer> m_zeroTransmittanceSliceTexture;
    PipelineState m_pso;
};
