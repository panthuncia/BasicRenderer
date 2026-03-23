#include "Mesh/ClusterLODUtilities.h"
#include "Mesh/VoxelGroupBuilder.h"

#include <limits>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <bit>
#include <cmath>
#include <array>
#include <cstring>
#include <mutex>
#include <atomic>
#include <cassert>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Mesh/VertexFlags.h"
#include "Utilities/mikktspace.h"

#include "../shaders/Common/defines.h"

namespace
{
	constexpr uint32_t CLOD_COMPRESSED_POSITIONS = 1u << 0;
	constexpr uint32_t CLOD_COMPRESSED_MESHLET_VERTEX_INDICES = 1u << 1;
	constexpr uint32_t CLOD_COMPRESSED_NORMALS = 1u << 2;
	constexpr uint32_t kMaxSkinInfluences = 8u;
	constexpr float CLOD_UV_QUANTIZATION_SCALE = 65535.0f;
	constexpr float CLOD_UV_QUANTIZATION_INV_SCALE = 1.0f / CLOD_UV_QUANTIZATION_SCALE;

	struct PackedSkinningInfluences
	{
		DirectX::XMUINT4 joints0{ 0, 0, 0, 0 };
		DirectX::XMUINT4 joints1{ 0, 0, 0, 0 };
		DirectX::XMFLOAT4 weights0{ 0, 0, 0, 0 };
		DirectX::XMFLOAT4 weights1{ 0, 0, 0, 0 };
	};

	uint32_t BitsNeededForRange(uint32_t range)
	{
		if (range == 0)
		{
			return 1;
		}
		return 32u - static_cast<uint32_t>(std::countl_zero(range));
	}

	uint32_t ReadBits(const std::vector<uint32_t>& words, uint64_t& bitCursor, uint32_t bitCount)
	{
		if (bitCount == 0) return 0;
		const uint64_t bitOffset = bitCursor & 31ull;
		const uint64_t wordIndex = bitCursor >> 5ull;
		const uint64_t mask = (bitCount >= 32u) ? 0xffffffffull : ((1ull << bitCount) - 1ull);
		uint32_t value = (words[static_cast<size_t>(wordIndex)] >> static_cast<uint32_t>(bitOffset)) & static_cast<uint32_t>(mask);
		const uint32_t spillBits = static_cast<uint32_t>(bitOffset) + bitCount;
		if (spillBits > 32u) {
			value |= (words[static_cast<size_t>(wordIndex + 1ull)] << (32u - static_cast<uint32_t>(bitOffset))) & static_cast<uint32_t>(mask);
		}
		bitCursor += bitCount;
		return value;
	}

	void AppendBits(std::vector<uint32_t>& words, uint64_t& bitCursor, uint32_t value, uint32_t bitCount)
	{
		if (bitCount == 0)
		{
			return;
		}

		const uint64_t requiredBits = bitCursor + bitCount;
		const size_t requiredWords = static_cast<size_t>((requiredBits + 31ull) / 32ull);
		if (words.size() < requiredWords)
		{
			words.resize(requiredWords, 0u);
		}

		const uint64_t bitOffset = bitCursor & 31ull;
		const uint64_t wordIndex = bitCursor >> 5ull;
		const uint64_t mask = (bitCount >= 32u) ? 0xffffffffull : ((1ull << bitCount) - 1ull);
		const uint64_t clampedValue = static_cast<uint64_t>(value) & mask;
		words[static_cast<size_t>(wordIndex)] |= static_cast<uint32_t>(clampedValue << bitOffset);

		const uint32_t spillBits = static_cast<uint32_t>(bitOffset) + bitCount;
		if (spillBits > 32u)
		{
			if (words.size() <= static_cast<size_t>(wordIndex + 1ull))
			{
				words.resize(static_cast<size_t>(wordIndex + 2ull), 0u);
			}
			words[static_cast<size_t>(wordIndex + 1ull)] |= static_cast<uint32_t>(clampedValue >> (32u - static_cast<uint32_t>(bitOffset)));
		}

		bitCursor += bitCount;
	}

	// Compute the exact byte size of a packed page blob in the new SoA format:
	// Header(64) + Descriptors(64*N) + PositionBitstream + optional streams + TriangleStream
	size_t ComputePageBlobSize(
		uint32_t attributeMask,
		uint32_t meshletCount,
		uint32_t pageUvSetCount,
		uint64_t totalPositionBits,
		const std::vector<uint64_t>& totalUvBitsPerSet,
		uint32_t totalVertexCount,
		uint32_t totalNormalWords,
		uint32_t totalBoneIndexCount,
		uint32_t totalTriangleBytes)
	{
		auto align4 = [](size_t v) -> size_t { return (v + 3u) & ~size_t(3); };

		size_t size = CLOD_PAGE_HEADER_SIZE; // 64
		size = align4(size) + align4(static_cast<size_t>(meshletCount) * sizeof(CLodMeshletDescriptor));
		if (pageUvSetCount > 0u)
		{
			size = align4(size) + align4(static_cast<size_t>(meshletCount) * static_cast<size_t>(pageUvSetCount) * sizeof(CLodMeshletUvDescriptor));
		}
		size = align4(size) + align4(static_cast<size_t>((totalPositionBits + 31ull) / 32ull) * sizeof(uint32_t));
		if ((attributeMask & CLOD_PAGE_ATTRIBUTE_NORMAL) != 0u)
		{
			size = align4(size) + align4(static_cast<size_t>(totalNormalWords) * sizeof(uint32_t));
		}
		if ((attributeMask & CLOD_PAGE_ATTRIBUTE_JOINTS) != 0u)
		{
			size = align4(size) + align4(static_cast<size_t>(totalVertexCount) * sizeof(DirectX::XMUINT4) * 2u);
		}
		if ((attributeMask & CLOD_PAGE_ATTRIBUTE_WEIGHTS) != 0u)
		{
			size = align4(size) + align4(static_cast<size_t>(totalVertexCount) * sizeof(DirectX::XMFLOAT4) * 2u);
		}
		if (pageUvSetCount > 0u)
		{
			size = align4(size) + align4(static_cast<size_t>(pageUvSetCount) * sizeof(uint32_t));
			for (uint32_t uvSetIndex = 0; uvSetIndex < pageUvSetCount; ++uvSetIndex)
			{
				const uint64_t totalUvBits = uvSetIndex < totalUvBitsPerSet.size() ? totalUvBitsPerSet[uvSetIndex] : 0ull;
				size = align4(size) + align4(static_cast<size_t>((totalUvBits + 31ull) / 32ull) * sizeof(uint32_t));
			}
		}
		size = align4(size) + align4(static_cast<size_t>(totalBoneIndexCount) * sizeof(uint32_t));
		size = align4(size) + align4(static_cast<size_t>(totalTriangleBytes));
		return align4(size);
	}

	uint32_t ComputeMeshQuantizationExponent(const std::vector<std::byte>& vertices, size_t vertexStrideBytes)
	{
		if (vertices.empty() || vertexStrideBytes < sizeof(float) * 3)
		{
			return 10u;
		}

		DirectX::XMFLOAT3 minv{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
		DirectX::XMFLOAT3 maxv{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
		const size_t vertexCount = vertices.size() / vertexStrideBytes;
		for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
		{
			const size_t byteOffset = vertexIndex * vertexStrideBytes;
			float px = 0.0f;
			float py = 0.0f;
			float pz = 0.0f;
			std::memcpy(&px, vertices.data() + byteOffset, sizeof(float));
			std::memcpy(&py, vertices.data() + byteOffset + sizeof(float), sizeof(float));
			std::memcpy(&pz, vertices.data() + byteOffset + sizeof(float) * 2, sizeof(float));

			minv.x = std::min(minv.x, px);
			minv.y = std::min(minv.y, py);
			minv.z = std::min(minv.z, pz);
			maxv.x = std::max(maxv.x, px);
			maxv.y = std::max(maxv.y, py);
			maxv.z = std::max(maxv.z, pz);
		}

		const float dx = maxv.x - minv.x;
		const float dy = maxv.y - minv.y;
		const float dz = maxv.z - minv.z;
		const float diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (diagonal < 1.0f) return 14u;
		if (diagonal < 10.0f) return 12u;
		if (diagonal < 100.0f) return 10u;
		return 8u;
	}

	std::array<float, 2> OctEncodeNormal(DirectX::XMFLOAT3 normal)
	{
		float nx = normal.x;
		float ny = normal.y;
		float nz = normal.z;
		const float denom = std::abs(nx) + std::abs(ny) + std::abs(nz);
		if (denom > 1e-8f)
		{
			nx /= denom;
			ny /= denom;
			nz /= denom;
		}

		if (nz < 0.0f)
		{
			const float ox = nx;
			nx = (1.0f - std::abs(ny)) * (ox >= 0.0f ? 1.0f : -1.0f);
			ny = (1.0f - std::abs(ox)) * (ny >= 0.0f ? 1.0f : -1.0f);
		}

		return { nx, ny };
	}

	int32_t QuantizeSnorm16(float value)
	{
		const float clamped = std::max(-1.0f, std::min(1.0f, value));
		const float scaled = std::round(clamped * 32767.0f);
		return static_cast<int32_t>(scaled);
	}

	uint32_t PackOctNormalSnorm16(const std::array<float, 2>& oct)
	{
		const uint16_t x = static_cast<uint16_t>(static_cast<int16_t>(QuantizeSnorm16(oct[0])));
		const uint16_t y = static_cast<uint16_t>(static_cast<int16_t>(QuantizeSnorm16(oct[1])));
		return static_cast<uint32_t>(x) | (static_cast<uint32_t>(y) << 16u);
	}

	uint32_t QuantizeUvOffset(float value)
	{
		const int64_t scaled = std::llround(static_cast<double>(value) * static_cast<double>(CLOD_UV_QUANTIZATION_SCALE));
		const int64_t clamped = std::clamp<int64_t>(
			scaled,
			0,
			static_cast<int64_t>((std::numeric_limits<uint32_t>::max)()));
		return static_cast<uint32_t>(clamped);
	}

	DirectX::XMFLOAT3 ReadVertexFloat3(const std::vector<std::byte>& vertices, size_t vertexStrideBytes, uint32_t vertexIndex, size_t attributeByteOffset)
	{
		DirectX::XMFLOAT3 value{};
		const size_t byteOffset = static_cast<size_t>(vertexIndex) * vertexStrideBytes + attributeByteOffset;
		std::memcpy(&value.x, vertices.data() + byteOffset, sizeof(float));
		std::memcpy(&value.y, vertices.data() + byteOffset + sizeof(float), sizeof(float));
		std::memcpy(&value.z, vertices.data() + byteOffset + sizeof(float) * 2, sizeof(float));
		return value;
	}

	DirectX::XMFLOAT3 NormalizeOrFallback(DirectX::XMFLOAT3 value, DirectX::XMFLOAT3 fallback)
	{
		const float lenSq = value.x * value.x + value.y * value.y + value.z * value.z;
		if (lenSq <= 1e-20f)
		{
			const float fallbackLenSq = fallback.x * fallback.x + fallback.y * fallback.y + fallback.z * fallback.z;
			if (fallbackLenSq <= 1e-20f)
			{
				return DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f);
			}

			const float invFallbackLen = 1.0f / std::sqrt(fallbackLenSq);
			return DirectX::XMFLOAT3(
				fallback.x * invFallbackLen,
				fallback.y * invFallbackLen,
				fallback.z * invFallbackLen);
		}

		const float invLen = 1.0f / std::sqrt(lenSq);
		return DirectX::XMFLOAT3(value.x * invLen, value.y * invLen, value.z * invLen);
	}

	DirectX::XMFLOAT2 ReadVertexFloat2(const std::vector<std::byte>& vertices, size_t vertexStrideBytes, uint32_t vertexIndex, size_t attributeByteOffset)
	{
		DirectX::XMFLOAT2 value{};
		const size_t byteOffset = static_cast<size_t>(vertexIndex) * vertexStrideBytes + attributeByteOffset;
		std::memcpy(&value.x, vertices.data() + byteOffset, sizeof(float));
		std::memcpy(&value.y, vertices.data() + byteOffset + sizeof(float), sizeof(float));
		return value;
	}

	DirectX::XMFLOAT3 BuildFallbackTangentFromNormal(DirectX::XMFLOAT3 normal)
	{
		normal = NormalizeOrFallback(normal, DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
		DirectX::XMFLOAT3 axis = (std::abs(normal.z) < 0.999f)
			? DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)
			: DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);

		DirectX::XMFLOAT3 tangent(
			axis.y * normal.z - axis.z * normal.y,
			axis.z * normal.x - axis.x * normal.z,
			axis.x * normal.y - axis.y * normal.x);

		const float tangentLenSq = tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z;
		if (tangentLenSq <= 1e-20f)
		{
			return DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
		}

		const float invTangentLen = 1.0f / std::sqrt(tangentLenSq);
		return DirectX::XMFLOAT3(tangent.x * invTangentLen, tangent.y * invTangentLen, tangent.z * invTangentLen);
	}

	struct MikkTangentBuildData
	{
		const std::vector<std::byte>* vertices = nullptr;
		size_t vertexStrideBytes = 0;
		const std::vector<uint32_t>* indices = nullptr;
		size_t positionByteOffset = 0;
		size_t normalByteOffset = 0;
		size_t texcoordByteOffset = 0;
		std::vector<DirectX::XMFLOAT3> accumulatedTangents;
		std::vector<float> accumulatedSigns;
		std::vector<uint32_t> accumulatedContributions;
	};

	int MikkGetNumFaces(const SMikkTSpaceContext* context)
	{
		const MikkTangentBuildData* data = static_cast<const MikkTangentBuildData*>(context->m_pUserData);
		return static_cast<int>(data->indices->size() / 3ull);
	}

	int MikkGetNumVerticesOfFace(const SMikkTSpaceContext*, const int)
	{
		return 3;
	}

	void MikkGetPosition(const SMikkTSpaceContext* context, float positionOut[], const int face, const int vertexInFace)
	{
		const MikkTangentBuildData* data = static_cast<const MikkTangentBuildData*>(context->m_pUserData);
		const size_t index = static_cast<size_t>(face) * 3ull + static_cast<size_t>(vertexInFace);
		const uint32_t vertexIndex = (*data->indices)[index];
		const DirectX::XMFLOAT3 position = ReadVertexFloat3(*data->vertices, data->vertexStrideBytes, vertexIndex, data->positionByteOffset);
		positionOut[0] = position.x;
		positionOut[1] = position.y;
		positionOut[2] = position.z;
	}

	void MikkGetNormal(const SMikkTSpaceContext* context, float normalOut[], const int face, const int vertexInFace)
	{
		const MikkTangentBuildData* data = static_cast<const MikkTangentBuildData*>(context->m_pUserData);
		const size_t index = static_cast<size_t>(face) * 3ull + static_cast<size_t>(vertexInFace);
		const uint32_t vertexIndex = (*data->indices)[index];
		const DirectX::XMFLOAT3 normal = ReadVertexFloat3(*data->vertices, data->vertexStrideBytes, vertexIndex, data->normalByteOffset);
		normalOut[0] = normal.x;
		normalOut[1] = normal.y;
		normalOut[2] = normal.z;
	}

