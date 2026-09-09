#include "Render/GraphExtensions/ClusterLOD/ClusterRasterizationPass.h"

#include <algorithm>
#include <limits>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "RenderPasses/PreparedRenderIndirect.h"
#include "../shaders/PerPassRootConstants/clodRasterizationRootConstants.h"
#include "../shaders/PerPassRootConstants/visUtilRootConstants.h"

namespace {
constexpr uint32_t kDeepVisibilityAverageFragmentsPerPixel = 5u;
}

ClusterRasterizationPass::ClusterRasterizationPass(
    ClusterRasterizationPassInputs inputs,
    std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
    std::shared_ptr<Buffer> compactedVisibleClusterTransformIndicesBuffer,
    std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
    std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer,
    std::shared_ptr<Buffer> sortedToUnsortedMappingBuffer,
    std::shared_ptr<Buffer> deepVisibilityNodesBuffer,
    std::shared_ptr<Buffer> deepVisibilityCounterBuffer,
    std::shared_ptr<Buffer> deepVisibilityOverflowCounterBuffer,
    std::shared_ptr<Buffer> AVBOITConfigBuffer,
    std::shared_ptr<PixelBuffer> AVBOITOccupancyTexture,
    std::shared_ptr<PixelBuffer> AVBOITScalarExtinctionTexture,
    std::shared_ptr<PixelBuffer> AVBOITChromaticExtinctionTexture,
    std::shared_ptr<PixelBuffer> AVBOITIntegratedTransmittanceTexture,
    std::shared_ptr<PixelBuffer> AVBOITZeroTransmittanceSliceTexture,
    std::shared_ptr<PixelBuffer> AVBOITAccumulationTexture,
    std::shared_ptr<PixelBuffer> AVBOITNormalizationTexture,
    std::shared_ptr<PixelBuffer> AVBOITShadingExtinctionTexture,
    std::shared_ptr<Buffer> visibleClustersResolveBuffer,
    std::shared_ptr<ResourceGroup> slabResourceGroup,
    std::shared_ptr<PixelBuffer> virtualShadowPageTableTexture,
    std::shared_ptr<PixelBuffer> virtualShadowPhysicalPagesTexture,
    std::shared_ptr<Buffer> virtualShadowClipmapInfoBuffer,
    std::shared_ptr<PixelBuffer> AVBOITOccupancySliceMaskTexture,
    std::shared_ptr<PixelBuffer> AVBOITEarlyDepthTexture,
    std::shared_ptr<Buffer> telemetryBuffer,
    std::shared_ptr<Buffer> sourceGroupMismatchCounterBuffer,
    std::shared_ptr<Buffer> sourceGroupMismatchDetailsBuffer,
    std::shared_ptr<PixelBuffer> virtualShadowDynamicPagesTexture)
    : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer))
    , m_compactedVisibleClusterTransformIndicesBuffer(std::move(compactedVisibleClusterTransformIndicesBuffer))
    , m_rasterBucketsHistogramBuffer(std::move(rasterBucketsHistogramBuffer))
    , m_rasterBucketsIndirectArgsBuffer(std::move(rasterBucketsIndirectArgsBuffer))
    , m_sortedToUnsortedMappingBuffer(std::move(sortedToUnsortedMappingBuffer))
    , m_deepVisibilityNodesBuffer(std::move(deepVisibilityNodesBuffer))
    , m_deepVisibilityCounterBuffer(std::move(deepVisibilityCounterBuffer))
    , m_deepVisibilityOverflowCounterBuffer(std::move(deepVisibilityOverflowCounterBuffer))
    , m_AVBOITConfigBuffer(std::move(AVBOITConfigBuffer))
    , m_AVBOITOccupancyTexture(std::move(AVBOITOccupancyTexture))
    , m_AVBOITScalarExtinctionTexture(std::move(AVBOITScalarExtinctionTexture))
    , m_AVBOITChromaticExtinctionTexture(std::move(AVBOITChromaticExtinctionTexture))
    , m_AVBOITIntegratedTransmittanceTexture(std::move(AVBOITIntegratedTransmittanceTexture))
    , m_AVBOITZeroTransmittanceSliceTexture(std::move(AVBOITZeroTransmittanceSliceTexture))
    , m_AVBOITAccumulationTexture(std::move(AVBOITAccumulationTexture))
    , m_AVBOITNormalizationTexture(std::move(AVBOITNormalizationTexture))
    , m_AVBOITShadingExtinctionTexture(std::move(AVBOITShadingExtinctionTexture))
    , m_AVBOITEarlyDepthTexture(std::move(AVBOITEarlyDepthTexture))
    , m_AVBOITOccupancySliceMaskTexture(std::move(AVBOITOccupancySliceMaskTexture))
    , m_visibleClustersResolveBuffer(std::move(visibleClustersResolveBuffer))
    , m_virtualShadowPageTableTexture(std::move(virtualShadowPageTableTexture))
    , m_virtualShadowPhysicalPagesTexture(std::move(virtualShadowPhysicalPagesTexture))
    , m_virtualShadowDynamicPagesTexture(std::move(virtualShadowDynamicPagesTexture))
    , m_virtualShadowClipmapInfoBuffer(std::move(virtualShadowClipmapInfoBuffer))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_sourceGroupMismatchCounterBuffer(std::move(sourceGroupMismatchCounterBuffer))
    , m_sourceGroupMismatchDetailsBuffer(std::move(sourceGroupMismatchDetailsBuffer))
    , m_slabResourceGroup(std::move(slabResourceGroup)) {
    m_wireframe = inputs.wireframe;
    m_clearGbuffer = inputs.clearGbuffer;
    m_renderPhase = std::move(inputs.renderPhase);
    m_outputKind = inputs.outputKind;

    auto& settingsManager = SettingsManager::GetInstance();
    m_getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
    m_getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
    m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName("CLodViewRasterInfoBuffer");
    org::memory::SetResourceUsageHint(*m_viewRasterInfoBuffer, "Cluster LOD rasterization");

    rhi::IndirectArg args[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
        {.kind = rhi::IndirectArgKind::DispatchMesh }
    };
    auto device = DeviceManager::GetInstance().GetDevice();
    m_rasterizationCommandSignature = std::make_shared<rhi::CommandSignaturePtr>();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterizeClustersCommand) },
        PSOManager::GetInstance().GetRootSignature().GetHandle(),
        *m_rasterizationCommandSignature);
}

