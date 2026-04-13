#include "Render/GraphExtensions/ClusterLOD/FixedSliceScalarVBOITAdaptiveFitPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"

#include "../shaders/PerPassRootConstants/clodFixedSliceScalarVBOITAdaptiveFitRootConstants.h"

FixedSliceScalarVBOITAdaptiveFitPass::FixedSliceScalarVBOITAdaptiveFitPass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<Buffer> fitStateBuffer)
    : m_configBuffer(std::move(configBuffer))
    , m_fitStateBuffer(std::move(fitStateBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/FixedSliceScalarVBOITAdaptiveFit.hlsl",
        L"CLodFixedSliceScalarVBOITAdaptiveFitCS",
        {},
        "CLod.FixedSliceScalarVBOITAdaptiveFit.PSO");
}

void FixedSliceScalarVBOITAdaptiveFitPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_fitStateBuffer)
        .WithUnorderedAccess(m_configBuffer);
}

void FixedSliceScalarVBOITAdaptiveFitPass::Setup()
{
}

void FixedSliceScalarVBOITAdaptiveFitPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

PassReturn FixedSliceScalarVBOITAdaptiveFitPass::Execute(PassExecutionContext& executionContext)
{
    if (!m_configBuffer || !m_fitStateBuffer) {
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
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_ADAPTIVE_FIT_CONFIG_DESCRIPTOR_INDEX] =
        m_configBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_ADAPTIVE_FIT_STATE_DESCRIPTOR_INDEX] =
        m_fitStateBuffer->GetSRVInfo(0).slot.index;
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

void FixedSliceScalarVBOITAdaptiveFitPass::Cleanup()
{
}