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

#include "Mesh.h"

using namespace Microsoft::WRL;

class DX12Renderer {
public:
    void Initialize(HWND hwnd);
    void Render();
    void Cleanup();

private:
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> renderTargets[2];
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    UINT rtvDescriptorSize;
    UINT frameIndex;
    HANDLE fenceEvent;
    UINT64 fenceValue;

    // Cube components
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12RootSignature> rootSignature;
    std::unique_ptr<Mesh> cubeMesh;

    struct ConstantBuffer {
        DirectX::XMMATRIX model;
        DirectX::XMMATRIX view;
        DirectX::XMMATRIX projection;
    };

    // Add a constant buffer resource and view
    ComPtr<ID3D12Resource> constantBuffer;
    UINT8* pConstantBuffer;
    ConstantBuffer cbData;

    void LoadPipeline(HWND hwnd);
    void LoadAssets();
    void CreateConstantBuffer();
    void UpdateConstantBuffer();

    void WaitForPreviousFrame();
    void CheckDebugMessages();
};

#endif //DX12RENDERER_H
