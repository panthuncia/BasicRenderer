#include "Import/CLodCache.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

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

#include "Utilities/Utilities.h"

namespace CLodCache {

	namespace {
		static constexpr const char* kRootPrimPath = "/CLodCache";
		static constexpr const char* kGroupsPrimPath = "/CLodCache/Groups";

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

		std::vector<std::byte> SerializeMetadata(const CacheData& data)
		{
			std::vector<std::byte> out;
			WritePod(out, data.schemaVersion);
			WritePod(out, data.buildConfigHash);

			WriteVectorPod(out, data.prebuiltData.groups);
			WriteVectorPod(out, data.prebuiltData.meshlets);
			WriteVectorPod(out, data.prebuiltData.meshletVertices);
			WriteVectorPod(out, data.prebuiltData.meshletTriangles);
			WriteVectorPod(out, data.prebuiltData.meshletBounds);
			WriteVectorPod(out, data.prebuiltData.children);
			WriteVectorPod(out, data.prebuiltData.duplicatedVertices);
			WriteVectorPod(out, data.prebuiltData.duplicatedSkinningVertices);
			WriteVectorPod(out, data.prebuiltData.groupChunks);
			WriteVectorPod(out, data.prebuiltData.groupDiskSpans);
			WriteString(out, data.prebuiltData.cacheSource.sourceIdentifier);
			WriteString(out, data.prebuiltData.cacheSource.primPath);
			WriteString(out, data.prebuiltData.cacheSource.subsetName);
			WritePod(out, data.prebuiltData.cacheSource.buildConfigHash);
			WriteString(out, ws2s(data.prebuiltData.cacheSource.containerFileName));
			WriteVectorPod(out, data.prebuiltData.nodes);
			return out;
		}

