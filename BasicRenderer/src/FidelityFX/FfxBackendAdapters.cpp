#include "FidelityFX/FfxBackendAdapters.h"

#include <spdlog/spdlog.h>

namespace fidelityfx_backend::detail {
bool CreateUpscaleContextDX12(ffx::Context& context, rhi::Device device, ffx::CreateContextDescUpscale& createUpscaling);
bool CreateUpscaleContextVulkan(ffx::Context& context, rhi::Device device, ffx::CreateContextDescUpscale& createUpscaling);
FfxApiResource GetApiResourceDX12(PixelBuffer* resource, FfxApiResourceState state);
FfxApiResource GetApiResourceVulkan(PixelBuffer* resource, FfxApiResourceState state);
void* GetApiCommandListDX12(rhi::CommandList& commandList);
void* GetApiCommandListVulkan(rhi::CommandList& commandList);

bool CreateHostBackendInterfaceDX12(FfxInterface& backendInterface, void*& scratchMemory, rhi::Device device, size_t maxContexts);
bool CreateHostBackendInterfaceVulkan(FfxInterface& backendInterface, void*& scratchMemory, rhi::Device device, size_t maxContexts);
FfxResource GetHostResourceDX12(PixelBuffer* resource, const wchar_t* name, FfxResourceStates state);
FfxResource GetHostResourceVulkan(PixelBuffer* resource, const wchar_t* name, FfxResourceStates state);
void* GetHostCommandListDX12(rhi::CommandList& commandList);
void* GetHostCommandListVulkan(rhi::CommandList& commandList);
}

namespace {
const char* BackendName(rhi::Backend backend) {
    switch (backend) {
    case rhi::Backend::D3D12:
        return "D3D12";
    case rhi::Backend::Vulkan:
        return "Vulkan";
    default:
        return "Unknown";
    }
}
}

bool fidelityfx_backend::api::CreateUpscaleContext(ffx::Context& context, rhi::Backend backend, rhi::Device device, ffx::CreateContextDescUpscale& createUpscaling) {
    switch (backend) {
    case rhi::Backend::D3D12:
        return detail::CreateUpscaleContextDX12(context, device, createUpscaling);
    case rhi::Backend::Vulkan:
        return detail::CreateUpscaleContextVulkan(context, device, createUpscaling);
    default:
        spdlog::warn("FidelityFX upscale does not support backend {}", static_cast<uint32_t>(backend));
        return false;
    }
}

FfxApiResource fidelityfx_backend::api::GetResource(rhi::Backend backend, PixelBuffer* resource, const wchar_t* name, FfxApiResourceState state) {
    switch (backend) {
    case rhi::Backend::D3D12:
        return detail::GetApiResourceDX12(resource, state);
    case rhi::Backend::Vulkan:
        return detail::GetApiResourceVulkan(resource, state);
    default:
        spdlog::error("Failed to get FFX API resource '{}' for unsupported backend {}", name ? "<named>" : "<unnamed>", BackendName(backend));
        return {};
    }
}

void* fidelityfx_backend::api::GetCommandList(rhi::Backend backend, rhi::CommandList& commandList) {
    switch (backend) {
    case rhi::Backend::D3D12:
        return detail::GetApiCommandListDX12(commandList);
    case rhi::Backend::Vulkan:
        return detail::GetApiCommandListVulkan(commandList);
    default:
        spdlog::error("Failed to get FFX API command list for unsupported backend {}", BackendName(backend));
        return nullptr;
    }
}

bool fidelityfx_backend::host::CreateBackendInterface(FfxInterface& backendInterface, void*& scratchMemory, rhi::Backend backend, rhi::Device device, size_t maxContexts) {
    switch (backend) {
    case rhi::Backend::D3D12:
        return detail::CreateHostBackendInterfaceDX12(backendInterface, scratchMemory, device, maxContexts);
    case rhi::Backend::Vulkan:
        return detail::CreateHostBackendInterfaceVulkan(backendInterface, scratchMemory, device, maxContexts);
    default:
        spdlog::warn("FidelityFX host backend does not support backend {}", static_cast<uint32_t>(backend));
        return false;
    }
}

FfxResource fidelityfx_backend::host::GetResource(rhi::Backend backend, PixelBuffer* resource, const wchar_t* name, FfxResourceStates state) {
    switch (backend) {
    case rhi::Backend::D3D12:
        return detail::GetHostResourceDX12(resource, name, state);
    case rhi::Backend::Vulkan:
        return detail::GetHostResourceVulkan(resource, name, state);
    default:
        spdlog::error("Failed to get FFX host resource '{}' for unsupported backend {}", name ? "<named>" : "<unnamed>", BackendName(backend));
        return {};
    }
}

void* fidelityfx_backend::host::GetCommandList(rhi::Backend backend, rhi::CommandList& commandList) {
    switch (backend) {
    case rhi::Backend::D3D12:
        return detail::GetHostCommandListDX12(commandList);
    case rhi::Backend::Vulkan:
        return detail::GetHostCommandListVulkan(commandList);
    default:
        spdlog::error("Failed to get FFX host command list for unsupported backend {}", BackendName(backend));
        return nullptr;
    }
}