#include "Resources/Texture.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <DirectXTex.h>

#include <spdlog/spdlog.h>

#include <rhi_dx12.h>
#include <rhi_helpers.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/DirectStorageManager.h"
#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Managers/Singletons/TextureProcessingManager.h"
#include "Utilities/ProcessedTextureCache.h"

namespace {
const char* ToString(TextureSemantic semantic) {
	switch (semantic) {
	case TextureSemantic::BaseColor:
		return "BaseColor";
	case TextureSemantic::Emissive:
		return "Emissive";
	case TextureSemantic::Normal:
		return "Normal";
	case TextureSemantic::Height:
		return "Height";
	case TextureSemantic::AO:
		return "AO";
	case TextureSemantic::Opacity:
		return "Opacity";
	case TextureSemantic::Metallic:
		return "Metallic";
	case TextureSemantic::Roughness:
		return "Roughness";
	case TextureSemantic::MetallicRoughness:
		return "MetallicRoughness";
	case TextureSemantic::OpenPBRColor:
		return "OpenPBRColor";
	case TextureSemantic::OpenPBRScalar:
		return "OpenPBRScalar";
	default:
		return "Unknown";
	}
}

const char* ToString(TextureLoadPathTelemetry path) {
	switch (path) {
	case TextureLoadPathTelemetry::DirectStorageGpuDirect:
		return "directstorage_gpu_direct";
	case TextureLoadPathTelemetry::DirectStorageSystemMemoryRead:
		return "directstorage_system_memory_read";
	case TextureLoadPathTelemetry::CpuFileRead:
		return "cpu_file_read";
	case TextureLoadPathTelemetry::InMemoryContainer:
		return "in_memory_container";
	default:
		return "unknown";
	}
}

const char* ToString(TextureUploadPathTelemetry path) {
	switch (path) {
	case TextureUploadPathTelemetry::DirectStorageGpuDirect:
		return "directstorage_gpu_direct";
	case TextureUploadPathTelemetry::CpuImmediateUpload:
		return "cpu_immediate_upload";
	case TextureUploadPathTelemetry::AsyncProcessingPlaceholder:
		return "async_processing_placeholder";
	case TextureUploadPathTelemetry::AsyncProcessingReadyUpload:
		return "async_processing_ready_upload";
	case TextureUploadPathTelemetry::ProcessingCacheUpload:
		return "processing_cache_upload";
	case TextureUploadPathTelemetry::ProcessingFailedFallback:
		return "processing_failed_fallback";
	default:
		return "unknown";
	}
}

std::string TextureTelemetryLabel(const TextureAsset& texture) {
	if (!texture.Meta().filePath.empty()) {
		return texture.Meta().filePath;
	}
	if (!texture.Meta().processing.sourceIdentity.empty()) {
		return texture.Meta().processing.sourceIdentity + "|semantic:" + ToString(texture.Meta().processing.semantic);
	}
	return texture.GetWidth() && texture.GetHeight()
		? std::to_string(texture.GetWidth()) + "x" + std::to_string(texture.GetHeight())
		: std::string("unnamed_texture");
}

uint32_t CalcFullMipCount(uint32_t width, uint32_t height) {
	uint32_t levels = 1;
	while (width > 1 || height > 1) {
		width = (std::max)(1u, width >> 1);
		height = (std::max)(1u, height >> 1);
		++levels;
	}
	return levels;
}

uint32_t CalcMipCountFromDescription(const TextureDescription& desc) {
	if (desc.imageDimensions.empty()) {
		return 1u;
	}

	const uint32_t faces = desc.isCubemap ? 6u : 1u;
	const uint32_t slices = faces * (std::max)(1u, desc.arraySize);
	if (slices == 0u) {
		return 1u;
	}

	return static_cast<uint32_t>((std::max)(size_t(1), desc.imageDimensions.size() / slices));
}

bool IsDDSFilePath(const std::string& path) {
	if (path.empty()) {
		return false;
	}

	std::wstring extension = std::filesystem::path(path).extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
	return extension == L".dds";
}

bool IsConditionedCacheFilePath(const std::string& path) {
	if (path.empty()) {
		return false;
	}

	std::wstring extension = std::filesystem::path(path).extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
	return br::processed_texture_cache::IsConditionedCacheExtension(extension);
}

bool ReadProcessedTextureCacheHeader(const std::wstring& filePath, br::processed_texture_cache::FileHeader& header, std::string* outError = nullptr) {
	if (outError) {
		outError->clear();
	}

	std::ifstream file(filePath, std::ios::binary);
	if (!file) {
		if (outError) {
			*outError = "failed to open conditioned texture cache file";
		}
		return false;
	}

	file.read(reinterpret_cast<char*>(&header), sizeof(header));
	if (!file || static_cast<size_t>(file.gcount()) != sizeof(header)) {
		if (outError) {
			*outError = "failed to read conditioned texture cache header";
		}
		return false;
	}

	if (header.magic != br::processed_texture_cache::kMagic ||
		header.version != br::processed_texture_cache::kVersion ||
		header.headerSize != sizeof(header) ||
		header.dataOffset < sizeof(header) ||
		header.dataSizeBytes == 0) {
		if (outError) {
			*outError = "conditioned texture cache header is invalid";
		}
		return false;
	}

	return true;
}

bool BuildProcessedTextureCacheLayouts(
	const br::processed_texture_cache::FileHeader& header,
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts,
	std::vector<UINT>& numRows,
	UINT64& totalBytes,
	std::string* outError = nullptr)
{
	if (outError) {
		outError->clear();
	}

	if (header.baseWidth == 0 || header.baseHeight == 0 || header.mipLevels == 0 ||
		header.totalArraySlices == 0 || header.subresourceCount == 0 ||
		header.subresourceCount != header.totalArraySlices * header.mipLevels) {
		if (outError) {
			*outError = "conditioned texture cache header has inconsistent dimensions";
		}
		return false;
	}

	auto* nativeDevice = rhi::dx12::get_device(DeviceManager::GetInstance().GetDevice());
	if (nativeDevice == nullptr) {
		if (outError) {
			*outError = "failed to get native D3D12 device for conditioned texture cache layout";
		}
		return false;
	}

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = header.baseWidth;
	resourceDesc.Height = header.baseHeight;
	resourceDesc.DepthOrArraySize = static_cast<uint16_t>(header.totalArraySlices);
	resourceDesc.MipLevels = static_cast<uint16_t>(header.mipLevels);
	resourceDesc.Format = rhi::ToDxgi(static_cast<rhi::Format>(header.format));
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	layouts.resize(header.subresourceCount);
	numRows.resize(header.subresourceCount);
	std::vector<UINT64> rowSizes(header.subresourceCount);
	totalBytes = 0;
	nativeDevice->GetCopyableFootprints(
		&resourceDesc,
		0,
		header.subresourceCount,
		0,
		layouts.data(),
		numRows.data(),
		rowSizes.data(),
		&totalBytes);

	if (totalBytes == 0 || totalBytes > header.dataSizeBytes) {
		if (outError) {
			*outError = "conditioned texture cache payload size did not match computed D3D12 copyable footprints";
		}
		return false;
	}

	return true;
}

std::shared_ptr<TextureSourceData> BuildSourceDataFromConditionedCacheFilePath(const std::string& path) {
	const std::wstring widePath = std::filesystem::path(path).wstring();
	br::processed_texture_cache::FileHeader header{};
	std::string error;
	if (!ReadProcessedTextureCacheHeader(widePath, header, &error)) {
		throw std::runtime_error(error.empty() ? "failed to read conditioned texture cache header" : error);
	}

	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
	std::vector<UINT> numRows;
	UINT64 totalBytes = 0;
	if (!BuildProcessedTextureCacheLayouts(header, layouts, numRows, totalBytes, &error)) {
		throw std::runtime_error(error.empty() ? "failed to compute conditioned texture cache layout" : error);
	}

	std::ifstream file(widePath, std::ios::binary);
	if (!file) {
		throw std::runtime_error("failed to open conditioned texture cache payload");
	}
	file.seekg(static_cast<std::streamoff>(header.dataOffset), std::ios::beg);
	std::vector<uint8_t> payload(static_cast<size_t>(header.dataSizeBytes), 0u);
	file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
	if (!file || static_cast<size_t>(file.gcount()) != payload.size()) {
		throw std::runtime_error("failed to read conditioned texture cache payload");
	}

	auto result = std::make_shared<TextureSourceData>();
	result->desc.format = static_cast<rhi::Format>(header.format);
	result->desc.channels = static_cast<unsigned short>(header.channels);
	result->desc.isCubemap = br::processed_texture_cache::HasFlag(header, br::processed_texture_cache::FlagIsCubemap);
	result->desc.isArray = br::processed_texture_cache::HasFlag(header, br::processed_texture_cache::FlagIsArray);
	result->desc.arraySize = (std::max)(1u, header.arraySize);
	result->desc.generateMipMaps = false;
	result->desc.imageDimensions.reserve(header.subresourceCount);
	result->subresources.reserve(header.subresourceCount);
	const DXGI_FORMAT dxgiFormat = rhi::ToDxgi(result->desc.format);

	for (uint32_t subresourceIndex = 0; subresourceIndex < header.subresourceCount; ++subresourceIndex) {
		const auto& layout = layouts[subresourceIndex];
		const uint32_t mipIndex = subresourceIndex % header.mipLevels;
		const size_t mipWidth = (std::max)(size_t(1), static_cast<size_t>(header.baseWidth) >> mipIndex);
		const size_t mipHeight = (std::max)(size_t(1), static_cast<size_t>(header.baseHeight) >> mipIndex);
		size_t rowPitch = 0;
		size_t slicePitch = 0;
		if (FAILED(DirectX::ComputePitch(dxgiFormat, mipWidth, mipHeight, rowPitch, slicePitch))) {
			throw std::runtime_error("failed to compute conditioned texture cache subresource pitch");
		}

		const size_t offset = static_cast<size_t>(layout.Offset);
		const size_t sourceRowPitch = static_cast<size_t>(layout.Footprint.RowPitch);
		const size_t rowCount = static_cast<size_t>(numRows[subresourceIndex]);
		const size_t sourceSpan = rowCount == 0u
			? 0u
			: sourceRowPitch * (rowCount - 1u) + rowPitch;
		if (offset > payload.size() || sourceSpan > payload.size() - offset) {
			throw std::runtime_error("conditioned texture cache payload ended before expected subresource data");
		}

		ImageDimensions dims{};
		dims.width = static_cast<uint32_t>(mipWidth);
		dims.height = static_cast<uint32_t>(mipHeight);
		dims.rowPitch = rowPitch;
		dims.slicePitch = slicePitch;
		result->desc.imageDimensions.push_back(dims);

		auto bytes = std::make_shared<std::vector<uint8_t>>(slicePitch, 0u);
		const uint8_t* srcBase = payload.data() + offset;
		uint8_t* dstBase = bytes->data();
		for (size_t row = 0; row < rowCount; ++row) {
			std::memcpy(
				dstBase + row * rowPitch,
				srcBase + row * sourceRowPitch,
				rowPitch);
		}
		result->subresources.push_back(std::move(bytes));
	}

	result->hasFullMipChain = br::processed_texture_cache::HasFlag(header, br::processed_texture_cache::FlagHasFullMipChain);
	result->isBlockCompressed = br::processed_texture_cache::HasFlag(header, br::processed_texture_cache::FlagIsBlockCompressed);
	return result;
}

bool TryBuildConditionedCacheResidentUpload(
	const std::string& path,
	uint32_t topMip,
	bool allowRTV,
	bool allowUAV,
	TextureDescription& outDesc,
	DirectStorageTextureSubresourceRangeCopy& outRange,
	uint32_t& outClampedTopMip,
	std::string& outError)
{
	outError.clear();
	const std::wstring widePath = std::filesystem::path(path).wstring();
	br::processed_texture_cache::FileHeader header{};
	if (!ReadProcessedTextureCacheHeader(widePath, header, &outError)) {
		return false;
	}

	if (header.totalArraySlices != 1u) {
		outError = "conditioned texture cache partial DirectStorage reload currently supports only non-array 2D textures";
		return false;
	}

	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
	std::vector<UINT> numRows;
	UINT64 totalBytes = 0;
	if (!BuildProcessedTextureCacheLayouts(header, layouts, numRows, totalBytes, &outError)) {
		return false;
	}

	outClampedTopMip = (std::min)(topMip, header.mipLevels - 1u);
	const uint32_t residentMipCount = header.mipLevels - outClampedTopMip;
	const auto& firstLayout = layouts[outClampedTopMip];
	if (firstLayout.Offset > header.dataSizeBytes || header.dataSizeBytes - firstLayout.Offset > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
		outError = "conditioned texture cache resident range exceeded DirectStorage upload limits";
		return false;
	}

	outDesc = {};
	outDesc.format = static_cast<rhi::Format>(header.format);
	outDesc.channels = static_cast<unsigned short>(header.channels);
	outDesc.hasRTV = allowRTV;
	outDesc.hasUAV = allowUAV;
	outDesc.generateMipMaps = false;
	outDesc.imageDimensions.reserve(residentMipCount);
	for (uint32_t subresourceIndex = outClampedTopMip; subresourceIndex < header.mipLevels; ++subresourceIndex) {
		const size_t mipWidth = (std::max)(size_t(1), static_cast<size_t>(header.baseWidth) >> subresourceIndex);
		const size_t mipHeight = (std::max)(size_t(1), static_cast<size_t>(header.baseHeight) >> subresourceIndex);
		size_t rowPitch = 0;
		size_t slicePitch = 0;
		if (FAILED(DirectX::ComputePitch(rhi::ToDxgi(static_cast<rhi::Format>(header.format)), mipWidth, mipHeight, rowPitch, slicePitch))) {
			outError = "failed to compute conditioned texture cache resident pitch";
			return false;
		}

		ImageDimensions dims{};
		dims.width = static_cast<uint32_t>(mipWidth);
		dims.height = static_cast<uint32_t>(mipHeight);
		dims.rowPitch = rowPitch;
		dims.slicePitch = slicePitch;
		outDesc.imageDimensions.push_back(dims);
	}

	outRange = {};
	outRange.sourceOffset = header.dataOffset + firstLayout.Offset;
	outRange.sourceSizeBytes = static_cast<uint32_t>(header.dataSizeBytes - firstLayout.Offset);
	outRange.uncompressedSizeBytes = outRange.sourceSizeBytes;
	outRange.firstSubresource = 0u;
	outRange.subresourceCount = residentMipCount;

	return true;
}

uint32_t ComputeDefaultStreamingBootstrapTopMip(const TextureDescription& desc, uint32_t totalMipCount) {
	if (desc.imageDimensions.empty() || totalMipCount <= 1u) {
		return 0u;
	}

	static constexpr uint32_t kBootstrapMaxDimension = 1024u;
	uint32_t width = desc.imageDimensions[0].width;
	uint32_t height = desc.imageDimensions[0].height;
	uint32_t topMip = 0u;
	while (topMip + 1u < totalMipCount && (width > kBootstrapMaxDimension || height > kBootstrapMaxDimension)) {
		width = (std::max)(1u, width >> 1);
		height = (std::max)(1u, height >> 1);
		++topMip;
	}
	return topMip;
}

std::shared_ptr<TextureSourceData> ClipTextureSourceDataTopMip(
	const std::shared_ptr<TextureSourceData>& sourceData,
	uint32_t topMip)
{
	if (!sourceData) {
		return {};
	}

	const uint32_t fullMipCount = CalcMipCountFromDescription(sourceData->desc);
	if (topMip == 0u || fullMipCount <= 1u) {
		return sourceData;
	}

	const uint32_t clampedTopMip = (std::min)(topMip, fullMipCount - 1u);
	if (clampedTopMip == 0u) {
		return sourceData;
	}

	const uint32_t faces = sourceData->desc.isCubemap ? 6u : 1u;
	const uint32_t slices = faces * (std::max)(1u, sourceData->desc.arraySize);
	const uint32_t residentMipCount = fullMipCount - clampedTopMip;

	auto clipped = std::make_shared<TextureSourceData>();
	clipped->desc = sourceData->desc;
	clipped->desc.imageDimensions.clear();
	clipped->desc.imageDimensions.reserve(static_cast<size_t>(slices) * residentMipCount);
	clipped->subresources.reserve(static_cast<size_t>(slices) * residentMipCount);

	for (uint32_t slice = 0u; slice < slices; ++slice) {
		const size_t sliceBase = static_cast<size_t>(slice) * fullMipCount;
		for (uint32_t mip = clampedTopMip; mip < fullMipCount; ++mip) {
			const size_t index = sliceBase + mip;
			clipped->desc.imageDimensions.push_back(sourceData->desc.imageDimensions[index]);
			clipped->subresources.push_back(sourceData->subresources[index]);
		}
	}

	clipped->isBlockCompressed = sourceData->isBlockCompressed;
	clipped->hasFullMipChain = false;
	return clipped;
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
		TextureFactory::TextureInitialData::FromBytes({ bytes }));
}

