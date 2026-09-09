#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapBuildActiveBlocksPass final : public org::TypedRenderGraphPass<VirtualShadowMapBuildActiveBlocksPass, br::render::PreparedComputeDispatch> {
public:
    VirtualShadowMapBuildActiveBlocksPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> clipmapInfoBuffer,
        std::shared_ptr<Buffer> activeBlockMetadataBuffer,
        bool dynamicPages = false);

    void Declare(org::PassBuilder& builder);
    void Initialize() {}
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void ShutdownPass() {}

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
    std::shared_ptr<Buffer> m_activeBlockMetadataBuffer;
    bool m_dynamicPages = false;
};
