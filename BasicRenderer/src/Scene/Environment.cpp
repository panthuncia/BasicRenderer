#include "Scene/Environment.h"

#include <DirectXMath.h>

#include "Managers/EnvironmentManager.h"
#include "Resources/Texture.h"
#include "Resources/PixelBuffer.h"

void Environment::SetFromHDRI(std::shared_ptr<Texture> hdriTexture) {
	m_hdriTexture = hdriTexture;
	m_currentManager->SetFromHDRI(this, hdriTexture);
}

unsigned int Environment::GetEnvironmentIndex() const {
	return m_environmentBufferView->GetOffset()/sizeof(EnvironmentInfo);
}

void Environment::SetEnvironmentCubemap(std::shared_ptr<Texture> texture) {
	m_environmentCubemap = texture;
	m_environmentInfo.cubeMapDescriptorIndex = texture->GetBuffer()->GetSRVInfo()[0].index;
	m_currentManager->UpdateEnvironmentView(*this);
}

void Environment::SetEnvironmentPrefilteredCubemap(std::shared_ptr<Texture> texture) {
	m_environmentPrefilteredCubemap = texture;
	m_environmentInfo.prefilteredCubemapDescriptorIndex = texture->GetBuffer()->GetSRVInfo()[0].index;
	m_currentManager->UpdateEnvironmentView(*this);
}

void Environment::SetReflectionCubemapResolution(unsigned int resolution) {
	reflectionCubemapResolution = resolution;
	m_environmentInfo.sphericalHarmonicsScale = (4.0f * DirectX::XM_PI / (resolution * resolution * 6));
	m_currentManager->UpdateEnvironmentView(*this);
}