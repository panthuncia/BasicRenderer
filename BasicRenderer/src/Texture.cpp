#include "Texture.h"
#include <string>
#include <stdexcept>

#include "DeviceManager.h"
#include "DirectX/d3dx12.h"
#include "Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

Texture::Texture(const stbi_uc* image, int width, int height, bool sRGB) {
    CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto device = DeviceManager::getInstance().getDevice();
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&textureResource)));

    // Upload texture data to the GPU
    // Create an upload heap and copy the image data to the textureResource
    // This part is omitted for brevity
}