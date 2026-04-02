#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearPagesPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowClearRootConstants.h"

VirtualShadowMapClearPagesPass::VirtualShadowMapClearPagesPass(
    std::shared_ptr<PixelBuffer> physicalPagesTexture,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer)
    : m_physicalPagesTexture(std::move(physicalPagesTexture))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowClearPhysicalPagesCSMain",
        {},
        "CLod.VirtualShadow.ClearPhysicalPages.PSO");
}

void VirtualShadowMapClearPagesPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithUnorderedAccess(m_physicalPagesTexture, m_dirtyPageFlagsBuffer);
}

void VirtualShadowMapClearPagesPass::Setup()
{
}

PassReturn VirtualShadowMapClearPagesPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_PHYSICAL_PAGES_DESCRIPTOR_INDEX] = m_physicalPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    commandList.Dispatch(CLodVirtualShadowDefaultPhysicalPageCount, 1u, 1u);
    return {};
}

void VirtualShadowMapClearPagesPass::Cleanup()
{
}