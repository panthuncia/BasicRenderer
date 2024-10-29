#include "Texture.h"
#include "Sampler.h"
#include "PixelBuffer.h"
#include "RenderContext.h"

Texture::Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler) {
	m_image = image;
	m_sampler = sampler;
}

UINT Texture::GetSamplerDescriptorIndex() {
	return m_sampler->GetDescriptorIndex();
}

void Texture::Transition(ID3D12GraphicsCommandList* commandList, ResourceState fromState, ResourceState toState) {
	currentState = toState;
	m_image->Transition(commandList, fromState, toState); // Transition the underlying PixelBuffer
}

void Texture::SetName(const std::wstring& name) {
	Resource::SetName(name);
	m_image->SetName(name);
}