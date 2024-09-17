#include "Material.h"
#include <string>
#include "PSOFlags.h"

Material::Material(const std::string& name,
    UINT psoFlags)
    : m_name(name),
    m_psoFlags(psoFlags) {
    auto& resourceManager = ResourceManager::GetInstance();
    m_perMaterialHandle = resourceManager.CreateIndexedConstantBuffer<PerMaterialCB>();
    resourceManager.UpdateConstantBuffer(m_perMaterialHandle, m_materialData);
}

Material::Material(const std::string& name,
    UINT psoFlags,
    std::shared_ptr<Texture> baseColorTexture,
    std::shared_ptr<Texture> normalTexture,
    std::shared_ptr<Texture> aoMap,
    std::shared_ptr<Texture> heightMap,
    std::shared_ptr<Texture> metallicRoughnessTexture,
    std::shared_ptr<Texture> emissiveTexture,
    float metallicFactor,
    float roughnessFactor,
    DirectX::XMFLOAT4 baseColorFactor,
    DirectX::XMFLOAT4 emissiveFactor,
    BlendState blendState)
    : m_name(name),
    m_psoFlags(psoFlags),
    m_baseColorTexture(baseColorTexture),
    m_normalTexture(normalTexture),
    m_aoMap(aoMap),
    m_heightMap(heightMap),
    m_metallicRoughnessTexture(metallicRoughnessTexture),
    m_emissiveTexture(emissiveTexture),
    m_metallicFactor(metallicFactor),
    m_roughnessFactor(roughnessFactor),
    m_baseColorFactor(baseColorFactor),
    m_emissiveFactor(emissiveFactor),
    m_blendState(blendState) {

    m_materialData.psoFlags = psoFlags;
    m_materialData.ambientStrength = 0.5;
    m_materialData.specularStrength = 2.0;
    m_materialData.heightMapScale = 0.05;
    m_materialData.textureScale = 1.0;
    m_materialData.baseColorFactor = baseColorFactor;
    m_materialData.emissiveFactor = emissiveFactor;
    m_materialData.metallicFactor = metallicFactor;
    m_materialData.roughnessFactor = roughnessFactor;
    if (baseColorTexture != nullptr) {
        m_materialData.baseColorTextureIndex = baseColorTexture->GetBufferDescriptorIndex();
        m_materialData.baseColorSamplerIndex = baseColorTexture->GetSamplerDescriptorIndex();
    }
    if (normalTexture != nullptr) {
        m_materialData.normalTextureIndex = normalTexture->GetBufferDescriptorIndex();
        m_materialData.normalSamplerIndex = normalTexture->GetSamplerDescriptorIndex();
    }
    if (aoMap != nullptr) {
        m_materialData.aoMapIndex = aoMap->GetBufferDescriptorIndex();
        m_materialData.aoSamplerIndex = aoMap->GetSamplerDescriptorIndex();
    }
    if (heightMap != nullptr) {
        m_materialData.heightMapIndex = heightMap->GetBufferDescriptorIndex();
        m_materialData.heightMapIndex = heightMap->GetSamplerDescriptorIndex();
    }
    if (metallicRoughnessTexture != nullptr) {
        m_materialData.metallicRoughnessTextureIndex = metallicRoughnessTexture->GetBufferDescriptorIndex();
        m_materialData.metallicRoughnessSamplerIndex = metallicRoughnessTexture->GetSamplerDescriptorIndex();
    }
    if (emissiveTexture != nullptr) {
        m_materialData.emissiveTextureIndex = emissiveTexture->GetBufferDescriptorIndex();
        m_materialData.emissiveSamplerIndex = emissiveTexture->GetSamplerDescriptorIndex();
    }

    auto& resourceManager = ResourceManager::GetInstance();
    m_perMaterialHandle = resourceManager.CreateIndexedConstantBuffer<PerMaterialCB>();
    resourceManager.UpdateConstantBuffer(m_perMaterialHandle, m_materialData);
}

std::shared_ptr<Texture> Material::createDefaultTexture() {
    // Create a 1x1 white texture
    static const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    std::shared_ptr<PixelBuffer>defaultImage = PixelBuffer::CreateFromImage(whitePixel, 1, 1, 4, false);

    D3D12_SAMPLER_DESC defaultSamplerDesc = {};
    defaultSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    defaultSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    defaultSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    defaultSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    defaultSamplerDesc.MipLODBias = 0.0f;
    defaultSamplerDesc.MaxAnisotropy = 1;
    defaultSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    defaultSamplerDesc.BorderColor[0] = 0.0f;
    defaultSamplerDesc.BorderColor[1] = 0.0f;
    defaultSamplerDesc.BorderColor[2] = 0.0f;
    defaultSamplerDesc.BorderColor[3] = 0.0f;
    defaultSamplerDesc.MinLOD = 0.f;
    defaultSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    std::shared_ptr<Sampler> defaultSampler = std::make_shared<Sampler>(defaultSamplerDesc);

    std::shared_ptr<Texture> defaultTexture = std::make_shared<Texture>(defaultImage, defaultSampler);

    return defaultTexture;
}

UINT Material::GetMaterialBufferIndex() {
    return m_perMaterialHandle.index;
}

void Material::SetHeightmap(std::shared_ptr<Texture> heightmap) {
    m_psoFlags |= PSOFlags::PARALLAX;
	m_heightMap = heightmap;
	m_materialData.heightMapIndex = heightmap->GetBufferDescriptorIndex();
	m_materialData.heightSamplerIndex = heightmap->GetSamplerDescriptorIndex();
	ResourceManager::GetInstance().UpdateConstantBuffer(m_perMaterialHandle, m_materialData);
}

void Material::SetTextureScale(float scale) {
	m_materialData.textureScale = scale;
	ResourceManager::GetInstance().UpdateConstantBuffer(m_perMaterialHandle, m_materialData);
}

void Material::SetHeightmapScale(float scale) {
	m_materialData.heightMapScale = scale;
	ResourceManager::GetInstance().UpdateConstantBuffer(m_perMaterialHandle, m_materialData);
}