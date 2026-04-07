#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearDirtyBitsPass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowClearDirtyBitsRootConstants.h"

VirtualShadowMapClearDirtyBitsPass::VirtualShadowMapClearDirtyBitsPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> allocationRequestsBuffer,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_allocationRequestsBuffer(std::move(allocationRequestsBuffer))
    , m_allocationCountBuffer(std::move(allocationCountBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowClearDirtyBitsCSMain",
        {},
        "CLod.VirtualShadow.ClearDirtyBits.PSO");

    rhi::IndirectArg dispatchArgs[] = {
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(dispatchArgs, 1), sizeof(CLodReyesDispatchIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_commandSignature);
}

void VirtualShadowMapClearDirtyBitsPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_allocationRequestsBuffer, m_allocationCountBuffer)
        .WithIndirectArguments(m_indirectArgsBuffer)
        .WithUnorderedAccess(m_pageTableTexture);

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
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
    const CLodVirtualShadowResolutionConfig virtualShadowConfig = CLodVirtualShadowBuildRuntimeResolutionConfig();

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_REQUESTS_DESCRIPTOR_INDEX] = m_allocationRequestsBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_REQUEST_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_RESOLUTION] = virtualShadowConfig.pageTableResolution;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    commandList.ExecuteIndirect(m_commandSignature->GetHandle(), m_indirectArgsBuffer->GetAPIResource().GetHandle(), 0, {}, 0, 1);
    return {};
}

void VirtualShadowMapClearDirtyBitsPass::Cleanup()
{
}