#pragma once

#include <functional>
#include <memory>
#include <string>
#include <spdlog/spdlog.h>

#include "Managers/Singletons/SettingsManager.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Sampler.h"
#include "Utilities/Utilities.h"
#include "Resources/TextureDescription.h"
class ShadowMaps : public ResourceGroup {
public:
    ShadowMaps(const std::wstring& name)
        : ResourceGroup(name) {
		getNumCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	}

    std::shared_ptr<PixelBuffer> AddMap(LightInfo* light, uint16_t shadowResolution) {
		std::shared_ptr<PixelBuffer> shadowMap;
		auto shadowSampler = Sampler::GetDefaultShadowSampler();
		TextureDescription desc;
		ImageDimensions dims;
		dims.height = shadowResolution;
		dims.width = shadowResolution;
		dims.rowPitch = shadowResolution * 4;
		dims.slicePitch = shadowResolution * shadowResolution * 4;
		desc.imageDimensions.push_back(dims);
		desc.hasDSV = true;
		desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
		desc.channels = 1;
		desc.format = DXGI_FORMAT_R32_TYPELESS;
		desc.hasSRV = true;
		desc.srvFormat = DXGI_FORMAT_R32_FLOAT;
		//desc.generateMipMaps = true; // Mips will only be used by aliased downsample maps
		//desc.allowAlias = true; // We will alias the shadow maps to allow UAV downsampling
		switch (light->type) {
		case Components::LightType::Point: // Cubemap
			desc.isCubemap = true;
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"PointShadowMap");
			break;
		case Components::LightType::Spot: // 2D texture
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"SpotShadowMap");
			break;
		case Components::LightType::Directional: // Texture array
			desc.isArray = true;
			desc.arraySize = getNumCascades();
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"DirectionalShadowMap");
			break;

		}
		//light->SetShadowMap(map);
        AddResource(shadowMap);
		return shadowMap;
    }

	void RemoveMap(std::shared_ptr<Texture> map) {
		if (map != nullptr) {
			RemoveResource(map.get());
		}

	}

private:
	std::function<uint8_t()> getNumCascades;
};

class LinearShadowMaps : public ResourceGroup {
public:
	LinearShadowMaps(const std::wstring& name)
		: ResourceGroup(name) {
		getNumCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	}

	std::shared_ptr<PixelBuffer> AddMap(LightInfo* light, uint16_t shadowResolution) {
		std::shared_ptr<PixelBuffer> shadowMap;
		auto shadowSampler = Sampler::GetDefaultShadowSampler();
		TextureDescription desc;
		ImageDimensions dims;
		unsigned int res = shadowResolution;
		dims.height = res;
		dims.width = res;
		desc.imageDimensions.push_back(dims);
		desc.channels = 1;
		desc.format = DXGI_FORMAT_R32_FLOAT;
		desc.hasSRV = true;
		desc.srvFormat = DXGI_FORMAT_R32_FLOAT;
		desc.hasUAV = true;
		desc.uavFormat = DXGI_FORMAT_R32_FLOAT;
		desc.generateMipMaps = true;
		desc.hasRTV = true;
		desc.rtvFormat = DXGI_FORMAT_R32_FLOAT;
		switch (light->type) {
		case Components::LightType::Point: // Cubemap
			desc.isCubemap = true;
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"linearShadowMap");
			break;
		case Components::LightType::Spot: // 2D texture
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"linearShadowMap");
			break;
		case Components::LightType::Directional: // Texture array
			desc.isArray = true;
			desc.arraySize = getNumCascades();
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"linearShadowMap");
			break;

		}
		AddResource(shadowMap);
		return shadowMap;
	}

	void RemoveMap(std::shared_ptr<Texture> map) {
		if (map != nullptr) {
			RemoveResource(map.get());
		}

	}

private:
	std::function<uint8_t()> getNumCascades;
};