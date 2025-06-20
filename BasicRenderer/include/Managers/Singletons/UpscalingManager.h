#pragma once

#include <dxgi.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include <DirectXMath.h>
#include <functional>

#include <ThirdParty/Streamline/sl.h>
#include <ThirdParty/Streamline/sl_consts.h>
#include <ThirdParty/Streamline/sl_dlss.h>
#include <ThirdParty/Streamline/sl_matrix_helpers.h>

enum class UpscalingMode {
    None,
    FSR3,
    DLSS
};

class PixelBuffer;
class RenderContext;

class UpscalingManager {
public:
    static UpscalingManager& GetInstance();
    void InitializeAdapter(Microsoft::WRL::ComPtr<IDXGIAdapter1>& adapter);
	void SetDevice(Microsoft::WRL::ComPtr<ID3D12Device10>& device);
    void Setup();
    void Evaluate(const RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors);
	void Shutdown();

private:
    UpscalingManager() = default;
    void EvaluateDLSS(const RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors);
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_currentAdapter;
	UpscalingMode m_upscalingMode = UpscalingMode::None;
    std::vector<sl::FrameToken*> m_frameTokens; // Frame tokens for each frame in flight
    uint8_t m_numFramesInFlight;
    std::function<DirectX::XMUINT2()> m_getRenderRes;
	std::function<DirectX::XMUINT2()> m_getOutputRes;
};

inline UpscalingManager& UpscalingManager::GetInstance() {
    static UpscalingManager instance;
    return instance;
}