std::shared_ptr<TextureSourceData> BuildSourceDataFromDDSFilePath(const std::string& path, bool preferSRGB) {
	DirectX::ScratchImage image;
	DirectX::TexMetadata metadata{};
	const std::wstring widePath = std::filesystem::path(path).wstring();
	const HRESULT hr = DirectX::LoadFromDDSFile(widePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to load DDS from file path: " + path);
	}

	auto result = std::make_shared<TextureSourceData>();
	result->desc.format = rhi::helpers::ToRHI(preferSRGB ? DirectX::MakeSRGB(metadata.format) : DirectX::MakeLinear(metadata.format));
	result->desc.channels = static_cast<unsigned short>(rhi::helpers::FormatChannelCount(result->desc.format));
	result->desc.isCubemap = metadata.IsCubemap();
	result->desc.isArray = metadata.arraySize > 1 && !result->desc.isCubemap;
	result->desc.arraySize = result->desc.isCubemap
		? static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize / size_t(6)))
		: static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize));

	result->desc.imageDimensions.reserve(image.GetImageCount());
	result->subresources.reserve(image.GetImageCount());

	const DirectX::Image* images = image.GetImages();
	if (!images || image.GetImageCount() == 0) {
		throw std::runtime_error("DDS file did not produce any images: " + path);
	}

	for (size_t imageIndex = 0; imageIndex < image.GetImageCount(); ++imageIndex) {
		const DirectX::Image& src = images[imageIndex];

		ImageDimensions dims{};
		dims.width = static_cast<uint32_t>(src.width);
		dims.height = static_cast<uint32_t>(src.height);
		dims.rowPitch = src.rowPitch;
		dims.slicePitch = src.slicePitch;
		result->desc.imageDimensions.push_back(dims);

		const auto* first = reinterpret_cast<const uint8_t*>(src.pixels);
		result->subresources.push_back(std::make_shared<std::vector<uint8_t>>(first, first + src.slicePitch));
	}

	result->isBlockCompressed = rhi::helpers::IsBlockCompressed(result->desc.format);
	result->hasFullMipChain = metadata.mipLevels == CalcFullMipCount(
		result->desc.imageDimensions[0].width,
		result->desc.imageDimensions[0].height);
	return result;
}

