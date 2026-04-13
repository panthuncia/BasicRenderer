#include "Render/GraphExtensions/ClusterLOD/FixedSliceScalarVBOITDepthWarpPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"

#include "../shaders/PerPassRootConstants/clodFixedSliceScalarVBOITDepthWarpRootConstants.h"

FixedSliceScalarVBOITDepthWarpPass::FixedSliceScalarVBOITDepthWarpPass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<Buffer> occupancyHistogramBuffer,
    std::shared_ptr<Buffer> depthWarpLUTBuffer)
    : m_configBuffer(std::move(configBuffer))
    , m_occupancyHistogramBuffer(std::move(occupancyHistogramBuffer))
    , m_depthWarpLUTBuffer(std::move(depthWarpLUTBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/FixedSliceScalarVBOITDepthWarp.hlsl",
        L"CLodFixedSliceScalarVBOITDepthWarpCS",
        {},
        "CLod.FixedSliceScalarVBOITDepthWarp.PSO");
}

void FixedSliceScalarVBOITDepthWarpPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_configBuffer, m_occupancyHistogramBuffer)
        .WithUnorderedAccess(m_depthWarpLUTBuffer);
}

void FixedSliceScalarVBOITDepthWarpPass::Setup()
{
}

void FixedSliceScalarVBOITDepthWarpPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

PassReturn FixedSliceScalarVBOITDepthWarpPass::Execute(PassExecutionContext& executionContext)
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
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_HISTOGRAM_DESCRIPTOR_INDEX] = m_occupancyHistogramBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_LUT_DESCRIPTOR_INDEX] = m_depthWarpLUTBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        misc);

    commandList.Dispatch(1u, 1u, 1u);
    return {};
}

void FixedSliceScalarVBOITDepthWarpPass::Cleanup()
{
}