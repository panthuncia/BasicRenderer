#include "Material.h"
#include <string>

Material::Material(const std::string& name,
    UINT psoFlags)
    : name(name),
    psoFlags(psoFlags) {
}

Material::Material(const std::string& name,
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
    int blendMode)
    : name(name),
    psoFlags(psoFlags),
    baseColorTexture(baseColorTexture),
    normalTexture(normalTexture),
    aoMap(aoMap),
    heightMap(heightMap),
    metallicRoughnessTexture(metallicRoughnessTexture),
    metallicFactor(metallicFactor),
    roughnessFactor(roughnessFactor),
    baseColorFactor(baseColorFactor),
    emissiveFactor(emissiveFactor),
    blendMode(blendMode) {

    materialData.psoFlags = psoFlags;
    materialData.ambientStrength = 0.5;
    materialData.specularStrength = 2.0;
    materialData.heightMapScale = 0.05;
    materialData.textureScale = 1.0;
    materialData.baseColorFactor = baseColorFactor;
    materialData.emissiveFactor = emissiveFactor;
    materialData.metallicFactor = metallicFactor;
    materialData.roughnessFactor = roughnessFactor;
    if (baseColorTexture != nullptr) {
        materialData.baseColorTextureIndex = baseColorTexture->GetBufferDescriptorIndex();
    }
    if (normalTexture != nullptr) {
        materialData.normalTextureIndex = normalTexture->GetBufferDescriptorIndex();
    }
    if (aoMap != nullptr) {
        materialData.aoMapIndex = aoMap->GetBufferDescriptorIndex();
    }
    if (heightMap != nullptr) {
        materialData.heightMapIndex = heightMap->GetBufferDescriptorIndex();
    }
    if (metallicRoughnessTexture != nullptr) {
        materialData.metallicRoughnessTextureIndex = metallicRoughnessTexture->GetBufferDescriptorIndex();
    }

    auto& resourceManager = ResourceManager::GetInstance();
    perMaterialHandle = resourceManager.CreateIndexedConstantBuffer<PerMaterialCB>();
    resourceManager.UpdateIndexedConstantBuffer(perMaterialHandle, materialData);
}

std::shared_ptr<Texture> Material::createDefaultTexture() {
    // Create a 1x1 white texture
    static const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    std::shared_ptr<PixelBuffer>defaultImage = std::make_shared<PixelBuffer>(whitePixel, 1, 1, false);

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
    return perMaterialHandle.index;
}