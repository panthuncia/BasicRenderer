#pragma once

#include <functional>
#include <memory>
#include <string>

#include "SettingsManager.h"
#include "ResourceGroup.h"
#include "Texture.h"
#include "Light.h"
#include "ResourceManager.h"
#include "PixelBuffer.h"
#include "Sampler.h"
#include "utilities.h"
#include "TextureDescription.h"
class ShadowMaps : public ResourceGroup {
public:
    ShadowMaps(const std::wstring& name)
        : ResourceGroup(name) {
		getNumCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		currentState = ResourceState::UNKNOWN;
	}

    void AddMap(Light* light, uint16_t shadowResolution) {
		std::shared_ptr<PixelBuffer> shadowMap;
		auto shadowSampler = Sampler::GetDefaultShadowSampler();
		TextureDescription desc;
		desc.width = shadowResolution;
		desc.height = shadowResolution;
		desc.hasDSV = true;
		desc.channels = 1;
		desc.format = DXGI_FORMAT_R32_TYPELESS;
		switch (light->GetLightType()) {
		case LightType::Point: // Cubemap
			desc.isCubemap = true;
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"PointShadowMap: "+light->m_name);
			break;
		case LightType::Spot: // 2D texture
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"SpotShadowMap");
			break;
		case LightType::Directional: // Texture array
			desc.isArray = true;
			desc.arraySize = getNumCascades();
			shadowMap = PixelBuffer::Create(desc);
			shadowMap->SetName(L"DirectionalShadowMap");
			break;

		}
		std::shared_ptr<Texture> map = std::make_shared<Texture>(shadowMap, shadowSampler);
		light->SetShadowMap(map);
        AddIndexedResource(map->GetBuffer(), map->GetBuffer()->GetSRVInfo().index);
    }

	void RemoveMap(Light* light) {
		int index = light->getShadowMap()->GetBuffer()->GetSRVInfo().index;
		if (index != -1) {
			RemoveIndexedResource(index);
		}

	}

private:
	std::function<uint8_t()> getNumCascades;
};