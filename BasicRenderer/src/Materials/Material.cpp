#include "Materials/Material.h"
#include <string>
#include "Render/PSOFlags.h"
#include "Utilities/Utilities.h"
#include "Materials/MaterialFlags.h"
#include "Resources/PixelBuffer.h"
#include "Resources/MemoryStatisticsComponents.h"

Material::Material(const std::string& name,
    MaterialFlags materialFlags, PSOFlags psoFlags)
    : m_name(name), m_psoFlags(psoFlags) {
    auto& resourceManager = ResourceManager::GetInstance();
    m_materialData.materialFlags = materialFlags;
}

Material::Material(const std::string& name,
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
    float alphaCutoff)
    : m_name(name),
    m_psoFlags(psoFlags),
    m_baseColorTexture(baseColorTexture),
    m_normalTexture(normalTexture),
    m_aoMap(aoMap),
    m_heightMap(heightMap),
    m_metallicTexture(metallicTexture),
    m_roughnessTexture(roughnessTexture),
    m_emissiveTexture(emissiveTexture),
	m_opacityTexture(opacityTexture),
    m_metallicFactor(metallicFactor),
    m_roughnessFactor(roughnessFactor),
    m_baseColorFactor(baseColorFactor),
    m_emissiveFactor(emissiveFactor),
	m_technique(technique)
{
    m_materialData.materialFlags = materialFlags;
    m_materialData.ambientStrength = 0.5f;
    m_materialData.specularStrength = 2.0f;
    m_materialData.heightMapScale = 0.05f;
    m_materialData.textureScale = 1.0f;
    m_materialData.baseColorFactor = baseColorFactor;
    m_materialData.emissiveFactor = emissiveFactor;
    m_materialData.metallicFactor = metallicFactor;
    m_materialData.roughnessFactor = roughnessFactor;
    m_materialData.alphaCutoff = alphaCutoff;
    if (baseColorTexture != nullptr) {
        m_materialData.baseColorTextureIndex = baseColorTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.baseColorSamplerIndex = baseColorTexture->SamplerDescriptorIndex();
		m_materialData.baseColorChannels = { baseColorChannels[0], baseColorChannels[1], baseColorChannels[2], baseColorChannels[3] };
        baseColorTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
    	baseColorTexture->Image().SetName("BaseColorTexture");
    }
    if (normalTexture != nullptr) {
        m_materialData.normalTextureIndex = normalTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.normalSamplerIndex = normalTexture->SamplerDescriptorIndex();
		m_materialData.normalChannels = { normalChannels[0], normalChannels[1], normalChannels[2] };
		normalTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        normalTexture->Image().SetName("NormalTexture");
    }
    if (aoMap != nullptr) {
        m_materialData.aoMapIndex = aoMap->Image().GetSRVInfo(0).slot.index;
        m_materialData.aoSamplerIndex = aoMap->SamplerDescriptorIndex();
		m_materialData.aoChannel = aoChannel[0];
        aoMap->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        aoMap->Image().SetName("AOMap");
    }
    if (heightMap != nullptr) {
        m_materialData.heightMapIndex = heightMap->Image().GetSRVInfo(0).slot.index;
        m_materialData.heightSamplerIndex = heightMap->SamplerDescriptorIndex();
		m_materialData.heightChannel = heightChannel[0];
        heightMap->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        heightMap->Image().SetName("HeightMap");
    }
    if (metallicTexture != nullptr) {
        m_materialData.metallicTextureIndex = metallicTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.metallicSamplerIndex = metallicTexture->SamplerDescriptorIndex();
		m_materialData.metallicChannel = metallicChannel[0];
		metallicTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        metallicTexture->Image().SetName("MetallicTexture");
    }
    if (roughnessTexture != nullptr) {
        m_materialData.roughnessTextureIndex = roughnessTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.roughnessSamplerIndex = roughnessTexture->SamplerDescriptorIndex();
		m_materialData.roughnessChannel = roughnessChannel[0];
        roughnessTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        roughnessTexture->Image().SetName("RoughnessTexture");
    }
    if (metallicTexture == roughnessTexture && metallicTexture != nullptr) {
        roughnessTexture->Image().SetName("MetallicRoughnessTexture");
    }

    if (emissiveTexture != nullptr) {
        m_materialData.emissiveTextureIndex = emissiveTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.emissiveSamplerIndex = emissiveTexture->SamplerDescriptorIndex();
		m_materialData.emissiveChannels = { emissiveChannels[0], emissiveChannels[1], emissiveChannels[2] };
        emissiveTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        emissiveTexture->Image().SetName("EmissiveTexture");
    }

    if (opacityTexture != nullptr) {
        m_materialData.opacityTextureIndex = opacityTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.opacitySamplerIndex = opacityTexture->SamplerDescriptorIndex();
        opacityTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
		opacityTexture->Image().SetName("OpacityTexture");
    }

}

Material::~Material() {
}

void Material::SetHeightmap(std::shared_ptr<TextureAsset> heightmap) {
    m_materialData.materialFlags |= MaterialFlags::MATERIAL_PARALLAX;
    m_heightMap = heightmap;
    heightmap->Image().SetName("HeightMap");
	heightmap->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
    m_materialData.heightMapIndex = heightmap->Image().GetSRVInfo(0).slot.index;
    m_materialData.heightSamplerIndex = heightmap->SamplerDescriptorIndex();
}

void Material::SetTextureScale(float scale) {
    m_materialData.textureScale = scale;
}

void Material::SetHeightmapScale(float scale) {
    m_materialData.heightMapScale = scale;
}

void Material::SetCompileFlagsID(uint32_t id) {
    m_materialData.compileFlagsID = id;
}

std::shared_ptr<Material> Material::GetDefaultMaterial() {
    if (defaultMaterial) {
        return defaultMaterial;
    }

    MaterialDescription desc = {};
	desc.name = "DefaultMaterial";
	desc.alphaCutoff = 0.5f;
	desc.diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	desc.emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	desc.baseColor = { nullptr, 1.0f, { 0, 1, 2, 3 } };
	desc.metallic = { nullptr, 0.0f, { 0 } };
	desc.roughness = { nullptr, 0.5f, { 0 } };
	desc.emissive = { nullptr, 1.0f, { 0, 1, 2 } };
	desc.opacity = { nullptr, 1.0f, { 0 } };
	desc.aoMap = { nullptr, 1.0f, { 0 } };
	desc.heightMap = { nullptr, 1.0f, { 0 } };
	desc.normal = { nullptr, 1.0f, { 0, 1, 2 } };

    defaultMaterial = Material::CreateShared(desc);

    return defaultMaterial;
}