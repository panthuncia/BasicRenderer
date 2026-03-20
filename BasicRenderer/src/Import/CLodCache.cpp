#include "Import/CLodCache.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#include <cwctype>

#include <boost/container_hash/hash.hpp>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>

#include <spdlog/spdlog.h>

#include "Utilities/CachePathUtilities.h"

namespace CLodCache {

	namespace {
		static constexpr const char* kRootPrimPath = "/CLodCache";
		static constexpr const char* kGroupsPrimPath = "/CLodCache/Groups";

		std::wstring SanitizeFolderName(const std::wstring& input)
		{
			if (input.empty()) {
				return L"scene";
			}

			std::wstring out;
			out.reserve(input.size());
			for (wchar_t ch : input) {
				if (std::iswalnum(ch) != 0 || ch == L'_' || ch == L'-') {
					out.push_back(ch);
				}
				else {
					out.push_back(L'_');
				}
			}

			if (out.empty()) {
				return L"scene";
			}

			return out;
		}

		std::wstring BuildSceneCacheSubdirectory(const std::string& sourceIdentifier)
		{
			std::wstring stem = L"scene";
			if (!sourceIdentifier.empty()) {
				std::filesystem::path sourcePath = s2ws(sourceIdentifier);
				std::wstring sourceStem = sourcePath.stem().wstring();
				if (!sourceStem.empty()) {
					stem = sourceStem;
				}
			}

			stem = SanitizeFolderName(stem);

			size_t hashSeed = 0;
			boost::hash_combine(hashSeed, sourceIdentifier);

			std::wstringstream folderName;
			folderName << stem << L"_" << std::hex << hashSeed;
			return L"clod\\" + folderName.str();
		}

		std::wstring GetCacheFilePathBySource(const std::wstring& fileName, const std::string& sourceIdentifier)
		{
			return GetCacheFilePath(fileName, BuildSceneCacheSubdirectory(sourceIdentifier));
		}

		template<typename T>
		void WritePod(std::vector<std::byte>& out, const T& value)
		{
			const std::byte* ptr = reinterpret_cast<const std::byte*>(&value);
			out.insert(out.end(), ptr, ptr + sizeof(T));
		}

		template<typename T>
		bool ReadPod(const std::vector<std::byte>& in, size_t& offset, T& out)
		{
			if (offset + sizeof(T) > in.size()) {
				return false;
			}
			std::memcpy(&out, in.data() + offset, sizeof(T));
			offset += sizeof(T);
			return true;
		}

		template<typename T>
		void WriteVectorPod(std::vector<std::byte>& out, const std::vector<T>& values)
		{
			const uint64_t count = static_cast<uint64_t>(values.size());
			WritePod(out, count);
			if (!values.empty()) {
				const std::byte* ptr = reinterpret_cast<const std::byte*>(values.data());
				out.insert(out.end(), ptr, ptr + sizeof(T) * values.size());
			}
		}

		template<typename T>
		bool ReadVectorPod(const std::vector<std::byte>& in, size_t& offset, std::vector<T>& values)
		{
			uint64_t count = 0;
			if (!ReadPod(in, offset, count)) {
				return false;
			}
			if (count > (std::numeric_limits<size_t>::max)()) {
				return false;
			}
			const size_t byteCount = sizeof(T) * static_cast<size_t>(count);
			if (offset + byteCount > in.size()) {
				return false;
			}
			values.resize(static_cast<size_t>(count));
			if (byteCount > 0) {
				std::memcpy(values.data(), in.data() + offset, byteCount);
			}
			offset += byteCount;
			return true;
		}

		void WriteString(std::vector<std::byte>& out, const std::string& value)
		{
			const uint64_t length = static_cast<uint64_t>(value.size());
			WritePod(out, length);
			if (!value.empty()) {
				const std::byte* ptr = reinterpret_cast<const std::byte*>(value.data());
				out.insert(out.end(), ptr, ptr + value.size());
			}
		}

		bool ReadString(const std::vector<std::byte>& in, size_t& offset, std::string& value)
		{
			uint64_t length = 0;
			if (!ReadPod(in, offset, length)) {
				return false;
			}
			if (length > (std::numeric_limits<size_t>::max)()) {
				return false;
			}
			if (offset + static_cast<size_t>(length) > in.size()) {
				return false;
			}
			value.resize(static_cast<size_t>(length));
			if (length > 0) {
				std::memcpy(value.data(), in.data() + offset, static_cast<size_t>(length));
			}
			offset += static_cast<size_t>(length);
			return true;
		}

