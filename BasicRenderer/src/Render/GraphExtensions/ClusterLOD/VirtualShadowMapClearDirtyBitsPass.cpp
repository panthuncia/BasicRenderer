#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearDirtyBitsPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowClearDirtyBitsRootConstants.h"

VirtualShadowMapClearDirtyBitsPass::VirtualShadowMapClearDirtyBitsPass(std::shared_ptr<PixelBuffer> pageTableTexture)
    : m_pageTableTexture(std::move(pageTableTexture))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowClearDirtyBitsCSMain",
        {},
        "CLod.VirtualShadow.ClearDirtyBits.PSO");
}

void VirtualShadowMapClearDirtyBitsPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithUnorderedAccess(m_pageTableTexture);
}

void VirtualShadowMapClearDirtyBitsPass::Setup()
{
}

PassReturn VirtualShadowMapClearDirtyBitsPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_RESOLUTION] = CLodVirtualShadowDefaultPageTableResolution;
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_CLIPMAP_COUNT] = CLodVirtualShadowDefaultClipmapCount;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    const uint32_t groupsX = (CLodVirtualShadowDefaultPageTableResolution + 7u) / 8u;
    const uint32_t groupsY = (CLodVirtualShadowDefaultPageTableResolution + 7u) / 8u;
    commandList.Dispatch(groupsX, groupsY, CLodVirtualShadowDefaultClipmapCount);
    return {};
}

void VirtualShadowMapClearDirtyBitsPass::Cleanup()
{
}