//
// Created by matth on 6/25/2024.
//

#ifndef DX12RENDERER_H
#define DX12RENDERER_H

#define NOMINMAX
#include <windows.h>
#include <wrl.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <directxmath.h>
#include <memory>
#include <functional>
#include <flecs.h>

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

class DX12Renderer {
public:
    DX12Renderer() : m_gpuCrashTracker(m_markerMap){
    }

    void Initialize(HWND hwnd, UINT x_res, UINT y_res);
    void OnResize(UINT newWidth, UINT newHeight);
    void Update(double elapsedSeconds);
    void Render();
    void Cleanup();
    ComPtr<ID3D12Device>& GetDevice();
    std::shared_ptr<Scene>& GetCurrentScene();
    void SetCurrentScene(std::shared_ptr<Scene> newScene);
    InputManager& GetInputManager();
    void SetInputMode(InputMode mode);
    void SetDebugTexture(std::shared_ptr<PixelBuffer> texture);
    void SetEnvironment(std::string name);

private:
    ComPtr<IDXGIFactory7> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12CommandQueue> graphicsQueue;
	ComPtr<ID3D12CommandQueue> computeQueue;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
	std::vector<ComPtr<ID3D12Resource>> renderTargets;
    //ComPtr<ID3D12DescriptorHeap> dsvHeap;
	//std::vector<ComPtr<ID3D12Resource>> depthStencilBuffers;
	std::shared_ptr<PixelBuffer> m_depthStencilBuffer;
    std::vector<ComPtr<ID3D12CommandAllocator>> m_commandAllocators;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> m_commandLists;
    UINT rtvDescriptorSize;
    UINT dsvDescriptorSize;
    uint8_t m_frameIndex = 0;
	uint8_t m_numFramesInFlight = 0;
    ComPtr<ID3D12Fence> m_frameFence;
    std::vector<UINT64> m_frameFenceValues; // Store fence values per frame
    HANDLE m_frameFenceEvent;
    UINT64 m_currentFrameFenceValue = 0;

	ComPtr<ID3D12Fence> m_readbackFence;

    InputManager inputManager;
    MovementState movementState;
    float verticalAngle = 0;
    float horizontalAngle = 0;

    std::shared_ptr<Scene> currentScene;

    std::unique_ptr<RenderGraph> currentRenderGraph = nullptr;
    bool rebuildRenderGraph = true;

    UINT m_xRes;
    UINT m_yRes;

    RenderContext m_context;

	std::string m_environmentName;
	std::unique_ptr<Environment> m_currentEnvironment = nullptr;

    std::shared_ptr<ShadowMaps> m_shadowMaps = nullptr;
	std::shared_ptr<PixelBuffer> m_currentDebugTexture = nullptr;

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

    void LoadPipeline(HWND hwnd, UINT x_res, UINT y_res);
    void CreateTextures();
    void MoveForward();
    void SetupInputHandlers(InputManager& inputManager, InputContext& context);
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

	// Settings
	bool m_allowTearing = false;
	bool m_clusteredLighting = true;
    bool m_imageBasedLighting = true;
	bool m_gtaoEnabled = true;
	bool m_deferredRendering = false;

    std::function<void(ShadowMaps*)> setShadowMaps;
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

    GpuCrashTracker::MarkerMap m_markerMap;
    // Nsight Aftermath instrumentation
    GFSDK_Aftermath_ContextHandle m_hAftermathCommandListContext;
    GpuCrashTracker m_gpuCrashTracker;

	DeferredFunctions m_preFrameDeferredFunctions;
};

#endif //DX12RENDERER_H
