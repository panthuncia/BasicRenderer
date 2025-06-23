#pragma once

#pragma once

#include <dxgi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include <DirectXMath.h>
#include <functional>

#include "ThirdParty/FFX/dx12/ffx_api_dx12.hpp"
#include "ThirdParty/FFX/ffx_api_loader.h"
#include "ThirdParty/FFX/ffx_api.hpp"
#include "ThirdParty/FFX/host/ffx_sssr.h"


class PixelBuffer;
class RenderContext;
class Buffer;

class FFXManager {
public:
    static FFXManager& GetInstance();
    void Setup();
    void Evaluate(const RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors);
    void Shutdown();

    bool InitFFX();

private:
    FFXManager() = default;
    uint8_t m_numFramesInFlight;
    std::function<DirectX::XMUINT2()> m_getRenderRes;
    std::function<DirectX::XMUINT2()> m_getOutputRes;
    std::shared_ptr<Buffer> m_scratchMemory;
    FfxInterface m_backendInterface;
	FfxSssrContext m_sssrContext;
};

inline FFXManager& FFXManager::GetInstance() {
    static FFXManager instance;
    return instance;
}