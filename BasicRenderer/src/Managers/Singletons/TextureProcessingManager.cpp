#include "Managers/Singletons/TextureProcessingManager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <boost/container_hash/hash.hpp>
#include <DirectXTex.h>
#include <spdlog/spdlog.h>

#include <rhi_dx12.h>
#include <rhi_helpers.h>

#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Utilities/CachePathUtilities.h"

using namespace DirectX;

namespace {
uint32_t CalcMipCount(uint32_t width, uint32_t height) {
	uint32_t levels = 1;
	while (width > 1 || height > 1) {
		width = (std::max)(1u, width >> 1);
		height = (std::max)(1u, height >> 1);
		++levels;
	}
	return levels;
}

std::string TextureSemanticToString(TextureSemantic semantic) {
	switch (semantic) {
	case TextureSemantic::BaseColor: return "BaseColor";
	case TextureSemantic::Emissive: return "Emissive";
	case TextureSemantic::Normal: return "Normal";
	case TextureSemantic::Height: return "Height";
	case TextureSemantic::AO: return "AO";
	case TextureSemantic::Opacity: return "Opacity";
	case TextureSemantic::Metallic: return "Metallic";
	case TextureSemantic::Roughness: return "Roughness";
	case TextureSemantic::MetallicRoughness: return "MetallicRoughness";
	case TextureSemantic::OpenPBRColor: return "OpenPBRColor";
	case TextureSemantic::OpenPBRScalar: return "OpenPBRScalar";
	default: return "Unknown";
	}
}

bool NeedsNormalConventionConversion(const TextureFileMeta& meta) {
	return meta.processing.semantic == TextureSemantic::Normal &&
		meta.processing.normalConvention == NormalMapConvention::OpenGL;
}

bool SupportsNormalGreenFlip(DXGI_FORMAT format) {
	switch (format) {
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return true;
	default:
		return false;
	}
}

void FlipNormalGreenChannel(ScratchImage& image) {
	const TexMetadata metadata = image.GetMetadata();
	const DXGI_FORMAT format = metadata.format;
	if (!SupportsNormalGreenFlip(format)) {
		throw std::runtime_error("TextureProcessingManager: unsupported normal map format for green-channel conversion");
	}

	const size_t bytesPerPixel = format == DXGI_FORMAT_R8G8_UNORM ? 2u : 4u;
	const size_t imageCount = image.GetImageCount();
	for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
		const Image* imageView = image.GetImage(imageIndex, 0, 0);
		if (!imageView || !imageView->pixels) {
			throw std::runtime_error("TextureProcessingManager: invalid normal map image during green-channel conversion");
		}

		uint8_t* pixels = const_cast<uint8_t*>(imageView->pixels);
		for (size_t y = 0; y < imageView->height; ++y) {
			uint8_t* row = pixels + (y * imageView->rowPitch);
			for (size_t x = 0; x < imageView->width; ++x) {
				row[x * bytesPerPixel + 1] = static_cast<uint8_t>(255u - row[x * bytesPerPixel + 1]);
			}
		}
	}
}

std::string ResolveProcessingIdentity(const TextureFileMeta& meta) {
	std::string normalizedIdentity = meta.processing.sourceIdentity.empty()
		? NormalizeCacheSourcePath(meta.filePath)
		: NormalizeCacheSourcePath(meta.processing.sourceIdentity);
	if (normalizedIdentity.empty()) {
		normalizedIdentity = meta.processing.sourceIdentity.empty() ? meta.filePath : meta.processing.sourceIdentity;
	}
	return normalizedIdentity;
}

std::string TryGetSourceVersionTag(const TextureFileMeta& meta) {
	for (const std::string* candidate : { &meta.filePath, &meta.processing.sourceIdentity }) {
		if (!candidate || candidate->empty()) {
			continue;
		}

		std::error_code ec;
		const std::filesystem::path path(*candidate);
		if (!std::filesystem::exists(path, ec) || ec) {
			continue;
		}

		auto lastWriteTime = std::filesystem::last_write_time(path, ec);
		if (ec) {
			continue;
		}

		return std::to_string(lastWriteTime.time_since_epoch().count());
	}

	return {};
}

