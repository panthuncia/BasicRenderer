#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapDirtyHierarchyPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowDirtyHierarchyRootConstants.h"

VirtualShadowMapDirtyHierarchyPass::VirtualShadowMapDirtyHierarchyPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<PixelBuffer> dirtyHierarchyTexture)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_dirtyHierarchyTexture(std::move(dirtyHierarchyTexture))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowBuildDirtyHierarchyCSMain",
        {},
        "CLod.VirtualShadow.DirtyHierarchy.PSO");
}

void VirtualShadowMapDirtyHierarchyPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_pageTableTexture, m_dirtyHierarchyTexture)
        .WithUnorderedAccess(m_dirtyHierarchyTexture);
}

void VirtualShadowMapDirtyHierarchyPass::Setup()
{
}

PassReturn VirtualShadowMapDirtyHierarchyPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    const uint32_t mipCount = m_dirtyHierarchyTexture->GetNumUAVMipLevels();
    for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex) {
        const bool sourceIsPageTable = (mipIndex == 0u);
        const uint32_t srcResolution = sourceIsPageTable
            ? CLodVirtualShadowDefaultPageTableResolution
            : (CLodVirtualShadowDefaultPageTableResolution >> (mipIndex - 1u));
        const uint32_t dstResolution = sourceIsPageTable
            ? srcResolution
            : ((srcResolution > 1u) ? (srcResolution >> 1u) : 1u);

        uint32_t rootConstants[NumMiscUintRootConstants] = {};
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_DESCRIPTOR_INDEX] =
            sourceIsPageTable
            ? m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index
            : m_dirtyHierarchyTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, mipIndex - 1u).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_DEST_DESCRIPTOR_INDEX] = m_dirtyHierarchyTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, mipIndex).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_IS_PAGE_TABLE] = sourceIsPageTable ? 1u : 0u;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION] = srcResolution;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_COUNT] = CLodVirtualShadowDefaultClipmapCount;

        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            rootConstants);

        const uint32_t groupsX = (dstResolution + 7u) / 8u;
        const uint32_t groupsY = (dstResolution + 7u) / 8u;
        commandList.Dispatch(groupsX, groupsY, CLodVirtualShadowDefaultClipmapCount);
    }

    return {};
}

void VirtualShadowMapDirtyHierarchyPass::Cleanup()
{
}