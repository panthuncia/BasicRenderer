#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapBuildPageListsPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "ShaderBuffers.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowBuildPageListsRootConstants.h"

VirtualShadowMapBuildPageListsPass::VirtualShadowMapBuildPageListsPass(
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> freePhysicalPagesBuffer,
    std::shared_ptr<Buffer> reusablePhysicalPagesBuffer,
    std::shared_ptr<Buffer> pageListHeaderBuffer)
    : m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_freePhysicalPagesBuffer(std::move(freePhysicalPagesBuffer))
    , m_reusablePhysicalPagesBuffer(std::move(reusablePhysicalPagesBuffer))
    , m_pageListHeaderBuffer(std::move(pageListHeaderBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowBuildPageListsCSMain",
        {},
        "CLod.VirtualShadow.BuildPageLists.PSO");
}

void VirtualShadowMapBuildPageListsPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_pageMetadataBuffer)
        .WithUnorderedAccess(
            m_freePhysicalPagesBuffer,
            m_reusablePhysicalPagesBuffer,
            m_pageListHeaderBuffer);
}

void VirtualShadowMapBuildPageListsPass::Setup() {}

PassReturn VirtualShadowMapBuildPageListsPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_FREE_PAGES_DESCRIPTOR_INDEX] = m_freePhysicalPagesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_REUSABLE_PAGES_DESCRIPTOR_INDEX] = m_reusablePhysicalPagesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_HEADER_DESCRIPTOR_INDEX] = m_pageListHeaderBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PHYSICAL_PAGE_COUNT] = CLodVirtualShadowDefaultPhysicalPageCount;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    const uint32_t groupsX = (CLodVirtualShadowDefaultPhysicalPageCount + 63u) / 64u;
    commandList.Dispatch(groupsX, 1u, 1u);
    return {};
}

void VirtualShadowMapBuildPageListsPass::Cleanup() {}