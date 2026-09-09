#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapAllocatePagesPass final : public org::TypedRenderGraphPass<VirtualShadowMapAllocatePagesPass, br::render::PreparedComputeIndirect> {
public:
    VirtualShadowMapAllocatePagesPass(
        std::shared_ptr<Buffer> allocationRequestsBuffer,
        std::shared_ptr<Buffer> allocationCountBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<Buffer> clipmapInfoBuffer,
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
        std::shared_ptr<Buffer> freePhysicalPagesBuffer,
        std::shared_ptr<Buffer> reusablePhysicalPagesBuffer,
        std::shared_ptr<Buffer> pageListHeaderBuffer,
        std::shared_ptr<Buffer> statsBuffer);

    void Declare(org::PassBuilder& builder);
    br::render::PreparedComputeIndirect Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeIndirect&, org::PassRecordContext&);

private:
    PipelineState m_pso;
    std::shared_ptr<rhi::CommandSignaturePtr> m_commandSignature;
    std::shared_ptr<Buffer> m_allocationRequestsBuffer;
    std::shared_ptr<Buffer> m_allocationCountBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    ResourceBindingToken m_indirectArgumentsBinding{};
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_dirtyPageFlagsBuffer;
    std::shared_ptr<Buffer> m_freePhysicalPagesBuffer;
    std::shared_ptr<Buffer> m_reusablePhysicalPagesBuffer;
    std::shared_ptr<Buffer> m_pageListHeaderBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
};