std::shared_ptr<TextureReloadJobHandle> RequestReloadSourceDataAsync(
	std::string filePath,
	bool preferSRGB,
	uint32_t targetTopMip,
	bool streamingEnabled)
{
	auto handle = std::make_shared<TextureReloadJobHandle>();
	handle->targetTopMip = targetTopMip;
	handle->state.store(TextureReloadJobState::Queued, std::memory_order_release);

	TaskSchedulerManager::GetInstance().RunBackgroundTask("TextureAsset::RequestReloadSourceDataAsync", [handle, filePath = std::move(filePath), preferSRGB, targetTopMip, streamingEnabled]() mutable {
		handle->state.store(TextureReloadJobState::BuildingSourceData, std::memory_order_release);
		try {
			auto sourceData = BuildSourceDataFromDDSFilePath(filePath, preferSRGB);
			const uint32_t fullMipCount = CalcMipCountFromDescription(sourceData->desc);
			const uint32_t clippedTopMip = (streamingEnabled && fullMipCount > 1u)
				? (std::min)(targetTopMip, fullMipCount - 1u)
				: 0u;
			auto reloadSourceData = ClipTextureSourceDataTopMip(sourceData, clippedTopMip);

			{
				std::scoped_lock lock(handle->mutex);
				handle->sourceData = std::move(reloadSourceData);
				handle->sourceTotalMipCount = fullMipCount;
				handle->sourceFullWidth = sourceData->desc.imageDimensions.empty() ? 0u : sourceData->desc.imageDimensions[0].width;
				handle->sourceFullHeight = sourceData->desc.imageDimensions.empty() ? 0u : sourceData->desc.imageDimensions[0].height;
				handle->error.clear();
			}

			handle->state.store(TextureReloadJobState::Ready, std::memory_order_release);
		}
		catch (const std::exception& ex) {
			{
				std::scoped_lock lock(handle->mutex);
				handle->error = ex.what();
			}
			handle->state.store(TextureReloadJobState::Failed, std::memory_order_release);
		}
	});

	return handle;
}

std::shared_ptr<PixelBuffer> TryUploadDDSFilePathDirectToVRAM(
	const std::string& path,
	bool preferSRGB,
	uint32_t topMip,
	bool allowRTV,
	bool allowUAV)
{
	if (path.empty() || !DirectStorageManager::GetInstance().CanServiceQueue(DirectStorageQueueKind::Gpu)) {
		return {};
	}

	const std::filesystem::path filePath(path);
	std::wstring extension = filePath.extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
	if (extension != L".dds") {
		return {};
	}

	DirectX::ScratchImage image;
	DirectX::TexMetadata metadata{};
	const HRESULT loadHr = DirectX::LoadFromDDSFile(filePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
	if (FAILED(loadHr) || metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.depth != 1) {
		return {};
	}

	size_t headerSize = 0;
	const HRESULT headerHr = DirectX::EncodeDDSHeader(
		metadata,
		DirectX::DDS_FLAGS_NONE,
		nullptr,
		(std::numeric_limits<size_t>::max)(),
		headerSize);
	if (FAILED(headerHr) || headerSize == 0) {
		return {};
	}

	TextureDescription desc{};
	desc.format = rhi::helpers::ToRHI(preferSRGB ? DirectX::MakeSRGB(metadata.format) : DirectX::MakeLinear(metadata.format));
	desc.channels = static_cast<unsigned short>(rhi::helpers::FormatChannelCount(desc.format));
	if (rhi::helpers::IsBlockCompressed(desc.format)) {
		return {};
	}

	desc.isCubemap = metadata.IsCubemap();
	desc.isArray = metadata.arraySize > 1 && !desc.isCubemap;
	desc.arraySize = desc.isCubemap
		? static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize / size_t(6)))
		: static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize));
	desc.hasRTV = allowRTV;
	desc.hasUAV = allowUAV;
	desc.generateMipMaps = false;

	const uint32_t fullMipCount = static_cast<uint32_t>((std::max)(size_t(1), metadata.mipLevels));
	const uint32_t clampedTopMip = (std::min)(topMip, fullMipCount - 1u);
	const uint32_t arraySlices = static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize));
	const DirectX::Image* images = image.GetImages();
	const size_t imageCount = image.GetImageCount();
	if (images == nullptr || imageCount != static_cast<size_t>(arraySlices) * fullMipCount) {
		return {};
	}

	const uint32_t residentMipCount = fullMipCount - clampedTopMip;
	desc.imageDimensions.reserve(static_cast<size_t>(arraySlices) * residentMipCount);
	std::vector<br::DirectStorageTextureRegionCopy> regions;
	regions.reserve(static_cast<size_t>(arraySlices) * residentMipCount);

	uint64_t currentOffset = static_cast<uint64_t>(headerSize);
	uint32_t destinationSubresourceIndex = 0u;
	for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
		const DirectX::Image& srcImage = images[imageIndex];
		if (srcImage.width > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) ||
			srcImage.height > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) ||
			srcImage.slicePitch == 0 ||
			srcImage.slicePitch > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
			return {};
		}

		const uint32_t mipIndex = static_cast<uint32_t>(imageIndex % fullMipCount);
		if (mipIndex >= clampedTopMip) {
			ImageDimensions dims{};
			dims.width = static_cast<uint32_t>(srcImage.width);
			dims.height = static_cast<uint32_t>(srcImage.height);
			dims.rowPitch = srcImage.rowPitch;
			dims.slicePitch = srcImage.slicePitch;
			desc.imageDimensions.push_back(dims);

			br::DirectStorageTextureRegionCopy region{};
			region.sourceOffset = currentOffset;
			region.sourceSizeBytes = static_cast<uint32_t>(srcImage.slicePitch);
			region.uncompressedSizeBytes = static_cast<uint32_t>(srcImage.slicePitch);
			region.subresourceIndex = destinationSubresourceIndex++;
			region.width = dims.width;
			region.height = dims.height;
			region.depth = 1;
			regions.push_back(region);
		}

		currentOffset += srcImage.slicePitch;
	}

	if (regions.empty()) {
		return {};
	}

	auto pixelBuffer = PixelBuffer::CreateShared(desc);
	std::string directStorageMessage;
	if (!DirectStorageManager::GetInstance().UploadTextureRegionsFromFile(filePath.wstring(), pixelBuffer->GetAPIResource(), regions, &directStorageMessage)) {
		if (!directStorageMessage.empty()) {
			spdlog::debug("TextureAsset: DirectStorage fallback for '{}' because {}", path, directStorageMessage);
		}
		return {};
	}

	return pixelBuffer;
}

std::shared_ptr<PixelBuffer> TryUploadConditionedCacheFilePathDirectToVRAM(
	const std::string& path,
	uint32_t topMip,
	bool allowRTV,
	bool allowUAV)
{
	if (path.empty() || !DirectStorageManager::GetInstance().CanServiceQueue(DirectStorageQueueKind::Gpu)) {
		return {};
	}

	TextureDescription desc{};
	DirectStorageTextureSubresourceRangeCopy range{};
	uint32_t clampedTopMip = 0u;
	std::string error;
	if (!TryBuildConditionedCacheResidentUpload(path, topMip, allowRTV, allowUAV, desc, range, clampedTopMip, error)) {
		if (!error.empty()) {
			spdlog::debug("TextureAsset: conditioned cache DirectStorage fallback for '{}' because {}", path, error);
		}
		return {};
	}

	auto pixelBuffer = PixelBuffer::CreateShared(desc);
	if (!pixelBuffer) {
		return {};
	}

	std::string directStorageMessage;
	if (!DirectStorageManager::GetInstance().UploadTextureSubresourceRangeFromFile(
			std::filesystem::path(path).wstring(),
			pixelBuffer->GetAPIResource(),
			range,
			&directStorageMessage)) {
		if (!directStorageMessage.empty()) {
			spdlog::debug("TextureAsset: conditioned cache DirectStorage fallback for '{}' because {}", path, directStorageMessage);
		}
		return {};
	}

	return pixelBuffer;
}