	void MikkGetTexCoord(const SMikkTSpaceContext* context, float texcoordOut[], const int face, const int vertexInFace)
	{
		const MikkTangentBuildData* data = static_cast<const MikkTangentBuildData*>(context->m_pUserData);
		const size_t index = static_cast<size_t>(face) * 3ull + static_cast<size_t>(vertexInFace);
		const uint32_t vertexIndex = (*data->indices)[index];
		const DirectX::XMFLOAT2 texcoord = ReadVertexFloat2(*data->vertices, data->vertexStrideBytes, vertexIndex, data->texcoordByteOffset);
		texcoordOut[0] = texcoord.x;
		texcoordOut[1] = texcoord.y;
	}

	void MikkSetTSpaceBasic(const SMikkTSpaceContext* context, const float tangent[], const float sign, const int face, const int vertexInFace)
	{
		MikkTangentBuildData* data = static_cast<MikkTangentBuildData*>(context->m_pUserData);
		const size_t index = static_cast<size_t>(face) * 3ull + static_cast<size_t>(vertexInFace);
		const uint32_t vertexIndex = (*data->indices)[index];
		if (vertexIndex >= data->accumulatedTangents.size())
		{
			return;
		}

		DirectX::XMFLOAT3& accumulatedTangent = data->accumulatedTangents[vertexIndex];
		accumulatedTangent.x += tangent[0];
		accumulatedTangent.y += tangent[1];
		accumulatedTangent.z += tangent[2];
		data->accumulatedSigns[vertexIndex] += sign;
		data->accumulatedContributions[vertexIndex] += 1u;
	}

