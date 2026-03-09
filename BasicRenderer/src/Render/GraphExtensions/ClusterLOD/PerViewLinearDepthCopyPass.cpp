#include "Render/GraphExtensions/ClusterLOD/PerViewLinearDepthCopyPass.h"

#include "Managers/ViewManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"

PerViewLinearDepthCopyPass::PerViewLinearDepthCopyPass() {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/gbuffer.hlsl",
        L"PerViewPrimaryDepthCopyCS",
        {},
        "PerViewPrimaryDepthCopyPSO");
}

void PerViewLinearDepthCopyPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(Builtin::PrimaryCamera::VisibilityTexture)
        .WithUnorderedAccess(Builtin::PrimaryCamera::LinearDepthMap, Builtin::Shadows::LinearShadowMaps);
}

void PerViewLinearDepthCopyPass::Setup() {}

PassReturn PerViewLinearDepthCopyPass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};

    context.viewManager->ForEachView([&](uint64_t viewID) {
        const auto* view = context.viewManager->Get(viewID);
        if (!view || !view->gpu.visibilityBuffer || !view->gpu.linearDepthMap) {
            return;
        }

        rootConstants[UintRootConstant0] = view->gpu.visibilityBuffer->GetSRVInfo(0).slot.index;
        rootConstants[UintRootConstant1] = view->gpu.linearDepthMap->GetUAVShaderVisibleInfo(0).slot.index;
        rootConstants[UintRootConstant2] = view->gpu.visibilityBuffer->GetWidth();
        rootConstants[UintRootConstant3] = view->gpu.visibilityBuffer->GetHeight();

        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            rootConstants);

        const uint32_t groupsX = (rootConstants[UintRootConstant2] + 7u) / 8u;
        const uint32_t groupsY = (rootConstants[UintRootConstant3] + 7u) / 8u;
        commandList.Dispatch(groupsX, groupsY, 1);
    });

    return {};
}

void PerViewLinearDepthCopyPass::Cleanup() {}
