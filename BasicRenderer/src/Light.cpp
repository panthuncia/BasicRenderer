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
}

Light::Light(std::string name, unsigned int type, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction) : SceneNode(name) {
	m_lightInfo.type = type;
	m_lightInfo.posWorldSpace = XMLoadFloat3(&position);
	m_lightInfo.color = XMVector3Normalize(XMLoadFloat3(&color));
	m_lightInfo.color *= intensity;
	m_lightInfo.dirWorldSpace = XMLoadFloat3(&direction);
	m_lightInfo.shadowCaster = 1;
}

LightInfo& Light::GetLightInfo() {
	return m_lightInfo;
}
