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
	UINT GetSamplerDescriptorIndex();
	std::shared_ptr<PixelBuffer> GetBuffer() {
		return m_image;
	}
	std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState fromState, ResourceState toState);
	BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState);
	virtual void SetName(const std::wstring& name);
	ID3D12Resource* GetAPIResource() const override;
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
};