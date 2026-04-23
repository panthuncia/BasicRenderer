#include "Resources/Texture.h"

#include <algorithm>

#include <rhi_helpers.h>

#include "Managers/Singletons/TextureProcessingManager.h"

namespace {
uint32_t CalcFullMipCount(uint32_t width, uint32_t height) {
	uint32_t levels = 1;
	while (width > 1 || height > 1) {
		width = (std::max)(1u, width >> 1);
		height = (std::max)(1u, height >> 1);
		++levels;
	}
	return levels;
}

std::shared_ptr<PixelBuffer> CreatePlaceholderTexture(
	const TextureFactory& factory,
	const TextureProcessingSettings& settings)
{
	TextureDescription desc{};
	desc.channels = 4;
	desc.format = settings.preferSRGB
		? rhi::Format::R8G8B8A8_UNorm_sRGB
		: rhi::Format::R8G8B8A8_UNorm;
	desc.generateMipMaps = false;

	ImageDimensions dims{};
	dims.width = 1;
	dims.height = 1;
	dims.rowPitch = 4;
	dims.slicePitch = 4;
	desc.imageDimensions.push_back(dims);

	uint8_t rgba[4] = { 255u, 255u, 255u, 255u };
	switch (settings.semantic) {
	case TextureSemantic::Normal:
		rgba[0] = 128u;
		rgba[1] = 128u;
		rgba[2] = 255u;
		break;
	case TextureSemantic::Height:
		rgba[0] = 0u;
		rgba[1] = 0u;
		rgba[2] = 0u;
		break;
	case TextureSemantic::Metallic:
		rgba[0] = 0u;
		break;
	case TextureSemantic::Roughness:
		rgba[0] = 255u;
		break;
	case TextureSemantic::MetallicRoughness:
		rgba[0] = 0u;
		rgba[1] = 255u;
		rgba[2] = 0u;
		break;
	default:
		break;
	}

	auto bytes = std::make_shared<std::vector<uint8_t>>(std::begin(rgba), std::end(rgba));
	return factory.CreateAlwaysResidentPixelBuffer(
		desc,
		TextureFactory::TextureInitialData::FromBytes({ bytes }),
		"TextureProcessingPlaceholder");
}
}

std::shared_ptr<TextureSourceData> TextureAsset::BuildSourceData() const {
	auto sourceData = std::make_shared<TextureSourceData>();
	sourceData->desc = m_desc;
	sourceData->subresources = ResolveToBytes();
	sourceData->isBlockCompressed = rhi::helpers::IsBlockCompressed(m_desc.format);

	if (m_desc.imageDimensions.empty()) {
		return sourceData;
	}

	const uint32_t faces = m_desc.isCubemap ? 6u : 1u;
	const uint32_t slices = faces * (std::max)(1u, m_desc.arraySize);
	const uint32_t fullMipCount = CalcFullMipCount(
		m_desc.imageDimensions[0].width,
		m_desc.imageDimensions[0].height);
	const size_t expectedSubresources = static_cast<size_t>(slices) * fullMipCount;

	sourceData->hasFullMipChain =
		!sourceData->subresources.empty() &&
		sourceData->subresources.size() == expectedSubresources &&
		m_desc.imageDimensions.size() == expectedSubresources;

	return sourceData;
}

void TextureAsset::EnsureUploaded(const TextureFactory& factory) {
	if (m_hasUploadedFinalImage) {
		return;
	}

	std::shared_ptr<TextureSourceData> sourceData;
	if (TextureProcessingManager::GetInstance().ShouldProcess(m_meta)) {
		sourceData = BuildSourceData();
		if (!TextureProcessingManager::GetInstance().NeedsProcessing(*sourceData, m_meta)) {
			m_desc = sourceData->desc;
			m_image = factory.CreateAlwaysResidentPixelBuffer(
				sourceData->desc,
				TextureFactory::TextureInitialData::FromBytes(sourceData->subresources),
				m_name);
			m_hasUploadedFinalImage = true;
			m_hasUploadedPlaceholder = false;
			if (!m_initialDataString.empty()) {
				m_initialStorage = m_initialDataString;
			}
			else {
				m_initialStorage = std::monostate{};
			}
			return;
		}
	}

	if (TextureProcessingManager::GetInstance().ShouldProcess(m_meta)) {
		if (!m_processingHandle) {
			m_processingHandle = TextureProcessingManager::GetInstance().RequestProcessing(sourceData ? sourceData : BuildSourceData(), m_meta);
		}

		if (m_processingHandle) {
			const TextureProcessingJobState state = m_processingHandle->state.load(std::memory_order_acquire);
			if (state == TextureProcessingJobState::Ready) {
				std::shared_ptr<TextureSourceData> result;
				bool loadedFromCache = false;
				{
					std::scoped_lock lock(m_processingHandle->mutex);
					result = m_processingHandle->result;
					loadedFromCache = m_processingHandle->loadedFromCache;
				}

				if (result) {
					m_desc = result->desc;
					m_meta.isProcessingCacheArtifact = loadedFromCache;
					if (loadedFromCache) {
						m_meta.fileType = ImageFiletype::DDS;
						m_meta.loader = ImageLoader::DirectXTex;
					}
					m_image = factory.CreateAlwaysResidentPixelBuffer(
						result->desc,
						TextureFactory::TextureInitialData::FromBytes(result->subresources),
						m_name);
					m_hasUploadedFinalImage = true;
					m_hasUploadedPlaceholder = false;
					if (!m_initialDataString.empty()) {
						m_initialStorage = m_initialDataString;
					}
					else {
						m_initialStorage = std::monostate{};
					}
					return;
				}
			}
			else if (state == TextureProcessingJobState::Failed) {
				m_meta.isProcessingCacheArtifact = false;
				m_image = factory.CreateAlwaysResidentPixelBuffer(
					m_desc,
					TextureFactory::TextureInitialData::FromBytes(ResolveToBytes()),
					m_name);
				m_hasUploadedFinalImage = true;
				m_hasUploadedPlaceholder = false;
				return;
			}
		}

		if (!m_image && m_meta.processing.allowAsyncPlaceholder) {
			m_image = CreatePlaceholderTexture(factory, m_meta.processing);
			if (!m_name.empty()) {
				m_image->SetName(m_name + "[placeholder]");
			}
			m_hasUploadedPlaceholder = true;
		}

		return;
	}

	if (!m_image) {
		m_image = factory.CreateAlwaysResidentPixelBuffer(
			m_desc,
			TextureFactory::TextureInitialData::FromBytes(ResolveToBytes()),
			m_name);
		m_hasUploadedFinalImage = true;
		if (!m_initialDataString.empty()) {
			m_initialStorage = m_initialDataString;
		}
		else {
			m_initialStorage = std::monostate{};
		}
	}
}