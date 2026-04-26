#pragma once
#include <memory>
#include <mutex>

#include <string>
#include <variant>

#include "Import/Filetypes.h"
#include "OpenRenderGraph/OpenRenderGraph.h"
#include "Factories/TextureFactory.h"
#include "Resources/Sampler.h"

struct RenderContext;
struct TextureProcessingJobHandle;

enum class TextureSemantic : uint8_t {
    Unknown = 0,
    BaseColor,
    Emissive,
    Normal,
    Height,
    AO,
    Opacity,
    Metallic,
    Roughness,
    MetallicRoughness,
    OpenPBRColor,
    OpenPBRScalar,
};

enum class NormalMapConvention : uint8_t {
    DirectX = 0,
    OpenGL,
};

enum class TextureLoadPathTelemetry : uint8_t {
    Unknown = 0,
    DirectStorageGpuDirect,
    DirectStorageSystemMemoryRead,
    CpuFileRead,
    InMemoryContainer,
};

enum class TextureUploadPathTelemetry : uint8_t {
    Unknown = 0,
    DirectStorageGpuDirect,
    CpuImmediateUpload,
    AsyncProcessingPlaceholder,
    AsyncProcessingReadyUpload,
    ProcessingCacheUpload,
    ProcessingFailedFallback,
};

struct TextureProcessingSettings {
    TextureSemantic semantic = TextureSemantic::Unknown;
    bool isParticipatingMaterialTexture = false;
    bool requestMipChain = false;
    bool requestBlockCompression = false;
    bool allowAsyncPlaceholder = false;
    bool preferSRGB = false;
    bool preservePackedChannels = false;
    NormalMapConvention normalConvention = NormalMapConvention::DirectX;
    std::string sourceIdentity;
};

inline TextureProcessingSettings MakeMaterialTextureProcessingSettings(
    TextureSemantic semantic,
    bool preferSRGB,
    std::string sourceIdentity = {},
    bool preservePackedChannels = false,
    NormalMapConvention normalConvention = NormalMapConvention::DirectX)
{
    TextureProcessingSettings settings{};
    settings.semantic = semantic;
    settings.isParticipatingMaterialTexture = true;
    settings.requestMipChain = true;
    settings.requestBlockCompression = semantic != TextureSemantic::Height;
    settings.allowAsyncPlaceholder = true;
    settings.preferSRGB = preferSRGB;
    settings.preservePackedChannels = preservePackedChannels;
    settings.normalConvention = normalConvention;
    settings.sourceIdentity = std::move(sourceIdentity);
    return settings;
}

struct TextureSourceData {
    using BytesPtr = std::shared_ptr<std::vector<uint8_t>>;
    using BytesList = std::vector<BytesPtr>;

    TextureDescription desc;
    BytesList subresources;
    bool hasFullMipChain = false;
    bool isBlockCompressed = false;
};

//enum class ImageFiletype {
//	UNKNOWN,
//	HDR,
//	DDS,
//	TGA,
//	WIC
//};

struct TextureFileMeta {
	std::string filePath;
	ImageFiletype fileType = ImageFiletype::UNKNOWN;
	ImageLoader loader{};
	bool alphaIsAllOpaque = true;
    bool preferSRGB = false;
    bool isProcessingCacheArtifact = false;
    TextureLoadPathTelemetry loadPath = TextureLoadPathTelemetry::Unknown;
    TextureUploadPathTelemetry uploadPath = TextureUploadPathTelemetry::Unknown;
    std::string loadPathDetail;
    std::string uploadPathDetail;
    TextureProcessingSettings processing = {};
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
    const BytesList& ResolveToBytes() const
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
    TextureFileMeta& Meta() { return m_meta; }

    const TextureProcessingSettings& ProcessingSettings() const { return m_meta.processing; }
    void SetProcessingSettings(TextureProcessingSettings settings);

    void AdoptUploadedImage(std::shared_ptr<PixelBuffer> image);
    void RecordLoadPath(TextureLoadPathTelemetry path, std::string detail = {});
    void RecordUploadPath(TextureUploadPathTelemetry path, std::string detail = {});

    std::shared_ptr<TextureSourceData> BuildSourceData() const;

    void SetName(const std::string& name)
    {
        m_name = name;
        if (m_image) {
            m_image->SetName(name);
        }
    }

    void EnsureUploaded(const TextureFactory& factory);

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
			m_hasUploadedFinalImage = true;
        }
		if (std::holds_alternative<std::string>(m_initialStorage)) { // Store path for potential re-use
            m_initialDataString = std::get<std::string>(m_initialStorage);
		}
    }
	TextureDescription m_desc;
    std::shared_ptr<PixelBuffer> m_image;
    std::shared_ptr<Sampler> m_sampler;
    TextureFileMeta m_meta;
	std::shared_ptr<TextureProcessingJobHandle> m_processingHandle;
    std::string m_initialDataString;
    std::string m_name;
	bool m_hasUploadedPlaceholder = false;
	bool m_hasUploadedFinalImage = false;
    TextureLoadPathTelemetry m_lastReportedLoadPath = TextureLoadPathTelemetry::Unknown;
    TextureUploadPathTelemetry m_lastReportedUploadPath = TextureUploadPathTelemetry::Unknown;
};