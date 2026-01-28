#pragma once

#include <DirectXMath.h>
#include <string>
#include <array>
#include <unordered_set>
#include "Resources/Texture.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/BlendState.h"
#include "Render/PSOFlags.h"
#include "Render/RenderPhase.h"
#include "Materials/MaterialFlags.h"
#include "Materials/MaterialDescription.h"
#include "../generated/BuiltinRenderPasses.h"
#include "Materials/TechniqueDescriptor.h"
#include "Factories/TextureFactory.h"

struct TransparencyPick { bool isTransparent = false; bool masked = false; };

inline TransparencyPick PickTransparency(const MaterialDescription& d) {
    TransparencyPick t{};
    const bool hasOpacityTex = (d.opacity.texture != nullptr);
    const bool explicitBlend = (d.blendState == BlendState::BLEND_STATE_BLEND);
    const bool alphaFactor = (d.opacity.factor.Get() < 1.0f);

    t.isTransparent = hasOpacityTex || explicitBlend || alphaFactor || d.blendState == BlendState::BLEND_STATE_MASK;
    if (!t.isTransparent) return t;

    // Heuristic: prefer masked if alphaCutoff provided and we have an alpha-carrying tex
    const bool cutoff = (d.alphaCutoff > 0.0f);
    const bool hasAlphaCandidate = hasOpacityTex || (d.baseColor.texture != nullptr);
    t.masked = ((!explicitBlend) && cutoff && hasAlphaCandidate) || d.blendState == BlendState::BLEND_STATE_MASK;
    return t;
}

inline TechniqueDescriptor PickTechnique(const MaterialDescription& d) {
    TechniqueDescriptor tech{};
    const auto transparency = PickTransparency(d);
	tech.passes.insert(Engine::Primary::ShadowMapsPass); // All materials cast shadows
    if (transparency.isTransparent && !transparency.masked) { // OIT transparency
		tech.compileFlags |= MaterialCompileFlags::MaterialCompileBlend;
		tech.compileFlags |= MaterialCompileFlags::MaterialCompileDoubleSided;
        tech.passes.insert(Engine::Primary::OITAccumulationPass);
    }
    else {
        if (transparency.isTransparent) {
			tech.compileFlags |= MaterialCompileFlags::MaterialCompileAlphaTest;
			tech.compileFlags |= MaterialCompileFlags::MaterialCompileDoubleSided;
			tech.rasterFlags |= MaterialRasterFlags::MaterialRasterFlagsAlphaTest;
        }
		tech.passes.insert(Engine::Primary::GBufferPass);
    }
	if (d.baseColor.texture) {
		tech.compileFlags |= MaterialCompileFlags::MaterialCompileBaseColorTexture;
	}
	if (d.normal.texture) {
		tech.compileFlags |= MaterialCompileFlags::MaterialCompileNormalMap;
	}
	if (d.aoMap.texture) {
		tech.compileFlags |= MaterialCompileFlags::MaterialCompileAOTexture;
	}
	if (d.metallic.texture || d.roughness.texture) {
		tech.compileFlags |= MaterialCompileFlags::MaterialCompilePBRMaps;
	}
	if (d.emissive.texture) {
		tech.compileFlags |= MaterialCompileFlags::MaterialCompileEmissiveTexture;
	}
	if (d.heightMap.texture) {
		tech.compileFlags |= MaterialCompileFlags::MaterialCompileParallax;
	}

    return tech;
}

