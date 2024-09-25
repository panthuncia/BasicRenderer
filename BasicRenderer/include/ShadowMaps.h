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
		switch (light->GetLightType()) {
		case LightType::Point: // Cubemap
			shadowMap = PixelBuffer::CreateSingleTexture(shadowResolution, shadowResolution, 1, true, false, true, false);
			shadowMap->SetName(L"PointShadowMap: "+light->m_name);
			break;
		case LightType::Spot: // 2D texture
			shadowMap = PixelBuffer::CreateSingleTexture(shadowResolution, shadowResolution, 1, false, false, true, false);
			shadowMap->SetName(L"SpotShadowMap");
			break;
		case LightType::Directional: // Texture array
			shadowMap = PixelBuffer::CreateTextureArray(shadowResolution, shadowResolution, 1, getNumCascades(), false, false, true, false);
			shadowMap->SetName(L"DirectionalShadowMap");
			break;

		}
		std::shared_ptr<Texture> map = std::make_shared<Texture>(shadowMap, shadowSampler);
		light->SetShadowMap(map);
        AddGloballyIndexedResource(map->GetBuffer());
    }

	void RemoveMap(Light* light) {
		int index = light->getShadowMap()->GetBufferDescriptorIndex();
		if (index != -1) {
			RemoveGloballyIndexedResource(index);
		}

	}

private:
	std::function<uint8_t()> getNumCascades;
};