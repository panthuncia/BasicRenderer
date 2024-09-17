#pragma once
#include <memory>

#include "PixelBuffer.h"
#include "Sampler.h"
#include "GloballyIndexedResource.h"
#include "ResourceHandles.h"

class Texture : public GloballyIndexedResource{
public:
	Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler);
	UINT GetBufferDescriptorIndex();
	UINT GetSamplerDescriptorIndex();
	TextureHandle<PixelBuffer>& GetHandle();
	
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
};