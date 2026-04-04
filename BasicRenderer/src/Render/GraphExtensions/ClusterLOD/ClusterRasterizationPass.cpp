#include "Render/GraphExtensions/ClusterLOD/ClusterRasterizationPass.h"

#include <algorithm>
#include <limits>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "../shaders/PerPassRootConstants/clodRasterizationRootConstants.h"

namespace {
constexpr uint32_t kDeepVisibilityAverageFragmentsPerPixel = 5u;
}

ClusterRasterizationPass::ClusterRasterizationPass(
    ClusterRasterizationPassInputs inputs,
    std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
    std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
    std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer,
    std::shared_ptr<Buffer> sortedToUnsortedMappingBuffer,
    std::shared_ptr<Buffer> deepVisibilityNodesBuffer,
    std::shared_ptr<Buffer> deepVisibilityCounterBuffer,
    std::shared_ptr<Buffer> deepVisibilityOverflowCounterBuffer,
    std::shared_ptr<ResourceGroup> slabResourceGroup,
    std::shared_ptr<PixelBuffer> virtualShadowPageTableTexture,
    std::shared_ptr<PixelBuffer> virtualShadowPhysicalPagesTexture,
    std::shared_ptr<Buffer> virtualShadowClipmapInfoBuffer)
    : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer))
    , m_rasterBucketsHistogramBuffer(std::move(rasterBucketsHistogramBuffer))
    , m_rasterBucketsIndirectArgsBuffer(std::move(rasterBucketsIndirectArgsBuffer))
    , m_sortedToUnsortedMappingBuffer(std::move(sortedToUnsortedMappingBuffer))
    , m_deepVisibilityNodesBuffer(std::move(deepVisibilityNodesBuffer))
    , m_deepVisibilityCounterBuffer(std::move(deepVisibilityCounterBuffer))
    , m_deepVisibilityOverflowCounterBuffer(std::move(deepVisibilityOverflowCounterBuffer))
    , m_virtualShadowPageTableTexture(std::move(virtualShadowPageTableTexture))
    , m_virtualShadowPhysicalPagesTexture(std::move(virtualShadowPhysicalPagesTexture))
    , m_virtualShadowClipmapInfoBuffer(std::move(virtualShadowClipmapInfoBuffer))
    , m_slabResourceGroup(std::move(slabResourceGroup)) {
    m_wireframe = inputs.wireframe;
    m_clearGbuffer = inputs.clearGbuffer;
    m_renderPhase = std::move(inputs.renderPhase);
    m_outputKind = inputs.outputKind;

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName("CLodViewRasterInfoBuffer");
    rg::memory::SetResourceUsageHint(*m_viewRasterInfoBuffer, "Cluster LOD rasterization");

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
            Builtin::SkeletonResources::InverseBindMatrices,
            Builtin::SkeletonResources::BoneTransforms,
            Builtin::SkeletonResources::SkinningInstanceInfo,
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
            m_viewRasterInfoBuffer,
            m_sortedToUnsortedMappingBuffer)
        .WithIndirectArguments(m_rasterBucketsIndirectArgsBuffer)
        .IsGeometryPass();

    if (m_outputKind == CLodRasterOutputKind::VisibilityBuffer) {
        for (auto& vb : m_visibilityBuffers) {
            builder->WithUnorderedAccess(vb);
        }
    }
    else if (m_outputKind == CLodRasterOutputKind::DeepVisibility) {
        for (auto& vb : m_visibilityBuffers) {
            builder->WithShaderResource(vb);
        }
        for (auto& headPointers : m_deepVisibilityHeadPointerBuffers) {
            builder->WithUnorderedAccess(headPointers);
        }
        builder->WithUnorderedAccess(
            m_deepVisibilityNodesBuffer,
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer);
    }
    else if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
        builder->WithShaderResource(m_virtualShadowClipmapInfoBuffer)
            .WithUnorderedAccess(m_virtualShadowPageTableTexture, m_virtualShadowPhysicalPagesTexture);
    }

    // Declare page pool slabs for bindless access (auto-invalidates when new slabs are added).
    if (m_slabResourceGroup) {
        builder->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
    }
}

