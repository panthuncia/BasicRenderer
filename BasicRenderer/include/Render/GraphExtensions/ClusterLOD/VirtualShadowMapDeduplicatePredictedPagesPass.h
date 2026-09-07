#pragma once

#include <memory>
#include <vector>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapDeduplicatePredictedPagesPass final : public ComputePass {
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

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    PreparedPass PrepareFrame(FramePreparationContext& preparation) override;
    void Cleanup() override;

private:
    struct PreparedData {
        rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
        rhi::PipelineLayoutHandle layout{};
        rhi::PipelineHandle clearPipeline{}, deduplicatePipeline{};
        std::shared_ptr<const PipelineStatePayload> clearOwner, deduplicateOwner;
        std::vector<unsigned int> clearDescriptorIndices, deduplicateDescriptorIndices;
        std::vector<unsigned int> constants;
        uint32_t clearGroups = 0, deduplicateGroups = 0;
    };
    static void RecordPrepared(const PreparedData&, RecordingContext&);
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
