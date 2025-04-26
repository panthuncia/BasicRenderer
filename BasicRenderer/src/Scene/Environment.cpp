#include "Scene/Environment.h"

#include "Managers/EnvironmentManager.h"

void Environment::SetFromHDRI(std::shared_ptr<Texture> hdriTexture) {
	m_hdriTexture = hdriTexture;
	m_currentManager->SetFromHDRI(this, hdriTexture);
}

unsigned int Environment::GetEnvironmentIndex() const {
	return m_environmentBufferView->GetOffset()/sizeof(EnvironmentInfo);
}