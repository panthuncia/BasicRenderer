#pragma once
#include <memory>

#include "ResourceHandles.h"
#include "ResourceStates.h"

class PixelBuffer;
class Sampler;
class RenderContext;

class Texture{
public:
	Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler);
	UINT GetBufferDescriptorIndex();
	UINT GetSamplerDescriptorIndex();
	TextureHandle<PixelBuffer>& GetHandle();
	std::shared_ptr<PixelBuffer> GetBuffer() {
		return m_image;
	}
protected:
	void Transition(RenderContext& context, ResourceState fromState, ResourceState toState);
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
};