#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapNonRasterableHierarchyPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowDirtyHierarchyRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

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

PreparedPass VirtualShadowMapNonRasterableHierarchyPass::PrepareFrame(FramePreparationContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    auto payload = m_pso.GetPayload(); br::render::PreparedComputeDispatchSequence data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle(); data.pipeline = payload->pso.Get().GetHandle();
    data.pipelineOwner = std::move(payload); data.descriptorIndices = CaptureResourceDescriptorIndices(data.pipelineOwner->pipelineResources);
    const uint32_t mipCount = m_nonRasterableHierarchyTexture->GetNumUAVMipLevels(); data.steps.reserve(mipCount);
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        const bool pageTable = mip == 0; const uint32_t src = pageTable ? config.pageTableResolution : (std::max)(config.pageTableResolution >> (mip - 1u), 1u);
        const uint32_t dst = pageTable ? src : (src > 1u ? src >> 1u : 1u); br::render::PreparedComputeDispatchSequence::Step step{};
        step.uavBarrierBefore = !pageTable;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_DESCRIPTOR_INDEX] = pageTable ? m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index : m_nonRasterableHierarchyTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, mip - 1u).slot.index;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_DEST_DESCRIPTOR_INDEX] = m_nonRasterableHierarchyTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, mip).slot.index;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_IS_PAGE_TABLE] = pageTable ? 1u : 0u;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION] = src;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
        step.groupsX = (dst + 7u) / 8u; step.groupsY = step.groupsX; step.groupsZ = CLodVirtualShadowMaxSupportedClipmapCount; data.steps.push_back(std::move(step));
    }
    return PreparedPass::MakeOwned(std::move(data), &br::render::RecordPreparedComputeDispatchSequence);
}
