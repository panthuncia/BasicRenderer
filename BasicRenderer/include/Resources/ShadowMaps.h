#pragma once

#include <functional>
#include <memory>
#include <string>
#include <limits>
#include <rhi.h>

#include "Managers/Singletons/SettingsManager.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Texture.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Sampler.h"
#include "Utilities/Utilities.h"
#include "Resources/TextureDescription.h"
#include "Resources/MemoryStatisticsComponents.h"
class ShadowMaps : public ResourceGroup {
public:
    ShadowMaps(const std::string& name)
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
		desc.dsvFormat = rhi::Format::D32_Float;
		desc.channels = 1;
		desc.format = rhi::Format::R32_Typeless;
		desc.hasSRV = true;
		desc.srvFormat = rhi::Format::R32_Float;
		//desc.generateMipMaps = true; // Mips will only be used by aliased downsample maps
		//desc.allowAlias = true; // We will alias the shadow maps to allow UAV downsampling
		switch (light->type) {
		case Components::LightType::Point: // Cubemap
			desc.isCubemap = true;
			shadowMap = PixelBuffer::CreateShared(desc);
			shadowMap->SetName("PointShadowMap");
			break;
		case Components::LightType::Spot: // 2D texture
			shadowMap = PixelBuffer::CreateShared(desc);
			shadowMap->SetName("SpotShadowMap");
			break;
		case Components::LightType::Directional: // Texture array
			desc.isArray = true;
			desc.arraySize = getNumCascades();
			shadowMap = PixelBuffer::CreateShared(desc);
			shadowMap->SetName("DirectionalShadowMap");
			break;

		}
		shadowMap->ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Shadow maps" }));
		//light->SetShadowMap(map);
        AddResource(shadowMap);
		return shadowMap;
    }

	void RemoveMap(std::shared_ptr<TextureAsset> map) {
		if (map != nullptr) {
			RemoveResource(map->ImagePtr().get());
		}

	}

private:
	std::function<uint8_t()> getNumCascades;
};

class LinearShadowMaps : public ResourceGroup {
public:
	LinearShadowMaps(const std::string& name)
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
		desc.format = rhi::Format::R32_Float;
		desc.hasSRV = true;
		desc.srvFormat = rhi::Format::R32_Float;
		desc.hasUAV = true;
		desc.uavFormat = rhi::Format::R32_Float;
		desc.generateMipMaps = true;
		desc.hasRTV = true;
		desc.rtvFormat = rhi::Format::R32_Float;
		desc.clearColor[0] = std::numeric_limits<float>().max();
		switch (light->type) {
		case Components::LightType::Point: // Cubemap
			desc.isCubemap = true;
			shadowMap = PixelBuffer::CreateShared(desc);
			shadowMap->SetName("linearShadowMap");
			break;
		case Components::LightType::Spot: // 2D texture
			shadowMap = PixelBuffer::CreateShared(desc);
			shadowMap->SetName("linearShadowMap");
			break;
		case Components::LightType::Directional: // Texture array
			desc.isArray = true;
			desc.arraySize = getNumCascades();
			shadowMap = PixelBuffer::CreateShared(desc);
			shadowMap->SetName("linearShadowMap");
			break;

		}
		shadowMap->ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Shadow maps" }));
		AddResource(shadowMap);
		return shadowMap;
	}

	void RemoveMap(std::shared_ptr<TextureAsset> map) {
		if (map != nullptr) {
			RemoveResource(map->ImagePtr().get());
		}

	}

private:
	std::function<uint8_t()> getNumCascades;
};