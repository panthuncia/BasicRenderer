#pragma once
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Render/MaterialStateArtifacts.h"
#include "RenderPasses/PreparedComputeDispatch.h"

// Pass B: scan block sums, add block prefixes to per-element offsets, and write total pixel count.
// Dispatch dimension: x = 1 (single group), unless we implement recursive scan for very large numBlocks.
class MaterialBlockOffsetsPass : public org::TypedRenderGraphPass<MaterialBlockOffsetsPass, br::render::PreparedComputeDispatch> {
public:
    MaterialBlockOffsetsPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/materialPrefixSum.hlsl",
            L"BlockOffsetsCS",
            {},
            "VisUtil_BlockOffsetsPSO");
    }

    void Declare(org::PassBuilder& b) {
        b.WithShaderResource("Builtin::VisUtil::MaterialPixelCountBuffer",
                              "Builtin::VisUtil::BlockSumsBuffer")
         .WithUnorderedAccess("Builtin::VisUtil::MaterialOffsetBuffer",
                              "Builtin::VisUtil::ScannedBlockSumsBuffer",
                              "Builtin::VisUtil::TotalPixelCountBuffer")
         .PreferQueue(org::QueueKind::Compute);
    }

    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
        const auto* update = preparation.preparationData->Get<UpdateContext>();
        const auto* render = preparation.preparationData->Get<RenderContext>();
        if (!update && !render) throw std::logic_error("MaterialBlockOffsetsPass requires frame context");
        auto payload = m_pso.GetPayload();
        br::render::PreparedComputeDispatch data{};
        data.resourceHeap = update ? update->textureDescriptorHeap.GetHandle() : render->textureDescriptorHeap.GetHandle();
        data.samplerHeap = update ? update->samplerDescriptorHeap.GetHandle() : render->samplerDescriptorHeap.GetHandle();
        data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
        data.pipeline = payload->pso.Get().GetHandle();
        data.pipelineOwner = std::move(payload);
        data.descriptorIndices = CaptureResourceDescriptorIndices(data.pipelineOwner->pipelineResources);
        const auto& published = update ? update->publishedRendererState : render->publishedRendererState;
        const auto materialState = published
            ? published->materials.payload.Get<br::render::PublishedMaterialState>()
            : nullptr;
        data.constants[0] = materialState ? materialState->compileFlagSlotsUsed : 0u;
        data.constants[1] = (data.constants[0] + m_blockSize - 1u) / m_blockSize;
        data.groupsX = 1;
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatch(data, recording);
    }

private:
    PipelineState m_pso;
    uint32_t m_blockSize = 1024;
};
