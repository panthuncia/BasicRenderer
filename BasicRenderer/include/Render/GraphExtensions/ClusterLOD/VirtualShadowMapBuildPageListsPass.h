#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapBuildPageListsPass final : public org::TypedRenderGraphPass<VirtualShadowMapBuildPageListsPass, br::render::PreparedComputeDispatch> {
public:
    VirtualShadowMapBuildPageListsPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> allocationCountBuffer,
        std::shared_ptr<Buffer> freePhysicalPagesBuffer,
        std::shared_ptr<Buffer> reusablePhysicalPagesBuffer,
        std::shared_ptr<Buffer> pageListHeaderBuffer);

    void Declare(org::PassBuilder& builder);
    void Initialize();
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void ShutdownPass();

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_allocationCountBuffer;
    std::shared_ptr<Buffer> m_freePhysicalPagesBuffer;
    std::shared_ptr<Buffer> m_reusablePhysicalPagesBuffer;
    std::shared_ptr<Buffer> m_pageListHeaderBuffer;
};
