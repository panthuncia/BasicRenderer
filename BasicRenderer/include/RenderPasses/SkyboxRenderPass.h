#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class SkyboxRenderPass : public org::TypedRenderGraphPass<SkyboxRenderPass, br::render::PreparedComputeDispatch> {
public:
    SkyboxRenderPass() {
        CreatePSO();
    }

    void Declare(org::PassBuilder& declaration) {
        declaration.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        auto* builder = &declaration;
        builder->WithShaderResource(Builtin::Environment::CurrentCubemap, Builtin::Environment::InfoBuffer)
            .WithShaderResource(Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }), Builtin::CameraBuffer)
			.WithUnorderedAccess(Builtin::Color::HDRColorTarget, Builtin::Surface::Motion);
		builder->WithConstantBuffer(Builtin::PerFrameBuffer);
    }

    void Initialize() {
        m_pHDRTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Color::HDRColorTarget);
    }

    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {

        const auto& context = *preparation.preparationData->Get<UpdateContext>();
        br::render::PreparedComputeDispatch data{};
        data.resourceHeap = context.textureDescriptorHeap.GetHandle();
        data.samplerHeap = context.samplerDescriptorHeap.GetHandle();
        auto program = preparation.CaptureProgramBinding(m_pso);
        data.program = program.program;
        data.descriptorIndices = std::move(program.descriptorIndices);
        data.constants[0] = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::PrimaryCamera::LinearDepthMap)->GetSRVInfo(0).slot.index;
        data.constants[1] = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).slot.index;
        data.constants[2] = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Environment::InfoBuffer)->GetSRVInfo(0).slot.index;
        data.constants[3] = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Color::HDRColorTarget)->GetUAVShaderVisibleInfo(0).slot.index;
		data.constants[4] = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::Motion)->GetUAVShaderVisibleInfo(0).slot.index;
        data.groupsX = (m_pHDRTarget->GetWidth() + 7u) / 8u;
        data.groupsY = (m_pHDRTarget->GetHeight() + 7u) / 8u;
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatch(data, recording);
    }

private:
    PipelineState m_pso;

    PixelBuffer* m_pHDRTarget = nullptr;
    void CreatePSO() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/skybox.hlsl",
            L"SkyboxCSMain",
            {},
            "SkyboxComputePSO"
        );
    }
};
