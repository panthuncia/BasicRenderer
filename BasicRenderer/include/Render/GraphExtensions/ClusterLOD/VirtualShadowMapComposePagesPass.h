#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct VirtualShadowMapComposePagesBindings {
    org::ResourceBindingToken staticPages, dynamicPages, pageTable, pageMetadata, stats;
};

class VirtualShadowMapComposePagesPass final : public org::TypedRenderGraphPass<VirtualShadowMapComposePagesPass,
    br::render::PreparedComputeDispatch, VirtualShadowMapComposePagesBindings> {
public:
    VirtualShadowMapComposePagesPass(
        std::shared_ptr<PixelBuffer> staticPagesTexture,
        std::shared_ptr<PixelBuffer> dynamicPagesTexture,
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> statsBuffer);

    VirtualShadowMapComposePagesBindings Declare(org::PassBuilder& builder);
    void Initialize() {}
    br::render::PreparedComputeDispatch Prepare(const VirtualShadowMapComposePagesBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const VirtualShadowMapComposePagesBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void ShutdownPass() {}

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_staticPagesTexture;
    std::shared_ptr<PixelBuffer> m_dynamicPagesTexture;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
};
