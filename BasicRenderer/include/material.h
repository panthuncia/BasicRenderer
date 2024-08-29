#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <string>
#include "Texture.h"
#include "ResourceManager.h"
#include "BlendState.h"

using Microsoft::WRL::ComPtr;

class Material {
public:
    std::string name;
    UINT psoFlags;
    std::shared_ptr<Texture> baseColorTexture;
    std::shared_ptr<Texture> normalTexture;
    std::shared_ptr<Texture> aoMap;
    std::shared_ptr<Texture> heightMap;
    std::shared_ptr<Texture> metallicRoughnessTexture;
    std::shared_ptr<Texture> emissiveTexture;
    float metallicFactor;
    float roughnessFactor;
    DirectX::XMFLOAT4 baseColorFactor;
    DirectX::XMFLOAT4 emissiveFactor;
    BlendState blendState;
    PerMaterialCB materialData = {0};

    Material(const std::string& name,
        UINT psoFlags);

    Material(const std::string& name,
        UINT psoFlags,
        std::shared_ptr<Texture> baseColorTexture,
        std::shared_ptr<Texture> normalTexture,
        std::shared_ptr<Texture> aoMap,
        std::shared_ptr<Texture> heightMap,
        std::shared_ptr<Texture> metallicRoughnessTexture,
        float metallicFactor,
        float roughnessFactor,
        DirectX::XMFLOAT4 baseColorFactor,
        DirectX::XMFLOAT4 emissiveFactor,
        BlendState blendState);

    static std::shared_ptr<Texture> createDefaultTexture();
    UINT GetMaterialBufferIndex();

private:
    BufferHandle<PerMaterialCB> perMaterialHandle;
};