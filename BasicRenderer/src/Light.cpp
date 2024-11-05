#include "Light.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "Texture.h"
#include "PixelBuffer.h"
#include "Utilities.h"

Light::Light(std::wstring name, LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation, float linearAttenuation, float quadraticAttenuation, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle) : SceneNode(name) {
	m_lightInfo.type = type;
	m_lightInfo.posWorldSpace = XMLoadFloat3(&position);
	m_lightInfo.color = XMVector3Normalize(XMLoadFloat3(&color));
	m_lightInfo.color *= intensity;
	float nearPlane = 0.01;
	float farPlane = 5.0;
	m_lightInfo.attenuation = XMVectorSet(constantAttenuation, linearAttenuation, quadraticAttenuation, 0);
	m_lightInfo.dirWorldSpace = XMLoadFloat3(&direction);
	m_lightInfo.innerConeAngle = cos(innerConeAngle);
	m_lightInfo.outerConeAngle = cos(outerConeAngle);
	m_lightInfo.shadowViewInfoIndex = -1;
	m_lightInfo.nearPlane = nearPlane;
	m_lightInfo.farPlane = farPlane;
	transform.setLocalPosition(position);

	if (direction.x != 0 || direction.y != 0 || direction.z || 0) {
		transform.setDirection(direction);
	}

	getNumCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");

	CreateProjectionMatrix(nearPlane, farPlane);
}

Light::Light(LightInfo& lightInfo) : SceneNode(m_name) {
	m_lightInfo = lightInfo;
	getNumCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	float nearPlane = 0.07;
	float farPlane = 5.0;
	m_lightInfo.nearPlane = nearPlane;
	m_lightInfo.farPlane = farPlane;
	CreateProjectionMatrix(nearPlane, farPlane);
}

LightInfo& Light::GetLightInfo() {
	return m_lightInfo;
}

void Light::UpdateLightInfo() {
	auto& matrix = transform.modelMatrix;
	m_lightInfo.posWorldSpace = XMVectorSet(matrix.r[3].m128_f32[0],  // _41
											matrix.r[3].m128_f32[1],  // _42
											matrix.r[3].m128_f32[2],  // _43
											1.0f);

	XMVECTOR worldForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	m_lightInfo.dirWorldSpace = XMVector3Normalize(XMVector3TransformNormal(worldForward, matrix));
}

void Light::AddLightObserver(ISceneNodeObserver<Light>* observer) {
	if (observer && std::find(lightObservers.begin(), lightObservers.end(), observer) == lightObservers.end()) {
		lightObservers.push_back(observer);
	}
}

void Light::RemoveLightObserver(ISceneNodeObserver<Light>* observer) {
	auto it = std::remove(lightObservers.begin(), lightObservers.end(), observer);
	if (it != lightObservers.end()) {
		lightObservers.erase(it);
	}
}

void Light::NotifyLightObservers() {
	for (auto observer : lightObservers) {
		observer->OnNodeUpdated(this);
	}
}

void Light::UpdateLightMatrices() {
	switch (m_lightFrameConstantHandles.size()) {
	case 1: {
		PerFrameCB frameCB;
		frameCB.viewMatrix = XMMatrixIdentity();
		frameCB.projectionMatrix = XMMatrixIdentity();
		frameCB.eyePosWorldSpace = m_lightInfo.posWorldSpace;
		frameCB.ambientLighting = m_lightInfo.color;
		frameCB.lightBufferIndex = m_currentLightBufferIndex;
		frameCB.numLights = 1;
		ResourceManager::GetInstance().UpdateConstantBuffer<PerFrameCB>(m_lightFrameConstantHandles[0], frameCB);
	}
	}
}

void Light::CreateFrameConstantBuffers() {
	auto& resourceManager = ResourceManager::GetInstance();

	switch (m_lightInfo.type) {
	case LightType::Point: // Six views for cubemap
		m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		break;
	case LightType::Spot:
		m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		break;
	case LightType::Directional:
		uint8_t numCascades = getNumCascades();
		for (int i = 0; i < numCascades; i++) {
			m_lightFrameConstantHandles.push_back(resourceManager.CreateConstantBuffer<PerFrameCB>());
		}
		break;
	}
	UpdateLightMatrices();
}

LightType Light::GetLightType() const {
	return (LightType)m_lightInfo.type;
}

void Light::CreateProjectionMatrix(float nearPlane, float farPlane) {
	float aspect = 1.0f;

	switch ((LightType)m_lightInfo.type) {
	case LightType::Spot:
		m_lightProjection = XMMatrixPerspectiveFovRH(acos(this->m_lightInfo.outerConeAngle) * 2, aspect, nearPlane, farPlane);
		break;
	case LightType::Point:
		m_lightProjection = XMMatrixPerspectiveFovRH(XM_PI / 2, aspect, nearPlane, farPlane);
		break;
	}
}

DirectX::XMMATRIX Light::GetLightViewMatrix() {
	auto dir = GetLightDir();
	auto pos = transform.getGlobalPosition();
	auto up = XMFLOAT3(0, 1, 0);
	auto matrix = XMMatrixLookToRH(XMLoadFloat3(&pos), dir, XMLoadFloat3(&up));
	return matrix;
	//auto matrix = XMMatrixInverse(nullptr, transform.modelMatrix);
	//return RemoveScalingFromMatrix(matrix);
}

DirectX::XMMATRIX Light::GetLightProjectionMatrix() {
	return m_lightProjection;
}

// Returns a 3-axis direction vector
DirectX::XMVECTOR Light::GetLightDir() {
	// Extract the forward vector (Z-axis)
	return XMVector3Normalize(transform.modelMatrix.r[2]);
}

void Light::SetShadowMap(std::shared_ptr<Texture> shadowMap) {
	m_shadowMap = shadowMap;
	m_lightInfo.shadowMapIndex = shadowMap->GetBuffer()->GetSRVInfo().index;
	m_lightInfo.shadowSamplerIndex = shadowMap->GetSamplerDescriptorIndex();
}

std::shared_ptr<Texture>& Light::getShadowMap() {
	return m_shadowMap;
}

void Light::SetCameraBufferViews(std::vector<std::shared_ptr<BufferView>> views) {
	m_cameraBufferViews = views;
}