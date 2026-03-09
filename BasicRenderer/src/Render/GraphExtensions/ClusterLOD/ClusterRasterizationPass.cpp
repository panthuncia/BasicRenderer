#include "Render/GraphExtensions/ClusterLOD/ClusterRasterizationPass.h"

#include <algorithm>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodRootConstants.h"

ClusterRasterizationPass::ClusterRasterizationPass(
    ClusterRasterizationPassInputs inputs,
    std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
    std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
    std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer)
    : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer))
    , m_rasterBucketsHistogramBuffer(std::move(rasterBucketsHistogramBuffer))
    , m_rasterBucketsIndirectArgsBuffer(std::move(rasterBucketsIndirectArgsBuffer)) {
    m_wireframe = inputs.wireframe;
    m_clearGbuffer = inputs.clearGbuffer;

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName("CLodViewRasterInfoBuffer");

    rhi::IndirectArg args[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
        {.kind = rhi::IndirectArgKind::DispatchMesh }
    };
    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterizeClustersCommand) },
        PSOManager::GetInstance().GetRootSignature().GetHandle(),
        m_rasterizationCommandSignature);
}

ClusterRasterizationPass::~ClusterRasterizationPass() = default;

void ClusterRasterizationPass::DeclareResourceUsages(RenderPassBuilder* builder) {
    builder->WithShaderResource(
            Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::PostSkinningVertices,
            Builtin::CameraBuffer,
            Builtin::CLod::Offsets,
            Builtin::CLod::GroupChunks,
            Builtin::CLod::Groups,
            Builtin::CLod::MeshMetadata,
            Builtin::MeshResources::MeshletTriangles,
            Builtin::MeshResources::MeshletVertexIndices,
            Builtin::MeshResources::MeshletOffsets,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBuffer,
            m_viewRasterInfoBuffer)
        .WithIndirectArguments(m_rasterBucketsIndirectArgsBuffer)
        .IsGeometryPass();

    for (auto& vb : m_visibilityBuffers) {
        builder->WithUnorderedAccess(vb);
    }
}

void ClusterRasterizationPass::Setup() {
    RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
    RegisterSRV(Builtin::MeshResources::MeshletTriangles);
    RegisterSRV(Builtin::CLod::Offsets);
    RegisterSRV(Builtin::CLod::GroupChunks);
    RegisterSRV(Builtin::CLod::Groups);
    RegisterSRV(Builtin::NormalMatrixBuffer);
    RegisterSRV(Builtin::PostSkinningVertices);
    RegisterSRV(Builtin::PerObjectBuffer);
    RegisterSRV(Builtin::CameraBuffer);
    RegisterSRV(Builtin::PerMeshInstanceBuffer);
    RegisterSRV(Builtin::PerMeshBuffer);
    RegisterSRV(Builtin::PerMaterialDataBuffer);
    RegisterSRV(Builtin::MeshResources::MeshletOffsets);
    RegisterSRV(Builtin::CLod::MeshMetadata);
}

void ClusterRasterizationPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    auto numViews = context.viewManager->GetCameraBufferSize();

    m_visibilityBuffers.clear();

    uint32_t maxViewWidth = 1;
    uint32_t maxViewHeight = 1;

    context.viewManager->ForEachView([&](uint64_t v) {
        auto viewInfo = context.viewManager->Get(v);
        if (viewInfo->gpu.visibilityBuffer != nullptr) {
            maxViewWidth = std::max(maxViewWidth, viewInfo->gpu.visibilityBuffer->GetWidth());
            maxViewHeight = std::max(maxViewHeight, viewInfo->gpu.visibilityBuffer->GetHeight());
        }
    });

    std::vector<CLodViewRasterInfo> viewRasterInfo(numViews);
    context.viewManager->ForEachView([&](uint64_t v) {
        auto viewInfo = context.viewManager->Get(v);
        if (viewInfo->gpu.visibilityBuffer != nullptr) {
            auto cameraIndex = viewInfo->gpu.cameraBufferIndex;

            CLodViewRasterInfo info{};
            info.visibilityUAVDescriptorIndex = viewInfo->gpu.visibilityBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            info.scissorMinX = 0;
            info.scissorMinY = 0;
            info.scissorMaxX = viewInfo->gpu.visibilityBuffer->GetWidth();
            info.scissorMaxY = viewInfo->gpu.visibilityBuffer->GetHeight();
            info.viewportScaleX = static_cast<float>(info.scissorMaxX) / static_cast<float>(maxViewWidth);
            info.viewportScaleY = static_cast<float>(info.scissorMaxY) / static_cast<float>(maxViewHeight);
            viewRasterInfo[cameraIndex] = info;

            m_visibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
        }
    });

    m_passWidth = maxViewWidth;
    m_passHeight = maxViewHeight;

    if (m_viewRasterInfos != viewRasterInfo) {
        m_viewRasterInfos = std::move(viewRasterInfo);
        m_viewRasterInfoBuffer->ResizeStructured(static_cast<uint32_t>(m_viewRasterInfos.size()));
        BUFFER_UPLOAD(
            m_viewRasterInfos.data(),
            static_cast<uint32_t>(m_viewRasterInfos.size() * sizeof(CLodViewRasterInfo)),
            rg::runtime::UploadTarget::FromShared(m_viewRasterInfoBuffer),
            0);
        m_declaredResourcesChanged = true;
    }
    else {
        m_declaredResourcesChanged = false;
    }
}

bool ClusterRasterizationPass::DeclaredResourcesChanged() const {
    return m_declaredResourcesChanged;
}

PassReturn ClusterRasterizationPass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    rhi::PassBeginInfo p{};
    p.width = m_passWidth;
    p.height = m_passHeight;
    p.debugName = "CLod raster pass";

    executionContext.commandList.BeginPass(p);

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
    commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());

    auto& psoManager = PSOManager::GetInstance();

    uint32_t misc[NumMiscUintRootConstants] = {};
    misc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_rasterBucketsHistogramBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
    commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);

    auto numBuckets = context.materialManager->GetRasterBucketCount();
    if (numBuckets == 0) {
        return {};
    }

    auto apiResource = m_rasterBucketsIndirectArgsBuffer->GetAPIResource();
    auto stride = sizeof(RasterizeClustersCommand);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto flags = context.materialManager->GetRasterFlagsForBucket(i);
        auto& pso = psoManager.GetClusterLODRasterPSO(
            flags,
            m_wireframe);

        BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
        commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

        const uint64_t argOffset = static_cast<uint64_t>(i) * stride;
        commandList.ExecuteIndirect(
            m_rasterizationCommandSignature->GetHandle(),
            apiResource.GetHandle(),
            argOffset,
            {},
            0,
            1);
    }

    return {};
}

void ClusterRasterizationPass::Cleanup() {
}
