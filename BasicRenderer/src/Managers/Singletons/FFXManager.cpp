#include "Managers/Singletons/FFXManager.h"

#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "OpenRenderGraph/OpenRenderGraph.h"
#include "ThirdParty/FFX/host/ffx_sssr.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Scene/Scene.h"
#include "rhi_interop.h"
#include "rhi_interop_dx12.h"
#include "rhi_interop_vulkan.h"
#include "Render/RenderContext.h"

#include "ThirdParty/FFX/host/backends/dx12/ffx_dx12.h"

#if BASICRHI_HAS_VULKAN_HEADERS
#include "ThirdParty/FFX/host/backends/vk/ffx_vk.h"
#endif

extern ffxFunctions ffxModule;

namespace {
#if BASICRHI_HAS_VULKAN_HEADERS
VkFormat ToVkFormat(rhi::Format format) {
    switch (format) {
    case rhi::Format::R32G32B32A32_Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case rhi::Format::R16G16B16A16_Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case rhi::Format::R11G11B10_Float:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case rhi::Format::R16G16_Float:
        return VK_FORMAT_R16G16_SFLOAT;
    case rhi::Format::R8G8B8A8_UNorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case rhi::Format::R8G8B8A8_UNorm_sRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case rhi::Format::B8G8R8A8_UNorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case rhi::Format::B8G8R8A8_UNorm_sRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case rhi::Format::R32_Float:
        return VK_FORMAT_R32_SFLOAT;
    case rhi::Format::D32_Float:
        return VK_FORMAT_D32_SFLOAT;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

uint32_t ComputeMipCount(const TextureDescription& description) {
    const uint32_t layers = (description.isCubemap ? 6u : 1u) * (std::max)(1u, description.arraySize);
    if (layers == 0u || description.imageDimensions.empty()) {
        return 1u;
    }
    const size_t mipCount = description.imageDimensions.size() / layers;
    return static_cast<uint32_t>((std::max)(size_t(1), mipCount));
}

VkImageUsageFlags BuildVkImageUsage(const TextureDescription& description) {
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (description.hasUAV || description.hasNonShaderVisibleUAV) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (description.hasRTV) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (description.hasDSV) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    return usage;
}

VkImageCreateInfo BuildVkImageCreateInfo(const TextureDescription& description) {
    VkImageCreateInfo createInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.format = ToVkFormat(description.format);
    createInfo.extent.width = description.imageDimensions.empty() ? 1u : description.imageDimensions.front().width;
    createInfo.extent.height = description.imageDimensions.empty() ? 1u : description.imageDimensions.front().height;
    createInfo.extent.depth = 1u;
    createInfo.mipLevels = ComputeMipCount(description);
    createInfo.arrayLayers = (description.isCubemap ? 6u : 1u) * (std::max)(1u, description.arraySize);
    createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    createInfo.usage = BuildVkImageUsage(description);
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (description.isCubemap) {
        createInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    return createInfo;
}

FfxResource GetFFXResource(PixelBuffer* resource, const wchar_t* name, FfxResourceStates state) {
    const rhi::Backend backend = DeviceManager::GetInstance().GetBackend();
    if (backend == rhi::Backend::D3D12) {
        auto* nativeResource = rhi::dx12::get_resource(resource->GetAPIResource());
        auto desc = ffxGetResourceDescriptionDX12(nativeResource, FFX_RESOURCE_USAGE_READ_ONLY);
        return ffxGetResourceDX12(nativeResource, desc, name, state);
    }

    if (backend == rhi::Backend::Vulkan) {
        const VkImage nativeImage = rhi::vulkan::get_resource(resource->GetAPIResource());
        const VkImageCreateInfo createInfo = BuildVkImageCreateInfo(resource->GetDescription());
        const FfxResourceDescription desc = ffxGetImageResourceDescriptionVK(nativeImage, createInfo, FFX_RESOURCE_USAGE_READ_ONLY);
        return ffxGetResourceVK(reinterpret_cast<void*>(nativeImage), desc, name, state);
    }

    return {};
}
#else
FfxResource GetFFXResource(PixelBuffer* resource, const wchar_t* name, FfxResourceStates state) {
    const rhi::Backend backend = DeviceManager::GetInstance().GetBackend();
    if (backend == rhi::Backend::D3D12) {
        auto* nativeResource = rhi::dx12::get_resource(resource->GetAPIResource());
        auto desc = ffxGetResourceDescriptionDX12(nativeResource, FFX_RESOURCE_USAGE_READ_ONLY);
        return ffxGetResourceDX12(nativeResource, desc, name, state);
    }

    spdlog::error("Failed to get FFX resource '{}' because Vulkan SDK headers are not available for this build.", ws2s(name));
    return {};
}
#endif
}

bool FFXManager::InitFFX() {
    m_getRenderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution");
    m_getOutputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution");
    auto outputRes = m_getOutputRes();
    auto renderRes = m_getRenderRes();

    const rhi::Device device = DeviceManager::GetInstance().GetDevice();
    const rhi::Backend backend = DeviceManager::GetInstance().GetBackend();
    FfxDevice ffxDevice{};
    size_t scratchMemorySize = 0;

    if (backend == rhi::Backend::D3D12) {
        ffxDevice = ffxGetDeviceDX12(rhi::dx12::get_device(device));
        scratchMemorySize = ffxGetScratchMemorySizeDX12(10);
    } else if (backend == rhi::Backend::Vulkan) {
#if BASICRHI_HAS_VULKAN_HEADERS
        VkDeviceContext deviceContext{};
        deviceContext.vkDevice = rhi::vulkan::get_device(device);
        deviceContext.vkPhysicalDevice = rhi::vulkan::get_physical_device(device);
        deviceContext.vkDeviceProcAddr = vkGetDeviceProcAddr;
        ffxDevice = ffxGetDeviceVK(&deviceContext);
        scratchMemorySize = ffxGetScratchMemorySizeVK(deviceContext.vkPhysicalDevice, 10);
#else
        spdlog::warn("FFXManager::InitFFX cannot enable Vulkan because Vulkan SDK headers are not available in this build environment.");
        return false;
#endif
    } else {
        spdlog::warn("FFXManager::InitFFX does not support backend {}", static_cast<uint32_t>(backend));
        return false;
    }

    // Why can't FFX allocate this itself?
    m_pScratchMemory = malloc(scratchMemorySize);

    if (!m_pScratchMemory) {
        spdlog::error("Failed to allocate scratch memory for FFX SSRR");
        return false;
	}

    memset(m_pScratchMemory, 0, scratchMemorySize);

    FfxErrorCode backendResult = FFX_OK;
    if (backend == rhi::Backend::D3D12) {
        backendResult = ffxGetInterfaceDX12(&m_backendInterface, ffxDevice, m_pScratchMemory, scratchMemorySize, 1);
    } else {
#if BASICRHI_HAS_VULKAN_HEADERS
        backendResult = ffxGetInterfaceVK(&m_backendInterface, ffxDevice, m_pScratchMemory, scratchMemorySize, 1);
#else
        backendResult = FFX_ERROR_INVALID_POINTER;
#endif
    }
    if (backendResult != FFX_OK) {
        spdlog::error("FFXManager::InitFFX failed to create backend interface for backend {}", static_cast<uint32_t>(backend));
        return false;
    }

	FfxSssrContextDescription sssrDesc{};
	sssrDesc.backendInterface = m_backendInterface;
    sssrDesc.flags = FFX_SSSR_ENABLE_DEPTH_INVERTED;
    sssrDesc.normalsHistoryBufferFormat = FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS;
	sssrDesc.renderSize = { renderRes.x, renderRes.y };

    ffxSssrContextCreate(&m_sssrContext, &sssrDesc);

    return true;
}

void FFXManager::Shutdown() {
	ffxSssrContextDestroy(&m_sssrContext);
    if (m_pScratchMemory) {
        free(m_pScratchMemory);
        m_pScratchMemory = nullptr;
    }
}

void FFXManager::EvaluateSSSR(rhi::CommandList& commandList,
    const Components::Camera* currentCamera,
    PixelBuffer* pHDRTarget,
    PixelBuffer* pDepthTexture,
    PixelBuffer* pNormals,
    PixelBuffer* pMetallicRoughness,
    PixelBuffer* pMotionVectors,
    PixelBuffer* pEnvironmentCubemap,
    PixelBuffer* pBRDFLUT,
    PixelBuffer* pReflectionsTarget) {

	FfxSssrDispatchDescription sssrDesc{};
    sssrDesc.brdfTexture = GetFFXResource(pBRDFLUT, L"BRDFLUT", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.color = GetFFXResource(pHDRTarget, L"HDRColor", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.depth = GetFFXResource(pDepthTexture, L"Depth", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.environmentMap = GetFFXResource(pEnvironmentCubemap, L"EnvironmentMap", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.materialParameters = GetFFXResource(pMetallicRoughness, L"MaterialParameters", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.motionVectors = GetFFXResource(pMotionVectors, L"MotionVectors", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.normal = GetFFXResource(pNormals, L"Normals", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.brdfTexture = GetFFXResource(pBRDFLUT, L"BRDFLUT", FFX_RESOURCE_STATE_COMMON);
    sssrDesc.output = GetFFXResource(pReflectionsTarget, L"Reflections", FFX_RESOURCE_STATE_COMMON);

    auto invViewProjection = DirectX::XMMatrixInverse(nullptr, currentCamera->info.viewProjection);
    auto prevViewProjection = DirectX::XMMatrixMultiply(currentCamera->info.prevView, currentCamera->info.prevJitteredProjection);

    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invViewProjection), invViewProjection);
    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.projection), currentCamera->info.jitteredProjection);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invProjection), currentCamera->info.projectionInverse);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.view), currentCamera->info.view);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invView), currentCamera->info.viewInverse);
    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.prevViewProjection), prevViewProjection);
    if (DeviceManager::GetInstance().GetBackend() == rhi::Backend::D3D12) {
        sssrDesc.commandList = rhi::dx12::get_cmd_list(commandList);
    } else {
#if BASICRHI_HAS_VULKAN_HEADERS
        sssrDesc.commandList = reinterpret_cast<void*>(rhi::vulkan::get_cmd_list(commandList));
#else
        spdlog::warn("FFXManager::EvaluateSSSR skipped Vulkan dispatch because Vulkan SDK headers are not available in this build environment.");
        return;
#endif
    }
    auto renderSize = m_getRenderRes();
    sssrDesc.renderSize = { renderSize.x, renderSize.y };
	sssrDesc.motionVectorScale = { -0.5f, 0.5f };
    sssrDesc.normalUnPackMul = 1.0f;
	sssrDesc.normalUnPackAdd = 0.0f;
	sssrDesc.roughnessChannel = 1; // metallic roughness texture, roughness is in channel 1
    sssrDesc.isRoughnessPerceptual = true;
	sssrDesc.temporalStabilityFactor = 0.7f; // TODO: make everything below configurable
    sssrDesc.iblFactor = 1.0f;
	sssrDesc.depthBufferThickness = 0.015f;
	sssrDesc.roughnessThreshold = 0.3f;
	sssrDesc.varianceThreshold = 0.1f;
	sssrDesc.maxTraversalIntersections = 128;
    sssrDesc.minTraversalOccupancy = 4;
	sssrDesc.mostDetailedMip = 0;
    sssrDesc.samplesPerQuad = 1;
	sssrDesc.temporalVarianceGuidedTracingEnabled = true;

	ffxSssrContextDispatch(&m_sssrContext, &sssrDesc);
    
}
