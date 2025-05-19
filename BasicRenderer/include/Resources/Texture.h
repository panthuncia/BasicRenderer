#pragma once
#include <memory>

#include "Resources/ResourceHandles.h"
#include "Resources/ResourceStates.h"
#include "Resources/Resource.h"
#include "Import/Filetypes.h"

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
	BarrierGroups GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState);
	virtual void SetName(const std::wstring& name);
	ID3D12Resource* GetAPIResource() const override;
	void SetFileType(ImageFiletype fileType) { m_fileType = fileType; }
	ImageFiletype GetFileType() const { return m_fileType; }
	std::string GetFilePath() const { return m_filePath; }
	ImageLoader GetImageLoader() const { return m_imageLoader; }
	bool AlphaIsAllOpaque() const { return m_alphaIsAllOpaque; }
	void SetAlphaIsAllOpaque(bool value) { m_alphaIsAllOpaque = value; }
	virtual uint64_t GetGlobalResourceID() const;
	ResourceAccessType GetSubresourceAccessType(unsigned int subresourceIndex) const override;
	ResourceLayout GetSubresourceLayout(unsigned int subresourceIndex) const override;
	ResourceSyncState GetSubresourceSyncState(unsigned int subresourceIndex) const override;
	virtual SymbolicTracker* GetStateTracker() override;
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
	std::string m_filePath;
	ImageFiletype m_fileType = ImageFiletype::UNKNOWN;
	ImageLoader m_imageLoader;
	bool m_alphaIsAllOpaque = true;
};