	bool GenerateMikkTangents(
		const std::vector<std::byte>& vertices,
		size_t vertexStrideBytes,
		const std::vector<uint32_t>& indices,
		std::vector<DirectX::XMFLOAT4>& outTangents)
	{
		constexpr size_t PositionByteOffset = 0;
		constexpr size_t NormalByteOffset = sizeof(float) * 3;
		constexpr size_t TexcoordByteOffset = sizeof(float) * 6;

		if (vertexStrideBytes < sizeof(float) * 8 || indices.empty() || (indices.size() % 3ull) != 0ull)
		{
			return false;
		}

		const size_t vertexCount = vertices.size() / vertexStrideBytes;
		if (vertexCount == 0)
		{
			return false;
		}

		for (uint32_t index : indices)
		{
			if (static_cast<size_t>(index) >= vertexCount)
			{
				return false;
			}
		}

		MikkTangentBuildData buildData{};
		buildData.vertices = &vertices;
		buildData.vertexStrideBytes = vertexStrideBytes;
		buildData.indices = &indices;
		buildData.positionByteOffset = PositionByteOffset;
		buildData.normalByteOffset = NormalByteOffset;
		buildData.texcoordByteOffset = TexcoordByteOffset;
		buildData.accumulatedTangents.assign(vertexCount, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
		buildData.accumulatedSigns.assign(vertexCount, 0.0f);
		buildData.accumulatedContributions.assign(vertexCount, 0u);

		SMikkTSpaceInterface mikkInterface{};
		mikkInterface.m_getNumFaces = &MikkGetNumFaces;
		mikkInterface.m_getNumVerticesOfFace = &MikkGetNumVerticesOfFace;
		mikkInterface.m_getPosition = &MikkGetPosition;
		mikkInterface.m_getNormal = &MikkGetNormal;
		mikkInterface.m_getTexCoord = &MikkGetTexCoord;
		mikkInterface.m_setTSpaceBasic = &MikkSetTSpaceBasic;

		SMikkTSpaceContext mikkContext{};
		mikkContext.m_pInterface = &mikkInterface;
		mikkContext.m_pUserData = &buildData;

		if (genTangSpaceDefault(&mikkContext) == 0)
		{
			return false;
		}

		outTangents.resize(vertexCount);
		for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
		{
			DirectX::XMFLOAT3 tangent = buildData.accumulatedTangents[vertexIndex];
			const float tangentLenSq = tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z;

			if (buildData.accumulatedContributions[vertexIndex] == 0u || tangentLenSq <= 1e-20f ||
				!std::isfinite(tangent.x) || !std::isfinite(tangent.y) || !std::isfinite(tangent.z))
			{
				const DirectX::XMFLOAT3 normal = ReadVertexFloat3(vertices, vertexStrideBytes, static_cast<uint32_t>(vertexIndex), NormalByteOffset);
				tangent = BuildFallbackTangentFromNormal(normal);
			}
			else
			{
				const float invLen = 1.0f / std::sqrt(tangentLenSq);
				tangent.x *= invLen;
				tangent.y *= invLen;
				tangent.z *= invLen;
			}

			const float sign = buildData.accumulatedSigns[vertexIndex] < 0.0f ? -1.0f : 1.0f;
			outTangents[vertexIndex] = DirectX::XMFLOAT4(tangent.x, tangent.y, tangent.z, sign);
		}

		return true;
	}

	std::vector<DirectX::XMFLOAT3> RecalculateGroupNormals(
		const std::vector<uint32_t>& groupLocalToGlobal,
		const std::vector<meshopt_Meshlet>& meshlets,
		const std::vector<uint32_t>& meshletVertices,
		const std::vector<uint8_t>& meshletTriangles,
		const std::vector<std::byte>& vertices,
		size_t vertexStrideBytes)
	{
		constexpr size_t PositionByteOffset = 0;
		constexpr size_t NormalByteOffset = sizeof(float) * 3;

		std::vector<DirectX::XMFLOAT3> accumulatedNormals(groupLocalToGlobal.size(), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

		for (const meshopt_Meshlet& meshlet : meshlets)
		{
			const uint32_t meshletVertexOffset = meshlet.vertex_offset;
			const uint32_t meshletTriangleOffset = meshlet.triangle_offset;

			for (uint32_t triangleIndex = 0; triangleIndex < meshlet.triangle_count; ++triangleIndex)
			{
				const uint32_t triBase = meshletTriangleOffset + triangleIndex * 3u;
				const uint32_t localIndex0 = static_cast<uint32_t>(meshletTriangles[triBase + 0u]);
				const uint32_t localIndex1 = static_cast<uint32_t>(meshletTriangles[triBase + 1u]);
				const uint32_t localIndex2 = static_cast<uint32_t>(meshletTriangles[triBase + 2u]);

				if (localIndex0 >= meshlet.vertex_count || localIndex1 >= meshlet.vertex_count || localIndex2 >= meshlet.vertex_count)
				{
					continue;
				}

				const uint32_t groupVertex0 = meshletVertices[meshletVertexOffset + localIndex0];
				const uint32_t groupVertex1 = meshletVertices[meshletVertexOffset + localIndex1];
				const uint32_t groupVertex2 = meshletVertices[meshletVertexOffset + localIndex2];

				if (groupVertex0 >= groupLocalToGlobal.size() || groupVertex1 >= groupLocalToGlobal.size() || groupVertex2 >= groupLocalToGlobal.size())
				{
					continue;
				}

				const DirectX::XMFLOAT3 p0 = ReadVertexFloat3(vertices, vertexStrideBytes, groupLocalToGlobal[groupVertex0], PositionByteOffset);
				const DirectX::XMFLOAT3 p1 = ReadVertexFloat3(vertices, vertexStrideBytes, groupLocalToGlobal[groupVertex1], PositionByteOffset);
				const DirectX::XMFLOAT3 p2 = ReadVertexFloat3(vertices, vertexStrideBytes, groupLocalToGlobal[groupVertex2], PositionByteOffset);

				const float e10x = p1.x - p0.x;
				const float e10y = p1.y - p0.y;
				const float e10z = p1.z - p0.z;
				const float e20x = p2.x - p0.x;
				const float e20y = p2.y - p0.y;
				const float e20z = p2.z - p0.z;

				const DirectX::XMFLOAT3 faceNormal(
					e10y * e20z - e10z * e20y,
					e10z * e20x - e10x * e20z,
					e10x * e20y - e10y * e20x);

				accumulatedNormals[groupVertex0].x += faceNormal.x;
				accumulatedNormals[groupVertex0].y += faceNormal.y;
				accumulatedNormals[groupVertex0].z += faceNormal.z;
				accumulatedNormals[groupVertex1].x += faceNormal.x;
				accumulatedNormals[groupVertex1].y += faceNormal.y;
				accumulatedNormals[groupVertex1].z += faceNormal.z;
				accumulatedNormals[groupVertex2].x += faceNormal.x;
				accumulatedNormals[groupVertex2].y += faceNormal.y;
				accumulatedNormals[groupVertex2].z += faceNormal.z;
			}
		}

		std::vector<DirectX::XMFLOAT3> result;
		result.resize(groupLocalToGlobal.size());

		for (size_t groupVertex = 0; groupVertex < groupLocalToGlobal.size(); ++groupVertex)
		{
			const DirectX::XMFLOAT3 sourceNormal = ReadVertexFloat3(
				vertices,
				vertexStrideBytes,
				groupLocalToGlobal[groupVertex],
				NormalByteOffset);

			result[groupVertex] = NormalizeOrFallback(accumulatedNormals[groupVertex], sourceNormal);
		}

		return result;
	}

	struct CapturedClusterLODCluster
	{
		int32_t refinedGroup = -1;
		clodBounds bounds{};
		uint32_t indicesOffset = 0;
		uint32_t indexCount = 0;
		uint32_t vertexCount = 0;
	};

	struct CapturedClusterLODGroup
	{
		int depth = 0;
		clodBounds simplified{};
		std::vector<unsigned int> flattenedIndices;
		std::vector<CapturedClusterLODCluster> clusters;
	};

	struct ClusterLODGroupBuildOutput
	{
		ClusterLODGroup group{};
		std::vector<meshopt_Meshlet> meshlets;
		std::vector<uint32_t> meshletVertices;
		std::vector<uint8_t> meshletTriangles;
		std::vector<BoundingSphere> meshletBounds;
		std::vector<ClusterLODGroupSegment> segments;
		std::vector<BoundingSphere> segmentBounds;
		std::vector<std::byte> vertexChunk;
		std::vector<std::vector<std::byte>> pageBlobs;
		ClusterLODGroupChunk groupChunk{};
	};

	ClusterLODGroupBuildOutput BuildClusterLODGroupOutput(
		const CapturedClusterLODGroup& capturedGroup,
		const std::vector<std::byte>& vertices,
		const std::vector<MeshUvSetData>& uvSets,
		size_t vertexStrideBytes,
		const std::vector<std::byte>* skinningVertices,
		size_t skinningVertexStrideBytes,
		float meshPositionQuantScale,
		uint32_t meshPositionQuantExp,
		bool recomputeNormals)
	{
		ClusterLODGroupBuildOutput output{};

		output.group.bounds = capturedGroup.simplified;
		output.group.depth = capturedGroup.depth;
		output.group.firstMeshlet = 0;
		output.group.meshletCount = static_cast<uint32_t>(capturedGroup.clusters.size());
		output.group.firstGroupVertex = 0;
		output.group.groupVertexCount = 0;
		output.group.firstSegment = 0;
		output.group.segmentCount = 0;
		output.group.terminalSegmentCount = 0;

		std::unordered_map<uint32_t, uint32_t> groupVertexToLocal;
		groupVertexToLocal.reserve(capturedGroup.clusters.size() * MS_MESHLET_SIZE);
		std::vector<uint32_t> groupLocalToGlobal;
		groupLocalToGlobal.reserve(capturedGroup.clusters.size() * MS_MESHLET_SIZE);

		auto getGroupLocalVertexIndex = [&](uint32_t globalVertexIndex) -> uint32_t
			{
				auto it = groupVertexToLocal.find(globalVertexIndex);
				if (it != groupVertexToLocal.end())
				{
					return it->second;
				}

				const uint32_t localIndex = static_cast<uint32_t>(groupLocalToGlobal.size());
				groupVertexToLocal.emplace(globalVertexIndex, localIndex);
				groupLocalToGlobal.push_back(globalVertexIndex);
				return localIndex;
			};

		struct ChildBucket
		{
			int32_t refinedGroup = -1;
			std::vector<uint32_t> clusterIndices;
		};

		std::vector<ChildBucket> buckets;
		buckets.reserve(capturedGroup.clusters.size());

		std::unordered_map<int32_t, uint32_t> bucketLookup;
		bucketLookup.reserve(capturedGroup.clusters.size());

		auto addToBucket = [&](int32_t refinedGroup, uint32_t clusterIndex)
			{
				auto lookupIt = bucketLookup.find(refinedGroup);
				if (lookupIt != bucketLookup.end())
				{
					buckets[lookupIt->second].clusterIndices.push_back(clusterIndex);
					return;
				}

				const uint32_t newBucketIndex = static_cast<uint32_t>(buckets.size());
				bucketLookup.emplace(refinedGroup, newBucketIndex);
				buckets.push_back(ChildBucket{ refinedGroup, {} });
				buckets.back().clusterIndices.reserve(8);
				buckets.back().clusterIndices.push_back(clusterIndex);
			};

		for (uint32_t clusterIndex = 0; clusterIndex < static_cast<uint32_t>(capturedGroup.clusters.size()); ++clusterIndex)
		{
			addToBucket(capturedGroup.clusters[clusterIndex].refinedGroup, clusterIndex);
		}

		// Track which bucket (refinedGroup) each meshlet belongs to
		std::vector<int32_t> meshletBucketTag;
		meshletBucketTag.reserve(capturedGroup.clusters.size());

		uint32_t groupMeshletVertexCursor = 0;
		uint32_t localMeshletCursor = 0;

		for (const ChildBucket& bucket : buckets)
		{
			for (uint32_t clusterIndex : bucket.clusterIndices)
			{
				const CapturedClusterLODCluster& cluster = capturedGroup.clusters[clusterIndex];
				const uint32_t triangleCount = cluster.indexCount / 3;
				const unsigned int* clusterIndices = capturedGroup.flattenedIndices.data() + cluster.indicesOffset;

				std::vector<unsigned int> localVertices(cluster.vertexCount);
				std::vector<unsigned char> localTriangles(cluster.indexCount);

				const size_t uniqueVertexCount = clodLocalIndices(
					localVertices.data(),
					localTriangles.data(),
					clusterIndices,
					cluster.indexCount);

				assert(uniqueVertexCount == cluster.vertexCount);

				std::vector<uint32_t> groupLocalVertices(uniqueVertexCount);
				for (size_t localVertex = 0; localVertex < uniqueVertexCount; ++localVertex)
				{
					groupLocalVertices[localVertex] = getGroupLocalVertexIndex(localVertices[localVertex]);
				}

				meshopt_Meshlet meshlet{};
				meshlet.vertex_offset = groupMeshletVertexCursor;
				meshlet.triangle_offset = static_cast<uint32_t>(output.meshletTriangles.size());
				meshlet.vertex_count = static_cast<uint32_t>(uniqueVertexCount);
				meshlet.triangle_count = triangleCount;

				output.meshletVertices.insert(output.meshletVertices.end(), groupLocalVertices.begin(), groupLocalVertices.end());
				output.meshletTriangles.insert(output.meshletTriangles.end(), localTriangles.begin(), localTriangles.end());
				groupMeshletVertexCursor += static_cast<uint32_t>(uniqueVertexCount);

				BoundingSphere sphere{};
				sphere.sphere = DirectX::XMFLOAT4(cluster.bounds.center[0], cluster.bounds.center[1], cluster.bounds.center[2], cluster.bounds.radius);

				output.meshlets.push_back(meshlet);
				output.meshletBounds.push_back(sphere);
				meshletBucketTag.push_back(bucket.refinedGroup);

				++localMeshletCursor;
			}
		}

		assert(localMeshletCursor == output.group.meshletCount);

		output.group.groupVertexCount = static_cast<uint32_t>(groupLocalToGlobal.size());

		output.vertexChunk.reserve(static_cast<size_t>(output.group.groupVertexCount) * vertexStrideBytes);

		for (uint32_t globalVertexIndex : groupLocalToGlobal)
		{
			const size_t sourceVertexByteOffset = static_cast<size_t>(globalVertexIndex) * vertexStrideBytes;
			output.vertexChunk.insert(
				output.vertexChunk.end(),
				vertices.begin() + static_cast<std::ptrdiff_t>(sourceVertexByteOffset),
				vertices.begin() + static_cast<std::ptrdiff_t>(sourceVertexByteOffset + vertexStrideBytes));
		}

		const bool hasNormalStream = vertexStrideBytes >= sizeof(float) * 6;
		const bool hasTexcoordStream = vertexStrideBytes >= sizeof(float) * 8;
		std::vector<DirectX::XMFLOAT3> groupNormals;
		if (hasNormalStream)
		{
			constexpr size_t NormalByteOffset = sizeof(float) * 3;
			groupNormals.resize(groupLocalToGlobal.size());

			if (recomputeNormals)
			{
				groupNormals = RecalculateGroupNormals(
					groupLocalToGlobal,
					output.meshlets,
					output.meshletVertices,
					output.meshletTriangles,
					vertices,
					vertexStrideBytes);

				for (size_t groupVertexIndex = 0; groupVertexIndex < groupNormals.size(); ++groupVertexIndex)
				{
					const size_t destinationByteOffset = groupVertexIndex * vertexStrideBytes + NormalByteOffset;
					std::memcpy(output.vertexChunk.data() + destinationByteOffset, &groupNormals[groupVertexIndex].x, sizeof(float));
					std::memcpy(output.vertexChunk.data() + destinationByteOffset + sizeof(float), &groupNormals[groupVertexIndex].y, sizeof(float));
					std::memcpy(output.vertexChunk.data() + destinationByteOffset + sizeof(float) * 2, &groupNormals[groupVertexIndex].z, sizeof(float));
				}
			}
			else
			{
				for (size_t groupVertexIndex = 0; groupVertexIndex < groupLocalToGlobal.size(); ++groupVertexIndex)
				{
					groupNormals[groupVertexIndex] = ReadVertexFloat3(vertices, vertexStrideBytes, groupLocalToGlobal[groupVertexIndex], NormalByteOffset);
				}
			}
		}

		std::vector<MeshUvSetData> groupUvSets;
		groupUvSets.reserve(uvSets.size());
		const size_t sourceVertexCount = vertexStrideBytes > 0 ? (vertices.size() / vertexStrideBytes) : 0u;
		for (const MeshUvSetData& sourceUvSet : uvSets)
		{
			MeshUvSetData groupUvSet;
			groupUvSet.name = sourceUvSet.name;
			groupUvSet.values.resize(groupLocalToGlobal.size(), DirectX::XMFLOAT2(0.0f, 0.0f));

			if (sourceUvSet.values.size() == sourceVertexCount)
			{
				for (size_t groupVertexIndex = 0; groupVertexIndex < groupLocalToGlobal.size(); ++groupVertexIndex)
				{
					groupUvSet.values[groupVertexIndex] = sourceUvSet.values[groupLocalToGlobal[groupVertexIndex]];
				}
			}

			groupUvSets.push_back(std::move(groupUvSet));
		}

		if (groupUvSets.empty() && hasTexcoordStream)
		{
			MeshUvSetData legacyUvSet;
			legacyUvSet.name = "UV0";
			legacyUvSet.values.resize(groupLocalToGlobal.size());
			constexpr size_t TexcoordByteOffset = sizeof(float) * 6;
			for (size_t groupVertexIndex = 0; groupVertexIndex < groupLocalToGlobal.size(); ++groupVertexIndex)
			{
				legacyUvSet.values[groupVertexIndex] = ReadVertexFloat2(
					vertices,
					vertexStrideBytes,
					groupLocalToGlobal[groupVertexIndex],
					TexcoordByteOffset);
			}
			groupUvSets.push_back(std::move(legacyUvSet));
		}

		std::vector<PackedSkinningInfluences> groupSkinningInfluences;
		const bool hasSkinningStream =
			(skinningVertices != nullptr) &&
			(skinningVertexStrideBytes >= sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3) + sizeof(PackedSkinningInfluences)) &&
			!skinningVertices->empty();
		const size_t sourceSkinningVertexCount =
			(hasSkinningStream && skinningVertexStrideBytes > 0) ? (skinningVertices->size() / skinningVertexStrideBytes) : 0u;
		if (hasSkinningStream)
		{
			constexpr size_t JointByteOffset = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3);
			groupSkinningInfluences.resize(groupLocalToGlobal.size(), PackedSkinningInfluences{});

			for (size_t groupVertexIndex = 0; groupVertexIndex < groupLocalToGlobal.size(); ++groupVertexIndex)
			{
				const uint32_t globalVertexIndex = groupLocalToGlobal[groupVertexIndex];
				if (globalVertexIndex >= sourceSkinningVertexCount)
				{
					continue;
				}

				const size_t sourceByteOffset = static_cast<size_t>(globalVertexIndex) * skinningVertexStrideBytes;
				std::memcpy(&groupSkinningInfluences[groupVertexIndex],
					skinningVertices->data() + sourceByteOffset + JointByteOffset,
					sizeof(PackedSkinningInfluences));
			}
		}

		std::vector<std::array<int32_t, 3>> quantizedGroupPositions;
		quantizedGroupPositions.reserve(groupLocalToGlobal.size());

		for (uint32_t globalVertexIndex : groupLocalToGlobal)
		{
			const size_t byteOffset = static_cast<size_t>(globalVertexIndex) * vertexStrideBytes;
			float px = 0.0f;
			float py = 0.0f;
			float pz = 0.0f;
			std::memcpy(&px, vertices.data() + byteOffset, sizeof(float));
			std::memcpy(&py, vertices.data() + byteOffset + sizeof(float), sizeof(float));
			std::memcpy(&pz, vertices.data() + byteOffset + sizeof(float) * 2, sizeof(float));

			const int32_t qx = static_cast<int32_t>(std::floor(px * meshPositionQuantScale + 0.5f));
			const int32_t qy = static_cast<int32_t>(std::floor(py * meshPositionQuantScale + 0.5f));
			const int32_t qz = static_cast<int32_t>(std::floor(pz * meshPositionQuantScale + 0.5f));

			quantizedGroupPositions.push_back({ qx, qy, qz });
		}

		// Compress normals for page blob construction (oct-encoded, one word per vertex)
		std::vector<uint32_t> compressedNormalWords;
		if (hasNormalStream)
		{
			compressedNormalWords.reserve(groupNormals.size());
			for (const DirectX::XMFLOAT3& normal : groupNormals)
			{
				auto oct = OctEncodeNormal(normal);
				compressedNormalWords.push_back(PackOctNormalSnorm16(oct));
			}
		}

		output.groupChunk.groupVertexCount = output.group.groupVertexCount;
		output.groupChunk.meshletCount = static_cast<uint32_t>(output.meshlets.size());
		output.groupChunk.meshletTrianglesByteCount = static_cast<uint32_t>(output.meshletTriangles.size());
		output.groupChunk.compressedPositionQuantExp = meshPositionQuantExp;
		output.groupChunk.compressedFlags = CLOD_COMPRESSED_POSITIONS;
		if (!compressedNormalWords.empty())
		{
			output.groupChunk.compressedFlags |= CLOD_COMPRESSED_NORMALS;
		}

		// Per-meshlet compression + page binning + segment creation + page blob construction
		{
			const bool hasNormals = !compressedNormalWords.empty();
			auto align4 = [](size_t v) -> size_t { return (v + 3u) & ~size_t(3); };

			// === Per-meshlet compression parameters ===
			struct PerUvSetCompression {
				float uvMinU = 0.0f;
				float uvMinV = 0.0f;
				float uvScaleU = 0.0f;
				float uvScaleV = 0.0f;
				uint32_t uvBitsU = 0;
				uint32_t uvBitsV = 0;
				uint64_t totalUvBits = 0;
			};

			struct PerMeshletCompression {
				std::array<int32_t, 3> minQ;
				uint32_t bitsX, bitsY, bitsZ;
				uint64_t totalPositionBits;
				uint32_t attributeMask = 0;
				std::vector<uint32_t> boneList;
				std::vector<PerUvSetCompression> uvSets;
			};

			const uint32_t totalMeshlets = static_cast<uint32_t>(output.meshlets.size());
			std::vector<PerMeshletCompression> perMeshletComp(totalMeshlets);

			for (uint32_t mi = 0; mi < totalMeshlets; ++mi)
			{
				const auto& meshlet = output.meshlets[mi];
				auto& comp = perMeshletComp[mi];

				std::array<int32_t, 3> meshletMinQ = { std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max() };
				std::array<int32_t, 3> meshletMaxQ = { std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min() };

				for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
				{
					const uint32_t groupLocalVertex = output.meshletVertices[meshlet.vertex_offset + vi];
					const auto& q = quantizedGroupPositions[groupLocalVertex];
					for (int axis = 0; axis < 3; ++axis)
					{
						meshletMinQ[axis] = std::min(meshletMinQ[axis], q[axis]);
						meshletMaxQ[axis] = std::max(meshletMaxQ[axis], q[axis]);
					}
				}

				if (meshlet.vertex_count == 0)
				{
					meshletMinQ = { 0, 0, 0 };
					meshletMaxQ = { 0, 0, 0 };
				}

				comp.minQ = meshletMinQ;
				comp.bitsX = BitsNeededForRange(static_cast<uint32_t>(meshletMaxQ[0] - meshletMinQ[0]));
				comp.bitsY = BitsNeededForRange(static_cast<uint32_t>(meshletMaxQ[1] - meshletMinQ[1]));
				comp.bitsZ = BitsNeededForRange(static_cast<uint32_t>(meshletMaxQ[2] - meshletMinQ[2]));
				comp.totalPositionBits = static_cast<uint64_t>(meshlet.vertex_count) * (comp.bitsX + comp.bitsY + comp.bitsZ);
				if (hasNormals)
				{
					comp.attributeMask |= CLOD_PAGE_ATTRIBUTE_NORMAL;
				}
				if (hasSkinningStream)
				{
					comp.attributeMask |= (CLOD_PAGE_ATTRIBUTE_JOINTS | CLOD_PAGE_ATTRIBUTE_WEIGHTS);
					std::unordered_set<uint32_t> uniqueBones;
					for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
					{
						const uint32_t groupLocalVertex = output.meshletVertices[meshlet.vertex_offset + vi];
						const PackedSkinningInfluences& skinning = groupSkinningInfluences[groupLocalVertex];
						const uint32_t jointValues[kMaxSkinInfluences] = {
							skinning.joints0.x, skinning.joints0.y, skinning.joints0.z, skinning.joints0.w,
							skinning.joints1.x, skinning.joints1.y, skinning.joints1.z, skinning.joints1.w
						};
						const float weightValues[kMaxSkinInfluences] = {
							skinning.weights0.x, skinning.weights0.y, skinning.weights0.z, skinning.weights0.w,
							skinning.weights1.x, skinning.weights1.y, skinning.weights1.z, skinning.weights1.w
						};
						for (uint32_t influence = 0; influence < kMaxSkinInfluences; ++influence)
						{
							if (weightValues[influence] > 0.0f)
							{
								uniqueBones.insert(jointValues[influence]);
							}
						}
					}

					comp.boneList.assign(uniqueBones.begin(), uniqueBones.end());
					std::sort(comp.boneList.begin(), comp.boneList.end());
				}
				if (!groupUvSets.empty())
				{
					comp.uvSets.resize(groupUvSets.size());
					for (size_t uvSetIndex = 0; uvSetIndex < groupUvSets.size(); ++uvSetIndex)
					{
						float minU = std::numeric_limits<float>::max();
						float minV = std::numeric_limits<float>::max();
						float maxU = -std::numeric_limits<float>::max();
						float maxV = -std::numeric_limits<float>::max();

						for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
						{
							const uint32_t groupLocalVertex = output.meshletVertices[meshlet.vertex_offset + vi];
							const DirectX::XMFLOAT2 uv = groupUvSets[uvSetIndex].values[groupLocalVertex];
							minU = std::min(minU, uv.x);
							minV = std::min(minV, uv.y);
							maxU = std::max(maxU, uv.x);
							maxV = std::max(maxV, uv.y);
						}

						if (meshlet.vertex_count == 0)
						{
							minU = minV = maxU = maxV = 0.0f;
						}

						const float rangeU = std::max(0.0f, maxU - minU);
						const float rangeV = std::max(0.0f, maxV - minV);
						const uint32_t maxEncodedU = QuantizeUvOffset(rangeU);
						const uint32_t maxEncodedV = QuantizeUvOffset(rangeV);
						PerUvSetCompression& uvComp = comp.uvSets[uvSetIndex];
						uvComp.uvMinU = minU;
						uvComp.uvMinV = minV;
						uvComp.uvScaleU = CLOD_UV_QUANTIZATION_INV_SCALE;
						uvComp.uvScaleV = CLOD_UV_QUANTIZATION_INV_SCALE;
						uvComp.uvBitsU = BitsNeededForRange(maxEncodedU);
						uvComp.uvBitsV = BitsNeededForRange(maxEncodedV);
						uvComp.totalUvBits = static_cast<uint64_t>(meshlet.vertex_count) * (uvComp.uvBitsU + uvComp.uvBitsV);
					}
				}
			}

			auto GetMeshletNormalWords = [&](uint32_t pageMask, const meshopt_Meshlet& meshlet) -> uint32_t
			{
				return ((pageMask & CLOD_PAGE_ATTRIBUTE_NORMAL) != 0u) ? meshlet.vertex_count : 0u;
			};

			auto GetMeshletUvBits = [&](uint32_t pageUvSetIndex, const meshopt_Meshlet& meshlet, const PerMeshletCompression& comp) -> uint64_t
			{
				if (pageUvSetIndex < comp.uvSets.size())
				{
					return comp.uvSets[pageUvSetIndex].totalUvBits;
				}

				// Future mixed-format pages backfill missing UV sets with a constant zero stream.
				return static_cast<uint64_t>(meshlet.vertex_count) * 2ull;
			};

			struct PageTotals {
				uint64_t totalPositionBits = 0;
				std::vector<uint64_t> totalUvBitsPerSet;
				uint32_t totalVertexCount = 0;
				uint32_t totalNormalWords = 0;
				uint32_t totalBoneIndexCount = 0;
				uint32_t totalTriangleBytes = 0;
			};

			auto ComputePageTotals = [&](const std::vector<uint32_t>& meshletIndices, uint32_t pageMask, uint32_t pageUvSetCount) -> PageTotals
			{
				PageTotals totals{};
				totals.totalUvBitsPerSet.assign(pageUvSetCount, 0ull);
				for (uint32_t meshletIndex : meshletIndices)
				{
					const auto& meshlet = output.meshlets[meshletIndex];
					const auto& comp = perMeshletComp[meshletIndex];
					totals.totalPositionBits += comp.totalPositionBits;
					totals.totalVertexCount += meshlet.vertex_count;
					for (uint32_t uvSetIndex = 0; uvSetIndex < pageUvSetCount; ++uvSetIndex)
					{
						totals.totalUvBitsPerSet[uvSetIndex] += GetMeshletUvBits(uvSetIndex, meshlet, comp);
					}
					totals.totalNormalWords += GetMeshletNormalWords(pageMask, meshlet);
					totals.totalBoneIndexCount += static_cast<uint32_t>(comp.boneList.size());
					totals.totalTriangleBytes += meshlet.triangle_count * 3u;
				}
				return totals;
			};

			// === Page binning (simplified: no vertex set dedup) ===
			struct PageBin {
				std::vector<uint32_t> meshletIndices;
				uint32_t attributeMask = 0;
				uint32_t uvSetCount = 0;
			};

			std::vector<PageBin> pageBins;
			pageBins.emplace_back();

			for (uint32_t mi = 0; mi < totalMeshlets; ++mi)
			{
				const auto& meshlet = output.meshlets[mi];
				const auto& comp = perMeshletComp[mi];
				PageBin& currentPage = pageBins.back();
				uint32_t candidateMask = currentPage.attributeMask | comp.attributeMask;
				uint32_t candidateUvSetCount = std::max(currentPage.uvSetCount, static_cast<uint32_t>(comp.uvSets.size()));
				std::vector<uint32_t> candidateMeshlets = currentPage.meshletIndices;
				candidateMeshlets.push_back(mi);
				PageTotals candidateTotals = ComputePageTotals(candidateMeshlets, candidateMask, candidateUvSetCount);
				const size_t candidateSize = ComputePageBlobSize(
					candidateMask,
					static_cast<uint32_t>(candidateMeshlets.size()),
					candidateUvSetCount,
					candidateTotals.totalPositionBits,
					candidateTotals.totalUvBitsPerSet,
					candidateTotals.totalVertexCount,
					candidateTotals.totalNormalWords,
					candidateTotals.totalBoneIndexCount,
					candidateTotals.totalTriangleBytes);

				if (candidateSize > CLOD_PAGE_SIZE && !currentPage.meshletIndices.empty())
				{
					pageBins.emplace_back();
					PageBin& newPage = pageBins.back();
					newPage.attributeMask = comp.attributeMask;
					newPage.uvSetCount = static_cast<uint32_t>(comp.uvSets.size());
					newPage.meshletIndices.push_back(mi);
					continue;
				}

				currentPage.attributeMask = candidateMask;
				currentPage.uvSetCount = candidateUvSetCount;
				currentPage.meshletIndices.push_back(mi);
			}

			if (!pageBins.empty() && pageBins.back().meshletIndices.empty())
			{
				pageBins.pop_back();
			}

			// === Create segments: one segment per page ===
			for (uint32_t pi = 0; pi < static_cast<uint32_t>(pageBins.size()); ++pi)
			{
				const PageBin& page = pageBins[pi];
				if (page.meshletIndices.empty()) continue;

				int32_t representativeTag = -1;
				for (uint32_t mi : page.meshletIndices)
				{
					if (meshletBucketTag[mi] >= 0)
					{
						representativeTag = meshletBucketTag[mi];
						break;
					}
				}

				ClusterLODGroupSegment seg{};
				seg.refinedGroup = representativeTag;
				seg.pageIndex = pi;
				seg.firstMeshletInPage = 0;
				seg.meshletCount = static_cast<uint32_t>(page.meshletIndices.size());
				output.segments.push_back(seg);
			}

			// Sort segments: terminal (refinedGroup < 0) first.
			std::stable_sort(output.segments.begin(), output.segments.end(),
				[](const ClusterLODGroupSegment& a, const ClusterLODGroupSegment& b) {
					const bool aTerminal = (a.refinedGroup < 0);
					const bool bTerminal = (b.refinedGroup < 0);
					return aTerminal > bTerminal;
				});

			// Compute per-segment bounding spheres.
			output.segmentBounds.resize(output.segments.size());
			for (uint32_t si = 0; si < static_cast<uint32_t>(output.segments.size()); ++si)
			{
				const ClusterLODGroupSegment& seg = output.segments[si];
				const PageBin& page = pageBins[seg.pageIndex];

				float mergedCx = 0.f, mergedCy = 0.f, mergedCz = 0.f, mergedR = 0.f;
				if (seg.meshletCount > 0)
				{
					std::vector<float> centers(seg.meshletCount * 4);
					std::vector<float> radii(seg.meshletCount);
					for (uint32_t i = 0; i < seg.meshletCount; ++i)
					{
						const uint32_t groupLocalMeshlet = page.meshletIndices[seg.firstMeshletInPage + i];
						const BoundingSphere& mb = output.meshletBounds[groupLocalMeshlet];
						centers[i * 4 + 0] = mb.sphere.x;
						centers[i * 4 + 1] = mb.sphere.y;
						centers[i * 4 + 2] = mb.sphere.z;
						centers[i * 4 + 3] = 0.f;
						radii[i] = mb.sphere.w;
					}
					meshopt_Bounds merged = meshopt_computeSphereBounds(
						centers.data(), seg.meshletCount, sizeof(float) * 4,
						radii.data(), sizeof(float));
					mergedCx = merged.center[0];
					mergedCy = merged.center[1];
					mergedCz = merged.center[2];
					mergedR = merged.radius;
				}
				output.segmentBounds[si].sphere = DirectX::XMFLOAT4(mergedCx, mergedCy, mergedCz, mergedR);
			}

			output.group.pageCount = static_cast<uint32_t>(pageBins.size());
			output.group.segmentCount = static_cast<uint32_t>(output.segments.size());
			output.group.terminalSegmentCount = 0;
			for (const ClusterLODGroupSegment& seg : output.segments)
			{
				if (seg.refinedGroup < 0)
					output.group.terminalSegmentCount++;
				else
					break;
			}

			// === Build page blobs: new SoA format ===
			// Layout: Header | CoreDescriptors | UvDescriptors | PositionBitstream | Optional normal stream |
			//         UV bitstream directory | UV bitstreams | TriangleStream
			output.pageBlobs.resize(pageBins.size());

			for (uint32_t pi = 0; pi < static_cast<uint32_t>(pageBins.size()); ++pi)
			{
				const PageBin& page = pageBins[pi];
				const uint32_t pageMeshletCount = static_cast<uint32_t>(page.meshletIndices.size());
				if (pageMeshletCount == 0) continue;
				const PageTotals pageTotals = ComputePageTotals(page.meshletIndices, page.attributeMask, page.uvSetCount);
				const bool pageHasNormals = (page.attributeMask & CLOD_PAGE_ATTRIBUTE_NORMAL) != 0u;
				const bool pageHasJoints = (page.attributeMask & CLOD_PAGE_ATTRIBUTE_JOINTS) != 0u;
				const bool pageHasWeights = (page.attributeMask & CLOD_PAGE_ATTRIBUTE_WEIGHTS) != 0u;
				const bool pageHasUvSets = page.uvSetCount > 0u;

				// Compute stream offsets
				const uint32_t descriptorOffset = static_cast<uint32_t>(align4(CLOD_PAGE_HEADER_SIZE));
				const size_t descriptorBytes = static_cast<size_t>(pageMeshletCount) * sizeof(CLodMeshletDescriptor);
				const uint32_t uvDescriptorOffset = pageHasUvSets
					? static_cast<uint32_t>(align4(descriptorOffset + descriptorBytes))
					: 0u;
				const size_t uvDescriptorBytes = pageHasUvSets
					? static_cast<size_t>(pageMeshletCount) * static_cast<size_t>(page.uvSetCount) * sizeof(CLodMeshletUvDescriptor)
					: 0u;
				const uint32_t positionBitstreamOffset = static_cast<uint32_t>(align4(pageHasUvSets ? (uvDescriptorOffset + uvDescriptorBytes) : (descriptorOffset + descriptorBytes)));
				const size_t positionWords = static_cast<size_t>((pageTotals.totalPositionBits + 31ull) / 32ull);
				const size_t positionBytes = positionWords * sizeof(uint32_t);
				const uint32_t normalArrayOffset = pageHasNormals
					? static_cast<uint32_t>(align4(positionBitstreamOffset + positionBytes))
					: 0u;
				const size_t normalBytes = pageHasNormals ? static_cast<size_t>(pageTotals.totalNormalWords) * sizeof(uint32_t) : 0u;
				const uint32_t jointArrayOffset = pageHasJoints
					? static_cast<uint32_t>(align4(pageHasNormals ? (normalArrayOffset + normalBytes) : (positionBitstreamOffset + positionBytes)))
					: 0u;
				const size_t jointBytes = pageHasJoints ? static_cast<size_t>(pageTotals.totalVertexCount) * sizeof(DirectX::XMUINT4) * 2u : 0u;
				const uint32_t weightArrayOffset = pageHasWeights
					? static_cast<uint32_t>(align4(pageHasJoints ? (jointArrayOffset + jointBytes) : (pageHasNormals ? (normalArrayOffset + normalBytes) : (positionBitstreamOffset + positionBytes))))
					: 0u;
				const size_t weightBytes = pageHasWeights ? static_cast<size_t>(pageTotals.totalVertexCount) * sizeof(DirectX::XMFLOAT4) * 2u : 0u;
				const uint32_t uvBitstreamDirectoryOffset = pageHasUvSets
					? static_cast<uint32_t>(align4(
						pageHasWeights ? (weightArrayOffset + weightBytes) :
						(pageHasJoints ? (jointArrayOffset + jointBytes) :
						(pageHasNormals ? (normalArrayOffset + normalBytes) : (positionBitstreamOffset + positionBytes)))))
					: 0u;
				std::vector<uint32_t> uvBitstreamOffsets(page.uvSetCount, 0u);
				size_t uvBitstreamCursor = pageHasUvSets
					? align4(static_cast<size_t>(uvBitstreamDirectoryOffset) + static_cast<size_t>(page.uvSetCount) * sizeof(uint32_t))
					: align4(
						pageHasWeights ? (weightArrayOffset + weightBytes) :
						(pageHasJoints ? (jointArrayOffset + jointBytes) :
						(pageHasNormals ? (normalArrayOffset + normalBytes) : (positionBitstreamOffset + positionBytes))));
				for (uint32_t uvSetIndex = 0; uvSetIndex < page.uvSetCount; ++uvSetIndex)
				{
					uvBitstreamOffsets[uvSetIndex] = static_cast<uint32_t>(uvBitstreamCursor);
					const size_t uvWords = static_cast<size_t>((pageTotals.totalUvBitsPerSet[uvSetIndex] + 31ull) / 32ull);
					const size_t uvBytes = uvWords * sizeof(uint32_t);
					uvBitstreamCursor = align4(uvBitstreamCursor + uvBytes);
				}
				const uint32_t boneIndexStreamOffset = static_cast<uint32_t>(align4(uvBitstreamCursor));
				const size_t boneIndexBytes = static_cast<size_t>(pageTotals.totalBoneIndexCount) * sizeof(uint32_t);
				const uint32_t triangleStreamOffset = static_cast<uint32_t>(align4(boneIndexStreamOffset + boneIndexBytes));

				const size_t totalBlobSize = align4(triangleStreamOffset + pageTotals.totalTriangleBytes);
				auto& blob = output.pageBlobs[pi];
				blob.assign(totalBlobSize, std::byte{0});

				// Build streams + descriptors in one pass
				std::vector<CLodMeshletDescriptor> descriptors(pageMeshletCount);
				std::vector<CLodMeshletUvDescriptor> uvDescriptors(static_cast<size_t>(pageMeshletCount) * static_cast<size_t>(page.uvSetCount));
				std::vector<uint32_t> pagePosWords;
				std::vector<std::vector<uint32_t>> pageUvWordsPerSet(page.uvSetCount);
				uint64_t pageBitCursor = 0;
				std::vector<uint64_t> pageUvBitCursors(page.uvSetCount, 0ull);
				uint32_t vertexAttributeCursor = 0;
				uint32_t boneIndexCursor = 0;
				uint32_t triangleByteCursor = 0;

				for (uint32_t li = 0; li < pageMeshletCount; ++li)
				{
					const uint32_t mi = page.meshletIndices[li];
					const auto& meshlet = output.meshlets[mi];
					const auto& comp = perMeshletComp[mi];

					CLodMeshletDescriptor& desc = descriptors[li];
					desc.positionBitOffset = static_cast<uint32_t>(pageBitCursor);
					desc.vertexAttributeOffset = vertexAttributeCursor;
					desc.triangleByteOffset = triangleByteCursor;
					desc.boneListOffset = boneIndexCursor;
					desc.minQx = comp.minQ[0];
					desc.minQy = comp.minQ[1];
					desc.minQz = comp.minQ[2];
					desc.bitsAndVertexCount =
						(comp.bitsX & 0xFFu)
						| ((comp.bitsY & 0xFFu) << 8u)
						| ((comp.bitsZ & 0xFFu) << 16u)
						| ((meshlet.vertex_count & 0xFFu) << 24u);

					const int32_t tag = meshletBucketTag[mi];
					const uint32_t refinedGroupEncoded = (tag >= 0) ? static_cast<uint32_t>(tag + 1) : 0u;
					desc.triangleCountAndRefinedGroup =
						(meshlet.triangle_count & 0xFFFFu)
						| (refinedGroupEncoded << 16u);
					desc.boneCount = static_cast<uint32_t>(comp.boneList.size());

					const BoundingSphere& bounds = output.meshletBounds[mi];
					desc.bounds = bounds.sphere;
					if (pageHasUvSets)
					{
						for (uint32_t uvSetIndex = 0; uvSetIndex < page.uvSetCount; ++uvSetIndex)
						{
							CLodMeshletUvDescriptor& uvDesc = uvDescriptors[static_cast<size_t>(li) * static_cast<size_t>(page.uvSetCount) + uvSetIndex];
							uvDesc.uvBitOffset = static_cast<uint32_t>(pageUvBitCursors[uvSetIndex]);
							if (uvSetIndex < comp.uvSets.size())
							{
								const PerUvSetCompression& uvComp = comp.uvSets[uvSetIndex];
								uvDesc.uvMinU = uvComp.uvMinU;
								uvDesc.uvMinV = uvComp.uvMinV;
								uvDesc.uvScaleU = uvComp.uvScaleU;
								uvDesc.uvScaleV = uvComp.uvScaleV;
								uvDesc.uvBits = (uvComp.uvBitsU & 0xFFu) | ((uvComp.uvBitsV & 0xFFu) << 8u);
							}
							else
							{
								uvDesc.uvMinU = 0.0f;
								uvDesc.uvMinV = 0.0f;
								uvDesc.uvScaleU = 0.0f;
								uvDesc.uvScaleV = 0.0f;
								uvDesc.uvBits = 1u | (1u << 8u);
							}
						}
					}

					// Append per-meshlet compressed positions to page bitstream
					for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
					{
						const uint32_t gv = output.meshletVertices[meshlet.vertex_offset + vi];
						const auto& q = quantizedGroupPositions[gv];
						AppendBits(pagePosWords, pageBitCursor,
							static_cast<uint32_t>(q[0] - comp.minQ[0]), comp.bitsX);
						AppendBits(pagePosWords, pageBitCursor,
							static_cast<uint32_t>(q[1] - comp.minQ[1]), comp.bitsY);
						AppendBits(pagePosWords, pageBitCursor,
							static_cast<uint32_t>(q[2] - comp.minQ[2]), comp.bitsZ);
					}

					// Append per-meshlet compressed normals
					if (pageHasNormals)
					{
						for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
						{
							uint32_t normalWord = 0u;
							if ((comp.attributeMask & CLOD_PAGE_ATTRIBUTE_NORMAL) != 0u)
							{
								const uint32_t gv = output.meshletVertices[meshlet.vertex_offset + vi];
								normalWord = compressedNormalWords[gv];
							}
							std::memcpy(blob.data() + normalArrayOffset + static_cast<size_t>(vertexAttributeCursor + vi) * sizeof(uint32_t),
								&normalWord, sizeof(uint32_t));
						}
					}
					if (pageHasJoints)
					{
						for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
						{
							PackedSkinningInfluences skinning{};
							if ((comp.attributeMask & CLOD_PAGE_ATTRIBUTE_JOINTS) != 0u)
							{
								const uint32_t gv = output.meshletVertices[meshlet.vertex_offset + vi];
								skinning = groupSkinningInfluences[gv];
							}
							std::memcpy(blob.data() + jointArrayOffset + static_cast<size_t>(vertexAttributeCursor + vi) * sizeof(DirectX::XMUINT4) * 2u,
								&skinning.joints0, sizeof(DirectX::XMUINT4) * 2u);
						}
					}
					if (pageHasWeights)
					{
						for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
						{
							PackedSkinningInfluences skinning{};
							if ((comp.attributeMask & CLOD_PAGE_ATTRIBUTE_WEIGHTS) != 0u)
							{
								const uint32_t gv = output.meshletVertices[meshlet.vertex_offset + vi];
								skinning = groupSkinningInfluences[gv];
							}
							std::memcpy(blob.data() + weightArrayOffset + static_cast<size_t>(vertexAttributeCursor + vi) * sizeof(DirectX::XMFLOAT4) * 2u,
								&skinning.weights0, sizeof(DirectX::XMFLOAT4) * 2u);
						}
					}
					if (!comp.boneList.empty())
					{
						std::memcpy(blob.data() + boneIndexStreamOffset + static_cast<size_t>(boneIndexCursor) * sizeof(uint32_t),
							comp.boneList.data(),
							comp.boneList.size() * sizeof(uint32_t));
						boneIndexCursor += static_cast<uint32_t>(comp.boneList.size());
					}
					vertexAttributeCursor += meshlet.vertex_count;

					if (pageHasUvSets)
					{
						for (uint32_t uvSetIndex = 0; uvSetIndex < page.uvSetCount; ++uvSetIndex)
						{
							const bool meshletHasUv = uvSetIndex < comp.uvSets.size();
							const uint32_t uvBitsU = meshletHasUv ? comp.uvSets[uvSetIndex].uvBitsU : 1u;
							const uint32_t uvBitsV = meshletHasUv ? comp.uvSets[uvSetIndex].uvBitsV : 1u;
							for (uint32_t vi = 0; vi < meshlet.vertex_count; ++vi)
							{
								uint32_t encodedU = 0u;
								uint32_t encodedV = 0u;
								if (meshletHasUv)
								{
									const uint32_t gv = output.meshletVertices[meshlet.vertex_offset + vi];
									const DirectX::XMFLOAT2 uv = groupUvSets[uvSetIndex].values[gv];
									const PerUvSetCompression& uvComp = comp.uvSets[uvSetIndex];
									const float offsetU = std::max(0.0f, uv.x - uvComp.uvMinU);
									const float offsetV = std::max(0.0f, uv.y - uvComp.uvMinV);
									const uint32_t maxEncodedU = (uvBitsU >= 32u) ? 0xFFFFFFFFu : ((1u << uvBitsU) - 1u);
									const uint32_t maxEncodedV = (uvBitsV >= 32u) ? 0xFFFFFFFFu : ((1u << uvBitsV) - 1u);
									encodedU = std::min(maxEncodedU, QuantizeUvOffset(offsetU));
									encodedV = std::min(maxEncodedV, QuantizeUvOffset(offsetV));
								}

								AppendBits(pageUvWordsPerSet[uvSetIndex], pageUvBitCursors[uvSetIndex], encodedU, uvBitsU);
								AppendBits(pageUvWordsPerSet[uvSetIndex], pageUvBitCursors[uvSetIndex], encodedV, uvBitsV);
							}
						}
					}

					// Append per-meshlet triangle bytes (already meshlet-local 0..vertexCount-1)
					const uint32_t triBytes = meshlet.triangle_count * 3u;
					std::memcpy(blob.data() + triangleStreamOffset + triangleByteCursor,
						output.meshletTriangles.data() + meshlet.triangle_offset,
						triBytes);
					triangleByteCursor += triBytes;
				}

				// Write descriptors
				std::memcpy(blob.data() + descriptorOffset, descriptors.data(), descriptorBytes);
				if (pageHasUvSets && !uvDescriptors.empty())
				{
					std::memcpy(blob.data() + uvDescriptorOffset, uvDescriptors.data(), uvDescriptorBytes);
				}

				// Write position bitstream
				if (!pagePosWords.empty())
				{
					std::memcpy(blob.data() + positionBitstreamOffset,
						pagePosWords.data(),
						pagePosWords.size() * sizeof(uint32_t));
				}
				if (pageHasUvSets)
				{
					std::memcpy(blob.data() + uvBitstreamDirectoryOffset,
						uvBitstreamOffsets.data(),
						static_cast<size_t>(page.uvSetCount) * sizeof(uint32_t));
					for (uint32_t uvSetIndex = 0; uvSetIndex < page.uvSetCount; ++uvSetIndex)
					{
						if (!pageUvWordsPerSet[uvSetIndex].empty())
						{
							std::memcpy(blob.data() + uvBitstreamOffsets[uvSetIndex],
								pageUvWordsPerSet[uvSetIndex].data(),
								pageUvWordsPerSet[uvSetIndex].size() * sizeof(uint32_t));
						}
					}
				}

				// Write header
				CLodPageHeader header{};
				header.meshletCount = pageMeshletCount;
				header.compressedPositionQuantExp = meshPositionQuantExp;
				header.attributeMask = page.attributeMask;
				header.uvSetCount = page.uvSetCount;
				header.descriptorOffset = descriptorOffset;
				header.uvDescriptorOffset = uvDescriptorOffset;
				header.positionBitstreamOffset = positionBitstreamOffset;
				header.normalArrayOffset = normalArrayOffset;
				header.jointArrayOffset = jointArrayOffset;
				header.weightArrayOffset = weightArrayOffset;
				header.uvBitstreamDirectoryOffset = uvBitstreamDirectoryOffset;
				header.triangleStreamOffset = triangleStreamOffset;
				header.boneIndexStreamOffset = boneIndexStreamOffset;
				std::memcpy(blob.data(), &header, sizeof(CLodPageHeader));

				assert(blob.size() <= CLOD_PAGE_SIZE && "Page blob exceeds CLOD_PAGE_SIZE");
			}
		}

		return output;
	}

