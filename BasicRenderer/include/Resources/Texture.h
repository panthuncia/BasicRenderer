#pragma once
#include <memory>

#include "Resources/Resource.h"
#include "Import/Filetypes.h"

#include <rhi.h>

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
	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);
	virtual void SetName(const std::string& name);
	rhi::Resource GetAPIResource() override;
	void SetFileType(ImageFiletype fileType) { m_fileType = fileType; }
	ImageFiletype GetFileType() const { return m_fileType; }
	std::string GetFilePath() const { return m_filePath; }
	ImageLoader GetImageLoader() const { return m_imageLoader; }
	bool AlphaIsAllOpaque() const { return m_alphaIsAllOpaque; }
	void SetAlphaIsAllOpaque(bool value) { m_alphaIsAllOpaque = value; }
	virtual uint64_t GetGlobalResourceID() const;
	virtual SymbolicTracker* GetStateTracker() override;
private:
	std::shared_ptr<PixelBuffer> m_image;
	std::shared_ptr<Sampler> m_sampler;
	std::string m_filePath;
	ImageFiletype m_fileType = ImageFiletype::UNKNOWN;
	ImageLoader m_imageLoader;
	bool m_alphaIsAllOpaque = true;
};