#pragma once
#include <vector>
#include <DirectXMath.h>
#include <array>
#include <functional>
#include <memory>

#include "Texture.h"
#include "SceneNode.h"
#include "buffers.h"
#include "ResourceHandles.h"
#include "PixelBuffer.h"

enum LightType {
	Point = 0,
	Spot = 1,
	Directional = 2
};

class Light : public SceneNode{
public:
	Light(std::string name, LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0, XMFLOAT3 direction = {0, 0, 0}, float innerConeAngle = 0, float outerConeAngle = 0);
	Light(std::string name, LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction);
	Light(LightInfo& lightInfo);

	LightInfo& GetLightInfo();
	LightType GetLightType() const;

	int GetCurrentLightBufferIndex() {
		return m_currentLightBufferIndex;
	}

	void DecrementLightBufferIndex() {
		m_currentLightBufferIndex--;
	}

	void SetLightBufferIndex(int index) {
		m_currentLightBufferIndex = index;
	}

	int GetCurrentviewInfoIndex() {
		return m_currentLightViewInfoIndex;
	}

	void DecrementLightViewInfoIndex() {
		m_currentLightViewInfoIndex--;
	}

	void SetLightViewInfoIndex(int index) {
		m_currentLightViewInfoIndex = index;
		m_lightInfo.shadowViewInfoIndex = index;
	}

	void AddLightObserver(ISceneNodeObserver<Light>* observer);
	void RemoveLightObserver(ISceneNodeObserver<Light>* observer);

	void OnUpdate() override {
		UpdateLightInfo();
		// Notify Light-specific observers
		NotifyLightObservers();
	}

	void UpdateLightMatrices();
	DirectX::XMMATRIX GetLightViewMatrix();
	std::array<DirectX::XMMATRIX, 6> GetCubemapViewMatrices();
	DirectX::XMMATRIX GetLightProjectionMatrix();
	DirectX::XMVECTOR GetLightDir();
	void SetShadowMap(std::shared_ptr<Texture> shadowMap);
	std::shared_ptr<Texture>& getShadowMap();

private:
	std::vector<BufferHandle> m_lightFrameConstantHandles;
	LightInfo m_lightInfo;
	int m_currentLightBufferIndex = -1;
	int m_currentLightViewInfoIndex = -1;
	std::vector<ISceneNodeObserver<Light>*> lightObservers;
	XMMATRIX m_lightProjection;
	std::shared_ptr<Texture> m_shadowMap;

	void NotifyLightObservers();
	void UpdateLightInfo();
	void CreateFrameConstantBuffers();
	void CreateProjectionMatrix();

	std::function<uint8_t()> getNumCascades;

	friend class ShadowMaps;
};