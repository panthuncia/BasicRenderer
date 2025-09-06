#pragma once

#include <dxgi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include <DirectXMath.h>
#include <functional>

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>

#include <rhi.h>

#include "ThirdParty/FFX/dx12/ffx_api_dx12.hpp"
#include "ThirdParty/FFX/ffx_api_loader.h"
#include "ThirdParty/FFX/ffx_api.hpp"
#include "ThirdParty/FFX/ffx_upscale.hpp"

enum class UpscalingMode {
    None,
    FSR3,
    DLSS
};

static constexpr const char* UpscalingModeNames[] = {
    "None",
    "FSR3",
    "DLSS",
};
static constexpr int UpscalingModeCount = ARRAYSIZE(UpscalingModeNames);

enum class UpscaleQualityMode {
    DLAA,
	//UltraQuality, // DLSS UltraQuality returns a resolution of 0? What is this?
    Quality,
    Balanced,
    Performance,
    UltraPerformance
};

static constexpr const char* UpscaleQualityModeNames[] = {
    "DLAA",
    //"UltraQuality",
    "Quality",
    "Balanced",
    "Performance",
    "UltraPerformance"
};
static constexpr int UpscaleQualityModeCount = ARRAYSIZE(UpscaleQualityModeNames);

inline FfxApiUpscaleQualityMode ToFFXQualityMode(UpscaleQualityMode mode) {
    switch (mode) {
    case UpscaleQualityMode::DLAA:
        return FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
    //case UpscaleQualityMode::UltraQuality:
        //return FFX_UPSCALE_QUALITY_MODE_QUALITY; // FFX does not have a separate UltraQuality mode
    case UpscaleQualityMode::Quality:
        return FFX_UPSCALE_QUALITY_MODE_QUALITY;
    case UpscaleQualityMode::Balanced:
        return FFX_UPSCALE_QUALITY_MODE_BALANCED;
    case UpscaleQualityMode::Performance:
        return FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
    case UpscaleQualityMode::UltraPerformance:
        return FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE;
    default:
        return FFX_UPSCALE_QUALITY_MODE_BALANCED; // Default to balanced
    }
}

inline sl::DLSSMode ToSLQualityMode(UpscaleQualityMode mode) {
    switch (mode) {
    case UpscaleQualityMode::DLAA:
        return sl::DLSSMode::eDLAA;
    //case UpscaleQualityMode::UltraQuality:
        //return sl::DLSSMode::eUltraQuality;
    case UpscaleQualityMode::Quality:
        return sl::DLSSMode::eMaxQuality;
    case UpscaleQualityMode::Balanced:
        return sl::DLSSMode::eBalanced;
    case UpscaleQualityMode::Performance:
        return sl::DLSSMode::eMaxPerformance;
    case UpscaleQualityMode::UltraPerformance:
        return sl::DLSSMode::eUltraPerformance;
    default:
        return sl::DLSSMode::eBalanced; // Default to balanced
    }
}

class PixelBuffer;
class RenderContext;

class UpscalingManager {
public:
    static UpscalingManager& GetInstance();
    void InitializeAdapter();
	ID3D12Device10* ProxyDevice(Microsoft::WRL::ComPtr<ID3D12Device10>& device);
	IDXGIFactory7* ProxyFactory(Microsoft::WRL::ComPtr<IDXGIFactory7>& factory);
    void Setup();
    void Evaluate(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors);
	void Shutdown();

    bool InitSL();
	bool InitFFX();
    DirectX::XMFLOAT2 GetJitter(uint64_t frameNumber);
	UpscalingMode GetCurrentUpscalingMode() const { return m_upscalingMode; }
    UpscaleQualityMode GetCurrentUpscalingQualityMode() const { return m_upscaleQualityMode; }

    void SetUpscalingMode(UpscalingMode mode) { m_upscalingMode = mode; }
    void SetUpscalingQualityMode(UpscaleQualityMode mode) { m_upscaleQualityMode = mode; }

private:
    UpscalingManager() = default;
    void EvaluateDLSS(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors);
    void EvaluateFSR3(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors);
	void EvaluateNone(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors);
	UpscalingMode m_upscalingMode = UpscalingMode::None;
    UpscaleQualityMode m_upscaleQualityMode = UpscaleQualityMode::DLAA;
    std::vector<sl::FrameToken*> m_frameTokens; // Frame tokens for each frame in flight
    uint8_t m_numFramesInFlight;
    std::function<DirectX::XMUINT2()> m_getRenderRes;
	std::function<DirectX::XMUINT2()> m_getOutputRes;
    ffx::Context m_fsrUpscalingContext;
};

inline UpscalingManager& UpscalingManager::GetInstance() {
    static UpscalingManager instance;
    return instance;
}