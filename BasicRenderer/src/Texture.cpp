#include "Texture.h"
#include "Sampler.h"
#include "PixelBuffer.h"
#include "RenderContext.h"

Texture::Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler) {
	m_image = image;
	if (sampler == nullptr) {
		m_sampler = Sampler::GetDefaultSampler();
	}
	else {
		m_sampler = sampler;
	}
}

UINT Texture::GetSamplerDescriptorIndex() {
	return m_sampler->GetDescriptorIndex();
}

std::vector<D3D12_RESOURCE_BARRIER>& Texture::GetTransitions(ResourceState fromState, ResourceState toState) {
	currentState = toState;
	return m_image->GetTransitions(fromState, toState); // Transition the underlying PixelBuffer
}

BarrierGroups& Texture::GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
	currentState = newState;
	return m_image->GetEnhancedBarrierGroup(prevState, newState, prevSyncState, newSyncState); // Transition the underlying PixelBuffer
}

void Texture::SetName(const std::wstring& name) {
	Resource::SetName(name);
	m_image->SetName(name);
}

ID3D12Resource* Texture::GetAPIResource() const { return m_image->GetAPIResource(); }