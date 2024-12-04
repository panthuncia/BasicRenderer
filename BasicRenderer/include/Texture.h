#pragma once
#include <memory>

#include "ResourceHandles.h"
#include "ResourceStates.h"
#include "Resource.h"

class PixelBuffer;
class Sampler;
class RenderContext;

class Texture : public SingleResource { // Sometimes, a resource needs a unique sampler, so both Texture and PixelBuffer inherit from Resource
public:
	Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler);
	UINT GetSamplerDescriptorIndex();
	std::shared_ptr<PixelBuffer> GetBuffer() {
		return m_image;
	}
	std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(uint8_t frameIndex, ResourceState fromState, ResourceState toState) override;
	virtual void SetName(const std::wstring& name);
	ID3D12Resource* GetAPIResource(uint8_t frameIndex) const override;
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
};