#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class DilateMotionVectorsPass : public org::TypedRenderGraphPass<DilateMotionVectorsPass, br::render::PreparedComputeDispatch> {
public:
    DilateMotionVectorsPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/PostProcessing/dilateMotionVectors.hlsl",
            L"DilateMotionVectorsCS",
            {},
            "DilateMotionVectorsCS");
    }

    void Declare(org::PassBuilder& builder) {
        builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        builder.WithShaderResource(
            Builtin::Surface::Motion,
            Builtin::PrimaryCamera::ProjectedDepthTexture)
            .WithUnorderedAccess(Builtin::Surface::DilatedMotion);
    }

    void Initialize() {
        m_source = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Surface::Motion);
        m_depth = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::ProjectedDepthTexture);
        m_destination = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Surface::DilatedMotion);
    }



    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
        const auto* context = preparation.preparationData->Get<UpdateContext>();
        auto payload = m_pso.GetPayload(); br::render::PreparedComputeDispatch data{};
        data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
        data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle(); auto program = preparation.CaptureProgramBinding(std::move(payload));
        data.program = program.program;
        data.descriptorIndices = std::move(program.descriptorIndices);

        data.constants[0] = m_source->GetSRVInfo(0).slot.index; data.constants[1] = m_depth->GetSRVInfo(0).slot.index;
        data.constants[2] = m_destination->GetUAVShaderVisibleInfo(0).slot.index;
        data.constants[3] = m_destination->GetWidth(); data.constants[4] = m_destination->GetHeight();
        data.groupsX = (m_destination->GetWidth() + 7u) / 8u; data.groupsY = (m_destination->GetHeight() + 7u) / 8u;
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatch(data, recording);
    }

    void ShutdownPass() {}

private:
    PipelineState m_pso;
    PixelBuffer* m_source = nullptr;
    PixelBuffer* m_depth = nullptr;
    PixelBuffer* m_destination = nullptr;
};
