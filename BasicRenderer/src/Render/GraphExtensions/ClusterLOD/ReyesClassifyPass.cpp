#include "Render/GraphExtensions/ClusterLOD/ReyesClassifyPass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodReyesRootConstants.h"
#include "Resources/Buffers/Buffer.h"
#include "RenderPasses/PreparedComputeDispatch.h"

ReyesClassifyPass::ReyesClassifyPass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> visibleClustersCounterBuffer,
    std::shared_ptr<Buffer> visibleClustersReadBaseCounterBuffer,
    std::shared_ptr<Buffer> fullClusterOutputsBuffer,
    std::shared_ptr<Buffer> fullClusterCounterBuffer,
    uint32_t fullClusterOutputCapacity,
    std::shared_ptr<Buffer> ownedClustersBuffer,
    std::shared_ptr<Buffer> ownedClustersCounterBuffer,
    uint32_t ownedClusterCapacity,
    std::shared_ptr<Buffer> ownershipBitsetBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> telemetryBuffer,
    uint32_t phaseIndex,
    ReyesClassifyMode classifyMode)
    : m_visibleClustersBuffer(std::move(visibleClustersBuffer))
    , m_visibleClustersCounterBuffer(std::move(visibleClustersCounterBuffer))
    , m_visibleClustersReadBaseCounterBuffer(std::move(visibleClustersReadBaseCounterBuffer))
    , m_fullClusterOutputsBuffer(std::move(fullClusterOutputsBuffer))
    , m_fullClusterCounterBuffer(std::move(fullClusterCounterBuffer))
    , m_fullClusterOutputCapacity(fullClusterOutputCapacity)
    , m_ownedClustersBuffer(std::move(ownedClustersBuffer))
    , m_ownedClustersCounterBuffer(std::move(ownedClustersCounterBuffer))
    , m_ownedClusterCapacity(ownedClusterCapacity)
    , m_ownershipBitsetBuffer(std::move(ownershipBitsetBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_phaseIndex(phaseIndex)
    , m_classifyMode(classifyMode) {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/reyesClassify.hlsl",
        L"ReyesClassifyCS",
        {},
        "CLod.ReyesClassify.PSO");

    rhi::IndirectArg dispatchArgs[] = {
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    rhi::CommandSignaturePtr commandSignature;
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(dispatchArgs, 1), sizeof(CLodReyesDispatchIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        commandSignature);
    m_commandSignature = std::make_shared<rhi::CommandSignaturePtr>(std::move(commandSignature));
}

void ReyesClassifyPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_visibleClustersBuffer, m_visibleClustersCounterBuffer)
        .WithShaderResource(
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::InstanceDrawRecordBuffer,
            Builtin::PerInstanceTransformBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::CLod::Offsets,
            Builtin::CLod::MeshMetadata,
            Builtin::CLod::AssemblyTransforms,
            Builtin::CLod::AssemblyBoneRemaps,
            Builtin::CLod::AssemblyBoneRemapIndices,
            Builtin::Material::TextureStreamingMetadataBuffer,
            Builtin::PerObjectBuffer,
            Builtin::CullingCameraBuffer,
            Builtin::SkeletonResources::InverseBindMatrices,
            Builtin::SkeletonResources::BoneTransforms,
            Builtin::SkeletonResources::SkinningInstanceInfo)
        .WithConstantBuffer(Builtin::PerFrameBuffer)
        .WithUnorderedAccess(
            Builtin::Material::TextureStreamingFeedbackBuffer,
            m_fullClusterOutputsBuffer,
            m_fullClusterCounterBuffer,
            m_ownedClustersBuffer,
            m_ownedClustersCounterBuffer,
            m_telemetryBuffer);
    m_indirectArgumentsBinding = builder.BindIndirectArguments(m_indirectArgsBuffer);
    if (m_ownershipBitsetBuffer) {
        builder.WithUnorderedAccess(m_ownershipBitsetBuffer);
    }
    if (m_visibleClustersReadBaseCounterBuffer) {
        builder.WithShaderResource(m_visibleClustersReadBaseCounterBuffer);
    }
}

br::render::PreparedComputeIndirect ReyesClassifyPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedComputeIndirect data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.commandSignature = preparation.CaptureCommandSignature(m_commandSignature);
    data.argumentsReference = preparation.CaptureResource(m_indirectArgumentsBinding);
    auto program = preparation.CaptureProgramBinding(m_pso);
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_READ_BASE_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersReadBaseCounterBuffer ? m_visibleClustersReadBaseCounterBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
    data.constants[CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_CLASSIFY_FULL_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_fullClusterOutputsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_CLASSIFY_FULL_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_fullClusterCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_CLASSIFY_FULL_CLUSTERS_CAPACITY] = m_fullClusterOutputCapacity;
    data.constants[CLOD_REYES_CLASSIFY_OWNED_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_ownedClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_CLASSIFY_OWNED_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_ownedClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_CLASSIFY_OWNED_CLUSTERS_CAPACITY] = m_ownedClusterCapacity;
    data.constants[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX] = m_telemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_CLASSIFY_PHASE_INDEX] = m_phaseIndex;
    data.constants[CLOD_REYES_CLASSIFY_OWNERSHIP_BITSET_DESCRIPTOR_INDEX] = m_ownershipBitsetBuffer ? m_ownershipBitsetBuffer->GetUAVShaderVisibleInfo(0).slot.index : 0xFFFFFFFFu;
    data.constants[CLOD_REYES_CLASSIFY_MODE] = static_cast<uint32_t>(m_classifyMode);
    return data;
}

void ReyesClassifyPass::Record(const br::render::PreparedComputeIndirect& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeIndirect(data, recording);
}
