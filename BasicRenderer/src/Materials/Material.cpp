#include "Materials/Material.h"
#include <string>
#include "Render/PSOFlags.h"
#include "Resources/Sampler.h"
#include "Utilities/Utilities.h"
#include "Resources/TextureDescription.h"
#include "Materials/MaterialFlags.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/DeletionManager.h"

Material::Material(const std::string& name,
    MaterialFlags materialFlags, PSOFlags psoFlags)
    : m_name(name), m_psoFlags(psoFlags) {
    auto& resourceManager = ResourceManager::GetInstance();
    m_perMaterialHandle = resourceManager.CreateIndexedConstantBuffer<PerMaterialCB>();
    m_materialData.materialFlags = materialFlags;
    UploadManager::GetInstance().UploadData(&m_materialData, sizeof(PerMaterialCB), m_perMaterialHandle.get(), 0);
}

Material::Material(const std::string& name,
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
    m_blendState(blendState) {

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
        m_materialData.baseColorTextureIndex = baseColorTexture->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.baseColorSamplerIndex = baseColorTexture->GetSamplerDescriptorIndex();
		m_materialData.baseColorChannels = { baseColorChannels[0], baseColorChannels[1], baseColorChannels[2], baseColorChannels[3] };
        baseColorTexture->GetBuffer()->SetName(L"BaseColorTexture");
    }
    if (normalTexture != nullptr) {
        m_materialData.normalTextureIndex = normalTexture->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.normalSamplerIndex = normalTexture->GetSamplerDescriptorIndex();
		m_materialData.normalChannels = { normalChannels[0], normalChannels[1], normalChannels[2] };
        normalTexture->GetBuffer()->SetName(L"NormalTexture");
    }
    if (aoMap != nullptr) {
        m_materialData.aoMapIndex = aoMap->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.aoSamplerIndex = aoMap->GetSamplerDescriptorIndex();
		m_materialData.aoChannel = aoChannel[0];
        aoMap->GetBuffer()->SetName(L"AOMap");
    }
    if (heightMap != nullptr) {
        m_materialData.heightMapIndex = heightMap->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.heightSamplerIndex = heightMap->GetSamplerDescriptorIndex();
		m_materialData.heightChannel = heightChannel[0];
        heightMap->GetBuffer()->SetName(L"HeightMap");
    }
    if (metallicTexture != nullptr) {
        m_materialData.metallicTextureIndex = metallicTexture->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.metallicSamplerIndex = metallicTexture->GetSamplerDescriptorIndex();
		m_materialData.metallicChannel = metallicChannel[0];
        metallicTexture->GetBuffer()->SetName(L"MetallicTexture");
    }
    if (roughnessTexture != nullptr) {
        m_materialData.roughnessTextureIndex = roughnessTexture->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.roughnessSamplerIndex = roughnessTexture->GetSamplerDescriptorIndex();
		m_materialData.roughnessChannel = roughnessChannel[0];
        roughnessTexture->GetBuffer()->SetName(L"RoughnessTexture");
    }
    if (metallicTexture == roughnessTexture && metallicTexture != nullptr) {
        roughnessTexture->GetBuffer()->SetName(L"MetallicRoughnessTexture");
    }

    if (emissiveTexture != nullptr) {
        m_materialData.emissiveTextureIndex = emissiveTexture->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.emissiveSamplerIndex = emissiveTexture->GetSamplerDescriptorIndex();
		m_materialData.emissiveChannels = { emissiveChannels[0], emissiveChannels[1], emissiveChannels[2] };
        emissiveTexture->GetBuffer()->SetName(L"EmissiveTexture");
    }

    if (opacityTexture != nullptr) {
        m_materialData.opacityTextureIndex = opacityTexture->GetBuffer()->GetSRVInfo(0).index;
        m_materialData.opacitySamplerIndex = opacityTexture->GetSamplerDescriptorIndex();
		opacityTexture->GetBuffer()->SetName(L"OpacityTexture");
    }

    auto& resourceManager = ResourceManager::GetInstance();
    m_perMaterialHandle = resourceManager.CreateIndexedConstantBuffer<PerMaterialCB>();
    m_perMaterialHandle->SetName(L"PerMaterialCB (" + s2ws(name) + L")");

    UploadManager::GetInstance().UploadData(&m_materialData, sizeof(PerMaterialCB), m_perMaterialHandle.get(), 0);
}

Material::~Material() {
    DeletionManager::GetInstance().MarkForDelete(m_perMaterialHandle);
}

//std::shared_ptr<Texture> Material::CreateDefaultTexture() {
//    // Create a 1x1 white texture
//    static const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
//
//	TextureDescription desc;
//	desc.channels = 4;
//    ImageDimensions dims;
//	dims.width = 1;
//	dims.height = 1;
//	dims.rowPitch = 4;
//	dims.slicePitch = 4;
//	desc.imageDimensions.push_back(dims);
//	desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
//
//    std::shared_ptr<PixelBuffer>defaultImage = PixelBuffer::Create(desc, {whitePixel});
//
//    D3D12_SAMPLER_DESC defaultSamplerDesc = {};
//    defaultSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
//    defaultSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
//    defaultSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
//    defaultSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
//    defaultSamplerDesc.MipLODBias = 0.0f;
//    defaultSamplerDesc.MaxAnisotropy = 1;
//    defaultSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
//    defaultSamplerDesc.BorderColor[0] = 0.0f;
//    defaultSamplerDesc.BorderColor[1] = 0.0f;
//    defaultSamplerDesc.BorderColor[2] = 0.0f;
//    defaultSamplerDesc.BorderColor[3] = 0.0f;
//    defaultSamplerDesc.MinLOD = 0.f;
//    defaultSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
//
//    std::shared_ptr<Sampler> defaultSampler = Sampler::CreateSampler(defaultSamplerDesc);
//
//    std::shared_ptr<Texture> defaultTexture = std::make_shared<Texture>(defaultImage, defaultSampler);
//
//    return defaultTexture;
//}

UINT Material::GetMaterialBufferIndex() {
    return m_perMaterialHandle->GetCBVInfo().index;
}

void Material::SetHeightmap(std::shared_ptr<Texture> heightmap) {
    m_materialData.materialFlags |= MaterialFlags::MATERIAL_PARALLAX;
    m_heightMap = heightmap;
    heightmap->GetBuffer()->SetName(L"HeightMap");
    m_materialData.heightMapIndex = heightmap->GetBuffer()->GetSRVInfo(0).index;
    m_materialData.heightSamplerIndex = heightmap->GetSamplerDescriptorIndex();
    UploadManager::GetInstance().UploadData(&m_materialData, sizeof(PerMaterialCB), m_perMaterialHandle.get(), 0);
}

void Material::SetTextureScale(float scale) {
    m_materialData.textureScale = scale;
    UploadManager::GetInstance().UploadData(&m_materialData, sizeof(PerMaterialCB), m_perMaterialHandle.get(), 0);
}

void Material::SetHeightmapScale(float scale) {
    m_materialData.heightMapScale = scale;
    UploadManager::GetInstance().UploadData(&m_materialData, sizeof(PerMaterialCB), m_perMaterialHandle.get(), 0);
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