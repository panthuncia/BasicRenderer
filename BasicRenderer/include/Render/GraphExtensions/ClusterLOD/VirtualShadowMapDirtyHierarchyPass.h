#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapDirtyHierarchyPass final : public org::TypedRenderGraphPass<VirtualShadowMapDirtyHierarchyPass, br::render::PreparedComputeDispatchSequence> {
public:
    VirtualShadowMapDirtyHierarchyPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<PixelBuffer> dirtyHierarchyTexture,
        std::shared_ptr<Buffer> clipmapInfoBuffer);

    void Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatchSequence Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatchSequence&, org::PassRecordContext&);

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<PixelBuffer> m_dirtyHierarchyTexture;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
};
