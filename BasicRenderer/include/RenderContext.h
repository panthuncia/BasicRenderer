#pragma once

#include <directx/d3d12.h>
#include "wrl/client.h"

class Scene;

class RenderContext {
public:
    Scene* currentScene;
    ID3D12Device* device;
    ID3D12GraphicsCommandList* commandList;
	ID3D12CommandQueue* commandQueue;
    ID3D12DescriptorHeap* textureDescriptorHeap;
    ID3D12DescriptorHeap* samplerDescriptorHeap;
    ID3D12DescriptorHeap* rtvHeap;
    ID3D12DescriptorHeap* dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> *renderTargets;
    UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
    UINT frameIndex;
    UINT xRes;
    UINT yRes;
};