std::wstring BuildProcessingCachePath(const std::string& key) {
	size_t seed = 0;
	boost::hash_combine(seed, key);

	std::ostringstream fileName;
	fileName << "processed_"
		<< std::hex
		<< std::setw(static_cast<int>(sizeof(size_t) * 2))
		<< std::setfill('0')
		<< seed
		<< ".dds";
	return GetCacheFilePath(s2ws(fileName.str()), L"textures");
}

DXGI_FORMAT ChooseWorkingFormat(const TextureFileMeta& meta) {
	switch (meta.processing.semantic) {
	case TextureSemantic::Normal:
		return DXGI_FORMAT_R8G8_UNORM;
	case TextureSemantic::Height:
	case TextureSemantic::AO:
	case TextureSemantic::Opacity:
	case TextureSemantic::Metallic:
	case TextureSemantic::Roughness:
	case TextureSemantic::OpenPBRScalar:
		return DXGI_FORMAT_R8_UNORM;
	default:
		return meta.preferSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}

DXGI_FORMAT ChooseCompressedFormat(const TextureSourceData& sourceData, const TextureFileMeta& meta) {
	if (!meta.processing.requestBlockCompression) {
		return rhi::ToDxgi(sourceData.desc.format);
	}

	switch (meta.processing.semantic) {
	case TextureSemantic::BaseColor:
	case TextureSemantic::Emissive:
	case TextureSemantic::OpenPBRColor:
		return meta.preferSRGB ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
	case TextureSemantic::Normal:
		return DXGI_FORMAT_BC5_UNORM;
	case TextureSemantic::AO:
	case TextureSemantic::Opacity:
	case TextureSemantic::Metallic:
	case TextureSemantic::Roughness:
	case TextureSemantic::OpenPBRScalar:
		return meta.processing.preservePackedChannels ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC4_UNORM;
	case TextureSemantic::MetallicRoughness:
		return DXGI_FORMAT_BC7_UNORM;
	default:
		return meta.preferSRGB ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
	}
}

HRESULT InitializeScratchImageFromSource(const TextureSourceData& sourceData, ScratchImage& scratchImage) {
	const DXGI_FORMAT format = rhi::ToDxgi(sourceData.desc.format);
	if (sourceData.desc.imageDimensions.empty()) {
		return E_INVALIDARG;
	}

	const uint32_t baseWidth = sourceData.desc.imageDimensions[0].width;
	const uint32_t baseHeight = sourceData.desc.imageDimensions[0].height;
	const uint32_t faces = sourceData.desc.isCubemap ? 6u : 1u;
	const uint32_t arraySize = (std::max)(1u, sourceData.desc.arraySize) * faces;
	const uint32_t mipLevels = sourceData.hasFullMipChain ? CalcMipCount(baseWidth, baseHeight) : 1u;

	HRESULT hr = scratchImage.Initialize2D(format, baseWidth, baseHeight, arraySize, mipLevels);
	if (FAILED(hr)) {
		return hr;
	}

	const size_t expectedImages = static_cast<size_t>(arraySize) * mipLevels;
	if (sourceData.subresources.size() != expectedImages || sourceData.desc.imageDimensions.size() != expectedImages) {
		return E_INVALIDARG;
	}

	size_t imageIndex = 0;
	for (uint32_t item = 0; item < arraySize; ++item) {
		for (uint32_t mip = 0; mip < mipLevels; ++mip, ++imageIndex) {
			const Image* dstImage = scratchImage.GetImage(mip, item, 0);
			if (!dstImage || !dstImage->pixels) {
				return E_FAIL;
			}

			const auto& bytes = sourceData.subresources[imageIndex];
			const auto& dims = sourceData.desc.imageDimensions[imageIndex];
			if (!bytes || bytes->size() < dims.slicePitch) {
				return E_INVALIDARG;
			}

			std::memcpy(const_cast<uint8_t*>(dstImage->pixels), bytes->data(), static_cast<size_t>(dims.slicePitch));
		}
	}

	return S_OK;
}

std::shared_ptr<TextureSourceData> BuildSourceDataFromScratchImage(const ScratchImage& image) {
	const TexMetadata metadata = image.GetMetadata();
	auto result = std::make_shared<TextureSourceData>();
	result->desc.format = rhi::helpers::ToRHI(metadata.format);
	result->desc.channels = static_cast<unsigned short>(rhi::helpers::FormatChannelCount(result->desc.format));
	result->desc.isCubemap = metadata.IsCubemap();
	result->desc.isArray = metadata.arraySize > 1 && !result->desc.isCubemap;
	result->desc.arraySize = result->desc.isCubemap
		? static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize / size_t(6)))
		: static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize));
	result->desc.generateMipMaps = false;

	const Image* images = image.GetImages();
	const size_t imageCount = image.GetImageCount();
	result->desc.imageDimensions.reserve(imageCount);
	result->subresources.reserve(imageCount);

	for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
		const Image& src = images[imageIndex];
		if (src.width > std::numeric_limits<uint32_t>::max() || src.height > std::numeric_limits<uint32_t>::max()) {
			throw std::runtime_error("Texture dimensions exceed uint32_t range");
		}

		ImageDimensions dims{};
		dims.width = static_cast<uint32_t>(src.width);
		dims.height = static_cast<uint32_t>(src.height);
		dims.rowPitch = src.rowPitch;
		dims.slicePitch = src.slicePitch;
		result->desc.imageDimensions.push_back(dims);

		const auto* first = reinterpret_cast<const uint8_t*>(src.pixels);
		auto bytes = std::make_shared<std::vector<uint8_t>>(first, first + src.slicePitch);
		result->subresources.push_back(std::move(bytes));
	}

	result->isBlockCompressed = rhi::helpers::IsBlockCompressed(result->desc.format);
	result->hasFullMipChain = metadata.mipLevels == CalcMipCount(
		result->desc.imageDimensions[0].width,
		result->desc.imageDimensions[0].height);
	return result;
}