std::shared_ptr<TextureDirectStorageReloadJobHandle> BeginUploadDDSFilePathDirectToVRAMAsync(
	const std::string& path,
	bool preferSRGB,
	uint32_t topMip,
	bool allowRTV,
	bool allowUAV)
{
	if (path.empty() || !DirectStorageManager::GetInstance().CanServiceQueue(DirectStorageQueueKind::Gpu)) {
		return {};
	}

	auto handle = std::make_shared<TextureDirectStorageReloadJobHandle>();
	handle->state.store(TextureDirectStorageReloadJobState::Queued, std::memory_order_release);

	TaskSchedulerManager::GetInstance().RunBackgroundTask("TextureAsset::BeginUploadDDSFilePathDirectToVRAMAsync", [handle, path, preferSRGB, topMip, allowRTV, allowUAV]() mutable {
		handle->state.store(TextureDirectStorageReloadJobState::CreatingResource, std::memory_order_release);

		try {
			const std::filesystem::path filePath(path);
			std::wstring extension = filePath.extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
			if (extension != L".dds") {
				throw std::runtime_error("only DDS files support DirectStorage GPU-direct texture upload");
			}

			DirectX::ScratchImage image;
			DirectX::TexMetadata metadata{};
			const HRESULT loadHr = DirectX::LoadFromDDSFile(filePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
			if (FAILED(loadHr) || metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.depth != 1) {
				throw std::runtime_error("failed to load DDS metadata for DirectStorage GPU-direct texture upload");
			}

			size_t headerSize = 0;
			const HRESULT headerHr = DirectX::EncodeDDSHeader(
				metadata,
				DirectX::DDS_FLAGS_NONE,
				nullptr,
				(std::numeric_limits<size_t>::max)(),
				headerSize);
			if (FAILED(headerHr) || headerSize == 0) {
				throw std::runtime_error("failed to encode DDS header for DirectStorage GPU-direct texture upload");
			}

			TextureDescription desc{};
			desc.format = rhi::helpers::ToRHI(preferSRGB ? DirectX::MakeSRGB(metadata.format) : DirectX::MakeLinear(metadata.format));
			desc.channels = static_cast<unsigned short>(rhi::helpers::FormatChannelCount(desc.format));
			if (rhi::helpers::IsBlockCompressed(desc.format)) {
				throw std::runtime_error("block-compressed DDS textures do not use this DirectStorage GPU-direct path");
			}

			desc.isCubemap = metadata.IsCubemap();
			desc.isArray = metadata.arraySize > 1 && !desc.isCubemap;
			desc.arraySize = desc.isCubemap
				? static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize / size_t(6)))
				: static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize));
			desc.hasRTV = allowRTV;
			desc.hasUAV = allowUAV;
			desc.generateMipMaps = false;

			const uint32_t fullMipCount = static_cast<uint32_t>((std::max)(size_t(1), metadata.mipLevels));
			const uint32_t clampedTopMip = (std::min)(topMip, fullMipCount - 1u);
			const uint32_t arraySlices = static_cast<uint32_t>((std::max)(size_t(1), metadata.arraySize));
			const DirectX::Image* images = image.GetImages();
			const size_t imageCount = image.GetImageCount();
			if (images == nullptr || imageCount != static_cast<size_t>(arraySlices) * fullMipCount) {
				throw std::runtime_error("DDS image layout did not match expected subresource count for DirectStorage GPU-direct texture upload");
			}

			const uint32_t residentMipCount = fullMipCount - clampedTopMip;
			desc.imageDimensions.reserve(static_cast<size_t>(arraySlices) * residentMipCount);
			std::vector<br::DirectStorageTextureRegionCopy> regions;
			regions.reserve(static_cast<size_t>(arraySlices) * residentMipCount);

			uint64_t currentOffset = static_cast<uint64_t>(headerSize);
			uint32_t destinationSubresourceIndex = 0u;
			for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
				const DirectX::Image& srcImage = images[imageIndex];
				if (srcImage.width > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) ||
					srcImage.height > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) ||
					srcImage.slicePitch == 0 ||
					srcImage.slicePitch > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
					throw std::runtime_error("DDS image dimensions exceeded DirectStorage GPU-direct texture upload limits");
				}

				const uint32_t mipIndex = static_cast<uint32_t>(imageIndex % fullMipCount);
				if (mipIndex >= clampedTopMip) {
					ImageDimensions dims{};
					dims.width = static_cast<uint32_t>(srcImage.width);
					dims.height = static_cast<uint32_t>(srcImage.height);
					dims.rowPitch = srcImage.rowPitch;
					dims.slicePitch = srcImage.slicePitch;
					desc.imageDimensions.push_back(dims);

					br::DirectStorageTextureRegionCopy region{};
					region.sourceOffset = currentOffset;
					region.sourceSizeBytes = static_cast<uint32_t>(srcImage.slicePitch);
					region.uncompressedSizeBytes = static_cast<uint32_t>(srcImage.slicePitch);
					region.subresourceIndex = destinationSubresourceIndex++;
					region.width = dims.width;
					region.height = dims.height;
					region.depth = 1;
					regions.push_back(region);
				}

				currentOffset += srcImage.slicePitch;
			}

			if (regions.empty()) {
				throw std::runtime_error("no texture regions were produced for DirectStorage GPU-direct texture upload");
			}

			auto uploadedImage = PixelBuffer::CreateShared(desc);
			if (!uploadedImage) {
				throw std::runtime_error("failed to create resident PixelBuffer for DirectStorage GPU-direct texture upload");
			}

			std::string directStorageMessage;
			DirectStorageAsyncRequestHandle requestHandle = DirectStorageManager::GetInstance().EnqueueUploadTextureRegionsFromFile(
				filePath.wstring(),
				uploadedImage->GetAPIResource(),
				regions,
				&directStorageMessage);
			if (!requestHandle.IsValid()) {
				throw std::runtime_error(directStorageMessage.empty()
					? "failed to enqueue DirectStorage GPU-direct texture upload"
					: directStorageMessage);
			}

			{
				std::scoped_lock lock(handle->mutex);
				handle->targetTopMip = clampedTopMip;
				handle->uploadedImage = std::move(uploadedImage);
				handle->requestHandle = std::move(requestHandle);
				handle->error.clear();
			}

			handle->state.store(TextureDirectStorageReloadJobState::Uploading, std::memory_order_release);
		}
		catch (const std::exception& ex) {
			{
				std::scoped_lock lock(handle->mutex);
				handle->error = ex.what();
			}
			handle->state.store(TextureDirectStorageReloadJobState::Failed, std::memory_order_release);
		}
	});

	return handle;
}

std::shared_ptr<TextureDirectStorageReloadJobHandle> BeginUploadConditionedCacheFilePathDirectToVRAMAsync(
	const std::string& path,
	uint32_t topMip,
	bool allowRTV,
	bool allowUAV)
{
	if (path.empty() || !DirectStorageManager::GetInstance().CanServiceQueue(DirectStorageQueueKind::Gpu)) {
		return {};
	}

	auto handle = std::make_shared<TextureDirectStorageReloadJobHandle>();
	handle->state.store(TextureDirectStorageReloadJobState::Queued, std::memory_order_release);

	TaskSchedulerManager::GetInstance().RunBackgroundTask("TextureAsset::BeginUploadConditionedCacheFilePathDirectToVRAMAsync", [handle, path, topMip, allowRTV, allowUAV]() mutable {
		handle->state.store(TextureDirectStorageReloadJobState::CreatingResource, std::memory_order_release);

		try {
			TextureDescription desc{};
			DirectStorageTextureSubresourceRangeCopy range{};
			uint32_t clampedTopMip = 0u;
			std::string error;
			if (!TryBuildConditionedCacheResidentUpload(path, topMip, allowRTV, allowUAV, desc, range, clampedTopMip, error)) {
				throw std::runtime_error(error.empty() ? "failed to build conditioned texture cache resident upload" : error);
			}

			auto uploadedImage = PixelBuffer::CreateShared(desc);
			if (!uploadedImage) {
				throw std::runtime_error("failed to create resident PixelBuffer for conditioned texture cache DirectStorage upload");
			}

			std::string directStorageMessage;
			DirectStorageAsyncRequestHandle requestHandle = DirectStorageManager::GetInstance().EnqueueUploadTextureSubresourceRangeFromFile(
				std::filesystem::path(path).wstring(),
				uploadedImage->GetAPIResource(),
				range,
				&directStorageMessage);
			if (!requestHandle.IsValid()) {
				throw std::runtime_error(directStorageMessage.empty()
					? "failed to enqueue conditioned texture cache DirectStorage upload"
					: directStorageMessage);
			}

			{
				std::scoped_lock lock(handle->mutex);
				handle->targetTopMip = clampedTopMip;
				handle->uploadedImage = std::move(uploadedImage);
				handle->requestHandle = std::move(requestHandle);
				handle->error.clear();
			}

			handle->state.store(TextureDirectStorageReloadJobState::Uploading, std::memory_order_release);
		}
		catch (const std::exception& ex) {
			{
				std::scoped_lock lock(handle->mutex);
				handle->error = ex.what();
			}
			handle->state.store(TextureDirectStorageReloadJobState::Failed, std::memory_order_release);
		}
	});

	return handle;
}
}

