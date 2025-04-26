#include "Resources/Texture.h"
#include "Resources/Sampler.h"
#include "Resources/PixelBuffer.h"
#include "Render/RenderContext.h"

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

BarrierGroups& Texture::GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
	currentState = newState;
	return m_image->GetEnhancedBarrierGroup(prevState, newState, prevAccessType, newAccessType, prevSyncState, newSyncState); // Transition the underlying PixelBuffer
}

void Texture::SetName(const std::wstring& name) {
	Resource::SetName(name);
	m_image->SetName(name);
}

ID3D12Resource* Texture::GetAPIResource() const { return m_image->GetAPIResource(); }

void Texture::SetFilepath(const std::string& filepath) {
	m_filePath = filepath;
	ImageFiletype format = extensionToFiletype[GetFileExtension(filepath)];
	ImageLoader loader = imageFiletypeToLoader[format];
	m_fileType = format;
	m_imageLoader = loader;
}

uint64_t Texture::GetGlobalResourceID() const { 
	return m_image->GetGlobalResourceID(); 
}