#include "Render/GraphExtensions/CLodExtension.h"

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "Managers/MeshManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/GraphExtensions/ClusterLOD/ClearDeepVisibilityPass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingSystem.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITAdaptiveFitPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITAdaptiveFitUpdatePass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITDepthWarpPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITEarlyDepthBuildPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITEarlyDepthPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITOccupancyHistogramPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITOccupancyRemapPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITSetupPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITSparseClearPass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITIntegratePass.h"
#include "Render/GraphExtensions/ClusterLOD/AVBOITResolvePass.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterPageJobBuildArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterPageJobExpandPass.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterPageJobRasterPass.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/DeepVisibilityResolvePass.h"
#include "Render/GraphExtensions/ClusterLOD/HierarchicalDispatchCullingPass.h"
#include "Render/GraphExtensions/ClusterLOD/HierarchicalCullingPass.h"
#include "Render/GraphExtensions/ClusterLOD/PerViewLinearDepthCopyPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockOffsetsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockScanPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCompactAndArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCreateCommandPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketHistogramPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesClassifyPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesBuildRasterWorkPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesRasterWorkCompactAndArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesRasterWorkHistogramPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesDeepVisibilityRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesCopyCounterPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesCreateDispatchArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesDicePass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesPatchRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesVirtualShadowRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesVirtualShadowHardwareRasterPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesQueueResetPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesSeedPatchesPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesSplitPass.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationTable.h"
#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationTableUploadPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapAllocatePagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowBlockExpandPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowBuildRasterArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapBuildPageListsPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapBuildMarkTilesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearDirtyBitsPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapConsumePredictedPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapFreeWrappedPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapGatherStatsPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapDirtyHierarchyPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapNonRasterableHierarchyPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapDeduplicatePredictedPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapExpandPredictedPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapInvalidatePagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapMarkPagesPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapResolveMarkedBlocksPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapSetupPass.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "Render/RenderPhase.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Buffers/PagePool.h"
#include "Resources/components.h"
#include "ShaderBuffers.h"
#include "Utilities/ProportionalBudgetAllocator.h"
#include "BuiltinResources.h"

namespace {

bool AreRendererShadowsEnabled()
{
    return SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
}

flecs::entity EnsureVisibilityBufferTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionVisibilityBufferTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionVisibilityBufferTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionVisibilityBufferTag>();
    }
    return ecsWorld.entity<CLodExtensionVisibilityBufferTag>();
}

flecs::entity EnsureAlphaBlendTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionAlphaBlendTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionAlphaBlendTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionAlphaBlendTag>();
    }
    return ecsWorld.entity<CLodExtensionAlphaBlendTag>();
}

flecs::entity EnsureShadowTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionShadowTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionShadowTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionShadowTag>();
    }
    return ecsWorld.entity<CLodExtensionShadowTag>();
}

struct CLodVariantTraits {
    enum class ScheduleMode : uint8_t {
        TwoPassVisibility,
        SinglePassCullOnly,
        SinglePassDeepVisibility
    };

    CLodExtensionType type;
    flecs::entity (*ensureTypeEntity)(flecs::world&);
    std::string_view passPrefix;
    std::string_view resourcePrefix;
    std::string_view renderPhaseName;
    CLodRasterOutputKind rasterOutputKind;
    ScheduleMode scheduleMode = ScheduleMode::TwoPassVisibility;
    bool ownsStreaming = false;
    bool usesPhase2OcclusionReplay = false;
    bool schedulesPerViewDepthCopy = false;
    bool schedulesStructuralPasses = false;
};

const CLodVariantTraits& GetVariantTraits(CLodExtensionType type)
{
    static const std::array<CLodVariantTraits, 3> kTraits = {{
        {
            CLodExtensionType::VisiblityBuffer,
            &EnsureVisibilityBufferTag,
            "CLodOpaque::",
            "CLod[Opaque] ",
            Engine::Primary::GBufferPass,
            CLodRasterOutputKind::VisibilityBuffer,
            CLodVariantTraits::ScheduleMode::TwoPassVisibility,
            true,
            true,
            true,
            true
        },
        {
            CLodExtensionType::AlphaBlend,
            &EnsureAlphaBlendTag,
            "CLodAlpha::",
            "CLod[Alpha] ",
            Engine::Primary::CLodTransparentPass,
            CLodRasterOutputKind::DeepVisibility,
            CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility,
            false,
            false,
            false,
            true
        },
        {
            CLodExtensionType::Shadow,
            &EnsureShadowTag,
            "CLodShadow::",
            "CLod[Shadow] ",
            Engine::Primary::ShadowMapsPass,
            CLodRasterOutputKind::VirtualShadow,
            CLodVariantTraits::ScheduleMode::TwoPassVisibility,
            false,
            false,
            false,
            true
        },
    }};

    for (const auto& traits : kTraits) {
        if (traits.type == type) {
            return traits;
        }
    }

    throw std::runtime_error("Unknown CLod extension type.");
}

std::string MakeVariantPassName(const CLodVariantTraits& traits, std::string_view suffix)
{
    return std::string(traits.passPrefix) + std::string(suffix);
}

std::string MakeVariantResourceName(const CLodVariantTraits& traits, std::string_view suffix)
{
    return std::string(traits.resourcePrefix) + std::string(suffix);
}

std::string GetVariantTechniquePath(const CLodVariantTraits& traits, std::string_view passName)
{
    switch (traits.type) {
    case CLodExtensionType::VisiblityBuffer:
        if (passName.find("Reyes") != std::string_view::npos) {
            return "Primary Visibility::CLod::Reyes";
        }
        return "Primary Visibility::CLod";
    case CLodExtensionType::AlphaBlend:
        if (passName.find("Reyes") != std::string_view::npos) {
            return "Transparency::Deep Visibility::Reyes";
        }
        if (passName.find("TransparentVBOIT") != std::string_view::npos ||
            passName.find("TransparentExtinction") != std::string_view::npos) {
            return "Transparency::VBOIT";
        }
        if (passName.find("DeepVisibility") != std::string_view::npos ||
            passName.find("ClearDeepVisibility") != std::string_view::npos ||
            passName.find("RasterizeClustersPass") != std::string_view::npos) {
            return "Transparency::Deep Visibility";
        }
        return "Transparency::CLod";
    case CLodExtensionType::Shadow:
        if (passName.find("Reyes") != std::string_view::npos) {
            return "Shadows::Virtual Shadow Mapping::Reyes";
        }
        if (passName.find("PageJob") != std::string_view::npos ||
            passName.find("VirtualShadowBlock") != std::string_view::npos) {
            return "Shadows::Virtual Shadow Mapping::Page Job";
        }
        return "Shadows::Virtual Shadow Mapping";
    default:
        return {};
    }
}

constexpr std::string_view kTransparentExtinctionSetupPassName = "TransparentExtinctionSetupPass";
constexpr std::string_view kTransparentExtinctionOccupancyPassName = "TransparentExtinctionOccupancyPass";
constexpr std::string_view kTransparentExtinctionOccupancyHistogramPassName = "TransparentExtinctionOccupancyHistogramPass";
constexpr std::string_view kTransparentExtinctionAdaptiveFitPassName = "TransparentExtinctionAdaptiveFitPass";
constexpr std::string_view kTransparentExtinctionAdaptiveFitUpdatePassName = "TransparentExtinctionAdaptiveFitUpdatePass";
constexpr std::string_view kTransparentExtinctionDepthWarpPassName = "TransparentExtinctionDepthWarpPass";
constexpr std::string_view kTransparentExtinctionOccupancyRemapPassName = "TransparentExtinctionOccupancyRemapPass";
constexpr std::string_view kTransparentExtinctionSparseClearPassName = "TransparentExtinctionSparseClearPass";
constexpr std::string_view kTransparentExtinctionCapturePassName = "TransparentExtinctionCapturePass";
constexpr std::string_view kTransparentVBOITEarlyDepthBuildPassName = "TransparentVBOITEarlyDepthBuildPass";
constexpr std::string_view kTransparentVBOITEarlyDepthPassName = "TransparentVBOITEarlyDepthPass";
constexpr std::string_view kTransparentTransmittanceIntegratePassName = "TransparentTransmittanceIntegratePass";
constexpr std::string_view kTransparentVBOITShadePassName = "TransparentVBOITShadePass";
constexpr std::string_view kTransparentVBOITResolvePassName = "TransparentVBOITResolvePass";

DirectX::XMUINT2 GetAVBOITLowResolution(const DirectX::XMUINT2& renderResolution)
{
    const uint32_t downsampleFactor = (std::max)(1u, CLodAVBOITDefaultDownsampleFactor);
    return {
        (std::max)(1u, (renderResolution.x + downsampleFactor - 1u) / downsampleFactor),
        (std::max)(1u, (renderResolution.y + downsampleFactor - 1u) / downsampleFactor),
    };
}

TextureDescription CreateAVBOITOccupancyDescription()
{
    TextureDescription desc;
    const auto renderResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    const auto lowResolution = GetAVBOITLowResolution(renderResolution);

    desc.channels = 1;
    desc.format = rhi::Format::R32_Float;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_Float;
    desc.hasUAV = true;
    desc.uavFormat = rhi::Format::R32_Float;
    desc.hasNonShaderVisibleUAV = true;
    desc.allowAlias = true;
    desc.imageDimensions.push_back(ImageDimensions{ lowResolution.x, lowResolution.y, 0, 0 });
    return desc;
}

TextureDescription CreateAVBOITSliceVolumeDescription()
{
    TextureDescription desc = CreateAVBOITOccupancyDescription();
    desc.isArray = true;
    desc.arraySize = CLodAVBOITDefaultSliceCount;
    return desc;
}

TextureDescription CreateAVBOITExtinctionDescription()
{
    TextureDescription desc = CreateAVBOITSliceVolumeDescription();
    desc.format = rhi::Format::R32_UInt;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.uavFormat = rhi::Format::R32_UInt;
    return desc;
}

TextureDescription CreateAVBOITChromaticExtinctionDescription()
{
    TextureDescription desc = CreateAVBOITSliceVolumeDescription();
    desc.channels = 1;
    desc.arraySize = CLodAVBOITDefaultSliceCount * 3u;
    desc.format = rhi::Format::R32_UInt;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.uavFormat = rhi::Format::R32_UInt;
    return desc;
}

TextureDescription CreateAVBOITIntegratedTransmittanceDescription()
{
    TextureDescription desc = CreateAVBOITSliceVolumeDescription();
    desc.channels = 4;
    desc.format = rhi::Format::R16G16B16A16_Float;
    desc.srvFormat = rhi::Format::R16G16B16A16_Float;
    desc.uavFormat = rhi::Format::R16G16B16A16_Float;
    return desc;
}

TextureDescription CreateAVBOITZeroTransmittanceSliceDescription()
{
    TextureDescription desc = CreateAVBOITOccupancyDescription();
    desc.format = rhi::Format::R32_UInt;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.uavFormat = rhi::Format::R32_UInt;
    return desc;
}

TextureDescription CreateAVBOITOccupancySliceMaskDescription()
{
    return CreateAVBOITZeroTransmittanceSliceDescription();
}

TextureDescription CreateAVBOITAccumulationDescription()
{
    TextureDescription desc;
    const auto renderResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    desc.channels = 4;
    desc.format = rhi::Format::R16G16B16A16_Float;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R16G16B16A16_Float;
    desc.hasRTV = true;
    desc.rtvFormat = rhi::Format::R16G16B16A16_Float;
    desc.clearColor[0] = 0.0f;
    desc.clearColor[1] = 0.0f;
    desc.clearColor[2] = 0.0f;
    desc.clearColor[3] = 0.0f;
    desc.allowAlias = true;
    desc.imageDimensions.push_back(ImageDimensions{ renderResolution.x, renderResolution.y, 0, 0 });
    return desc;
}

TextureDescription CreateAVBOITNormalizationDescription()
{
    return CreateAVBOITAccumulationDescription();
}

TextureDescription CreateAVBOITShadingExtinctionDescription()
{
    return CreateAVBOITAccumulationDescription();
}

TextureDescription CreateAVBOITEarlyDepthDescription()
{
    TextureDescription desc;
    const auto renderResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    desc.channels = 1;
    desc.format = rhi::Format::R32_Typeless;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_Float;
    desc.hasDSV = true;
    desc.dsvFormat = rhi::Format::D32_Float;
    desc.clearColor[0] = 1.0f;
    desc.clearColor[1] = 0.0f;
    desc.clearColor[2] = 0.0f;
    desc.clearColor[3] = 0.0f;
    desc.allowAlias = true;
    desc.imageDimensions.push_back(ImageDimensions{ renderResolution.x, renderResolution.y, 0, 0 });
    return desc;
}

CLodTransparencyMode GetTransparencyMode(CLodExtensionType type)
{
    if (type != CLodExtensionType::AlphaBlend) {
        return CLodTransparencyMode::LinkedListDeepVisibility;
    }

    return SettingsManager::GetInstance().getSettingGetter<CLodTransparencyMode>(CLodTransparencyModeSettingName)();
}

TextureDescription CreateVirtualShadowPageTableDescription()
{
    TextureDescription desc;
    ImageDimensions dims;
    dims.width = CLodVirtualShadowMaxPageTableResolution;
    dims.height = CLodVirtualShadowMaxPageTableResolution;
    dims.rowPitch = static_cast<uint64_t>(dims.width) * sizeof(uint32_t);
    dims.slicePitch = dims.rowPitch * static_cast<uint64_t>(dims.height);
    desc.imageDimensions.push_back(dims);
    desc.channels = 1;
    desc.format = rhi::Format::R32_UInt;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.hasUAV = true;
    desc.uavFormat = rhi::Format::R32_UInt;
    desc.isArray = true;
    desc.arraySize = CLodVirtualShadowMaxSupportedClipmapCount;
    return desc;
}

TextureDescription CreateVirtualShadowPhysicalPagesDescription(uint32_t backingResolution)
{
    TextureDescription desc;
    ImageDimensions dims;
    const uint32_t sanitizedBackingResolution = CLodVirtualShadowSanitizeBackingResolution(backingResolution);
    dims.width = sanitizedBackingResolution;
    dims.height = sanitizedBackingResolution;
    dims.rowPitch = static_cast<uint64_t>(dims.width) * sizeof(uint32_t);
    dims.slicePitch = dims.rowPitch * static_cast<uint64_t>(dims.height);
    desc.imageDimensions.push_back(dims);
    desc.channels = 1;
    desc.format = rhi::Format::R32_UInt;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.hasUAV = true;
    desc.uavFormat = rhi::Format::R32_UInt;
    return desc;
}

TextureDescription CreateVirtualShadowDirtyHierarchyDescription()
{
    TextureDescription desc;
    ImageDimensions dims;
    dims.width = CLodVirtualShadowMaxPageTableResolution;
    dims.height = CLodVirtualShadowMaxPageTableResolution;
    dims.rowPitch = static_cast<uint64_t>(dims.width) * sizeof(uint32_t);
    dims.slicePitch = dims.rowPitch * static_cast<uint64_t>(dims.height);
    desc.imageDimensions.push_back(dims);
    desc.channels = 1;
    desc.format = rhi::Format::R32_UInt;
    desc.hasSRV = true;
    desc.srvFormat = rhi::Format::R32_UInt;
    desc.hasUAV = true;
    desc.uavFormat = rhi::Format::R32_UInt;
    desc.isArray = true;
    desc.arraySize = CLodVirtualShadowMaxSupportedClipmapCount;
    desc.generateMipMaps = true;
    return desc;
}

HierarchicalCullingWorkGraphMode GetCullingWorkGraphMode(CLodSoftwareRasterMode softwareRasterMode)
{
    switch (softwareRasterMode) {
    case CLodSoftwareRasterMode::Disabled:
        return HierarchicalCullingWorkGraphMode::HardwareOnly;
    case CLodSoftwareRasterMode::Compute:
        return HierarchicalCullingWorkGraphMode::SoftwareRasterCompute;
    case CLodSoftwareRasterMode::WorkGraph:
        return HierarchicalCullingWorkGraphMode::SoftwareRasterWorkGraph;
    }

    return HierarchicalCullingWorkGraphMode::HardwareOnly;
}

HierarchicalCullingBackend GetHierarchicalCullingBackend(CLodCullingBackend backend)
{
    switch (backend) {
    case CLodCullingBackend::PureCompute:
        return HierarchicalCullingBackend::PureCompute;
    case CLodCullingBackend::WorkGraph:
    default:
        return HierarchicalCullingBackend::WorkGraph;
    }
}

bool UseHierarchicalDispatchCullingPass(
    HierarchicalCullingBackend backend,
    bool isFirstPass,
    HierarchicalCullingWorkGraphMode workGraphMode,
    CLodRasterOutputKind rasterOutputKind)
{
    return backend == HierarchicalCullingBackend::PureCompute
        && isFirstPass
        && workGraphMode == HierarchicalCullingWorkGraphMode::HardwareOnly
        && rasterOutputKind == CLodRasterOutputKind::VisibilityBuffer;
}

std::shared_ptr<ResourceGroup> GetSlabResourceGroup()
{
    try {
        auto getter = SettingsManager::GetInstance().getSettingGetter<std::function<MeshManager*()>>(CLodStreamingMeshManagerGetterSettingName);
        if (auto* meshManager = getter()()) {
            if (auto* pool = meshManager->GetCLodPagePool()) {
                return pool->GetSlabResourceGroup();
            }
        }
    } catch (...) {}

    return nullptr;
}

template<typename... Tags>
void SyncTaggedBufferEntity(const std::shared_ptr<Buffer>& buffer, const flecs::entity& typeEntity, bool enabled)
{
    if (!buffer) {
        return;
    }

    auto entity = buffer->GetECSEntity();
    if (enabled) {
        entity.set<Components::Resource>({ buffer });
        entity.add<CLodExtensionTypeTag>(typeEntity);
        (entity.add<Tags>(), ...);
    }
    else if (entity.has<Components::Resource>()) {
        entity.remove<Components::Resource>();
    }
}

struct ReyesResourceSizing {
    uint32_t fullClusterOutputCapacity = 0u;
    uint32_t ownedClusterCapacity = 0u;
    uint32_t splitQueueCapacity = 0u;
    uint32_t diceQueueCapacity = 0u;
    uint32_t diceQueuePhysicalCapacity = 0u;
    uint32_t rasterWorkCapacity = 0u;
    uint32_t ownershipBitsetWordCount = 0u;
    uint64_t requestedBudgetBytes = 0u;
    uint64_t allocatedBudgetBytes = 0u;
    bool budgetLimited = false;
};

uint64_t BufferBytes(uint32_t elementCount, size_t elementSize)
{
    return static_cast<uint64_t>(elementCount) * static_cast<uint64_t>(elementSize);
}

uint32_t BytesToElementCount(uint64_t allocatedBytes, size_t elementSize)
{
    if (elementSize == 0u) {
        throw std::runtime_error("Element size must be non-zero when converting budgeted bytes to element count.");
    }

    return static_cast<uint32_t>(allocatedBytes / static_cast<uint64_t>(elementSize));
}

ReyesResourceSizing BuildReyesResourceSizing(const CLodVariantTraits& traits, uint32_t maxVisibleClusters)
{
    const bool usesPhase2ReyesResources = traits.usesPhase2OcclusionReplay;
    const uint32_t ownershipBitsetWordCount = CLodBitsetWordCount(maxVisibleClusters);
    const uint32_t idealFullClusterOutputCapacity = maxVisibleClusters;
    const uint32_t idealOwnedClusterCapacity = maxVisibleClusters;
    const uint32_t idealSplitQueueCapacity = CLodReyesSplitQueueCapacity(maxVisibleClusters);
    const uint32_t idealDiceQueueCapacity = CLodReyesDiceQueueCapacity(maxVisibleClusters);
    const uint32_t idealRasterWorkCapacity = CLodReyesRasterWorkCapacity(maxVisibleClusters);
    const uint32_t diceQueuePhaseMultiplier = usesPhase2ReyesResources ? 2u : 1u;
    const ReyesTessellationTableData& reyesTessellationTableData = GetReyesTessellationTableData();

    std::vector<budget::ProportionalBudgetItem> budgetItems;
    budgetItems.reserve(24);

    auto addFixedItem = [&budgetItems](std::string_view id, uint64_t bytes) {
        budgetItems.push_back(budget::ProportionalBudgetItem{
            .id = id,
            .idealBytes = bytes,
            .minBytes = bytes,
            .maxBytes = bytes,
            .quantumBytes = 1u,
        });
    };

    auto addFlexibleItem = [&budgetItems](std::string_view id, uint64_t idealBytes, uint64_t quantumBytes) {
        budgetItems.push_back(budget::ProportionalBudgetItem{
            .id = id,
            .idealBytes = idealBytes,
            .minBytes = quantumBytes,
            .maxBytes = idealBytes,
            .quantumBytes = quantumBytes,
        });
    };

    addFlexibleItem("fullClusterOutputs", BufferBytes(idealFullClusterOutputCapacity, sizeof(CLodReyesFullClusterOutput)), sizeof(CLodReyesFullClusterOutput));
    addFixedItem("fullClusterOutputCounter", sizeof(uint32_t));
    addFlexibleItem("ownedClusters", BufferBytes(idealOwnedClusterCapacity, sizeof(CLodReyesOwnedClusterEntry)), sizeof(CLodReyesOwnedClusterEntry));
    addFixedItem("ownedClusterCounter", sizeof(uint32_t));
    addFixedItem("ownershipBitset", BufferBytes(ownershipBitsetWordCount, sizeof(uint32_t)));
    if (usesPhase2ReyesResources) {
        addFixedItem("ownershipBitsetPhase2", BufferBytes(ownershipBitsetWordCount, sizeof(uint32_t)));
    }

    addFixedItem("classifyIndirectArgs", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFixedItem("classifyIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }

    addFixedItem("splitIndirectArgs", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFixedItem("splitIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }

    addFlexibleItem("splitQueueA", BufferBytes(idealSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry)), sizeof(CLodReyesSplitQueueEntry));
    addFixedItem("splitQueueCounterA", sizeof(uint32_t));
    addFixedItem("splitQueueOverflowA", sizeof(uint32_t));
    addFlexibleItem("splitQueueB", BufferBytes(idealSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry)), sizeof(CLodReyesSplitQueueEntry));
    addFixedItem("splitQueueCounterB", sizeof(uint32_t));
    addFixedItem("splitQueueOverflowB", sizeof(uint32_t));

    addFlexibleItem(
        "diceQueue",
        BufferBytes(idealDiceQueueCapacity * diceQueuePhaseMultiplier, sizeof(CLodReyesDiceQueueEntry)),
        static_cast<uint64_t>(sizeof(CLodReyesDiceQueueEntry)) * static_cast<uint64_t>(diceQueuePhaseMultiplier));
    addFixedItem("diceQueueCounter", sizeof(uint32_t));
    if (usesPhase2ReyesResources) {
        addFixedItem("diceQueuePhase1Count", sizeof(uint32_t));
    }
    addFixedItem("diceQueueOverflow", sizeof(uint32_t));

    addFlexibleItem("rasterWorkPhase1", BufferBytes(idealRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry)), sizeof(CLodReyesRasterWorkEntry));
    addFixedItem("rasterWorkCounterPhase1", sizeof(uint32_t));
    addFixedItem("rasterWorkIndirectArgsPhase1", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFlexibleItem("rasterWorkPhase2", BufferBytes(idealRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry)), sizeof(CLodReyesRasterWorkEntry));
        addFixedItem("rasterWorkCounterPhase2", sizeof(uint32_t));
        addFixedItem("rasterWorkIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }

    addFixedItem("tessTableConfigs", BufferBytes(static_cast<uint32_t>(reyesTessellationTableData.configs.size()), sizeof(CLodReyesTessTableConfigEntry)));
    addFixedItem("tessTableVertices", BufferBytes(static_cast<uint32_t>(reyesTessellationTableData.vertices.size()), sizeof(uint32_t)));
    addFixedItem("tessTableTriangles", BufferBytes(static_cast<uint32_t>(reyesTessellationTableData.triangles.size()), sizeof(uint32_t)));
    addFixedItem("diceIndirectArgsPhase1", sizeof(CLodReyesDispatchIndirectCommand));
    if (usesPhase2ReyesResources) {
        addFixedItem("diceIndirectArgsPhase2", sizeof(CLodReyesDispatchIndirectCommand));
    }
    addFixedItem("telemetryPhase1", sizeof(CLodReyesTelemetry));
    if (usesPhase2ReyesResources) {
        addFixedItem("telemetryPhase2", sizeof(CLodReyesTelemetry));
    }

    uint64_t idealBudgetBytes = 0u;
    for (const budget::ProportionalBudgetItem& item : budgetItems) {
        idealBudgetBytes += item.idealBytes;
    }

    const uint64_t configuredBudgetBytes = static_cast<uint64_t>(SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodReyesResourceBudgetBytesSettingName)());
    const uint64_t effectiveBudgetBytes = configuredBudgetBytes == 0u ? idealBudgetBytes : configuredBudgetBytes;
    const budget::ProportionalBudgetResult budgetResult = budget::AllocateProportionalBudget(budgetItems, effectiveBudgetBytes);

    ReyesResourceSizing sizing{};
    sizing.fullClusterOutputCapacity = BytesToElementCount(budgetResult.Find("fullClusterOutputs").allocatedBytes, sizeof(CLodReyesFullClusterOutput));
    sizing.ownedClusterCapacity = BytesToElementCount(budgetResult.Find("ownedClusters").allocatedBytes, sizeof(CLodReyesOwnedClusterEntry));
    sizing.splitQueueCapacity = BytesToElementCount(budgetResult.Find("splitQueueA").allocatedBytes, sizeof(CLodReyesSplitQueueEntry));
    sizing.diceQueueCapacity = static_cast<uint32_t>(
        budgetResult.Find("diceQueue").allocatedBytes /
        (static_cast<uint64_t>(sizeof(CLodReyesDiceQueueEntry)) * static_cast<uint64_t>(diceQueuePhaseMultiplier)));
    sizing.diceQueuePhysicalCapacity = sizing.diceQueueCapacity * diceQueuePhaseMultiplier;
    sizing.rasterWorkCapacity = BytesToElementCount(budgetResult.Find("rasterWorkPhase1").allocatedBytes, sizeof(CLodReyesRasterWorkEntry));
    sizing.ownershipBitsetWordCount = ownershipBitsetWordCount;
    sizing.requestedBudgetBytes = budgetResult.requestedTotalBytes;
    sizing.allocatedBudgetBytes = budgetResult.allocatedTotalBytes;
    sizing.budgetLimited = budgetResult.allocatedTotalBytes < budgetResult.requestedTotalBytes;
    return sizing;
}

} // namespace