std::shared_ptr<TextureSourceData> TryLoadTextureSourceDataFromCache(const std::string& key) {
	const std::wstring cachePath = BuildProcessingCachePath(key);
	std::error_code ec;
	if (!std::filesystem::exists(std::filesystem::path(cachePath), ec) || ec) {
		return {};
	}

	ScratchImage cachedImage;
	TexMetadata cachedMetadata{};
	const HRESULT hr = LoadFromDDSFile(cachePath.c_str(), DDS_FLAGS_NONE, &cachedMetadata, cachedImage);
	if (FAILED(hr)) {
		spdlog::warn("TextureProcessingManager: failed to load cache file '{}'", ws2s(cachePath));
		return {};
	}

	try {
		return BuildSourceDataFromScratchImage(cachedImage);
	}
	catch (const std::exception& ex) {
		spdlog::warn("TextureProcessingManager: failed to decode cache file '{}': {}", ws2s(cachePath), ex.what());
		return {};
	}
}

void TryWriteTextureSourceDataToCache(const std::string& key, const TextureSourceData& sourceData) {
	const std::wstring cachePath = BuildProcessingCachePath(key);
	ScratchImage cachedImage;
	const HRESULT initHr = InitializeScratchImageFromSource(sourceData, cachedImage);
	if (FAILED(initHr)) {
		spdlog::warn("TextureProcessingManager: failed to initialize cache image for '{}'", ws2s(cachePath));
		return;
	}

	const HRESULT writeHr = SaveToDDSFile(
		cachedImage.GetImages(),
		cachedImage.GetImageCount(),
		cachedImage.GetMetadata(),
		DDS_FLAGS_NONE,
		cachePath.c_str());
	if (FAILED(writeHr)) {
		spdlog::warn("TextureProcessingManager: failed to write cache file '{}'", ws2s(cachePath));
	}
}

