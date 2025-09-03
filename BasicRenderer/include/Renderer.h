//
// Created by matth on 6/25/2024.
//

#ifndef DX12RENDERER_H
#define DX12RENDERER_H

#include <windows.h>
#include <wrl.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <directxmath.h>
#include <memory>
#include <functional>
#include <flecs.h>

#include <rhi.h>

#include <ThirdParty/Streamline/sl.h>
#include <ThirdParty/Streamline/sl_consts.h>
#include <ThirdParty/Streamline/sl_dlss.h>

#include "Mesh/Mesh.h"
#include "ShaderBuffers.h"
#include "Scene/Scene.h"
#include "Managers/InputManager.h"
#include "Render/RenderGraph.h"
#include "Resources/ShadowMaps.h"
#include "RenderPasses/DebugRenderPass.h"
#include "NsightAftermathGpuCrashTracker.h"
#include "Managers/CameraManager.h"
#include "Managers/LightManager.h"
#include "Managers/MeshManager.h"
#include "Managers/ObjectManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Managers/EnvironmentManager.h"
#include "Scene/MovementState.h"
#include "Scene/Components.h"
#include "../generated/BuiltinResources.h"
#include "Utilities/Timer.h"

using namespace Microsoft::WRL;

class DeferredFunctions {
public:
    // enqueue any void() callable
    void defer(std::function<void()> fn) {
        _queue.emplace_back(std::move(fn));
    }

    // invoke all, then clear
    void flush() {
        for (auto &fn : _queue)
            fn();
        _queue.clear();
    }

    bool empty() const { return _queue.empty(); }

private:
    std::vector<std::function<void()>> _queue;
};

class Renderer {
public:
    Renderer() : m_gpuCrashTracker(m_markerMap){
    }

    void Initialize(HWND hwnd, UINT x_res, UINT y_res);
    void OnResize(UINT newWidth, UINT newHeight);
    void Update(float elapsedSeconds);
    void Render();
    void Cleanup();
    std::shared_ptr<Scene>& GetCurrentScene();
    void SetCurrentScene(std::shared_ptr<Scene> newScene);
    InputManager& GetInputManager();
    void SetInputMode(InputMode mode);
    void SetDebugTexture(std::shared_ptr<PixelBuffer> texture);
    void SetEnvironment(std::string name);
    std::shared_ptr<Scene> AppendScene(std::shared_ptr<Scene> scene);

private:
    ComPtr<IDXGIAdapter1> m_currentAdapter;
    ComPtr<IDXGIFactory7> factory;
    ComPtr<IDXGIFactory7> nativeFactory;
    ComPtr<IDXGIFactory7> slProxyFactory;
    rhi::Device m_device;
    ComPtr<ID3D12Device10> nativeDevice;
    ComPtr<ID3D12Device10> slProxyDevice;

    rhi::SwapchainPtr m_swapChain;

    rhi::DescriptorHeapPtr rtvHeap;
	std::vector<rhi::Resource> renderTargets;
    //ComPtr<ID3D12DescriptorHeap> dsvHeap;
	//std::vector<ComPtr<ID3D12Resource>> depthStencilBuffers;
	//Components::DepthMap m_depthMap;
    std::vector<rhi::CommandAllocatorPtr> m_commandAllocators;
    std::vector<rhi::CommandListPtr> m_commandLists;
    UINT rtvDescriptorSize;
    UINT dsvDescriptorSize;
    uint8_t m_frameIndex = 0;
    uint64_t m_totalFramesRendered = 0;
	uint8_t m_numFramesInFlight = 0;
    rhi::TimelinePtr m_frameFence;
    std::vector<UINT64> m_frameFenceValues; // Store fence values per frame
    UINT64 m_currentFrameFenceValue = 0;

	rhi::TimelinePtr m_readbackFence;

    InputManager inputManager;
    MovementState movementState;
    float verticalAngle = 0;
    float horizontalAngle = 0;

    std::shared_ptr<Scene> currentScene;

    std::shared_ptr<RenderGraph> currentRenderGraph = nullptr;
    bool rebuildRenderGraph = true;

    RenderContext m_context;

	std::string m_environmentName;
	std::unique_ptr<Environment> m_currentEnvironment = nullptr;

    // GPU resource managers
    std::unique_ptr<LightManager> m_pLightManager = nullptr;
    std::unique_ptr<MeshManager> m_pMeshManager = nullptr;
    std::unique_ptr<ObjectManager> m_pObjectManager = nullptr;
    std::unique_ptr<IndirectCommandBufferManager> m_pIndirectCommandBufferManager = nullptr;
    std::unique_ptr<CameraManager> m_pCameraManager = nullptr;
	std::unique_ptr<EnvironmentManager> m_pEnvironmentManager = nullptr;

	ManagerInterface m_managerInterface;
    flecs::system m_hierarchySystem;

    DirectX::XMUINT3 m_lightClusterSize = { 12, 12, 24 };
    FrameTimer m_frameTimer;