CLodExtension::~CLodExtension() = default;

bool CLodExtension::IsReyesTessellationDisabled() const
{
    return SettingsManager::GetInstance().getSettingGetter<bool>(CLodDisableReyesRasterizationSettingName)();
}

void CLodExtension::RefreshShadowConfiguredSettings()
{
    if (m_type != CLodExtensionType::Shadow) {
        return;
    }

    m_shadowConfiguredBackingResolution = CLodVirtualShadowGetConfiguredMaxBackingResolution();
    m_shadowConfiguredMaxPhysicalPageCount = CLodVirtualShadowGetConfiguredMaxPhysicalPageCapacity();
    m_shadowConfiguredPageJobMaxPages = std::max(
        1u,
        std::min(
            SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodPageJobMaxPagesPerClusterSettingName)(),
            255u));
    m_shadowConfiguredPageJobRecordCapacity = std::max(
        1u,
        SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodPageJobRecordCapacitySettingName)());
    m_shadowConfiguredComputeClusterCapacity = CLodVirtualShadowGetConfiguredComputeClusterCapacity(m_maxVisibleClusters);
    m_shadowConfiguredExpandedRecordCapacity =
        std::max(1u, m_shadowConfiguredComputeClusterCapacity * (m_shadowConfiguredPageJobMaxPages + 1u));
}

uint32_t CLodExtension::GetVisibleClusterCapacity() const
{
    if (m_type != CLodExtensionType::Shadow) {
        return m_maxVisibleClusters;
    }

    return std::max(m_maxVisibleClusters, m_shadowConfiguredExpandedRecordCapacity);
}

void CLodExtension::RefreshCoreVisibleClusterCapacity()
{
    const uint32_t desiredVisibleClusterCapacity = GetVisibleClusterCapacity();
    if (m_visibleClusterCapacity == desiredVisibleClusterCapacity) {
        return;
    }

    m_visibleClusterCapacity = desiredVisibleClusterCapacity;

    if (m_visibleClustersBuffer) {
        m_visibleClustersBuffer->ResizeBytes(static_cast<uint64_t>(m_visibleClusterCapacity) * PackedVisibleClusterStrideBytes);
        m_visibleClustersBuffer->GetECSEntity().set<CLodVisibleClusterCapacity>({ m_visibleClusterCapacity });
    }

    if (m_compactedVisibleClustersBuffer) {
        m_compactedVisibleClustersBuffer->ResizeBytes(static_cast<uint64_t>(m_visibleClusterCapacity) * PackedVisibleClusterStrideBytes);
    }

    if (m_sortedToUnsortedMappingBuffer) {
        m_sortedToUnsortedMappingBuffer->ResizeStructured(m_visibleClusterCapacity);
    }
}

void CLodExtension::InitializeCoreResources()
{
    const auto& traits = GetVariantTraits(m_type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    m_visibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(static_cast<uint64_t>(m_visibleClusterCapacity) * PackedVisibleClusterStrideBytes, true, false, true);
    m_visibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Visible Clusters Buffer (uncompacted)"));

    m_histogramIndirectCommand = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterBucketsHistogramIndirectCommand), true, false, true, true);
    m_histogramIndirectCommand->SetName(MakeVariantResourceName(traits, "Raster Buckets Histogram Indirect Command Buffer"));

    m_rasterBucketsHistogramBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket histogram"));

    m_visibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "Visible Clusters Counter Buffer"));
    m_visibleClustersCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersCounterBuffer })
        .add<VisibleClustersCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_workGraphTelemetryBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodWorkGraphCounterCount, sizeof(uint32_t), true, false, false, false);
    m_workGraphTelemetryBuffer->SetName(MakeVariantResourceName(traits, "Work Graph Telemetry Buffer"));
    m_workGraphTelemetryBuffer->GetECSEntity()
        .set<Components::Resource>({ m_workGraphTelemetryBuffer })
        .add<CLodWorkGraphTelemetryBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_occlusionReplayBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodReplayBufferNumUints, sizeof(uint32_t), true, false, false, false); // TODO: Alias this when we don't need the gpu address in node input during setup
    m_occlusionReplayBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Replay Buffer"));

    m_occlusionReplayStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReplayBufferState), true, false, false, false);
    m_occlusionReplayStateBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Replay State Buffer"));

    m_occlusionNodeGpuInputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(3, sizeof(CLodNodeGpuInput), true, false, false, false);
    m_occlusionNodeGpuInputsBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Node GPU Inputs Buffer"));

    m_viewDepthSrvIndicesBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodMaxViewDepthIndices, sizeof(CLodViewDepthSRVIndex), true, false, false, false);
    m_viewDepthSrvIndicesBuffer->SetName(MakeVariantResourceName(traits, "View Depth SRV Indices Buffer"));

    m_rasterBucketsOffsetsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_rasterBucketsOffsetsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket offsets"));

    m_rasterBucketsBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_rasterBucketsBlockSumsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket block sums"));

    m_rasterBucketsScannedBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_rasterBucketsScannedBlockSumsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket scanned block sums"));

    m_rasterBucketsTotalCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_rasterBucketsTotalCountBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket total count"));

    m_rasterBucketsTotalCountBufferPhase1 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_rasterBucketsTotalCountBufferPhase1->SetName(MakeVariantResourceName(traits, "Raster bucket total count phase1"));

    m_rasterBucketsTotalCountBufferPhase1Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_rasterBucketsTotalCountBufferPhase1Sw->SetName(MakeVariantResourceName(traits, "Raster bucket total count phase1 SW"));

    m_compactedVisibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(static_cast<uint64_t>(m_visibleClusterCapacity) * PackedVisibleClusterStrideBytes, true, false);
    m_compactedVisibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Compacted Visible Clusters Buffer"));

    m_visibleClustersBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersBuffer })
        .set<CLodVisibleClusterCapacity>({ m_visibleClusterCapacity })
        .add<VisibleClustersBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_rasterBucketsWriteCursorBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor"));

    m_rasterBucketsIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, true);
    m_rasterBucketsIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args HW phase1"));

    m_rasterBucketsIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, true);
    m_rasterBucketsIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args HW phase2"));

    m_rasterBucketsIndirectArgsBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, true);
    m_rasterBucketsIndirectArgsBufferSw->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args SW phase1"));

    m_rasterBucketsIndirectArgsBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, true);
    m_rasterBucketsIndirectArgsBufferPhase2Sw->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args SW phase2"));

    m_rasterBucketsIndirectArgsBufferPageJob = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, true);
    m_rasterBucketsIndirectArgsBufferPageJob->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args Page Job phase1"));

    m_rasterBucketsIndirectArgsBufferPhase2PageJob = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, true);
    m_rasterBucketsIndirectArgsBufferPhase2PageJob->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args Page Job phase2"));

    m_visibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "Visible Clusters Counter Buffer Phase2"));

    m_rasterBucketsHistogramBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket histogram phase2"));

    m_rasterBucketsHistogramBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
    m_rasterBucketsHistogramBufferSw->SetName(MakeVariantResourceName(traits, "Raster bucket histogram SW phase1"));

    m_rasterBucketsHistogramBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
    m_rasterBucketsHistogramBufferPhase2Sw->SetName(MakeVariantResourceName(traits, "Raster bucket histogram SW phase2"));

    m_rasterBucketsWriteCursorBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor phase2"));

    m_rasterBucketsWriteCursorBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
    m_rasterBucketsWriteCursorBufferSw->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor SW phase1"));

    m_rasterBucketsWriteCursorBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
    m_rasterBucketsWriteCursorBufferPhase2Sw->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor SW phase2"));

    m_swVisibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "SW Visible Clusters Counter Buffer Phase1"));

    m_swVisibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "SW Visible Clusters Counter Buffer Phase2"));

    m_sortedToUnsortedMappingBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_visibleClusterCapacity, sizeof(uint32_t), true, false, false, true);
    m_sortedToUnsortedMappingBuffer->SetName(MakeVariantResourceName(traits, "Sorted-to-Unsorted Mapping Buffer"));

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName(MakeVariantResourceName(traits, "View Raster Info Buffer"));
}

void CLodExtension::InitializeDeepVisibilityResources()
{
    const auto& traits = GetVariantTraits(m_type);
    if (traits.rasterOutputKind != CLodRasterOutputKind::DeepVisibility) {
        return;
    }

    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    m_deepVisibilityNodesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(CLodDeepVisibilityNode),
        true,
        false,
        false,
        true);
    m_deepVisibilityNodesBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Nodes Buffer"));

    m_deepVisibilityCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_deepVisibilityCounterBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Counter Buffer"));
    m_deepVisibilityCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_deepVisibilityCounterBuffer })
        .add<CLodDeepVisibilityCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_deepVisibilityOverflowCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, true);
    m_deepVisibilityOverflowCounterBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Overflow Counter Buffer"));
    m_deepVisibilityOverflowCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_deepVisibilityOverflowCounterBuffer })
        .add<CLodDeepVisibilityOverflowCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_deepVisibilityStatsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(CLodDeepVisibilityStats),
        true,
        false,
        true,
		true);
    m_deepVisibilityStatsBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Stats Buffer"));
    m_deepVisibilityStatsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_deepVisibilityStatsBuffer })
        .add<CLodDeepVisibilityStatsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);
}

void CLodExtension::InitializeAVBOITResources()
{
    if (m_type != CLodExtensionType::AlphaBlend) {
        return;
    }

    const auto renderResolution =
        SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    m_transparencyConfiguredRenderWidth = renderResolution.x;
    m_transparencyConfiguredRenderHeight = renderResolution.y;

    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const auto& traits = GetVariantTraits(m_type);
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    m_AVBOITConfigBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(CLodAVBOITConfig),
        true,
        false,
        false,
        false);
    m_AVBOITConfigBuffer->SetName(MakeVariantResourceName(traits, "AVBOIT Config Buffer"));
    m_AVBOITConfigBuffer->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITConfigBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITOccupancyHistogramBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodAVBOITDefaultVirtualSliceCount,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_AVBOITOccupancyHistogramBuffer->SetName(
        MakeVariantResourceName(traits, "AVBOIT Occupancy Histogram"));
    m_AVBOITOccupancyHistogramBuffer->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITOccupancyHistogramBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITDepthWarpLUTBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodAVBOITDepthWarpLUTResolution,
        sizeof(CLodAVBOITDepthWarpLUTEntry),
        true,
        false,
        false,
        false);
    m_AVBOITDepthWarpLUTBuffer->SetName(
        MakeVariantResourceName(traits, "AVBOIT Depth Warp LUT"));
    m_AVBOITDepthWarpLUTBuffer->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITDepthWarpLUTBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITFitStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(CLodAVBOITFitState),
        true,
        false,
        false,
        false);
    m_AVBOITFitStateBuffer->SetName(
        MakeVariantResourceName(traits, "AVBOIT Adaptive Fit State"));
    m_AVBOITFitStateBuffer->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITFitStateBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITEarlyDepthTileCommandsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(CLodAVBOITEarlyDepthTileIndirectCommand),
        true,
        false,
        false,
        true);
    m_AVBOITEarlyDepthTileCommandsBuffer->SetName(
        MakeVariantResourceName(traits, "AVBOIT Early Depth Tile Commands"));
    m_AVBOITEarlyDepthTileCommandsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITEarlyDepthTileCommandsBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITEarlyDepthTileCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(uint32_t),
        true,
        false,
        false,
        true);
    m_AVBOITEarlyDepthTileCountBuffer->SetName(
        MakeVariantResourceName(traits, "AVBOIT Early Depth Tile Count"));
    m_AVBOITEarlyDepthTileCountBuffer->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITEarlyDepthTileCountBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITOccupancyTexture = PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITOccupancyDescription());
    m_AVBOITOccupancyTexture->SetName(MakeVariantResourceName(traits, "AVBOIT Occupancy"));
    m_AVBOITOccupancyTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITOccupancyTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITCoverageTexture = PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITOccupancyDescription());
    m_AVBOITCoverageTexture->SetName(MakeVariantResourceName(traits, "AVBOIT Coverage"));
    m_AVBOITCoverageTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITCoverageTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITOccupancySliceMaskTexture =
        PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITOccupancySliceMaskDescription());
    m_AVBOITOccupancySliceMaskTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Occupancy Slice Mask"));
    m_AVBOITOccupancySliceMaskTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITOccupancySliceMaskTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITExtinctionTexture = PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITExtinctionDescription());
    m_AVBOITExtinctionTexture->SetName(MakeVariantResourceName(traits, "AVBOIT Extinction"));
    m_AVBOITExtinctionTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITExtinctionTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITChromaticExtinctionTexture =
        PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITChromaticExtinctionDescription());
    m_AVBOITChromaticExtinctionTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Chromatic Extinction"));
    m_AVBOITChromaticExtinctionTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITChromaticExtinctionTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITIntegratedTransmittanceTexture =
        PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITIntegratedTransmittanceDescription());
    m_AVBOITIntegratedTransmittanceTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Integrated Transmittance"));
    m_AVBOITIntegratedTransmittanceTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITIntegratedTransmittanceTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITZeroTransmittanceSliceTexture =
        PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITZeroTransmittanceSliceDescription());
    m_AVBOITZeroTransmittanceSliceTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Zero Transmittance Slice"));
    m_AVBOITZeroTransmittanceSliceTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITZeroTransmittanceSliceTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITAccumulationTexture = PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITAccumulationDescription());
    m_AVBOITAccumulationTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Accumulation"));
    m_AVBOITAccumulationTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITAccumulationTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITNormalizationTexture =
        PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITNormalizationDescription());
    m_AVBOITNormalizationTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Normalization"));
    m_AVBOITNormalizationTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITNormalizationTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITShadingExtinctionTexture =
        PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITShadingExtinctionDescription());
    m_AVBOITShadingExtinctionTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Shading Extinction"));
    m_AVBOITShadingExtinctionTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITShadingExtinctionTexture })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_AVBOITEarlyDepthTexture =
        PixelBuffer::CreateSharedUnmaterialized(CreateAVBOITEarlyDepthDescription());
    m_AVBOITEarlyDepthTexture->SetName(
        MakeVariantResourceName(traits, "AVBOIT Early Depth"));
    m_AVBOITEarlyDepthTexture->GetECSEntity()
        .set<Components::Resource>({ m_AVBOITEarlyDepthTexture })
        .add<CLodExtensionTypeTag>(typeEntity);
}

