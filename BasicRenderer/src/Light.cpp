#include "Light.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "DefaultDirection.h"

Light::Light(std::string name, LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation, float linearAttenuation, float quadraticAttenuation, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle) : SceneNode(name) {
	m_lightInfo.type = type;
	m_lightInfo.posWorldSpace = XMLoadFloat3(&position);
	m_lightInfo.color = XMVector3Normalize(XMLoadFloat3(&color));
	m_lightInfo.color *= intensity;
	m_lightInfo.attenuation = XMVectorSet(constantAttenuation, linearAttenuation, quadraticAttenuation, 0.0); // TODO: max range
	m_lightInfo.dirWorldSpace = XMLoadFloat3(&direction);
	m_lightInfo.innerConeAngle = innerConeAngle;
	m_lightInfo.outerConeAngle = outerConeAngle;
	m_lightInfo.shadowCaster = 1;

	transform.setLocalPosition(position);
	transform.setDirection(direction);

	CreateProjectionMatrix();
}

Light::Light(std::string name, LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction) : SceneNode(name) {
	m_lightInfo.type = type;
	m_lightInfo.posWorldSpace = XMLoadFloat3(&position);
	m_lightInfo.color = XMVector3Normalize(XMLoadFloat3(&color));
	m_lightInfo.color *= intensity;
	m_lightInfo.dirWorldSpace = XMLoadFloat3(&direction);
	m_lightInfo.shadowCaster = 1;
	m_lightInfo.attenuation = XMVectorSet(0, 0, 0, 0);

	transform.setLocalPosition(position);
	transform.setDirection(direction);

	CreateProjectionMatrix();
}

Light::Light(LightInfo& lightInfo) : SceneNode(name) {
	m_lightInfo = lightInfo;
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

void Light::CreateShadowMap() {
	
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
		uint8_t numCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();
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

void Light::CreateProjectionMatrix() {
	float aspect = 1.0f;
	float nearZ = 0.07f;
	float farZ = 70.0f;

	switch ((LightType)m_lightInfo.type) {
	case LightType::Spot:
		m_lightProjection = XMMatrixPerspectiveFovRH(this->m_lightInfo.outerConeAngle * 2, aspect, nearZ, farZ);
		break;
	case LightType::Point:
		m_lightProjection = XMMatrixPerspectiveFovRH(XM_PI / 2, aspect, nearZ, farZ);
		break;
	}
}

std::array<DirectX::XMMATRIX, 6> Light::GetCubemapViewMatrices() {
	// Define directions and up vectors for the six faces of the cubemap
	struct DirectionSet {
		XMVECTOR dir;
		XMVECTOR up;
	};
	const std::array<DirectionSet, 6> directions = {
		DirectionSet{XMVectorSet(1, 0, 0, 0), XMVectorSet(0, 1, 0, 0)},
		DirectionSet{XMVectorSet(-1, 0, 0, 0), XMVectorSet(0, 1, 0, 0)},
		DirectionSet{XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, 1, 0)},
		DirectionSet{XMVectorSet(0, -1, 0, 0), XMVectorSet(0, 0, -1, 0)},
		DirectionSet{XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 1, 0, 0)},
		DirectionSet{XMVectorSet(0, 0, -1, 0), XMVectorSet(0, 1, 0, 0)},
	};

	std::array<XMMATRIX, 6> viewMatrices{};
	auto posVec = transform.getGlobalPosition();
	XMVECTOR pos = XMLoadFloat3(&posVec);

	for (int i = 0; i < directions.size(); ++i) {
		XMVECTOR target = XMVectorAdd(pos, directions[i].dir);
		viewMatrices[i] = XMMatrixLookAtRH(pos, target, directions[i].up);
	}

	return viewMatrices;
}

DirectX::XMMATRIX Light::GetLightViewMatrix() {
	return XMMatrixTranspose(transform.modelMatrix);
}

DirectX::XMMATRIX Light::GetLightProjectionMatrix() {
	return m_lightProjection;
}

// Returns a 3-axis direction vector
DirectX::XMVECTOR Light::GetLightDir() {
	// Extract the forward vector (Z-axis)
	return transform.modelMatrix.r[2];
}