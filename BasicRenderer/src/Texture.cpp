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

std::vector<D3D12_RESOURCE_BARRIER>& Texture::GetTransitions(ResourceState fromState, ResourceState toState) {
	currentState = toState;
	return m_image->GetTransitions(fromState, toState); // Transition the underlying PixelBuffer
}

void Texture::SetName(const std::wstring& name) {
	Resource::SetName(name);
	m_image->SetName(name);
}

ID3D12Resource* Texture::GetAPIResource() const { return m_image->GetAPIResource(); }