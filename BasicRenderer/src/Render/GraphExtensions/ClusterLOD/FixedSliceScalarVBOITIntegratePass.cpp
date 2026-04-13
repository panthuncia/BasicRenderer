#include "Render/GraphExtensions/ClusterLOD/FixedSliceScalarVBOITIntegratePass.h"

#include "Managers/Singletons/PSOManager.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "../shaders/PerPassRootConstants/clodFixedSliceScalarVBOITIntegrateRootConstants.h"
#include "Render/RenderContext.h"

FixedSliceScalarVBOITIntegratePass::FixedSliceScalarVBOITIntegratePass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<Buffer> fitStateBuffer,
    std::shared_ptr<PixelBuffer> occupancyTexture,
    std::shared_ptr<PixelBuffer> coverageTexture,
    std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
    std::shared_ptr<PixelBuffer> extinctionTexture,
    std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
    std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_fitStateBuffer(std::move(fitStateBuffer))
    , m_occupancyTexture(std::move(occupancyTexture))
    , m_coverageTexture(std::move(coverageTexture))
    , m_occupancySliceMaskTexture(std::move(occupancySliceMaskTexture))
    , m_extinctionTexture(std::move(extinctionTexture))
    , m_integratedTransmittanceTexture(std::move(integratedTransmittanceTexture))
    , m_zeroTransmittanceSliceTexture(std::move(zeroTransmittanceSliceTexture))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/FixedSliceScalarVBOITIntegrate.hlsl",
        L"CLodFixedSliceScalarVBOITIntegrateCS",
        {},
        "CLod.FixedSliceScalarVBOITIntegrate.PSO");
}

void FixedSliceScalarVBOITIntegratePass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_configBuffer);
    builder->WithShaderResource(m_fitStateBuffer);

    builder->WithUnorderedAccess(
        Builtin::DebugVisualization,
        m_occupancyTexture,
        m_coverageTexture,
        m_occupancySliceMaskTexture,
        m_extinctionTexture,
        m_integratedTransmittanceTexture,
        m_zeroTransmittanceSliceTexture);

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void FixedSliceScalarVBOITIntegratePass::Setup()
{
}

void FixedSliceScalarVBOITIntegratePass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
    m_declaredResourcesChanged = false;
}

bool FixedSliceScalarVBOITIntegratePass::DeclaredResourcesChanged() const
{
    return m_declaredResourcesChanged;
}

PassReturn FixedSliceScalarVBOITIntegratePass::Execute(PassExecutionContext& executionContext)
{
    if (!m_configBuffer || !m_fitStateBuffer || !m_occupancyTexture || !m_coverageTexture || !m_occupancySliceMaskTexture || !m_extinctionTexture || !m_zeroTransmittanceSliceTexture) {
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
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_FIT_STATE_DESCRIPTOR_INDEX] = m_fitStateBuffer->GetSRVInfo(0).slot.index;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        misc);

    const uint32_t groupCountX = (m_occupancyTexture->GetWidth() + 7u) / 8u;
    const uint32_t groupCountY = (m_occupancyTexture->GetHeight() + 7u) / 8u;
    commandList.Dispatch(groupCountX, groupCountY, 1u);
    return {};
}

void FixedSliceScalarVBOITIntegratePass::Cleanup()
{
}