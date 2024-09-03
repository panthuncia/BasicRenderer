#include "Light.h"

Light::Light(std::string name, unsigned int type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation, float linearAttenuation, float quadraticAttenuation, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle) : SceneNode(name) {
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
}

Light::Light(std::string name, unsigned int type, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction) : SceneNode(name) {
	m_lightInfo.type = type;
	m_lightInfo.posWorldSpace = XMLoadFloat3(&position);
	m_lightInfo.color = XMVector3Normalize(XMLoadFloat3(&color));
	m_lightInfo.color *= intensity;
	m_lightInfo.dirWorldSpace = XMLoadFloat3(&direction);
	m_lightInfo.shadowCaster = 1;
	m_lightInfo.attenuation = XMVectorSet(0, 0, 0, 0);

	transform.setLocalPosition(position);
	transform.setDirection(direction);
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