class Material {
public:
    static std::shared_ptr<Material> CreateShared(const MaterialDescription& desc) {
        uint32_t materialFlags = 0;
        uint32_t psoFlags = 0;
        materialFlags |= MaterialFlags::MATERIAL_PBR; // TODO: Non-PBR materials
        BlendState blendState = BlendState::BLEND_STATE_OPAQUE; // Default blend state
        if (desc.baseColor.texture) {
            if (!desc.baseColor.texture->Meta().alphaIsAllOpaque) {
                materialFlags |= MaterialFlags::MATERIAL_DOUBLE_SIDED;
                blendState = BlendState::BLEND_STATE_MASK; // Use mask blending for alpha-tested materials
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
        auto diffuseColor = desc.diffuseColor;
        if (desc.opacity.texture) { // TODO: How can we tell if this should be used as a mask or as a blend?
            materialFlags |= MaterialFlags::MATERIAL_OPACITY_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
            blendState = BlendState::BLEND_STATE_BLEND; // Use blend state for opacity
        }
        if (desc.opacity.factor.Get() < 1.0f) {
            materialFlags |= MaterialFlags::MATERIAL_DOUBLE_SIDED;
            diffuseColor.w = desc.opacity.factor.Get(); // Use opacity factor as alpha
            blendState = BlendState::BLEND_STATE_BLEND; // Use blend state for opacity
        }
        if (desc.negateNormals) {
            materialFlags |= MaterialFlags::MATERIAL_NEGATE_NORMALS;
        }
        if (desc.invertNormalGreen) {
            materialFlags |= MaterialFlags::MATERIAL_INVERT_NORMAL_GREEN;
        }
		TechniqueDescriptor technique = PickTechnique(desc);

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
            desc.opacity.texture,
            desc.metallic.factor.Get(),
            desc.roughness.factor.Get(),
            diffuseColor,
            desc.emissiveColor,
            desc.baseColor.channels,
            desc.normal.channels,
            desc.aoMap.channels,
            desc.heightMap.channels,
            desc.metallic.channels,
            desc.roughness.channels,
            desc.emissive.channels,
            technique,
            desc.alphaCutoff
        );
    }
    ~Material();

    void SetHeightmap(std::shared_ptr<TextureAsset> heightmap);
    void SetTextureScale(float scale);
    void SetHeightmapScale(float scale);
    void SetCompileFlagsID(uint32_t id);
    void SetRasterBucketIndex(uint32_t index);
    PSOFlags GetPSOFlags() const { return m_psoFlags; }
    MaterialFlags GetMaterialFlags() const { return static_cast<MaterialFlags>(m_materialData.materialFlags); }
    static std::shared_ptr<Material> GetDefaultMaterial();
    TechniqueDescriptor const& Technique() const { return m_technique; }
    static void DestroyDefaultMaterial() {
        defaultMaterial.reset();
    }
    uint32_t GetMaterialID() const { return m_materialID; }
	PerMaterialCB const& GetData() const { return m_materialData; }
    void EnsureTexturesUploaded(const TextureFactory& factory);
private:
	inline static std::atomic<uint32_t> globalMaterialCount;
	const uint32_t m_materialID = globalMaterialCount.fetch_add(1, std::memory_order_relaxed);

    std::string m_name;
    std::shared_ptr<TextureAsset> m_baseColorTexture;
    std::shared_ptr<TextureAsset> m_normalTexture;
    std::shared_ptr<TextureAsset> m_aoMap;
    std::shared_ptr<TextureAsset> m_heightMap;
    std::shared_ptr<TextureAsset> m_roughnessTexture;
    std::shared_ptr<TextureAsset> m_metallicTexture;
    std::shared_ptr<TextureAsset> m_emissiveTexture;
    std::shared_ptr<TextureAsset> m_opacityTexture;
    std::vector<uint32_t> m_baseColorChannels;
    std::vector<uint32_t> m_normalChannels;
    std::vector<uint32_t> m_aoChannel;
    std::vector<uint32_t> m_heightChannel;
    std::vector<uint32_t> m_metallicChannel;
    std::vector<uint32_t> m_roughnessChannel;
    std::vector<uint32_t> m_emissiveChannels;
    float m_metallicFactor;
    float m_roughnessFactor;
    DirectX::XMFLOAT4 m_baseColorFactor;
    DirectX::XMFLOAT4 m_emissiveFactor;
    PerMaterialCB m_materialData = { 0 };
    PSOFlags m_psoFlags;
    TechniqueDescriptor m_technique;

    Material(const std::string& name,
        MaterialFlags materialFlags, PSOFlags psoFlags);

    Material(const std::string& name,
        MaterialFlags materialFlags, PSOFlags psoFlags,
        std::shared_ptr<TextureAsset> baseColorTexture,
        std::shared_ptr<TextureAsset> normalTexture,
        std::shared_ptr<TextureAsset> aoMap,
        std::shared_ptr<TextureAsset> heightMap,
        std::shared_ptr<TextureAsset> metallicTexture,
        std::shared_ptr<TextureAsset> m_roughnessTexture,
        std::shared_ptr<TextureAsset> emissiveTexture,
        std::shared_ptr<TextureAsset> opacityTexture,
        float metallicFactor,
        float roughnessFactor,
        DirectX::XMFLOAT4 baseColorFactor,
        DirectX::XMFLOAT4 emissiveFactor,
        std::vector<uint32_t> baseColorChannels,
        std::vector<uint32_t> normalChannels,
        std::vector<uint32_t> aoChannel,
        std::vector<uint32_t> heightChannel,
        std::vector<uint32_t> metallicChannel,
        std::vector<uint32_t> roughnessChannel,
        std::vector<uint32_t> emissiveChannels,
		TechniqueDescriptor technique,
        float alphaCutoff);

    static std::shared_ptr<Material> CreateShared(const std::string& name,
        MaterialFlags materialFlags, PSOFlags psoFlags,
        std::shared_ptr<TextureAsset> baseColorTexture,
        std::shared_ptr<TextureAsset> normalTexture,
        std::shared_ptr<TextureAsset> aoMap,
        std::shared_ptr<TextureAsset> heightMap,
        std::shared_ptr<TextureAsset> metallicTexture,
        std::shared_ptr<TextureAsset> roughnessTexture,
        std::shared_ptr<TextureAsset> emissiveTexture,
        std::shared_ptr<TextureAsset> opacityTexture,
        float metallicFactor,
        float roughnessFactor,
        DirectX::XMFLOAT4 baseColorFactor,
        DirectX::XMFLOAT4 emissiveFactor,
        std::vector<uint32_t> baseColorChannels,
        std::vector<uint32_t> normalChannels,
        std::vector<uint32_t> aoChannel,
        std::vector<uint32_t> heightChannel,
        std::vector<uint32_t> metallicChannel,
        std::vector<uint32_t> roughnessChannel,
        std::vector<uint32_t> emissiveChannels,
        TechniqueDescriptor technique,
        float alphaCutoff) {
        return std::shared_ptr<Material>(new Material(name, materialFlags, psoFlags,
            baseColorTexture, normalTexture, aoMap, heightMap,
            metallicTexture, roughnessTexture, emissiveTexture, opacityTexture,
            metallicFactor, roughnessFactor, baseColorFactor, emissiveFactor,
            baseColorChannels, normalChannels, aoChannel, heightChannel,
            metallicChannel, roughnessChannel, emissiveChannels,
			technique,
            alphaCutoff));
    }

    inline static std::shared_ptr<Material> defaultMaterial;
};