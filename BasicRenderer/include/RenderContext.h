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

    //CD3DX12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVHandle() const {
    //    return CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
    //}

    //CD3DX12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const {
    //    return CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvHeap->GetCPUDescriptorHandleForHeapStart());
    //}
};