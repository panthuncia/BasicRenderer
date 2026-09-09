#include "Render/GraphExtensions/ClusterLOD/RasterBucketCompactAndArgsPass.h"

#include <algorithm>
#include <vector>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"
#include "../shaders/PerPassRootConstants/clodCompactionRootConstants.h"

RasterBucketCompactAndArgsPass::RasterBucketCompactAndArgsPass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> visibleClusterTransformIndicesBuffer,
    std::shared_ptr<Buffer> visibleClustersCounterBuffer,
    std::shared_ptr<Buffer> compactedBaseCounterBuffer,
    std::shared_ptr<Buffer> readBaseCounterBuffer,
    std::shared_ptr<Buffer> indirectCommand,
    std::shared_ptr<Buffer> histogramBuffer,
    std::shared_ptr<Buffer> offsetsBuffer,
    std::shared_ptr<Buffer> writeCursorBuffer,
    std::shared_ptr<Buffer> compactedClustersBuffer,
    std::shared_ptr<Buffer> compactedClusterTransformIndicesBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> sortedToUnsortedMappingBuffer,
    std::shared_ptr<Buffer> reyesOwnershipBitsetBuffer,
    std::shared_ptr<Buffer> telemetryBuffer,
    uint64_t maxVisibleClusters,
    bool appendToExisting,
    bool readReverse,
    bool buildSoftwareRasterDispatch,
    bool runWhenComputeSWRasterEnabledOnly)
    : m_visibleClustersBuffer(std::move(visibleClustersBuffer))
    , m_visibleClusterTransformIndicesBuffer(std::move(visibleClusterTransformIndicesBuffer))
    , m_visibleClustersCounterBuffer(std::move(visibleClustersCounterBuffer))
    , m_compactedBaseCounterBuffer(std::move(compactedBaseCounterBuffer))
    , m_readBaseCounterBuffer(std::move(readBaseCounterBuffer))
    , m_indirectCommand(std::move(indirectCommand))
    , m_histogramBuffer(std::move(histogramBuffer))
    , m_offsetsBuffer(std::move(offsetsBuffer))
    , m_writeCursorBuffer(std::move(writeCursorBuffer))
    , m_compactedClustersBuffer(std::move(compactedClustersBuffer))
    , m_compactedClusterTransformIndicesBuffer(std::move(compactedClusterTransformIndicesBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_sortedToUnsortedMappingBuffer(std::move(sortedToUnsortedMappingBuffer))
    , m_reyesOwnershipBitsetBuffer(std::move(reyesOwnershipBitsetBuffer))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_maxVisibleClusters(maxVisibleClusters)
    , m_appendToExisting(appendToExisting)
    , m_readReverse(readReverse)
    , m_buildSoftwareRasterDispatch(buildSoftwareRasterDispatch)
    , m_runWhenComputeSWRasterEnabledOnly(runWhenComputeSWRasterEnabledOnly)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"CompactClustersAndBuildIndirectArgsCS",
        {},
        "CLod_RasterBucketsCompactAndArgsPSO");
    m_clearPipeline = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain",
        {},
        "CLod_RasterBucketsClearUintPSO");

    rhi::IndirectArg args[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    rhi::CommandSignaturePtr commandSignature;
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        commandSignature);
    m_compactionCommandSignature = std::make_shared<rhi::CommandSignaturePtr>(std::move(commandSignature));
}

void RasterBucketCompactAndArgsPass::Declare(org::PassBuilder& builder) {
    builder.WithShaderResource(
            m_visibleClustersBuffer,
            m_visibleClusterTransformIndicesBuffer,
            m_visibleClustersCounterBuffer,
            m_compactedBaseCounterBuffer,
            m_histogramBuffer,
            m_offsetsBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::InstanceDrawRecordBuffer,
            Builtin::PerInstanceTransformBuffer,
            Builtin::SkeletonResources::SkinningInstanceInfo,
            Builtin::PerMeshBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::Material::TextureStreamingMetadataBuffer)
        .WithUnorderedAccess(
            Builtin::Material::TextureStreamingFeedbackBuffer,
            m_writeCursorBuffer,
            m_compactedClustersBuffer,
            m_compactedClusterTransformIndicesBuffer,
            m_indirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer)
        .WithIndirectArguments(m_indirectCommand);
    if (m_reyesOwnershipBitsetBuffer) {
        builder.WithShaderResource(m_reyesOwnershipBitsetBuffer);
    }
    if (m_readBaseCounterBuffer) {
        builder.WithShaderResource(m_readBaseCounterBuffer);
    }
    if (m_telemetryBuffer) {
        builder.WithUnorderedAccess(m_telemetryBuffer);
    }

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}


