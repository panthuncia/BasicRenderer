#include "Import/CLodCache.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
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
			if (!ReadVectorPod(blob, offset, out.prebuiltData.nodes)) return false;

			const size_t groupCount = out.prebuiltData.groupChunks.size();
			out.prebuiltData.groupVertexChunks.assign(groupCount, {});
			out.prebuiltData.groupSkinningVertexChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletVertexChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletTriangleChunks.assign(groupCount, {});
			out.prebuiltData.groupMeshletBoundsChunks.assign(groupCount, {});

			return offset == blob.size();
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

		const uint32_t groupCount = static_cast<uint32_t>(out.prebuiltData.groupChunks.size());
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
		metadataOnly.prebuiltData.groupMeshletChunks.clear();
		metadataOnly.prebuiltData.groupMeshletTriangleChunks.clear();
		metadataOnly.prebuiltData.groupMeshletBoundsChunks.clear();

		auto blob = SerializeMetadata(metadataOnly);
		auto vtBlob = ToVtUChar(blob);
		prim.CreateAttribute(pxr::TfToken("clodBlob"), pxr::SdfValueTypeNames->UCharArray, true)
			.Set(vtBlob);

		const size_t groupCount = data.prebuiltData.groupChunks.size();
		for (uint32_t groupIndex = 0; groupIndex < static_cast<uint32_t>(groupCount); ++groupIndex) {
			const std::vector<std::byte> emptyBytes;
			const std::vector<uint32_t> emptyU32;
			const std::vector<meshopt_Meshlet> emptyMeshlets;
			const std::vector<uint8_t> emptyU8;
			const std::vector<BoundingSphere> emptyBounds;
			const auto& vertexChunk = (groupIndex < data.prebuiltData.groupVertexChunks.size()) ? data.prebuiltData.groupVertexChunks[groupIndex] : emptyBytes;
			const auto& skinningChunk = (groupIndex < data.prebuiltData.groupSkinningVertexChunks.size()) ? data.prebuiltData.groupSkinningVertexChunks[groupIndex] : emptyBytes;
			const auto& meshletVertexChunk = (groupIndex < data.prebuiltData.groupMeshletVertexChunks.size()) ? data.prebuiltData.groupMeshletVertexChunks[groupIndex] : emptyU32;
			const auto& meshletChunk = (groupIndex < data.prebuiltData.groupMeshletChunks.size()) ? data.prebuiltData.groupMeshletChunks[groupIndex] : emptyMeshlets;
			const auto& meshletTriangleChunk = (groupIndex < data.prebuiltData.groupMeshletTriangleChunks.size()) ? data.prebuiltData.groupMeshletTriangleChunks[groupIndex] : emptyU8;
			const auto& meshletBoundsChunk = (groupIndex < data.prebuiltData.groupMeshletBoundsChunks.size()) ? data.prebuiltData.groupMeshletBoundsChunks[groupIndex] : emptyBounds;

			if (!SaveGroupPayloadLayer(key, data.buildConfigHash, groupIndex, vertexChunk, skinningChunk, meshletVertexChunk, meshletChunk, meshletTriangleChunk, meshletBoundsChunk)) {
				spdlog::warn("Failed to save CLod group payload {}", groupIndex);
				return false;
			}

			const pxr::SdfPath groupPrimPath(GroupPrimPathString(groupIndex));
			auto groupPrim = stage->DefinePrim(groupPrimPath, pxr::TfToken("Scope"));
			if (!groupPrim) {
				return false;
			}

			const std::wstring groupFileName = BuildGroupPayloadFileName(key, data.buildConfigHash, groupIndex);
			groupPrim.GetPayloads().AddPayload(ws2s(groupFileName), pxr::SdfPath("/GroupPayload"));
		}

		return stage->GetRootLayer()->Save();
	}

}
