#include "Render/GraphExtensions/ClusterLOD/PerViewLinearDepthCopyPass.h"

#include "Managers/ViewManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/PixelBuffer.h"
#include "Utilities/Utilities.h"

PerViewLinearDepthCopyPass::PerViewLinearDepthCopyPass(bool writeProjectedDepth)
    : m_writeProjectedDepth(writeProjectedDepth) {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/canonicalSurface.hlsl",
        L"PerViewPrimaryDepthCopyCS",
        {},
        "PerViewPrimaryDepthCopyPSO");
}

void PerViewLinearDepthCopyPass::Declare(org::PassBuilder& builder) {
    builder.WithShaderResource(Builtin::PrimaryCamera::VisibilityTexture)
        .WithUnorderedAccess(Builtin::PrimaryCamera::LinearDepthMap,
            Builtin::PrimaryCamera::ProjectedDepthTexture, Builtin::Surface::DeviceDepth);
    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}

void PerViewLinearDepthCopyPass::Initialize() {
    m_pProjectedDepthTexture = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::ProjectedDepthTexture);
    m_pCanonicalDeviceDepth = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Surface::DeviceDepth);
}


PerViewLinearDepthCopyPreparedData PerViewLinearDepthCopyPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    PreparedData data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    data.program = preparation.CaptureProgram(m_pso);
    context->viewManager->ForEachView([&](uint64_t viewID) {
        const auto* view = context->viewManager->Get(viewID);
        if (!view || !view->gpu.visibilityBuffer || !view->gpu.linearDepthMap) return;
        PreparedView item{};
        item.constants.resize(NumMiscUintRootConstants);
        auto& c = item.constants;
        c[UintRootConstant0] = view->gpu.visibilityBuffer->GetSRVInfo(0).slot.index;
        c[UintRootConstant1] = view->gpu.linearDepthMap->GetUAVShaderVisibleInfo(0).slot.index;
        c[UintRootConstant2] = view->gpu.visibilityBuffer->GetWidth();
        c[UintRootConstant3] = view->gpu.visibilityBuffer->GetHeight();
        if (m_writeProjectedDepth && view->flags.primaryCamera && m_pProjectedDepthTexture) {
            c[UintRootConstant4] = m_pProjectedDepthTexture->GetUAVShaderVisibleInfo(0).slot.index;
            c[UintRootConstant7] = m_pCanonicalDeviceDepth
                ? m_pCanonicalDeviceDepth->GetUAVShaderVisibleInfo(0).slot.index : 0xFFFFFFFFu;
            const auto& proj = view->cameraInfo.unjitteredProjection;
            c[UintRootConstant5] = as_uint(DirectX::XMVectorGetZ(proj.r[2]));
            c[UintRootConstant6] = as_uint(DirectX::XMVectorGetZ(proj.r[3]));
        } else c[UintRootConstant4] = c[UintRootConstant7] = 0xFFFFFFFFu;
        item.groupsX = (c[UintRootConstant2] + 7u) / 8u;
        item.groupsY = (c[UintRootConstant3] + 7u) / 8u;
        data.views.push_back(std::move(item));
    });
    return data;
}

void PerViewLinearDepthCopyPass::Record(const PreparedData& data, org::PassRecordContext& recording) {
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    commands.BindPipeline(recording.Resolve(data.program));
    for (const auto& view : data.views) {
        commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
            NumMiscUintRootConstants, view.constants.data());
        commands.Dispatch(view.groupsX, view.groupsY, 1);
    }
}

