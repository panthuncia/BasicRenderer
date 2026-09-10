#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct VirtualShadowMapClearPagesBindings {
    org::ResourceBindingToken staticPages, dynamicPages, dirtyFlags, pageTable;
    org::ResourceBindingToken pageMetadata, clipmapInfo, pageViewInfo, stats;
    bool dynamicContentFilter = false;
};

class VirtualShadowMapClearPagesPass final : public org::TypedRenderGraphPass<VirtualShadowMapClearPagesPass,
    br::render::PreparedComputeDispatch, VirtualShadowMapClearPagesBindings> {
public:
    VirtualShadowMapClearPagesPass(
        std::shared_ptr<PixelBuffer> staticPagesTexture,
        std::shared_ptr<PixelBuffer> dynamicPagesTexture,
        std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> clipmapInfoBuffer,
        std::shared_ptr<Buffer> pageViewInfoBuffer,
        std::shared_ptr<Buffer> statsBuffer);

    VirtualShadowMapClearPagesBindings Declare(org::PassBuilder& builder);
    void Initialize();
    br::render::PreparedComputeDispatch Prepare(const VirtualShadowMapClearPagesBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const VirtualShadowMapClearPagesBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void ShutdownPass();

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_staticPagesTexture;
    std::shared_ptr<PixelBuffer> m_dynamicPagesTexture;
    std::shared_ptr<Buffer> m_dirtyPageFlagsBuffer;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
    std::shared_ptr<Buffer> m_pageViewInfoBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
};
