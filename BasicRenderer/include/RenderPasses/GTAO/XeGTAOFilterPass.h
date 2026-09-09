#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "ShaderBuffers.h"
#include "ThirdParty/XeGTAO.h"
#include "Resources/PixelBuffer.h"
#include <Resources/Buffers/Buffer.h>
#include "Render/Runtime/DescriptorServiceAccess.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class GTAOFilterPass : public org::TypedRenderGraphPass<GTAOFilterPass, br::render::PreparedComputeDispatch> {
public:
    GTAOFilterPass() {
        CreatePointClampSampler();
        CreateXeGTAOComputePSO();
    }

    void Initialize() {
        m_gtaoConstantsHandle = m_resourceRegistryView->RequestHandle("Builtin::GTAO::ConstantsBuffer");
    }

    void Update(const UpdateExecutionContext& updateExecutionContext) override {
        const auto* updateContext = updateExecutionContext.hostData->Get<UpdateContext>();
        if (updateContext == nullptr || !updateContext->hasPrimaryCamera) {
            return;
        }

        GTAOInfo gtaoInfo{};
        XeGTAO::GTAOSettings gtaoSettings;
        XeGTAO::GTAOUpdateConstants(
            gtaoInfo.g_GTAOConstants,
            updateContext->renderResolution.x,
            updateContext->renderResolution.y,
            gtaoSettings,
            false,
            static_cast<unsigned int>(updateContext->frameNumber),
            updateContext->primaryCamera);

        BUFFER_UPLOAD(
            &gtaoInfo,
            sizeof(GTAOInfo),
            org::runtime::UploadTarget::FromHandle(m_gtaoConstantsHandle),
            0);
    }

    void Declare(org::PassBuilder& builder) {
        builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        builder.WithShaderResource(Builtin::Surface::NormalRoughness)
            .WithShaderResource(Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }))
            .WithUnorderedAccess(Builtin::GTAO::WorkingDepths)
            .WithConstantBuffer("Builtin::GTAO::ConstantsBuffer");
		builder.WithConstantBuffer(Builtin::PerFrameBuffer);
    }



    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
        const auto* context = preparation.preparationData->Get<UpdateContext>();
        const auto depth = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::PrimaryCamera::LinearDepthMap);
        const auto workingDepths = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GTAO::WorkingDepths);
        auto payload = PrefilterDepths16x16PSO.GetPayload(); br::render::PreparedComputeDispatch data{};
        data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
        data.layout = PSOManager::GetInstance().GetRootSignature().GetHandle(); auto program = preparation.CaptureProgramBinding(std::move(payload));
        data.program = program.program;
        data.descriptorIndices = std::move(program.descriptorIndices);

        data.constants[UintRootConstant0] = m_samplerIndex;
        data.constants[UintRootConstant1] = depth->GetSRVInfo(0).slot.index;
        data.constants[UintRootConstant2] = workingDepths->GetUAVShaderVisibleInfo(0).slot.index;
        data.constants[UintRootConstant3] = workingDepths->GetUAVShaderVisibleInfo(1).slot.index;
        data.constants[UintRootConstant4] = workingDepths->GetUAVShaderVisibleInfo(2).slot.index;
        data.constants[UintRootConstant5] = workingDepths->GetUAVShaderVisibleInfo(3).slot.index;
        data.constants[UintRootConstant6] = workingDepths->GetUAVShaderVisibleInfo(4).slot.index;
        data.groupsX = (context->renderResolution.x + 15u) / 16u; data.groupsY = (context->renderResolution.y + 15u) / 16u;
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
    uint32_t m_samplerIndex = 0;
    ResourceRegistry::RegistryHandle m_gtaoConstantsHandle;

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

    void CreateXeGTAOComputePSO()
    {
        auto& psoManager = PSOManager::GetInstance();
        PrefilterDepths16x16PSO = psoManager.MakeComputePipeline(
            psoManager.GetRootSignature().GetHandle(),
            L"shaders/GTAO.hlsl",
            L"CSPrefilterDepths16x16"
		);
    }
};