		std::vector<std::byte> SerializeMetadata(
			uint64_t buildConfigHash,
			const ClusterLODPrebuiltData& prebuiltData,
			const std::vector<ClusterLODGroupDiskLocator>& groupDiskLocators,
			const ClusterLODCacheSource& cacheSource)
		{
			std::vector<std::byte> out;
			WritePod(out, kSchemaVersion);
			WritePod(out, buildConfigHash);

			WriteVectorPod(out, prebuiltData.groups);
			WriteVectorPod(out, prebuiltData.segments);
			WriteVectorPod(out, prebuiltData.segmentBounds);
			WritePod(out, prebuiltData.objectBoundingSphere);
			const uint8_t hasInlineGroupChunks = prebuiltData.groupChunks.empty() ? 0u : 1u;
			WritePod(out, hasInlineGroupChunks);
			if (hasInlineGroupChunks != 0u) {
				WriteVectorPod(out, prebuiltData.groupChunks);
			}
			WriteVectorPod(out, groupDiskLocators);
			WriteString(out, cacheSource.sourceIdentifier);
			WriteString(out, cacheSource.primPath);
			WriteString(out, cacheSource.subsetName);
			WritePod(out, cacheSource.buildConfigHash);
			WriteString(out, ws2s(cacheSource.containerFileName));
			WriteVectorPod(out, prebuiltData.nodes);

			// --- Voxel group mapping ---
			const auto& vgm = prebuiltData.voxelGroupMapping;
			const uint32_t payloadCount = static_cast<uint32_t>(vgm.payloads.size());
			WritePod(out, payloadCount);
			for (const VoxelGroupPayload& payload : vgm.payloads)
			{
				WritePod(out, payload.resolution);
				WritePod(out, payload.aabbMin);
				WritePod(out, payload.aabbMax);
				WriteVectorPod(out, payload.activeCells);
			}
			WriteVectorPod(out, vgm.groupToPayloadIndex);

			return out;
		}

		bool DeserializeMetadata(const std::vector<std::byte>& blob, CacheData& out)
		{
			size_t offset = 0;
			if (!ReadPod(blob, offset, out.schemaVersion)) return false;
			if (out.schemaVersion != kSchemaVersion) return false;
			if (!ReadPod(blob, offset, out.buildConfigHash)) return false;

			if (!ReadVectorPod(blob, offset, out.prebuiltData.groups)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.segments)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.segmentBounds)) return false;
			if (!ReadPod(blob, offset, out.prebuiltData.objectBoundingSphere)) return false;

			uint8_t hasInlineGroupChunks = 0u;
			if (!ReadPod(blob, offset, hasInlineGroupChunks)) return false;
			if (hasInlineGroupChunks != 0u) {
				if (!ReadVectorPod(blob, offset, out.prebuiltData.groupChunks)) return false;
			}
			else {
				out.prebuiltData.groupChunks.clear();
			}
			if (!ReadVectorPod(blob, offset, out.prebuiltData.groupDiskLocators)) return false;
			if (!ReadString(blob, offset, out.prebuiltData.cacheSource.sourceIdentifier)) return false;
			if (!ReadString(blob, offset, out.prebuiltData.cacheSource.primPath)) return false;
			if (!ReadString(blob, offset, out.prebuiltData.cacheSource.subsetName)) return false;
			if (!ReadPod(blob, offset, out.prebuiltData.cacheSource.buildConfigHash)) return false;
			std::string containerFileName;
			if (!ReadString(blob, offset, containerFileName)) return false;
			out.prebuiltData.cacheSource.containerFileName = s2ws(containerFileName);
			if (!ReadVectorPod(blob, offset, out.prebuiltData.nodes)) return false;

			// --- Voxel group mapping ---
			uint32_t payloadCount = 0;
			if (!ReadPod(blob, offset, payloadCount)) return false;
			out.prebuiltData.voxelGroupMapping.payloads.resize(payloadCount);
			for (uint32_t pi = 0; pi < payloadCount; ++pi)
			{
				VoxelGroupPayload& payload = out.prebuiltData.voxelGroupMapping.payloads[pi];
				if (!ReadPod(blob, offset, payload.resolution)) return false;
				if (!ReadPod(blob, offset, payload.aabbMin)) return false;
				if (!ReadPod(blob, offset, payload.aabbMax)) return false;
				if (!ReadVectorPod(blob, offset, payload.activeCells)) return false;
			}
			if (!ReadVectorPod(blob, offset, out.prebuiltData.voxelGroupMapping.groupToPayloadIndex)) return false;

