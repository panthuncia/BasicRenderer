#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <string>
#include "Resources/Texture.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/BlendState.h"

using Microsoft::WRL::ComPtr;

class Material {
public:
    std::string m_name;
    std::shared_ptr<Texture> m_baseColorTexture;
    std::shared_ptr<Texture> m_normalTexture;
    std::shared_ptr<Texture> m_aoMap;
    std::shared_ptr<Texture> m_heightMap;
    std::shared_ptr<Texture> m_roughnessTexture;
    std::shared_ptr<Texture> m_metallicTexture;
    std::shared_ptr<Texture> m_emissiveTexture;
    float m_metallicFactor;
    float m_roughnessFactor;
    DirectX::XMFLOAT4 m_baseColorFactor;
    DirectX::XMFLOAT4 m_emissiveFactor;
    BlendState m_blendState;
    PerMaterialCB m_materialData = {0};
    UINT m_psoFlags;

    Material(const std::string& name,
        UINT materialFlags, UINT psoFlags);

    Material(const std::string& name,
		UINT materialFlags, UINT psoFlags,
        std::shared_ptr<Texture> baseColorTexture,
        std::shared_ptr<Texture> normalTexture,
        std::shared_ptr<Texture> aoMap,
        std::shared_ptr<Texture> heightMap,
        std::shared_ptr<Texture> metallicTexture,
        std::shared_ptr<Texture> m_roughnessTexture,
        std::shared_ptr<Texture> emissiveTexture,
        float metallicFactor,
        float roughnessFactor,
        DirectX::XMFLOAT4 baseColorFactor,
        DirectX::XMFLOAT4 emissiveFactor,
        BlendState blendState,
        float alphaCutoff);
    ~Material();

    static std::shared_ptr<Texture> createDefaultTexture();
    UINT GetMaterialBufferIndex();
    void SetHeightmap(std::shared_ptr<Texture> heightmap);
    void SetTextureScale(float scale);
    void SetHeightmapScale(float scale);
private:
    std::shared_ptr<Buffer> m_perMaterialHandle;
};