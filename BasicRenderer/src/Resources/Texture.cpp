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

BarrierGroups& Texture::GetEnhancedBarrierGroup(ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
	m_currentAccessType = newAccessType;
	m_currentLayout = newLayout;
	m_prevSyncState = newSyncState;
	return m_image->GetEnhancedBarrierGroup(prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState); // Transition the underlying PixelBuffer
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

ResourceAccessType Texture::GetCurrentAccessType() const { return m_image->GetCurrentAccessType(); }
ResourceLayout Texture::GetCurrentLayout() const { return m_image->GetCurrentLayout(); }
ResourceSyncState Texture::GetPrevSyncState() const { return m_image->GetPrevSyncState(); }