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
	m_baseColorChannels = baseColorChannels;
	m_normalChannels = normalChannels;
	m_aoChannel = aoChannel;
	m_heightChannel = heightChannel;
	m_metallicChannel = metallicChannel;
	m_roughnessChannel = roughnessChannel;
	m_emissiveChannels = emissiveChannels;
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

void Material::SetRasterBucketIndex(uint32_t index) {
    m_materialData.rasterBuckedIndex = index;
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

void Material::EnsureTexturesUploaded(const TextureFactory& factory) {
    if (m_baseColorTexture) {
        m_baseColorTexture->SetGenerateMipmaps(true);
        m_baseColorTexture->EnsureUploaded(factory);
    }
    if (m_normalTexture) {
        m_normalTexture->SetGenerateMipmaps(true);
        m_normalTexture->EnsureUploaded(factory);
	}
    if (m_aoMap) {
        m_aoMap->SetGenerateMipmaps(true);
        m_aoMap->EnsureUploaded(factory);
    }
    if (m_heightMap) {
        m_heightMap->SetGenerateMipmaps(true);
        m_heightMap->EnsureUploaded(factory);
    }
    if (m_metallicTexture) {
        m_metallicTexture->SetGenerateMipmaps(true);
        m_metallicTexture->EnsureUploaded(factory);
    }
    if (m_roughnessTexture) {
        m_roughnessTexture->SetGenerateMipmaps(true);
        m_roughnessTexture->EnsureUploaded(factory);
    }
    if (m_emissiveTexture) {
        m_emissiveTexture->SetGenerateMipmaps(true);
        m_emissiveTexture->EnsureUploaded(factory);
	}
    if (m_opacityTexture) {
        m_opacityTexture->SetGenerateMipmaps(true);
        m_opacityTexture->EnsureUploaded(factory);
	}

    if (m_baseColorTexture != nullptr) {
        m_materialData.baseColorTextureIndex = m_baseColorTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.baseColorSamplerIndex = m_baseColorTexture->SamplerDescriptorIndex();
        m_materialData.baseColorChannels = { m_baseColorChannels[0], m_baseColorChannels[1], m_baseColorChannels[2], m_baseColorChannels[3] };
        m_baseColorTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_baseColorTexture->Image().SetName("BaseColorTexture");
    }
    if (m_normalTexture != nullptr) {
        m_materialData.normalTextureIndex = m_normalTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.normalSamplerIndex = m_normalTexture->SamplerDescriptorIndex();
        m_materialData.normalChannels = { m_normalChannels[0], m_normalChannels[1], m_normalChannels[2] };
        m_normalTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_normalTexture->Image().SetName("NormalTexture");
    }
    if (m_aoMap != nullptr) {
        m_materialData.aoMapIndex = m_aoMap->Image().GetSRVInfo(0).slot.index;
        m_materialData.aoSamplerIndex = m_aoMap->SamplerDescriptorIndex();
        m_materialData.aoChannel = m_aoChannel[0];
        m_aoMap->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_aoMap->Image().SetName("AOMap");
    }
    if (m_heightMap != nullptr) {
        m_materialData.heightMapIndex = m_heightMap->Image().GetSRVInfo(0).slot.index;
        m_materialData.heightSamplerIndex = m_heightMap->SamplerDescriptorIndex();
        m_materialData.heightChannel = m_heightChannel[0];
        m_heightMap->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_heightMap->Image().SetName("HeightMap");
    }
    if (m_metallicTexture != nullptr) {
        m_materialData.metallicTextureIndex = m_metallicTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.metallicSamplerIndex = m_metallicTexture->SamplerDescriptorIndex();
        m_materialData.metallicChannel = m_metallicChannel[0];
        m_metallicTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_metallicTexture->Image().SetName("MetallicTexture");
    }
    if (m_roughnessTexture != nullptr) {
        m_materialData.roughnessTextureIndex = m_roughnessTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.roughnessSamplerIndex = m_roughnessTexture->SamplerDescriptorIndex();
        m_materialData.roughnessChannel = m_roughnessChannel[0];
        m_roughnessTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_roughnessTexture->Image().SetName("RoughnessTexture");
    }
    if (m_metallicTexture == m_roughnessTexture && m_metallicTexture != nullptr) {
        m_roughnessTexture->Image().SetName("MetallicRoughnessTexture");
    }

    if (m_emissiveTexture != nullptr) {
        m_materialData.emissiveTextureIndex = m_emissiveTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.emissiveSamplerIndex = m_emissiveTexture->SamplerDescriptorIndex();
        m_materialData.emissiveChannels = { m_emissiveChannels[0], m_emissiveChannels[1], m_emissiveChannels[2] };
        m_emissiveTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_emissiveTexture->Image().SetName("EmissiveTexture");
    }

    if (m_opacityTexture != nullptr) {
        m_materialData.opacityTextureIndex = m_opacityTexture->Image().GetSRVInfo(0).slot.index;
        m_materialData.opacitySamplerIndex = m_opacityTexture->SamplerDescriptorIndex();
        m_opacityTexture->Image().ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Material textures" }));
        m_opacityTexture->Image().SetName("OpacityTexture");
    }
}