void CLodExtension::InitializeShadowResources()
{
    const auto& traits = GetVariantTraits(m_type);
    if (traits.type != CLodExtensionType::Shadow) {
        return;
    }

    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);
    const uint32_t maxShadowPhysicalPageCount = m_shadowConfiguredMaxPhysicalPageCount;
    const uint32_t pageJobRecordCapacity = m_shadowConfiguredPageJobRecordCapacity;
    const uint32_t vsmExpandedRecordCapacity = m_shadowConfiguredExpandedRecordCapacity;

    m_shadowPageTableTexture = PixelBuffer::CreateSharedUnmaterialized(CreateVirtualShadowPageTableDescription());
    m_shadowPageTableTexture->SetName(MakeVariantResourceName(traits, "Virtual Shadow Page Table"));
    m_shadowPageTableTexture->GetECSEntity()
        .set<Components::Resource>({ m_shadowPageTableTexture })
        .add<CLodVirtualShadowPageTableTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPhysicalPagesTexture = PixelBuffer::CreateSharedUnmaterialized(
        CreateVirtualShadowPhysicalPagesDescription(m_shadowConfiguredBackingResolution));
    m_shadowPhysicalPagesTexture->SetName(MakeVariantResourceName(traits, "Virtual Shadow Physical Pages"));
    m_shadowPhysicalPagesTexture->GetECSEntity()
        .set<Components::Resource>({ m_shadowPhysicalPagesTexture })
        .add<CLodVirtualShadowPhysicalPagesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPageMetadataBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        maxShadowPhysicalPageCount,
        sizeof(CLodVirtualShadowPhysicalPageMeta),
        true,
        false,
        false,
        false);
    m_shadowPageMetadataBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Page Metadata Buffer"));
    m_shadowPageMetadataBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPageMetadataBuffer })
        .add<CLodVirtualShadowPageMetadataTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowInvalidationInputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxInvalidationInputs,
        sizeof(CLodVirtualShadowInvalidationInput),
        false,
        false,
        false);
    m_shadowInvalidationInputsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Invalidation Inputs Buffer"));
    m_shadowInvalidationInputsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowInvalidationInputsBuffer })
        .add<CLodVirtualShadowInvalidationInputsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowInvalidationCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), false, false, false);
    m_shadowInvalidationCountBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Invalidation Count Buffer"));
    m_shadowInvalidationCountBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowInvalidationCountBuffer })
        .add<CLodVirtualShadowInvalidationCountTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowInvalidatedInstancesBitsetBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMovedInstanceBitWordCount(),
        sizeof(uint32_t),
        false,
        false,
        false);
    m_shadowInvalidatedInstancesBitsetBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Invalidated Instances Bitset Buffer"));

    m_shadowPredictiveInvalidationCandidatesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowPredictiveCandidateCapacity,
        sizeof(CLodVirtualShadowPredictiveInvalidationCandidate),
        true,
        false,
        false);
    m_shadowPredictiveInvalidationCandidatesBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Predictive Invalidation Candidates Buffer"));
    m_shadowPredictiveInvalidationCandidatesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPredictiveInvalidationCandidatesBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPredictiveInvalidationCandidateCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_shadowPredictiveInvalidationCandidateCountBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Predictive Invalidation Candidate Count Buffer"));
    m_shadowPredictiveInvalidationCandidateCountBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPredictiveInvalidationCandidateCountBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPredictiveRawPagesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowPredictiveRawPageCapacity,
        sizeof(CLodVirtualShadowPredictedRawPage),
        true,
        false,
        false);
    m_shadowPredictiveRawPagesBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Predictive Raw Pages Buffer"));
    m_shadowPredictiveRawPagesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPredictiveRawPagesBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPredictiveRawPageCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_shadowPredictiveRawPageCountBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Predictive Raw Page Count Buffer"));
    m_shadowPredictiveRawPageCountBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPredictiveRawPageCountBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPredictedInvalidationScratchBitsetBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowPredictedPageBitsetWordCount(),
        sizeof(uint32_t),
        true,
        false,
        false,
		true);
    m_shadowPredictedInvalidationScratchBitsetBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Predicted Invalidation Scratch Bitset Buffer"));
    m_shadowPredictedInvalidationScratchBitsetBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPredictedInvalidationScratchBitsetBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    // This buffer is consumed at the start of the next frame before any pass rewrites it,
    // so it cannot participate in transient aliasing.
    m_shadowPredictedInvalidationPagesBufferA = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowPredictedPageListCapacity(),
        sizeof(CLodVirtualShadowPredictedPage),
        true,
        false,
        false,
        false);
    m_shadowPredictedInvalidationPagesBufferA->SetName(MakeVariantResourceName(traits, "Virtual Shadow Predicted Invalidation Pages Buffer"));
    m_shadowPredictedInvalidationPagesBufferA->GetECSEntity()
        .set<Components::Resource>({ m_shadowPredictedInvalidationPagesBufferA })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPredictedInvalidationPageCountBufferA = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_shadowPredictedInvalidationPageCountBufferA->SetName(MakeVariantResourceName(traits, "Virtual Shadow Predicted Invalidation Page Count Buffer"));
    m_shadowPredictedInvalidationPageCountBufferA->GetECSEntity()
        .set<Components::Resource>({ m_shadowPredictedInvalidationPageCountBufferA })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowAllocationRequestsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxAllocationRequests,
        sizeof(CLodVirtualShadowPageAllocationRequest),
        true,
        false,
        false,
		true);
    m_shadowAllocationRequestsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Allocation Requests Buffer"));
    m_shadowAllocationRequestsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowAllocationRequestsBuffer })
        .add<CLodVirtualShadowAllocationRequestsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowAllocationCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_shadowAllocationCountBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Allocation Count Buffer"));
    m_shadowAllocationCountBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowAllocationCountBuffer })
        .add<CLodVirtualShadowAllocationCountTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowAllocationIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
    m_shadowAllocationIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Allocation Indirect Args Buffer"));

    m_shadowMarkTileWorkBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxMarkTileCount,
        sizeof(CLodVirtualShadowMarkTileWorkItem),
        true,
        false,
        false,
        true);
    m_shadowMarkTileWorkBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Mark Tile Work Buffer"));

    m_shadowMarkTileCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_shadowMarkTileCountBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Mark Tile Count Buffer"));

    m_shadowMarkTileIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
    m_shadowMarkTileIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Mark Tile Indirect Args Buffer"));

    m_shadowMarkedBlocksMaskBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxMarkedBlockCount,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowMarkedBlocksMaskBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Marked Blocks Mask Buffer"));

    m_shadowMarkedBlocksListBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxMarkedBlockCount,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowMarkedBlocksListBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Marked Blocks List Buffer"));

    m_shadowMarkedBlocksCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_shadowMarkedBlocksCountBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Marked Blocks Count Buffer"));

    m_shadowFreePhysicalPagesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        maxShadowPhysicalPageCount,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowFreePhysicalPagesBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Free Physical Pages Buffer"));
    m_shadowFreePhysicalPagesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowFreePhysicalPagesBuffer })
        .add<CLodVirtualShadowFreePhysicalPagesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowReusablePhysicalPagesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        maxShadowPhysicalPageCount,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowReusablePhysicalPagesBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Reusable Physical Pages Buffer"));
    m_shadowReusablePhysicalPagesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowReusablePhysicalPagesBuffer })
        .add<CLodVirtualShadowReusablePhysicalPagesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowPageListHeaderBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodVirtualShadowPageListHeader), true, false, false, false);
    m_shadowPageListHeaderBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Page List Header Buffer"));
    m_shadowPageListHeaderBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowPageListHeaderBuffer })
        .add<CLodVirtualShadowPageListHeaderTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowDirtyPageFlagsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowDirtyWordCount(maxShadowPhysicalPageCount),
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_shadowDirtyPageFlagsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Dirty Page Flags Buffer"));
    m_shadowDirtyPageFlagsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowDirtyPageFlagsBuffer })
        .add<CLodVirtualShadowDirtyPageFlagsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowDirtyPageHierarchyTexture = PixelBuffer::CreateSharedUnmaterialized(CreateVirtualShadowDirtyHierarchyDescription());
    m_shadowDirtyPageHierarchyTexture->SetName(MakeVariantResourceName(traits, "Virtual Shadow Dirty Hierarchy"));
    m_shadowDirtyPageHierarchyTexture->GetECSEntity()
        .set<Components::Resource>({ m_shadowDirtyPageHierarchyTexture })
        .add<CLodVirtualShadowDirtyHierarchyTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowNonRasterablePageHierarchyTexture = PixelBuffer::CreateSharedUnmaterialized(CreateVirtualShadowDirtyHierarchyDescription());
    m_shadowNonRasterablePageHierarchyTexture->SetName(MakeVariantResourceName(traits, "Virtual Shadow Non-Rasterable Hierarchy"));
    m_shadowNonRasterablePageHierarchyTexture->GetECSEntity()
        .set<Components::Resource>({ m_shadowNonRasterablePageHierarchyTexture })
        .add<CLodVirtualShadowNonRasterableHierarchyTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowClipmapInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxSupportedClipmapCount,
        sizeof(CLodVirtualShadowClipmapInfo),
        true,
        false,
        false,
        false);
    m_shadowClipmapInfoBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Clipmap Info Buffer"));
    m_shadowClipmapInfoBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowClipmapInfoBuffer })
        .add<CLodVirtualShadowClipmapInfoTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowMarkClipmapDataBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxSupportedClipmapCount,
        sizeof(CLodVirtualShadowMarkClipmapData),
        true,
        false,
        false,
        false);
    m_shadowMarkClipmapDataBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Mark Clipmap Data Buffer"));
    m_shadowMarkClipmapDataBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowMarkClipmapDataBuffer })
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowCompactMainCameraBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(CLodVirtualShadowMainCameraInfo),
        true,
        false,
        false,
        false);
    m_shadowCompactMainCameraBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Compact Main Camera Buffer"));

    m_shadowCompactShadowCameraBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxSupportedClipmapCount,
        sizeof(CLodVirtualShadowCompactShadowCameraInfo),
        true,
        false,
        false,
        false);
    m_shadowCompactShadowCameraBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Compact Shadow Camera Buffer"));

    m_shadowDirectionalPageViewInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodVirtualShadowMaxDirectionalPageViewInfoEntryCount(),
        sizeof(float) * 4u,
        true,
        false,
        false,
		false);
    m_shadowDirectionalPageViewInfoBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Directional Page View Info Buffer"));
    m_shadowDirectionalPageViewInfoBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowDirectionalPageViewInfoBuffer })
        .add<CLodVirtualShadowDirectionalPageViewInfoTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowRuntimeStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodVirtualShadowRuntimeState), true, false, false);
    m_shadowRuntimeStateBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Runtime State Buffer"));
    m_shadowRuntimeStateBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowRuntimeStateBuffer })
        .add<CLodVirtualShadowRuntimeStateTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_shadowStatsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodVirtualShadowStats), true, false, false, true);
    m_shadowStatsBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Stats Buffer"));
    m_shadowStatsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_shadowStatsBuffer })
        .add<CLodVirtualShadowStatsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_swPageJobVisibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(m_maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false, true);
    m_swPageJobVisibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Visible Clusters Buffer"));

    m_swPageJobVisibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_swPageJobVisibleClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Visible Clusters Counter Buffer"));

    m_swPageJobVisibleClustersBufferPhase2 = m_swPageJobVisibleClustersBuffer;
    m_swPageJobVisibleClustersCounterBufferPhase2 = m_swPageJobVisibleClustersCounterBuffer;

    m_swPageJobRecordsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        pageJobRecordCapacity,
        sizeof(CLodSoftwareRasterPageJobRecord),
        true,
        false,
        false,
        true);
    m_swPageJobRecordsBuffer->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Records Buffer"));
    m_swPageJobRecordsBufferSkinned = CreateAliasedUnmaterializedStructuredBuffer(
        pageJobRecordCapacity,
        sizeof(CLodSoftwareRasterPageJobRecord),
        true,
        false,
        false,
        true);
    m_swPageJobRecordsBufferSkinned->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Records Buffer Skinned"));

    m_swPageJobRecordsBufferPhase2 = m_swPageJobRecordsBuffer;
    m_swPageJobRecordsBufferPhase2Skinned = m_swPageJobRecordsBufferSkinned;

    m_swPageJobCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_swPageJobCountBuffer->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Count Buffer"));
    m_swPageJobCountBufferSkinned = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_swPageJobCountBufferSkinned->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Count Buffer Skinned"));

    m_swPageJobCountBufferPhase2 = m_swPageJobCountBuffer;
    m_swPageJobCountBufferPhase2Skinned = m_swPageJobCountBufferSkinned;

    m_swPageJobIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, false);
    m_swPageJobIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Indirect Args Buffer Phase1"));
    m_swPageJobIndirectArgsBufferSkinned = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false, false, false);
    m_swPageJobIndirectArgsBufferSkinned->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Indirect Args Buffer Phase1 Skinned"));

    m_swPageJobIndirectArgsBufferPhase2 = m_swPageJobIndirectArgsBuffer;
    m_swPageJobIndirectArgsBufferPhase2Skinned = m_swPageJobIndirectArgsBufferSkinned;

    m_swPageJobClusterTagsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        m_maxVisibleClusters,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_swPageJobClusterTagsBuffer->SetName(MakeVariantResourceName(traits, "Software Raster Page Job Cluster Tags Buffer"));

    m_swPageJobClusterTagsBufferPhase2 = m_swPageJobClusterTagsBuffer;

    m_vsmExpandedVisibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(
        static_cast<uint64_t>(vsmExpandedRecordCapacity) * PackedVisibleClusterStrideBytes,
        true,
		false);
    m_vsmExpandedVisibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Expanded Visible Clusters Buffer"));

    m_vsmExpandedBlockMetaBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        vsmExpandedRecordCapacity,
        sizeof(CLodVirtualShadowBlockMeta),
        true,
        false,
        false,
        false);
    m_vsmExpandedBlockMetaBuffer->SetName(MakeVariantResourceName(traits, "Virtual Shadow Expanded Block Metadata Buffer"));

    m_vsmExpandedVisibleClustersBufferSw = m_vsmExpandedVisibleClustersBuffer;
    m_vsmExpandedBlockMetaBufferSw = m_vsmExpandedBlockMetaBuffer;

    m_shadowVirtualResourcesNeedReset = true;
}

void CLodExtension::TagCoreResourceUsages()
{
    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };

    tagBufferUsage(m_visibleClustersBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_histogramIndirectCommand, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsHistogramBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_visibleClustersCounterBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_workGraphTelemetryBuffer, "Cluster LOD telemetry");
    tagBufferUsage(m_occlusionReplayBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_occlusionReplayStateBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_occlusionNodeGpuInputsBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_viewDepthSrvIndicesBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_rasterBucketsOffsetsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsBlockSumsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsScannedBlockSumsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsTotalCountBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsTotalCountBufferPhase1, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsTotalCountBufferPhase1Sw, "Cluster LOD rasterization");
    tagBufferUsage(m_compactedVisibleClustersBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_rasterBucketsWriteCursorBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsIndirectArgsBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsIndirectArgsBufferPhase2, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsIndirectArgsBufferSw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsIndirectArgsBufferPhase2Sw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsIndirectArgsBufferPageJob, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsIndirectArgsBufferPhase2PageJob, "Cluster LOD rasterization");
    tagBufferUsage(m_visibleClustersCounterBufferPhase2, "Cluster LOD visibility");
    tagBufferUsage(m_rasterBucketsHistogramBufferPhase2, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsHistogramBufferSw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsHistogramBufferPhase2Sw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsWriteCursorBufferPhase2, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsWriteCursorBufferSw, "Cluster LOD rasterization");
    tagBufferUsage(m_rasterBucketsWriteCursorBufferPhase2Sw, "Cluster LOD rasterization");
    tagBufferUsage(m_swVisibleClustersCounterBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_swVisibleClustersCounterBufferPhase2, "Cluster LOD visibility");
    tagBufferUsage(m_sortedToUnsortedMappingBuffer, "Cluster LOD visibility");
    tagBufferUsage(m_viewRasterInfoBuffer, "Cluster LOD rasterization");
    tagBufferUsage(m_deepVisibilityNodesBuffer, "Cluster LOD deep visibility");
    tagBufferUsage(m_deepVisibilityCounterBuffer, "Cluster LOD deep visibility");
    tagBufferUsage(m_deepVisibilityOverflowCounterBuffer, "Cluster LOD deep visibility");
    tagBufferUsage(m_deepVisibilityStatsBuffer, "Cluster LOD deep visibility");
}

void CLodExtension::TagShadowResourceUsages()
{
    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };
    auto tagTextureUsage = [](const std::shared_ptr<PixelBuffer>& texture, std::string_view usage) {
        if (texture) {
            rg::memory::SetResourceUsageHint(*texture, std::string(usage));
        }
    };

    tagTextureUsage(m_shadowPageTableTexture, "Cluster LOD virtual shadow maps");
    tagTextureUsage(m_shadowPhysicalPagesTexture, "Cluster LOD virtual shadow maps");
    tagTextureUsage(m_shadowDirtyPageHierarchyTexture, "Cluster LOD virtual shadow maps");
    tagTextureUsage(m_shadowNonRasterablePageHierarchyTexture, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPageMetadataBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowInvalidationInputsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowInvalidationCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowInvalidatedInstancesBitsetBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPredictiveInvalidationCandidatesBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPredictiveInvalidationCandidateCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPredictiveRawPagesBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPredictiveRawPageCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPredictedInvalidationScratchBitsetBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPredictedInvalidationPagesBufferA, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPredictedInvalidationPageCountBufferA, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowAllocationRequestsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowAllocationCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowAllocationIndirectArgsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowMarkTileWorkBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowMarkTileCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowMarkTileIndirectArgsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowMarkedBlocksMaskBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowMarkedBlocksListBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowMarkedBlocksCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowFreePhysicalPagesBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowReusablePhysicalPagesBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowPageListHeaderBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowDirtyPageFlagsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowClipmapInfoBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowMarkClipmapDataBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowCompactMainCameraBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowCompactShadowCameraBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowDirectionalPageViewInfoBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowRuntimeStateBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_shadowStatsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobVisibleClustersBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobVisibleClustersCounterBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobVisibleClustersBufferPhase2, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobVisibleClustersCounterBufferPhase2, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobRecordsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobRecordsBufferSkinned, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobCountBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobCountBufferSkinned, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobRecordsBufferPhase2, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobRecordsBufferPhase2Skinned, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobCountBufferPhase2, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobCountBufferPhase2Skinned, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobIndirectArgsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobIndirectArgsBufferSkinned, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobIndirectArgsBufferPhase2, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobIndirectArgsBufferPhase2Skinned, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobClusterTagsBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_swPageJobClusterTagsBufferPhase2, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_vsmExpandedVisibleClustersBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_vsmExpandedBlockMetaBuffer, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_vsmExpandedVisibleClustersBufferSw, "Cluster LOD virtual shadow maps");
    tagBufferUsage(m_vsmExpandedBlockMetaBufferSw, "Cluster LOD virtual shadow maps");
}

void CLodExtension::TagTransparencyResourceUsages()
{
    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };
    auto tagTextureUsage = [](const std::shared_ptr<PixelBuffer>& texture, std::string_view usage) {
        if (texture) {
            rg::memory::SetResourceUsageHint(*texture, std::string(usage));
        }
    };

    tagBufferUsage(m_AVBOITConfigBuffer, "Cluster LOD AVBOIT");
    tagBufferUsage(m_AVBOITOccupancyHistogramBuffer, "Cluster LOD AVBOIT adaptive histogram");
    tagBufferUsage(m_AVBOITDepthWarpLUTBuffer, "Cluster LOD AVBOIT depth warp LUT");
    tagBufferUsage(m_AVBOITFitStateBuffer, "Cluster LOD AVBOIT adaptive fit state");
    tagBufferUsage(m_AVBOITEarlyDepthTileCommandsBuffer, "Cluster LOD AVBOIT early depth");
    tagBufferUsage(m_AVBOITEarlyDepthTileCountBuffer, "Cluster LOD AVBOIT early depth");
    tagTextureUsage(m_AVBOITOccupancyTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITCoverageTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITOccupancySliceMaskTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITExtinctionTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITChromaticExtinctionTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITIntegratedTransmittanceTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITZeroTransmittanceSliceTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITAccumulationTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITNormalizationTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITShadingExtinctionTexture, "Cluster LOD AVBOIT");
    tagTextureUsage(m_AVBOITEarlyDepthTexture, "Cluster LOD AVBOIT early depth");
}

void CLodExtension::ReleaseBufferBackings()
{
    std::unordered_set<Buffer*> releasedBuffers;
    auto releaseBufferBacking = [&releasedBuffers](const std::shared_ptr<Buffer>& buffer) {
        if (buffer && releasedBuffers.insert(buffer.get()).second) {
            buffer->Dematerialize();
        }
    };

    releaseBufferBacking(m_visibleClustersBuffer);
    releaseBufferBacking(m_visibleClustersCounterBuffer);
    releaseBufferBacking(m_workGraphTelemetryBuffer);
    releaseBufferBacking(m_occlusionReplayBuffer);
    releaseBufferBacking(m_occlusionReplayStateBuffer);
    releaseBufferBacking(m_occlusionNodeGpuInputsBuffer);
    releaseBufferBacking(m_viewDepthSrvIndicesBuffer);
    releaseBufferBacking(m_histogramIndirectCommand);
    releaseBufferBacking(m_rasterBucketsHistogramBuffer);
    releaseBufferBacking(m_rasterBucketsOffsetsBuffer);
    releaseBufferBacking(m_rasterBucketsBlockSumsBuffer);
    releaseBufferBacking(m_reyesPackedRasterWorkGroupsBuffer);
    releaseBufferBacking(m_rasterBucketsScannedBlockSumsBuffer);
    releaseBufferBacking(m_rasterBucketsTotalCountBuffer);
    releaseBufferBacking(m_rasterBucketsTotalCountBufferPhase1);
    releaseBufferBacking(m_rasterBucketsTotalCountBufferPhase1Sw);
    releaseBufferBacking(m_visibleClustersCounterBufferPhase2);
    releaseBufferBacking(m_rasterBucketsHistogramBufferPhase2);
    releaseBufferBacking(m_rasterBucketsWriteCursorBufferPhase2);
    releaseBufferBacking(m_reyesPackedRasterWorkGroupsBufferPhase2);
    releaseBufferBacking(m_rasterBucketsHistogramBufferSw);
    releaseBufferBacking(m_rasterBucketsHistogramBufferPhase2Sw);
    releaseBufferBacking(m_rasterBucketsWriteCursorBufferSw);
    releaseBufferBacking(m_rasterBucketsWriteCursorBufferPhase2Sw);
    releaseBufferBacking(m_compactedVisibleClustersBuffer);
    releaseBufferBacking(m_rasterBucketsWriteCursorBuffer);
    releaseBufferBacking(m_rasterBucketsIndirectArgsBuffer);
    releaseBufferBacking(m_rasterBucketsIndirectArgsBufferPhase2);
    releaseBufferBacking(m_rasterBucketsIndirectArgsBufferSw);
    releaseBufferBacking(m_rasterBucketsIndirectArgsBufferPhase2Sw);
    releaseBufferBacking(m_rasterBucketsIndirectArgsBufferPageJob);
    releaseBufferBacking(m_rasterBucketsIndirectArgsBufferPhase2PageJob);
    releaseBufferBacking(m_reyesFullClusterOutputsBuffer);
    releaseBufferBacking(m_reyesFullClusterOutputsCounterBuffer);
    releaseBufferBacking(m_reyesOwnedClustersBuffer);
    releaseBufferBacking(m_reyesOwnedClustersCounterBuffer);
    releaseBufferBacking(m_reyesOwnershipBitsetBuffer);
    releaseBufferBacking(m_reyesOwnershipBitsetBufferPhase2);
    releaseBufferBacking(m_reyesClassifyIndirectArgsBuffer);
    releaseBufferBacking(m_reyesClassifyIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesSplitIndirectArgsBuffer);
    releaseBufferBacking(m_reyesSplitIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesSplitQueueBufferA);
    releaseBufferBacking(m_reyesSplitQueueCounterBufferA);
    releaseBufferBacking(m_reyesSplitQueueOverflowBufferA);
    releaseBufferBacking(m_reyesSplitQueueBufferB);
    releaseBufferBacking(m_reyesSplitQueueCounterBufferB);
    releaseBufferBacking(m_reyesSplitQueueOverflowBufferB);
    releaseBufferBacking(m_reyesDiceQueueBuffer);
    releaseBufferBacking(m_reyesDiceQueueCounterBuffer);
    releaseBufferBacking(m_reyesDiceQueuePhase1CountBuffer);
    releaseBufferBacking(m_reyesDiceQueueOverflowBuffer);
    releaseBufferBacking(m_reyesRasterWorkBuffer);
    releaseBufferBacking(m_reyesRasterWorkCounterBuffer);
    releaseBufferBacking(m_reyesRasterWorkIndirectArgsBuffer);
    releaseBufferBacking(m_reyesCompactedRasterWorkIndicesBuffer);
    releaseBufferBacking(m_reyesPackedRasterWorkGroupsBuffer);
    releaseBufferBacking(m_reyesTessTableConfigsBuffer);
    releaseBufferBacking(m_reyesTessTableVerticesBuffer);
    releaseBufferBacking(m_reyesTessTableTrianglesBuffer);
    releaseBufferBacking(m_reyesDiceIndirectArgsBuffer);
    releaseBufferBacking(m_reyesDiceIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesRasterWorkBufferPhase2);
    releaseBufferBacking(m_reyesRasterWorkCounterBufferPhase2);
    releaseBufferBacking(m_reyesRasterWorkIndirectArgsBufferPhase2);
    releaseBufferBacking(m_reyesCompactedRasterWorkIndicesBufferPhase2);
    releaseBufferBacking(m_reyesPackedRasterWorkGroupsBufferPhase2);
    releaseBufferBacking(m_reyesTelemetryBufferPhase1);
    releaseBufferBacking(m_reyesTelemetryBufferPhase2);
    releaseBufferBacking(m_swVisibleClustersCounterBuffer);
    releaseBufferBacking(m_swVisibleClustersCounterBufferPhase2);
    releaseBufferBacking(m_sortedToUnsortedMappingBuffer);
    releaseBufferBacking(m_viewRasterInfoBuffer);
    releaseBufferBacking(m_deepVisibilityNodesBuffer);
    releaseBufferBacking(m_deepVisibilityCounterBuffer);
    releaseBufferBacking(m_deepVisibilityOverflowCounterBuffer);
    releaseBufferBacking(m_deepVisibilityStatsBuffer);
    releaseBufferBacking(m_AVBOITConfigBuffer);
    releaseBufferBacking(m_AVBOITOccupancyHistogramBuffer);
    releaseBufferBacking(m_AVBOITDepthWarpLUTBuffer);
    releaseBufferBacking(m_AVBOITFitStateBuffer);
    releaseBufferBacking(m_AVBOITEarlyDepthTileCommandsBuffer);
    releaseBufferBacking(m_AVBOITEarlyDepthTileCountBuffer);
    releaseBufferBacking(m_shadowPageMetadataBuffer);
    releaseBufferBacking(m_shadowInvalidationInputsBuffer);
    releaseBufferBacking(m_shadowInvalidationCountBuffer);
    releaseBufferBacking(m_shadowInvalidatedInstancesBitsetBuffer);
    releaseBufferBacking(m_shadowPredictiveInvalidationCandidatesBuffer);
    releaseBufferBacking(m_shadowPredictiveInvalidationCandidateCountBuffer);
    releaseBufferBacking(m_shadowPredictiveRawPagesBuffer);
    releaseBufferBacking(m_shadowPredictiveRawPageCountBuffer);
    releaseBufferBacking(m_shadowPredictedInvalidationScratchBitsetBuffer);
    releaseBufferBacking(m_shadowPredictedInvalidationPagesBufferA);
    releaseBufferBacking(m_shadowPredictedInvalidationPageCountBufferA);
    releaseBufferBacking(m_shadowAllocationRequestsBuffer);
    releaseBufferBacking(m_shadowAllocationCountBuffer);
    releaseBufferBacking(m_shadowAllocationIndirectArgsBuffer);
    releaseBufferBacking(m_shadowMarkTileWorkBuffer);
    releaseBufferBacking(m_shadowMarkTileCountBuffer);
    releaseBufferBacking(m_shadowMarkTileIndirectArgsBuffer);
    releaseBufferBacking(m_shadowMarkedBlocksMaskBuffer);
    releaseBufferBacking(m_shadowMarkedBlocksListBuffer);
    releaseBufferBacking(m_shadowMarkedBlocksCountBuffer);
    releaseBufferBacking(m_shadowFreePhysicalPagesBuffer);
    releaseBufferBacking(m_shadowReusablePhysicalPagesBuffer);
    releaseBufferBacking(m_shadowPageListHeaderBuffer);
    releaseBufferBacking(m_shadowDirtyPageFlagsBuffer);
    releaseBufferBacking(m_shadowClipmapInfoBuffer);
    releaseBufferBacking(m_shadowMarkClipmapDataBuffer);
    releaseBufferBacking(m_shadowCompactMainCameraBuffer);
    releaseBufferBacking(m_shadowCompactShadowCameraBuffer);
    releaseBufferBacking(m_shadowDirectionalPageViewInfoBuffer);
    releaseBufferBacking(m_shadowRuntimeStateBuffer);
    releaseBufferBacking(m_shadowStatsBuffer);
    releaseBufferBacking(m_swPageJobVisibleClustersBuffer);
    releaseBufferBacking(m_swPageJobVisibleClustersCounterBuffer);
    releaseBufferBacking(m_swPageJobVisibleClustersBufferPhase2);
    releaseBufferBacking(m_swPageJobVisibleClustersCounterBufferPhase2);
    releaseBufferBacking(m_swPageJobRecordsBuffer);
    releaseBufferBacking(m_swPageJobRecordsBufferSkinned);
    releaseBufferBacking(m_swPageJobCountBuffer);
    releaseBufferBacking(m_swPageJobCountBufferSkinned);
    releaseBufferBacking(m_swPageJobRecordsBufferPhase2);
    releaseBufferBacking(m_swPageJobRecordsBufferPhase2Skinned);
    releaseBufferBacking(m_swPageJobCountBufferPhase2);
    releaseBufferBacking(m_swPageJobCountBufferPhase2Skinned);
    releaseBufferBacking(m_swPageJobIndirectArgsBuffer);
    releaseBufferBacking(m_swPageJobIndirectArgsBufferSkinned);
    releaseBufferBacking(m_swPageJobIndirectArgsBufferPhase2);
    releaseBufferBacking(m_swPageJobIndirectArgsBufferPhase2Skinned);
    releaseBufferBacking(m_swPageJobClusterTagsBuffer);
    releaseBufferBacking(m_swPageJobClusterTagsBufferPhase2);
    releaseBufferBacking(m_vsmExpandedVisibleClustersBuffer);
    releaseBufferBacking(m_vsmExpandedBlockMetaBuffer);
    releaseBufferBacking(m_vsmExpandedVisibleClustersBufferSw);
    releaseBufferBacking(m_vsmExpandedBlockMetaBufferSw);
}