		bool DeserializeMetadata(const std::vector<std::byte>& blob, CacheData& out)
		{
			size_t offset = 0;
			if (!ReadPod(blob, offset, out.schemaVersion)) return false;
			if (!ReadPod(blob, offset, out.buildConfigHash)) return false;

			if (!ReadVectorPod(blob, offset, out.prebuiltData.groups)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.meshlets)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.meshletVertices)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.meshletTriangles)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.meshletBounds)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.children)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.duplicatedVertices)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.duplicatedSkinningVertices)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.groupChunks)) return false;
			if (!ReadVectorPod(blob, offset, out.prebuiltData.groupDiskSpans)) return false;
			if (!ReadString(blob, offset, out.prebuiltData.cacheSource.sourceIdentifier)) return false;
			if (!ReadString(blob, offset, out.prebuiltData.cacheSource.primPath)) return false;
			if (!ReadString(blob, offset, out.prebuiltData.cacheSource.subsetName)) return false;
			if (!ReadPod(blob, offset, out.prebuiltData.cacheSource.buildConfigHash)) return false;
			std::string containerFileName;
			if (!ReadString(blob, offset, containerFileName)) return false;
			out.prebuiltData.cacheSource.containerFileName = s2ws(containerFileName);
			if (!ReadVectorPod(blob, offset, out.prebuiltData.nodes)) return false;

			const size_t groupCount = out.prebuiltData.groupChunks.size();
			out.prebuiltData.groupVertexChunks.assign(groupCount, {});
			out.prebuiltData.groupSkinningVertexChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletVertexChunks.assign(groupCount, {});
			out.prebuiltData.groupCompressedPositionWordChunks.assign(groupCount, {});
			out.prebuiltData.groupCompressedNormalWordChunks.assign(groupCount, {});
			out.prebuiltData.groupCompressedMeshletVertexWordChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletTriangleChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletBoundsChunks.assign(groupCount, {});

			return offset == blob.size();
		}

		static constexpr uint32_t kContainerMagic = 0x444F4C43u; // CLOD

		struct ContainerHeader {
			uint32_t magic = kContainerMagic;
			uint32_t version = 1;
			uint32_t reserved = 0;
			uint32_t groupCount = 0;
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
		void WriteSpanToFile(std::ofstream& file, const std::vector<T>& values, ClusterLODDiskChunkSpan& outSpan)
		{
			outSpan.offset = static_cast<uint64_t>(file.tellp());
			outSpan.sizeBytes = static_cast<uint64_t>(values.size() * sizeof(T));
			if (!values.empty()) {
				file.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(outSpan.sizeBytes));
			}
		}

		bool SaveContainerPayload(
			const std::wstring& containerPath,
			const CacheData& data,
			std::vector<ClusterLODGroupDiskSpans>& outSpans)
		{
			const uint32_t groupCount = static_cast<uint32_t>(data.prebuiltData.groupChunks.size());
			outSpans.assign(groupCount, {});

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

			for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
				const std::vector<std::byte> emptyBytes;
				const std::vector<uint32_t> emptyU32;
				const std::vector<meshopt_Meshlet> emptyMeshlets;
				const std::vector<uint8_t> emptyU8;
				const std::vector<BoundingSphere> emptyBounds;
				const auto& vertexChunk = (groupIndex < data.prebuiltData.groupVertexChunks.size()) ? data.prebuiltData.groupVertexChunks[groupIndex] : emptyBytes;
				const auto& skinningChunk = (groupIndex < data.prebuiltData.groupSkinningVertexChunks.size()) ? data.prebuiltData.groupSkinningVertexChunks[groupIndex] : emptyBytes;
				const auto& meshletVertexChunk = (groupIndex < data.prebuiltData.groupMeshletVertexChunks.size()) ? data.prebuiltData.groupMeshletVertexChunks[groupIndex] : emptyU32;
				const auto& compressedPositionWordChunk = (groupIndex < data.prebuiltData.groupCompressedPositionWordChunks.size()) ? data.prebuiltData.groupCompressedPositionWordChunks[groupIndex] : emptyU32;
				const auto& compressedNormalWordChunk = (groupIndex < data.prebuiltData.groupCompressedNormalWordChunks.size()) ? data.prebuiltData.groupCompressedNormalWordChunks[groupIndex] : emptyU32;
				const auto& compressedMeshletVertexWordChunk = (groupIndex < data.prebuiltData.groupCompressedMeshletVertexWordChunks.size()) ? data.prebuiltData.groupCompressedMeshletVertexWordChunks[groupIndex] : emptyU32;
				const auto& meshletChunk = (groupIndex < data.prebuiltData.groupMeshletChunks.size()) ? data.prebuiltData.groupMeshletChunks[groupIndex] : emptyMeshlets;
				const auto& meshletTriangleChunk = (groupIndex < data.prebuiltData.groupMeshletTriangleChunks.size()) ? data.prebuiltData.groupMeshletTriangleChunks[groupIndex] : emptyU8;
				const auto& meshletBoundsChunk = (groupIndex < data.prebuiltData.groupMeshletBoundsChunks.size()) ? data.prebuiltData.groupMeshletBoundsChunks[groupIndex] : emptyBounds;

				auto& spans = outSpans[groupIndex];
				WriteSpanToFile(file, vertexChunk, spans.vertexChunk);
				WriteSpanToFile(file, skinningChunk, spans.skinningChunk);
				WriteSpanToFile(file, meshletVertexChunk, spans.meshletVertexChunk);
				WriteSpanToFile(file, compressedPositionWordChunk, spans.compressedPositionWordChunk);
				WriteSpanToFile(file, compressedNormalWordChunk, spans.compressedNormalWordChunk);
				WriteSpanToFile(file, compressedMeshletVertexWordChunk, spans.compressedMeshletVertexWordChunk);
				WriteSpanToFile(file, meshletChunk, spans.meshletChunk);
				WriteSpanToFile(file, meshletTriangleChunk, spans.meshletTriangleChunk);
				WriteSpanToFile(file, meshletBoundsChunk, spans.meshletBoundsChunk);
			}

			return file.good();
		}

		template<typename T>
		bool ReadSpanFromFile(std::ifstream& file, const ClusterLODDiskChunkSpan& span, std::vector<T>& outValues)
		{
			if ((span.sizeBytes % sizeof(T)) != 0) {
				return false;
			}
			outValues.resize(static_cast<size_t>(span.sizeBytes / sizeof(T)));
			if (outValues.empty()) {
				return true;
			}

			file.seekg(static_cast<std::streamoff>(span.offset), std::ios::beg);
			if (!file.good()) {
				return false;
			}
			file.read(reinterpret_cast<char*>(outValues.data()), static_cast<std::streamsize>(span.sizeBytes));
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
			const std::wstring groupCachePath = GetCacheFilePath(groupFileName, L"clod");

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
		const std::wstring cachePath = GetCacheFilePath(fileName, L"clod");
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

		const uint32_t groupCount = static_cast<uint32_t>(out.prebuiltData.groupChunks.size());
		const bool hasContainerSpans = (out.prebuiltData.groupDiskSpans.size() == groupCount) && groupCount > 0;
		if (hasContainerSpans) {
			return out;
		}

		out.prebuiltData.groupDiskSpans.assign(groupCount, {});
		for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
			const pxr::SdfPath groupPrimPath(GroupPrimPathString(groupIndex));
			stage->Load(groupPrimPath);

			pxr::UsdPrim groupPrim = stage->GetPrimAtPath(groupPrimPath);
			if (!groupPrim) {
				stage->Unload(groupPrimPath);
				continue;
			}

			pxr::VtArray<unsigned char> vertexChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupVertexChunk")).Get(&vertexChunk)) {
				out.prebuiltData.groupVertexChunks[groupIndex] = ToBytes(vertexChunk);
			}

			pxr::VtArray<unsigned char> skinningChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupSkinningVertexChunk")).Get(&skinningChunk)) {
				out.prebuiltData.groupSkinningVertexChunks[groupIndex] = ToBytes(skinningChunk);
			}

			pxr::VtArray<uint32_t> meshletVertexChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupMeshletVertexChunk")).Get(&meshletVertexChunk)) {
				out.prebuiltData.groupMeshletVertexChunks[groupIndex].assign(meshletVertexChunk.begin(), meshletVertexChunk.end());
			}

			pxr::VtArray<uint32_t> compressedPositionWordChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupCompressedPositionWordChunk")).Get(&compressedPositionWordChunk)) {
				out.prebuiltData.groupCompressedPositionWordChunks[groupIndex].assign(compressedPositionWordChunk.begin(), compressedPositionWordChunk.end());
			}

			pxr::VtArray<uint32_t> compressedNormalWordChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupCompressedNormalWordChunk")).Get(&compressedNormalWordChunk)) {
				out.prebuiltData.groupCompressedNormalWordChunks[groupIndex].assign(compressedNormalWordChunk.begin(), compressedNormalWordChunk.end());
			}

			pxr::VtArray<uint32_t> compressedMeshletVertexWordChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupCompressedMeshletVertexWordChunk")).Get(&compressedMeshletVertexWordChunk)) {
				out.prebuiltData.groupCompressedMeshletVertexWordChunks[groupIndex].assign(compressedMeshletVertexWordChunk.begin(), compressedMeshletVertexWordChunk.end());
			}

			pxr::VtArray<unsigned char> meshletChunkBytes;
			if (groupPrim.GetAttribute(pxr::TfToken("groupMeshletChunk")).Get(&meshletChunkBytes)) {
				const std::vector<std::byte> bytes = ToBytes(meshletChunkBytes);
				const size_t count = bytes.size() / sizeof(meshopt_Meshlet);
				out.prebuiltData.groupMeshletChunks[groupIndex].resize(count);
				if (count > 0) {
					std::memcpy(out.prebuiltData.groupMeshletChunks[groupIndex].data(), bytes.data(), count * sizeof(meshopt_Meshlet));
				}
			}

			pxr::VtArray<unsigned char> meshletTriangleChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupMeshletTriangleChunk")).Get(&meshletTriangleChunk)) {
				out.prebuiltData.groupMeshletTriangleChunks[groupIndex].assign(meshletTriangleChunk.begin(), meshletTriangleChunk.end());
			}

			pxr::VtArray<unsigned char> meshletBoundsChunk;
			if (groupPrim.GetAttribute(pxr::TfToken("groupMeshletBoundsChunk")).Get(&meshletBoundsChunk)) {
				const std::vector<std::byte> bytes = ToBytes(meshletBoundsChunk);
				const size_t count = bytes.size() / sizeof(BoundingSphere);
				out.prebuiltData.groupMeshletBoundsChunks[groupIndex].resize(count);
				if (count > 0) {
					std::memcpy(out.prebuiltData.groupMeshletBoundsChunks[groupIndex].data(), bytes.data(), count * sizeof(BoundingSphere));
				}
			}

			stage->Unload(groupPrimPath);
		}

		return out;
	}

	bool Save(const CacheKey& key, const CacheData& data)
	{
		const std::wstring fileName = BuildCacheFileName(key, data.buildConfigHash);
		const std::wstring cachePath = GetCacheFilePath(fileName, L"clod");
		const std::wstring containerFileName = BuildGroupContainerFileName(key, data.buildConfigHash);
		const std::wstring containerPath = GetCacheFilePath(containerFileName, L"clod");

		std::vector<ClusterLODGroupDiskSpans> groupDiskSpans;
		if (!SaveContainerPayload(containerPath, data, groupDiskSpans)) {
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
			.Set(static_cast<int>(data.schemaVersion));
		prim.CreateAttribute(pxr::TfToken("clodBuildConfigHash"), pxr::SdfValueTypeNames->Int64, true)
			.Set(static_cast<int64_t>(data.buildConfigHash));

		CacheData metadataOnly = data;
		metadataOnly.prebuiltData.groupVertexChunks.clear();
		metadataOnly.prebuiltData.groupSkinningVertexChunks.clear();
		metadataOnly.prebuiltData.groupMeshletVertexChunks.clear();
		metadataOnly.prebuiltData.groupCompressedPositionWordChunks.clear();
		metadataOnly.prebuiltData.groupCompressedNormalWordChunks.clear();
		metadataOnly.prebuiltData.groupCompressedMeshletVertexWordChunks.clear();
		metadataOnly.prebuiltData.groupMeshletChunks.clear();
		metadataOnly.prebuiltData.groupMeshletTriangleChunks.clear();
		metadataOnly.prebuiltData.groupMeshletBoundsChunks.clear();
		metadataOnly.prebuiltData.groupDiskSpans = std::move(groupDiskSpans);
		metadataOnly.prebuiltData.cacheSource.sourceIdentifier = key.sourceIdentifier;
		metadataOnly.prebuiltData.cacheSource.primPath = key.primPath;
		metadataOnly.prebuiltData.cacheSource.subsetName = key.subsetName;
		metadataOnly.prebuiltData.cacheSource.buildConfigHash = data.buildConfigHash;
		metadataOnly.prebuiltData.cacheSource.containerFileName = containerFileName;

		auto blob = SerializeMetadata(metadataOnly);
		auto vtBlob = ToVtUChar(blob);
		prim.CreateAttribute(pxr::TfToken("clodBlob"), pxr::SdfValueTypeNames->UCharArray, true)
			.Set(vtBlob);

		const size_t groupCount = data.prebuiltData.groupChunks.size();
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

	bool LoadGroupPayload(const CacheData& cacheData, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload)
	{
		const auto& prebuilt = cacheData.prebuiltData;
		if (groupLocalIndex >= prebuilt.groupDiskSpans.size()) {
			return false;
		}

		if (prebuilt.cacheSource.containerFileName.empty()) {
			return false;
		}

		const std::wstring containerPath = GetCacheFilePath(prebuilt.cacheSource.containerFileName, L"clod");
		std::ifstream file(containerPath, std::ios::binary);
		if (!file.is_open()) {
			return false;
		}

		ContainerHeader header{};
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!file.good() || header.magic != kContainerMagic || header.version != 1u) {
			return false;
		}

		if (groupLocalIndex >= header.groupCount) {
			return false;
		}

		const auto& spans = prebuilt.groupDiskSpans[groupLocalIndex];
		if (!ReadSpanFromFile(file, spans.vertexChunk, outPayload.vertexChunk)) return false;
		if (!ReadSpanFromFile(file, spans.skinningChunk, outPayload.skinningChunk)) return false;
		if (!ReadSpanFromFile(file, spans.meshletVertexChunk, outPayload.meshletVertexChunk)) return false;
		if (!ReadSpanFromFile(file, spans.compressedPositionWordChunk, outPayload.compressedPositionWordChunk)) return false;
		if (!ReadSpanFromFile(file, spans.compressedNormalWordChunk, outPayload.compressedNormalWordChunk)) return false;
		if (!ReadSpanFromFile(file, spans.compressedMeshletVertexWordChunk, outPayload.compressedMeshletVertexWordChunk)) return false;
		if (!ReadSpanFromFile(file, spans.meshletChunk, outPayload.meshletChunk)) return false;
		if (!ReadSpanFromFile(file, spans.meshletTriangleChunk, outPayload.meshletTriangleChunk)) return false;
		if (!ReadSpanFromFile(file, spans.meshletBoundsChunk, outPayload.meshletBoundsChunk)) return false;

		return true;
	}

}
