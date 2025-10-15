#pragma once

#include <DirectXMath.h>
#include <string>
#include <array>
#include "Resources/Texture.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/BlendState.h"
#include "Render/PSOFlags.h"
#include "Materials/MaterialFlags.h"
#include "Materials/MaterialDescription.h"


struct TechniquePass {
    RenderPhase  phase;
    PSOKey       pso;
    CommandSigId cmdSig;          // Draw/DispatchMesh/etc.
    ResourceSlots slots;          // your bindless/material root layout
    PhaseRequirements reqs;       // needsSceneColor, depth, oit buffers, etc.
};

struct TechniqueDescriptor {
    std::vector<TechniquePass> passes;
    // metadata: alphaMode, domain (Deferred/Forward), feature bits (aniso, parallax)?
};

class Material {
public:
    static std::shared_ptr<Material> CreateShared(const MaterialDescription& desc) {
        uint32_t materialFlags = 0;
        uint32_t psoFlags = 0;
        materialFlags |= MaterialFlags::MATERIAL_PBR; // TODO: Non-PBR materials
        BlendState blendState = BlendState::BLEND_STATE_OPAQUE; // Default blend state
        if (desc.baseColor.texture) {
            if (!desc.baseColor.texture->AlphaIsAllOpaque()) {
                materialFlags |= MaterialFlags::MATERIAL_DOUBLE_SIDED;
                psoFlags |= PSOFlags::PSO_ALPHA_TEST;
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
            psoFlags |= PSOFlags::PSO_ALPHA_TEST | PSOFlags::PSO_BLEND;
            materialFlags |= MaterialFlags::MATERIAL_OPACITY_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
            blendState = BlendState::BLEND_STATE_BLEND; // Use blend state for opacity
        }
        if (desc.opacity.factor.Get() < 1.0f) {
            materialFlags |= MaterialFlags::MATERIAL_DOUBLE_SIDED;
            psoFlags |= PSOFlags::PSO_BLEND | PSOFlags::PSO_ALPHA_TEST;
            diffuseColor.w = desc.opacity.factor.Get(); // Use opacity factor as alpha
            blendState = BlendState::BLEND_STATE_BLEND; // Use blend state for opacity
        }
        if (desc.negateNormals) {
            materialFlags |= MaterialFlags::MATERIAL_NEGATE_NORMALS;
        }
        if (desc.invertNormalGreen) {
            materialFlags |= MaterialFlags::MATERIAL_INVERT_NORMAL_GREEN;
        }
        if (desc.blendState != BlendState::BLEND_STATE_UNKNOWN) {
            blendState = desc.blendState; // Use provided blend state
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
            blendState,
            desc.alphaCutoff
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
    TechniqueDescriptor const& Technique() const { return m_technique; }
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
    std::shared_ptr<Texture> m_opacityTexture;
    float m_metallicFactor;
    float m_roughnessFactor;
    DirectX::XMFLOAT4 m_baseColorFactor;
    DirectX::XMFLOAT4 m_emissiveFactor;
    BlendState m_blendState;
    PerMaterialCB m_materialData = { 0 };
    PSOFlags m_psoFlags;
    TechniqueDescriptor m_technique;

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
        std::shared_ptr<Texture> opacityTexture,
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
        BlendState blendState,
        float alphaCutoff);

    static std::shared_ptr<Material> CreateShared(const std::string& name,
        MaterialFlags materialFlags, PSOFlags psoFlags,
        std::shared_ptr<Texture> baseColorTexture,
        std::shared_ptr<Texture> normalTexture,
        std::shared_ptr<Texture> aoMap,
        std::shared_ptr<Texture> heightMap,
        std::shared_ptr<Texture> metallicTexture,
        std::shared_ptr<Texture> roughnessTexture,
        std::shared_ptr<Texture> emissiveTexture,
        std::shared_ptr<Texture> opacityTexture,
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
        BlendState blendState,
        float alphaCutoff) {
        return std::shared_ptr<Material>(new Material(name, materialFlags, psoFlags,
            baseColorTexture, normalTexture, aoMap, heightMap,
            metallicTexture, roughnessTexture, emissiveTexture, opacityTexture,
            metallicFactor, roughnessFactor, baseColorFactor, emissiveFactor,
            baseColorChannels, normalChannels, aoChannel, heightChannel,
            metallicChannel, roughnessChannel, emissiveChannels,
            blendState, alphaCutoff));
    }

    std::shared_ptr<Buffer> m_perMaterialHandle;
    inline static std::shared_ptr<Material> defaultMaterial;
};