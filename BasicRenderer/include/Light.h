#pragma once

#include "SceneNode.h"
#include "buffers.h"
class Light : public SceneNode{
public:
	Light(std::string name, unsigned int type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0, XMFLOAT3 direction = {0, 0, 0}, float innerConeAngle = 0, float outerConeAngle = 0);
	Light(std::string name, unsigned int type, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction);
	Light(LightInfo& lightInfo);

	LightInfo& GetLightInfo();

	int GetCurrentLightBufferIndex() {
		return m_currentLightBufferIndex;
	}

	void DecrementLightBufferIndex() {
		m_currentLightBufferIndex--;
	}

	void SetLightBufferIndex(int index) {
		m_currentLightBufferIndex = index;
	}

	void AddLightObserver(ISceneNodeObserver<Light>* observer);
	void RemoveLightObserver(ISceneNodeObserver<Light>* observer);

	void OnUpdate() override {
		UpdateLightInfo();
		// Notify Light-specific observers
		NotifyLightObservers();
	}

private:
	LightInfo m_lightInfo;
	int m_currentLightBufferIndex = -1;
	std::vector<ISceneNodeObserver<Light>*> lightObservers;
	void NotifyLightObservers();
	void UpdateLightInfo();
};