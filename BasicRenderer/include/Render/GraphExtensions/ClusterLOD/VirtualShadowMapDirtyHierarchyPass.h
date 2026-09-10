#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct VirtualShadowMapDirtyHierarchyBindings {
    org::ResourceBindingToken pageTable;
    org::ResourceBindingToken hierarchy;
    org::ResourceBindingToken clipmapInfo;
};

class VirtualShadowMapDirtyHierarchyPass final : public org::TypedRenderGraphPass<VirtualShadowMapDirtyHierarchyPass,
    br::render::PreparedComputeDispatchSequence, VirtualShadowMapDirtyHierarchyBindings> {
public:
    VirtualShadowMapDirtyHierarchyPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<PixelBuffer> dirtyHierarchyTexture,
        std::shared_ptr<Buffer> clipmapInfoBuffer);

    VirtualShadowMapDirtyHierarchyBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatchSequence Prepare(const VirtualShadowMapDirtyHierarchyBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const VirtualShadowMapDirtyHierarchyBindings&,
        const br::render::PreparedComputeDispatchSequence&, org::PassRecordContext&);

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<PixelBuffer> m_dirtyHierarchyTexture;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
};
