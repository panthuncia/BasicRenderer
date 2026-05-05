#include "FidelityFX/FfxBackendAdapters.h"

#include <cstdlib>
#include <cstring>

#include "Resources/PixelBuffer.h"
#include "ThirdParty/FFX/dx12/ffx_api_dx12.hpp"
#include "ThirdParty/FFX/host/backends/dx12/ffx_dx12.h"
#include "rhi_interop_dx12.h"

namespace fidelityfx_backend::detail {
bool CreateUpscaleContextDX12(ffx::Context& context, rhi::Device device, ffx::CreateContextDescUpscale& createUpscaling) {
    ffx::CreateBackendDX12Desc backendDesc{};
    backendDesc.device = rhi::dx12::get_device(device);
    return ffx::CreateContext(context, nullptr, createUpscaling, backendDesc) == ffx::ReturnCode::Ok;
}

FfxApiResource GetApiResourceDX12(PixelBuffer* resource, FfxApiResourceState state) {
    auto* nativeResource = rhi::dx12::get_resource(resource->GetAPIResource());
    return ffxApiGetResourceDX12(nativeResource, state);
}

void* GetApiCommandListDX12(rhi::CommandList& commandList) {
    return rhi::dx12::get_cmd_list(commandList);
}

bool CreateHostBackendInterfaceDX12(FfxInterface& backendInterface, void*& scratchMemory, rhi::Device device, size_t maxContexts) {
    const FfxDevice ffxDevice = ffxGetDeviceDX12(rhi::dx12::get_device(device));
    const size_t scratchMemorySize = ffxGetScratchMemorySizeDX12(maxContexts);

    scratchMemory = std::malloc(scratchMemorySize);
    if (scratchMemory == nullptr) {
        return false;
    }

    std::memset(scratchMemory, 0, scratchMemorySize);
    if (ffxGetInterfaceDX12(&backendInterface, ffxDevice, scratchMemory, scratchMemorySize, maxContexts) != FFX_OK) {
        std::free(scratchMemory);
        scratchMemory = nullptr;
        return false;
    }

    return true;
}

FfxResource GetHostResourceDX12(PixelBuffer* resource, const wchar_t* name, FfxResourceStates state) {
    auto* nativeResource = rhi::dx12::get_resource(resource->GetAPIResource());
    const FfxResourceDescription desc = ffxGetResourceDescriptionDX12(nativeResource, FFX_RESOURCE_USAGE_READ_ONLY);
    return ffxGetResourceDX12(nativeResource, desc, name, state);
}

void* GetHostCommandListDX12(rhi::CommandList& commandList) {
    return rhi::dx12::get_cmd_list(commandList);
}
}