ClusterRasterizationPass::~ClusterRasterizationPass() = default;

void ClusterRasterizationPass::Declare(org::PassBuilder& declaration) {
    auto* builder = &declaration;
    builder->WithShaderResource(
            Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::InstanceDrawRecordBuffer,
            Builtin::PerInstanceTransformBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::PerMaterialOpenPBRDataBuffer,
            Builtin::Material::TextureStreamingMetadataBuffer,
            Builtin::SkeletonResources::InverseBindMatrices,
            Builtin::SkeletonResources::BoneTransforms,
            Builtin::SkeletonResources::SkinningInstanceInfo,
            Builtin::CameraBuffer,
            Builtin::CLod::Offsets,
            Builtin::CLod::GroupChunks,
            Builtin::CLod::Groups,
			Builtin::CLod::GroupPageMap,
            Builtin::CLod::Segments,
            Builtin::CLod::MeshMetadata,
            Builtin::CLod::AssemblyTransforms,
            Builtin::CLod::AssemblyBoneRemaps,
            Builtin::CLod::AssemblyBoneRemapIndices,
            m_compactedVisibleClustersBuffer,
            m_compactedVisibleClusterTransformIndicesBuffer,
            m_rasterBucketsHistogramBuffer,
            m_viewRasterInfoBuffer,
            m_sortedToUnsortedMappingBuffer)
        .WithUnorderedAccess(Builtin::Material::TextureStreamingFeedbackBuffer)
        .IsGeometryPass();
	m_indirectArgumentsBinding = builder->BindIndirectArguments(m_rasterBucketsIndirectArgsBuffer);

    if (m_telemetryBuffer) {
        builder->WithUnorderedAccess(m_telemetryBuffer);
    }
    if (m_sourceGroupMismatchCounterBuffer) {
        builder->WithUnorderedAccess(m_sourceGroupMismatchCounterBuffer);
    }
    if (m_sourceGroupMismatchDetailsBuffer) {
        builder->WithUnorderedAccess(m_sourceGroupMismatchDetailsBuffer);
    }

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
    else if (m_outputKind == CLodRasterOutputKind::AVBOITOccupancy) {
        for (auto& vb : m_visibilityBuffers) {
            builder->WithShaderResource(vb);
        }
        builder->WithShaderResource(m_AVBOITConfigBuffer, m_visibleClustersResolveBuffer)
            .WithUnorderedAccess(
                m_AVBOITOccupancyTexture,
                m_AVBOITOccupancySliceMaskTexture);
    }
    else if (m_outputKind == CLodRasterOutputKind::AVBOIT) {
        for (auto& vb : m_visibilityBuffers) {
            builder->WithShaderResource(vb);
        }
        builder->WithShaderResource(m_AVBOITConfigBuffer, m_visibleClustersResolveBuffer)
            .WithUnorderedAccess(
                m_AVBOITOccupancyTexture,
                m_AVBOITScalarExtinctionTexture,
                m_AVBOITChromaticExtinctionTexture);
    }
    else if (m_outputKind == CLodRasterOutputKind::AVBOITShading) {
        const bool shadowsEnabled = m_getShadowsEnabled ? m_getShadowsEnabled() : false;
        for (auto& vb : m_visibilityBuffers) {
            builder->WithShaderResource(vb);
        }
        builder->WithShaderResource(
                Builtin::Light::BufferGroup,
                Builtin::Environment::PrefilteredCubemapsGroup,
                Builtin::Environment::InfoBuffer,
                Builtin::Light::ActiveLightIndices,
                Builtin::Light::InfoBuffer,
                Builtin::Light::PointLightCubemapBuffer,
                Builtin::Light::SpotLightMatrixBuffer,
                Builtin::Light::DirectionalLightCascadeBuffer,
                Builtin::Light::ClusterBuffer,
                Builtin::Light::PagesBuffer,
                Builtin::OpenPBR::FuzzLTC,
                Builtin::OpenPBR::IdealMetalEnergyComplement,
                Builtin::OpenPBR::IdealMetalAverageEnergyComplement,
                Builtin::OpenPBR::OpaqueDielectricEnergyComplement,
                Builtin::OpenPBR::OpaqueDielectricAverageEnergyComplement,
                Builtin::Noise::BlueNoise2D,
                m_AVBOITConfigBuffer,
                m_AVBOITIntegratedTransmittanceTexture,
                m_AVBOITZeroTransmittanceSliceTexture,
                m_visibleClustersResolveBuffer)
            .WithRenderTarget(
                m_AVBOITAccumulationTexture,
                m_AVBOITNormalizationTexture,
                m_AVBOITShadingExtinctionTexture);
        if (shadowsEnabled) {
            builder->WithShaderResource(
                Builtin::Shadows::CLodClipmapInfo,
                Builtin::Shadows::CLodDirectionalPageViewInfo,
                Builtin::Shadows::CLodPageMetadata,
                Builtin::Shadows::CLodPageTable,
                Builtin::Shadows::CLodPhysicalPages,
                Builtin::Shadows::CLodCompactMainCamera,
                Builtin::Shadows::CLodCompactShadowCameras);
        }
        if (m_AVBOITEarlyDepthTexture) {
            builder->WithDepthRead(m_AVBOITEarlyDepthTexture);
        }
    }
    else if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
        builder->WithShaderResource(
                m_virtualShadowClipmapInfoBuffer,
                Builtin::Shadows::CLodDirectionalPageViewInfo)
            .WithUnorderedAccess(
                m_virtualShadowPageTableTexture,
                m_virtualShadowPhysicalPagesTexture,
                m_virtualShadowDynamicPagesTexture,
                Builtin::Shadows::CLodStats);
    }

    // Declare page pool slabs for bindless access (auto-invalidates when new slabs are added).
    if (m_slabResourceGroup) {
        builder->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
    }

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void ClusterRasterizationPass::Initialize() {
    if (m_outputKind == CLodRasterOutputKind::AVBOITShading) {
        RegisterSRV(SRVViewType::Texture2DArrayFull, Builtin::OpenPBR::OpaqueDielectricEnergyComplement);
    }
    if (m_outputKind == CLodRasterOutputKind::AVBOITShading && m_getShadowsEnabled && m_getShadowsEnabled()) {
        RegisterSRV(SRVViewType::Texture2DArrayFull, Builtin::Shadows::CLodPageTable);
    }
}

void ClusterRasterizationPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    const CLodVirtualShadowResolutionConfig virtualShadowConfig = CLodVirtualShadowBuildRuntimeResolutionConfig();

    auto numViews = context.viewManager->GetCameraBufferSize();
    std::vector<std::shared_ptr<PixelBuffer>> visibilityBuffers;
    std::vector<std::shared_ptr<PixelBuffer>> deepVisibilityHeadPointerBuffers;

    uint32_t maxViewWidth = 1;
    uint32_t maxViewHeight = 1;
    uint64_t totalViewPixels = 0;
    const bool lowResolutionAVBOITRaster =
        m_outputKind == CLodRasterOutputKind::AVBOITOccupancy ||
        m_outputKind == CLodRasterOutputKind::AVBOIT;
    const auto rasterDimension = [&](uint32_t dimension) {
        return lowResolutionAVBOITRaster
            ? (dimension + CLodAVBOITDefaultDownsampleFactor - 1u) / CLodAVBOITDefaultDownsampleFactor
            : dimension;
    };

    if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
        maxViewWidth = virtualShadowConfig.virtualResolution;
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
        else if (m_outputKind == CLodRasterOutputKind::DeepVisibility) {
            auto headPointers = context.viewManager->EnsureCLodDeepVisibilityHeadPointers(v);
            if (!headPointers) {
                return;
            }

            maxViewWidth = std::max(maxViewWidth, headPointers->GetWidth());
            maxViewHeight = std::max(maxViewHeight, headPointers->GetHeight());
            totalViewPixels += static_cast<uint64_t>(headPointers->GetWidth()) *
                static_cast<uint64_t>(headPointers->GetHeight());
        }
        else {
            maxViewWidth = std::max(maxViewWidth, rasterDimension(viewInfo->gpu.visibilityBuffer->GetWidth()));
            maxViewHeight = std::max(maxViewHeight, rasterDimension(viewInfo->gpu.visibilityBuffer->GetHeight()));
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
        else if (m_outputKind == CLodRasterOutputKind::DeepVisibility) {
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
        else {
            info.opaqueVisibilitySRVDescriptorIndex = viewInfo->gpu.visibilityBuffer->GetSRVInfo(0).slot.index;
            info.scissorMaxX = rasterDimension(viewInfo->gpu.visibilityBuffer->GetWidth());
            info.scissorMaxY = rasterDimension(viewInfo->gpu.visibilityBuffer->GetHeight());
            visibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
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
            org::runtime::UploadTarget::FromShared(m_viewRasterInfoBuffer),
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

br::render::PreparedRenderIndirectSequence ClusterRasterizationPass::Prepare(const org::PassPrepareContext& preparation) {
    if (m_outputKind == CLodRasterOutputKind::VisibilityBuffer &&
        SettingsManager::GetInstance().getSettingGetter<bool>(CLodDisableNonVoxelVisibilitySettingName)())
        return {};
    const auto* context = preparation.preparationData
        ? preparation.preparationData->Get<UpdateContext>() : nullptr;
    if (!context) throw std::logic_error("CLod raster preparation requires the owned render snapshot");
    br::render::PreparedRenderIndirectSequence data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.commandSignature = preparation.CaptureCommandSignature(m_rasterizationCommandSignature);
	data.arguments = preparation.CaptureResource(m_indirectArgumentsBinding);
    data.width = m_passWidth; data.height = m_passHeight; data.debugName = "CLod raster pass";
    if (m_outputKind == CLodRasterOutputKind::AVBOITShading && m_AVBOITAccumulationTexture
        && m_AVBOITNormalizationTexture && m_AVBOITShadingExtinctionTexture) {
        const std::shared_ptr<PixelBuffer> targets[] = {m_AVBOITAccumulationTexture, m_AVBOITNormalizationTexture, m_AVBOITShadingExtinctionTexture};
        data.colorCount = 3;
        for (uint32_t i = 0; i < 3; ++i) {
            data.colors[i].rtv = targets[i]->GetRTVInfo(0).slot; data.colors[i].loadOp = rhi::LoadOp::Load;
            data.colors[i].storeOp = rhi::StoreOp::Store; data.colors[i].clear = targets[i]->GetClearColor();
        }
        if (m_AVBOITEarlyDepthTexture) {
            data.hasDepth = true; data.depth.dsv = m_AVBOITEarlyDepthTexture->GetDSVInfo(0).slot;
            data.depth.depthLoad = rhi::LoadOp::Load; data.depth.depthStore = rhi::StoreOp::Store;
            data.depth.stencilLoad = rhi::LoadOp::DontCare; data.depth.stencilStore = rhi::StoreOp::DontCare;
            data.depth.clear = m_AVBOITEarlyDepthTexture->GetClearColor(); data.depth.readOnly = true;
        }
    }
    auto& misc = data.constants;
    if (m_outputKind == CLodRasterOutputKind::AVBOITShading) {
        misc[MiscEnableShadows] = m_getShadowsEnabled ? m_getShadowsEnabled() : 0u;
        misc[MiscEnablePunctualLights] = m_getPunctualLightingEnabled ? m_getPunctualLightingEnabled() : 0u;
        misc[MiscEnableGTAO] = m_gtaoEnabled;
    }
    misc[CLOD_RASTER_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_rasterBucketsHistogramBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTER_TRANSFORM_INDICES_DESCRIPTOR_INDEX] = m_compactedVisibleClusterTransformIndicesBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX] = m_sortedToUnsortedMappingBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_TELEMETRY_DESCRIPTOR_INDEX] = 0xFFFFFFFFu;
    misc[CLOD_RASTER_SINGLE_VIEW_VISIBILITY_UAV_DESCRIPTOR_INDEX] = 0xFFFFFFFFu;
    misc[CLOD_RASTER_SOURCE_GROUP_MISMATCH_COUNTER_DESCRIPTOR_INDEX] = 0xFFFFFFFFu;
    misc[CLOD_RASTER_SOURCE_GROUP_MISMATCH_DETAILS_DESCRIPTOR_INDEX] = 0xFFFFFFFFu;
    if (m_outputKind == CLodRasterOutputKind::VisibilityBuffer && m_visibilityBuffers.size() == 1u)
        misc[CLOD_RASTER_SINGLE_VIEW_VISIBILITY_UAV_DESCRIPTOR_INDEX] = m_visibilityBuffers.front()->GetUAVShaderVisibleInfo(0).slot.index;
    if (m_telemetryBuffer && IsCLodWorkGraphTelemetryEnabled()) misc[CLOD_RASTER_TELEMETRY_DESCRIPTOR_INDEX] = m_telemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    if (m_sourceGroupMismatchCounterBuffer && m_sourceGroupMismatchDetailsBuffer) {
        misc[CLOD_RASTER_SOURCE_GROUP_MISMATCH_COUNTER_DESCRIPTOR_INDEX] = m_sourceGroupMismatchCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_SOURCE_GROUP_MISMATCH_DETAILS_DESCRIPTOR_INDEX] = m_sourceGroupMismatchDetailsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    }
    if (m_outputKind == CLodRasterOutputKind::VirtualShadow) {
        const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
        misc[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX] = m_virtualShadowPageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_virtualShadowClipmapInfoBuffer->GetSRVInfo(0).slot.index;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX] = m_virtualShadowPhysicalPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_DYNAMIC_PAGES_DESCRIPTOR_INDEX] = m_virtualShadowDynamicPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
        misc[CLOD_RASTER_VIRTUAL_SHADOW_VIRTUAL_RESOLUTION] = config.virtualResolution;
    }
    if (m_outputKind == CLodRasterOutputKind::DeepVisibility) {
        misc[CLOD_RASTER_DEEP_VISIBILITY_NODE_BUFFER_DESCRIPTOR_INDEX] = m_deepVisibilityNodesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_DEEP_VISIBILITY_NODE_COUNTER_DESCRIPTOR_INDEX] = m_deepVisibilityCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_DEEP_VISIBILITY_OVERFLOW_COUNTER_DESCRIPTOR_INDEX] = m_deepVisibilityOverflowCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_RASTER_DEEP_VISIBILITY_NODE_CAPACITY] = m_deepVisibilityNodeCapacity;
    }
    if (m_outputKind == CLodRasterOutputKind::AVBOITOccupancy ||
        m_outputKind == CLodRasterOutputKind::AVBOIT ||
        m_outputKind == CLodRasterOutputKind::AVBOITShading) {
        misc[CLOD_RASTER_AVBOIT_VBOIT_CONFIG_DESCRIPTOR_INDEX] = m_AVBOITConfigBuffer->GetSRVInfo(0).slot.index;
        misc[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersResolveBuffer
            ? m_visibleClustersResolveBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
        misc[VISBUF_REYES_DICE_QUEUE_DESCRIPTOR_INDEX] = 0xFFFFFFFFu;
    }
    const auto numBuckets = context->preparedRasterBucketCount;
    BT_PLOT("CLod.RasterArgs.PreparedBucketCount", static_cast<int64_t>(numBuckets));
    BT_PLOT("CLod.RasterArgs.PreparedBackingBytes", static_cast<int64_t>(m_rasterBucketsIndirectArgsBuffer->GetSize()));
    data.steps.reserve(numBuckets);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        const auto flags = context->preparedRasterBucketFlags.at(i);
        const PipelineState* pso = m_outputKind == CLodRasterOutputKind::VisibilityBuffer
            ? PSOManager::GetInstance().TryGetClusterLODRasterPSO(flags, m_wireframe, m_visibilityBuffers.size() == 1u)
            : m_outputKind == CLodRasterOutputKind::VirtualShadow
                ? PSOManager::GetInstance().TryGetClusterLODVirtualShadowRasterPSO(flags, m_wireframe)
            : m_outputKind == CLodRasterOutputKind::AVBOITOccupancy
                ? PSOManager::GetInstance().TryGetClusterLODAVBOITOccupancyPSO(flags, m_wireframe)
            : m_outputKind == CLodRasterOutputKind::AVBOIT
                ? PSOManager::GetInstance().TryGetClusterLODAVBOITRasterPSO(flags, m_wireframe)
            : m_outputKind == CLodRasterOutputKind::AVBOITShading
                ? PSOManager::GetInstance().TryGetClusterLODAVBOITShadePSO(flags, m_wireframe, context->globalPSOFlags)
            : PSOManager::GetInstance().TryGetClusterLODDeepVisibilityRasterPSO(flags, m_wireframe);
        if (!pso) continue;
        br::render::PreparedRenderIndirectSequence::Step step{};
        step.program = preparation.CaptureProgramBinding(*pso);
        step.argumentsOffset = static_cast<uint64_t>(i) * sizeof(RasterizeClustersCommand);
        data.steps.push_back(std::move(step));
    }
    return data;
}