void CLodExtension::ReleaseTransparencyResourceBackings()
{
    std::unordered_set<PixelBuffer*> releasedTextures;
    auto releaseTextureBacking = [&releasedTextures](const std::shared_ptr<PixelBuffer>& texture) {
        if (texture && releasedTextures.insert(texture.get()).second) {
            texture->Dematerialize();
        }
    };

    releaseTextureBacking(m_AVBOITOccupancyTexture);
    releaseTextureBacking(m_AVBOITCoverageTexture);
    releaseTextureBacking(m_AVBOITOccupancySliceMaskTexture);
    releaseTextureBacking(m_AVBOITExtinctionTexture);
    releaseTextureBacking(m_AVBOITChromaticExtinctionTexture);
    releaseTextureBacking(m_AVBOITIntegratedTransmittanceTexture);
    releaseTextureBacking(m_AVBOITZeroTransmittanceSliceTexture);
    releaseTextureBacking(m_AVBOITAccumulationTexture);
    releaseTextureBacking(m_AVBOITNormalizationTexture);
    releaseTextureBacking(m_AVBOITShadingExtinctionTexture);
    releaseTextureBacking(m_AVBOITEarlyDepthTexture);
}

void CLodExtension::ReleaseShadowResourceBackings()
{
    std::unordered_set<Buffer*> releasedBuffers;
    std::unordered_set<PixelBuffer*> releasedTextures;
    auto releaseBufferBacking = [&releasedBuffers](const std::shared_ptr<Buffer>& buffer) {
        if (buffer && releasedBuffers.insert(buffer.get()).second) {
            buffer->Dematerialize();
        }
    };
    auto releaseTextureBacking = [&releasedTextures](const std::shared_ptr<PixelBuffer>& texture) {
        if (texture && releasedTextures.insert(texture.get()).second) {
            texture->Dematerialize();
        }
    };

    releaseTextureBacking(m_shadowPageTableTexture);
    releaseTextureBacking(m_shadowPhysicalPagesTexture);
    releaseTextureBacking(m_shadowDirtyPageHierarchyTexture);
    releaseTextureBacking(m_shadowNonRasterablePageHierarchyTexture);
    releaseBufferBacking(m_swPageJobVisibleClustersBuffer);
    releaseBufferBacking(m_swPageJobVisibleClustersCounterBuffer);
    releaseBufferBacking(m_swPageJobVisibleClustersBufferPhase2);
    releaseBufferBacking(m_swPageJobVisibleClustersCounterBufferPhase2);
    releaseBufferBacking(m_swPageJobRecordsBuffer);
    releaseBufferBacking(m_swPageJobRecordsBufferSkinned);
    releaseBufferBacking(m_swPageJobCountBuffer);
    releaseBufferBacking(m_swPageJobCountBufferSkinned);
    releaseBufferBacking(m_swPageJobRecordsBufferPhase2);
    releaseBufferBacking(m_swPageJobRecordsBufferPhase2Skinned);
    releaseBufferBacking(m_swPageJobCountBufferPhase2);
    releaseBufferBacking(m_swPageJobCountBufferPhase2Skinned);
    releaseBufferBacking(m_swPageJobIndirectArgsBuffer);
    releaseBufferBacking(m_swPageJobIndirectArgsBufferSkinned);
    releaseBufferBacking(m_swPageJobIndirectArgsBufferPhase2);
    releaseBufferBacking(m_swPageJobIndirectArgsBufferPhase2Skinned);
    releaseBufferBacking(m_swPageJobClusterTagsBuffer);
    releaseBufferBacking(m_swPageJobClusterTagsBufferPhase2);
    releaseBufferBacking(m_vsmExpandedVisibleClustersBuffer);
    releaseBufferBacking(m_vsmExpandedBlockMetaBuffer);
    releaseBufferBacking(m_vsmExpandedVisibleClustersBufferSw);
    releaseBufferBacking(m_vsmExpandedBlockMetaBufferSw);
    m_shadowVirtualResourcesNeedReset = true;
}

void CLodExtension::SyncReyesResourceEntities(bool enabled)
{
    if (!m_reyesFullClusterOutputsBuffer) {
        return;
    }

    const auto& traits = GetVariantTraits(m_type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    SyncTaggedBufferEntity<CLodReyesFullClustersCounterTag>(m_reyesFullClusterOutputsCounterBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueCounterATag>(m_reyesSplitQueueCounterBufferA, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueOverflowATag>(m_reyesSplitQueueOverflowBufferA, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueCounterBTag>(m_reyesSplitQueueCounterBufferB, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesSplitQueueOverflowBTag>(m_reyesSplitQueueOverflowBufferB, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesDiceQueueTag>(m_reyesDiceQueueBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTessTableConfigsTag>(m_reyesTessTableConfigsBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTessTableVerticesTag>(m_reyesTessTableVerticesBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTessTableTrianglesTag>(m_reyesTessTableTrianglesBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesDiceQueueCounterTag>(m_reyesDiceQueueCounterBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesDiceQueueOverflowTag>(m_reyesDiceQueueOverflowBuffer, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTelemetryBufferPhase1Tag>(m_reyesTelemetryBufferPhase1, typeEntity, enabled);
    SyncTaggedBufferEntity<CLodReyesTelemetryBufferPhase2Tag>(m_reyesTelemetryBufferPhase2, typeEntity, enabled);
}

void CLodExtension::EnsureReyesResourcesInitialized()
{
    if (m_reyesFullClusterOutputsBuffer) {
        SyncReyesResourceEntities(true);
        return;
    }

    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };

    const auto& traits = GetVariantTraits(m_type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);
    const ReyesResourceSizing reyesResourceSizing = BuildReyesResourceSizing(traits, m_maxVisibleClusters);
    const bool usesPhase2ReyesResources = traits.usesPhase2OcclusionReplay;
    m_reyesFullClusterOutputCapacity = reyesResourceSizing.fullClusterOutputCapacity;
    m_reyesOwnedClusterCapacity = reyesResourceSizing.ownedClusterCapacity;
    m_reyesSplitQueueCapacity = reyesResourceSizing.splitQueueCapacity;
    m_reyesDiceQueueCapacity = reyesResourceSizing.diceQueueCapacity;
    m_reyesDiceQueuePhysicalCapacity = reyesResourceSizing.diceQueuePhysicalCapacity;
    m_reyesRasterWorkCapacity = reyesResourceSizing.rasterWorkCapacity;
    m_reyesOwnershipBitsetWordCount = reyesResourceSizing.ownershipBitsetWordCount;
    m_reyesRequestedBudgetBytes = reyesResourceSizing.requestedBudgetBytes;
    m_reyesAllocatedBudgetBytes = reyesResourceSizing.allocatedBudgetBytes;
    m_reyesBudgetLimited = reyesResourceSizing.budgetLimited;

    m_reyesFullClusterOutputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesFullClusterOutputCapacity, sizeof(CLodReyesFullClusterOutput), true, false, false, true);
    m_reyesFullClusterOutputsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Full Cluster Outputs Buffer"));

    m_reyesFullClusterOutputsCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesFullClusterOutputsCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Full Cluster Outputs Counter Buffer"));
    m_reyesFullClusterOutputsCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesFullClusterOutputsCounterBuffer })
        .add<CLodReyesFullClustersCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesOwnedClustersBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesOwnedClusterCapacity, sizeof(CLodReyesOwnedClusterEntry), true, false, false, true);
    m_reyesOwnedClustersBuffer->SetName(MakeVariantResourceName(traits, "Reyes Owned Clusters Buffer"));

    m_reyesOwnedClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesOwnedClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Owned Clusters Counter Buffer"));

    m_reyesOwnershipBitsetBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesOwnershipBitsetWordCount, sizeof(uint32_t), true, false, false, true);
    m_reyesOwnershipBitsetBuffer->SetName(MakeVariantResourceName(traits, "Reyes Ownership Bitset Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesOwnershipBitsetBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(m_reyesOwnershipBitsetWordCount, sizeof(uint32_t), true, false, false, true);
        m_reyesOwnershipBitsetBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Ownership Bitset Buffer Phase2"));
    }

    m_reyesClassifyIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
    m_reyesClassifyIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Classify Indirect Args Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesClassifyIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
        m_reyesClassifyIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Classify Indirect Args Buffer Phase2"));
    }

    m_reyesSplitIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
    m_reyesSplitIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Split Indirect Args Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesSplitIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
        m_reyesSplitIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Split Indirect Args Buffer Phase2"));
    }

    m_reyesSplitQueueBufferA = CreateAliasedUnmaterializedStructuredBuffer(m_reyesSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry), true, false, true);
    m_reyesSplitQueueBufferA->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Buffer A"));

    m_reyesSplitQueueCounterBufferA = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesSplitQueueCounterBufferA->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Counter Buffer A"));
    m_reyesSplitQueueCounterBufferA->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueCounterBufferA })
        .add<CLodReyesSplitQueueCounterATag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesSplitQueueOverflowBufferA = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesSplitQueueOverflowBufferA->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Overflow Buffer A"));
    m_reyesSplitQueueOverflowBufferA->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueOverflowBufferA })
        .add<CLodReyesSplitQueueOverflowATag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesSplitQueueBufferB = CreateAliasedUnmaterializedStructuredBuffer(m_reyesSplitQueueCapacity, sizeof(CLodReyesSplitQueueEntry), true, false, true);
    m_reyesSplitQueueBufferB->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Buffer B"));

    m_reyesSplitQueueCounterBufferB = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesSplitQueueCounterBufferB->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Counter Buffer B"));
    m_reyesSplitQueueCounterBufferB->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueCounterBufferB })
        .add<CLodReyesSplitQueueCounterBTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesSplitQueueOverflowBufferB = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesSplitQueueOverflowBufferB->SetName(MakeVariantResourceName(traits, "Reyes Split Queue Overflow Buffer B"));
    m_reyesSplitQueueOverflowBufferB->GetECSEntity()
        .set<Components::Resource>({ m_reyesSplitQueueOverflowBufferB })
        .add<CLodReyesSplitQueueOverflowBTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesDiceQueueBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesDiceQueuePhysicalCapacity, sizeof(CLodReyesDiceQueueEntry), true, false, true);
    m_reyesDiceQueueBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Buffer"));
    m_reyesDiceQueueBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesDiceQueueBuffer })
        .add<CLodReyesDiceQueueTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    const ReyesTessellationTableData& reyesTessellationTableData = GetReyesTessellationTableData();
    m_reyesTessTableConfigsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        static_cast<uint32_t>(reyesTessellationTableData.configs.size()),
        sizeof(CLodReyesTessTableConfigEntry),
        false,
        false,
        false,
        false);
    m_reyesTessTableConfigsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Tess Table Configs Buffer"));
    m_reyesTessTableConfigsBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesTessTableConfigsBuffer })
        .add<CLodReyesTessTableConfigsTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesTessTableVerticesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        static_cast<uint32_t>(reyesTessellationTableData.vertices.size()),
        sizeof(uint32_t),
        false,
        false,
        false,
        false);
    m_reyesTessTableVerticesBuffer->SetName(MakeVariantResourceName(traits, "Reyes Tess Table Vertices Buffer"));
    m_reyesTessTableVerticesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesTessTableVerticesBuffer })
        .add<CLodReyesTessTableVerticesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesTessTableTrianglesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        static_cast<uint32_t>(reyesTessellationTableData.triangles.size()),
        sizeof(uint32_t),
        false,
        false,
        false,
        false);
    m_reyesTessTableTrianglesBuffer->SetName(MakeVariantResourceName(traits, "Reyes Tess Table Triangles Buffer"));
    m_reyesTessTableTrianglesBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesTessTableTrianglesBuffer })
        .add<CLodReyesTessTableTrianglesTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesDiceQueueCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesDiceQueueCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Counter Buffer"));
    m_reyesDiceQueueCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesDiceQueueCounterBuffer })
        .add<CLodReyesDiceQueueCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    if (usesPhase2ReyesResources) {
        m_reyesDiceQueuePhase1CountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), false, false, false);
        m_reyesDiceQueuePhase1CountBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Phase1 Count Buffer"));
    }

    m_reyesDiceQueueOverflowBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesDiceQueueOverflowBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Queue Overflow Buffer"));
    m_reyesDiceQueueOverflowBuffer->GetECSEntity()
        .set<Components::Resource>({ m_reyesDiceQueueOverflowBuffer })
        .add<CLodReyesDiceQueueOverflowTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_reyesRasterWorkBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry), true, false, false, true);
    m_reyesRasterWorkBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Buffer"));

    m_reyesRasterWorkCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
    m_reyesRasterWorkCounterBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Counter Buffer"));

    m_reyesRasterWorkIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
    m_reyesRasterWorkIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Indirect Args Buffer"));

    m_reyesCompactedRasterWorkIndicesBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(uint32_t), true, false, false, true);
    m_reyesCompactedRasterWorkIndicesBuffer->SetName(MakeVariantResourceName(traits, "Reyes Compacted Raster Work Indices Buffer"));

    m_reyesPackedRasterWorkGroupsBuffer = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(CLodReyesPackedRasterWorkGroupEntry), true, false, false, true);
    m_reyesPackedRasterWorkGroupsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Packed Raster Work Groups Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesRasterWorkBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(CLodReyesRasterWorkEntry), true, false, false, true);
        m_reyesRasterWorkBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Buffer Phase2"));

        m_reyesRasterWorkCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false);
        m_reyesRasterWorkCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Counter Buffer Phase2"));

        m_reyesRasterWorkIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
        m_reyesRasterWorkIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Raster Work Indirect Args Buffer Phase2"));

        m_reyesCompactedRasterWorkIndicesBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(uint32_t), true, false, false, true);
        m_reyesCompactedRasterWorkIndicesBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Compacted Raster Work Indices Buffer Phase2"));

        m_reyesPackedRasterWorkGroupsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(m_reyesRasterWorkCapacity, sizeof(CLodReyesPackedRasterWorkGroupEntry), true, false, false, true);
        m_reyesPackedRasterWorkGroupsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Packed Raster Work Groups Buffer Phase2"));
    }

    m_reyesDiceIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
    m_reyesDiceIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Reyes Dice Indirect Args Buffer"));

    if (usesPhase2ReyesResources) {
        m_reyesDiceIndirectArgsBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesDispatchIndirectCommand), true, false, false);
        m_reyesDiceIndirectArgsBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Dice Indirect Args Buffer Phase2"));
    }

    m_reyesTelemetryBufferPhase1 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesTelemetry), true, false, false, true);
    m_reyesTelemetryBufferPhase1->SetName(MakeVariantResourceName(traits, "Reyes Telemetry Buffer Phase1"));
    m_reyesTelemetryBufferPhase1->GetECSEntity()
        .set<Components::Resource>({ m_reyesTelemetryBufferPhase1 })
        .add<CLodReyesTelemetryBufferPhase1Tag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    if (usesPhase2ReyesResources) {
        m_reyesTelemetryBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReyesTelemetry), true, false, false, true);
        m_reyesTelemetryBufferPhase2->SetName(MakeVariantResourceName(traits, "Reyes Telemetry Buffer Phase2"));
        m_reyesTelemetryBufferPhase2->GetECSEntity()
            .set<Components::Resource>({ m_reyesTelemetryBufferPhase2 })
            .add<CLodReyesTelemetryBufferPhase2Tag>()
            .add<CLodExtensionTypeTag>(typeEntity);
    }

    tagBufferUsage(m_reyesFullClusterOutputsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesFullClusterOutputsCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnedClustersBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnedClustersCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnershipBitsetBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesOwnershipBitsetBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesClassifyIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesClassifyIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueBufferA, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueCounterBufferA, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueOverflowBufferA, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueBufferB, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueCounterBufferB, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesSplitQueueOverflowBufferB, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceQueueBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTessTableConfigsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTessTableVerticesBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTessTableTrianglesBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceQueueCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceQueuePhase1CountBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceQueueOverflowBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkCounterBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesCompactedRasterWorkIndicesBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesPackedRasterWorkGroupsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkCounterBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesRasterWorkIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesCompactedRasterWorkIndicesBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesPackedRasterWorkGroupsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceIndirectArgsBuffer, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesDiceIndirectArgsBufferPhase2, "Cluster LOD Reyes");
    tagBufferUsage(m_reyesTelemetryBufferPhase1, "Cluster LOD telemetry");
    tagBufferUsage(m_reyesTelemetryBufferPhase2, "Cluster LOD telemetry");
}

CLodExtension::CLodExtension(CLodExtensionType type, uint32_t maxVisibleClusters)
    : m_type(type)
    , m_maxVisibleClusters(maxVisibleClusters) {
    const auto& traits = GetVariantTraits(type);

    if (traits.ownsStreaming) {
        m_streamingSystem = std::make_unique<CLodStreamingSystem>();
    }

    if (m_type == CLodExtensionType::Shadow) {
        RefreshShadowConfiguredSettings();
    }
    m_visibleClusterCapacity = GetVisibleClusterCapacity();

    InitializeCoreResources();

    if (!IsReyesTessellationDisabled()) {
        EnsureReyesResourcesInitialized();
    }

    InitializeDeepVisibilityResources();
    InitializeAVBOITResources();
    InitializeShadowResources();
    TagCoreResourceUsages();
    TagTransparencyResourceUsages();
    TagShadowResourceUsages();
}

void CLodExtension::RefreshTransparencyResourcesForCurrentSettings()
{
    if (m_type != CLodExtensionType::AlphaBlend) {
        return;
    }

    const auto renderResolution =
        SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    const bool renderResolutionChanged =
        m_transparencyConfiguredRenderWidth != renderResolution.x ||
        m_transparencyConfiguredRenderHeight != renderResolution.y;

    if (!renderResolutionChanged || !m_AVBOITOccupancyTexture) {
        return;
    }

    ReleaseTransparencyResourceBackings();
    InitializeAVBOITResources();
    TagTransparencyResourceUsages();
}

void CLodExtension::RefreshShadowResourcesForCurrentSettings()
{
    if (m_type != CLodExtensionType::Shadow) {
        return;
    }

    const uint32_t previousBackingResolution = m_shadowConfiguredBackingResolution;
    const uint32_t previousMaxPhysicalPageCount = m_shadowConfiguredMaxPhysicalPageCount;
    const uint32_t previousPageJobMaxPages = m_shadowConfiguredPageJobMaxPages;
    const uint32_t previousPageJobRecordCapacity = m_shadowConfiguredPageJobRecordCapacity;
    const uint32_t previousComputeClusterCapacity = m_shadowConfiguredComputeClusterCapacity;
    const uint32_t previousVisibleClusterCapacity = m_visibleClusterCapacity;

    RefreshShadowConfiguredSettings();

    if (m_shadowPhysicalPagesTexture &&
        previousBackingResolution == m_shadowConfiguredBackingResolution &&
        previousMaxPhysicalPageCount == m_shadowConfiguredMaxPhysicalPageCount &&
        previousPageJobMaxPages == m_shadowConfiguredPageJobMaxPages &&
        previousPageJobRecordCapacity == m_shadowConfiguredPageJobRecordCapacity &&
        previousComputeClusterCapacity == m_shadowConfiguredComputeClusterCapacity &&
        previousVisibleClusterCapacity == GetVisibleClusterCapacity()) {
        return;
    }

    RefreshCoreVisibleClusterCapacity();

    ReleaseShadowResourceBackings();
    InitializeShadowResources();
    TagShadowResourceUsages();
    m_shadowVirtualResourcesNeedReset = true;
}

void CLodExtension::PrepareForBuild(RenderGraph& rg)
{
    if (m_type == CLodExtensionType::Shadow && !AreRendererShadowsEnabled()) {
        return;
    }

    RefreshTransparencyResourcesForCurrentSettings();
    RefreshShadowResourcesForCurrentSettings();

    if (m_type == CLodExtensionType::Shadow && !m_providerRegisteredForCurrentRegistry) {
        rg.RegisterProvider(this);
        m_providerRegisteredForCurrentRegistry = true;
    }
}

void CLodExtension::Initialize(RenderGraph& rg)
{
    PrepareForBuild(rg);

    if (m_streamingSystem) {
        m_streamingSystem->Initialize(rg);
    }
}

void CLodExtension::OnRegistryReset(ResourceRegistry* reg)
{
    m_providerRegisteredForCurrentRegistry = false;
    ReleaseBufferBackings();
    ReleaseTransparencyResourceBackings();
    ReleaseShadowResourceBackings();
    m_shadowVirtualResourcesNeedReset = true;

    SyncReyesResourceEntities(!IsReyesTessellationDisabled());

    if (m_streamingSystem) {
        m_streamingSystem->OnRegistryReset(reg);
    }
}

