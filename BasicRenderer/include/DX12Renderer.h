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

#include "Mesh.h"
#include "Buffers.h"
#include "Scene.h"
#include "InputManager.h"
#include "RenderGraph.h"
#include "ShadowMaps.h"
#include "RenderPasses/DebugRenderPass.h"
#include "ReadbackRequest.h"

using namespace Microsoft::WRL;

class DX12Renderer {
public:
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
    void SetDebugTexture(Texture* texture);
    void SetEnvironment(std::string name);
    void SetSkybox(std::shared_ptr<Texture> texture);
	void SetIrradiance(std::shared_ptr<Texture> texture);
	void SetPrefilteredEnvironment(std::shared_ptr<Texture> texture);
    void SetEnvironmentTexture(std::shared_ptr<Texture> texture, std::string environmentName);
    void SubmitReadbackRequest(ReadbackRequest&& request);
    std::vector<ReadbackRequest>& GetPendingReadbackRequests();
    std::shared_ptr<SceneNode> AppendScene(Scene& scene);

private:
    ComPtr<IDXGIFactory7> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
	std::vector<ComPtr<ID3D12Resource>> renderTargets;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
	std::vector<ComPtr<ID3D12Resource>> depthStencilBuffers;
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

	std::shared_ptr<Texture> m_currentSkybox = nullptr;
	std::shared_ptr<Texture> m_currentEnvironmentTexture = nullptr;
	std::shared_ptr<Texture> m_environmentIrradiance = nullptr;
	std::shared_ptr<Texture> m_prefilteredEnvironment = nullptr;
	std::shared_ptr<Texture> m_lutTexture = nullptr;
	std::string m_environmentName;

    std::shared_ptr<ShadowMaps> m_shadowMaps = nullptr;

    std::mutex readbackRequestsMutex;
	std::vector<ReadbackRequest> m_readbackRequests;

    void LoadPipeline(HWND hwnd, UINT x_res, UINT y_res);
    void MoveForward();
    void SetupInputHandlers(InputManager& inputManager, InputContext& context);
    void CreateGlobalResources();
    void CreateRenderGraph();
    void SetSettings();
    void SetEnvironmentInternal(std::wstring name);

    void WaitForFrame(uint8_t frameIndex);
    void SignalFence(ComPtr<ID3D12CommandQueue> commandQueue, uint8_t currentFrameIndex);
    void AdvanceFrameIndex();
    void CheckDebugMessages();
    void FlushCommandQueue();

    void ProcessReadbackRequests();

	// Settings
	bool m_allowTearing = false;

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
};

#endif //DX12RENDERER_H
