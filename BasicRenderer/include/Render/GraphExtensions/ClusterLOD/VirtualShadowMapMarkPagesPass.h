#pragma once

#include <memory>
#include <array>
#include <vector>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "ShaderBuffers.h"
#include "RenderPasses/Base/ComputePass.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapMarkPagesPass final : public ComputePass {
public:
    VirtualShadowMapMarkPagesPass(
        std::shared_ptr<Buffer> tileWorkBuffer,
        std::shared_ptr<Buffer> tileCountBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<Buffer> markClipmapDataBuffer,
        std::shared_ptr<Buffer> markedBlocksMaskBuffer,
        std::shared_ptr<Buffer> markedBlocksListBuffer,
        std::shared_ptr<Buffer> markedBlocksCountBuffer,
        std::shared_ptr<Buffer> receiverSubpageMaskBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    PreparedPass PrepareFrame(FramePreparationContext& preparation) override;
    void Cleanup() override;

private:
    struct PreparedData {
        rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
        rhi::PipelineLayoutHandle layout{};
        rhi::PipelineHandle clearPipeline{}, clearUint2Pipeline{}, markPipeline{};
        std::shared_ptr<const PipelineStatePayload> clearOwner, clearUint2Owner, markOwner;
        std::shared_ptr<const rhi::CommandSignaturePtr> commandSignatureOwner;
        rhi::CommandSignatureHandle commandSignature{};
        rhi::ResourceHandle indirectArguments{};
        std::vector<unsigned int> clearIndices, clearUint2Indices, markIndices;
        std::array<unsigned int, NumMiscUintRootConstants> clearMask{}, clearReceiver{}, clearCount{}, mark{};
        std::array<rhi::ResourceHandle, 3> barrierResources{};
        uint32_t barrierCount = 2;
        uint32_t receiverGroups = 0;
        bool receiverUint2 = false;
    };
    static void RecordPrepared(const PreparedData&, RecordingContext&);
    PipelineState m_pso;
    PipelineState m_clearPso;
    PipelineState m_clearUint2Pso;
    std::shared_ptr<rhi::CommandSignaturePtr> m_commandSignature;
    std::shared_ptr<Buffer> m_tileWorkBuffer;
    std::shared_ptr<Buffer> m_tileCountBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    std::shared_ptr<Buffer> m_markClipmapDataBuffer;
    std::shared_ptr<Buffer> m_markedBlocksMaskBuffer;
    std::shared_ptr<Buffer> m_markedBlocksListBuffer;
    std::shared_ptr<Buffer> m_markedBlocksCountBuffer;
    std::shared_ptr<Buffer> m_receiverSubpageMaskBuffer;
    uint32_t m_activeClipmapCount = 0u;
    uint32_t m_receiverSubpageMode = 0u;
};
