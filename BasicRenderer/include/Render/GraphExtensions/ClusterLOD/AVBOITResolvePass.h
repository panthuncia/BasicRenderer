#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct AVBOITResolveBindings {
    org::ResourceBindingToken config, accumulation, normalization, extinction;
};

class AVBOITResolvePass final : public org::TypedRenderGraphPass<AVBOITResolvePass,
    br::render::PreparedComputeDispatch, AVBOITResolveBindings> {
public:
    AVBOITResolvePass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> accumulationTexture,
        std::shared_ptr<PixelBuffer> normalizationTexture,
        std::shared_ptr<PixelBuffer> shadingExtinctionTexture);

    AVBOITResolveBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const AVBOITResolveBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const AVBOITResolveBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_accumulationTexture;
    std::shared_ptr<PixelBuffer> m_normalizationTexture;
    std::shared_ptr<PixelBuffer> m_shadingExtinctionTexture;
    PipelineState m_pso;
};