void ClusterRasterizationPass::Setup() {
}

void ClusterRasterizationPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    auto numViews = context.viewManager->GetCameraBufferSize();
    std::vector<std::shared_ptr<PixelBuffer>> visibilityBuffers;
    std::vector<std::shared_ptr<PixelBuffer>> deepVisibilityHeadPointerBuffers;

    uint32_t maxViewWidth = 1;
    uint32_t maxViewHeight = 1;
    uint64_t totalViewPixels = 0;

    if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
        maxViewWidth = CLodVirtualShadowDefaultVirtualResolution;
        maxViewHeight = maxViewWidth;
    }

    context.viewManager->ForEachView([&](uint64_t v) {
        auto viewInfo = context.viewManager->Get(v);
        if (!viewInfo) {
            return;
        }

        if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
            if (viewInfo->flags.shadow && viewInfo->lightType == Components::LightType::Directional) {
                totalViewPixels += static_cast<uint64_t>(maxViewWidth) * static_cast<uint64_t>(maxViewHeight);
            }
            return;
        }

        if (!viewInfo->gpu.visibilityBuffer) {
            return;
        }

        if (m_outputKind == CLodRasterOutputKind::VisibilityBuffer) {
            maxViewWidth = std::max(maxViewWidth, viewInfo->gpu.visibilityBuffer->GetWidth());
            maxViewHeight = std::max(maxViewHeight, viewInfo->gpu.visibilityBuffer->GetHeight());
        }
        else {
            auto headPointers = context.viewManager->EnsureCLodDeepVisibilityHeadPointers(v);
            if (!headPointers) {
                return;
            }

            maxViewWidth = std::max(maxViewWidth, headPointers->GetWidth());
            maxViewHeight = std::max(maxViewHeight, headPointers->GetHeight());
            totalViewPixels += static_cast<uint64_t>(headPointers->GetWidth()) *
                static_cast<uint64_t>(headPointers->GetHeight());
        }
    });

    std::vector<CLodViewRasterInfo> viewRasterInfo(numViews);
    context.viewManager->ForEachView([&](uint64_t v) {
        auto viewInfo = context.viewManager->Get(v);
        if (!viewInfo) {
            return;
        }

        auto cameraIndex = viewInfo->gpu.cameraBufferIndex;
        CLodViewRasterInfo info{};
        info.scissorMinX = 0;
        info.scissorMinY = 0;

        if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
            if (viewInfo->flags.shadow && viewInfo->lightType == Components::LightType::Directional) {
                info.scissorMaxX = maxViewWidth;
                info.scissorMaxY = maxViewHeight;
                info.viewportScaleX = 1.0f;
                info.viewportScaleY = 1.0f;
            }
            viewRasterInfo[cameraIndex] = info;
            return;
        }

        if (!viewInfo->gpu.visibilityBuffer) {
            return;
        }

        if (m_outputKind == CLodRasterOutputKind::VisibilityBuffer) {
            info.visibilityUAVDescriptorIndex = viewInfo->gpu.visibilityBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            info.scissorMaxX = viewInfo->gpu.visibilityBuffer->GetWidth();
            info.scissorMaxY = viewInfo->gpu.visibilityBuffer->GetHeight();
            visibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
        }
        else {
            auto headPointers = context.viewManager->EnsureCLodDeepVisibilityHeadPointers(v);
            if (!headPointers) {
                viewRasterInfo[cameraIndex] = info;
                return;
            }

            info.opaqueVisibilitySRVDescriptorIndex = viewInfo->gpu.visibilityBuffer->GetSRVInfo(0).slot.index;
            info.deepVisibilityHeadPointerUAVDescriptorIndex = headPointers->GetUAVShaderVisibleInfo(0).slot.index;
            info.scissorMaxX = headPointers->GetWidth();
            info.scissorMaxY = headPointers->GetHeight();
            visibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
            deepVisibilityHeadPointerBuffers.push_back(std::move(headPointers));
        }

        info.viewportScaleX = static_cast<float>(info.scissorMaxX) / static_cast<float>(maxViewWidth);
        info.viewportScaleY = static_cast<float>(info.scissorMaxY) / static_cast<float>(maxViewHeight);
        viewRasterInfo[cameraIndex] = info;
    });

    m_passWidth = maxViewWidth;
    m_passHeight = maxViewHeight;
    if (m_outputKind == CLodRasterOutputKind::DeepVisibility) {
        const uint64_t maxNodes = totalViewPixels * kDeepVisibilityAverageFragmentsPerPixel;
        m_deepVisibilityNodeCapacity = std::max<uint32_t>(
            1u,
            static_cast<uint32_t>(std::min<uint64_t>(maxNodes, std::numeric_limits<uint32_t>::max())));
        if (m_deepVisibilityNodesBuffer) {
            m_deepVisibilityNodesBuffer->ResizeStructured(m_deepVisibilityNodeCapacity);
        }
    }
    else {
        m_deepVisibilityNodeCapacity = 1u;
    }

    const bool resourcesChanged =
        (m_visibilityBuffers != visibilityBuffers) ||
        (m_deepVisibilityHeadPointerBuffers != deepVisibilityHeadPointerBuffers);

    m_visibilityBuffers = std::move(visibilityBuffers);
    m_deepVisibilityHeadPointerBuffers = std::move(deepVisibilityHeadPointerBuffers);

    if (m_viewRasterInfos != viewRasterInfo || resourcesChanged) {
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
    misc[CLOD_RASTER_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_rasterBucketsHistogramBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX] = m_sortedToUnsortedMappingBuffer->GetSRVInfo(0).slot.index;
    if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
        misc[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX] = m_virtualShadowPageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_virtualShadowClipmapInfoBuffer->GetSRVInfo(0).slot.index;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX] = m_virtualShadowPhysicalPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_RESOLUTION] = CLodVirtualShadowDefaultPageTableResolution;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_COUNT] = CLodVirtualShadowDefaultClipmapCount;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_VIRTUAL_RESOLUTION] = CLodVirtualShadowDefaultVirtualResolution;
    }
    if (m_outputKind == CLodRasterOutputKind::DeepVisibility) {
        misc[CLOD_RASTER_DEEP_VISIBILITY_NODE_BUFFER_DESCRIPTOR_INDEX] = m_deepVisibilityNodesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_DEEP_VISIBILITY_NODE_COUNTER_DESCRIPTOR_INDEX] = m_deepVisibilityCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_DEEP_VISIBILITY_OVERFLOW_COUNTER_DESCRIPTOR_INDEX] = m_deepVisibilityOverflowCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_DEEP_VISIBILITY_NODE_CAPACITY] = m_deepVisibilityNodeCapacity;
    }
    commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);

    auto numBuckets = context.materialManager->GetRasterBucketCount();
    if (numBuckets == 0) {
        return {};
    }

    auto apiResource = m_rasterBucketsIndirectArgsBuffer->GetAPIResource();
    auto stride = sizeof(RasterizeClustersCommand);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto flags = context.materialManager->GetRasterFlagsForBucket(i);
        const auto& pso = (m_outputKind == CLodRasterOutputKind::VisibilityBuffer)
            ? psoManager.GetClusterLODRasterPSO(flags, m_wireframe)
            : (m_outputKind == CLodRasterOutputKind::VirtualShadow)
                ? psoManager.GetClusterLODVirtualShadowRasterPSO(flags, m_wireframe)
                : psoManager.GetClusterLODDeepVisibilityRasterPSO(flags, m_wireframe);

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
