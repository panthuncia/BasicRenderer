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
    template <typename T>
    void MarkForDelete(const std::shared_ptr<T>& ptr) {
        m_stuffToDelete.push_back(std::static_pointer_cast<void>(ptr));
    }

private:
    ComPtr<IDXGIFactory7> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> renderTargets[2];
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> depthStencilBuffer;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    UINT rtvDescriptorSize;
    UINT dsvDescriptorSize;
    UINT frameIndex = 0;
    HANDLE fenceEvent;
    UINT64 fenceValue;

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

    std::vector<std::shared_ptr<void>> m_stuffToDelete; // Some things need deferred deletion

    void LoadPipeline(HWND hwnd, UINT x_res, UINT y_res);
    void MoveForward();
    void SetupInputHandlers(InputManager& inputManager, InputContext& context);
    void CreateGlobalResources();
    void CreateRenderGraph();
    void SetSettings();
    void SetEnvironmentInternal(std::wstring name);

    void WaitForPreviousFrame();
    void CheckDebugMessages();

    void ProcessReadbackRequests();

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
};

#endif //DX12RENDERER_H
