#pragma once
#include <memory>

#include "Resources/ResourceHandles.h"
#include "Resources/ResourceStates.h"
#include "Resources/Resource.h"
#include "Filetypes.h"

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
	void SetFilepath(const std::string& path);
	std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState fromState, ResourceState toState);
	BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState);
	virtual void SetName(const std::wstring& name);
	ID3D12Resource* GetAPIResource() const override;
	ImageFiletype GetFileType() const { return m_fileType; }
	std::string GetFilePath() const { return m_filePath; }
	ImageLoader GetImageLoader() const { return m_imageLoader; }
	bool AlphaIsAllOpaque() const { return m_alphaIsAllOpaque; }
	void SetAlphaIsAllOpaque(bool value) { m_alphaIsAllOpaque = value; }
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
	std::string m_filePath;
	ImageFiletype m_fileType;
	ImageLoader m_imageLoader;
	bool m_alphaIsAllOpaque = true;
};