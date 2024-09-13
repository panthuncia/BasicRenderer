#pragma once

#include <d3d12.h>
#include "Scene.h"
#include "wrl/client.h"

class RenderContext {
public:
    Scene* currentScene;
    ID3D12Device* device;
    ID3D12GraphicsCommandList* commandList;
    ID3D12DescriptorHeap* textureDescriptorHeap;
    ID3D12DescriptorHeap* samplerDescriptorHeap;
    ID3D12DescriptorHeap* rtvHeap;
    ID3D12DescriptorHeap* dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> *renderTargets;
    UINT rtvDescriptorSize;
    UINT frameIndex;
    UINT xRes;
    UINT yRes;
};