std::shared_ptr<TextureSourceData> ProcessTextureSourceData(
	const std::shared_ptr<TextureSourceData>& sourceData,
	const TextureFileMeta& meta)
{
	if (!sourceData) {
		throw std::runtime_error("TextureProcessingManager: source data is null");
	}

	const bool needMipChain = meta.processing.requestMipChain && !sourceData->hasFullMipChain;
	const bool needCompression = meta.processing.requestBlockCompression && !sourceData->isBlockCompressed;
	const bool needDecompression = !meta.processing.requestBlockCompression && sourceData->isBlockCompressed;
	const bool needNormalConventionConversion = NeedsNormalConventionConversion(meta);

	if (!needMipChain && !needCompression && !needDecompression && !needNormalConventionConversion) {
		return sourceData;
	}

	ScratchImage workingImage;
	HRESULT hr = InitializeScratchImageFromSource(*sourceData, workingImage);
	if (FAILED(hr)) {
		throw std::runtime_error("TextureProcessingManager: failed to initialize working scratch image");
	}

	ScratchImage linearImage;
	if (sourceData->isBlockCompressed) {
		hr = Decompress(
			workingImage.GetImages(),
			workingImage.GetImageCount(),
			workingImage.GetMetadata(),
			ChooseWorkingFormat(meta),
			linearImage);
		if (FAILED(hr)) {
			throw std::runtime_error("TextureProcessingManager: DirectXTex decompress failed");
		}
	}
	else {
		linearImage = std::move(workingImage);
	}

	if (needNormalConventionConversion) {
		if (linearImage.GetMetadata().format != ChooseWorkingFormat(meta)) {
			ScratchImage convertedNormalImage;
			hr = Convert(
				linearImage.GetImages(),
				linearImage.GetImageCount(),
				linearImage.GetMetadata(),
				ChooseWorkingFormat(meta),
				TEX_FILTER_DEFAULT,
				TEX_THRESHOLD_DEFAULT,
				convertedNormalImage);
			if (FAILED(hr)) {
				throw std::runtime_error("TextureProcessingManager: DirectXTex normal conversion failed");
			}
			linearImage = std::move(convertedNormalImage);
		}

		FlipNormalGreenChannel(linearImage);
	}

	ScratchImage mipChainImage;
	ScratchImage* currentImage = &linearImage;
	if (needMipChain) {
		hr = GenerateMipMaps(
			linearImage.GetImages(),
			linearImage.GetImageCount(),
			linearImage.GetMetadata(),
			TEX_FILTER_DEFAULT,
			0,
			mipChainImage);
		if (FAILED(hr)) {
			throw std::runtime_error("TextureProcessingManager: DirectXTex GenerateMipMaps failed");
		}
		currentImage = &mipChainImage;
	}

	if (!meta.processing.requestBlockCompression) {
		return BuildSourceDataFromScratchImage(*currentImage);
	}

	ScratchImage compressedImage;
	TEX_COMPRESS_FLAGS flags = TEX_COMPRESS_DEFAULT;
	if (meta.processing.semantic != TextureSemantic::BaseColor &&
		meta.processing.semantic != TextureSemantic::Emissive &&
		meta.processing.semantic != TextureSemantic::OpenPBRColor)
	{
		flags = static_cast<TEX_COMPRESS_FLAGS>(flags | TEX_COMPRESS_UNIFORM);
	}
	flags = static_cast<TEX_COMPRESS_FLAGS>(flags | TEX_COMPRESS_PARALLEL);

	hr = Compress(
		currentImage->GetImages(),
		currentImage->GetImageCount(),
		currentImage->GetMetadata(),
		ChooseCompressedFormat(*sourceData, meta),
		flags,
		TEX_THRESHOLD_DEFAULT,
		compressedImage);
	if (FAILED(hr)) {
		throw std::runtime_error("TextureProcessingManager: DirectXTex Compress failed");
	}

	return BuildSourceDataFromScratchImage(compressedImage);
}
}

TextureProcessingManager& TextureProcessingManager::GetInstance() {
	static TextureProcessingManager instance;
	return instance;
}

bool TextureProcessingManager::ShouldProcess(const TextureFileMeta& meta) const {
	return meta.processing.isParticipatingMaterialTexture &&
		(meta.processing.requestMipChain || meta.processing.requestBlockCompression);
}

bool TextureProcessingManager::NeedsProcessing(const TextureSourceData& sourceData, const TextureFileMeta& meta) const {
	if (!ShouldProcess(meta)) {
		return false;
	}

	const bool needMipChain = meta.processing.requestMipChain && !sourceData.hasFullMipChain;
	const bool needCompression = meta.processing.requestBlockCompression && !sourceData.isBlockCompressed;
	const bool needDecompression = !meta.processing.requestBlockCompression && sourceData.isBlockCompressed;
	const bool needNormalConventionConversion = NeedsNormalConventionConversion(meta);
	return needMipChain || needCompression || needDecompression || needNormalConventionConversion;
}

