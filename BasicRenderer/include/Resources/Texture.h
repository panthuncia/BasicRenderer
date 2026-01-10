#pragma once
#include <memory>

#include <string>
#include <variant>

#include "Import/Filetypes.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Sampler.h"
#include "Factories/TextureFactory.h"

class PixelBuffer;
class Sampler;
struct RenderContext;

struct TextureFileMeta {
	std::string filePath;
	ImageFiletype fileType = ImageFiletype::UNKNOWN;
	ImageLoader loader{};
	bool alphaIsAllOpaque = true;
};

// Helper for std::visit with multiple lambdas
template<class... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

class TextureAsset {
public:
    using BytesPtr = std::shared_ptr<std::vector<uint8_t>>;
    using BytesList = std::vector<BytesPtr>;
    // Variant for different ways an asset might be stored
    // On-disk file, in-memory bytes, etc.
    using StorageVariant = std::variant<
        std::monostate,
        BytesList,
        std::string,
        std::shared_ptr<PixelBuffer>>;
    StorageVariant m_initialStorage;

    static std::shared_ptr<TextureAsset> CreateShared(TextureDescription desc,
        StorageVariant initialStorage,
        std::shared_ptr<Sampler> defaultSampler,
        TextureFileMeta meta) {
	    return std::shared_ptr<TextureAsset>(new TextureAsset(
            std::move(desc),
            std::move(initialStorage),
            std::move(defaultSampler),
            std::move(meta)
		));
    }
    
	// Resolve to a vector of bytes
    const BytesList& ResolveToBytes()
    {
        // Something we can safely return by reference in "empty" cases.
        static const BytesList kEmpty = {};

        return std::visit(Overloaded{
            [&](std::monostate) -> const BytesList& {
                throw std::runtime_error("ResolveToBytes: no initial storage set");
                return kEmpty;
            },
            [&](const BytesList& p) -> const BytesList& {
                return p;
            },
            [&](const std::string& path) -> const BytesList& {
				throw std::runtime_error("Not implemented");
            },
        [&](const std::shared_ptr<PixelBuffer>& pb) -> const BytesList& {
	            throw std::runtime_error("ResolveToBytes: Not implemented for PixelBuffer");
	            return kEmpty;
			},
            }, m_initialStorage);
    }

    PixelBuffer& Image() const { return *m_image; }
    std::shared_ptr<PixelBuffer> ImagePtr() const { return m_image; }

    Sampler& SamplerState() const { return *m_sampler; }
    UINT SamplerDescriptorIndex() const { return m_sampler->GetDescriptorIndex(); }

    const TextureFileMeta& Meta() const { return m_meta; }

    void SetName(const std::string& name)
    {
        m_name = name;
        if (m_image) {
            m_image->SetName(name);
        }
    }

    void EnsureUploaded(const TextureFactory& factory) {
	    if (!m_image) {
            m_image = factory.CreateAlwaysResidentPixelBuffer(m_desc, TextureFactory::TextureInitialData::FromBytes(ResolveToBytes()), m_name);
			// If we're uploading from raw bytes, we can clear the initial storage to save memory. Revert to path storage if needed again.
			if (m_initialDataString != "") {
				m_initialStorage = m_initialDataString;
            }
            else {
                m_initialStorage = std::monostate{};
            }
	    }
    }

    unsigned int GetWidth() const {
        return m_desc.imageDimensions[0].width;
    }
    unsigned int GetHeight() const {
        return m_desc.imageDimensions[0].height;
    }
    void SetGenerateMipmaps(bool generate) {
	    m_desc.generateMipMaps = generate;
    }

private:
    TextureAsset(TextureDescription desc,
        StorageVariant initialStorage,
        std::shared_ptr<Sampler> defaultSampler,
        TextureFileMeta meta)
        : m_desc(std::move(desc))
        , m_initialStorage(std::move(initialStorage))
        , m_sampler(defaultSampler ? std::move(defaultSampler) : Sampler::GetDefaultSampler())
        , m_meta(std::move(meta)) {
        if (std::holds_alternative<std::shared_ptr<PixelBuffer>>(m_initialStorage)) { // Already initialized
            m_image = std::get<std::shared_ptr<PixelBuffer>>(m_initialStorage);
        }
		if (std::holds_alternative<std::string>(m_initialStorage)) { // Store path for potential re-use
            m_initialDataString = std::get<std::string>(m_initialStorage);
		}
    }
	TextureDescription m_desc;
    std::shared_ptr<PixelBuffer> m_image;
    std::shared_ptr<Sampler> m_sampler;
    TextureFileMeta m_meta;
    std::string m_initialDataString;
    std::string m_name;
};