	BoundingSphere BuildObjectBoundingSphereFromRootNode(const std::vector<ClusterLODNode>& nodes, uint32_t rootNodeIndex)
	{
		BoundingSphere sphere{};
		if (rootNodeIndex >= nodes.size()) {
			return sphere;
		}

		const ClusterLODTraversalMetric& metric = nodes[rootNodeIndex].traversalMetric;
		sphere.sphere = metric.cullingSphere;
		return sphere;
	}

	struct ClusterLODBuildState
	{
		std::vector<ClusterLODGroup> groups;
		std::vector<ClusterLODGroupSegment> segments;
		std::vector<BoundingSphere> segmentBounds;
		std::vector<ClusterLODGroupChunk> groupChunks;
		std::vector<std::vector<std::vector<std::byte>>> groupPageBlobs;

		// Raw per-group streams kept for voxel hierarchy building (reads vertex/meshlet data).
		std::vector<std::vector<std::byte>> groupVertexChunks;
		std::vector<std::vector<uint32_t>> groupMeshletVertexChunks;
		std::vector<std::vector<meshopt_Meshlet>> groupMeshletChunks;
		std::vector<std::vector<uint8_t>> groupMeshletTriangleChunks;

		std::vector<ClusterLODNode> nodes;
		std::vector<ClusterLODNodeRangeAlloc> lodNodeRanges;
		std::vector<uint32_t> lodLevelRoots;
		uint32_t topRootNode = 0;
		uint32_t maxDepth = 0;
		VoxelGroupMapping voxelGroupMapping;
	};

