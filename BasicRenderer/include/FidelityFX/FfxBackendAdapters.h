#pragma once

#include <cstddef>

#include <rhi.h>

#include "ThirdParty/FFX/ffx_api.hpp"
#include "ThirdParty/FFX/ffx_upscale.hpp"
#include "ThirdParty/FFX/host/ffx_sssr.h"

class PixelBuffer;

namespace fidelityfx_backend::api {
bool CreateUpscaleContext(ffx::Context& context, rhi::Backend backend, rhi::Device device, ffx::CreateContextDescUpscale& createUpscaling);
FfxApiResource GetResource(rhi::Backend backend, PixelBuffer* resource, const wchar_t* name, FfxApiResourceState state);
void* GetCommandList(rhi::Backend backend, rhi::CommandList& commandList);
}

namespace fidelityfx_backend::host {
bool CreateBackendInterface(FfxInterface& backendInterface, void*& scratchMemory, rhi::Backend backend, rhi::Device device, size_t maxContexts);
FfxResource GetResource(rhi::Backend backend, PixelBuffer* resource, const wchar_t* name, FfxResourceStates state);
void* GetCommandList(rhi::Backend backend, rhi::CommandList& commandList);
}