uint32_t TextureAsset::NextStreamingTextureID() {
	static std::atomic<uint32_t> nextID{1u};
	return nextID.fetch_add(1u, std::memory_order_relaxed);
}

void TextureAsset::UpdateSourceShapeFromDescription(const TextureDescription& desc, uint32_t totalMipCountHint) {
	if (desc.imageDimensions.empty()) {
		return;
	}

	const uint32_t descMipCount = CalcMipCountFromDescription(desc);
	const uint32_t totalMipCount = (std::max)(descMipCount, totalMipCountHint);
	ApplySourceShapeHint(desc.imageDimensions[0].width, desc.imageDimensions[0].height, totalMipCount);
}

void TextureAsset::ApplySourceShapeHint(uint32_t fullWidth, uint32_t fullHeight, uint32_t totalMipCount) {
	if (fullWidth == 0u || fullHeight == 0u || totalMipCount == 0u) {
		return;
	}

	if (m_sourceFullWidth == 0u ||
		m_sourceFullHeight == 0u ||
		totalMipCount >= m_sourceTotalMipCount) {
		m_sourceFullWidth = fullWidth;
		m_sourceFullHeight = fullHeight;
	}

	m_sourceTotalMipCount = (std::max)(m_sourceTotalMipCount, totalMipCount);
}

void TextureAsset::RefreshStreamingStateFromDescription() {
	const bool wasEligible = m_streamingState.eligible;
	UpdateSourceShapeFromDescription(m_desc);
	const uint32_t descMipCount = CalcMipCountFromDescription(m_desc);
	const uint32_t totalMipCount = (std::max)(1u, m_sourceTotalMipCount);

	m_streamingState.eligible = m_meta.processing.isParticipatingMaterialTexture && totalMipCount > 1u && HasStreamingSourceData();
	if (!wasEligible && m_streamingState.eligible) {
		m_streamingState.enabled = true;
	}
	else {
		m_streamingState.enabled = m_streamingState.enabled && m_streamingState.eligible;
	}
	m_streamingState.residency.totalMipCount = totalMipCount;
	m_streamingState.residency.residentTopMip = (std::min)(m_streamingState.residency.residentTopMip, totalMipCount - 1u);
	const uint32_t maxResidentMipCount = totalMipCount - m_streamingState.residency.residentTopMip;
	m_streamingState.residency.residentMipCount = (std::max)(1u, (std::min)(descMipCount, maxResidentMipCount));
	m_streamingState.requestedTopMip = (std::min)(m_streamingState.requestedTopMip, totalMipCount - 1u);
	m_streamingState.pendingTopMip = (std::min)(m_streamingState.pendingTopMip, totalMipCount - 1u);
	if (!wasEligible && m_streamingState.enabled) {
		ApplyStreamingBootstrapTopMip();
	}
}

void TextureAsset::ApplyStreamingBootstrapTopMip() {
	if (!m_streamingState.eligible ||
		m_streamingState.requestedTopMip != 0u ||
		m_streamingState.pendingTopMip != 0u ||
		m_streamingState.lastSeenFrame != 0u) {
		return;
	}

	const uint32_t bootstrapTopMip = ComputeDefaultStreamingBootstrapTopMip(
		m_desc,
		m_streamingState.residency.totalMipCount);
	if (bootstrapTopMip == 0u) {
		return;
	}

	m_streamingState.requestedTopMip = bootstrapTopMip;
	m_streamingState.pendingTopMip = bootstrapTopMip;
}

bool TextureAsset::HasStreamingSourceData() const {
	return !m_initialDataString.empty() || !m_originalSourceBytes.empty();
}

uint32_t TextureAsset::GetDesiredResidentTopMip() const {
	return m_streamingState.enabled
		? (std::min)(m_streamingState.pendingTopMip, m_streamingState.residency.totalMipCount - 1u)
		: 0u;
}

void TextureAsset::InvalidateResidentImageForStreamingRequest() {
	if (!m_hasUploadedFinalImage || !HasStreamingSourceData()) {
		return;
	}

	if (m_streamingState.residency.residentTopMip == GetDesiredResidentTopMip()) {
		return;
	}

	m_image.reset();
	m_hasUploadedFinalImage = false;
	m_hasUploadedPlaceholder = false;
	BumpBindingRevision();
}

void TextureAsset::BumpStreamingStateRevision() {
	++m_streamingState.stateRevision;
}

void TextureAsset::BumpBindingRevision() {
	++m_streamingState.bindingRevision;
	BumpStreamingStateRevision();
}

