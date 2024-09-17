#include "Texture.h"

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