std::string TextureProcessingManager::BuildProcessingKey(
	const std::shared_ptr<TextureSourceData>& sourceData,
	const TextureFileMeta& meta) const
{
	const std::string normalizedIdentity = ResolveProcessingIdentity(meta);
	const std::string sourceVersionTag = TryGetSourceVersionTag(meta);

	size_t seed = 0;
	boost::hash_combine(seed, normalizedIdentity);
	boost::hash_combine(seed, sourceVersionTag);
	boost::hash_combine(seed, static_cast<uint32_t>(meta.processing.semantic));
	boost::hash_combine(seed, meta.processing.requestMipChain);
	boost::hash_combine(seed, meta.processing.requestBlockCompression);
	boost::hash_combine(seed, meta.processing.preferSRGB);
	boost::hash_combine(seed, meta.processing.preservePackedChannels);
	boost::hash_combine(seed, static_cast<uint32_t>(meta.processing.normalConvention));
	boost::hash_combine(seed, static_cast<uint32_t>(sourceData ? sourceData->desc.format : rhi::Format::Unknown));
	boost::hash_combine(seed, static_cast<uint32_t>(sourceData ? sourceData->desc.arraySize : 0u));
	boost::hash_combine(seed, sourceData ? sourceData->hasFullMipChain : false);
	boost::hash_combine(seed, sourceData ? sourceData->isBlockCompressed : false);
	boost::hash_combine(seed, sourceData ? sourceData->subresources.size() : size_t{ 0 });
	if (sourceData && !sourceData->desc.imageDimensions.empty()) {
		boost::hash_combine(seed, sourceData->desc.imageDimensions[0].width);
		boost::hash_combine(seed, sourceData->desc.imageDimensions[0].height);
	}
	return normalizedIdentity + "#" + TextureSemanticToString(meta.processing.semantic) + "#" + std::to_string(seed);
}

std::shared_ptr<TextureProcessingJobHandle> TextureProcessingManager::RequestProcessing(
	const std::shared_ptr<TextureSourceData>& sourceData,
	const TextureFileMeta& meta)
{
	if (!ShouldProcess(meta) || !sourceData) {
		return {};
	}

	const std::string key = BuildProcessingKey(sourceData, meta);
	{
		std::scoped_lock lock(m_mutex);
		auto existing = m_jobsByKey.find(key);
		if (existing != m_jobsByKey.end()) {
			if (auto handle = existing->second.lock()) {
				return handle;
			}
			m_jobsByKey.erase(existing);
		}
	}

	auto handle = std::make_shared<TextureProcessingJobHandle>();
	handle->state.store(TextureProcessingJobState::Queued, std::memory_order_release);

	{
		std::scoped_lock lock(m_mutex);
		m_jobsByKey[key] = handle;
	}

	TaskSchedulerManager::GetInstance().RunBackgroundTask("TextureProcessingManager::RequestProcessing", [handle, sourceData, meta, key]() {
		handle->state.store(TextureProcessingJobState::Processing, std::memory_order_release);
		try {
			if (auto cachedResult = TryLoadTextureSourceDataFromCache(key)) {
				{
					std::scoped_lock lock(handle->mutex);
					handle->result = std::move(cachedResult);
					handle->loadedFromCache = true;
					handle->error.clear();
				}
				handle->state.store(TextureProcessingJobState::Ready, std::memory_order_release);
				spdlog::info(
					"TextureProcessingManager: cache hit for '{}' semantic={} bc={} mips={}",
					key,
					TextureSemanticToString(meta.processing.semantic),
					meta.processing.requestBlockCompression,
					meta.processing.requestMipChain);
				return;
			}

			auto result = ProcessTextureSourceData(sourceData, meta);
			if (result) {
				TryWriteTextureSourceDataToCache(key, *result);
			}
			{
				std::scoped_lock lock(handle->mutex);
				handle->result = std::move(result);
				handle->loadedFromCache = false;
				handle->error.clear();
			}
			handle->state.store(TextureProcessingJobState::Ready, std::memory_order_release);
			spdlog::info(
				"TextureProcessingManager: processed texture '{}' semantic={} bc={} mips={}",
				key,
				TextureSemanticToString(meta.processing.semantic),
				meta.processing.requestBlockCompression,
				meta.processing.requestMipChain);
		}
		catch (const std::exception& ex) {
			{
				std::scoped_lock lock(handle->mutex);
				handle->error = ex.what();
			}
			handle->state.store(TextureProcessingJobState::Failed, std::memory_order_release);
			spdlog::error("TextureProcessingManager: processing failed for '{}': {}", key, ex.what());
		}
	});

	return handle;
}