#pragma once
#include <vector>
#include <DirectXMath.h>
#include <array>
#include <functional>
#include <memory>
#include <string>

#include "Texture.h"
#include "SceneNode.h"
#include "buffers.h"
#include "PixelBuffer.h"

class DynamicGloballyIndexedResource;
class BufferView;
class Buffer;

enum LightType {
	Point = 0,
	Spot = 1,
	Directional = 2
};

class Light : public SceneNode{
public:

	static std::shared_ptr<Light> CreateDirectionalLight(std::wstring name, XMFLOAT3 color, float intensity, XMFLOAT3 direction) {
		return std::shared_ptr<Light>(new Light(name, LightType::Directional, { 0, 0, 0 }, color, intensity, 0, 0, 0, direction));
	}

	static std::shared_ptr<Light> CreatePointLight(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0) {
		return std::shared_ptr<Light>(new Light(name, LightType::Point, position, color, intensity, constantAttenuation, linearAttenuation, quadraticAttenuation));
	}

	static std::shared_ptr<Light> CreateSpotLight(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0) {
		return std::shared_ptr<Light>(new Light(name, LightType::Spot, position, color, intensity, constantAttenuation, linearAttenuation, quadraticAttenuation, direction, innerConeAngle, outerConeAngle));
	}

	static std::shared_ptr<Light> CopyLight(LightInfo& lightInfo) {
		return std::shared_ptr<Light>(new Light(lightInfo));
	}

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

	void SetIsShadowCaster(bool shadowCaster) {
		m_shadowCaster = shadowCaster;
	}

	bool GetIsShadowCaster() {
		return m_shadowCaster;
	}

	void AddLightObserver(ISceneNodeObserver<Light>* observer);
	void RemoveLightObserver(ISceneNodeObserver<Light>* observer);

	void OnUpdate() override {
		UpdateLightInfo();
		// Notify Light-specific observers
		NotifyLightObservers();
	}

	DirectX::XMMATRIX GetLightViewMatrix();
	DirectX::XMMATRIX GetLightProjectionMatrix();
	DirectX::XMVECTOR GetLightDir();
	void SetShadowMap(std::shared_ptr<Texture> shadowMap);
	std::shared_ptr<Texture>& getShadowMap();
	void AddPerViewOpaqueIndirectCommandBuffer(std::shared_ptr<DynamicGloballyIndexedResource> buffer) {
		m_perViewOpaqueIndirectCommandBuffers.push_back(buffer);
	}

	void AddPerViewAlphaTestIndirectCommandBuffer(std::shared_ptr<DynamicGloballyIndexedResource> buffer) {
		m_perViewAlphaTestIndirectCommandBuffers.push_back(buffer);
	}

	void AddPerViewBlendIndirectCommandBuffer(std::shared_ptr<DynamicGloballyIndexedResource> buffer) {
		m_perViewBlendIndirectCommandBuffers.push_back(buffer);
	}

	void DeleteAllIndirectCommandBuffers() {
		m_perViewOpaqueIndirectCommandBuffers.clear();
		m_perViewAlphaTestIndirectCommandBuffers.clear();
		m_perViewBlendIndirectCommandBuffers.clear();
	}

	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>& GetPerViewOpaqueIndirectCommandBuffers() {
		return m_perViewOpaqueIndirectCommandBuffers;
	}

	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>& GetPerViewAlphaTestIndirectCommandBuffers() {
		return m_perViewAlphaTestIndirectCommandBuffers;
	}

	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>& GetPerViewBlendIndirectCommandBuffers() {
		return m_perViewBlendIndirectCommandBuffers;
	}

	void SetCameraBufferViews(std::vector<std::shared_ptr<BufferView>> cameraBufferViews);

	std::vector<std::shared_ptr<BufferView>>& GetCameraBufferViews() {
		return m_cameraBufferViews;
	}

	std::vector<std::array<ClippingPlane, 6>>& GetFrustumPlanes() {
		return m_frustumPlanes;
	}

private:

	Light(std::wstring name, LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0, XMFLOAT3 direction = { 0, 0, 0 }, float innerConeAngle = 0, float outerConeAngle = 0);
	Light(LightInfo& lightInfo);

	std::vector<std::shared_ptr<Buffer>> m_lightFrameConstantHandles;
	LightInfo m_lightInfo;
	int m_currentLightBufferIndex = -1;
	int m_currentLightViewInfoIndex = -1;
	std::vector<ISceneNodeObserver<Light>*> lightObservers;
	XMMATRIX m_lightProjection;
	std::shared_ptr<Texture> m_shadowMap;
	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> m_perViewOpaqueIndirectCommandBuffers;
	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> m_perViewAlphaTestIndirectCommandBuffers;
	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> m_perViewBlendIndirectCommandBuffers;
	std::vector<std::shared_ptr<BufferView>> m_cameraBufferViews;
	std::vector<std::array<ClippingPlane, 6>> m_frustumPlanes;
	bool m_shadowCaster = false;

	void NotifyLightObservers();
	void UpdateLightInfo();
	void CreateProjectionMatrix(float nearPlane, float farPlane);
	void CalculateFrustumPlanes();

	std::function<uint8_t()> getNumCascades;
	std::function<std::vector<float>()> getDirectionalLightCascadeSplits;

	friend class ShadowMaps;
};