#include "Render/GraphExtensions/ClusterLOD/AVBOITIntegratePass.h"

#include "Managers/Singletons/PSOManager.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "../shaders/PerPassRootConstants/clodAVBOITIntegrateRootConstants.h"
#include "Render/RenderContext.h"

AVBOITIntegratePass::AVBOITIntegratePass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<Buffer> fitStateBuffer,
    std::shared_ptr<PixelBuffer> occupancyTexture,
    std::shared_ptr<PixelBuffer> coverageTexture,
    std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
    std::shared_ptr<PixelBuffer> scalarExtinctionTexture,
    std::shared_ptr<PixelBuffer> chromaticExtinctionTexture,
    std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
    std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_fitStateBuffer(std::move(fitStateBuffer))
    , m_occupancyTexture(std::move(occupancyTexture))
    , m_coverageTexture(std::move(coverageTexture))
    , m_occupancySliceMaskTexture(std::move(occupancySliceMaskTexture))
    , m_scalarExtinctionTexture(std::move(scalarExtinctionTexture))
    , m_chromaticExtinctionTexture(std::move(chromaticExtinctionTexture))
    , m_integratedTransmittanceTexture(std::move(integratedTransmittanceTexture))
    , m_zeroTransmittanceSliceTexture(std::move(zeroTransmittanceSliceTexture))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/AVBOITIntegrate.hlsl",
        L"CLodAVBOITIntegrateCS",
        {},
        "CLod.AVBOITIntegrate.PSO");
}

void AVBOITIntegratePass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_configBuffer);
    builder->WithShaderResource(m_fitStateBuffer);

    builder->WithUnorderedAccess(
        Builtin::DebugVisualization,
        m_occupancyTexture,
        m_coverageTexture,
        m_occupancySliceMaskTexture,
        m_scalarExtinctionTexture,
        m_chromaticExtinctionTexture,
        m_integratedTransmittanceTexture,
        m_zeroTransmittanceSliceTexture);

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void AVBOITIntegratePass::Setup()
{
}

void AVBOITIntegratePass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
    m_declaredResourcesChanged = false;
}

bool AVBOITIntegratePass::DeclaredResourcesChanged() const
{
    return m_declaredResourcesChanged;
}

PassReturn AVBOITIntegratePass::Execute(PassExecutionContext& executionContext)
{
    if (!m_configBuffer || !m_fitStateBuffer || !m_occupancyTexture || !m_coverageTexture ||
        !m_occupancySliceMaskTexture || !m_scalarExtinctionTexture || !m_chromaticExtinctionTexture ||
        !m_zeroTransmittanceSliceTexture) {
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
    misc[CLOD_AVBOIT_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_INTEGRATE_FIT_STATE_DESCRIPTOR_INDEX] = m_fitStateBuffer->GetSRVInfo(0).slot.index;
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

void AVBOITIntegratePass::Cleanup()
{
}