			return offset == blob.size();
		}

		static constexpr uint32_t kContainerMagic = 0x444F4C43u; // CLOD

		struct ContainerHeader {
			uint32_t magic = kContainerMagic;
			uint32_t version = 3;
			uint32_t reserved = 0;
			uint32_t groupCount = 0;
		};

		struct GroupPayloadHeader {
			ClusterLODGroupChunk groupChunkMetadata{};
			uint32_t pageCount = 0;
			// Followed by pageCount x uint32_t page blob sizes,
			// then pageCount page blobs in sequence.
		};

		std::wstring BuildGroupContainerFileName(const CacheKey& key, uint64_t buildConfigHash)
		{
			size_t hashSeed = 0;
			boost::hash_combine(hashSeed, key.sourceIdentifier);
			boost::hash_combine(hashSeed, key.primPath);
			boost::hash_combine(hashSeed, key.subsetName);
			boost::hash_combine(hashSeed, buildConfigHash);

			std::stringstream ss;
			ss << "clod_" << std::hex << hashSeed << ".clodbin";
			return s2ws(ss.str());
		}

		template<typename T>
		bool TryGetByteSize(const std::vector<T>& values, uint32_t& outSizeBytes)
		{
			const uint64_t sizeBytes64 = static_cast<uint64_t>(values.size()) * static_cast<uint64_t>(sizeof(T));
			if (sizeBytes64 > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
				return false;
			}
			outSizeBytes = static_cast<uint32_t>(sizeBytes64);
			return true;
		}

		template<typename T>
		bool WriteVectorRaw(std::ofstream& file, const std::vector<T>& values)
		{
			if (values.empty()) {
				return true;
			}
			file.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
			return file.good();
		}

		bool SaveContainerPayload(
			const std::wstring& containerPath,
			const ClusterLODPrebuiltData& prebuiltData,
			const ClusterLODCacheBuildPayload& payload,
			std::vector<ClusterLODGroupDiskLocator>& outLocators)
		{
			const uint32_t groupCount = static_cast<uint32_t>(prebuiltData.groups.size());
			outLocators.assign(groupCount, {});

			std::ofstream file(containerPath, std::ios::binary | std::ios::trunc);
			if (!file.is_open()) {
				return false;
			}

			ContainerHeader header{};
			header.groupCount = groupCount;
			file.write(reinterpret_cast<const char*>(&header), sizeof(header));
			if (!file.good()) {
				return false;
			}

			const std::streamoff directoryOffset = static_cast<std::streamoff>(file.tellp());
			if (groupCount > 0) {
				std::vector<ClusterLODGroupDiskLocator> emptyDirectory(groupCount);
				file.write(reinterpret_cast<const char*>(emptyDirectory.data()), static_cast<std::streamsize>(emptyDirectory.size() * sizeof(ClusterLODGroupDiskLocator)));
				if (!file.good()) {
					return false;
				}
			}

			for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
				const std::vector<std::vector<std::byte>> emptyPageBlobs;
				ClusterLODGroupChunk groupChunkMetadata{};
				if (groupIndex < prebuiltData.groupChunks.size()) {
					groupChunkMetadata = prebuiltData.groupChunks[groupIndex];
				}
				else if (groupIndex < prebuiltData.groups.size()) {
					const auto& group = prebuiltData.groups[groupIndex];
					groupChunkMetadata.groupVertexCount = group.groupVertexCount;
					groupChunkMetadata.meshletCount = group.meshletCount;
				}

				const auto* pageBlobsPtr = payload.groupPageBlobs;
				const auto& pageBlobs = (pageBlobsPtr != nullptr && groupIndex < pageBlobsPtr->size()) ? (*pageBlobsPtr)[groupIndex] : emptyPageBlobs;

				GroupPayloadHeader groupHeader{};
				groupHeader.groupChunkMetadata = groupChunkMetadata;
				groupHeader.pageCount = static_cast<uint32_t>(pageBlobs.size());

				const uint64_t blobOffset64 = static_cast<uint64_t>(file.tellp());

				file.write(reinterpret_cast<const char*>(&groupHeader), sizeof(groupHeader));
				if (!file.good()) return false;

				// Write per-page blob sizes
				std::vector<uint32_t> pageBlobSizes(pageBlobs.size());
				for (size_t pi = 0; pi < pageBlobs.size(); ++pi) {
					if (pageBlobs[pi].size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) return false;
					pageBlobSizes[pi] = static_cast<uint32_t>(pageBlobs[pi].size());
				}
				if (!pageBlobSizes.empty()) {
					file.write(reinterpret_cast<const char*>(pageBlobSizes.data()),
						static_cast<std::streamsize>(pageBlobSizes.size() * sizeof(uint32_t)));
					if (!file.good()) return false;
				}

				// Write page blob data
				for (const auto& pageBlob : pageBlobs) {
					if (!pageBlob.empty()) {
						file.write(reinterpret_cast<const char*>(pageBlob.data()),
							static_cast<std::streamsize>(pageBlob.size()));
						if (!file.good()) return false;
					}
				}

				const uint64_t blobEnd64 = static_cast<uint64_t>(file.tellp());
				if (blobEnd64 < blobOffset64) return false;
				const uint64_t blobSize64 = blobEnd64 - blobOffset64;
				if (blobSize64 > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) return false;

				auto& locator = outLocators[groupIndex];
				locator.blobOffset = blobOffset64;
				locator.blobSizeBytes = static_cast<uint32_t>(blobSize64);
				locator.reserved = 0;
			}

			if (groupCount > 0) {
				file.seekp(directoryOffset, std::ios::beg);
				if (!file.good()) return false;
				file.write(reinterpret_cast<const char*>(outLocators.data()), static_cast<std::streamsize>(outLocators.size() * sizeof(ClusterLODGroupDiskLocator)));
				if (!file.good()) return false;
			}

			return file.good();
		}

		template<typename T>
		bool ReadVectorRaw(std::ifstream& file, uint32_t sizeBytes, std::vector<T>& outValues)
		{
			if ((sizeBytes % sizeof(T)) != 0u) {
				return false;
			}
			outValues.resize(static_cast<size_t>(sizeBytes / sizeof(T)));
			if (sizeBytes == 0u) {
				return true;
			}
			file.read(reinterpret_cast<char*>(outValues.data()), static_cast<std::streamsize>(sizeBytes));
			return file.good();
		}

		std::vector<std::byte> ToBytes(const pxr::VtArray<unsigned char>& data)
		{
			std::vector<std::byte> bytes(data.size());
			for (size_t i = 0; i < data.size(); ++i) {
				bytes[i] = static_cast<std::byte>(data[i]);
			}
			return bytes;
		}

		pxr::VtArray<unsigned char> ToVtUChar(const std::vector<std::byte>& bytes)
		{
			pxr::VtArray<unsigned char> out;
			out.resize(bytes.size());
			for (size_t i = 0; i < bytes.size(); ++i) {
				out[i] = static_cast<unsigned char>(bytes[i]);
			}
			return out;
		}

		std::wstring BuildGroupPayloadFileName(const CacheKey& key, uint64_t buildConfigHash, uint32_t groupIndex)
		{
			size_t hashSeed = 0;
			boost::hash_combine(hashSeed, key.sourceIdentifier);
			boost::hash_combine(hashSeed, key.primPath);
			boost::hash_combine(hashSeed, key.subsetName);
			boost::hash_combine(hashSeed, buildConfigHash);
			boost::hash_combine(hashSeed, groupIndex);

			std::stringstream ss;
			ss << "clod_" << std::hex << hashSeed << "_g" << std::dec << groupIndex << ".usdc";
			return s2ws(ss.str());
		}

		std::string GroupPrimPathString(uint32_t groupIndex)
		{
			return std::string(kGroupsPrimPath) + "/g_" + std::to_string(groupIndex);
		}

		bool SaveGroupPayloadLayer(
			const CacheKey& key,
			uint64_t buildConfigHash,
			uint32_t groupIndex,
			const std::vector<std::byte>& vertexChunk,
			const std::vector<std::byte>& skinningChunk,
			const std::vector<uint32_t>& meshletVertexChunk,
			const std::vector<uint32_t>& compressedPositionWordChunk,
			const std::vector<uint32_t>& compressedNormalWordChunk,
			const std::vector<uint32_t>& compressedMeshletVertexWordChunk,
			const std::vector<meshopt_Meshlet>& meshletChunk,
			const std::vector<uint8_t>& meshletTriangleChunk,
			const std::vector<BoundingSphere>& meshletBoundsChunk)
		{
			const std::wstring groupFileName = BuildGroupPayloadFileName(key, buildConfigHash, groupIndex);
			const std::wstring groupCachePath = GetCacheFilePathBySource(groupFileName, key.sourceIdentifier);

			auto groupStage = pxr::UsdStage::CreateNew(ws2s(groupCachePath), pxr::UsdStage::LoadNone);
			if (!groupStage) {
				return false;
			}

			auto groupRoot = groupStage->DefinePrim(pxr::SdfPath("/GroupPayload"), pxr::TfToken("Scope"));
			if (!groupRoot) {
				return false;
			}

			groupRoot.CreateAttribute(pxr::TfToken("groupIndex"), pxr::SdfValueTypeNames->UInt, true)
				.Set(groupIndex);
			groupRoot.CreateAttribute(pxr::TfToken("groupVertexChunk"), pxr::SdfValueTypeNames->UCharArray, true)
				.Set(ToVtUChar(vertexChunk));
			groupRoot.CreateAttribute(pxr::TfToken("groupSkinningVertexChunk"), pxr::SdfValueTypeNames->UCharArray, true)
				.Set(ToVtUChar(skinningChunk));

			pxr::VtArray<uint32_t> meshletVertices;
			meshletVertices.assign(meshletVertexChunk.begin(), meshletVertexChunk.end());
			groupRoot.CreateAttribute(pxr::TfToken("groupMeshletVertexChunk"), pxr::SdfValueTypeNames->UIntArray, true)
				.Set(meshletVertices);

			pxr::VtArray<uint32_t> compressedPositionWords;
			compressedPositionWords.assign(compressedPositionWordChunk.begin(), compressedPositionWordChunk.end());
			groupRoot.CreateAttribute(pxr::TfToken("groupCompressedPositionWordChunk"), pxr::SdfValueTypeNames->UIntArray, true)
				.Set(compressedPositionWords);

			pxr::VtArray<uint32_t> compressedNormalWords;
			compressedNormalWords.assign(compressedNormalWordChunk.begin(), compressedNormalWordChunk.end());
			groupRoot.CreateAttribute(pxr::TfToken("groupCompressedNormalWordChunk"), pxr::SdfValueTypeNames->UIntArray, true)
				.Set(compressedNormalWords);

			pxr::VtArray<uint32_t> compressedMeshletVertexWords;
			compressedMeshletVertexWords.assign(compressedMeshletVertexWordChunk.begin(), compressedMeshletVertexWordChunk.end());
			groupRoot.CreateAttribute(pxr::TfToken("groupCompressedMeshletVertexWordChunk"), pxr::SdfValueTypeNames->UIntArray, true)
				.Set(compressedMeshletVertexWords);

			std::vector<std::byte> meshletChunkBytes(meshletChunk.size() * sizeof(meshopt_Meshlet));
			if (!meshletChunkBytes.empty()) {
				std::memcpy(meshletChunkBytes.data(), meshletChunk.data(), meshletChunkBytes.size());
			}

			std::vector<std::byte> meshletTriangleChunkBytes(meshletTriangleChunk.size() * sizeof(uint8_t));
			if (!meshletTriangleChunkBytes.empty()) {
				std::memcpy(meshletTriangleChunkBytes.data(), meshletTriangleChunk.data(), meshletTriangleChunkBytes.size());
			}

			std::vector<std::byte> meshletBoundsChunkBytes(meshletBoundsChunk.size() * sizeof(BoundingSphere));
			if (!meshletBoundsChunkBytes.empty()) {
				std::memcpy(meshletBoundsChunkBytes.data(), meshletBoundsChunk.data(), meshletBoundsChunkBytes.size());
			}

			groupRoot.CreateAttribute(pxr::TfToken("groupMeshletChunk"), pxr::SdfValueTypeNames->UCharArray, true)
				.Set(ToVtUChar(meshletChunkBytes));

			groupRoot.CreateAttribute(pxr::TfToken("groupMeshletTriangleChunk"), pxr::SdfValueTypeNames->UCharArray, true)
				.Set(ToVtUChar(meshletTriangleChunkBytes));

			groupRoot.CreateAttribute(pxr::TfToken("groupMeshletBoundsChunk"), pxr::SdfValueTypeNames->UCharArray, true)
				.Set(ToVtUChar(meshletBoundsChunkBytes));

			return groupStage->GetRootLayer()->Save();
		}
	}

	namespace {
		bool SaveImpl(const CacheKey& key, uint64_t buildConfigHash, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload)
		{
			const std::wstring fileName = BuildCacheFileName(key, buildConfigHash);
			const std::wstring cachePath = GetCacheFilePathBySource(fileName, key.sourceIdentifier);
			const std::wstring containerFileName = BuildGroupContainerFileName(key, buildConfigHash);
			const std::wstring containerPath = GetCacheFilePathBySource(containerFileName, key.sourceIdentifier);

			spdlog::info("CLodCache::SaveImpl  metadata='{}' container='{}'",
				ws2s(cachePath), ws2s(containerPath));

			std::vector<ClusterLODGroupDiskLocator> groupDiskLocators;
			if (!SaveContainerPayload(containerPath, prebuiltData, payload, groupDiskLocators)) {
				spdlog::warn("Failed to write CLod container payload: {}", ws2s(containerPath));
				return false;
			}

			auto stage = pxr::UsdStage::CreateNew(ws2s(cachePath), pxr::UsdStage::LoadNone);
			if (!stage) {
				spdlog::warn("Failed to create CLod cache stage: {}", ws2s(cachePath));
				return false;
			}

			auto prim = stage->DefinePrim(pxr::SdfPath(kRootPrimPath), pxr::TfToken("Scope"));
			if (!prim) {
				return false;
			}

			auto groupsPrim = stage->DefinePrim(pxr::SdfPath(kGroupsPrimPath), pxr::TfToken("Scope"));
			if (!groupsPrim) {
				return false;
			}

			prim.CreateAttribute(pxr::TfToken("clodSchemaVersion"), pxr::SdfValueTypeNames->Int, true)
				.Set(static_cast<int>(kSchemaVersion));
			prim.CreateAttribute(pxr::TfToken("clodBuildConfigHash"), pxr::SdfValueTypeNames->Int64, true)
				.Set(static_cast<int64_t>(buildConfigHash));

			ClusterLODCacheSource cacheSource = prebuiltData.cacheSource;
			cacheSource.sourceIdentifier = key.sourceIdentifier;
			cacheSource.primPath = key.primPath;
			cacheSource.subsetName = key.subsetName;
			cacheSource.buildConfigHash = buildConfigHash;
			cacheSource.containerFileName = containerFileName;

			auto blob = SerializeMetadata(buildConfigHash, prebuiltData, groupDiskLocators, cacheSource);
			auto vtBlob = ToVtUChar(blob);
			prim.CreateAttribute(pxr::TfToken("clodBlob"), pxr::SdfValueTypeNames->UCharArray, true)
				.Set(vtBlob);

			const size_t groupCount = prebuiltData.groups.size();
			for (uint32_t groupIndex = 0; groupIndex < static_cast<uint32_t>(groupCount); ++groupIndex) {
				const pxr::SdfPath groupPrimPath(GroupPrimPathString(groupIndex));
				auto groupPrim = stage->DefinePrim(groupPrimPath, pxr::TfToken("Scope"));
				if (!groupPrim) {
					return false;
				}
				groupPrim.CreateAttribute(pxr::TfToken("groupIndex"), pxr::SdfValueTypeNames->UInt, true)
					.Set(groupIndex);
			}

			return stage->GetRootLayer()->Save();
		}
	}

	uint64_t ComputeBuildConfigHash()
	{
		size_t seed = 0;
		boost::hash_combine(seed, static_cast<uint32_t>(kSchemaVersion));
		boost::hash_combine(seed, static_cast<uint32_t>(64)); // MS_MESHLET_SIZE expectation
		boost::hash_combine(seed, static_cast<uint32_t>(32)); // target bucket clusters
		boost::hash_combine(seed, static_cast<uint32_t>(4));  // max group children
		boost::hash_combine(seed, static_cast<uint32_t>(4));  // traversal node fanout
		boost::hash_combine(seed, static_cast<uint32_t>(1));  // compressed group position bitstream enabled
		boost::hash_combine(seed, static_cast<uint32_t>(1));  // compressed group normal stream enabled
		boost::hash_combine(seed, static_cast<uint32_t>(1));  // compressed meshlet vertex index bitstream enabled
		boost::hash_combine(seed, static_cast<uint32_t>(1));  // mesh quantization heuristic version
		return static_cast<uint64_t>(seed);
	}

	std::wstring BuildCacheFileName(const CacheKey& key, uint64_t buildConfigHash)
	{
		size_t hashSeed = 0;
		boost::hash_combine(hashSeed, key.sourceIdentifier);
		boost::hash_combine(hashSeed, key.primPath);
		boost::hash_combine(hashSeed, key.subsetName);
		boost::hash_combine(hashSeed, buildConfigHash);

		std::stringstream ss;
		ss << "clod_" << std::hex << hashSeed << ".usdc";
		return s2ws(ss.str());
	}

	std::optional<CacheData> TryLoad(const CacheKey& key, uint64_t expectedBuildConfigHash)
	{
		const std::wstring fileName = BuildCacheFileName(key, expectedBuildConfigHash);
		const std::wstring cachePath = GetCacheFilePathBySource(fileName, key.sourceIdentifier);
		if (!std::filesystem::exists(cachePath)) {
			return std::nullopt;
		}

		auto stage = pxr::UsdStage::Open(ws2s(cachePath), pxr::UsdStage::LoadNone);
		if (!stage) {
			spdlog::warn("CLod cache exists but failed to open: {}", ws2s(cachePath));
			return std::nullopt;
		}

		pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath(kRootPrimPath));
		if (!root) {
			return std::nullopt;
		}

		CacheData out;
		int authoredSchema = 0;
		if (!root.GetAttribute(pxr::TfToken("clodSchemaVersion")).Get(&authoredSchema)) {
			return std::nullopt;
		}
		if (authoredSchema != static_cast<int>(kSchemaVersion)) {
			return std::nullopt;
		}

		int64_t authoredBuildHash = 0;
		if (!root.GetAttribute(pxr::TfToken("clodBuildConfigHash")).Get(&authoredBuildHash)) {
			return std::nullopt;
		}
		if (static_cast<uint64_t>(authoredBuildHash) != expectedBuildConfigHash) {
			return std::nullopt;
		}

		pxr::VtArray<unsigned char> blobData;
		if (!root.GetAttribute(pxr::TfToken("clodBlob")).Get(&blobData)) {
			return std::nullopt;
		}

		auto bytes = ToBytes(blobData);
		if (!DeserializeMetadata(bytes, out)) {
			spdlog::warn("Failed to deserialize CLod cache blob: {}", ws2s(cachePath));
			return std::nullopt;
		}

		if (out.buildConfigHash != expectedBuildConfigHash || out.schemaVersion != kSchemaVersion) {
			return std::nullopt;
		}

		if (out.prebuiltData.cacheSource.sourceIdentifier.empty()) {
			out.prebuiltData.cacheSource.sourceIdentifier = key.sourceIdentifier;
		}
		if (out.prebuiltData.cacheSource.primPath.empty()) {
			out.prebuiltData.cacheSource.primPath = key.primPath;
		}
		if (out.prebuiltData.cacheSource.subsetName.empty()) {
			out.prebuiltData.cacheSource.subsetName = key.subsetName;
		}
		if (out.prebuiltData.cacheSource.buildConfigHash == 0) {
			out.prebuiltData.cacheSource.buildConfigHash = expectedBuildConfigHash;
		}
		if (out.prebuiltData.cacheSource.containerFileName.empty()) {
			out.prebuiltData.cacheSource.containerFileName = BuildGroupContainerFileName(key, expectedBuildConfigHash);
		}

		const uint32_t groupCount = static_cast<uint32_t>(out.prebuiltData.groups.size());
		const bool hasContainerLocators = (out.prebuiltData.groupDiskLocators.size() == groupCount);
		if (hasContainerLocators) {
			return out;
		}

		spdlog::warn(
			"CLod cache '{}' is missing disk locator metadata for {} groups; treating as cache miss.",
			ws2s(cachePath),
			groupCount);
		return std::nullopt;
	}

	bool Save(const CacheKey& key, const CacheData& data)
	{
		if (data.schemaVersion != kSchemaVersion) {
			return false;
		}
		ClusterLODCacheBuildPayload payload{};
		return SaveImpl(key, data.buildConfigHash, data.prebuiltData, payload);
	}

	bool Save(const CacheKey& key, uint64_t buildConfigHash, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload)
	{
		return SaveImpl(key, buildConfigHash, prebuiltData, payload);
	}

	bool LoadGroupPayload(const CacheData& cacheData, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload)
	{
		const auto& prebuilt = cacheData.prebuiltData;
		if (groupLocalIndex >= prebuilt.groupDiskLocators.size()) {
			return false;
		}
		return LoadGroupPayload(prebuilt.cacheSource, groupLocalIndex, outPayload);
	}

	bool LoadGroupPayload(const ClusterLODCacheSource& cacheSource, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload)
	{
		if (cacheSource.containerFileName.empty()) {
			return false;
		}

		const std::wstring containerPath = GetCacheFilePathBySource(cacheSource.containerFileName, cacheSource.sourceIdentifier);
		std::ifstream file(containerPath, std::ios::binary);
		if (!file.is_open()) {
			return false;
		}

		ContainerHeader header{};
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!file.good() || header.magic != kContainerMagic || header.version != 3u) {
			return false;
		}

		if (groupLocalIndex >= header.groupCount) {
			return false;
		}

		const uint64_t directoryEntryOffset = static_cast<uint64_t>(sizeof(ContainerHeader)) + static_cast<uint64_t>(groupLocalIndex) * static_cast<uint64_t>(sizeof(ClusterLODGroupDiskLocator));
		if (directoryEntryOffset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)())) {
			return false;
		}

		file.seekg(static_cast<std::streamoff>(directoryEntryOffset), std::ios::beg);
		if (!file.good()) {
			return false;
		}

		ClusterLODGroupDiskLocator groupDiskLocator{};
		file.read(reinterpret_cast<char*>(&groupDiskLocator), sizeof(groupDiskLocator));
		if (!file.good() || groupDiskLocator.blobSizeBytes < sizeof(GroupPayloadHeader)) {
			return false;
		}

		if (groupDiskLocator.blobOffset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)())) {
			return false;
		}
		file.seekg(static_cast<std::streamoff>(groupDiskLocator.blobOffset), std::ios::beg);
		if (!file.good()) {
			return false;
		}

		GroupPayloadHeader groupHeader{};
		file.read(reinterpret_cast<char*>(&groupHeader), sizeof(groupHeader));
		if (!file.good()) {
			return false;
		}

		outPayload.groupChunkMetadata = groupHeader.groupChunkMetadata;

		const uint32_t pageCount = groupHeader.pageCount;
		const uint64_t sizeTableBytes = static_cast<uint64_t>(pageCount) * sizeof(uint32_t);

		// Read per-page blob sizes
		std::vector<uint32_t> pageBlobSizes(pageCount);
		if (pageCount > 0) {
			file.read(reinterpret_cast<char*>(pageBlobSizes.data()),
				static_cast<std::streamsize>(sizeTableBytes));
			if (!file.good()) return false;
		}

		// Validate total size
		uint64_t totalBlobBytes = sizeof(GroupPayloadHeader) + sizeTableBytes;
		for (uint32_t s : pageBlobSizes) totalBlobBytes += s;
		if (totalBlobBytes != static_cast<uint64_t>(groupDiskLocator.blobSizeBytes)) {
			return false;
		}

		// Read page blobs
		outPayload.pageBlobs.resize(pageCount);
		for (uint32_t pi = 0; pi < pageCount; ++pi) {
			if (!ReadVectorRaw(file, pageBlobSizes[pi], outPayload.pageBlobs[pi])) return false;
		}

		return true;
	}

	bool LoadGroupPayloadDirect(std::ifstream& file,
		const ClusterLODGroupDiskLocator& locator,
		LoadedGroupPayload& outPayload)
	{
		if (locator.blobSizeBytes < sizeof(GroupPayloadHeader)) {
			return false;
		}

		if (locator.blobOffset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)())) {
			return false;
		}

		file.seekg(static_cast<std::streamoff>(locator.blobOffset), std::ios::beg);
		if (!file.good()) {
			return false;
		}

		GroupPayloadHeader groupHeader{};
		file.read(reinterpret_cast<char*>(&groupHeader), sizeof(groupHeader));
		if (!file.good()) {
			return false;
		}

		outPayload.groupChunkMetadata = groupHeader.groupChunkMetadata;

		const uint32_t pageCount = groupHeader.pageCount;
		const uint64_t sizeTableBytes = static_cast<uint64_t>(pageCount) * sizeof(uint32_t);

		std::vector<uint32_t> pageBlobSizes(pageCount);
		if (pageCount > 0) {
			file.read(reinterpret_cast<char*>(pageBlobSizes.data()),
				static_cast<std::streamsize>(sizeTableBytes));
			if (!file.good()) return false;
		}

		uint64_t totalBlobBytes = sizeof(GroupPayloadHeader) + sizeTableBytes;
		for (uint32_t s : pageBlobSizes) totalBlobBytes += s;
		if (totalBlobBytes != static_cast<uint64_t>(locator.blobSizeBytes)) {
			return false;
		}

		outPayload.pageBlobs.resize(pageCount);
		for (uint32_t pi = 0; pi < pageCount; ++pi) {
			if (!ReadVectorRaw(file, pageBlobSizes[pi], outPayload.pageBlobs[pi])) return false;
		}

		return true;
	}

	bool LoadGroupPayloadSelective(std::ifstream& file,
		const ClusterLODGroupDiskLocator& locator,
		const std::vector<bool>& segmentNeedsFetch,
		LoadedGroupPayload& outPayload)
	{
		// Fall back to full read if no skip mask provided.
		if (segmentNeedsFetch.empty()) {
			return LoadGroupPayloadDirect(file, locator, outPayload);
		}

		if (locator.blobSizeBytes < sizeof(GroupPayloadHeader)) {
			return false;
		}

		if (locator.blobOffset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)())) {
			return false;
		}

		file.seekg(static_cast<std::streamoff>(locator.blobOffset), std::ios::beg);
		if (!file.good()) {
			return false;
		}

		GroupPayloadHeader groupHeader{};
		file.read(reinterpret_cast<char*>(&groupHeader), sizeof(groupHeader));
		if (!file.good()) {
			return false;
		}

		outPayload.groupChunkMetadata = groupHeader.groupChunkMetadata;

		const uint32_t pageCount = groupHeader.pageCount;
		const uint64_t sizeTableBytes = static_cast<uint64_t>(pageCount) * sizeof(uint32_t);

		std::vector<uint32_t> pageBlobSizes(pageCount);
		if (pageCount > 0) {
			file.read(reinterpret_cast<char*>(pageBlobSizes.data()),
				static_cast<std::streamsize>(sizeTableBytes));
			if (!file.good()) return false;
		}

		uint64_t totalBlobBytes = sizeof(GroupPayloadHeader) + sizeTableBytes;
		for (uint32_t s : pageBlobSizes) totalBlobBytes += s;
		if (totalBlobBytes != static_cast<uint64_t>(locator.blobSizeBytes)) {
			return false;
		}

		outPayload.pageBlobs.resize(pageCount);
		for (uint32_t pi = 0; pi < pageCount; ++pi) {
			if (pi < static_cast<uint32_t>(segmentNeedsFetch.size()) && !segmentNeedsFetch[pi]) {
				// Skip this blob — seek forward.
				if (pageBlobSizes[pi] > 0) {
					file.seekg(static_cast<std::streamoff>(pageBlobSizes[pi]), std::ios::cur);
					if (!file.good()) return false;
				}
				// Leave outPayload.pageBlobs[pi] empty.
			} else {
				if (!ReadVectorRaw(file, pageBlobSizes[pi], outPayload.pageBlobs[pi])) return false;
			}
		}

		return true;
	}

	bool OpenContainerFile(const ClusterLODCacheSource& cacheSource,
		std::ifstream& outFile,
		uint32_t& outGroupCount)
	{
		outGroupCount = 0u;
		if (cacheSource.containerFileName.empty()) {
			return false;
		}

		const std::wstring containerPath = GetCacheFilePathBySource(cacheSource.containerFileName, cacheSource.sourceIdentifier);
		outFile.open(containerPath, std::ios::binary);
		if (!outFile.is_open()) {
			return false;
		}

		ContainerHeader header{};
		outFile.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!outFile.good() || header.magic != kContainerMagic || header.version != 3u) {
			outFile.close();
			return false;
		}

		outGroupCount = header.groupCount;
		return true;
	}

}
