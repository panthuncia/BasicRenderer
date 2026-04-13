#include "Render/GraphExtensions/ClusterLOD/FixedSliceScalarVBOITResolvePass.h"

#include "Managers/Singletons/PSOManager.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "Render/RenderContext.h"
#include "../shaders/PerPassRootConstants/clodFixedSliceScalarVBOITResolveRootConstants.h"

FixedSliceScalarVBOITResolvePass::FixedSliceScalarVBOITResolvePass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<PixelBuffer> accumulationTexture,
    std::shared_ptr<PixelBuffer> shadingExtinctionTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_accumulationTexture(std::move(accumulationTexture))
    , m_shadingExtinctionTexture(std::move(shadingExtinctionTexture))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/FixedSliceScalarVBOITResolve.hlsl",
        L"CLodFixedSliceScalarVBOITResolveCS",
        {},
        "CLod.FixedSliceScalarVBOITResolve.PSO");
}

void FixedSliceScalarVBOITResolvePass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            m_configBuffer,
            m_accumulationTexture,
            m_shadingExtinctionTexture)
        .WithUnorderedAccess(Builtin::Color::HDRColorTarget);
}

void FixedSliceScalarVBOITResolvePass::Setup()
{
}

void FixedSliceScalarVBOITResolvePass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

PassReturn FixedSliceScalarVBOITResolvePass::Execute(PassExecutionContext& executionContext)
{
    if (!m_configBuffer || !m_accumulationTexture || !m_shadingExtinctionTexture) {
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
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_RESOLVE_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_RESOLVE_ACCUMULATION_DESCRIPTOR_INDEX] = m_accumulationTexture->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_RESOLVE_SHADING_EXTINCTION_DESCRIPTOR_INDEX] =
        m_shadingExtinctionTexture->GetSRVInfo(0).slot.index;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        misc);

    const uint32_t groupCountX = (m_accumulationTexture->GetWidth() + 7u) / 8u;
    const uint32_t groupCountY = (m_accumulationTexture->GetHeight() + 7u) / 8u;
    commandList.Dispatch(groupCountX, groupCountY, 1u);
    return {};
}

void FixedSliceScalarVBOITResolvePass::Cleanup()
{
}
