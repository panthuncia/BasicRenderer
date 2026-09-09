#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/PixelBuffer.h"
#include "ThirdParty/XeGTAO.h"
#include "Render/Runtime/DescriptorServiceAccess.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class GTAOMainPass : public org::TypedRenderGraphPass<GTAOMainPass, br::render::PreparedComputeDispatch> {
public:
    GTAOMainPass() {
        CreatePointClampSampler();
        CreateXeGTAOComputePSO();
    }

    void Declare(org::PassBuilder& builder) {
        builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        builder.WithShaderResource(Builtin::Surface::NormalRoughness, Builtin::GTAO::WorkingDepths, Builtin::CameraBuffer)
            .WithUnorderedAccess(Builtin::GTAO::WorkingEdges, Builtin::GTAO::WorkingAOTerm1)
            .WithConstantBuffer("Builtin::GTAO::ConstantsBuffer");
		builder.WithConstantBuffer(Builtin::PerFrameBuffer);
    }

    void Initialize() {
        // Removed redundant Register calls now covered by declared-resource auto descriptor registration
    }



    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
        const auto* context = preparation.preparationData->Get<UpdateContext>();
        const auto workingDepths = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GTAO::WorkingDepths);
        const auto workingAO = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GTAO::WorkingAOTerm1);
        const auto workingEdges = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GTAO::WorkingEdges);
        const auto normals = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::NormalRoughness);
        auto payload = GTAOHighPSO.GetPayload(); br::render::PreparedComputeDispatch data{};
        data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
        data.layout = PSOManager::GetInstance().GetRootSignature().GetHandle(); auto program = preparation.CaptureProgramBinding(std::move(payload));
        data.program = program.program;
        data.descriptorIndices = std::move(program.descriptorIndices);

        data.constants[UintRootConstant0] = (++frameIndex) % 64;
        data.constants[UintRootConstant1] = m_samplerIndex;
        data.constants[UintRootConstant2] = workingDepths->GetSRVInfo(0).slot.index;
        data.constants[UintRootConstant3] = normals->GetSRVInfo(0).slot.index;
        data.constants[UintRootConstant4] = workingAO->GetUAVShaderVisibleInfo(0).slot.index;
        data.constants[UintRootConstant5] = workingEdges->GetUAVShaderVisibleInfo(0).slot.index;
        data.groupsX = (context->renderResolution.x + XE_GTAO_NUMTHREADS_X - 1u) / XE_GTAO_NUMTHREADS_X;
        data.groupsY = (context->renderResolution.y + XE_GTAO_NUMTHREADS_Y - 1u) / XE_GTAO_NUMTHREADS_Y;
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatch(data, recording);
    }

    void ShutdownPass() {
        // Cleanup if necessary
    }

private:

    PipelineState PrefilterDepths16x16PSO;
    PipelineState GTAOLowPSO;
    PipelineState GTAOMediumPSO;
    PipelineState GTAOHighPSO;
    PipelineState GTAOUltraPSO;
    PipelineState DenoisePassPSO;
    PipelineState DenoiseLastPassPSO;
    PipelineState GenerateNormalsPSO;

    uint64_t frameIndex = 0;
    uint32_t m_samplerIndex = 0;

    void CreatePointClampSampler()
    {
        rhi::SamplerDesc samplerDesc;
        samplerDesc.minFilter = rhi::Filter::Nearest;
        samplerDesc.magFilter = rhi::Filter::Nearest;
        samplerDesc.mipFilter = rhi::MipFilter::Nearest;
        samplerDesc.addressU = rhi::AddressMode::Clamp;
        samplerDesc.addressV = rhi::AddressMode::Clamp;
        samplerDesc.addressW = rhi::AddressMode::Clamp;
        samplerDesc.mipLodBias = 0.0f;
        samplerDesc.maxAnisotropy = 1;
        samplerDesc.compareEnable = false;
        samplerDesc.borderPreset = rhi::BorderPreset::TransparentBlack;
        samplerDesc.minLod = 0.0f;
        samplerDesc.maxLod = 0.0f;
        m_samplerIndex = org::runtime::CreateIndexedSamplerFromActiveDescriptorService(samplerDesc);
    }

    void CreateXeGTAOComputePSO() {

		GTAOUltraPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature().GetHandle(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOUltra",
			{},
			"GTAO Ultra Quality");

		GTAOHighPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature().GetHandle(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOHigh",
			{},
			"GTAO High Quality");

		GTAOMediumPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature().GetHandle(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOMedium",
			{},
			"GTAO Medium Quality");

		GTAOLowPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature().GetHandle(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOLow",
			{},
			"GTAO Low Quality");

    }
};
