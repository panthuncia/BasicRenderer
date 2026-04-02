#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapGatherStatsPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowGatherStatsRootConstants.h"

VirtualShadowMapGatherStatsPass::VirtualShadowMapGatherStatsPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> allocationIndirectArgsBuffer,
    std::shared_ptr<Buffer> pageListHeaderBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> statsBuffer,
    bool capturePreAllocateState)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_allocationCountBuffer(std::move(allocationCountBuffer))
    , m_allocationIndirectArgsBuffer(std::move(allocationIndirectArgsBuffer))
    , m_pageListHeaderBuffer(std::move(pageListHeaderBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_statsBuffer(std::move(statsBuffer))
    , m_capturePreAllocateState(capturePreAllocateState)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowGatherStatsCSMain",
        {},
        "CLod.VirtualShadow.GatherStats.PSO");
}

void VirtualShadowMapGatherStatsPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            m_pageTableTexture,
            m_allocationCountBuffer,
            m_allocationIndirectArgsBuffer,
            m_pageListHeaderBuffer,
            m_clipmapInfoBuffer)
        .WithUnorderedAccess(m_statsBuffer);
}

void VirtualShadowMapGatherStatsPass::Setup() {}

PassReturn VirtualShadowMapGatherStatsPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_ALLOCATION_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_ALLOCATION_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_allocationIndirectArgsBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_PAGE_LIST_HEADER_DESCRIPTOR_INDEX] = m_pageListHeaderBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_PAGE_TABLE_RESOLUTION] = CLodVirtualShadowDefaultPageTableResolution;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_CLIPMAP_COUNT] = CLodVirtualShadowDefaultClipmapCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_GATHER_STATS_CAPTURE_PRE_ALLOCATE_STATE] = m_capturePreAllocateState ? 1u : 0u;

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

void VirtualShadowMapGatherStatsPass::Cleanup() {}