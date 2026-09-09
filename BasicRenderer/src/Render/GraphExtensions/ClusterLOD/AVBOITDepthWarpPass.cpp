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

void AVBOITDepthWarpPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_configBuffer, m_occupancyHistogramBuffer)
        .WithUnorderedAccess(m_depthWarpLUTBuffer);
}

br::render::PreparedComputeDispatch AVBOITDepthWarpPass::Prepare(const org::PassPrepareContext& preparation) {
    br::render::PreparedComputeDispatch data{};
    if (!m_configBuffer || !m_occupancyHistogramBuffer || !m_depthWarpLUTBuffer) {
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
    misc[CLOD_AVBOIT_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_DEPTH_WARP_HISTOGRAM_DESCRIPTOR_INDEX] = m_occupancyHistogramBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_DESCRIPTOR_INDEX] = m_depthWarpLUTBuffer->GetUAVShaderVisibleInfo(0).slot.index;

    const uint32_t groupCountX =
        (CLodAVBOITDepthWarpLUTResolution + 63u) / 64u;
    data.groupsX = groupCountX; data.groupsY = 1u; data.groupsZ = 1u;
    return data;
}

void AVBOITDepthWarpPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
