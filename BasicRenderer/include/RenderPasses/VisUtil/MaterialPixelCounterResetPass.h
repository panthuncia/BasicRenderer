#pragma once
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Render/MaterialStateArtifacts.h"
#include "Materials/TechniqueDescriptor.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class MaterialUAVResetPass : public org::TypedRenderGraphPass<MaterialUAVResetPass, br::render::PreparedComputeDispatch> {
public:
    MaterialUAVResetPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/VisUtil.hlsl",
            L"ClearMaterialCountersCS",
            {},
            "ClearMaterialCountersPSO");
    }

    void Declare(org::PassBuilder& builder) {
        builder.WithUnorderedAccess("Builtin::VisUtil::MaterialPixelCountBuffer",
            "Builtin::VisUtil::MaterialWriteCursorBuffer");
		builder.WithConstantBuffer(Builtin::PerFrameBuffer)
            .PreferQueue(org::QueueKind::Compute);
    }

    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
        const auto* update = preparation.preparationData->Get<UpdateContext>();
        const auto* render = preparation.preparationData->Get<RenderContext>();
        if (!update && !render) throw std::logic_error("MaterialUAVResetPass requires frame context");
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
        data.groupsX = (data.constants[0] + 63u) / 64u;
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatch(data, recording);
    }

private:
    PipelineState m_pso;
};
