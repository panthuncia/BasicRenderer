#pragma once
#include <memory>

#include "PixelBuffer.h"
#include "Sampler.h"
#include "GloballyIndexedResource.h"
class Texture : public GloballyIndexedResource{
public:
	Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler);
	UINT GetBufferDescriptorIndex();
	UINT GetSamplerDescriptorIndex();
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
};