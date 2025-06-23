#include "Managers/Singletons/FFXManager.h"

#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/PixelBuffer.h"
#include "ThirdParty/FFX/dx12/ffx_api_dx12.hpp"
#include "ThirdParty/FFX/host/ffx_sssr.h"
#include "ThirdParty/FFX/host/backends/dx12/ffx_dx12.h"
#include "Managers/Singletons/ResourceManager.h"

//ffxFunctions ffxModule;
//
//FfxApiResource getFFXResource(Resource* resource, const wchar_t* name, FfxApiResourceState state) {
//    //auto desc = ffx::ApiGetResourceDescriptionDX12(resource->GetAPIResource(), FFX_API_RESOURCE_USAGE_READ_ONLY);
//    auto ffxResource = ffxApiGetResourceDX12(resource->GetAPIResource(), state);
//    if (ffxResource.resource == nullptr) {
//        spdlog::error("Failed to get FFX resource for resource");
//    }
//    return ffxResource;
//}

bool FFXManager::InitFFX() {
    m_getRenderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution");
    m_getOutputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution");
    auto outputRes = m_getOutputRes();
    auto renderRes = m_getRenderRes();
    auto module = LoadLibrary(L"FFX/amd_fidelityfx_dx12.dll");
    if (module) {
        //ffxLoadFunctions(&ffxModule, module);

        auto device = ffxGetDeviceDX12(DeviceManager::GetInstance().GetDevice().Get());
        auto scratchMemorySize = ffxGetScratchMemorySizeDX12(1);

        m_scratchMemory = ResourceManager::GetInstance().CreateBuffer(scratchMemorySize, nullptr);

        ffxGetInterfaceDX12(&m_backendInterface, device, m_scratchMemory->GetAPIResource(), scratchMemorySize, 1);

		//FfxSssrContextDescription sssrDesc{};
		//sssrDesc.backendInterface = m_backendInterface;
  //      sssrDesc.normalsHistoryBufferFormat = FFX_

  //      ffxSssrContextCreate(&m_sssrContext, )

        return true;
    }
    return false;
}