	void BuildClusterLODTraversalHierarchy(ClusterLODBuildState& state, uint32_t preferredNodeWidth)
	{
		if (state.groups.empty())
			return;

		preferredNodeWidth = std::max(2u, preferredNodeWidth);

		state.maxDepth = 0;
		for (const ClusterLODGroup& g : state.groups)
			state.maxDepth = std::max(state.maxDepth, uint32_t(g.depth));

		const uint32_t lodLevelCount = state.maxDepth + 1;

		// Collect segments (not groups) by depth for segment-leaf BVH.
		struct SegmentLeafInfo { uint32_t segGlobalIndex; uint32_t ownerGroupId; };
		std::vector<std::vector<SegmentLeafInfo>> segmentsByDepth(lodLevelCount);
		for (uint32_t groupID = 0; groupID < uint32_t(state.groups.size()); ++groupID)
		{
			const ClusterLODGroup& grp = state.groups[groupID];
			const uint32_t d = uint32_t(grp.depth);
			for (uint32_t s = 0; s < grp.segmentCount; ++s)
			{
				segmentsByDepth[d].push_back({ grp.firstSegment + s, groupID });
			}
		}

		for (uint32_t d = 0; d < lodLevelCount; ++d)
		{
			if (segmentsByDepth[d].empty())
			{
				throw std::runtime_error("Cluster LOD: missing segments for an intermediate depth; compact depths or handle gaps.");
			}
		}

		// Build parent error map: for each group, store the max error of any
		// parent (coarser) group that refines into it.  Also track the parent
		// group ID so the shader can load the parent's sphere for an exact
		// bidirectional LOD check.
		std::vector<float> parentErrorForGroup(state.groups.size(), 0.0f);
		std::vector<int32_t> parentGroupIdForGroup(state.groups.size(), -1);
		for (uint32_t groupID = 0; groupID < uint32_t(state.groups.size()); ++groupID)
		{
			const ClusterLODGroup& grp = state.groups[groupID];
			const float parentError = grp.bounds.error;
			for (uint32_t s = 0; s < grp.segmentCount; ++s)
			{
				const ClusterLODGroupSegment& seg = state.segments[grp.firstSegment + s];
				if (seg.refinedGroup >= 0)
				{
					const uint32_t childGroupId = static_cast<uint32_t>(seg.refinedGroup);
					if (parentError >= parentErrorForGroup[childGroupId])
					{
						parentErrorForGroup[childGroupId] = parentError;
						parentGroupIdForGroup[childGroupId] = static_cast<int32_t>(groupID);
					}
				}
			}
		}
		// Root groups (no parent) get FLT_MAX so they are always traversed.
		// Assign parentGroupId to each group.
		for (uint32_t i = 0; i < uint32_t(state.groups.size()); ++i)
		{
			if (parentGroupIdForGroup[i] < 0)
			{
				parentErrorForGroup[i] = std::numeric_limits<float>::max();
			}
			state.groups[i].parentGroupId = parentGroupIdForGroup[i];
		}

		state.lodNodeRanges.assign(lodLevelCount, {});
		state.lodLevelRoots.resize(lodLevelCount);
		for (uint32_t d = 0; d < lodLevelCount; ++d) {
			state.lodLevelRoots[d] = 1 + d;
		}

		uint32_t nodeOffset = 1 + lodLevelCount;

		for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
		{
			const uint32_t leafCount = uint32_t(segmentsByDepth[depth].size());

			uint32_t nodeCount = leafCount;
			uint32_t iterCount = leafCount;

			while (iterCount > 1)
			{
				iterCount = (iterCount + preferredNodeWidth - 1) / preferredNodeWidth;
				nodeCount += iterCount;
			}

			nodeCount--;

			state.lodNodeRanges[depth].offset = nodeOffset;
			state.lodNodeRanges[depth].count = nodeCount;
			nodeOffset += nodeCount;
		}

		state.nodes.clear();
		state.nodes.resize(nodeOffset);

		for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
		{
			const auto& segLeaves = segmentsByDepth[depth];
			const uint32_t leafCount = uint32_t(segLeaves.size());
			const ClusterLODNodeRangeAlloc& range = state.lodNodeRanges[depth];

			uint32_t writeOffset = range.offset;
			uint32_t lastLayerOffset = writeOffset;

			for (uint32_t i = 0; i < leafCount; ++i)
			{
				const SegmentLeafInfo& info = segLeaves[i];
				const ClusterLODGroupSegment& seg = state.segments[info.segGlobalIndex];
				const ClusterLODGroup& grp = state.groups[info.ownerGroupId];
				const BoundingSphere& segBounds = state.segmentBounds[info.segGlobalIndex];

				ClusterLODNode& node = (leafCount == 1) ? state.nodes[1 + depth] : state.nodes[writeOffset++];

				node = {};
				node.range.isGroup = 2;  // segment-leaf
				node.range.indexOrOffset = info.segGlobalIndex;
				node.range.countMinusOne = (seg.meshletCount > 0) ? (seg.meshletCount - 1) : 0;
				node.range.ownerGroupId = info.ownerGroupId;

				if (false) { // Testing
					node.traversalMetric.cullingSphere = segBounds.sphere;
				} else {
					// Expand the BVH leaf bounding sphere to enclose the owning
					// group's bounding sphere for conservative frustum culling.
					// TraverseNodes uses the actual group sphere for LOD checks.
					const float sx = segBounds.sphere.x, sy = segBounds.sphere.y, sz = segBounds.sphere.z;
					const float sr = segBounds.sphere.w;
					const float gx = grp.bounds.center[0], gy = grp.bounds.center[1], gz = grp.bounds.center[2];
					const float gr = grp.bounds.radius;

					const float dx = gx - sx, dy = gy - sy, dz = gz - sz;
					const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

					float cx, cy, cz, cr;
					if (dist + gr <= sr) {
						cx = sx; cy = sy; cz = sz; cr = sr;
					}
					else if (dist + sr <= gr) {
						cx = gx; cy = gy; cz = gz; cr = gr;
					}
					else {
						cr = (dist + sr + gr) * 0.5f;
						const float t = (cr - sr) / std::max(dist, 1e-12f);
						cx = sx + dx * t;
						cy = sy + dy * t;
						cz = sz + dz * t;
					}
					// Pad for floating-point rounding in the minimal-enclosing
					// sphere formula to guarantee strict enclosure.
					cr *= (1.0f + 1e-5f);

					node.traversalMetric.cullingSphere = DirectX::XMFLOAT4(cx, cy, cz, cr);
				}
				node.traversalMetric.lodBoundingSphere = DirectX::XMFLOAT4(
					grp.bounds.center[0],
					grp.bounds.center[1],
					grp.bounds.center[2],
					grp.bounds.radius);
				// Store the own group's error for propagation to parent BVH
				// internal nodes.  TraverseNodes loads the actual group for
				// the leaf-level LOD check, but internal nodes use the
				// propagated maxQuadricError as a conservative bound.
				node.traversalMetric.maxQuadricError = grp.bounds.error;
			}

			if (leafCount == 1) {
				writeOffset++;
			}

			uint32_t iterCount = leafCount;

			std::vector<uint32_t> partitioned;
			std::vector<ClusterLODNode> scratch;

			while (iterCount > 1)
			{
				const uint32_t lastCount = iterCount;
				ClusterLODNode* lastNodes = &state.nodes[lastLayerOffset];

				partitioned.resize(lastCount);
				meshopt_spatialClusterPoints(
					partitioned.data(),
					&lastNodes->traversalMetric.cullingSphere.x,
					lastCount,
					sizeof(ClusterLODNode),
					preferredNodeWidth);

				scratch.assign(lastNodes, lastNodes + lastCount);
				for (uint32_t n = 0; n < lastCount; ++n)
					lastNodes[n] = scratch[partitioned[n]];

				iterCount = (lastCount + preferredNodeWidth - 1) / preferredNodeWidth;

				ClusterLODNode* newNodes = (iterCount == 1) ? &state.nodes[1 + depth] : &state.nodes[writeOffset];

				for (uint32_t n = 0; n < iterCount; ++n)
				{
					ClusterLODNode& node = newNodes[n];

					const uint32_t childBegin = n * preferredNodeWidth;
					const uint32_t childEnd = std::min(childBegin + preferredNodeWidth, lastCount);
					const uint32_t childCount = childEnd - childBegin;

					ClusterLODNode* children = &lastNodes[childBegin];

					node = {};
					node.range.isGroup = 0;
					node.range.indexOrOffset = lastLayerOffset + childBegin;
					node.range.countMinusOne = childCount - 1;

					float maxErr = 0.f;
					for (uint32_t c = 0; c < childCount; ++c)
						maxErr = std::max(maxErr, children[c].traversalMetric.maxQuadricError);
					node.traversalMetric.maxQuadricError = maxErr;

					meshopt_Bounds mergedCull = meshopt_computeSphereBounds(
						&children[0].traversalMetric.cullingSphere.x,
						childCount,
						sizeof(ClusterLODNode),
						&children[0].traversalMetric.cullingSphere.w,
						sizeof(ClusterLODNode));
					meshopt_Bounds mergedLod = meshopt_computeSphereBounds(
						&children[0].traversalMetric.lodBoundingSphere.x,
						childCount,
						sizeof(ClusterLODNode),
						&children[0].traversalMetric.lodBoundingSphere.w,
						sizeof(ClusterLODNode));

					node.traversalMetric.cullingSphere = DirectX::XMFLOAT4(
						mergedCull.center[0],
						mergedCull.center[1],
						mergedCull.center[2],
						mergedCull.radius * (1.0f + 1e-5f));
					node.traversalMetric.lodBoundingSphere = DirectX::XMFLOAT4(
						mergedLod.center[0],
						mergedLod.center[1],
						mergedLod.center[2],
						mergedLod.radius * (1.0f + 1e-5f));
				}

				lastLayerOffset = writeOffset;
				writeOffset += iterCount;
			}

			writeOffset--;
			if (range.offset + range.count != writeOffset) {
				throw std::runtime_error("Cluster LOD: traversal node allocation mismatch (range/count).");
			}
		}

		{
			auto BuildInternalNode = [&](uint32_t childOffset, uint32_t childCount) -> ClusterLODNode {
				if (childCount == 0)
					throw std::runtime_error("Cluster LOD: internal node with zero children");

				ClusterLODNode node{};
				node.range.isGroup = 0;
				node.range.indexOrOffset = childOffset;
				node.range.countMinusOne = childCount - 1;

				const ClusterLODNode* children = &state.nodes[childOffset];

				float maxErr = 0.f;
				for (uint32_t c = 0; c < childCount; ++c)
					maxErr = std::max(maxErr, children[c].traversalMetric.maxQuadricError);
				node.traversalMetric.maxQuadricError = maxErr;

				meshopt_Bounds mergedCull = meshopt_computeSphereBounds(
					&children[0].traversalMetric.cullingSphere.x,
					childCount,
					sizeof(ClusterLODNode),
					&children[0].traversalMetric.cullingSphere.w,
					sizeof(ClusterLODNode));
				meshopt_Bounds mergedLod = meshopt_computeSphereBounds(
					&children[0].traversalMetric.lodBoundingSphere.x,
					childCount,
					sizeof(ClusterLODNode),
					&children[0].traversalMetric.lodBoundingSphere.w,
					sizeof(ClusterLODNode));

				node.traversalMetric.cullingSphere = DirectX::XMFLOAT4(
					mergedCull.center[0],
					mergedCull.center[1],
					mergedCull.center[2],
					mergedCull.radius * (1.0f + 1e-5f));
				node.traversalMetric.lodBoundingSphere = DirectX::XMFLOAT4(
					mergedLod.center[0],
					mergedLod.center[1],
					mergedLod.center[2],
					mergedLod.radius * (1.0f + 1e-5f));

				return node;
			};

			std::vector<uint32_t> currentLayer;
			currentLayer.reserve(lodLevelCount);
			for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
				currentLayer.push_back(state.lodLevelRoots[depth]);

			while (currentLayer.size() > preferredNodeWidth)
			{
				std::vector<uint32_t> nextLayer;
				nextLayer.reserve((currentLayer.size() + preferredNodeWidth - 1) / preferredNodeWidth);

				for (uint32_t begin = 0; begin < currentLayer.size(); begin += preferredNodeWidth)
				{
					const uint32_t childCount = std::min<uint32_t>(preferredNodeWidth, uint32_t(currentLayer.size()) - begin);
					const uint32_t childOffset = currentLayer[begin];

					for (uint32_t c = 1; c < childCount; ++c)
					{
						if (currentLayer[begin + c] != childOffset + c)
						{
							throw std::runtime_error("Cluster LOD: expected contiguous node ids while building top hierarchy");
						}
					}

					ClusterLODNode parent = BuildInternalNode(childOffset, childCount);
					const uint32_t parentId = uint32_t(state.nodes.size());
					state.nodes.push_back(parent);
					nextLayer.push_back(parentId);
				}

				currentLayer = std::move(nextLayer);
			}

			if (currentLayer.empty())
				throw std::runtime_error("Cluster LOD: top hierarchy has no roots");

			const uint32_t rootChildOffset = currentLayer.front();
			const uint32_t rootChildCount = uint32_t(currentLayer.size());

			for (uint32_t c = 1; c < rootChildCount; ++c)
			{
				if (currentLayer[c] != rootChildOffset + c)
				{
					throw std::runtime_error("Cluster LOD: expected contiguous root children in top hierarchy");
				}
			}

			ClusterLODNode& root = state.nodes[0];
			root = BuildInternalNode(rootChildOffset, rootChildCount);

			state.topRootNode = 0;
		}
	}
}

