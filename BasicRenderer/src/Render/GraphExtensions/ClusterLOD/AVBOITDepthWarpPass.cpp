#include "Render/GraphExtensions/ClusterLOD/AVBOITDepthWarpPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"

#include "../shaders/PerPassRootConstants/clodAVBOITDepthWarpRootConstants.h"

AVBOITDepthWarpPass::AVBOITDepthWarpPass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<Buffer> occupancyHistogramBuffer,
    std::shared_ptr<Buffer> depthWarpLUTBuffer)
    : m_configBuffer(std::move(configBuffer))
    , m_occupancyHistogramBuffer(std::move(occupancyHistogramBuffer))
    , m_depthWarpLUTBuffer(std::move(depthWarpLUTBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/AVBOITDepthWarp.hlsl",
        L"CLodAVBOITDepthWarpCS",
        {},
        "CLod.AVBOITDepthWarp.PSO");
}

void AVBOITDepthWarpPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_configBuffer, m_occupancyHistogramBuffer)
        .WithUnorderedAccess(m_depthWarpLUTBuffer);
}

void AVBOITDepthWarpPass::Setup()
{
}

void AVBOITDepthWarpPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

PassReturn AVBOITDepthWarpPass::Execute(PassExecutionContext& executionContext)
{
    if (!m_configBuffer || !m_occupancyHistogramBuffer || !m_depthWarpLUTBuffer) {
        return {};
    }

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t misc[NumMiscUintRootConstants] = {};
    misc[CLOD_AVBOIT_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_DEPTH_WARP_HISTOGRAM_DESCRIPTOR_INDEX] = m_occupancyHistogramBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_DESCRIPTOR_INDEX] = m_depthWarpLUTBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        misc);

    const uint32_t groupCountX =
        (CLodAVBOITDepthWarpLUTResolution + 63u) / 64u;
    commandList.Dispatch(groupCountX, 1u, 1u);
    return {};
}

void AVBOITDepthWarpPass::Cleanup()
{
}