std::shared_ptr<TextureSourceData> TextureAsset::BuildSourceData() {
	if (std::holds_alternative<std::string>(m_initialStorage)) {
		const auto& path = std::get<std::string>(m_initialStorage);
		auto sourceData = IsConditionedCacheFilePath(path)
			? BuildSourceDataFromConditionedCacheFilePath(path)
			: BuildSourceDataFromDDSFilePath(path, m_meta.preferSRGB);
		UpdateSourceShapeFromDescription(sourceData->desc, CalcMipCountFromDescription(sourceData->desc));
		if (!m_streamingState.enabled) {
			return sourceData;
		}

		const uint32_t fullMipCount = CalcMipCountFromDescription(sourceData->desc);
		if (fullMipCount <= 1u) {
			return sourceData;
		}

		const uint32_t targetTopMip = (std::min)(GetDesiredResidentTopMip(), fullMipCount - 1u);
		return ClipTextureSourceDataTopMip(sourceData, targetTopMip);
	}

	if (!m_originalSourceBytes.empty()) {
		auto sourceData = std::make_shared<TextureSourceData>();
		sourceData->desc = m_originalSourceDesc;
		sourceData->subresources = m_originalSourceBytes;
		sourceData->isBlockCompressed = rhi::helpers::IsBlockCompressed(m_originalSourceDesc.format);
		UpdateSourceShapeFromDescription(sourceData->desc);

		if (!sourceData->desc.imageDimensions.empty()) {
			const uint32_t faces = sourceData->desc.isCubemap ? 6u : 1u;
			const uint32_t slices = faces * (std::max)(1u, sourceData->desc.arraySize);
			const uint32_t fullMipCount = CalcFullMipCount(
				sourceData->desc.imageDimensions[0].width,
				sourceData->desc.imageDimensions[0].height);
			const size_t expectedSubresources = static_cast<size_t>(slices) * fullMipCount;

			sourceData->hasFullMipChain =
				!sourceData->subresources.empty() &&
				sourceData->subresources.size() == expectedSubresources &&
				sourceData->desc.imageDimensions.size() == expectedSubresources;

			if (m_streamingState.enabled && fullMipCount > 1u) {
				const uint32_t targetTopMip = (std::min)(GetDesiredResidentTopMip(), fullMipCount - 1u);
				return ClipTextureSourceDataTopMip(sourceData, targetTopMip);
			}
		}

		return sourceData;
	}

	auto sourceData = std::make_shared<TextureSourceData>();
	sourceData->desc = m_desc;
	sourceData->subresources = ResolveToBytes();
	sourceData->isBlockCompressed = rhi::helpers::IsBlockCompressed(m_desc.format);
	UpdateSourceShapeFromDescription(sourceData->desc);

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

std::shared_ptr<TextureSourceData> TextureAsset::BuildProcessingSourceData() {
	if (!m_initialDataString.empty()) {
		auto sourceData = IsConditionedCacheFilePath(m_initialDataString)
			? BuildSourceDataFromConditionedCacheFilePath(m_initialDataString)
			: BuildSourceDataFromDDSFilePath(m_initialDataString, m_meta.preferSRGB);
		UpdateSourceShapeFromDescription(sourceData->desc, CalcMipCountFromDescription(sourceData->desc));
		return sourceData;
	}

	if (!m_originalSourceBytes.empty()) {
		auto sourceData = std::make_shared<TextureSourceData>();
		sourceData->desc = m_originalSourceDesc;
		sourceData->subresources = m_originalSourceBytes;
		sourceData->isBlockCompressed = rhi::helpers::IsBlockCompressed(m_originalSourceDesc.format);
		UpdateSourceShapeFromDescription(sourceData->desc);

		if (!sourceData->desc.imageDimensions.empty()) {
			const uint32_t faces = sourceData->desc.isCubemap ? 6u : 1u;
			const uint32_t slices = faces * (std::max)(1u, sourceData->desc.arraySize);
			const uint32_t fullMipCount = CalcFullMipCount(
				sourceData->desc.imageDimensions[0].width,
				sourceData->desc.imageDimensions[0].height);
			const size_t expectedSubresources = static_cast<size_t>(slices) * fullMipCount;

			sourceData->hasFullMipChain =
				!sourceData->subresources.empty() &&
				sourceData->subresources.size() == expectedSubresources &&
				sourceData->desc.imageDimensions.size() == expectedSubresources;
		}

		return sourceData;
	}

	return BuildSourceData();
}

void TextureAsset::RecordLoadPath(TextureLoadPathTelemetry path, std::string detail) {
	m_meta.loadPath = path;
	m_meta.loadPathDetail = std::move(detail);
	if (m_lastReportedLoadPath == path) {
		return;
	}
	m_lastReportedLoadPath = path;
	spdlog::info(
		"TextureTelemetry: load_path={} texture='{}' detail='{}'",
		ToString(path),
		TextureTelemetryLabel(*this),
		m_meta.loadPathDetail);
}

void TextureAsset::RecordUploadPath(TextureUploadPathTelemetry path, std::string detail) {
	m_meta.uploadPath = path;
	m_meta.uploadPathDetail = std::move(detail);
	if (m_lastReportedUploadPath == path) {
		return;
	}
	m_lastReportedUploadPath = path;
	spdlog::info(
		"TextureTelemetry: upload_path={} texture='{}' detail='{}'",
		ToString(path),
		TextureTelemetryLabel(*this),
		m_meta.uploadPathDetail);
}

void TextureAsset::SetProcessingSettings(TextureProcessingSettings settings) {
	const bool wasEligible = m_streamingState.eligible;
	m_meta.processing = std::move(settings);
	RefreshStreamingStateFromDescription();
	if (m_streamingState.eligible) {
		m_streamingState.enabled = true;
		if (!wasEligible) {
			ApplyStreamingBootstrapTopMip();
		}
	}
	BumpStreamingStateRevision();
	InvalidateResidentImageForStreamingRequest();
}

void TextureAsset::ApplyStreamingSystemRequest(uint32_t topMip, uint64_t frameIndex) {
	const uint32_t clampedTopMip = (std::min)(topMip, m_streamingState.residency.totalMipCount - 1u);
	const bool requestChanged = m_streamingState.requestedTopMip != clampedTopMip;
	const bool pendingChanged = m_streamingState.pendingTopMip != clampedTopMip;
	const bool frameChanged = frameIndex != 0u && m_streamingState.lastSeenFrame != frameIndex;
	if (!requestChanged && !pendingChanged && !frameChanged) {
		return;
	}

	m_streamingState.requestedTopMip = clampedTopMip;
	m_streamingState.pendingTopMip = clampedTopMip;
	if (frameIndex != 0u) {
		m_streamingState.lastSeenFrame = frameIndex;
	}
	BumpStreamingStateRevision();
}

void TextureAsset::EnableMipStreaming(bool enabled) {
	const bool newEnabled = enabled && m_streamingState.eligible;
	if (m_streamingState.enabled == newEnabled) {
		return;
	}
	m_streamingState.enabled = newEnabled;
	if (m_streamingState.enabled) {
		ApplyStreamingBootstrapTopMip();
	}
	BumpStreamingStateRevision();
	InvalidateResidentImageForStreamingRequest();
}

void TextureAsset::SetRequestedTopMip(uint32_t topMip, uint64_t frameIndex) {
	const uint32_t clampedTopMip = (std::min)(topMip, m_streamingState.residency.totalMipCount - 1u);
	if (m_streamingState.requestedTopMip == clampedTopMip &&
		(frameIndex == 0u || m_streamingState.lastSeenFrame == frameIndex)) {
		return;
	}
	m_streamingState.requestedTopMip = clampedTopMip;
	if (frameIndex != 0u) {
		m_streamingState.lastSeenFrame = frameIndex;
	}
	BumpStreamingStateRevision();
}

void TextureAsset::SetPendingTopMip(uint32_t topMip) {
	const uint32_t clampedTopMip = (std::min)(topMip, m_streamingState.residency.totalMipCount - 1u);
	if (m_streamingState.pendingTopMip == clampedTopMip) {
		return;
	}
	m_streamingState.pendingTopMip = clampedTopMip;
	BumpStreamingStateRevision();
}

void TextureAsset::SetResidentMipWindow(uint32_t residentTopMip, uint32_t residentMipCount) {
	const uint32_t totalMipCount = m_streamingState.residency.totalMipCount;
	const uint32_t clampedTopMip = (std::min)(residentTopMip, totalMipCount - 1u);
	const uint32_t clampedMipCount = (std::max)(1u, (std::min)(residentMipCount, totalMipCount - clampedTopMip));
	if (m_streamingState.residency.residentTopMip == clampedTopMip &&
		m_streamingState.residency.residentMipCount == clampedMipCount) {
		return;
	}
	m_streamingState.residency.residentTopMip = clampedTopMip;
	m_streamingState.residency.residentMipCount = clampedMipCount;
	BumpStreamingStateRevision();
}

void TextureAsset::NoteTextureSeen(uint64_t frameIndex) {
	if (frameIndex == 0u || m_streamingState.lastSeenFrame == frameIndex) {
		return;
	}
	m_streamingState.lastSeenFrame = frameIndex;
	BumpStreamingStateRevision();
}

void TextureAsset::AdoptUploadedImage(std::shared_ptr<PixelBuffer> image) {
	const uint32_t residentTopMip = GetDesiredResidentTopMip();
	m_image = std::move(image);
	if (m_image) {
		m_desc = m_image->GetDescription();
	}
	m_hasUploadedFinalImage = (m_image != nullptr);
	m_hasUploadedPlaceholder = false;
	RefreshStreamingStateFromDescription();
	SetResidentMipWindow(residentTopMip, CalcMipCountFromDescription(m_desc));
	SetPendingTopMip(residentTopMip);
	BumpBindingRevision();
	if (!m_name.empty() && m_image) {
		m_image->SetName(m_name);
	}
}

DirectStorageAsyncRequestHandle TextureAsset::QueueInitialDirectStorageUploadIfNeeded() {
	if (m_hasUploadedFinalImage) {
		return {};
	}

	const uint32_t desiredResidentTopMip = GetDesiredResidentTopMip();
	if (m_directStorageReloadHandle) {
		const TextureDirectStorageReloadJobState state = m_directStorageReloadHandle->state.load(std::memory_order_acquire);
		if (m_directStorageReloadHandle->targetTopMip == desiredResidentTopMip &&
			(state == TextureDirectStorageReloadJobState::Queued ||
			 state == TextureDirectStorageReloadJobState::CreatingResource ||
			 state == TextureDirectStorageReloadJobState::Uploading ||
			 state == TextureDirectStorageReloadJobState::Ready)) {
			return {};
		}

		if (state == TextureDirectStorageReloadJobState::Queued ||
			state == TextureDirectStorageReloadJobState::CreatingResource ||
			state == TextureDirectStorageReloadJobState::Uploading) {
			return {};
		}

		m_directStorageReloadHandle.reset();
	}

	auto* filePath = std::get_if<std::string>(&m_initialStorage);
	if (filePath == nullptr || filePath->empty()) {
		return {};
	}

	try {
		auto sourceData = BuildSourceData();
		if (!sourceData) {
			return {};
		}

		if (TextureProcessingManager::GetInstance().ShouldProcess(m_meta) &&
			TextureProcessingManager::GetInstance().NeedsProcessing(*sourceData, m_meta)) {
			return {};
		}
	}
	catch (const std::exception&) {
		return {};
	}

	m_directStorageReloadHandle = IsConditionedCacheFilePath(*filePath)
		? BeginUploadConditionedCacheFilePathDirectToVRAMAsync(
			*filePath,
			desiredResidentTopMip,
			m_desc.hasRTV,
			m_desc.hasUAV)
		: BeginUploadDDSFilePathDirectToVRAMAsync(
			*filePath,
			m_meta.preferSRGB,
			desiredResidentTopMip,
			m_desc.hasRTV,
			m_desc.hasUAV);
	return {};
}

void TextureAsset::EnsureUploaded(const TextureFactory& factory) {
	const bool needsStreamingReload =
		m_hasUploadedFinalImage &&
		m_streamingState.enabled &&
		HasStreamingSourceData() &&
		m_streamingState.pendingTopMip != m_streamingState.residency.residentTopMip;
	if (m_hasUploadedFinalImage && !needsStreamingReload) {
		return;
	}

	const uint32_t desiredResidentTopMip = GetDesiredResidentTopMip();
	const bool isParticipatingMaterialTexture = m_meta.processing.isParticipatingMaterialTexture;
	const bool useConditionedCacheResidency =
		isParticipatingMaterialTexture &&
		DirectStorageManager::GetInstance().CanServiceQueue(DirectStorageQueueKind::Gpu);
	auto ensureProcessingPlaceholder = [&](const std::string& detail) {
		if (m_image || !m_meta.processing.allowAsyncPlaceholder) {
			return false;
		}

		m_image = CreatePlaceholderTexture(factory, m_meta.processing);
		m_desc = m_image->GetDescription();
		RefreshStreamingStateFromDescription();
		RecordUploadPath(TextureUploadPathTelemetry::AsyncProcessingPlaceholder, detail);
		m_hasUploadedPlaceholder = true;
		BumpBindingRevision();
		return true;
	};

	auto tryAdvanceAsyncDirectStorageReload = [&](const std::string& detail) -> bool {
		auto* filePath = std::get_if<std::string>(&m_initialStorage);
		if (filePath == nullptr || filePath->empty()) {
			return false;
		}

		if (m_directStorageReloadHandle) {
			TextureDirectStorageReloadJobState state = m_directStorageReloadHandle->state.load(std::memory_order_acquire);
			if (state == TextureDirectStorageReloadJobState::Uploading) {
				const DirectStorageAsyncRequestStatus requestStatus = DirectStorageManager::GetInstance().PollRequest(m_directStorageReloadHandle->requestHandle);
				if (requestStatus.state == DirectStorageAsyncRequestState::Ready) {
					m_directStorageReloadHandle->state.store(TextureDirectStorageReloadJobState::Ready, std::memory_order_release);
					state = TextureDirectStorageReloadJobState::Ready;
				}
				else if (requestStatus.state == DirectStorageAsyncRequestState::Failed || requestStatus.state == DirectStorageAsyncRequestState::Invalid) {
					{
						std::scoped_lock lock(m_directStorageReloadHandle->mutex);
						m_directStorageReloadHandle->error = requestStatus.message;
					}
					m_directStorageReloadHandle->state.store(TextureDirectStorageReloadJobState::Failed, std::memory_order_release);
					state = TextureDirectStorageReloadJobState::Failed;
				}
			}
			else if (state == TextureDirectStorageReloadJobState::Queued || state == TextureDirectStorageReloadJobState::CreatingResource) {
				return true;
			}

			if (m_directStorageReloadHandle->targetTopMip != desiredResidentTopMip) {
				if (state == TextureDirectStorageReloadJobState::Ready || state == TextureDirectStorageReloadJobState::Failed) {
					m_directStorageReloadHandle.reset();
				}
				else {
					return true;
				}
			}
			else if (state == TextureDirectStorageReloadJobState::Ready) {
				std::shared_ptr<PixelBuffer> uploadedImage;
				{
					std::scoped_lock lock(m_directStorageReloadHandle->mutex);
					uploadedImage = m_directStorageReloadHandle->uploadedImage;
				}
				m_directStorageReloadHandle.reset();
				if (!uploadedImage) {
					return false;
				}

				AdoptUploadedImage(std::move(uploadedImage));
				RecordUploadPath(TextureUploadPathTelemetry::DirectStorageGpuDirect, detail);
				if (!m_initialDataString.empty()) {
					m_initialStorage = m_initialDataString;
				}
				return true;
			}
			else if (state == TextureDirectStorageReloadJobState::Failed) {
				m_directStorageReloadHandle.reset();
				return false;
			}
			else {
				return true;
			}
		}

		m_directStorageReloadHandle = IsConditionedCacheFilePath(*filePath)
			? BeginUploadConditionedCacheFilePathDirectToVRAMAsync(
				*filePath,
				desiredResidentTopMip,
				m_desc.hasRTV,
				m_desc.hasUAV)
			: BeginUploadDDSFilePathDirectToVRAMAsync(
				*filePath,
				m_meta.preferSRGB,
				desiredResidentTopMip,
				m_desc.hasRTV,
				m_desc.hasUAV);
		return m_directStorageReloadHandle != nullptr;
	};

	auto promoteStreamingSourceToProcessedCachePath = [&](const std::string& cachePath) {
		if (cachePath.empty()) {
			return false;
		}

		m_initialDataString = cachePath;
		m_initialStorage = m_initialDataString;
		m_meta.isProcessingCacheArtifact = true;
		return true;
	};

	auto promoteStreamingSourceToProcessedCache = [&]() {
		const std::wstring cachePath = TextureProcessingManager::GetInstance().GetExistingCachePathForFile(m_meta);
		if (cachePath.empty()) {
			return false;
		}

		return promoteStreamingSourceToProcessedCachePath(std::filesystem::path(cachePath).string());
	};

	const bool preferDirectStorageStreamingReload = [&]() {
		if (!needsStreamingReload) {
			return false;
		}

		auto* filePath = std::get_if<std::string>(&m_initialStorage);
		if (filePath == nullptr || (!IsDDSFilePath(*filePath) && !IsConditionedCacheFilePath(*filePath))) {
			return false;
		}

		return m_meta.isProcessingCacheArtifact ||
			m_lastReportedUploadPath == TextureUploadPathTelemetry::DirectStorageGpuDirect;
	}();

	if (preferDirectStorageStreamingReload &&
		tryAdvanceAsyncDirectStorageReload("texture residency reloaded asynchronously from DDS-backed source through DirectStorage GPU queue")) {
		return;
	}
	if (useConditionedCacheResidency && m_meta.isProcessingCacheArtifact) {
		if (tryAdvanceAsyncDirectStorageReload("texture residency uploaded asynchronously from conditioned cache through DirectStorage GPU queue")) {
			ensureProcessingPlaceholder("conditioned cache DirectStorage upload pending; placeholder texture uploaded");
			return;
		}

		ensureProcessingPlaceholder("conditioned cache DirectStorage upload pending; placeholder texture uploaded");
		return;
	}

	std::shared_ptr<TextureSourceData> sourceData;
	if (m_reloadHandle && m_reloadHandle->targetTopMip != desiredResidentTopMip) {
		m_reloadHandle.reset();
	}
	if (!m_reloadHandle && !useConditionedCacheResidency && std::holds_alternative<std::string>(m_initialStorage)) {
		const auto& filePath = std::get<std::string>(m_initialStorage);
		if (!filePath.empty() && !IsConditionedCacheFilePath(filePath)) {
			m_reloadHandle = RequestReloadSourceDataAsync(
				filePath,
				m_meta.preferSRGB,
				desiredResidentTopMip,
				m_streamingState.enabled);
		}
	}
	if (m_reloadHandle) {
		const TextureReloadJobState reloadState = m_reloadHandle->state.load(std::memory_order_acquire);
		if (reloadState == TextureReloadJobState::Ready) {
			uint32_t sourceTotalMipCount = 0u;
			uint32_t sourceFullWidth = 0u;
			uint32_t sourceFullHeight = 0u;
			{
				std::scoped_lock lock(m_reloadHandle->mutex);
				sourceData = m_reloadHandle->sourceData;
				sourceTotalMipCount = m_reloadHandle->sourceTotalMipCount;
				sourceFullWidth = m_reloadHandle->sourceFullWidth;
				sourceFullHeight = m_reloadHandle->sourceFullHeight;
			}
			ApplySourceShapeHint(sourceFullWidth, sourceFullHeight, sourceTotalMipCount);
			m_reloadHandle.reset();
		}
		else if (reloadState == TextureReloadJobState::Failed) {
			m_reloadHandle.reset();
		}
	}

	if (TextureProcessingManager::GetInstance().ShouldProcess(m_meta)) {
		sourceData = sourceData ? sourceData : BuildSourceData();
		if (!TextureProcessingManager::GetInstance().NeedsProcessing(*sourceData, m_meta)) {
			const uint32_t residentMipCount = CalcMipCountFromDescription(sourceData->desc);
			if (needsStreamingReload) {
				if (tryAdvanceAsyncDirectStorageReload("texture residency reloaded asynchronously from file-backed DDS through DirectStorage GPU queue")) {
					return;
				}
			}
			else if (tryAdvanceAsyncDirectStorageReload("texture residency uploaded asynchronously from file-backed DDS through DirectStorage GPU queue")) {
				ensureProcessingPlaceholder("DirectStorage texture upload pending; fallback texture uploaded");
				return;
			}
			if (useConditionedCacheResidency && m_meta.isProcessingCacheArtifact) {
				ensureProcessingPlaceholder("conditioned cache DirectStorage upload pending; placeholder texture uploaded");
				return;
			}
			m_desc = sourceData->desc;
			RefreshStreamingStateFromDescription();
			m_image = factory.CreateAlwaysResidentPixelBuffer(
				sourceData->desc,
				TextureFactory::TextureInitialData::FromBytes(sourceData->subresources),
				m_name);
			m_hasUploadedFinalImage = true;
			m_hasUploadedPlaceholder = false;
			SetResidentMipWindow(desiredResidentTopMip, residentMipCount);
			SetPendingTopMip(desiredResidentTopMip);
			RecordUploadPath(TextureUploadPathTelemetry::CpuImmediateUpload, "texture data uploaded through TextureFactory without async processing");
			BumpBindingRevision();
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
		if (!sourceData && m_reloadHandle) {
			ensureProcessingPlaceholder("async reload source build pending; placeholder texture uploaded");

			return;
		}

		const auto processingSourceData = BuildProcessingSourceData();
		m_processingHandle = TextureProcessingManager::GetInstance().RequestProcessing(processingSourceData, m_meta);

		if (m_processingHandle) {
			const TextureProcessingJobState state = m_processingHandle->state.load(std::memory_order_acquire);
			if (state == TextureProcessingJobState::GpuReadyToSubmit) {
				if (factory.SubmitBC7CompressionJob(m_processingHandle, m_name)) {
					TextureProcessingManager::GetInstance().MarkGpuJobSubmitted(m_processingHandle);
				}
			}

			if (state == TextureProcessingJobState::Ready) {
				std::shared_ptr<TextureSourceData> result;
				std::shared_ptr<PixelBuffer> uploadedImage;
				bool loadedFromCache = false;
				bool completedOnGpu = false;
				std::string conditionedCachePath;
				{
					std::scoped_lock lock(m_processingHandle->mutex);
					result = m_processingHandle->result;
					uploadedImage = m_processingHandle->uploadedImage;
					loadedFromCache = m_processingHandle->loadedFromCache;
					completedOnGpu = m_processingHandle->completedOnGpu;
					conditionedCachePath = m_processingHandle->conditionedCachePath;
				}

				if (uploadedImage) {
					m_desc = uploadedImage->GetDescription();
					m_meta.isProcessingCacheArtifact = loadedFromCache;
					if (!promoteStreamingSourceToProcessedCachePath(conditionedCachePath)) {
						promoteStreamingSourceToProcessedCache();
					}
					AdoptUploadedImage(std::move(uploadedImage));
					RecordUploadPath(
						loadedFromCache ? TextureUploadPathTelemetry::ProcessingCacheUpload : TextureUploadPathTelemetry::AsyncProcessingReadyUpload,
						completedOnGpu
							? "async GPU processing completed and adopted resident PixelBuffer"
							: "async processing completed and adopted resident PixelBuffer");
					if (!m_initialDataString.empty()) {
						m_initialStorage = m_initialDataString;
					}
					else {
						m_initialStorage = std::monostate{};
					}
					return;
				}

				if (useConditionedCacheResidency && promoteStreamingSourceToProcessedCachePath(conditionedCachePath)) {
					if (result) {
						m_desc = result->desc;
						RefreshStreamingStateFromDescription();
					}
					m_meta.isProcessingCacheArtifact = true;
					if (tryAdvanceAsyncDirectStorageReload(
							loadedFromCache
								? "processed texture cache hit; residency uploaded asynchronously from conditioned cache through DirectStorage GPU queue"
								: "async processing completed; residency uploaded asynchronously from conditioned cache through DirectStorage GPU queue")) {
						ensureProcessingPlaceholder("conditioned cache DirectStorage upload pending; placeholder texture uploaded");
						return;
					}

					ensureProcessingPlaceholder("conditioned cache DirectStorage upload pending; placeholder texture uploaded");
					return;
				}

				if (result) {
					const uint32_t residentMipCount = CalcMipCountFromDescription(result->desc);
					m_desc = result->desc;
					RefreshStreamingStateFromDescription();
					m_meta.isProcessingCacheArtifact = loadedFromCache;
					if (!promoteStreamingSourceToProcessedCachePath(conditionedCachePath)) {
						promoteStreamingSourceToProcessedCache();
					}
					if (loadedFromCache) {
						m_meta.fileType = ImageFiletype::DDS;
						m_meta.loader = ImageLoader::DirectXTex;
					}
					m_image = factory.CreateAlwaysResidentPixelBuffer(
						result->desc,
						TextureFactory::TextureInitialData::FromBytes(result->subresources),
						m_name);
					SetResidentMipWindow(desiredResidentTopMip, residentMipCount);
					SetPendingTopMip(desiredResidentTopMip);
					RecordUploadPath(
						loadedFromCache ? TextureUploadPathTelemetry::ProcessingCacheUpload : TextureUploadPathTelemetry::AsyncProcessingReadyUpload,
						loadedFromCache
							? "async processing completed from DDS cache artifact"
							: "async processing completed and uploaded through TextureFactory");
					m_hasUploadedFinalImage = true;
					m_hasUploadedPlaceholder = false;
					BumpBindingRevision();
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
				std::string processingError;
				{
					std::scoped_lock lock(m_processingHandle->mutex);
					processingError = m_processingHandle->error;
				}
				if (!useConditionedCacheResidency && tryAdvanceAsyncDirectStorageReload(
						processingError.empty()
							? "async processing failed; residency restored asynchronously from file-backed DDS through DirectStorage GPU queue"
							: "async processing failed ('" + processingError + "'); residency restored asynchronously from file-backed DDS through DirectStorage GPU queue")) {
					ensureProcessingPlaceholder(
						processingError.empty()
							? "async processing failed; DirectStorage fallback upload pending"
							: "async processing failed ('" + processingError + "'); DirectStorage fallback upload pending");
					return;
				}
				if (useConditionedCacheResidency) {
					ensureProcessingPlaceholder(
						processingError.empty()
							? "async processing failed; keeping placeholder texture"
							: "async processing failed ('" + processingError + "'); keeping placeholder texture");
					return;
				}
				const auto fallbackSourceData = BuildSourceData();
				const uint32_t residentMipCount = CalcMipCountFromDescription(fallbackSourceData->desc);
				m_meta.isProcessingCacheArtifact = false;
				m_desc = fallbackSourceData->desc;
				m_image = factory.CreateAlwaysResidentPixelBuffer(
					fallbackSourceData->desc,
					TextureFactory::TextureInitialData::FromBytes(fallbackSourceData->subresources),
					m_name);
				RefreshStreamingStateFromDescription();
				SetResidentMipWindow(desiredResidentTopMip, residentMipCount);
				SetPendingTopMip(desiredResidentTopMip);
				RecordUploadPath(
					TextureUploadPathTelemetry::ProcessingFailedFallback,
					processingError.empty()
						? "async processing failed; uploaded original bytes through TextureFactory"
						: "async processing failed ('" + processingError + "'); uploaded original bytes through TextureFactory");
				m_hasUploadedFinalImage = true;
				m_hasUploadedPlaceholder = false;
				BumpBindingRevision();
				return;
			}
		}

		ensureProcessingPlaceholder("async processing pending; placeholder texture uploaded");

		return;
	}

	if (m_hasUploadedPlaceholder && sourceData) {
		m_image.reset();
		m_hasUploadedPlaceholder = false;
	}

	if (m_hasUploadedPlaceholder && m_directStorageReloadHandle) {
		if (tryAdvanceAsyncDirectStorageReload("fallback texture kept resident while DirectStorage upload advances asynchronously")) {
			return;
		}

		m_image.reset();
		m_hasUploadedPlaceholder = false;
	}

	if (!m_image) {
		if (tryAdvanceAsyncDirectStorageReload("texture uploaded asynchronously from file-backed DDS through DirectStorage GPU queue without preprocessing")) {
			ensureProcessingPlaceholder("DirectStorage texture upload pending; fallback texture uploaded");
			return;
		}
		if (!sourceData && m_reloadHandle) {
			ensureProcessingPlaceholder("async reload source build pending; fallback texture uploaded");
			return;
		}
		const auto immediateSourceData = sourceData ? sourceData : BuildSourceData();
		const uint32_t residentMipCount = CalcMipCountFromDescription(immediateSourceData->desc);
		m_desc = immediateSourceData->desc;
		m_image = factory.CreateAlwaysResidentPixelBuffer(
			immediateSourceData->desc,
			TextureFactory::TextureInitialData::FromBytes(immediateSourceData->subresources),
			m_name);
		RecordUploadPath(TextureUploadPathTelemetry::CpuImmediateUpload, "texture uploaded through TextureFactory without preprocessing");
		m_hasUploadedFinalImage = true;
		RefreshStreamingStateFromDescription();
		SetResidentMipWindow(desiredResidentTopMip, residentMipCount);
		SetPendingTopMip(desiredResidentTopMip);
		BumpBindingRevision();
		if (!m_initialDataString.empty()) {
			m_initialStorage = m_initialDataString;
		}
		else {
			m_initialStorage = std::monostate{};
		}
	}
}
