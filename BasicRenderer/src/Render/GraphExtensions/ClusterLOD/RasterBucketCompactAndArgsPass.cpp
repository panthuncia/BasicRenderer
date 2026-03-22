#include "Render/GraphExtensions/ClusterLOD/RasterBucketCompactAndArgsPass.h"

#include <algorithm>
#include <vector>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodRootConstants.h"

RasterBucketCompactAndArgsPass::RasterBucketCompactAndArgsPass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> visibleClustersCounterBuffer,
    std::shared_ptr<Buffer> compactedBaseCounterBuffer,
    std::shared_ptr<Buffer> indirectCommand,
    std::shared_ptr<Buffer> histogramBuffer,
    std::shared_ptr<Buffer> offsetsBuffer,
    std::shared_ptr<Buffer> writeCursorBuffer,
    std::shared_ptr<Buffer> compactedClustersBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> sortedToUnsortedMappingBuffer,
    uint64_t maxVisibleClusters,
    bool appendToExisting)
    : m_visibleClustersBuffer(std::move(visibleClustersBuffer))
    , m_visibleClustersCounterBuffer(std::move(visibleClustersCounterBuffer))
    , m_compactedBaseCounterBuffer(std::move(compactedBaseCounterBuffer))
    , m_indirectCommand(std::move(indirectCommand))
    , m_histogramBuffer(std::move(histogramBuffer))
    , m_offsetsBuffer(std::move(offsetsBuffer))
    , m_writeCursorBuffer(std::move(writeCursorBuffer))
    , m_compactedClustersBuffer(std::move(compactedClustersBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_sortedToUnsortedMappingBuffer(std::move(sortedToUnsortedMappingBuffer))
    , m_maxVisibleClusters(maxVisibleClusters)
    , m_appendToExisting(appendToExisting)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"CompactClustersAndBuildIndirectArgsCS",
        {},
        "CLod_RasterBucketsCompactAndArgsPSO");

    rhi::IndirectArg args[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_compactionCommandSignature);
}

void RasterBucketCompactAndArgsPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_compactedBaseCounterBuffer,
            m_histogramBuffer,
            m_offsetsBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMaterialDataBuffer)
        .WithUnorderedAccess(
            m_writeCursorBuffer,
            m_compactedClustersBuffer,
            m_indirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer)
        .WithIndirectArguments(m_indirectCommand);
}

void RasterBucketCompactAndArgsPass::Setup() {
    RegisterSRV(Builtin::PerMeshInstanceBuffer);
    RegisterSRV(Builtin::PerMeshBuffer);
    RegisterSRV(Builtin::PerMaterialDataBuffer);
}

PassReturn RasterBucketCompactAndArgsPass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    auto& pm = PSOManager::GetInstance();

    auto numBuckets = context.materialManager->GetRasterBucketCount();
    const uint32_t kThreads = 64;
    const uint64_t maxItems = std::max<uint64_t>(m_maxVisibleClusters, numBuckets);
    const uint32_t groups = static_cast<uint32_t>((maxItems + kThreads - 1u) / kThreads);
    (void)groups;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    unsigned int rc[NumMiscUintRootConstants] = {};
    rc[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_COMPACTED_APPEND_BASE_COUNTER_DESCRIPTOR_INDEX] = m_compactedBaseCounterBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX] = m_sortedToUnsortedMappingBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_NUM_RASTER_BUCKETS] = numBuckets | (m_appendToExisting ? 0x80000000u : 0u);
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rc);

    commandList.ExecuteIndirect(
        m_compactionCommandSignature->GetHandle(),
        m_indirectCommand->GetAPIResource().GetHandle(),
        0,
        {},
        0,
        1);

    return {};
}

void RasterBucketCompactAndArgsPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    auto numBuckets = context.materialManager->GetRasterBucketCount();

    if (m_writeCursorBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
        m_writeCursorBuffer->ResizeStructured(numBuckets);
    }
    if (m_indirectArgsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(RasterizeClustersCommand)) {
        m_indirectArgsBuffer->ResizeStructured(numBuckets);
    }

    std::vector<uint32_t> zeroData(numBuckets, 0u);
    BUFFER_UPLOAD(
        zeroData.data(),
        static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)),
        rg::runtime::UploadTarget::FromShared(m_writeCursorBuffer),
        0);
}

void RasterBucketCompactAndArgsPass::Cleanup() {}
