#pragma once

#include <memory>
#include <vector>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapDeduplicatePredictedPagesPass final : public org::TypedRenderGraphPass<VirtualShadowMapDeduplicatePredictedPagesPass, br::render::PreparedComputePipelineSequence> {
public:
    VirtualShadowMapDeduplicatePredictedPagesPass(
        std::shared_ptr<Buffer> predictiveRawPagesBuffer,
        std::shared_ptr<Buffer> predictiveRawPageCountBuffer,
        std::shared_ptr<Buffer> predictedScratchBitsetBuffer,
        std::shared_ptr<Buffer> predictedPagesBuffer,
        std::shared_ptr<Buffer> predictedPageCountBuffer,
        std::shared_ptr<Buffer> statsBuffer,
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> dirtyFlagsBuffer,
        uint32_t physicalPageCount);

    void Declare(org::PassBuilder& builder);
    br::render::PreparedComputePipelineSequence Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputePipelineSequence&, org::PassRecordContext&);

private:

    PipelineState m_clearStatePso;
    PipelineState m_deduplicatePso;
    std::shared_ptr<Buffer> m_predictiveRawPagesBuffer;
    std::shared_ptr<Buffer> m_predictiveRawPageCountBuffer;
    std::shared_ptr<Buffer> m_predictedScratchBitsetBuffer;
    std::shared_ptr<Buffer> m_predictedPagesBuffer;
    std::shared_ptr<Buffer> m_predictedPageCountBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_dirtyFlagsBuffer;
    uint32_t m_physicalPageCount = 0u;
};
