#pragma once
#include "wrl.h"
#include "stb/stb_image.h"
#include "d3d12.h"
#include "DirectX/d3dx12.h"
using Microsoft::WRL::ComPtr;

class Texture {
public:
    Texture(const stbi_uc* image, int width, int height, bool sRGB);
    UINT GetDescriptorIndex();
private:
    ComPtr<ID3D12Resource> textureResource;
    ComPtr<ID3D12Resource> textureUploadHeap;
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    UINT descriptorIndex;
};