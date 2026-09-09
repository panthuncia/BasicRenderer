#include "Render/GraphExtensions/ClusterLOD/AVBOITResolvePass.h"

#include "Managers/Singletons/PSOManager.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "Render/RenderContext.h"
#include "../shaders/PerPassRootConstants/clodAVBOITResolveRootConstants.h"

AVBOITResolvePass::AVBOITResolvePass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<PixelBuffer> accumulationTexture,
    std::shared_ptr<PixelBuffer> normalizationTexture,
    std::shared_ptr<PixelBuffer> shadingExtinctionTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_accumulationTexture(std::move(accumulationTexture))
    , m_normalizationTexture(std::move(normalizationTexture))
    , m_shadingExtinctionTexture(std::move(shadingExtinctionTexture))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/AVBOITResolve.hlsl",
        L"CLodAVBOITResolveCS",
        {},
        "CLod.AVBOITResolve.PSO");
}

void AVBOITResolvePass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(
            m_configBuffer,
            m_accumulationTexture,
            m_normalizationTexture,
            m_shadingExtinctionTexture)
        .WithUnorderedAccess(Builtin::Color::HDRColorTarget);
}

br::render::PreparedComputeDispatch AVBOITResolvePass::Prepare(const org::PassPrepareContext& preparation) {
    br::render::PreparedComputeDispatch data{};
    if (!m_configBuffer || !m_accumulationTexture || !m_normalizationTexture || !m_shadingExtinctionTexture) {
        return {};
    }

    const auto* renderContext = preparation.preparationData->Get<UpdateContext>();
    auto& context = *renderContext;

    data.resourceHeap = context.textureDescriptorHeap.GetHandle();
    data.samplerHeap = context.samplerDescriptorHeap.GetHandle();
    auto program = preparation.CaptureProgramBinding(m_pso);
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);

    auto& misc = data.constants;
    misc[CLOD_AVBOIT_VBOIT_RESOLVE_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_RESOLVE_ACCUMULATION_DESCRIPTOR_INDEX] = m_accumulationTexture->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_RESOLVE_NORMALIZATION_DESCRIPTOR_INDEX] =
        m_normalizationTexture->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_RESOLVE_SHADING_EXTINCTION_DESCRIPTOR_INDEX] =
        m_shadingExtinctionTexture->GetSRVInfo(0).slot.index;

    const uint32_t groupCountX = (m_accumulationTexture->GetWidth() + 7u) / 8u;
    const uint32_t groupCountY = (m_accumulationTexture->GetHeight() + 7u) / 8u;
    data.groupsX = groupCountX; data.groupsY = groupCountY; data.groupsZ = 1u;
    return data;
}

void AVBOITResolvePass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
