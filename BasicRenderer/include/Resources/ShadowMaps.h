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
#include "Sampler.h"
#include "Utilities/Utilities.h"
#include "Resources/TextureDescription.h"
class ShadowMaps : public ResourceGroup {
public:
    ShadowMaps(const std::wstring& name)
        : ResourceGroup(name) {
		getNumCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		currentState = ResourceState::UNKNOWN;
	}

    std::shared_ptr<Texture> AddMap(LightInfo* light, uint16_t shadowResolution) {
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
		desc.channels = 1;
		desc.format = DXGI_FORMAT_R32_TYPELESS;
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
		std::shared_ptr<Texture> map = std::make_shared<Texture>(shadowMap, shadowSampler);
		//light->SetShadowMap(map);
        AddResource(map->GetBuffer());
		return map;
    }

	void RemoveMap(std::shared_ptr<Texture> map) {
		if (map != nullptr) {
			RemoveResource(map.get());
		}

	}

private:
	std::function<uint8_t()> getNumCascades;
};