std::shared_ptr<Resource> CLodExtension::ProvideResource(ResourceIdentifier const& key)
{
    if (m_type != CLodExtensionType::Shadow || !AreRendererShadowsEnabled()) {
        return nullptr;
    }

    if (key == Builtin::Shadows::CLodPageTable) {
        return m_shadowPageTableTexture;
    }

    if (key == Builtin::Shadows::CLodPhysicalPages) {
        return m_shadowPhysicalPagesTexture;
    }

    if (key == Builtin::Shadows::CLodClipmapInfo) {
        return m_shadowClipmapInfoBuffer;
    }

    if (key == Builtin::Shadows::CLodCompactMainCamera) {
        return m_shadowCompactMainCameraBuffer;
    }

    if (key == Builtin::Shadows::CLodCompactShadowCameras) {
        return m_shadowCompactShadowCameraBuffer;
    }

    if (key == Builtin::Shadows::CLodDirectionalPageViewInfo) {
        return m_shadowDirectionalPageViewInfoBuffer;
    }

    return nullptr;
}

std::vector<ResourceIdentifier> CLodExtension::GetSupportedKeys()
{
    if (m_type != CLodExtensionType::Shadow || !AreRendererShadowsEnabled()) {
        return {};
    }

    return {
        Builtin::Shadows::CLodPageTable,
        Builtin::Shadows::CLodPhysicalPages,
        Builtin::Shadows::CLodClipmapInfo,
        Builtin::Shadows::CLodCompactMainCamera,
        Builtin::Shadows::CLodCompactShadowCameras,
        Builtin::Shadows::CLodDirectionalPageViewInfo,
    };
}

