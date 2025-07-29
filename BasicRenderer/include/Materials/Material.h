#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <string>
#include "Resources/Texture.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/BlendState.h"
#include "Render/PSOFlags.h"
#include "Materials/MaterialFlags.h"
#include "Materials/MaterialDescription.h"

using Microsoft::WRL::ComPtr;

class Material {
public:
    static std::shared_ptr<Material> CreateShared(const std::string& name,
        MaterialFlags materialFlags, PSOFlags psoFlags,
        std::shared_ptr<Texture> baseColorTexture,
        std::shared_ptr<Texture> normalTexture,
        std::shared_ptr<Texture> aoMap,
        std::shared_ptr<Texture> heightMap,
        std::shared_ptr<Texture> metallicTexture,
        std::shared_ptr<Texture> roughnessTexture,
        std::shared_ptr<Texture> emissiveTexture,
        float metallicFactor,
        float roughnessFactor,
        DirectX::XMFLOAT4 baseColorFactor,
        DirectX::XMFLOAT4 emissiveFactor,
        BlendState blendState,
        float alphaCutoff) {
        return std::shared_ptr<Material>(new Material(name, materialFlags, psoFlags,
            baseColorTexture, normalTexture, aoMap, heightMap,
            metallicTexture, roughnessTexture, emissiveTexture,
            metallicFactor, roughnessFactor, baseColorFactor, emissiveFactor,
            blendState, alphaCutoff));
    }
    static std::shared_ptr<Material> CreateShared(const MaterialDescription& desc) {
        uint32_t materialFlags = 0;
		uint32_t psoFlags = 0;

        if (desc.baseColor.texture) {
            if (!desc.baseColor.texture->AlphaIsAllOpaque()) {
                materialFlags |= MaterialFlags::MATERIAL_DOUBLE_SIDED;
                psoFlags |= PSOFlags::PSO_ALPHA_TEST;
			}
			materialFlags |= MaterialFlags::MATERIAL_BASE_COLOR_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
        }
        if (desc.metallic.texture) {
            materialFlags |= MaterialFlags::MATERIAL_PBR | MaterialFlags::MATERIAL_PBR_MAPS | MaterialFlags::MATERIAL_TEXTURED;
        }
        if (desc.roughness.texture) {
            materialFlags |= MaterialFlags::MATERIAL_PBR | MaterialFlags::MATERIAL_PBR_MAPS | MaterialFlags::MATERIAL_TEXTURED;
		}
        if (desc.emissive.texture) {
            materialFlags |= MaterialFlags::MATERIAL_EMISSIVE_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
		}
        if (desc.normal.texture) {
            materialFlags |= MaterialFlags::MATERIAL_NORMAL_MAP | MaterialFlags::MATERIAL_TEXTURED;
        }

        return CreateShared(
            desc.name,
            static_cast<MaterialFlags>(materialFlags),
            static_cast<PSOFlags>(psoFlags),
            desc.baseColor.texture,
            desc.normal.texture,
            desc.aoMap.texture,
            desc.heightMap.texture,
            desc.metallic.texture,
            desc.roughness.texture,
            desc.emissive.texture,
            desc.metallic.factor,
            desc.roughness.factor,
            desc.diffuseColor,
            desc.emissiveColor,
            BlendState::BLEND_STATE_OPAQUE, // Default blend state
            0.5f // Default alpha cutoff
		);
    }
    ~Material();

    UINT GetMaterialBufferIndex();
    void SetHeightmap(std::shared_ptr<Texture> heightmap);
    void SetTextureScale(float scale);
    void SetHeightmapScale(float scale);
    PSOFlags GetPSOFlags() const { return m_psoFlags; }
    MaterialFlags GetMaterialFlags() const { return static_cast<MaterialFlags>(m_materialData.materialFlags); }
    BlendState GetBlendState() const { return m_blendState; }
    static std::shared_ptr<Material> GetDefaultMaterial();
    static void DestroyDefaultMaterial() {
        defaultMaterial.reset();
    }
private:

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
    PerMaterialCB m_materialData = { 0 };
    PSOFlags m_psoFlags;

    Material(const std::string& name,
        MaterialFlags materialFlags, PSOFlags psoFlags);

    Material(const std::string& name,
        MaterialFlags materialFlags, PSOFlags psoFlags,
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

    std::shared_ptr<Buffer> m_perMaterialHandle;
    inline static std::shared_ptr<Material> defaultMaterial;
};