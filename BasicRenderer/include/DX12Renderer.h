//
// Created by matth on 6/25/2024.
//

#ifndef DX12RENDERER_H
#define DX12RENDERER_H

#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
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
#include "DebugRenderPass.h"

using namespace Microsoft::WRL;

class DX12Renderer {
public:
    void Initialize(HWND hwnd, UINT x_res, UINT y_res);
    void Update(double elapsedSeconds);
    void Render();
    void Cleanup();
    ComPtr<ID3D12Device>& GetDevice();
    std::shared_ptr<Scene>& GetCurrentScene();
    void SetCurrentScene(std::shared_ptr<Scene> newScene);
    InputManager& GetInputManager();
    void SetInputMode(InputMode mode);
    void SetDebugTexture(Texture* texture) {
		debugPass->SetTexture(texture);
    }

private:
    ComPtr<IDXGIFactory6> factory;
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
    UINT frameIndex;
    HANDLE fenceEvent;
    UINT64 fenceValue;

    InputManager inputManager;
    MovementState movementState;
    float verticalAngle = 0;
    float horizontalAngle = 0;

    std::shared_ptr<Scene> currentScene;

    std::unique_ptr<RenderGraph> currentRenderGraph;

    UINT m_xRes;
    UINT m_yRes;

    std::shared_ptr<DebugRenderPass> debugPass;

    void LoadPipeline(HWND hwnd, UINT x_res, UINT y_res);
    void MoveForward();
    void SetupInputHandlers(InputManager& inputManager, InputContext& context);
    void CreateRenderGraph();
    void SetSettings();

    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignatureFromShaders(const std::vector<Microsoft::WRL::ComPtr<ID3DBlob>>& shaderBlobs);

    void WaitForPreviousFrame();
    void CheckDebugMessages();

    std::function<void(ShadowMaps*)> setShadowMaps;
    std::function<uint16_t()> getShadowResolution;
};

#endif //DX12RENDERER_H
