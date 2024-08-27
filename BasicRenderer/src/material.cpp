#include "Material.h"
#include <string>

Material::Material(const std::string& name,
    UINT psoFlags)
    : name(name),
    psoFlags(psoFlags) {
}

Material::Material(const std::string& name,
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
        materialData.baseColorTextureIndex = baseColorTexture->GetDescriptorIndex();
    }
    if (normalTexture != nullptr) {
        materialData.normalTextureIndex = normalTexture->GetDescriptorIndex();
    }
    if (aoMap != nullptr) {
        materialData.aoMapIndex = aoMap->GetDescriptorIndex();
    }
    if (heightMap != nullptr) {
        materialData.heightMapIndex = heightMap->GetDescriptorIndex();
    }
    if (metallicRoughnessTexture != nullptr) {
        materialData.metallicRoughnessTextureIndex = metallicRoughnessTexture->GetDescriptorIndex();
    }

    auto& resourceManager = ResourceManager::getInstance();
    perMaterialHandle = resourceManager.createConstantBuffer<PerMaterialCB>();
    resourceManager.updateConstantBuffer(perMaterialHandle, materialData);
}

Texture* Material::createDefaultTexture() {
    // Create a 1x1 white texture
    static const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    Texture* defaultTexture = new Texture(whitePixel, 1, 1, false);
    return defaultTexture;
}