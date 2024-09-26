#pragma once
#include <memory>

#include "ResourceHandles.h"
#include "ResourceStates.h"
#include "Resource.h"

class PixelBuffer;
class Sampler;
class RenderContext;

class Texture : public Resource { // Sometimes, a resource needs a unique sampler, so both Texture and PixelBuffer inherit from Resource
public:
	Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler);
	UINT GetBufferDescriptorIndex();
	UINT GetSamplerDescriptorIndex();
	TextureHandle<PixelBuffer>& GetHandle();
	std::shared_ptr<PixelBuffer> GetBuffer() {
		return m_image;
	}
	UINT GetSRVDescriptorIndex() const;
	void Transition(const RenderContext& context, ResourceState fromState, ResourceState toState);
	virtual void SetName(const std::wstring& name);
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
};