    void LoadPipeline(HWND hwnd, UINT x_res, UINT y_res);
    void CreateTextures();
	void TagDLSSResources(ID3D12Resource* pDepthTexture);
    void MoveForward();
    void SetupInputHandlers();
    void CreateGlobalResources();
    void CreateRenderGraph();
    void SetSettings();
    void SetEnvironmentInternal(std::wstring name);
	void ToggleMeshShaders(bool useMeshShaders);

    void WaitForFrame(uint8_t frameIndex);
    void SignalFence(ComPtr<ID3D12CommandQueue> commandQueue, uint8_t currentFrameIndex);
    void AdvanceFrameIndex();
    void CheckDebugMessages();
    void FlushCommandQueue();

    void StallPipeline();

	void RunBeforeNextFrame(std::function<void()> fn) {
		m_preFrameDeferredFunctions.defer(fn);
	}

    // Feature support
	bool m_dlssSupported = false;

	// Settings
	bool m_allowTearing = false;
	bool m_clusteredLighting = true;
    bool m_imageBasedLighting = true;
	bool m_gtaoEnabled = true;
	bool m_deferredRendering = true;
	bool m_occlusionCulling = true;
	bool m_meshletCulling = true;
    bool m_bloom = true;
    bool m_jitter = true;
	bool m_screenSpaceReflections = true;

    std::function<void(ShadowMaps*)> setShadowMaps;
    std::function<void(LinearShadowMaps*)> setLinearShadowMaps;
    std::function<uint16_t()> getShadowResolution;
	std::function<void(float)> setCameraSpeed;
	std::function<float()> getCameraSpeed;
	std::function<void(bool)> setWireframeEnabled;
	std::function<bool()> getWireframeEnabled;
	std::function<void(bool)> setShadowsEnabled;
	std::function<bool()> getShadowsEnabled;
    std::function<uint16_t()> getSkyboxResolution;
	std::function<void(bool)> setImageBasedLightingEnabled;
	std::function<void(std::string)> setEnvironment;
	std::function<bool()> getMeshShadersEnabled;
    std::function<bool()> getIndirectDrawsEnabled;
	std::function<uint8_t()> getNumFramesInFlight;
    std::function<bool()> getDrawBoundingSpheres;
	std::function<bool()> getImageBasedLightingEnabled;

	std::vector<SettingsManager::Subscription> m_settingsSubscriptions;

    GpuCrashTracker::MarkerMap m_markerMap;
    // Nsight Aftermath instrumentation
    GFSDK_Aftermath_ContextHandle m_hAftermathCommandListContext;
    GpuCrashTracker m_gpuCrashTracker;

	DeferredFunctions m_preFrameDeferredFunctions;

    class CoreResourceProvider : public IResourceProvider {
	public:
        std::shared_ptr<ShadowMaps> m_shadowMaps = nullptr;
        std::shared_ptr<LinearShadowMaps> m_linearShadowMaps = nullptr;
        std::shared_ptr<PixelBuffer> m_currentDebugTexture = nullptr;
		std::shared_ptr<Resource> m_primaryCameraMeshletBitfield = nullptr;
        std::shared_ptr<PixelBuffer> m_HDRColorTarget = nullptr;
		std::shared_ptr<PixelBuffer> m_upscaledHDRColorTarget = nullptr;
		std::shared_ptr<PixelBuffer> m_gbufferMotionVectors = nullptr;

		std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override { // TODO: don't use ifs
            if (key.ToString() == Builtin::GBuffer::MotionVectors)
				return m_gbufferMotionVectors;
            if (key.ToString() == Builtin::Color::HDRColorTarget)
				return m_HDRColorTarget;
            if (key.ToString() == Builtin::Shadows::ShadowMaps)
				return m_shadowMaps;
            if (key.ToString() == Builtin::Shadows::LinearShadowMaps)
				return m_linearShadowMaps;
            if (key.ToString() == Builtin::DebugTexture)
				return m_currentDebugTexture;
			if (key.ToString() == Builtin::PrimaryCamera::MeshletBitfield)
				return m_primaryCameraMeshletBitfield;
            if (key.ToString() == Builtin::PostProcessing::UpscaledHDR)
				return m_upscaledHDRColorTarget;
		
			spdlog::error("CoreResourceProvider: ProvideResource called with unknown key: {}", key.ToString());
			return nullptr;
        }

        std::vector<ResourceIdentifier> GetSupportedKeys() override {
			return {
                Builtin::GBuffer::MotionVectors,
                Builtin::Color::HDRColorTarget,
                Builtin::Shadows::ShadowMaps,
                Builtin::Shadows::LinearShadowMaps,
                Builtin::DebugTexture,
                Builtin::PrimaryCamera::MeshletBitfield,
				Builtin::PostProcessing::UpscaledHDR,
			};
        }

    };
	CoreResourceProvider m_coreResourceProvider;
};

#endif //DX12RENDERER_H