RasterBucketCompactAndArgsPreparedData RasterBucketCompactAndArgsPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    PreparedData data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    data.clearProgram = preparation.CaptureProgram(m_clearPipeline);
    data.compactProgram = preparation.CaptureProgram(m_pso);
    preparation.Retain(m_compactionCommandSignature);
    data.commandSignature = (*m_compactionCommandSignature)->GetHandle();
    data.indirectCommand = preparation.CaptureResource(m_indirectCommand->GetGlobalResourceID());
    data.cursorResource = preparation.CaptureResource(m_writeCursorBuffer->GetGlobalResourceID());
    data.clearDescriptorIndices = CaptureResourceDescriptorIndices(m_clearPipeline.GetResourceDescriptorSlots());
    data.compactDescriptorIndices = CaptureResourceDescriptorIndices(m_pso.GetResourceDescriptorSlots());
    data.clearConstants.resize(NumMiscUintRootConstants);
    data.compactConstants.resize(NumMiscUintRootConstants);
    const uint32_t numBuckets = context->materialManager->GetRasterBucketCount();
    data.enabled = numBuckets != 0u && (!m_runWhenComputeSWRasterEnabledOnly
        || CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)()));
    data.clearGroups = (numBuckets + 63u) / 64u;
    data.clearConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.clearConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = numBuckets;
    auto& c = data.compactConstants;
    c[CLOD_COMPACTION_READ_BASE_COUNTER_DESCRIPTOR_INDEX] = m_appendToExisting && m_readBaseCounterBuffer ? m_readBaseCounterBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
    c[CLOD_COMPACTION_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_COMPACTION_VISIBLE_CLUSTER_TRANSFORM_INDICES_DESCRIPTOR_INDEX] = m_visibleClusterTransformIndicesBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_COMPACTION_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_COMPACTION_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_COMPACTION_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_COMPACTION_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_COMPACTION_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_COMPACTION_COMPACTED_VISIBLE_CLUSTER_TRANSFORM_INDICES_DESCRIPTOR_INDEX] = m_compactedClusterTransformIndicesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_COMPACTION_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_COMPACTION_APPEND_BASE_COUNTER_DESCRIPTOR_INDEX] = m_compactedBaseCounterBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_COMPACTION_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX] = m_sortedToUnsortedMappingBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_COMPACTION_REYES_OWNERSHIP_BITSET_DESCRIPTOR_INDEX] = m_reyesOwnershipBitsetBuffer ? m_reyesOwnershipBitsetBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
    c[CLOD_COMPACTION_TELEMETRY_DESCRIPTOR_INDEX] = m_telemetryBuffer && IsCLodWorkGraphTelemetryEnabled() ? m_telemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index : 0xFFFFFFFFu;
    c[CLOD_COMPACTION_NUM_RASTER_BUCKETS] = numBuckets | (m_appendToExisting ? 0x80000000u : 0u);
    c[CLOD_COMPACTION_READ_MODE_FLAGS] = (m_readReverse ? CLOD_COMPACTION_READ_FLAG_REVERSED : 0u)
        | (m_buildSoftwareRasterDispatch ? CLOD_COMPACTION_READ_FLAG_BUILD_SW_DISPATCH : 0u)
        | (m_reyesOwnershipBitsetBuffer ? CLOD_COMPACTION_READ_FLAG_SKIP_REYES_OWNED : 0u);
    c[CLOD_COMPACTION_READ_CAPACITY] = static_cast<uint32_t>(m_maxVisibleClusters);
    return data;
}

void RasterBucketCompactAndArgsPass::Record(const PreparedData& data, org::PassRecordContext& recording) {
    if (!data.enabled) return;
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    auto bindIndices = [&](const std::vector<unsigned int>& indices) {
        if (!indices.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(indices.size()), indices.data());
    };
    commands.BindPipeline(recording.Resolve(data.clearProgram));
    bindIndices(data.clearDescriptorIndices);
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.clearConstants.data());
    commands.Dispatch(data.clearGroups, 1, 1);
    rhi::BufferBarrier barrier{};
    barrier.buffer = recording.Resolve(data.cursorResource).GetHandle();
    barrier.beforeAccess = barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    barrier.beforeSync = barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
    rhi::BarrierBatch barriers{}; barriers.buffers = {&barrier}; commands.Barriers(barriers);
    commands.BindPipeline(recording.Resolve(data.compactProgram));
    bindIndices(data.compactDescriptorIndices);
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.compactConstants.data());
    commands.ExecuteIndirect(data.commandSignature,
        recording.Resolve(data.indirectCommand).GetHandle(), 0, {}, 0, 1);
}

void RasterBucketCompactAndArgsPass::Update(const UpdateExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        return;
    }

    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    auto numBuckets = context.preparedRasterBucketCount;

    if (m_writeCursorBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
        m_writeCursorBuffer->ResizeStructured(numBuckets);
    }
    if (m_indirectArgsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(RasterizeClustersCommand)) {
        m_indirectArgsBuffer->ResizeStructured(numBuckets);
    }
    BT_PLOT("CLod.RasterArgs.UpdateBucketCount", static_cast<int64_t>(numBuckets));
    BT_PLOT("CLod.RasterArgs.UpdateBackingBytes", static_cast<int64_t>(m_indirectArgsBuffer->GetSize()));

}

