#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <string>
#include "Texture.h"
#include "ResourceManager.h"

using Microsoft::WRL::ComPtr;

class Material {
public:
    std::string name;
    UINT psoFlags;
    Texture* baseColorTexture;
    Texture* normalTexture;
    Texture* aoMap;
    Texture* heightMap;
    Texture* metallicRoughnessTexture;
    Texture* emissiveTexture;
    float metallicFactor;
    float roughnessFactor;
    DirectX::XMFLOAT4 baseColorFactor;
    DirectX::XMFLOAT4 emissiveFactor;
    int blendMode;
    PerMaterialCB materialData = {0};
    BufferHandle<PerMaterialCB> perMaterialHandle;

    Material(const std::string& name,
        UINT psoFlags);

    Material(const std::string& name,
        UINT psoFlags,
        Texture* baseColorTexture,
        Texture* normalTexture,
        Texture* aoMap,
        Texture* heightMap,
        Texture* metallicRoughnessTexture,
        float metallicFactor,
        float roughnessFactor,
        DirectX::XMFLOAT4 baseColorFactor,
        DirectX::XMFLOAT4 emissiveFactor,
        int blendMode);

    static Texture* createDefaultTexture();
    // Other methods...
};