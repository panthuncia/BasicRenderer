#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapNonRasterableHierarchyPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowDirtyHierarchyRootConstants.h"

VirtualShadowMapNonRasterableHierarchyPass::VirtualShadowMapNonRasterableHierarchyPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<PixelBuffer> nonRasterableHierarchyTexture,
    std::shared_ptr<Buffer> clipmapInfoBuffer)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_nonRasterableHierarchyTexture(std::move(nonRasterableHierarchyTexture))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowBuildNonRasterableHierarchyCSMain",
        {},
        "CLod.VirtualShadow.NonRasterableHierarchy.PSO");
}

void VirtualShadowMapNonRasterableHierarchyPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_pageTableTexture, Subresources(m_pageTableTexture, Mip{0, 1}))
        .WithShaderResource(m_clipmapInfoBuffer)
        .WithUnorderedAccess(Subresources(m_nonRasterableHierarchyTexture, FromMip{0}));

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void VirtualShadowMapNonRasterableHierarchyPass::Setup()
{
}

PassReturn VirtualShadowMapNonRasterableHierarchyPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());
    const CLodVirtualShadowResolutionConfig virtualShadowConfig = CLodVirtualShadowBuildRuntimeResolutionConfig();

    const uint32_t mipCount = m_nonRasterableHierarchyTexture->GetNumUAVMipLevels();
    for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex) {
        const bool sourceIsPageTable = (mipIndex == 0u);
        const uint32_t srcResolution = sourceIsPageTable
            ? virtualShadowConfig.pageTableResolution
            : (std::max)(virtualShadowConfig.pageTableResolution >> (mipIndex - 1u), 1u);
        const uint32_t dstResolution = sourceIsPageTable
            ? srcResolution
            : ((srcResolution > 1u) ? (srcResolution >> 1u) : 1u);

        if (!sourceIsPageTable) {
            rhi::GlobalBarrier gb{};
            gb.beforeSync = rhi::ResourceSyncState::ComputeShading;
            gb.afterSync = rhi::ResourceSyncState::ComputeShading;
            gb.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            gb.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            rhi::BarrierBatch batch{};
            batch.globals = rhi::Span<rhi::GlobalBarrier>(&gb, 1);
            commandList.Barriers(batch);
        }

        uint32_t rootConstants[NumMiscUintRootConstants] = {};
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_DESCRIPTOR_INDEX] =
            sourceIsPageTable
            ? m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index
            : m_nonRasterableHierarchyTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, mipIndex - 1u).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_DEST_DESCRIPTOR_INDEX] = m_nonRasterableHierarchyTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, mipIndex).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_IS_PAGE_TABLE] = sourceIsPageTable ? 1u : 0u;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION] = srcResolution;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
        rootConstants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;

        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            rootConstants);

        const uint32_t groupsX = (dstResolution + 7u) / 8u;
        const uint32_t groupsY = (dstResolution + 7u) / 8u;
        commandList.Dispatch(groupsX, groupsY, CLodVirtualShadowMaxSupportedClipmapCount);
    }

    return {};
}

void VirtualShadowMapNonRasterableHierarchyPass::Cleanup()
{
}