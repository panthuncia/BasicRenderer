#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct VirtualShadowMapResolveMarkedBlocksBindings {
    org::ResourceBindingToken mask, list, count, requests, requestCount, clipmapData;
    org::ResourceBindingToken pageTable, dirtyFlags, pageViewInfo, stats;
    uint32_t activeClipmapCount = 0;
};

class VirtualShadowMapResolveMarkedBlocksPass final : public org::TypedRenderGraphPass<VirtualShadowMapResolveMarkedBlocksPass,
    br::render::PreparedComputeDispatch, VirtualShadowMapResolveMarkedBlocksBindings> {
public:
    VirtualShadowMapResolveMarkedBlocksPass(
        std::shared_ptr<Buffer> markedBlocksMaskBuffer,
        std::shared_ptr<Buffer> markedBlocksListBuffer,
        std::shared_ptr<Buffer> markedBlocksCountBuffer,
        std::shared_ptr<Buffer> allocationRequestsBuffer,
        std::shared_ptr<Buffer> allocationCountBuffer,
        std::shared_ptr<Buffer> markClipmapDataBuffer,
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
        std::shared_ptr<Buffer> directionalPageViewInfoBuffer,
        std::shared_ptr<Buffer> statsBuffer);

    VirtualShadowMapResolveMarkedBlocksBindings Declare(org::PassBuilder& builder);
    void Initialize();
    void Update(const UpdateExecutionContext& executionContext) override;
    br::render::PreparedComputeDispatch Prepare(const VirtualShadowMapResolveMarkedBlocksBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const VirtualShadowMapResolveMarkedBlocksBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void ShutdownPass();

private:
    PipelineState m_pso;
    std::shared_ptr<Buffer> m_markedBlocksMaskBuffer;
    std::shared_ptr<Buffer> m_markedBlocksListBuffer;
    std::shared_ptr<Buffer> m_markedBlocksCountBuffer;
    std::shared_ptr<Buffer> m_allocationRequestsBuffer;
    std::shared_ptr<Buffer> m_allocationCountBuffer;
    std::shared_ptr<Buffer> m_markClipmapDataBuffer;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_dirtyPageFlagsBuffer;
    std::shared_ptr<Buffer> m_directionalPageViewInfoBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
    uint32_t m_activeClipmapCount = 0u;
};
