#include "Texture.h"
#include "Sampler.h"
#include "PixelBuffer.h"
#include "RenderContext.h"

Texture::Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler) {
	m_image = image;
	m_sampler = sampler;
}

UINT Texture::GetBufferDescriptorIndex() {
	return m_image->GetSRVDescriptorIndex();
}

UINT Texture::GetSamplerDescriptorIndex() {
	return m_sampler->GetDescriptorIndex();
}

TextureHandle<PixelBuffer>& Texture::GetHandle() {
	return m_image->GetHandle();
}

UINT Texture::GetSRVDescriptorIndex() const {
	return m_image->GetSRVDescriptorIndex();
}

void Texture::Transition(const RenderContext& context, ResourceState fromState, ResourceState toState) {
	m_image->Transition(context, fromState, toState); // Transition the underlying PixelBuffer
}

void Texture::SetName(const std::wstring& name) {
	Resource::SetName(name);
	m_image->SetName(name);
}