ClusterLODPrebuildArtifacts BuildClusterLODArtifactsFromGeometry(
	const std::vector<std::byte>& vertices,
	unsigned int vertexSize,
	const std::vector<std::byte>* skinningVertices,
	unsigned int skinningVertexSize,
	const std::vector<uint32_t>& indices,
	const std::vector<MeshUvSetData>& uvSets,
	unsigned int flags,
	const ClusterLODBuilderSettings& settings)
{
	ClusterLODBuildState state{};

	const unsigned int* idx = reinterpret_cast<const unsigned int*>(indices.data());

	const size_t vertexStrideBytes = vertexSize;
	const size_t globalVertexCount = vertices.size() / vertexStrideBytes;
	const uint32_t meshPositionQuantExp = ComputeMeshQuantizationExponent(vertices, vertexStrideBytes);
	const float meshPositionQuantScale = static_cast<float>(1u << meshPositionQuantExp);

	const bool enableNormalAttributeSimplification = settings.enableNormalAttributeSimplification;
	const float normalAttributeWeight = std::max(0.0f, settings.normalAttributeWeight);
	const float tangentAttributeWeight = std::max(0.0f, settings.simplifyTangentWeight);
	const float tangentSignAttributeWeight = std::max(0.0f, settings.simplifyTangentSignWeight);
	const bool hasNormalStreamInSource = (flags & VertexFlags::VERTEX_NORMALS) != 0u && vertexStrideBytes >= sizeof(float) * 6;
	const bool hasTexcoordStreamInSource = (flags & VertexFlags::VERTEX_TEXCOORDS) != 0u && vertexStrideBytes >= sizeof(float) * 8;
	const bool recomputeGroupNormals = hasNormalStreamInSource && !settings.preserveImportedNormals;
	std::vector<float> simplifyAttributeStream;
	std::vector<float> simplifyAttributeWeights;
	uint32_t simplifyAttributeCount = 0;
	uint32_t simplifyProtectMask = 0;
	std::vector<DirectX::XMFLOAT4> tangentAttributeStream;

	if (enableNormalAttributeSimplification && hasNormalStreamInSource && hasTexcoordStreamInSource)
	{
		if (!GenerateMikkTangents(vertices, vertexStrideBytes, indices, tangentAttributeStream))
		{
			spdlog::warn("ClusterLOD: failed to generate MikkTSpace tangents; continuing without tangent simplification attributes");
			tangentAttributeStream.clear();
		}
	}

	if (enableNormalAttributeSimplification && hasNormalStreamInSource)
	{
		simplifyAttributeWeights.push_back(normalAttributeWeight);
		simplifyAttributeWeights.push_back(normalAttributeWeight);
		simplifyAttributeWeights.push_back(normalAttributeWeight);
		simplifyProtectMask |= ((1u << 3u) - 1u) << simplifyAttributeCount;
		simplifyAttributeCount += 3u;
	}

	if (enableNormalAttributeSimplification && !tangentAttributeStream.empty())
	{
		simplifyAttributeWeights.push_back(tangentAttributeWeight);
		simplifyAttributeWeights.push_back(tangentAttributeWeight);
		simplifyAttributeWeights.push_back(tangentAttributeWeight);
		simplifyAttributeWeights.push_back(tangentSignAttributeWeight);
		simplifyProtectMask |= ((1u << 4u) - 1u) << simplifyAttributeCount;
		simplifyAttributeCount += 4u;
	}

	if (simplifyAttributeCount > 0u)
	{
		simplifyAttributeStream.resize(globalVertexCount * static_cast<size_t>(simplifyAttributeCount));
		for (size_t vertexIndex = 0; vertexIndex < globalVertexCount; ++vertexIndex)
		{
			size_t destinationFloatOffset = vertexIndex * static_cast<size_t>(simplifyAttributeCount);

			if (enableNormalAttributeSimplification && hasNormalStreamInSource)
			{
				const size_t normalSourceByteOffset = vertexIndex * vertexStrideBytes + sizeof(float) * 3;
				std::memcpy(&simplifyAttributeStream[destinationFloatOffset], vertices.data() + normalSourceByteOffset, sizeof(float) * 3);
				destinationFloatOffset += 3ull;
			}

			if (enableNormalAttributeSimplification && !tangentAttributeStream.empty())
			{
				const DirectX::XMFLOAT4 tangent = tangentAttributeStream[vertexIndex];
				simplifyAttributeStream[destinationFloatOffset + 0ull] = tangent.x;
				simplifyAttributeStream[destinationFloatOffset + 1ull] = tangent.y;
				simplifyAttributeStream[destinationFloatOffset + 2ull] = tangent.z;
				simplifyAttributeStream[destinationFloatOffset + 3ull] = tangent.w;
			}
		}
	}

	clodMesh mesh{};
	mesh.indices = idx;
	mesh.index_count = indices.size();
	mesh.vertex_count = globalVertexCount;
	mesh.vertex_positions = reinterpret_cast<const float*>(vertices.data());
	mesh.vertex_positions_stride = vertexStrideBytes;

	mesh.vertex_attributes = simplifyAttributeStream.empty() ? nullptr : simplifyAttributeStream.data();
	mesh.vertex_attributes_stride = simplifyAttributeStream.empty() ? 0 : sizeof(float) * simplifyAttributeCount;
	mesh.vertex_lock = nullptr;
	mesh.attribute_weights = simplifyAttributeWeights.empty() ? nullptr : simplifyAttributeWeights.data();
	mesh.attribute_count = simplifyAttributeStream.empty() ? 0 : simplifyAttributeCount;
	mesh.attribute_protect_mask = simplifyAttributeStream.empty() ? 0 : simplifyProtectMask;

	clodConfig config = clodDefaultConfig(/*max_triangles=*/MS_MESHLET_SIZE);
	config.max_vertices = MS_MESHLET_SIZE;
	config.max_triangles = MS_MESHLET_SIZE;
	config.min_triangles = MS_MESHLET_MIN_SIZE;
	config.cluster_spatial = true;
	config.cluster_fill_weight = 0.5f;
	config.cluster_split_factor = 2.0f;
	config.partition_spatial = true;
	config.partition_sort = true;
	config.optimize_clusters = true;
	config.optimize_bounds = true;

	const bool disableSloppyFallback = settings.disableSloppyFallback;
	const float lodErrorMergeAdditive = std::max(0.0f, settings.lodErrorMergeAdditive);
	const float lodErrorMergePrevious = std::max(0.0f, settings.lodErrorMergePrevious);
	const uint32_t partitionSizeFloor = std::max<uint32_t>(1u, settings.partitionSizeFloor);

	config.simplify_fallback_sloppy = true; // TODO: Useful?
	config.simplify_error_factor_sloppy = 100.0f; // Scales error for sloppy groups

	config.simplify_fallback_permissive = false; // Simplify in permissive, disable fallback-only

	config.simplify_error_merge_additive = lodErrorMergeAdditive;
	config.simplify_error_merge_previous = lodErrorMergePrevious;

	constexpr uint32_t MaxGroupChildren = 8;
	constexpr uint32_t TraversalNodeFanout = 8;
	constexpr uint32_t TargetBucketClusters = 512;
	config.partition_max_refined_groups = 8;

	{
		const size_t requestedPartitionSize = std::max<size_t>(1, (TargetBucketClusters * 3) / 4);
		config.partition_size = std::max<size_t>(requestedPartitionSize, static_cast<size_t>(partitionSizeFloor));
		size_t refinedCapSplitPartitionCount = 0;
		config.partition_refined_split_count = &refinedCapSplitPartitionCount;

		struct CaptureOutputContext
		{
			const std::vector<std::byte>* vertices = nullptr;
			const std::vector<MeshUvSetData>* uvSets = nullptr;
			size_t vertexStrideBytes = 0;
			const std::vector<std::byte>* skinningVertices = nullptr;
			size_t skinningVertexStrideBytes = 0;
			std::vector<ClusterLODGroup>* groups = nullptr;
			std::vector<ClusterLODGroupSegment>* segments = nullptr;
			std::vector<BoundingSphere>* segmentBounds = nullptr;
			std::vector<ClusterLODGroupChunk>* groupChunks = nullptr;
			std::vector<std::vector<std::vector<std::byte>>>* groupPageBlobs = nullptr;
			// Raw per-group streams for voxel hierarchy
			std::vector<std::vector<std::byte>>* groupVertexChunks = nullptr;
			std::vector<std::vector<uint32_t>>* groupMeshletVertexChunks = nullptr;
			std::vector<std::vector<meshopt_Meshlet>>* groupMeshletChunks = nullptr;
			std::vector<std::vector<uint8_t>>* groupMeshletTriangleChunks = nullptr;
			float meshPositionQuantScale = 1.0f;
			uint32_t meshPositionQuantExp = 0;
			bool recomputeGroupNormals = false;
			std::atomic<uint32_t> nextGroupId = 0;
			std::mutex finalizeMutex;
			uint32_t cumulativeMeshletCount = 0;
			uint32_t cumulativeGroupVertexCount = 0;
			uint32_t maxChildrenObserved = 0;
			uint32_t maxDepthObserved = 0;
		};

		struct ClodBuildCallbacks
		{
			static int Output(void* outputContext, clodGroup group, const clodCluster* clusters, size_t clusterCount, size_t, unsigned int)
			{
				CaptureOutputContext* context = static_cast<CaptureOutputContext*>(outputContext);
				const uint32_t groupId = context->nextGroupId.fetch_add(1u, std::memory_order_relaxed);

				CapturedClusterLODGroup capturedGroup{};
				capturedGroup.depth = group.depth;
				capturedGroup.simplified = group.simplified;
				capturedGroup.clusters.reserve(clusterCount);
				capturedGroup.flattenedIndices.reserve(clusterCount * MS_MESHLET_SIZE * 3);

				for (size_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
				{
					const clodCluster& cluster = clusters[clusterIndex];

					CapturedClusterLODCluster capturedCluster{};
					capturedCluster.refinedGroup = static_cast<int32_t>(cluster.refined);
					capturedCluster.bounds = cluster.bounds;
					capturedCluster.vertexCount = static_cast<uint32_t>(cluster.vertex_count);
					capturedCluster.indicesOffset = static_cast<uint32_t>(capturedGroup.flattenedIndices.size());
					capturedCluster.indexCount = static_cast<uint32_t>(cluster.index_count);
					capturedGroup.flattenedIndices.insert(
						capturedGroup.flattenedIndices.end(),
						cluster.indices,
						cluster.indices + cluster.index_count);

					capturedGroup.clusters.push_back(std::move(capturedCluster));
				}

				ClusterLODGroupBuildOutput output = BuildClusterLODGroupOutput(
					capturedGroup,
					*context->vertices,
					*context->uvSets,
					context->vertexStrideBytes,
					context->skinningVertices,
					context->skinningVertexStrideBytes,
					context->meshPositionQuantScale,
					context->meshPositionQuantExp,
					context->recomputeGroupNormals);

				ClusterLODGroup finalizedGroup = output.group;

				std::lock_guard<std::mutex> lock(context->finalizeMutex);

				auto ensureIndexedStorage = [&](auto& container)
					{
						if (container.size() <= groupId)
						{
							container.resize(static_cast<size_t>(groupId) + 1ull);
						}
					};

				ensureIndexedStorage(*context->groups);
				ensureIndexedStorage(*context->groupChunks);
				ensureIndexedStorage(*context->groupPageBlobs);
				ensureIndexedStorage(*context->groupVertexChunks);
				ensureIndexedStorage(*context->groupMeshletVertexChunks);
				ensureIndexedStorage(*context->groupMeshletChunks);
				ensureIndexedStorage(*context->groupMeshletTriangleChunks);

				finalizedGroup.firstMeshlet = context->cumulativeMeshletCount;
				finalizedGroup.firstGroupVertex = context->cumulativeGroupVertexCount;
				finalizedGroup.firstSegment = static_cast<uint32_t>(context->segments->size());

				context->cumulativeMeshletCount += finalizedGroup.meshletCount;
				context->cumulativeGroupVertexCount += finalizedGroup.groupVertexCount;
				context->segments->insert(context->segments->end(), output.segments.begin(), output.segments.end());
				context->segmentBounds->insert(context->segmentBounds->end(), output.segmentBounds.begin(), output.segmentBounds.end());

				(*context->groupPageBlobs)[groupId] = std::move(output.pageBlobs);

				// Store raw streams for voxel hierarchy building
				(*context->groupVertexChunks)[groupId] = std::move(output.vertexChunk);
				(*context->groupMeshletVertexChunks)[groupId] = std::move(output.meshletVertices);
				(*context->groupMeshletChunks)[groupId] = std::move(output.meshlets);
				(*context->groupMeshletTriangleChunks)[groupId] = std::move(output.meshletTriangles);

				(*context->groupChunks)[groupId] = output.groupChunk;
				(*context->groups)[groupId] = finalizedGroup;

		context->maxChildrenObserved = std::max(context->maxChildrenObserved, finalizedGroup.segmentCount);
				context->maxDepthObserved = (std::max)(context->maxDepthObserved, static_cast<uint32_t>(std::max(finalizedGroup.depth, 0)));

				return static_cast<int>(groupId);
			}

			static void Iterate(void* iterationContext, void*, int, size_t taskCount)
			{
				TaskSchedulerManager::GetInstance().ParallelFor("ClusterLODUtilities::BuildIteration", taskCount, [&](size_t taskIndex)
					{
						clodBuild_iterationTask(iterationContext, taskIndex, 0);
					});
			}
		};

		CaptureOutputContext captureContext{};
		captureContext.vertices = &vertices;
		captureContext.uvSets = &uvSets;
		captureContext.vertexStrideBytes = vertexStrideBytes;
		captureContext.skinningVertices = skinningVertices;
		captureContext.skinningVertexStrideBytes = skinningVertexSize;
		captureContext.groups = &state.groups;
		captureContext.segments = &state.segments;
		captureContext.segmentBounds = &state.segmentBounds;
		captureContext.groupChunks = &state.groupChunks;
		captureContext.groupPageBlobs = &state.groupPageBlobs;
		captureContext.groupVertexChunks = &state.groupVertexChunks;
		captureContext.groupMeshletVertexChunks = &state.groupMeshletVertexChunks;
		captureContext.groupMeshletChunks = &state.groupMeshletChunks;
		captureContext.groupMeshletTriangleChunks = &state.groupMeshletTriangleChunks;
		captureContext.meshPositionQuantScale = meshPositionQuantScale;
		captureContext.meshPositionQuantExp = meshPositionQuantExp;
		captureContext.recomputeGroupNormals = recomputeGroupNormals;
		clodBuildParallelConfig parallelConfig{};
		parallelConfig.iteration_callback = &ClodBuildCallbacks::Iterate;
		const clodBuildParallelConfig* parallelConfigPtr = TaskSchedulerManager::GetInstance().GetNumTaskThreads() > 1u ? &parallelConfig : nullptr;

		clodBuildEx(config, mesh, &captureContext, &ClodBuildCallbacks::Output, parallelConfigPtr);

		state.maxDepth = captureContext.maxDepthObserved;

		for (size_t groupIndex = 0; groupIndex < state.groups.size(); ++groupIndex)
		{
			if (state.groups[groupIndex].groupVertexCount != state.groupChunks[groupIndex].groupVertexCount)
			{
				state.groupChunks[groupIndex].groupVertexCount = state.groups[groupIndex].groupVertexCount;
			}
		}

		if (refinedCapSplitPartitionCount > 0)
		{
			spdlog::info(
				"ClusterLOD: refined-group cap split {} partitions at bucket target {}",
				refinedCapSplitPartitionCount,
				TargetBucketClusters);
		}
	}

	const uint32_t totalGroupCount = static_cast<uint32_t>(state.groups.size());
	uint32_t groupsWithRefinedChildren = 0;
	for (const ClusterLODGroup& group : state.groups)
	{
		if (group.segmentCount > group.terminalSegmentCount)
		{
			groupsWithRefinedChildren++;
		}
	}

	const float refinedGroupRatio = totalGroupCount > 0
		? static_cast<float>(groupsWithRefinedChildren) / static_cast<float>(totalGroupCount)
		: 0.0f;

	spdlog::info(
		"ClusterLOD metrics: groups={} segments={} refined_groups={} refined_ratio={:.3f} normal_attributes={} tangent_attributes={}",
		totalGroupCount,
		static_cast<uint32_t>(state.segments.size()),
		groupsWithRefinedChildren,
		refinedGroupRatio,
		hasNormalStreamInSource && enableNormalAttributeSimplification ? 1 : 0,
		(!tangentAttributeStream.empty() && enableNormalAttributeSimplification) ? 1 : 0);

	{
		std::vector<uint32_t> refinedGroupParentCounts(state.groups.size(), 0);
		for (const ClusterLODGroupSegment& seg : state.segments)
		{
			if (seg.refinedGroup >= 0)
			{
				const uint32_t refinedGroup = static_cast<uint32_t>(seg.refinedGroup);
				if (refinedGroup < refinedGroupParentCounts.size())
				{
					refinedGroupParentCounts[refinedGroup]++;
				}
			}
		}

		uint32_t groupsWithMultipleParents = 0;
		uint32_t maxParentCount = 0;
		for (uint32_t parentCount : refinedGroupParentCounts)
		{
			if (parentCount > 1)
			{
				groupsWithMultipleParents++;
				maxParentCount = std::max(maxParentCount, parentCount);
			}
		}
	}

	// Voxel hierarchy: detect terminal coarsest groups and build voxel group tree
	// Disabled for now, until voxel rendering is implemented
	if (settings.enableVoxelFallback && state.maxDepth > 0)
	{
		// Find groups at the coarsest depth that are fully terminal (no further refinement possible).
		std::vector<uint32_t> terminalCoarsestGroupIndices;
		for (uint32_t gIdx = 0; gIdx < static_cast<uint32_t>(state.groups.size()); ++gIdx)
		{
			const ClusterLODGroup& grp = state.groups[gIdx];
			if (static_cast<uint32_t>(grp.depth) == state.maxDepth && grp.segmentCount > 0 && grp.segmentCount == grp.terminalSegmentCount)
			{
				terminalCoarsestGroupIndices.push_back(gIdx);
			}
		}

		if (!terminalCoarsestGroupIndices.empty())
		{
			const uint32_t baseResolution = settings.voxelGridBaseResolution;
			const uint32_t coarsenFactor  = std::max(2u, settings.voxelCoarsenFactor);
			const uint32_t minResolution  = std::max(1u, settings.voxelMinResolution);
			const uint32_t raysPerCell    = settings.voxelRaysPerCell;
			const float errorMultiplier   = settings.voxelTerminalErrorMultiplier;
			const uint32_t originalMaxDepth = state.maxDepth;
			const size_t   originalGroupCount = state.groups.size();

			// Helper: read vertex position from a group's vertex chunk.
			auto readGroupPos = [&](uint32_t grpIdx, uint32_t groupLocalVertex) -> DirectX::XMFLOAT3
			{
				DirectX::XMFLOAT3 p{};
				if (grpIdx >= state.groupVertexChunks.size()) return p;
				const auto& chunk = state.groupVertexChunks[grpIdx];
				const size_t off = static_cast<size_t>(groupLocalVertex) * vertexStrideBytes;
				if (off + sizeof(float) * 3 <= chunk.size())
				{
					std::memcpy(&p.x, chunk.data() + off, sizeof(float));
					std::memcpy(&p.y, chunk.data() + off + sizeof(float), sizeof(float));
					std::memcpy(&p.z, chunk.data() + off + sizeof(float) * 2, sizeof(float));
				}
				return p;
			};

			// Helper: extract all triangle indices + AABB from one mesh group's meshlet data.
			auto extractGroupTriangles = [&](uint32_t grpIdx,
				std::vector<uint32_t>& outTriIndices,
				DirectX::XMFLOAT3& outMin,
				DirectX::XMFLOAT3& outMax)
			{
				if (grpIdx >= state.groupVertexChunks.size() || state.groupVertexChunks[grpIdx].empty()) return;
				if (grpIdx >= state.groupMeshletChunks.size()) return;
				const auto& meshlets       = state.groupMeshletChunks[grpIdx];
				const auto& meshletTris    = state.groupMeshletTriangleChunks[grpIdx];
				const auto& meshletVerts   = state.groupMeshletVertexChunks[grpIdx];

				for (const auto& meshlet : meshlets)
				{
					for (uint32_t triIdx = 0; triIdx < meshlet.triangle_count; ++triIdx)
					{
						const uint32_t triBase = meshlet.triangle_offset + triIdx * 3u;
						if (triBase + 2u >= meshletTris.size()) continue;
						const uint32_t ml0 = static_cast<uint32_t>(meshletTris[triBase + 0u]);
						const uint32_t ml1 = static_cast<uint32_t>(meshletTris[triBase + 1u]);
						const uint32_t ml2 = static_cast<uint32_t>(meshletTris[triBase + 2u]);
						if (ml0 >= meshlet.vertex_count || ml1 >= meshlet.vertex_count || ml2 >= meshlet.vertex_count) continue;

						const uint32_t gv0 = meshletVerts[meshlet.vertex_offset + ml0];
						const uint32_t gv1 = meshletVerts[meshlet.vertex_offset + ml1];
						const uint32_t gv2 = meshletVerts[meshlet.vertex_offset + ml2];

						DirectX::XMFLOAT3 p0 = readGroupPos(grpIdx, gv0);
						DirectX::XMFLOAT3 p1 = readGroupPos(grpIdx, gv1);
						DirectX::XMFLOAT3 p2 = readGroupPos(grpIdx, gv2);

						outMin.x = std::min({ outMin.x, p0.x, p1.x, p2.x });
						outMin.y = std::min({ outMin.y, p0.y, p1.y, p2.y });
						outMin.z = std::min({ outMin.z, p0.z, p1.z, p2.z });
						outMax.x = std::max({ outMax.x, p0.x, p1.x, p2.x });
						outMax.y = std::max({ outMax.y, p0.y, p1.y, p2.y });
						outMax.z = std::max({ outMax.z, p0.z, p1.z, p2.z });

						outTriIndices.push_back(gv0);
						outTriIndices.push_back(gv1);
						outTriIndices.push_back(gv2);
					}
				}
			};

			// Helper: append a voxel group + empty chunk placeholders to the build state.
			auto appendVoxelGroup = [&](const ClusterLODGroup& grp) -> uint32_t
			{
				const uint32_t idx = static_cast<uint32_t>(state.groups.size());
				state.groups.push_back(grp);
				state.groupChunks.push_back(ClusterLODGroupChunk{});
				state.groupVertexChunks.push_back({});
				state.groupMeshletVertexChunks.push_back({});
				state.groupMeshletChunks.push_back({});
				state.groupMeshletTriangleChunks.push_back({});
				return idx;
			};

			// Morton-sort terminal groups by center, merge into spatial batches
			float maxTerminalError = 0.0f;
			std::vector<DirectX::XMFLOAT3> termCenters(terminalCoarsestGroupIndices.size());
			DirectX::XMFLOAT3 tGlobalMin = { FLT_MAX, FLT_MAX, FLT_MAX };
			DirectX::XMFLOAT3 tGlobalMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

			for (size_t i = 0; i < terminalCoarsestGroupIndices.size(); ++i)
			{
				const auto& b = state.groups[terminalCoarsestGroupIndices[i]].bounds;
				maxTerminalError = std::max(maxTerminalError, b.error);
				termCenters[i] = DirectX::XMFLOAT3(b.center[0], b.center[1], b.center[2]);
				tGlobalMin.x = std::min(tGlobalMin.x, b.center[0]);
				tGlobalMin.y = std::min(tGlobalMin.y, b.center[1]);
				tGlobalMin.z = std::min(tGlobalMin.z, b.center[2]);
				tGlobalMax.x = std::max(tGlobalMax.x, b.center[0]);
				tGlobalMax.y = std::max(tGlobalMax.y, b.center[1]);
				tGlobalMax.z = std::max(tGlobalMax.z, b.center[2]);
			}

			std::vector<uint32_t> sortedTermIndices = MortonSort(
				termCenters.data(), static_cast<uint32_t>(termCenters.size()),
				tGlobalMin,
				tGlobalMax);

			std::vector<std::vector<uint32_t>> leafBatches = MergeGroupsSpatial(sortedTermIndices, MaxGroupChildren);

			// Create leaf-level voxel groups (depth = maxDepth + 1)
			struct VoxelLevelGroup
			{
				uint32_t groupIndex;       // index in state.groups
				uint32_t payloadIndex;     // index in voxelGroupMapping.payloads
				DirectX::XMFLOAT3 center;
			};

			uint32_t currentVoxelDepth = originalMaxDepth + 1;
			float currentLevelError = maxTerminalError * errorMultiplier;
			std::vector<VoxelLevelGroup> currentLevelGroups;

			for (const auto& batch : leafBatches)
			{
				// Collect combined triangles from all terminal mesh groups in this batch.
				std::vector<std::byte> batchVertices;
				std::vector<uint32_t> batchTriIndices;
				DirectX::XMFLOAT3 batchMin = { FLT_MAX, FLT_MAX, FLT_MAX };
				DirectX::XMFLOAT3 batchMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
				size_t batchVertexCount = 0;

				for (uint32_t termLocalIdx : batch)
				{
					const uint32_t meshGrpIdx = terminalCoarsestGroupIndices[termLocalIdx];
					if (meshGrpIdx >= state.groupVertexChunks.size()) continue;
					const auto& chunk = state.groupVertexChunks[meshGrpIdx];
					if (chunk.empty()) continue;

					const uint32_t baseVertex = static_cast<uint32_t>(batchVertexCount);
					batchVertices.insert(batchVertices.end(), chunk.begin(), chunk.end());
					batchVertexCount += chunk.size() / vertexStrideBytes;

					// Extract triangles using the per-group meshlet data.
					std::vector<uint32_t> groupTriIndices;
					DirectX::XMFLOAT3 groupMin = { FLT_MAX, FLT_MAX, FLT_MAX };
					DirectX::XMFLOAT3 groupMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
					extractGroupTriangles(meshGrpIdx, groupTriIndices, groupMin, groupMax);

					// Re-index group-local indices into the batch-local vertex range.
					for (uint32_t idx : groupTriIndices)
						batchTriIndices.push_back(baseVertex + idx);

					batchMin.x = std::min(batchMin.x, groupMin.x);
					batchMin.y = std::min(batchMin.y, groupMin.y);
					batchMin.z = std::min(batchMin.z, groupMin.z);
					batchMax.x = std::max(batchMax.x, groupMax.x);
					batchMax.y = std::max(batchMax.y, groupMax.y);
					batchMax.z = std::max(batchMax.z, groupMax.z);
				}

				if (batchTriIndices.empty()) continue;

				// Slightly expand AABB for boundary precision.
				const float eps = 1e-4f;
				batchMin.x -= eps; batchMin.y -= eps; batchMin.z -= eps;
				batchMax.x += eps; batchMax.y += eps; batchMax.z += eps;

				// Voxelize combined triangles.
				VoxelizeTrianglesInput voxInput{};
				voxInput.vertices        = &batchVertices;
				voxInput.vertexStrideBytes = vertexStrideBytes;
				voxInput.triangleIndices  = &batchTriIndices;
				voxInput.aabbMin          = { batchMin.x, batchMin.y, batchMin.z };
				voxInput.aabbMax          = { batchMax.x, batchMax.y, batchMax.z };
				voxInput.resolution       = baseResolution;
				voxInput.raysPerCell      = raysPerCell;

				VoxelGroupPayload batchPayload = VoxelizeTriangles(voxInput);

				if (batchPayload.activeCells.empty()) continue;

				const uint32_t payloadIdx = static_cast<uint32_t>(state.voxelGroupMapping.payloads.size());
				state.voxelGroupMapping.payloads.push_back(std::move(batchPayload));

				// Create child entries that refine into the terminal mesh groups.
				const uint32_t firstSegmentIdx = static_cast<uint32_t>(state.segments.size());
				for (uint32_t termLocalIdx : batch)
				{
					ClusterLODGroupSegment seg{};
					seg.refinedGroup = static_cast<int32_t>(terminalCoarsestGroupIndices[termLocalIdx]);
					seg.firstMeshletInPage = 0;
					seg.meshletCount = 0;
					state.segments.push_back(seg);
				}

				// Compute bounding sphere (center of AABB, radius = half-diagonal).
				const float cx = (batchMin.x + batchMax.x) * 0.5f;
				const float cy = (batchMin.y + batchMax.y) * 0.5f;
				const float cz = (batchMin.z + batchMax.z) * 0.5f;
				const float dx = batchMax.x - batchMin.x;
				const float dy = batchMax.y - batchMin.y;
				const float dz = batchMax.z - batchMin.z;
				const float radius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;

				ClusterLODGroup voxelGroup{};
				voxelGroup.bounds.center[0] = cx;
				voxelGroup.bounds.center[1] = cy;
				voxelGroup.bounds.center[2] = cz;
				voxelGroup.bounds.radius     = radius;
				voxelGroup.bounds.error      = currentLevelError;
				voxelGroup.depth             = static_cast<int32_t>(currentVoxelDepth);
				voxelGroup.firstSegment        = firstSegmentIdx;
				voxelGroup.segmentCount        = static_cast<uint32_t>(batch.size());
				voxelGroup.terminalSegmentCount = 0;
				voxelGroup.meshletCount      = 0;
				voxelGroup.groupVertexCount  = 0;
				voxelGroup.firstMeshlet      = 0;
				voxelGroup.firstGroupVertex  = 0;
				voxelGroup.flags             = CLOD_GROUP_FLAG_IS_VOXEL;

				const uint32_t newGroupIdx = appendVoxelGroup(voxelGroup);

				state.voxelGroupMapping.groupToPayloadIndex.resize(state.groups.size(), -1);
				state.voxelGroupMapping.groupToPayloadIndex[newGroupIdx] = static_cast<int32_t>(payloadIdx);

				VoxelLevelGroup lvl{};
				lvl.groupIndex   = newGroupIdx;
				lvl.payloadIndex = payloadIdx;
				lvl.center       = { cx, cy, cz };
				currentLevelGroups.push_back(lvl);
			}

			// Iterative coarsening loop
			uint32_t currentResolution = baseResolution;

			while (currentLevelGroups.size() > 1)
			{
				const uint32_t parentResolution = currentResolution / coarsenFactor;
				if (parentResolution < minResolution) break;

				currentVoxelDepth++;
				const float parentError = currentLevelError * errorMultiplier;

				// Morton-sort current-level groups by center.
				DirectX::XMFLOAT3 lvlMin = { FLT_MAX, FLT_MAX, FLT_MAX };
				DirectX::XMFLOAT3 lvlMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
				std::vector<DirectX::XMFLOAT3> lvlCenters(currentLevelGroups.size());

				for (size_t i = 0; i < currentLevelGroups.size(); ++i)
				{
					const auto& c = currentLevelGroups[i].center;
					lvlCenters[i] = c;
					lvlMin.x = std::min(lvlMin.x, c.x);
					lvlMin.y = std::min(lvlMin.y, c.y);
					lvlMin.z = std::min(lvlMin.z, c.z);
					lvlMax.x = std::max(lvlMax.x, c.x);
					lvlMax.y = std::max(lvlMax.y, c.y);
					lvlMax.z = std::max(lvlMax.z, c.z);
				}

				std::vector<uint32_t> sortedLvlIndices = MortonSort(
					lvlCenters.data(), static_cast<uint32_t>(currentLevelGroups.size()),
					lvlMin,
					lvlMax);

				std::vector<std::vector<uint32_t>> parentBatches = MergeGroupsSpatial(sortedLvlIndices, MaxGroupChildren);

				std::vector<VoxelLevelGroup> nextLevelGroups;

				for (const auto& batch : parentBatches)
				{
					// Gather child payloads and compute combined AABB.
					std::vector<const VoxelGroupPayload*> childPayloads;
					childPayloads.reserve(batch.size());
					DirectX::XMFLOAT3 parentMin = { FLT_MAX, FLT_MAX, FLT_MAX };
					DirectX::XMFLOAT3 parentMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

					for (uint32_t batchIdx : batch)
					{
						const auto& childGrp = currentLevelGroups[batchIdx];
						const auto& cp = state.voxelGroupMapping.payloads[childGrp.payloadIndex];
						childPayloads.push_back(&cp);
						parentMin.x = std::min(parentMin.x, cp.aabbMin.x);
						parentMin.y = std::min(parentMin.y, cp.aabbMin.y);
						parentMin.z = std::min(parentMin.z, cp.aabbMin.z);
						parentMax.x = std::max(parentMax.x, cp.aabbMax.x);
						parentMax.y = std::max(parentMax.y, cp.aabbMax.y);
						parentMax.z = std::max(parentMax.z, cp.aabbMax.z);
					}

					const float eps = 1e-4f;
					parentMin.x -= eps; parentMin.y -= eps; parentMin.z -= eps;
					parentMax.x += eps; parentMax.y += eps; parentMax.z += eps;

					// VoxelizeVoxels: reduce child voxel payloads into coarser parent.
					VoxelizeVoxelsInput vvInput{};
					vvInput.children    = &childPayloads;
					vvInput.aabbMin     = { parentMin.x, parentMin.y, parentMin.z };
					vvInput.aabbMax     = { parentMax.x, parentMax.y, parentMax.z };
					vvInput.resolution  = parentResolution;
					vvInput.raysPerCell = raysPerCell;

					VoxelGroupPayload parentPayload = VoxelizeVoxels(vvInput);

					if (parentPayload.activeCells.empty()) continue;

					const uint32_t payloadIdx = static_cast<uint32_t>(state.voxelGroupMapping.payloads.size());
					state.voxelGroupMapping.payloads.push_back(std::move(parentPayload));

					// Children entries that refine into the child voxel groups.
					const uint32_t firstSegmentIdx = static_cast<uint32_t>(state.segments.size());
					for (uint32_t batchIdx : batch)
					{
						ClusterLODGroupSegment seg{};
						seg.refinedGroup = static_cast<int32_t>(currentLevelGroups[batchIdx].groupIndex);
						seg.firstMeshletInPage = 0;
						seg.meshletCount = 0;
						state.segments.push_back(seg);
					}

					const float pcx = (parentMin.x + parentMax.x) * 0.5f;
					const float pcy = (parentMin.y + parentMax.y) * 0.5f;
					const float pcz = (parentMin.z + parentMax.z) * 0.5f;
					const float pdx = parentMax.x - parentMin.x;
					const float pdy = parentMax.y - parentMin.y;
					const float pdz = parentMax.z - parentMin.z;
					const float pRadius = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz) * 0.5f;

					ClusterLODGroup parentGroup{};
					parentGroup.bounds.center[0] = pcx;
					parentGroup.bounds.center[1] = pcy;
					parentGroup.bounds.center[2] = pcz;
					parentGroup.bounds.radius     = pRadius;
					parentGroup.bounds.error      = parentError;
					parentGroup.depth             = static_cast<int32_t>(currentVoxelDepth);
					parentGroup.firstSegment        = firstSegmentIdx;
					parentGroup.segmentCount        = static_cast<uint32_t>(batch.size());
					parentGroup.terminalSegmentCount = 0;
					parentGroup.meshletCount      = 0;
					parentGroup.groupVertexCount  = 0;
					parentGroup.firstMeshlet      = 0;
					parentGroup.firstGroupVertex  = 0;
					parentGroup.flags             = CLOD_GROUP_FLAG_IS_VOXEL;

					const uint32_t newGroupIdx = appendVoxelGroup(parentGroup);

					state.voxelGroupMapping.groupToPayloadIndex.resize(state.groups.size(), -1);
					state.voxelGroupMapping.groupToPayloadIndex[newGroupIdx] = static_cast<int32_t>(payloadIdx);

					VoxelLevelGroup lvl{};
					lvl.groupIndex   = newGroupIdx;
					lvl.payloadIndex = payloadIdx;
					lvl.center       = { pcx, pcy, pcz };
					nextLevelGroups.push_back(lvl);
				}

				currentLevelGroups = std::move(nextLevelGroups);
				currentLevelError  = parentError;
				currentResolution  = parentResolution;
			}

			// Ensure groupToPayloadIndex covers all groups (mesh groups map to -1).
			state.voxelGroupMapping.groupToPayloadIndex.resize(state.groups.size(), -1);

			const size_t voxelGroupCount = state.groups.size() - originalGroupCount;
			const uint32_t voxelLevelCount = currentVoxelDepth - originalMaxDepth;

			spdlog::info(
				"ClusterLOD: voxel hierarchy built - {} voxel groups across {} levels "
				"({} leaf batches from {} terminal mesh groups, final resolution {})",
				voxelGroupCount,
				voxelLevelCount,
				leafBatches.size(),
				terminalCoarsestGroupIndices.size(),
				currentResolution);
		}
	}

	// Build traversal hierarchy (including any voxel groups added above).
	BuildClusterLODTraversalHierarchy(state, /*preferredNodeWidth=*/TraversalNodeFanout);

	// Release voxel-only raw streams after the voxel hierarchy is built.
	{ std::vector<std::vector<std::byte>>().swap(state.groupVertexChunks); }
	{ std::vector<std::vector<uint32_t>>().swap(state.groupMeshletVertexChunks); }
	{ std::vector<std::vector<meshopt_Meshlet>>().swap(state.groupMeshletChunks); }
	{ std::vector<std::vector<uint8_t>>().swap(state.groupMeshletTriangleChunks); }

	for (const ClusterLODNode& node : state.nodes)
	{
		if (node.range.isGroup != 0)
			continue;

		const uint32_t childCount = uint32_t(node.range.countMinusOne) + 1u;
		if (childCount > TraversalNodeFanout)
		{
			throw std::runtime_error("Cluster LOD: traversal node fanout exceeded configured maximum");
		}
	}

	// Assign mesh-local pageMapBase for each group (running cumulative offset).
	{
		uint32_t localPageMapCursor = 0;
		for (auto& grp : state.groups) {
			grp.pageMapBase = localPageMapCursor;
			localPageMapCursor += grp.pageCount;
		}
	}

	ClusterLODPrebuildArtifacts artifacts{};
	artifacts.prebuiltData.groups = std::move(state.groups);
	artifacts.prebuiltData.segments = std::move(state.segments);
	artifacts.prebuiltData.segmentBounds = std::move(state.segmentBounds);
	artifacts.prebuiltData.objectBoundingSphere = BuildObjectBoundingSphereFromRootNode(state.nodes, state.topRootNode);
	artifacts.prebuiltData.groupChunks = std::move(state.groupChunks);
	artifacts.prebuiltData.nodes = std::move(state.nodes);
	artifacts.prebuiltData.voxelGroupMapping = std::move(state.voxelGroupMapping);

	artifacts.cacheBuildData.groupPageBlobs = std::move(state.groupPageBlobs);

	return artifacts;
}
