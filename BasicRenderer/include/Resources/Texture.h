#pragma once
#include <memory>

#include <string>

#include "Import/Filetypes.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Sampler.h"


class PixelBuffer;
class Sampler;
struct RenderContext;

struct TextureFileMeta {
	std::string filePath;
	ImageFiletype fileType = ImageFiletype::UNKNOWN;
	ImageLoader loader{};
	bool alphaIsAllOpaque = true;
};

class TextureAsset {
public:
    TextureAsset(std::shared_ptr<PixelBuffer> image,
        std::shared_ptr<Sampler> defaultSampler,
        TextureFileMeta meta)
        : m_image(std::move(image))
        , m_sampler(defaultSampler ? std::move(defaultSampler) : Sampler::GetDefaultSampler())
        , m_meta(std::move(meta)) {
    }

    PixelBuffer& Image() const { return *m_image; }
    std::shared_ptr<PixelBuffer> ImagePtr() const { return m_image; }

    Sampler& SamplerState() const { return *m_sampler; }
    UINT SamplerDescriptorIndex() const { return m_sampler->GetDescriptorIndex(); }

    const TextureFileMeta& Meta() const { return m_meta; }

    void SetName(std::string name)
    {
		m_image->SetName(name);
    }

private:
    std::shared_ptr<PixelBuffer> m_image;
    std::shared_ptr<Sampler> m_sampler;
    TextureFileMeta m_meta;
};