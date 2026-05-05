#include "FidelityFX/FfxBackendAdapters.h"

#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

#include "Resources/PixelBuffer.h"

#if BASICRHI_HAS_VULKAN_HEADERS
#include "ThirdParty/FFX/host/backends/vk/ffx_vk.h"
#include "ThirdParty/FFX/vk/ffx_api_vk.hpp"
#include "rhi_interop_vulkan.h"

namespace {
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
}

namespace fidelityfx_backend::detail {
bool CreateUpscaleContextVulkan(ffx::Context& context, rhi::Device device, ffx::CreateContextDescUpscale& createUpscaling) {
    ffx::CreateBackendVKDesc backendDesc{};
    backendDesc.vkDevice = rhi::vulkan::get_device(device);
    backendDesc.vkPhysicalDevice = rhi::vulkan::get_physical_device(device);
    backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;
    return ffx::CreateContext(context, nullptr, createUpscaling, backendDesc) == ffx::ReturnCode::Ok;
}

FfxApiResource GetApiResourceVulkan(PixelBuffer* resource, FfxApiResourceState state) {
    const VkImage nativeImage = rhi::vulkan::get_resource(resource->GetAPIResource());
    const VkImageCreateInfo createInfo = BuildVkImageCreateInfo(resource->GetDescription());
    const FfxApiResourceDescription desc = ffxApiGetImageResourceDescriptionVK(nativeImage, createInfo, 0u);
    return ffxApiGetResourceVK(reinterpret_cast<void*>(nativeImage), desc, state);
}

void* GetApiCommandListVulkan(rhi::CommandList& commandList) {
    return reinterpret_cast<void*>(rhi::vulkan::get_cmd_list(commandList));
}

bool CreateHostBackendInterfaceVulkan(FfxInterface& backendInterface, void*& scratchMemory, rhi::Device device, size_t maxContexts) {
    VkDeviceContext deviceContext{};
    deviceContext.vkDevice = rhi::vulkan::get_device(device);
    deviceContext.vkPhysicalDevice = rhi::vulkan::get_physical_device(device);
    deviceContext.vkDeviceProcAddr = vkGetDeviceProcAddr;

    const FfxDevice ffxDevice = ffxGetDeviceVK(&deviceContext);
    const size_t scratchMemorySize = ffxGetScratchMemorySizeVK(deviceContext.vkPhysicalDevice, maxContexts);

    scratchMemory = std::malloc(scratchMemorySize);
    if (scratchMemory == nullptr) {
        return false;
    }

    std::memset(scratchMemory, 0, scratchMemorySize);
    if (ffxGetInterfaceVK(&backendInterface, ffxDevice, scratchMemory, scratchMemorySize, maxContexts) != FFX_OK) {
        std::free(scratchMemory);
        scratchMemory = nullptr;
        return false;
    }

    return true;
}

FfxResource GetHostResourceVulkan(PixelBuffer* resource, const wchar_t* name, FfxResourceStates state) {
    const VkImage nativeImage = rhi::vulkan::get_resource(resource->GetAPIResource());
    const VkImageCreateInfo createInfo = BuildVkImageCreateInfo(resource->GetDescription());
    const FfxResourceDescription desc = ffxGetImageResourceDescriptionVK(nativeImage, createInfo, FFX_RESOURCE_USAGE_READ_ONLY);
    return ffxGetResourceVK(reinterpret_cast<void*>(nativeImage), desc, name, state);
}

void* GetHostCommandListVulkan(rhi::CommandList& commandList) {
    return reinterpret_cast<void*>(rhi::vulkan::get_cmd_list(commandList));
}
}
#else
namespace fidelityfx_backend::detail {
bool CreateUpscaleContextVulkan(ffx::Context&, rhi::Device, ffx::CreateContextDescUpscale&) {
    spdlog::warn("FidelityFX Vulkan upscaling is unavailable because Vulkan headers are not available in this build.");
    return false;
}

FfxApiResource GetApiResourceVulkan(PixelBuffer*, FfxApiResourceState) {
    spdlog::warn("FidelityFX Vulkan upscaling resource access is unavailable because Vulkan headers are not available in this build.");
    return {};
}

void* GetApiCommandListVulkan(rhi::CommandList&) {
    spdlog::warn("FidelityFX Vulkan upscaling command list access is unavailable because Vulkan headers are not available in this build.");
    return nullptr;
}

bool CreateHostBackendInterfaceVulkan(FfxInterface&, void*&, rhi::Device, size_t) {
    spdlog::warn("FidelityFX Vulkan host backend is unavailable because Vulkan headers are not available in this build.");
    return false;
}

FfxResource GetHostResourceVulkan(PixelBuffer*, const wchar_t*, FfxResourceStates) {
    spdlog::warn("FidelityFX Vulkan host resource access is unavailable because Vulkan headers are not available in this build.");
    return {};
}

void* GetHostCommandListVulkan(rhi::CommandList&) {
    spdlog::warn("FidelityFX Vulkan host command list access is unavailable because Vulkan headers are not available in this build.");
    return nullptr;
}
}
#endif