void CLodExtension::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    PrepareForBuild(rg);

    if (m_type == CLodExtensionType::Shadow && !AreRendererShadowsEnabled()) {
        return;
    }

    const auto& traits = GetVariantTraits(m_type);
    if (m_streamingSystem) {
        m_streamingSystem->GatherStructuralPasses(rg, outPasses);
    }

    if (!traits.schedulesStructuralPasses) {
        return;
    }

    const size_t techniqueTaggedPassStart = outPasses.size();
    const auto applyTechniqueTags = [&]() {
        for (size_t passIndex = techniqueTaggedPassStart; passIndex < outPasses.size(); ++passIndex) {
            auto& passDesc = outPasses[passIndex];
            if (passDesc.techniquePath.empty()) {
                passDesc.Technique(GetVariantTechniquePath(traits, passDesc.name));
            }
        }
    };

    const auto softwareRasterMode =
        SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)();
    const auto cullingBackendMode =
        SettingsManager::GetInstance().getSettingGetter<CLodCullingBackend>(CLodCullingBackendSettingName)();
    const auto shadowVSMRasterMode =
        traits.type == CLodExtensionType::Shadow
            ? SettingsManager::GetInstance().getSettingGetter<CLodVSMRasterMode>(CLodVSMRasterModeSettingName)()
            : CLodVSMRasterMode::Standard;
    const auto transparencyMode = GetTransparencyMode(m_type);
    const bool disableReyesTessellation =
        SettingsManager::GetInstance().getSettingGetter<bool>(CLodDisableReyesRasterizationSettingName)();
    if (!disableReyesTessellation) {
        EnsureReyesResourcesInitialized();
    }
    const bool forceHardwareOnly =
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly ||
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility ||
        (traits.type == CLodExtensionType::Shadow && CLodVSMRasterModeUsesLegacyRasterOnly(shadowVSMRasterMode));
    const bool useComputeSWRaster = !forceHardwareOnly && CLodSoftwareRasterUsesCompute(softwareRasterMode);
    const bool useShadowPageJob =
        traits.type == CLodExtensionType::Shadow && CLodVSMRasterModeUsesLargeClusterPageJob(shadowVSMRasterMode);
    const bool useShadowReyesRouting =
        traits.type == CLodExtensionType::Shadow && CLodVSMRasterModeUsesReyes(shadowVSMRasterMode);
    const bool useReyesForThisVariant =
        !disableReyesTessellation && (traits.type != CLodExtensionType::Shadow || useShadowReyesRouting);
    SyncReyesResourceEntities(useReyesForThisVariant);
    const uint32_t reyesSplitQueueCapacity = m_reyesSplitQueueCapacity;
    const uint32_t reyesDiceQueueCapacity = m_reyesDiceQueueCapacity;
    const uint32_t reyesRasterWorkCapacity = m_reyesRasterWorkCapacity;
    const auto workGraphMode = forceHardwareOnly
        ? HierarchicalCullingWorkGraphMode::HardwareOnly
        : GetCullingWorkGraphMode(softwareRasterMode);
    const auto cullingBackend = forceHardwareOnly
        ? HierarchicalCullingBackend::WorkGraph
        : GetHierarchicalCullingBackend(cullingBackendMode);
    const auto renderPhase = RenderPhase(traits.renderPhaseName.data());
    const std::shared_ptr<Buffer> reyesOwnershipBitsetBuffer = useReyesForThisVariant ? m_reyesOwnershipBitsetBuffer : nullptr;
    const std::shared_ptr<Buffer> reyesOwnershipBitsetBufferPhase2 = useReyesForThisVariant ? m_reyesOwnershipBitsetBufferPhase2 : nullptr;

    const auto makeTransparentTailInsertPoint = []() {
        auto insertPoint = RenderGraph::ExternalInsertPoint::After("LightCullingPass");
        insertPoint.after.push_back("CLodShadow::VirtualShadowDeduplicatePredictedPagesPass");
        insertPoint.before.push_back("Screen-Space Reflections Pass");
        insertPoint.before.push_back("UpscalingPass");
        insertPoint.before.push_back("luminanceHistogramPass");
        return insertPoint;
    };

    const auto makeTransparentCompositeInsertPoint = []() {
        auto insertPoint = RenderGraph::ExternalInsertPoint::After("Specular IBL & SSR Composite Pass");
        insertPoint.after.push_back("SkyboxPass");
        insertPoint.after.push_back("Forward render pass");
        insertPoint.after.push_back("Screen-Space Reflections Pass");
        insertPoint.before.push_back("DebugGridPass");
        insertPoint.before.push_back("UpscalingPass");
        insertPoint.before.push_back("luminanceHistogramPass");
        insertPoint.before.push_back("TonemappingPass");
        return insertPoint;
    };

    const auto makeShadowTailInsertPoint = [](const std::string& afterPassName) {
        auto insertPoint = RenderGraph::ExternalInsertPoint::After(afterPassName);
        insertPoint.before.push_back("Screen-Space Reflections Pass");
        insertPoint.before.push_back("luminanceHistogramPass");
        return insertPoint;
    };

    std::string shadowAllocationPassName;
    std::string shadowDirtyHierarchyPassName;
    std::string shadowNonRasterableHierarchyPassName;
    std::string shadowClearDirtyBitsAfterPassName;
    if (traits.type == CLodExtensionType::Shadow) {
        const std::string shadowSetupPassName = MakeVariantPassName(traits, "VirtualShadowSetupPass");
        auto shadowSetupPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowSetupPassName,
            std::make_shared<VirtualShadowMapSetupPass>(
                m_shadowPageTableTexture,
                m_shadowPageMetadataBuffer,
                m_shadowAllocationCountBuffer,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowMarkClipmapDataBuffer,
                m_shadowCompactMainCameraBuffer,
                m_shadowCompactShadowCameraBuffer,
                m_shadowStatsBuffer,
                m_shadowRuntimeStateBuffer,
                m_shadowPredictiveInvalidationCandidateCountBuffer,
                m_shadowPredictiveRawPageCountBuffer,
                m_shadowPredictedInvalidationPageCountBufferA,
                m_shadowVirtualResourcesNeedReset));
        shadowSetupPassDesc.At(RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass"));
        outPasses.push_back(std::move(shadowSetupPassDesc));
        m_shadowVirtualResourcesNeedReset = false;

        const std::string shadowFreeWrappedPagesPassName = MakeVariantPassName(traits, "VirtualShadowFreeWrappedPagesPass");
        auto shadowFreeWrappedPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowFreeWrappedPagesPassName,
            std::make_shared<VirtualShadowMapFreeWrappedPagesPass>(
                m_shadowPageTableTexture,
                m_shadowPageMetadataBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowStatsBuffer));
        shadowFreeWrappedPagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowSetupPassName));
        outPasses.push_back(std::move(shadowFreeWrappedPagesPassDesc));

        const std::string shadowConsumePredictedPagesPassName = MakeVariantPassName(traits, "VirtualShadowConsumePredictedPagesPass");
        auto shadowConsumePredictedPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowConsumePredictedPagesPassName,
            std::make_shared<VirtualShadowMapConsumePredictedPagesPass>(
                m_shadowPredictedInvalidationPagesBufferA,
                m_shadowPredictedInvalidationPageCountBufferA,
                m_shadowClipmapInfoBuffer,
                m_shadowPageTableTexture,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowPageMetadataBuffer,
                m_shadowDirectionalPageViewInfoBuffer,
                m_shadowStatsBuffer));
        shadowConsumePredictedPagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowFreeWrappedPagesPassName));
        outPasses.push_back(std::move(shadowConsumePredictedPagesPassDesc));

        const std::string shadowInvalidatePagesPassName = MakeVariantPassName(traits, "VirtualShadowInvalidatePagesPass");
        auto shadowInvalidatePagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowInvalidatePagesPassName,
            std::make_shared<VirtualShadowMapInvalidatePagesPass>(
                m_shadowInvalidationInputsBuffer,
                m_shadowInvalidationCountBuffer,
                m_shadowInvalidatedInstancesBitsetBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowPageTableTexture,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowPageMetadataBuffer,
                m_shadowDirectionalPageViewInfoBuffer,
                m_shadowStatsBuffer));
        shadowInvalidatePagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowConsumePredictedPagesPassName));
        outPasses.push_back(std::move(shadowInvalidatePagesPassDesc));

        const std::string shadowMarkPagesPassName = MakeVariantPassName(traits, "VirtualShadowMarkPagesPass");
        const std::string shadowBuildMarkTilesPassName = MakeVariantPassName(traits, "VirtualShadowBuildMarkTilesPass");
        auto shadowBuildMarkTilesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowBuildMarkTilesPassName,
            std::make_shared<VirtualShadowMapBuildMarkTilesPass>(
                m_shadowMarkTileWorkBuffer,
                m_shadowMarkTileCountBuffer));
        shadowBuildMarkTilesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowInvalidatePagesPassName));
        outPasses.push_back(std::move(shadowBuildMarkTilesPassDesc));

        const std::string shadowBuildMarkTileDispatchArgsPassName = MakeVariantPassName(traits, "VirtualShadowBuildMarkTileDispatchArgsPass");
        auto shadowBuildMarkTileDispatchArgsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowBuildMarkTileDispatchArgsPassName,
            std::make_shared<ReyesCreateDispatchArgsPass>(
                m_shadowMarkTileCountBuffer,
                m_shadowMarkTileIndirectArgsBuffer,
                nullptr,
                128u,
                CLodVirtualShadowMaxMarkTileCount));
        shadowBuildMarkTileDispatchArgsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowBuildMarkTilesPassName));
        outPasses.push_back(std::move(shadowBuildMarkTileDispatchArgsPassDesc));

        auto shadowMarkPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowMarkPagesPassName,
            std::make_shared<VirtualShadowMapMarkPagesPass>(
                m_shadowMarkTileWorkBuffer,
                m_shadowMarkTileCountBuffer,
                m_shadowMarkTileIndirectArgsBuffer,
                m_shadowMarkClipmapDataBuffer,
                m_shadowMarkedBlocksMaskBuffer,
                m_shadowMarkedBlocksListBuffer,
                m_shadowMarkedBlocksCountBuffer));
        shadowMarkPagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowBuildMarkTileDispatchArgsPassName));
			shadowMarkPagesPassDesc.preferredQueueKind = QueueKind::Graphics;
        outPasses.push_back(std::move(shadowMarkPagesPassDesc));

        const std::string shadowResolveMarkedBlocksPassName = MakeVariantPassName(traits, "VirtualShadowResolveMarkedBlocksPass");
        auto shadowResolveMarkedBlocksPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowResolveMarkedBlocksPassName,
            std::make_shared<VirtualShadowMapResolveMarkedBlocksPass>(
                m_shadowMarkedBlocksMaskBuffer,
                m_shadowMarkedBlocksListBuffer,
                m_shadowMarkedBlocksCountBuffer,
                m_shadowAllocationRequestsBuffer,
                m_shadowAllocationCountBuffer,
                m_shadowMarkClipmapDataBuffer,
                m_shadowPageTableTexture,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowDirectionalPageViewInfoBuffer,
                m_shadowStatsBuffer));
        shadowResolveMarkedBlocksPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowMarkPagesPassName));
        shadowResolveMarkedBlocksPassDesc.preferredQueueKind = QueueKind::Graphics;
        outPasses.push_back(std::move(shadowResolveMarkedBlocksPassDesc));

        const std::string shadowBuildPageListsPassName = MakeVariantPassName(traits, "VirtualShadowBuildPageListsPass");
        auto shadowBuildPageListsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowBuildPageListsPassName,
            std::make_shared<VirtualShadowMapBuildPageListsPass>(
                m_shadowPageTableTexture,
                m_shadowPageMetadataBuffer,
                m_shadowFreePhysicalPagesBuffer,
                m_shadowReusablePhysicalPagesBuffer,
                m_shadowPageListHeaderBuffer));
        shadowBuildPageListsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowResolveMarkedBlocksPassName));
        outPasses.push_back(std::move(shadowBuildPageListsPassDesc));

        const std::string shadowBuildDispatchArgsPassName = MakeVariantPassName(traits, "VirtualShadowBuildAllocationDispatchArgsPass");
        auto shadowBuildDispatchArgsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowBuildDispatchArgsPassName,
            std::make_shared<ReyesCreateDispatchArgsPass>(
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                nullptr,
                64u,
                m_shadowConfiguredMaxPhysicalPageCount));
        shadowBuildDispatchArgsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowBuildPageListsPassName));
        outPasses.push_back(std::move(shadowBuildDispatchArgsPassDesc));

        const std::string shadowPreAllocateStatsPassName = MakeVariantPassName(traits, "VirtualShadowPreAllocateStatsPass");
        auto shadowPreAllocateStatsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowPreAllocateStatsPassName,
            std::make_shared<VirtualShadowMapGatherStatsPass>(
                m_shadowPageTableTexture,
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                m_shadowPageListHeaderBuffer,
                m_shadowPageMetadataBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowStatsBuffer,
                true));
        shadowPreAllocateStatsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowBuildDispatchArgsPassName));
        outPasses.push_back(std::move(shadowPreAllocateStatsPassDesc));

        shadowAllocationPassName = MakeVariantPassName(traits, "VirtualShadowAllocatePagesPass");
        auto shadowAllocationPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowAllocationPassName,
            std::make_shared<VirtualShadowMapAllocatePagesPass>(
                m_shadowAllocationRequestsBuffer,
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowPageTableTexture,
                m_shadowPageMetadataBuffer,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowFreePhysicalPagesBuffer,
                m_shadowReusablePhysicalPagesBuffer,
                m_shadowDirectionalPageViewInfoBuffer,
                m_shadowPageListHeaderBuffer));
            shadowAllocationPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowPreAllocateStatsPassName));
        outPasses.push_back(std::move(shadowAllocationPassDesc));

        const std::string shadowGatherStatsPassName = MakeVariantPassName(traits, "VirtualShadowGatherStatsPass");
        auto shadowGatherStatsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowGatherStatsPassName,
            std::make_shared<VirtualShadowMapGatherStatsPass>(
                m_shadowPageTableTexture,
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer,
                m_shadowPageListHeaderBuffer,
                m_shadowPageMetadataBuffer,
                m_shadowClipmapInfoBuffer,
                m_shadowStatsBuffer,
                false));
        shadowGatherStatsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowAllocationPassName));
        outPasses.push_back(std::move(shadowGatherStatsPassDesc));

        const std::string shadowClearPagesPassName = MakeVariantPassName(traits, "VirtualShadowClearPagesPass");
        auto shadowClearPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowClearPagesPassName,
            std::make_shared<VirtualShadowMapClearPagesPass>(
                m_shadowPhysicalPagesTexture,
                m_shadowDirtyPageFlagsBuffer,
                m_shadowPageTableTexture,
                m_shadowPageMetadataBuffer));
            shadowClearPagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowGatherStatsPassName));
        outPasses.push_back(std::move(shadowClearPagesPassDesc));

        shadowDirtyHierarchyPassName = MakeVariantPassName(traits, "VirtualShadowDirtyHierarchyPass");
        auto shadowDirtyHierarchyPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowDirtyHierarchyPassName,
            std::make_shared<VirtualShadowMapDirtyHierarchyPass>(
                m_shadowPageTableTexture,
                m_shadowDirtyPageHierarchyTexture,
                m_shadowClipmapInfoBuffer));
        shadowDirtyHierarchyPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowClearPagesPassName));
        outPasses.push_back(std::move(shadowDirtyHierarchyPassDesc));

        shadowNonRasterableHierarchyPassName = MakeVariantPassName(traits, "VirtualShadowNonRasterableHierarchyPass");
        auto shadowNonRasterableHierarchyPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowNonRasterableHierarchyPassName,
            std::make_shared<VirtualShadowMapNonRasterableHierarchyPass>(
                m_shadowPageTableTexture,
                m_shadowNonRasterablePageHierarchyTexture,
                m_shadowClipmapInfoBuffer));
        shadowNonRasterableHierarchyPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowDirtyHierarchyPassName));
        outPasses.push_back(std::move(shadowNonRasterableHierarchyPassDesc));

    }

    std::shared_ptr<ResourceGroup> slabGroup = GetSlabResourceGroup();

    HierarchicalCullingPassInputs cullPassInputs;
    cullPassInputs.isFirstPass = true;
    cullPassInputs.maxVisibleClusters = m_visibleClusterCapacity;
    cullPassInputs.workGraphMode = workGraphMode;
    cullPassInputs.renderPhase = renderPhase;
    cullPassInputs.clodOnlyWorkloads = true;
    cullPassInputs.useShadowCascadeViews = (traits.type == CLodExtensionType::Shadow);
    cullPassInputs.rasterOutputKind = traits.rasterOutputKind;
    const std::string cullPassName = MakeVariantPassName(traits, "HierarchicalCullingPass1");
    const bool useDispatchCullingPass1 = UseHierarchicalDispatchCullingPass(
        cullingBackend,
        cullPassInputs.isFirstPass,
        cullPassInputs.workGraphMode,
        cullPassInputs.rasterOutputKind);
    std::shared_ptr<ComputePass> cullPass1 = useDispatchCullingPass1
        ? std::static_pointer_cast<ComputePass>(
            std::make_shared<HierarchicalDispatchCullingPass>(
                cullPassName,
                cullPassInputs,
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_swVisibleClustersCounterBuffer,
                traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersBuffer : nullptr,
                traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersCounterBuffer : nullptr,
                m_histogramIndirectCommand,
                m_workGraphTelemetryBuffer,
                m_occlusionReplayBuffer,
                m_occlusionReplayStateBuffer,
                m_occlusionNodeGpuInputsBuffer,
                m_viewDepthSrvIndicesBuffer,
                m_viewRasterInfoBuffer,
                traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
                slabGroup,
                nullptr,
                nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidatesBuffer : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidateCountBuffer : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowInvalidatedInstancesBitsetBuffer : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPageTableTexture : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPhysicalPagesTexture : nullptr))
        : std::static_pointer_cast<ComputePass>(
            std::make_shared<HierarchicalCullingPass>(
                cullPassName,
                cullPassInputs,
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_swVisibleClustersCounterBuffer,
                traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersBuffer : nullptr,
                traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersCounterBuffer : nullptr,
                m_histogramIndirectCommand,
                m_workGraphTelemetryBuffer,
                m_occlusionReplayBuffer,
                m_occlusionReplayStateBuffer,
                m_occlusionNodeGpuInputsBuffer,
                m_viewDepthSrvIndicesBuffer,
                m_viewRasterInfoBuffer,
                traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
                slabGroup,
                nullptr,
                nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidatesBuffer : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidateCountBuffer : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowInvalidatedInstancesBitsetBuffer : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPageTableTexture : nullptr,
                traits.type == CLodExtensionType::Shadow ? m_shadowPhysicalPagesTexture : nullptr));
    auto cullPassDesc = RenderGraph::ExternalPassDesc::Compute(
        cullPassName,
        cullPass1);
    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly ||
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        cullPassDesc.At(RenderGraph::ExternalInsertPoint::After("CLodOpaque::LinearDepthDownsamplePass2"));
    }
    else {
        if (traits.type == CLodExtensionType::Shadow) {
            cullPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowNonRasterableHierarchyPassName));
        }
        else {
            cullPassDesc.At(RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass"));
        }
    }
    outPasses.push_back(std::move(cullPassDesc));

        if (traits.scheduleMode != CLodVariantTraits::ScheduleMode::SinglePassCullOnly && useReyesForThisVariant) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesTessellationTableUploadPass"),
                    std::make_shared<ReyesTessellationTableUploadPass>(
                        m_reyesTessTableConfigsBuffer,
                        m_reyesTessTableVerticesBuffer,
                        m_reyesTessTableTrianglesBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesQueueResetPass1"),
                    std::make_shared<ReyesQueueResetPass>(
                        m_reyesFullClusterOutputsCounterBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
                        std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceQueueOverflowBuffer,
                        reyesOwnershipBitsetBuffer,
                        m_reyesTelemetryBufferPhase1,
                        1u,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateClassifyDispatchArgsPass1"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_visibleClustersCounterBuffer,
                        m_reyesClassifyIndirectArgsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesClassifyPass1"),
                    std::make_shared<ReyesClassifyPass>(
                        m_visibleClustersBuffer,
                        m_visibleClustersCounterBuffer,
                        nullptr,
                        m_reyesFullClusterOutputsBuffer,
                        m_reyesFullClusterOutputsCounterBuffer,
                        m_reyesFullClusterOutputCapacity,
                        m_reyesOwnedClustersBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesOwnedClusterCapacity,
                        reyesOwnershipBitsetBuffer,
                        m_reyesClassifyIndirectArgsBuffer,
                        m_reyesTelemetryBufferPhase1,
                        1u,
                        traits.type == CLodExtensionType::Shadow
                            ? ReyesClassifyMode::ShadowFineDisplacedOnly
                            : ReyesClassifyMode::Default)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateSeedDispatchArgsPass1"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesSplitIndirectArgsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesSeedPatchesPass1"),
                    std::make_shared<ReyesSeedPatchesPass>(
                        m_visibleClustersBuffer,
                        m_reyesOwnedClustersBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesSplitQueueBufferA,
                        m_reyesSplitQueueCounterBufferA,
                        m_reyesSplitQueueOverflowBufferA,
                        m_reyesSplitIndirectArgsBuffer,
                        slabGroup,
                        reyesSplitQueueCapacity,
                        1u)));

            const std::shared_ptr<Buffer> reyesSplitBuffers[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
            const std::shared_ptr<Buffer> reyesSplitCounters[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
            const std::shared_ptr<Buffer> reyesSplitOverflows[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
            for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
                const uint32_t inputIndex = splitPassIndex & 1u;
                const uint32_t outputIndex = inputIndex ^ 1u;

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesCreateSplitDispatchArgsPass1_" + std::to_string(splitPassIndex)),
                        std::make_shared<ReyesCreateDispatchArgsPass>(
                            reyesSplitCounters[inputIndex],
                            m_reyesSplitIndirectArgsBuffer)));

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesSplitPass1_" + std::to_string(splitPassIndex)),
                        std::make_shared<ReyesSplitPass>(
                            m_visibleClustersBuffer,
                            reyesSplitBuffers[inputIndex],
                            reyesSplitCounters[inputIndex],
                            reyesSplitBuffers[outputIndex],
                            reyesSplitCounters[outputIndex],
                            reyesSplitOverflows[outputIndex],
                            m_reyesDiceQueueBuffer,
                            m_reyesDiceQueueCounterBuffer,
                            m_reyesDiceQueueOverflowBuffer,
                            m_reyesTessTableConfigsBuffer,
                            m_reyesTessTableVerticesBuffer,
                            m_reyesTessTableTrianglesBuffer,
                            traits.type == CLodExtensionType::Shadow ? m_shadowClipmapInfoBuffer : nullptr,
                            traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
                            traits.type == CLodExtensionType::Shadow ? m_shadowNonRasterablePageHierarchyTexture : nullptr,
                            m_reyesSplitIndirectArgsBuffer,
                            m_reyesTelemetryBufferPhase1,
                            reyesSplitQueueCapacity,
                            splitPassIndex,
                            CLodReyesMaxSplitPassCount,
                            1u)));
            }

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateDiceDispatchArgsPass1"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceIndirectArgsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesDicePass1"),
                    std::make_shared<ReyesDicePass>(
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        nullptr,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesDiceIndirectArgsBuffer,
                        m_reyesTelemetryBufferPhase1,
                        reyesDiceQueueCapacity,
                        1u)));

            if (traits.usesPhase2OcclusionReplay) {
                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Copy(
                        MakeVariantPassName(traits, "ReyesCopyDiceCountPass1"),
                        std::make_shared<ReyesCopyCounterPass>(
                            m_reyesDiceQueueCounterBuffer,
                            m_reyesDiceQueuePhase1CountBuffer)));
            }

            if (traits.type == CLodExtensionType::VisiblityBuffer ||
                traits.type == CLodExtensionType::AlphaBlend ||
                traits.type == CLodExtensionType::Shadow) {
                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesBuildRasterWorkPass1"),
                        std::make_shared<ReyesBuildRasterWorkPass>(
                            m_reyesDiceQueueBuffer,
                            m_reyesDiceQueueCounterBuffer,
                            nullptr,
                            m_reyesTessTableConfigsBuffer,
                            m_reyesRasterWorkBuffer,
                            m_reyesRasterWorkCounterBuffer,
                            m_reyesDiceIndirectArgsBuffer,
                            m_reyesTelemetryBufferPhase1,
                            reyesRasterWorkCapacity)));

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesCreateRasterWorkDispatchArgsPass1"),
                        std::make_shared<ReyesCreateDispatchArgsPass>(
                            m_reyesRasterWorkCounterBuffer,
                            m_reyesRasterWorkIndirectArgsBuffer)));

                if (traits.type == CLodExtensionType::VisiblityBuffer) {
                    outPasses.push_back(
                        RenderGraph::ExternalPassDesc::Compute(
                            MakeVariantPassName(traits, "ReyesPatchRasterPass1"),
                            std::make_shared<ReyesPatchRasterizationPass>(
                                m_visibleClustersBuffer,
                                m_reyesDiceQueueBuffer,
                                m_reyesDiceQueueCounterBuffer,
                                m_reyesRasterWorkBuffer,
                                m_reyesRasterWorkCounterBuffer,
                                m_reyesTessTableConfigsBuffer,
                                m_reyesTessTableVerticesBuffer,
                                m_reyesTessTableTrianglesBuffer,
                                m_viewRasterInfoBuffer,
                                m_reyesRasterWorkIndirectArgsBuffer,
                                m_reyesTelemetryBufferPhase1,
                                slabGroup,
                                m_maxVisibleClusters,
                                1u,
                                CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters))));
                }
                else if (traits.type == CLodExtensionType::Shadow) {
                    const std::string reyesShadowRasterPassName = MakeVariantPassName(traits, "ReyesVirtualShadowRasterPass1");
                    outPasses.push_back(
                        RenderGraph::ExternalPassDesc::Compute(
                            reyesShadowRasterPassName,
                            std::make_shared<ReyesVirtualShadowRasterizationPass>(
                                m_visibleClustersBuffer,
                                m_reyesDiceQueueBuffer,
                                m_reyesDiceQueueCounterBuffer,
                                m_reyesRasterWorkBuffer,
                                m_reyesRasterWorkCounterBuffer,
                                m_reyesTessTableConfigsBuffer,
                                m_reyesTessTableVerticesBuffer,
                                m_reyesTessTableTrianglesBuffer,
                                m_reyesRasterWorkIndirectArgsBuffer,
                                m_reyesTelemetryBufferPhase1,
                                m_shadowPageTableTexture,
                                m_shadowPhysicalPagesTexture,
                                m_shadowClipmapInfoBuffer,
                                slabGroup,
                                MakeVariantResourceName(traits, "Reyes Virtual Shadow View Raster Info Buffer"),
                                1u)));
                    shadowClearDirtyBitsAfterPassName = reyesShadowRasterPassName;
                }
            }
        }
        else {
        }

    const std::string rasterBucketsHistogramPassName = MakeVariantPassName(traits, "RasterBucketsHistogramPass1");
    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            rasterBucketsHistogramPassName,
            std::make_shared<RasterBucketHistogramPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_histogramIndirectCommand,
                m_rasterBucketsHistogramBuffer,
                reyesOwnershipBitsetBuffer)));

    const std::string rasterBucketsPrefixScanPassName = MakeVariantPassName(traits, "RasterBucketsPrefixScanPass1");
    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            rasterBucketsPrefixScanPassName,
            std::make_shared<RasterBucketBlockScanPass>(
                m_rasterBucketsHistogramBuffer,
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer)));

    const std::string rasterBucketsPrefixOffsetsPassName = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass1");
    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            rasterBucketsPrefixOffsetsPassName,
            std::make_shared<RasterBucketBlockOffsetsPass>(
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer,
                m_rasterBucketsScannedBlockSumsBuffer,
                m_rasterBucketsTotalCountBufferPhase1)));

    const std::string rasterBucketsCompactAndArgsPassName = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass1");
    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            rasterBucketsCompactAndArgsPassName,
            std::make_shared<RasterBucketCompactAndArgsPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_visibleClustersCounterBuffer,
                m_histogramIndirectCommand,
                m_rasterBucketsHistogramBuffer,
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsWriteCursorBuffer,
                m_compactedVisibleClustersBuffer,
                m_rasterBucketsIndirectArgsBuffer,
                m_sortedToUnsortedMappingBuffer,
                reyesOwnershipBitsetBuffer,
                m_visibleClusterCapacity,
                false)));

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly) {
        applyTechniqueTags();
        return;
    }

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        const bool useAVBOIT = transparencyMode == CLodTransparencyMode::AVBOIT;
        if (transparencyMode == CLodTransparencyMode::AVBOIT) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Render(
                    MakeVariantPassName(traits, kTransparentExtinctionSetupPassName),
                    std::make_shared<AVBOITSetupPass>(
                        m_AVBOITConfigBuffer,
                        m_AVBOITFitStateBuffer,
                        m_AVBOITDepthWarpLUTBuffer,
                        m_AVBOITOccupancyTexture,
                        m_AVBOITCoverageTexture,
                        m_AVBOITOccupancySliceMaskTexture,
                        m_AVBOITExtinctionTexture,
                        m_AVBOITChromaticExtinctionTexture,
                        m_AVBOITIntegratedTransmittanceTexture,
                        m_AVBOITZeroTransmittanceSliceTexture,
                        m_AVBOITAccumulationTexture,
                        m_AVBOITNormalizationTexture,
                        m_AVBOITShadingExtinctionTexture)));

                auto adaptiveFitPassDesc = RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, kTransparentExtinctionAdaptiveFitPassName),
                    std::make_shared<AVBOITAdaptiveFitPass>(
                        m_AVBOITConfigBuffer,
                        m_AVBOITFitStateBuffer));
                adaptiveFitPassDesc.At(RenderGraph::ExternalInsertPoint::After(
                    MakeVariantPassName(traits, kTransparentExtinctionSetupPassName)));
                outPasses.push_back(std::move(adaptiveFitPassDesc));
        }

        if (!useAVBOIT) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Render(
                    MakeVariantPassName(traits, "ClearDeepVisibilityPass"),
                    std::make_shared<ClearDeepVisibilityPass>(
                        m_deepVisibilityCounterBuffer,
                        m_deepVisibilityOverflowCounterBuffer,
                        m_deepVisibilityStatsBuffer)));

            if (useReyesForThisVariant) {
                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesPatchRasterPass1"),
                        std::make_shared<ReyesDeepVisibilityRasterizationPass>(
                            m_visibleClustersBuffer,
                            m_reyesDiceQueueBuffer,
                            m_reyesDiceQueueCounterBuffer,
                            m_reyesRasterWorkBuffer,
                            m_reyesRasterWorkCounterBuffer,
                            m_reyesTessTableConfigsBuffer,
                            m_reyesTessTableVerticesBuffer,
                            m_reyesTessTableTrianglesBuffer,
                            m_reyesRasterWorkIndirectArgsBuffer,
                            m_reyesTelemetryBufferPhase1,
                            m_deepVisibilityNodesBuffer,
                            m_deepVisibilityCounterBuffer,
                            m_deepVisibilityOverflowCounterBuffer,
                            slabGroup,
                            MakeVariantResourceName(traits, "Reyes Deep Visibility View Raster Info Buffer"),
                            CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters))));
            }
        }

        if (useAVBOIT) {
            ClusterRasterizationPassInputs occupancyPassInputs;
            occupancyPassInputs.clearGbuffer = false;
            occupancyPassInputs.wireframe = false;
            occupancyPassInputs.renderPhase = renderPhase;
            occupancyPassInputs.outputKind = CLodRasterOutputKind::AVBOITOccupancy;
            auto occupancyPassDesc = RenderGraph::ExternalPassDesc::Render(
                MakeVariantPassName(traits, kTransparentExtinctionOccupancyPassName),
                std::make_shared<ClusterRasterizationPass>(
                    occupancyPassInputs,
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsHistogramBuffer,
                    m_rasterBucketsIndirectArgsBuffer,
                    m_sortedToUnsortedMappingBuffer,
                    nullptr,
                    nullptr,
                    nullptr,
                    m_AVBOITConfigBuffer,
                    m_AVBOITOccupancyTexture,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    m_visibleClustersBuffer,
                    slabGroup,
                    nullptr,
                    nullptr,
                    nullptr,
                    m_AVBOITOccupancySliceMaskTexture));
            occupancyPassDesc.GeometryPass();
            occupancyPassDesc.At(RenderGraph::ExternalInsertPoint::After(
                MakeVariantPassName(traits, kTransparentExtinctionAdaptiveFitPassName)));
            outPasses.push_back(std::move(occupancyPassDesc));

            auto occupancyHistogramPassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, kTransparentExtinctionOccupancyHistogramPassName),
                std::make_shared<AVBOITOccupancyHistogramPass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITOccupancyTexture,
                    m_AVBOITOccupancySliceMaskTexture,
                    m_AVBOITOccupancyHistogramBuffer));
            occupancyHistogramPassDesc.At(RenderGraph::ExternalInsertPoint::After(
                MakeVariantPassName(traits, kTransparentExtinctionOccupancyPassName)));
            outPasses.push_back(std::move(occupancyHistogramPassDesc));

            auto adaptiveFitUpdatePassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, kTransparentExtinctionAdaptiveFitUpdatePassName),
                std::make_shared<AVBOITAdaptiveFitUpdatePass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITOccupancyHistogramBuffer,
                    m_AVBOITFitStateBuffer));
            adaptiveFitUpdatePassDesc.At(RenderGraph::ExternalInsertPoint::After(
                MakeVariantPassName(traits, kTransparentExtinctionOccupancyHistogramPassName)));
            outPasses.push_back(std::move(adaptiveFitUpdatePassDesc));

            auto depthWarpPassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, kTransparentExtinctionDepthWarpPassName),
                std::make_shared<AVBOITDepthWarpPass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITOccupancyHistogramBuffer,
                    m_AVBOITDepthWarpLUTBuffer));
            depthWarpPassDesc.At(RenderGraph::ExternalInsertPoint::After(
                MakeVariantPassName(traits, kTransparentExtinctionAdaptiveFitUpdatePassName)));
            outPasses.push_back(std::move(depthWarpPassDesc));

            auto occupancyRemapPassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, kTransparentExtinctionOccupancyRemapPassName),
                std::make_shared<AVBOITOccupancyRemapPass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITOccupancyTexture,
                    m_AVBOITOccupancySliceMaskTexture,
                    m_AVBOITDepthWarpLUTBuffer));
            occupancyRemapPassDesc.At(RenderGraph::ExternalInsertPoint::After(
                MakeVariantPassName(traits, kTransparentExtinctionDepthWarpPassName)));
            outPasses.push_back(std::move(occupancyRemapPassDesc));

            auto sparseClearPassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, kTransparentExtinctionSparseClearPassName),
                std::make_shared<AVBOITSparseClearPass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITOccupancyTexture,
                    m_AVBOITOccupancySliceMaskTexture,
                    m_AVBOITExtinctionTexture,
                    m_AVBOITChromaticExtinctionTexture,
                    m_AVBOITZeroTransmittanceSliceTexture));
            sparseClearPassDesc.At(RenderGraph::ExternalInsertPoint::After(
                MakeVariantPassName(traits, kTransparentExtinctionOccupancyRemapPassName)));
            outPasses.push_back(std::move(sparseClearPassDesc));
        }

        ClusterRasterizationPassInputs rasterizePassInputs;
        rasterizePassInputs.clearGbuffer = false;
        rasterizePassInputs.wireframe = false;
        rasterizePassInputs.renderPhase = renderPhase;
        rasterizePassInputs.outputKind = useAVBOIT
            ? CLodRasterOutputKind::AVBOIT
            : traits.rasterOutputKind;
        auto rasterizeDeepVisibilityPassDesc = RenderGraph::ExternalPassDesc::Render(
            MakeVariantPassName(
                traits,
                useAVBOIT
                    ? kTransparentExtinctionCapturePassName
                    : std::string_view("RasterizeClustersPass1")),
            std::make_shared<ClusterRasterizationPass>(
                rasterizePassInputs,
                m_compactedVisibleClustersBuffer,
                m_rasterBucketsHistogramBuffer,
                m_rasterBucketsIndirectArgsBuffer,
                m_sortedToUnsortedMappingBuffer,
                useAVBOIT ? nullptr : m_deepVisibilityNodesBuffer,
                useAVBOIT ? nullptr : m_deepVisibilityCounterBuffer,
                useAVBOIT ? nullptr : m_deepVisibilityOverflowCounterBuffer,
                useAVBOIT ? m_AVBOITConfigBuffer : nullptr,
                useAVBOIT ? m_AVBOITOccupancyTexture : nullptr,
                useAVBOIT ? m_AVBOITExtinctionTexture : nullptr,
                useAVBOIT ? m_AVBOITChromaticExtinctionTexture : nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                useAVBOIT ? m_visibleClustersBuffer : nullptr,
                slabGroup));
        rasterizeDeepVisibilityPassDesc.GeometryPass();
        outPasses.push_back(std::move(rasterizeDeepVisibilityPassDesc));

        if (transparencyMode == CLodTransparencyMode::AVBOIT) {
            auto integratePassDesc = RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, kTransparentTransmittanceIntegratePassName),
                    std::make_shared<AVBOITIntegratePass>(
                        m_AVBOITConfigBuffer,
                        m_AVBOITFitStateBuffer,
                        m_AVBOITOccupancyTexture,
                        m_AVBOITCoverageTexture,
                        m_AVBOITOccupancySliceMaskTexture,
                        m_AVBOITExtinctionTexture,
                        m_AVBOITChromaticExtinctionTexture,
                        m_AVBOITIntegratedTransmittanceTexture,
                        m_AVBOITZeroTransmittanceSliceTexture));
            integratePassDesc.At(makeTransparentTailInsertPoint());
            outPasses.push_back(std::move(integratePassDesc));

            auto earlyDepthBuildPassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, kTransparentVBOITEarlyDepthBuildPassName),
                std::make_shared<AVBOITEarlyDepthBuildPass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITZeroTransmittanceSliceTexture,
                    m_AVBOITEarlyDepthTileCommandsBuffer,
                    m_AVBOITEarlyDepthTileCountBuffer));
            {
                auto insertPoint = makeTransparentTailInsertPoint();
                insertPoint.after.push_back(MakeVariantPassName(traits, kTransparentTransmittanceIntegratePassName));
                earlyDepthBuildPassDesc.At(std::move(insertPoint));
            }
            outPasses.push_back(std::move(earlyDepthBuildPassDesc));

            auto earlyDepthPassDesc = RenderGraph::ExternalPassDesc::Render(
                MakeVariantPassName(traits, kTransparentVBOITEarlyDepthPassName),
                std::make_shared<AVBOITEarlyDepthPass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITEarlyDepthTileCommandsBuffer,
                    m_AVBOITEarlyDepthTileCountBuffer,
                    m_AVBOITEarlyDepthTexture));
            {
                auto insertPoint = makeTransparentTailInsertPoint();
                insertPoint.after.push_back(MakeVariantPassName(traits, kTransparentVBOITEarlyDepthBuildPassName));
                earlyDepthPassDesc.At(std::move(insertPoint));
            }
            outPasses.push_back(std::move(earlyDepthPassDesc));

            ClusterRasterizationPassInputs shadePassInputs;
            shadePassInputs.clearGbuffer = false;
            shadePassInputs.wireframe = false;
            shadePassInputs.renderPhase = renderPhase;
            shadePassInputs.outputKind = CLodRasterOutputKind::AVBOITShading;
            auto shadePassDesc = RenderGraph::ExternalPassDesc::Render(
                MakeVariantPassName(traits, kTransparentVBOITShadePassName),
                std::make_shared<ClusterRasterizationPass>(
                    shadePassInputs,
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsHistogramBuffer,
                    m_rasterBucketsIndirectArgsBuffer,
                    m_sortedToUnsortedMappingBuffer,
                    nullptr,
                    nullptr,
                    nullptr,
                    m_AVBOITConfigBuffer,
                    nullptr,
                    nullptr,
                    nullptr,
                    m_AVBOITIntegratedTransmittanceTexture,
                    m_AVBOITZeroTransmittanceSliceTexture,
                    m_AVBOITAccumulationTexture,
                    m_AVBOITNormalizationTexture,
                    m_AVBOITShadingExtinctionTexture,
                    m_visibleClustersBuffer,
                    slabGroup,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    m_AVBOITEarlyDepthTexture));
            shadePassDesc.GeometryPass();
            {
                auto insertPoint = makeTransparentTailInsertPoint();
                insertPoint.after.push_back(MakeVariantPassName(traits, kTransparentVBOITEarlyDepthPassName));
                shadePassDesc.At(std::move(insertPoint));
            }
            outPasses.push_back(std::move(shadePassDesc));

            auto resolvePassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, kTransparentVBOITResolvePassName),
                std::make_shared<AVBOITResolvePass>(
                    m_AVBOITConfigBuffer,
                    m_AVBOITAccumulationTexture,
                    m_AVBOITNormalizationTexture,
                    m_AVBOITShadingExtinctionTexture));
            {
                auto insertPoint = makeTransparentCompositeInsertPoint();
                insertPoint.after.push_back(MakeVariantPassName(traits, kTransparentVBOITShadePassName));
                resolvePassDesc.At(std::move(insertPoint));
            }
            outPasses.push_back(std::move(resolvePassDesc));
        }

        if (!useAVBOIT) {
            auto resolveDeepVisibilityPassDesc = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "DeepVisibilityResolvePass"),
                std::make_shared<DeepVisibilityResolvePass>(
                    m_visibleClustersBuffer,
                    disableReyesTessellation ? nullptr : m_reyesDiceQueueBuffer,
                    disableReyesTessellation ? nullptr : m_reyesTessTableConfigsBuffer,
                    disableReyesTessellation ? nullptr : m_reyesTessTableVerticesBuffer,
                    disableReyesTessellation ? nullptr : m_reyesTessTableTrianglesBuffer,
                    m_deepVisibilityNodesBuffer,
                    m_deepVisibilityCounterBuffer,
                    m_deepVisibilityOverflowCounterBuffer,
                    m_deepVisibilityStatsBuffer,
                    CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters)));
                    resolveDeepVisibilityPassDesc.At(makeTransparentCompositeInsertPoint());
            outPasses.push_back(std::move(resolveDeepVisibilityPassDesc));
        }
        applyTechniqueTags();
        return;
    }

    ClusterRasterizationPassInputs rasterizePassInputs;
    rasterizePassInputs.clearGbuffer = true;
    rasterizePassInputs.wireframe = false;
    rasterizePassInputs.renderPhase = renderPhase;
    rasterizePassInputs.outputKind = traits.rasterOutputKind;
    auto shadowPageTable = traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_shadowPageTableTexture : nullptr;
    auto shadowPhysicalPages = traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_shadowPhysicalPagesTexture : nullptr;
    auto shadowClipmapInfo = traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_shadowClipmapInfoBuffer : nullptr;
    std::shared_ptr<Buffer> rasterClustersBufferPhase1 = m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> rasterHistogramBufferPhase1 = m_rasterBucketsHistogramBuffer;
    std::shared_ptr<Buffer> rasterIndirectArgsBufferPhase1 = m_rasterBucketsIndirectArgsBuffer;

    auto rasterizePassDesc = RenderGraph::ExternalPassDesc::Render(
        MakeVariantPassName(traits, "RasterizeClustersPass1"),
        std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs,
            rasterClustersBufferPhase1,
            rasterHistogramBufferPhase1,
            rasterIndirectArgsBufferPhase1,
            m_sortedToUnsortedMappingBuffer,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            slabGroup,
            shadowPageTable,
            shadowPhysicalPages,
            shadowClipmapInfo));
    rasterizePassDesc.GeometryPass();
            if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow) {
            shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "RasterizeClustersPass1");
            }
    outPasses.push_back(std::move(rasterizePassDesc));

    if (useComputeSWRaster) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW1"),
                std::make_shared<RasterBucketCreateCommandPass>(
                    m_swVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsHistogramPassSW1"),
                std::make_shared<RasterBucketHistogramPass>(
                    m_visibleClustersBuffer,
                    m_swVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferSw,
                    reyesOwnershipBitsetBuffer,
                    nullptr,
                    true,
                    m_visibleClusterCapacity,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW1"),
                std::make_shared<RasterBucketBlockScanPass>(
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW1"),
                std::make_shared<RasterBucketBlockOffsetsPass>(
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    m_rasterBucketsScannedBlockSumsBuffer,
                    m_rasterBucketsTotalCountBufferPhase1Sw,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW1"),
                std::make_shared<RasterBucketCompactAndArgsPass>(
                    m_visibleClustersBuffer,
                    m_swVisibleClustersCounterBuffer,
                    m_swVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsWriteCursorBufferSw,
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsIndirectArgsBufferSw,
                    m_sortedToUnsortedMappingBuffer,
                    reyesOwnershipBitsetBuffer,
                    m_visibleClusterCapacity,
                    false,
                    true,
                    true,
                    true)));
        std::shared_ptr<Buffer> swRasterClustersBufferPhase1 = m_compactedVisibleClustersBuffer;
        std::shared_ptr<Buffer> swRasterHistogramBufferPhase1 = m_rasterBucketsHistogramBufferSw;
        std::shared_ptr<Buffer> swRasterIndirectArgsBufferPhase1 = m_rasterBucketsIndirectArgsBufferSw;

        if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow) {
            const uint32_t vsmBlockSoftCap = std::max(1u, std::min(m_shadowConfiguredPageJobMaxPages, CLodVirtualShadowBlockMaxTrackedPerCluster));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockHistogramPassSW1"),
                    std::make_shared<VirtualShadowBlockExpandPass>(
                        VirtualShadowBlockExpandMode::Histogram,
                        m_compactedVisibleClustersBuffer,
                        m_rasterBucketsHistogramBufferSw,
                        m_rasterBucketsIndirectArgsBufferSw,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        m_shadowPageTableTexture,
                        m_shadowClipmapInfoBuffer,
                        m_shadowConfiguredExpandedRecordCapacity,
                        vsmBlockSoftCap,
                        slabGroup,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockPrefixScanPassSW1"),
                    std::make_shared<RasterBucketBlockScanPass>(
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsBlockSumsBuffer,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockPrefixOffsetsPassSW1"),
                    std::make_shared<RasterBucketBlockOffsetsPass>(
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsBlockSumsBuffer,
                        m_rasterBucketsScannedBlockSumsBuffer,
                        m_rasterBucketsTotalCountBuffer,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockEmitPassSW1"),
                    std::make_shared<VirtualShadowBlockExpandPass>(
                        VirtualShadowBlockExpandMode::Emit,
                        m_compactedVisibleClustersBuffer,
                        m_rasterBucketsHistogramBufferSw,
                        m_rasterBucketsIndirectArgsBufferSw,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsWriteCursorBufferPhase2Sw,
                        m_vsmExpandedVisibleClustersBufferSw,
                        m_vsmExpandedBlockMetaBufferSw,
                        m_shadowPageTableTexture,
                        m_shadowClipmapInfoBuffer,
                        m_shadowConfiguredExpandedRecordCapacity,
                        vsmBlockSoftCap,
                        slabGroup,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBuildArgsPassSW1"),
                    std::make_shared<VirtualShadowBuildRasterArgsPass>(
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsIndirectArgsBufferPhase2Sw,
                        true)));

            swRasterClustersBufferPhase1 = m_vsmExpandedVisibleClustersBufferSw;
            swRasterHistogramBufferPhase1 = m_rasterBucketsHistogramBufferPhase2Sw;
            swRasterIndirectArgsBufferPhase1 = m_rasterBucketsIndirectArgsBufferPhase2Sw;
        }

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "SoftwareRasterizeClustersPass1"),
                std::make_shared<ClusterSoftwareRasterizationPass>(
                    swRasterClustersBufferPhase1,
                    swRasterHistogramBufferPhase1,
                    swRasterIndirectArgsBufferPhase1,
                    m_sortedToUnsortedMappingBuffer,
                    m_viewRasterInfoBuffer,
                    traits.rasterOutputKind,
                    m_shadowPageTableTexture,
                    m_shadowPhysicalPagesTexture,
                    m_shadowClipmapInfoBuffer,
                    slabGroup,
                    true)));

        if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow) {
            shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass1");
        }
    }

    if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow && useShadowPageJob) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCreateCommandPassPageJob1"),
                std::make_shared<RasterBucketCreateCommandPass>(
                    m_swPageJobVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsHistogramPassPageJob1"),
                std::make_shared<RasterBucketHistogramPass>(
                    m_swPageJobVisibleClustersBuffer,
                    m_swPageJobVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferSw,
                    nullptr,
                    nullptr,
                    false,
                    m_maxVisibleClusters)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixScanPassPageJob1"),
                std::make_shared<RasterBucketBlockScanPass>(
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassPageJob1"),
                std::make_shared<RasterBucketBlockOffsetsPass>(
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    m_rasterBucketsScannedBlockSumsBuffer,
                    m_rasterBucketsTotalCountBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassPageJob1"),
                std::make_shared<RasterBucketCompactAndArgsPass>(
                    m_swPageJobVisibleClustersBuffer,
                    m_swPageJobVisibleClustersCounterBuffer,
                    m_swPageJobVisibleClustersCounterBuffer,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsWriteCursorBufferSw,
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsIndirectArgsBufferPageJob,
                    m_sortedToUnsortedMappingBuffer,
                    nullptr,
                    m_maxVisibleClusters,
                    false,
                    false,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "SoftwareRasterPageJobExpandPass1"),
                std::make_shared<ClusterSoftwareRasterPageJobExpandPass>(
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsIndirectArgsBufferPageJob,
                    m_viewRasterInfoBuffer,
                    m_shadowPageTableTexture,
                    m_shadowClipmapInfoBuffer,
                    m_swPageJobRecordsBuffer,
                    m_swPageJobCountBuffer,
                    m_swPageJobRecordsBufferSkinned,
                    m_swPageJobCountBufferSkinned,
                    m_swPageJobClusterTagsBuffer,
                    m_shadowConfiguredPageJobRecordCapacity,
                    slabGroup)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "SoftwareRasterPageJobBuildArgsPass1"),
                std::make_shared<ClusterSoftwareRasterPageJobBuildArgsPass>(
                    m_swPageJobCountBuffer,
                    m_swPageJobIndirectArgsBuffer,
                    m_swPageJobCountBufferSkinned,
                    m_swPageJobIndirectArgsBufferSkinned)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "SoftwareRasterPageJobRasterPass1"),
                std::make_shared<ClusterSoftwareRasterPageJobRasterPass>(
                    m_compactedVisibleClustersBuffer,
                    m_viewRasterInfoBuffer,
                    m_shadowPageTableTexture,
                    m_shadowPhysicalPagesTexture,
                    m_shadowClipmapInfoBuffer,
                    m_swPageJobCountBuffer,
                    m_swPageJobRecordsBuffer,
                    m_swPageJobIndirectArgsBuffer,
                    m_swPageJobCountBufferSkinned,
                    m_swPageJobRecordsBufferSkinned,
                    m_swPageJobIndirectArgsBufferSkinned,
                    slabGroup)));
        shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "SoftwareRasterPageJobRasterPass1");
    }
    else if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow && useShadowReyesRouting) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeQueueResetPass1"),
                std::make_shared<ReyesQueueResetPass>(
                    m_reyesFullClusterOutputsCounterBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
                    std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesDiceQueueOverflowBuffer,
                    nullptr,
                    m_reyesTelemetryBufferPhase1,
                    1u,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeCreateClassifyDispatchArgsPass1"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_swPageJobVisibleClustersCounterBuffer,
                    m_reyesClassifyIndirectArgsBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeClassifyPass1"),
                std::make_shared<ReyesClassifyPass>(
                    m_swPageJobVisibleClustersBuffer,
                    m_swPageJobVisibleClustersCounterBuffer,
                    nullptr,
                    m_reyesFullClusterOutputsBuffer,
                    m_reyesFullClusterOutputsCounterBuffer,
                    m_reyesFullClusterOutputCapacity,
                    m_reyesOwnedClustersBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesOwnedClusterCapacity,
                    nullptr,
                    m_reyesClassifyIndirectArgsBuffer,
                    m_reyesTelemetryBufferPhase1,
                    1u,
                    ReyesClassifyMode::ShadowCoarseLargeOnly)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeCreateSeedDispatchArgsPass1"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesSplitIndirectArgsBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeSeedPatchesPass1"),
                std::make_shared<ReyesSeedPatchesPass>(
                    m_swPageJobVisibleClustersBuffer,
                    m_reyesOwnedClustersBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesSplitQueueBufferA,
                    m_reyesSplitQueueCounterBufferA,
                    m_reyesSplitQueueOverflowBufferA,
                    m_reyesSplitIndirectArgsBuffer,
                    slabGroup,
                    reyesSplitQueueCapacity,
                    1u)));

        const std::shared_ptr<Buffer> reyesLargeSplitBuffers[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
        const std::shared_ptr<Buffer> reyesLargeSplitCounters[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
        const std::shared_ptr<Buffer> reyesLargeSplitOverflows[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
        for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
            const uint32_t inputIndex = splitPassIndex & 1u;
            const uint32_t outputIndex = inputIndex ^ 1u;

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeCreateSplitDispatchArgsPass1_" + std::to_string(splitPassIndex)),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        reyesLargeSplitCounters[inputIndex],
                        m_reyesSplitIndirectArgsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeSplitPass1_" + std::to_string(splitPassIndex)),
                    std::make_shared<ReyesSplitPass>(
                        m_swPageJobVisibleClustersBuffer,
                        reyesLargeSplitBuffers[inputIndex],
                        reyesLargeSplitCounters[inputIndex],
                        reyesLargeSplitBuffers[outputIndex],
                        reyesLargeSplitCounters[outputIndex],
                        reyesLargeSplitOverflows[outputIndex],
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceQueueOverflowBuffer,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesTessTableVerticesBuffer,
                        m_reyesTessTableTrianglesBuffer,
                        m_shadowClipmapInfoBuffer,
                        m_shadowDirtyPageHierarchyTexture,
                        m_shadowNonRasterablePageHierarchyTexture,
                        m_reyesSplitIndirectArgsBuffer,
                        m_reyesTelemetryBufferPhase1,
                        reyesSplitQueueCapacity,
                        splitPassIndex,
                        CLodReyesMaxSplitPassCount,
                        1u)));
        }

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeCreateDiceDispatchArgsPass1"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesDiceIndirectArgsBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeDicePass1"),
                std::make_shared<ReyesDicePass>(
                    m_reyesDiceQueueBuffer,
                    m_reyesDiceQueueCounterBuffer,
                    nullptr,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesDiceIndirectArgsBuffer,
                    m_reyesTelemetryBufferPhase1,
                    reyesDiceQueueCapacity,
                    1u)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesLargeBuildRasterWorkPass1"),
                std::make_shared<ReyesBuildRasterWorkPass>(
                    m_reyesDiceQueueBuffer,
                    m_reyesDiceQueueCounterBuffer,
                    nullptr,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesRasterWorkBuffer,
                    m_reyesRasterWorkCounterBuffer,
                    m_reyesDiceIndirectArgsBuffer,
                    m_reyesTelemetryBufferPhase1,
                    reyesRasterWorkCapacity)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCreateCommandPassReyesHW1"),
                std::make_shared<RasterBucketCreateCommandPass>(
                    m_reyesRasterWorkCounterBuffer,
                    m_histogramIndirectCommand,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesRasterWorkHistogramPass1"),
                std::make_shared<ReyesRasterWorkHistogramPass>(
                    m_reyesRasterWorkBuffer,
                    m_reyesRasterWorkCounterBuffer,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferSw)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesRasterWorkPrefixScanPass1"),
                std::make_shared<RasterBucketBlockScanPass>(
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesRasterWorkPrefixOffsetsPass1"),
                std::make_shared<RasterBucketBlockOffsetsPass>(
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    m_rasterBucketsScannedBlockSumsBuffer,
                    m_rasterBucketsTotalCountBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesRasterWorkCompactAndArgsPass1"),
                std::make_shared<ReyesRasterWorkCompactAndArgsPass>(
                    m_reyesRasterWorkBuffer,
                    m_reyesRasterWorkCounterBuffer,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferSw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsWriteCursorBufferSw,
                    m_reyesCompactedRasterWorkIndicesBuffer,
                    m_reyesPackedRasterWorkGroupsBuffer,
                    m_rasterBucketsIndirectArgsBufferPageJob)));

        const std::string reyesLargeShadowRasterPassName = MakeVariantPassName(traits, "ReyesLargeVirtualShadowHardwareRasterPass1");
        auto reyesLargeShadowRasterPassDesc = RenderGraph::ExternalPassDesc::Render(
            reyesLargeShadowRasterPassName,
            std::make_shared<ReyesVirtualShadowHardwareRasterPass>(
                m_swPageJobVisibleClustersBuffer,
                m_rasterBucketsHistogramBufferSw,
                m_rasterBucketsIndirectArgsBufferPageJob,
                m_reyesPackedRasterWorkGroupsBuffer,
                m_reyesCompactedRasterWorkIndicesBuffer,
                m_reyesRasterWorkBuffer,
                m_reyesDiceQueueBuffer,
                m_reyesTessTableConfigsBuffer,
                m_reyesTessTableVerticesBuffer,
                m_reyesTessTableTrianglesBuffer,
                m_shadowPageTableTexture,
                m_shadowPhysicalPagesTexture,
                m_shadowClipmapInfoBuffer,
                m_reyesTelemetryBufferPhase1,
                slabGroup));
        reyesLargeShadowRasterPassDesc.GeometryPass();
        outPasses.push_back(std::move(reyesLargeShadowRasterPassDesc));
        shadowClearDirtyBitsAfterPassName = reyesLargeShadowRasterPassName;
    }

    if (traits.schedulesPerViewDepthCopy) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "LinearDepthCopyPass1"),
                std::make_shared<PerViewLinearDepthCopyPass>()));
    }

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "LinearDepthDownsamplePass1"),
            std::make_shared<DownsamplePass>()));

    if (traits.usesPhase2OcclusionReplay) {
        HierarchicalCullingPassInputs cullPassInputs2;
        cullPassInputs2.isFirstPass = false;
        cullPassInputs2.maxVisibleClusters = m_visibleClusterCapacity;
        cullPassInputs2.workGraphMode = workGraphMode;
        cullPassInputs2.renderPhase = renderPhase;
        cullPassInputs2.clodOnlyWorkloads = true;
        cullPassInputs2.useShadowCascadeViews = (traits.type == CLodExtensionType::Shadow);
        cullPassInputs2.rasterOutputKind = traits.rasterOutputKind;
        const std::string cullPassName2 = MakeVariantPassName(traits, "HierarchicalCullingPass2");
        const bool useDispatchCullingPass2 = UseHierarchicalDispatchCullingPass(
            cullingBackend,
            cullPassInputs2.isFirstPass,
            cullPassInputs2.workGraphMode,
            cullPassInputs2.rasterOutputKind);
        std::shared_ptr<ComputePass> cullPass2 = useDispatchCullingPass2
            ? std::static_pointer_cast<ComputePass>(
                std::make_shared<HierarchicalDispatchCullingPass>(
                    cullPassName2,
                    cullPassInputs2,
                    m_visibleClustersBuffer,
                    m_visibleClustersCounterBufferPhase2,
                    m_swVisibleClustersCounterBufferPhase2,
                    traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersBufferPhase2 : nullptr,
                    traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersCounterBufferPhase2 : nullptr,
                    m_histogramIndirectCommand,
                    m_workGraphTelemetryBuffer,
                    m_occlusionReplayBuffer,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer,
                    m_viewDepthSrvIndicesBuffer,
                    m_viewRasterInfoBuffer,
                    traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
                    slabGroup,
                    m_visibleClustersCounterBuffer,
                    m_swVisibleClustersCounterBuffer,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidatesBuffer : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidateCountBuffer : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowInvalidatedInstancesBitsetBuffer : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPageTableTexture : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPhysicalPagesTexture : nullptr))
            : std::static_pointer_cast<ComputePass>(
                std::make_shared<HierarchicalCullingPass>(
                    cullPassName2,
                    cullPassInputs2,
                    m_visibleClustersBuffer,
                    m_visibleClustersCounterBufferPhase2,
                    m_swVisibleClustersCounterBufferPhase2,
                    traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersBufferPhase2 : nullptr,
                    traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow ? m_swPageJobVisibleClustersCounterBufferPhase2 : nullptr,
                    m_histogramIndirectCommand,
                    m_workGraphTelemetryBuffer,
                    m_occlusionReplayBuffer,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer,
                    m_viewDepthSrvIndicesBuffer,
                    m_viewRasterInfoBuffer,
                    traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
                    slabGroup,
                    m_visibleClustersCounterBuffer,
                    m_swVisibleClustersCounterBuffer,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidatesBuffer : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPredictiveInvalidationCandidateCountBuffer : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowInvalidatedInstancesBitsetBuffer : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPageTableTexture : nullptr,
                    traits.type == CLodExtensionType::Shadow ? m_shadowPhysicalPagesTexture : nullptr));
        auto cullPassDesc2 = RenderGraph::ExternalPassDesc::Compute(
            cullPassName2,
            cullPass2);
        cullPassDesc2.At(RenderGraph::ExternalInsertPoint::After(MakeVariantPassName(traits, "LinearDepthDownsamplePass1")));
        outPasses.push_back(std::move(cullPassDesc2));

        if (useReyesForThisVariant) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesQueueResetPass2"),
                std::make_shared<ReyesQueueResetPass>(
                    m_reyesFullClusterOutputsCounterBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
                    std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesDiceQueueOverflowBuffer,
                    reyesOwnershipBitsetBufferPhase2,
                    m_reyesTelemetryBufferPhase2,
                    2u,
                    false)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesCreateClassifyDispatchArgsPass2"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_visibleClustersCounterBufferPhase2,
                    m_reyesClassifyIndirectArgsBufferPhase2)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesClassifyPass2"),
                std::make_shared<ReyesClassifyPass>(
                    m_visibleClustersBuffer,
                    m_visibleClustersCounterBufferPhase2,
                    m_visibleClustersCounterBuffer,
                    m_reyesFullClusterOutputsBuffer,
                    m_reyesFullClusterOutputsCounterBuffer,
                    m_reyesFullClusterOutputCapacity,
                    m_reyesOwnedClustersBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesOwnedClusterCapacity,
                    reyesOwnershipBitsetBufferPhase2,
                    m_reyesClassifyIndirectArgsBufferPhase2,
                    m_reyesTelemetryBufferPhase2,
                    2u,
                    traits.type == CLodExtensionType::Shadow
                        ? ReyesClassifyMode::ShadowFineDisplacedOnly
                        : ReyesClassifyMode::Default)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesCreateSeedDispatchArgsPass2"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesSplitIndirectArgsBufferPhase2)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesSeedPatchesPass2"),
                std::make_shared<ReyesSeedPatchesPass>(
                    m_visibleClustersBuffer,
                    m_reyesOwnedClustersBuffer,
                    m_reyesOwnedClustersCounterBuffer,
                    m_reyesSplitQueueBufferA,
                    m_reyesSplitQueueCounterBufferA,
                    m_reyesSplitQueueOverflowBufferA,
                    m_reyesSplitIndirectArgsBufferPhase2,
                    slabGroup,
                    reyesSplitQueueCapacity,
                    2u)));

        const std::shared_ptr<Buffer> reyesSplitBuffers[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
        const std::shared_ptr<Buffer> reyesSplitCounters[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
        const std::shared_ptr<Buffer> reyesSplitOverflows[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
        for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
            const uint32_t inputIndex = splitPassIndex & 1u;
            const uint32_t outputIndex = inputIndex ^ 1u;

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateSplitDispatchArgsPass2_" + std::to_string(splitPassIndex)),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        reyesSplitCounters[inputIndex],
                        m_reyesSplitIndirectArgsBufferPhase2)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesSplitPass2_" + std::to_string(splitPassIndex)),
                    std::make_shared<ReyesSplitPass>(
                        m_visibleClustersBuffer,
                        reyesSplitBuffers[inputIndex],
                        reyesSplitCounters[inputIndex],
                        reyesSplitBuffers[outputIndex],
                        reyesSplitCounters[outputIndex],
                        reyesSplitOverflows[outputIndex],
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceQueueOverflowBuffer,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesTessTableVerticesBuffer,
                        m_reyesTessTableTrianglesBuffer,
                        traits.type == CLodExtensionType::Shadow ? m_shadowClipmapInfoBuffer : nullptr,
                        traits.type == CLodExtensionType::Shadow ? m_shadowDirtyPageHierarchyTexture : nullptr,
                        traits.type == CLodExtensionType::Shadow ? m_shadowNonRasterablePageHierarchyTexture : nullptr,
                        m_reyesSplitIndirectArgsBufferPhase2,
                        m_reyesTelemetryBufferPhase2,
                        reyesSplitQueueCapacity,
                        splitPassIndex,
                        CLodReyesMaxSplitPassCount,
                        2u)));
        }

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesCreateDiceDispatchArgsPass2"),
                std::make_shared<ReyesCreateDispatchArgsPass>(
                    m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceIndirectArgsBufferPhase2,
                        m_reyesDiceQueuePhase1CountBuffer)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "ReyesDicePass2"),
                std::make_shared<ReyesDicePass>(
                    m_reyesDiceQueueBuffer,
                    m_reyesDiceQueueCounterBuffer,
                    m_reyesDiceQueuePhase1CountBuffer,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesDiceIndirectArgsBufferPhase2,
                    m_reyesTelemetryBufferPhase2,
                    reyesDiceQueueCapacity,
                    2u)));

        if (traits.type == CLodExtensionType::VisiblityBuffer || traits.type == CLodExtensionType::Shadow) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesBuildRasterWorkPass2"),
                    std::make_shared<ReyesBuildRasterWorkPass>(
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceQueuePhase1CountBuffer,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesRasterWorkBufferPhase2,
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_reyesDiceIndirectArgsBufferPhase2,
                        m_reyesTelemetryBufferPhase2,
                        reyesRasterWorkCapacity)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesCreateRasterWorkDispatchArgsPass2"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_reyesRasterWorkIndirectArgsBufferPhase2)));

            if (traits.type == CLodExtensionType::VisiblityBuffer) {
                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesPatchRasterPass2"),
                        std::make_shared<ReyesPatchRasterizationPass>(
                            m_visibleClustersBuffer,
                            m_reyesDiceQueueBuffer,
                            m_reyesDiceQueueCounterBuffer,
                            m_reyesRasterWorkBufferPhase2,
                            m_reyesRasterWorkCounterBufferPhase2,
                            m_reyesTessTableConfigsBuffer,
                            m_reyesTessTableVerticesBuffer,
                            m_reyesTessTableTrianglesBuffer,
                            m_viewRasterInfoBuffer,
                            m_reyesRasterWorkIndirectArgsBufferPhase2,
                            m_reyesTelemetryBufferPhase2,
                            slabGroup,
                            m_maxVisibleClusters,
                            2u,
                            CLodReyesPatchVisibilityIndexBase(m_maxVisibleClusters))));
            }
            else {
                const std::string reyesShadowRasterPassName = MakeVariantPassName(traits, "ReyesVirtualShadowRasterPass2");
                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        reyesShadowRasterPassName,
                        std::make_shared<ReyesVirtualShadowRasterizationPass>(
                            m_visibleClustersBuffer,
                            m_reyesDiceQueueBuffer,
                            m_reyesDiceQueueCounterBuffer,
                            m_reyesRasterWorkBufferPhase2,
                            m_reyesRasterWorkCounterBufferPhase2,
                            m_reyesTessTableConfigsBuffer,
                            m_reyesTessTableVerticesBuffer,
                            m_reyesTessTableTrianglesBuffer,
                            m_reyesRasterWorkIndirectArgsBufferPhase2,
                            m_reyesTelemetryBufferPhase2,
                            m_shadowPageTableTexture,
                            m_shadowPhysicalPagesTexture,
                            m_shadowClipmapInfoBuffer,
                            slabGroup,
                            MakeVariantResourceName(traits, "Reyes Virtual Shadow View Raster Info Buffer Phase2"),
                            2u)));
                shadowClearDirtyBitsAfterPassName = reyesShadowRasterPassName;
            }
        }
    }

        outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsHistogramPass2"),
            std::make_shared<RasterBucketHistogramPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBufferPhase2,
                m_histogramIndirectCommand,
                m_rasterBucketsHistogramBufferPhase2,
                reyesOwnershipBitsetBufferPhase2,
                m_visibleClustersCounterBuffer)));

        outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsPrefixScanPass2"),
            std::make_shared<RasterBucketBlockScanPass>(
                m_rasterBucketsHistogramBufferPhase2,
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer)));

        outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass2"),
            std::make_shared<RasterBucketBlockOffsetsPass>(
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsBlockSumsBuffer,
                m_rasterBucketsScannedBlockSumsBuffer,
                m_rasterBucketsTotalCountBuffer)));

        outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass2"),
            std::make_shared<RasterBucketCompactAndArgsPass>(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBufferPhase2,
                m_rasterBucketsTotalCountBufferPhase1,
                m_histogramIndirectCommand,
                m_rasterBucketsHistogramBufferPhase2,
                m_rasterBucketsOffsetsBuffer,
                m_rasterBucketsWriteCursorBufferPhase2,
                m_compactedVisibleClustersBuffer,
                m_rasterBucketsIndirectArgsBufferPhase2,
                m_sortedToUnsortedMappingBuffer,
                reyesOwnershipBitsetBufferPhase2,
                m_visibleClusterCapacity,
                true)));

        ClusterRasterizationPassInputs rasterizePassInputs2;
        rasterizePassInputs2.clearGbuffer = false;
        rasterizePassInputs2.wireframe = false;
        rasterizePassInputs2.renderPhase = renderPhase;
        rasterizePassInputs2.outputKind = traits.rasterOutputKind;
        std::shared_ptr<Buffer> rasterClustersBufferPhase2 = m_compactedVisibleClustersBuffer;
        std::shared_ptr<Buffer> rasterHistogramBufferPhase2 = m_rasterBucketsHistogramBufferPhase2;
        std::shared_ptr<Buffer> rasterIndirectArgsBufferPhase2 = m_rasterBucketsIndirectArgsBufferPhase2;

        auto rasterizePassDesc2 = RenderGraph::ExternalPassDesc::Render(
            MakeVariantPassName(traits, "RasterizeClustersPass2"),
            std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs2,
            rasterClustersBufferPhase2,
            rasterHistogramBufferPhase2,
            rasterIndirectArgsBufferPhase2,
            m_sortedToUnsortedMappingBuffer,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            slabGroup,
            shadowPageTable,
            shadowPhysicalPages,
            shadowClipmapInfo));
        rasterizePassDesc2.At(RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass"));
        rasterizePassDesc2.GeometryPass();
            shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "RasterizeClustersPass2");
        outPasses.push_back(std::move(rasterizePassDesc2));

        if (useComputeSWRaster) {
        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW2"),
                std::make_shared<RasterBucketCreateCommandPass>(
                    m_swVisibleClustersCounterBufferPhase2,
                    m_histogramIndirectCommand,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsHistogramPassSW2"),
                std::make_shared<RasterBucketHistogramPass>(
                    m_visibleClustersBuffer,
                    m_swVisibleClustersCounterBufferPhase2,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    reyesOwnershipBitsetBufferPhase2,
                    m_swVisibleClustersCounterBuffer,
                    true,
                    m_visibleClusterCapacity,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW2"),
                std::make_shared<RasterBucketBlockScanPass>(
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW2"),
                std::make_shared<RasterBucketBlockOffsetsPass>(
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    m_rasterBucketsScannedBlockSumsBuffer,
                    m_rasterBucketsTotalCountBuffer,
                    true)));

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW2"),
                std::make_shared<RasterBucketCompactAndArgsPass>(
                    m_visibleClustersBuffer,
                    m_swVisibleClustersCounterBufferPhase2,
                    m_rasterBucketsTotalCountBufferPhase1Sw,
                    m_histogramIndirectCommand,
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsWriteCursorBufferPhase2Sw,
                    m_compactedVisibleClustersBuffer,
                    m_rasterBucketsIndirectArgsBufferPhase2Sw,
                    m_sortedToUnsortedMappingBuffer,
                    reyesOwnershipBitsetBufferPhase2,
                    m_visibleClusterCapacity,
                    true,
                    true,
                    true,
                    true)));

        std::shared_ptr<Buffer> swRasterClustersBufferPhase2 = m_compactedVisibleClustersBuffer;
        std::shared_ptr<Buffer> swRasterHistogramBufferPhase2 = m_rasterBucketsHistogramBufferPhase2Sw;
        std::shared_ptr<Buffer> swRasterIndirectArgsBufferPhase2 = m_rasterBucketsIndirectArgsBufferPhase2Sw;
        if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow) {
            const uint32_t vsmBlockSoftCap = std::max(1u, std::min(m_shadowConfiguredPageJobMaxPages, CLodVirtualShadowBlockMaxTrackedPerCluster));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockHistogramPassSW2"),
                    std::make_shared<VirtualShadowBlockExpandPass>(
                        VirtualShadowBlockExpandMode::Histogram,
                        m_compactedVisibleClustersBuffer,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsIndirectArgsBufferPhase2Sw,
                        m_rasterBucketsHistogramBufferSw,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        m_shadowPageTableTexture,
                        m_shadowClipmapInfoBuffer,
                        m_shadowConfiguredExpandedRecordCapacity,
                        vsmBlockSoftCap,
                        slabGroup,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockPrefixScanPassSW2"),
                    std::make_shared<RasterBucketBlockScanPass>(
                        m_rasterBucketsHistogramBufferSw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsBlockSumsBuffer,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockPrefixOffsetsPassSW2"),
                    std::make_shared<RasterBucketBlockOffsetsPass>(
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsBlockSumsBuffer,
                        m_rasterBucketsScannedBlockSumsBuffer,
                        m_rasterBucketsTotalCountBufferPhase1Sw,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBlockEmitPassSW2"),
                    std::make_shared<VirtualShadowBlockExpandPass>(
                        VirtualShadowBlockExpandMode::Emit,
                        m_compactedVisibleClustersBuffer,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsIndirectArgsBufferPhase2Sw,
                        m_rasterBucketsHistogramBufferSw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsWriteCursorBufferSw,
                        m_vsmExpandedVisibleClustersBufferSw,
                        m_vsmExpandedBlockMetaBufferSw,
                        m_shadowPageTableTexture,
                        m_shadowClipmapInfoBuffer,
                        m_shadowConfiguredExpandedRecordCapacity,
                        vsmBlockSoftCap,
                        slabGroup,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "VirtualShadowBuildArgsPassSW2"),
                    std::make_shared<VirtualShadowBuildRasterArgsPass>(
                        m_rasterBucketsHistogramBufferSw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsIndirectArgsBufferSw,
                        true)));

            swRasterClustersBufferPhase2 = m_vsmExpandedVisibleClustersBufferSw;
            swRasterHistogramBufferPhase2 = m_rasterBucketsHistogramBufferSw;
            swRasterIndirectArgsBufferPhase2 = m_rasterBucketsIndirectArgsBufferSw;
        }

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "SoftwareRasterizeClustersPass2"),
                std::make_shared<ClusterSoftwareRasterizationPass>(
                    swRasterClustersBufferPhase2,
                    swRasterHistogramBufferPhase2,
                    swRasterIndirectArgsBufferPhase2,
                    m_sortedToUnsortedMappingBuffer,
                    m_viewRasterInfoBuffer,
                    traits.rasterOutputKind,
                    m_shadowPageTableTexture,
                    m_shadowPhysicalPagesTexture,
                    m_shadowClipmapInfoBuffer,
                    slabGroup,
                    true)));

        if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow) {
            shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass2");
        }
    }

        if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow && useShadowPageJob) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "RasterBucketsCreateCommandPassPageJob2"),
                    std::make_shared<RasterBucketCreateCommandPass>(
                        m_swPageJobVisibleClustersCounterBufferPhase2,
                        m_histogramIndirectCommand,
                        m_occlusionReplayStateBuffer,
                        m_occlusionNodeGpuInputsBuffer,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "RasterBucketsHistogramPassPageJob2"),
                    std::make_shared<RasterBucketHistogramPass>(
                        m_swPageJobVisibleClustersBufferPhase2,
                        m_swPageJobVisibleClustersCounterBufferPhase2,
                        m_histogramIndirectCommand,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        nullptr,
                        nullptr,
                        false,
                        m_maxVisibleClusters,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "RasterBucketsPrefixScanPassPageJob2"),
                    std::make_shared<RasterBucketBlockScanPass>(
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsBlockSumsBuffer,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassPageJob2"),
                    std::make_shared<RasterBucketBlockOffsetsPass>(
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsBlockSumsBuffer,
                        m_rasterBucketsScannedBlockSumsBuffer,
                        m_rasterBucketsTotalCountBuffer,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassPageJob2"),
                    std::make_shared<RasterBucketCompactAndArgsPass>(
                        m_swPageJobVisibleClustersBufferPhase2,
                        m_swPageJobVisibleClustersCounterBufferPhase2,
                        m_swPageJobVisibleClustersCounterBufferPhase2,
                        m_histogramIndirectCommand,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsWriteCursorBufferPhase2Sw,
                        m_compactedVisibleClustersBuffer,
                        m_rasterBucketsIndirectArgsBufferPhase2PageJob,
                        m_sortedToUnsortedMappingBuffer,
                        nullptr,
                        m_maxVisibleClusters,
                        false,
                        false,
                        true,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "SoftwareRasterPageJobExpandPass2"),
                    std::make_shared<ClusterSoftwareRasterPageJobExpandPass>(
                        m_compactedVisibleClustersBuffer,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsIndirectArgsBufferPhase2PageJob,
                        m_viewRasterInfoBuffer,
                        m_shadowPageTableTexture,
                        m_shadowClipmapInfoBuffer,
                        m_swPageJobRecordsBufferPhase2,
                        m_swPageJobCountBufferPhase2,
                        m_swPageJobRecordsBufferPhase2Skinned,
                        m_swPageJobCountBufferPhase2Skinned,
                        m_swPageJobClusterTagsBufferPhase2,
                        m_shadowConfiguredPageJobRecordCapacity,
                        slabGroup,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "SoftwareRasterPageJobBuildArgsPass2"),
                    std::make_shared<ClusterSoftwareRasterPageJobBuildArgsPass>(
                        m_swPageJobCountBufferPhase2,
                        m_swPageJobIndirectArgsBufferPhase2,
                        m_swPageJobCountBufferPhase2Skinned,
                        m_swPageJobIndirectArgsBufferPhase2Skinned,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "SoftwareRasterPageJobRasterPass2"),
                    std::make_shared<ClusterSoftwareRasterPageJobRasterPass>(
                        m_compactedVisibleClustersBuffer,
                        m_viewRasterInfoBuffer,
                        m_shadowPageTableTexture,
                        m_shadowPhysicalPagesTexture,
                        m_shadowClipmapInfoBuffer,
                        m_swPageJobCountBufferPhase2,
                        m_swPageJobRecordsBufferPhase2,
                        m_swPageJobIndirectArgsBufferPhase2,
                        m_swPageJobCountBufferPhase2Skinned,
                        m_swPageJobRecordsBufferPhase2Skinned,
                        m_swPageJobIndirectArgsBufferPhase2Skinned,
                        slabGroup,
                        true)));
            shadowClearDirtyBitsAfterPassName = MakeVariantPassName(traits, "SoftwareRasterPageJobRasterPass2");
        }
        else if (traits.rasterOutputKind == CLodRasterOutputKind::VirtualShadow && useShadowReyesRouting) {
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeQueueResetPass2"),
                    std::make_shared<ReyesQueueResetPass>(
                        m_reyesFullClusterOutputsCounterBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB },
                        std::vector<std::shared_ptr<Buffer>>{ m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB },
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceQueueOverflowBuffer,
                        nullptr,
                        m_reyesTelemetryBufferPhase2,
                        2u,
                        true)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeCreateClassifyDispatchArgsPass2"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_swPageJobVisibleClustersCounterBufferPhase2,
                        m_reyesClassifyIndirectArgsBufferPhase2)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeClassifyPass2"),
                    std::make_shared<ReyesClassifyPass>(
                        m_swPageJobVisibleClustersBufferPhase2,
                        m_swPageJobVisibleClustersCounterBufferPhase2,
                        nullptr,
                        m_reyesFullClusterOutputsBuffer,
                        m_reyesFullClusterOutputsCounterBuffer,
                        m_reyesFullClusterOutputCapacity,
                        m_reyesOwnedClustersBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesOwnedClusterCapacity,
                        nullptr,
                        m_reyesClassifyIndirectArgsBufferPhase2,
                        m_reyesTelemetryBufferPhase2,
                        2u,
                        ReyesClassifyMode::ShadowCoarseLargeOnly)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeCreateSeedDispatchArgsPass2"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesSplitIndirectArgsBufferPhase2)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeSeedPatchesPass2"),
                    std::make_shared<ReyesSeedPatchesPass>(
                        m_swPageJobVisibleClustersBufferPhase2,
                        m_reyesOwnedClustersBuffer,
                        m_reyesOwnedClustersCounterBuffer,
                        m_reyesSplitQueueBufferA,
                        m_reyesSplitQueueCounterBufferA,
                        m_reyesSplitQueueOverflowBufferA,
                        m_reyesSplitIndirectArgsBufferPhase2,
                        slabGroup,
                        reyesSplitQueueCapacity,
                        2u)));

            const std::shared_ptr<Buffer> reyesLargeSplitBuffersPhase2[] = { m_reyesSplitQueueBufferA, m_reyesSplitQueueBufferB };
            const std::shared_ptr<Buffer> reyesLargeSplitCountersPhase2[] = { m_reyesSplitQueueCounterBufferA, m_reyesSplitQueueCounterBufferB };
            const std::shared_ptr<Buffer> reyesLargeSplitOverflowsPhase2[] = { m_reyesSplitQueueOverflowBufferA, m_reyesSplitQueueOverflowBufferB };
            for (uint32_t splitPassIndex = 0; splitPassIndex < CLodReyesMaxSplitPassCount; ++splitPassIndex) {
                const uint32_t inputIndex = splitPassIndex & 1u;
                const uint32_t outputIndex = inputIndex ^ 1u;

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesLargeCreateSplitDispatchArgsPass2_" + std::to_string(splitPassIndex)),
                        std::make_shared<ReyesCreateDispatchArgsPass>(
                            reyesLargeSplitCountersPhase2[inputIndex],
                            m_reyesSplitIndirectArgsBufferPhase2)));

                outPasses.push_back(
                    RenderGraph::ExternalPassDesc::Compute(
                        MakeVariantPassName(traits, "ReyesLargeSplitPass2_" + std::to_string(splitPassIndex)),
                        std::make_shared<ReyesSplitPass>(
                            m_swPageJobVisibleClustersBufferPhase2,
                            reyesLargeSplitBuffersPhase2[inputIndex],
                            reyesLargeSplitCountersPhase2[inputIndex],
                            reyesLargeSplitBuffersPhase2[outputIndex],
                            reyesLargeSplitCountersPhase2[outputIndex],
                            reyesLargeSplitOverflowsPhase2[outputIndex],
                            m_reyesDiceQueueBuffer,
                            m_reyesDiceQueueCounterBuffer,
                            m_reyesDiceQueueOverflowBuffer,
                            m_reyesTessTableConfigsBuffer,
                            m_reyesTessTableVerticesBuffer,
                            m_reyesTessTableTrianglesBuffer,
                            m_shadowClipmapInfoBuffer,
                            m_shadowDirtyPageHierarchyTexture,
                            m_shadowNonRasterablePageHierarchyTexture,
                            m_reyesSplitIndirectArgsBufferPhase2,
                            m_reyesTelemetryBufferPhase2,
                            reyesSplitQueueCapacity,
                            splitPassIndex,
                            CLodReyesMaxSplitPassCount,
                            2u)));
            }

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeCreateDiceDispatchArgsPass2"),
                    std::make_shared<ReyesCreateDispatchArgsPass>(
                        m_reyesDiceQueueCounterBuffer,
                        m_reyesDiceIndirectArgsBufferPhase2)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeDicePass2"),
                    std::make_shared<ReyesDicePass>(
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        nullptr,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesDiceIndirectArgsBufferPhase2,
                        m_reyesTelemetryBufferPhase2,
                        reyesDiceQueueCapacity,
                        2u)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesLargeBuildRasterWorkPass2"),
                    std::make_shared<ReyesBuildRasterWorkPass>(
                        m_reyesDiceQueueBuffer,
                        m_reyesDiceQueueCounterBuffer,
                        nullptr,
                        m_reyesTessTableConfigsBuffer,
                        m_reyesRasterWorkBufferPhase2,
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_reyesDiceIndirectArgsBufferPhase2,
                        m_reyesTelemetryBufferPhase2,
                        reyesRasterWorkCapacity)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "RasterBucketsCreateCommandPassReyesHW2"),
                std::make_shared<RasterBucketCreateCommandPass>(
                    m_reyesRasterWorkCounterBufferPhase2,
                    m_histogramIndirectCommand,
                    m_occlusionReplayStateBuffer,
                    m_occlusionNodeGpuInputsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesRasterWorkHistogramPass2"),
                    std::make_shared<ReyesRasterWorkHistogramPass>(
                        m_reyesRasterWorkBufferPhase2,
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_histogramIndirectCommand,
                        m_rasterBucketsHistogramBufferPhase2Sw)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesRasterWorkPrefixScanPass2"),
                std::make_shared<RasterBucketBlockScanPass>(
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesRasterWorkPrefixOffsetsPass2"),
                std::make_shared<RasterBucketBlockOffsetsPass>(
                    m_rasterBucketsOffsetsBuffer,
                    m_rasterBucketsBlockSumsBuffer,
                    m_rasterBucketsScannedBlockSumsBuffer,
                    m_rasterBucketsTotalCountBuffer)));

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Compute(
                    MakeVariantPassName(traits, "ReyesRasterWorkCompactAndArgsPass2"),
                    std::make_shared<ReyesRasterWorkCompactAndArgsPass>(
                        m_reyesRasterWorkBufferPhase2,
                        m_reyesRasterWorkCounterBufferPhase2,
                        m_histogramIndirectCommand,
                        m_rasterBucketsHistogramBufferPhase2Sw,
                        m_rasterBucketsOffsetsBuffer,
                        m_rasterBucketsWriteCursorBufferPhase2Sw,
                        m_reyesCompactedRasterWorkIndicesBufferPhase2,
                        m_reyesPackedRasterWorkGroupsBufferPhase2,
                        m_rasterBucketsIndirectArgsBufferPhase2PageJob)));

            const std::string reyesLargeShadowRasterPassName = MakeVariantPassName(traits, "ReyesLargeVirtualShadowHardwareRasterPass2");
            auto reyesLargeShadowRasterPassDesc = RenderGraph::ExternalPassDesc::Render(
                reyesLargeShadowRasterPassName,
                std::make_shared<ReyesVirtualShadowHardwareRasterPass>(
                    m_swPageJobVisibleClustersBufferPhase2,
                    m_rasterBucketsHistogramBufferPhase2Sw,
                    m_rasterBucketsIndirectArgsBufferPhase2PageJob,
                    m_reyesPackedRasterWorkGroupsBufferPhase2,
                    m_reyesCompactedRasterWorkIndicesBufferPhase2,
                    m_reyesRasterWorkBufferPhase2,
                    m_reyesDiceQueueBuffer,
                    m_reyesTessTableConfigsBuffer,
                    m_reyesTessTableVerticesBuffer,
                    m_reyesTessTableTrianglesBuffer,
                    m_shadowPageTableTexture,
                    m_shadowPhysicalPagesTexture,
                    m_shadowClipmapInfoBuffer,
                    m_reyesTelemetryBufferPhase2,
                    slabGroup));
            reyesLargeShadowRasterPassDesc.At(RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass"));
            reyesLargeShadowRasterPassDesc.GeometryPass();
            outPasses.push_back(std::move(reyesLargeShadowRasterPassDesc));
            shadowClearDirtyBitsAfterPassName = reyesLargeShadowRasterPassName;
        }

        if (traits.schedulesPerViewDepthCopy) {
            auto depthCopyPassDesc2 = RenderGraph::ExternalPassDesc::Compute(
                MakeVariantPassName(traits, "LinearDepthCopyPass2"),
                std::make_shared<PerViewLinearDepthCopyPass>());
            depthCopyPassDesc2.At(RenderGraph::ExternalInsertPoint::Before("DeferredShadingPass"));
            outPasses.push_back(std::move(depthCopyPassDesc2));
        }
    }

    if (traits.type == CLodExtensionType::Shadow && !shadowClearDirtyBitsAfterPassName.empty()) {
        const std::string shadowClearDirtyBitsPassName = MakeVariantPassName(traits, "VirtualShadowClearDirtyBitsPass");
        auto shadowClearDirtyBitsPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowClearDirtyBitsPassName,
            std::make_shared<VirtualShadowMapClearDirtyBitsPass>(
                m_shadowPageTableTexture,
                m_shadowAllocationRequestsBuffer,
                m_shadowAllocationCountBuffer,
                m_shadowAllocationIndirectArgsBuffer));
        shadowClearDirtyBitsPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowClearDirtyBitsAfterPassName));
        outPasses.push_back(std::move(shadowClearDirtyBitsPassDesc));

        const std::string shadowExpandPredictedPagesPassName = MakeVariantPassName(traits, "VirtualShadowExpandPredictedPagesPass");
        auto shadowExpandPredictedPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowExpandPredictedPagesPassName,
            std::make_shared<VirtualShadowMapExpandPredictedPagesPass>(
                m_shadowPredictiveInvalidationCandidatesBuffer,
                m_shadowPredictiveInvalidationCandidateCountBuffer,
                m_shadowPredictiveRawPagesBuffer,
                m_shadowPredictiveRawPageCountBuffer,
                m_shadowClipmapInfoBuffer));
        shadowExpandPredictedPagesPassDesc.At(RenderGraph::ExternalInsertPoint::After(shadowClearDirtyBitsPassName));
        outPasses.push_back(std::move(shadowExpandPredictedPagesPassDesc));

        const std::string shadowDeduplicatePredictedPagesPassName = MakeVariantPassName(traits, "VirtualShadowDeduplicatePredictedPagesPass");
        auto shadowDeduplicatePredictedPagesPassDesc = RenderGraph::ExternalPassDesc::Compute(
            shadowDeduplicatePredictedPagesPassName,
            std::make_shared<VirtualShadowMapDeduplicatePredictedPagesPass>(
                m_shadowPredictiveRawPagesBuffer,
                m_shadowPredictiveRawPageCountBuffer,
                m_shadowPredictedInvalidationScratchBitsetBuffer,
                m_shadowPredictedInvalidationPagesBufferA,
                    m_shadowPredictedInvalidationPageCountBufferA));
        shadowDeduplicatePredictedPagesPassDesc.At(makeShadowTailInsertPoint(shadowExpandPredictedPagesPassName));
        outPasses.push_back(std::move(shadowDeduplicatePredictedPagesPassDesc));
    }

    outPasses.push_back(
        RenderGraph::ExternalPassDesc::Compute(
            MakeVariantPassName(traits, "LinearDepthDownsamplePass2"),
            std::make_shared<DownsamplePass>()));

    applyTechniqueTags();
}

void CLodExtension::GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    if (m_streamingSystem) {
        m_streamingSystem->GatherFramePasses(rg, outPasses);
    }
}
