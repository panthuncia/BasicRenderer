#include "Mesh/Mesh.h"

#include <limits>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <bit>
#include <cmath>
#include <array>
#include <cstring>
#include <thread>
#include <mutex>
#include <exception>
#include <cassert>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/PSOFlags.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/Material.h"
#include "Mesh/VertexFlags.h"
#include "Managers/MeshManager.h"
#include "Animation/Skeleton.h"

#include "../shaders/Common/defines.h"

std::atomic<uint32_t> Mesh::globalMeshCount = 0;

namespace
{
	constexpr uint32_t CLOD_COMPRESSED_POSITIONS = 1u << 0;
	constexpr uint32_t CLOD_COMPRESSED_MESHLET_VERTEX_INDICES = 1u << 1;
	constexpr uint32_t CLOD_COMPRESSED_NORMALS = 1u << 2;

	struct U32Stats
	{
		uint32_t count = 0;
		uint64_t sum = 0;
		uint32_t minv = std::numeric_limits<uint32_t>::max();
		uint32_t maxv = 0;

		void add(uint32_t v)
		{
			++count;
			sum += v;
			minv = std::min(minv, v);
			maxv = std::max(maxv, v);
		}

		double avg() const { return count ? double(sum) / double(count) : 0.0; }
		bool   any() const { return count != 0; }
	};

	uint32_t BitsNeededForRange(uint32_t range)
	{
		if (range == 0)
		{
			return 1;
		}
		return 32u - static_cast<uint32_t>(std::countl_zero(range));
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
		std::vector<ClusterLODChild> children;
		std::vector<std::byte> vertexChunk;
		std::vector<std::byte> skinningVertexChunk;
		std::vector<uint32_t> compressedPositionWords;
		std::vector<uint32_t> compressedNormalWords;
		std::vector<uint32_t> compressedMeshletVertexWords;
		std::vector<meshopt_Meshlet> groupMeshletChunk;
		std::vector<uint8_t> groupMeshletTriangleChunk;
		std::vector<BoundingSphere> groupMeshletBoundsChunk;
		ClusterLODGroupChunk groupChunk{};
	};

	template <typename Func>
	void RunParallelFor(size_t itemCount, Func&& func)
	{
		if (itemCount == 0)
		{
			return;
		}

		const unsigned int hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
		const size_t workerCount = std::min<size_t>(static_cast<size_t>(hardwareThreads), itemCount);

		if (workerCount <= 1)
		{
			for (size_t itemIndex = 0; itemIndex < itemCount; ++itemIndex)
			{
				func(itemIndex);
			}
			return;
		}

		std::atomic<size_t> nextIndex = 0;
		std::atomic<bool> stopRequested = false;
		std::exception_ptr workerException;
		std::mutex exceptionMutex;

		auto workerBody = [&]()
			{
				for (;;)
				{
					if (stopRequested.load(std::memory_order_acquire))
					{
						return;
					}

					const size_t itemIndex = nextIndex.fetch_add(1, std::memory_order_relaxed);
					if (itemIndex >= itemCount)
					{
						return;
					}

					try
					{
						func(itemIndex);
					}
					catch (...)
					{
						std::lock_guard<std::mutex> lock(exceptionMutex);
						if (!workerException)
						{
							workerException = std::current_exception();
							stopRequested.store(true, std::memory_order_release);
						}
						return;
					}
				}
			};

		std::vector<std::jthread> workers;
		workers.reserve(workerCount);
		for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		{
			workers.emplace_back(workerBody);
		}

		for (std::jthread& worker : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}

		if (workerException)
		{
			std::rethrow_exception(workerException);
		}
	}

	ClusterLODGroupBuildOutput BuildClusterLODGroupOutput(
		const CapturedClusterLODGroup& capturedGroup,
		const std::vector<std::byte>& vertices,
		size_t vertexStrideBytes,
		const std::vector<std::byte>* skinningVertices,
		size_t skinningVertexStrideBytes,
		float meshPositionQuantScale,
		uint32_t meshPositionQuantExp)
	{
		ClusterLODGroupBuildOutput output{};

		output.group.bounds = capturedGroup.simplified;
		output.group.depth = capturedGroup.depth;
		output.group.firstMeshlet = 0;
		output.group.meshletCount = static_cast<uint32_t>(capturedGroup.clusters.size());
		output.group.firstGroupVertex = 0;
		output.group.groupVertexCount = 0;
		output.group.firstChild = 0;
		output.group.childCount = 0;
		output.group.terminalChildCount = 0;

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

		std::sort(
			buckets.begin(),
			buckets.end(),
			[](const ChildBucket& lhs, const ChildBucket& rhs)
			{
				const bool lhsTerminal = (lhs.refinedGroup < 0);
				const bool rhsTerminal = (rhs.refinedGroup < 0);
				if (lhsTerminal != rhsTerminal)
				{
					return lhsTerminal > rhsTerminal;
				}

				if (lhs.clusterIndices.size() != rhs.clusterIndices.size())
				{
					return lhs.clusterIndices.size() > rhs.clusterIndices.size();
				}

				return lhs.refinedGroup < rhs.refinedGroup;
			});

		uint32_t groupMeshletVertexCursor = 0;
		uint32_t localMeshletCursor = 0;

		for (const ChildBucket& bucket : buckets)
		{
			const uint32_t bucketLocalStart = localMeshletCursor;

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

				++localMeshletCursor;
			}

			ClusterLODChild child{};
			child.refinedGroup = bucket.refinedGroup;
			child.firstLocalMeshletIndex = bucketLocalStart;
			child.localMeshletCount = static_cast<uint32_t>(bucket.clusterIndices.size());
			output.children.push_back(child);
		}

		assert(localMeshletCursor == output.group.meshletCount);

		output.group.childCount = static_cast<uint32_t>(output.children.size());
		output.group.terminalChildCount = 0;
		for (const ChildBucket& bucket : buckets)
		{
			if (bucket.refinedGroup < 0)
			{
				output.group.terminalChildCount++;
			}
			else
			{
				break;
			}
		}

		output.group.groupVertexCount = static_cast<uint32_t>(groupLocalToGlobal.size());

		output.vertexChunk.reserve(static_cast<size_t>(output.group.groupVertexCount) * vertexStrideBytes);
		if (skinningVertices != nullptr && skinningVertexStrideBytes > 0)
		{
			output.skinningVertexChunk.reserve(static_cast<size_t>(output.group.groupVertexCount) * skinningVertexStrideBytes);
		}

		for (uint32_t globalVertexIndex : groupLocalToGlobal)
		{
			const size_t sourceVertexByteOffset = static_cast<size_t>(globalVertexIndex) * vertexStrideBytes;
			output.vertexChunk.insert(
				output.vertexChunk.end(),
				vertices.begin() + static_cast<std::ptrdiff_t>(sourceVertexByteOffset),
				vertices.begin() + static_cast<std::ptrdiff_t>(sourceVertexByteOffset + vertexStrideBytes));

			if (skinningVertices != nullptr && skinningVertexStrideBytes > 0)
			{
				const size_t sourceSkinningByteOffset = static_cast<size_t>(globalVertexIndex) * skinningVertexStrideBytes;
				output.skinningVertexChunk.insert(
					output.skinningVertexChunk.end(),
					skinningVertices->begin() + static_cast<std::ptrdiff_t>(sourceSkinningByteOffset),
					skinningVertices->begin() + static_cast<std::ptrdiff_t>(sourceSkinningByteOffset + skinningVertexStrideBytes));
			}
		}

		std::array<int32_t, 3> minQuantized = {
			std::numeric_limits<int32_t>::max(),
			std::numeric_limits<int32_t>::max(),
			std::numeric_limits<int32_t>::max()
		};
		std::array<int32_t, 3> maxQuantized = {
			std::numeric_limits<int32_t>::min(),
			std::numeric_limits<int32_t>::min(),
			std::numeric_limits<int32_t>::min()
		};
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
			minQuantized[0] = std::min(minQuantized[0], qx);
			minQuantized[1] = std::min(minQuantized[1], qy);
			minQuantized[2] = std::min(minQuantized[2], qz);
			maxQuantized[0] = std::max(maxQuantized[0], qx);
			maxQuantized[1] = std::max(maxQuantized[1], qy);
			maxQuantized[2] = std::max(maxQuantized[2], qz);
		}

		const uint32_t positionBitsX = BitsNeededForRange(static_cast<uint32_t>(maxQuantized[0] - minQuantized[0]));
		const uint32_t positionBitsY = BitsNeededForRange(static_cast<uint32_t>(maxQuantized[1] - minQuantized[1]));
		const uint32_t positionBitsZ = BitsNeededForRange(static_cast<uint32_t>(maxQuantized[2] - minQuantized[2]));

		uint64_t positionBitCursor = 0;
		for (const auto& quantized : quantizedGroupPositions)
		{
			AppendBits(output.compressedPositionWords, positionBitCursor, static_cast<uint32_t>(quantized[0] - minQuantized[0]), positionBitsX);
			AppendBits(output.compressedPositionWords, positionBitCursor, static_cast<uint32_t>(quantized[1] - minQuantized[1]), positionBitsY);
			AppendBits(output.compressedPositionWords, positionBitCursor, static_cast<uint32_t>(quantized[2] - minQuantized[2]), positionBitsZ);
		}

		output.compressedNormalWords.reserve(groupLocalToGlobal.size());
		for (uint32_t globalVertexIndex : groupLocalToGlobal)
		{
			const size_t byteOffset = static_cast<size_t>(globalVertexIndex) * vertexStrideBytes + 12u;
			DirectX::XMFLOAT3 normal{};
			std::memcpy(&normal.x, vertices.data() + byteOffset, sizeof(float));
			std::memcpy(&normal.y, vertices.data() + byteOffset + sizeof(float), sizeof(float));
			std::memcpy(&normal.z, vertices.data() + byteOffset + sizeof(float) * 2, sizeof(float));

			auto oct = OctEncodeNormal(normal);
			output.compressedNormalWords.push_back(PackOctNormalSnorm16(oct));
		}

		const uint32_t meshletVertexBits = BitsNeededForRange(std::max<uint32_t>(1u, output.group.groupVertexCount) - 1u);
		uint64_t meshletVertexBitCursor = 0;
		for (uint32_t localVertexIndex : output.meshletVertices)
		{
			AppendBits(output.compressedMeshletVertexWords, meshletVertexBitCursor, localVertexIndex, meshletVertexBits);
		}

		output.groupMeshletChunk = output.meshlets;
		output.groupMeshletTriangleChunk = output.meshletTriangles;
		output.groupMeshletBoundsChunk = output.meshletBounds;

		output.groupChunk.vertexChunkByteOffset = 0;
		output.groupChunk.meshletVerticesBase = 0;
		output.groupChunk.groupVertexCount = output.group.groupVertexCount;
		output.groupChunk.meshletVertexCount = static_cast<uint32_t>(output.meshletVertices.size());
		output.groupChunk.meshletBase = 0;
		output.groupChunk.meshletCount = static_cast<uint32_t>(output.meshlets.size());
		output.groupChunk.meshletTrianglesByteOffset = 0;
		output.groupChunk.meshletTrianglesByteCount = static_cast<uint32_t>(output.meshletTriangles.size());
		output.groupChunk.meshletBoundsBase = 0;
		output.groupChunk.meshletBoundsCount = static_cast<uint32_t>(output.meshletBounds.size());
		output.groupChunk.compressedPositionWordsBase = 0;
		output.groupChunk.compressedPositionWordCount = static_cast<uint32_t>(output.compressedPositionWords.size());
		output.groupChunk.compressedPositionBitsX = positionBitsX;
		output.groupChunk.compressedPositionBitsY = positionBitsY;
		output.groupChunk.compressedPositionBitsZ = positionBitsZ;
		output.groupChunk.compressedPositionQuantExp = meshPositionQuantExp;
		output.groupChunk.compressedPositionMinQx = minQuantized[0];
		output.groupChunk.compressedPositionMinQy = minQuantized[1];
		output.groupChunk.compressedPositionMinQz = minQuantized[2];
		output.groupChunk.compressedNormalWordsBase = 0;
		output.groupChunk.compressedNormalWordCount = static_cast<uint32_t>(output.compressedNormalWords.size());
		output.groupChunk.compressedMeshletVertexWordsBase = 0;
		output.groupChunk.compressedMeshletVertexWordCount = static_cast<uint32_t>(output.compressedMeshletVertexWords.size());
		output.groupChunk.compressedMeshletVertexBits = meshletVertexBits;
		output.groupChunk.compressedFlags = CLOD_COMPRESSED_POSITIONS | CLOD_COMPRESSED_MESHLET_VERTEX_INDICES | CLOD_COMPRESSED_NORMALS;

		return output;
	}
}


Mesh::Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags, const ClusterLODPrebuiltData* prebuiltClusterLOD) {
    m_vertices = std::move(vertices);
	if (skinningVertices.has_value()) {
		m_skinningVertices = std::move(skinningVertices.value());
	}
	if (prebuiltClusterLOD != nullptr) {
		m_prebuiltClusterLOD = *prebuiltClusterLOD;
	}
	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = static_cast<uint32_t>(m_vertices->size() / vertexSize);
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;

	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = static_cast<uint32_t>(m_vertices->size() / vertexSize);
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;
	m_perMeshBufferData.clodMeshletBufferOffset = 0;
	m_perMeshBufferData.clodMeshletVerticesBufferOffset = 0;
	m_perMeshBufferData.clodMeshletTrianglesBufferOffset = 0;
	m_perMeshBufferData.clodNumMeshlets = 0;

	m_skinningVertexSize = skinningVertexSize;
	CreateBuffers(indices);
    this->material = material;

	m_globalMeshID = GetNextGlobalIndex();
}

void Mesh::ApplyPrebuiltClusterLODData(const ClusterLODPrebuiltData& data)
{
	m_clodGroups = data.groups;
	m_clodChildren = data.children;
	m_clodDuplicatedVertices = data.duplicatedVertices;
	m_clodDuplicatedSkinningVertices = data.duplicatedSkinningVertices;
	m_clodGroupChunks = data.groupChunks;
	m_clodGroupVertexChunks = data.groupVertexChunks;
	m_clodGroupSkinningVertexChunks = data.groupSkinningVertexChunks;
	m_clodGroupMeshletVertexChunks = data.groupMeshletVertexChunks;
	m_clodGroupCompressedPositionWordChunks = data.groupCompressedPositionWordChunks;
	m_clodGroupCompressedNormalWordChunks = data.groupCompressedNormalWordChunks;
	m_clodGroupCompressedMeshletVertexWordChunks = data.groupCompressedMeshletVertexWordChunks;
	m_clodGroupMeshletChunks = data.groupMeshletChunks;
	m_clodGroupMeshletTriangleChunks = data.groupMeshletTriangleChunks;
	m_clodGroupMeshletBoundsChunks = data.groupMeshletBoundsChunks;
	m_clodGroupDiskSpans = data.groupDiskSpans;
	m_clodCacheSource = data.cacheSource;
	m_clodNodes = data.nodes;
	m_clodTopRootNode = 0;

	m_clodMaxDepth = 0;
	for (const auto& group : m_clodGroups) {
		m_clodMaxDepth = std::max<uint32_t>(m_clodMaxDepth, static_cast<uint32_t>(std::max(group.depth, 0)));
	}
	m_clodLodLevelRoots.resize(m_clodMaxDepth + 1);
	for (uint32_t depth = 0; depth <= m_clodMaxDepth; ++depth) {
		m_clodLodLevelRoots[depth] = 1u + depth;
	}
	m_clodLodNodeRanges.clear();

	if (!m_clodDuplicatedVertices.empty())
	{
		m_perMeshBufferData.numVertices = static_cast<uint32_t>(m_clodDuplicatedVertices.size() / m_perMeshBufferData.vertexByteSize);
	}
}

ClusterLODPrebuiltData Mesh::GetClusterLODPrebuiltData() const
{
	ClusterLODPrebuiltData out{};
	out.groups = m_clodGroups;
	out.children = m_clodChildren;
	out.duplicatedVertices = m_clodDuplicatedVertices;
	out.duplicatedSkinningVertices = m_clodDuplicatedSkinningVertices;
	out.groupChunks = m_clodGroupChunks;
	out.groupVertexChunks = m_clodGroupVertexChunks;
	out.groupSkinningVertexChunks = m_clodGroupSkinningVertexChunks;
	out.groupMeshletVertexChunks = m_clodGroupMeshletVertexChunks;
	out.groupCompressedPositionWordChunks = m_clodGroupCompressedPositionWordChunks;
	out.groupCompressedNormalWordChunks = m_clodGroupCompressedNormalWordChunks;
	out.groupCompressedMeshletVertexWordChunks = m_clodGroupCompressedMeshletVertexWordChunks;
	out.groupMeshletChunks = m_clodGroupMeshletChunks;
	out.groupMeshletTriangleChunks = m_clodGroupMeshletTriangleChunks;
	out.groupMeshletBoundsChunks = m_clodGroupMeshletBoundsChunks;
	out.groupDiskSpans = m_clodGroupDiskSpans;
	out.cacheSource = m_clodCacheSource;
	out.nodes = m_clodNodes;
	return out;
}

void Mesh::CreateVertexBuffer() {
    
    const UINT vertexBufferSize = static_cast<UINT>(m_vertices->size());

	m_vertexBufferHandle = Buffer::CreateShared(rhi::HeapType::DeviceLocal, vertexBufferSize);
	BUFFER_UPLOAD(m_vertices->data(), vertexBufferSize, rg::runtime::UploadTarget::FromShared(m_vertexBufferHandle), 0);

    m_vertexBufferView.buffer = m_vertexBufferHandle->GetAPIResource().GetHandle();
    m_vertexBufferView.stride = sizeof(m_perMeshBufferData.vertexByteSize);
    m_vertexBufferView.sizeBytes = vertexBufferSize;
}

void Mesh::LogClusterLODHierarchyStats() const
{
	if (m_clodGroups.empty())
	{
		spdlog::info("ClusterLOD stats: no groups (hierarchy not built).");
		return;
	}

	const uint32_t maxDepth = m_clodMaxDepth;
	const uint32_t lodLevelCount = maxDepth + 1;

	// -------------------------
	// Group-level stats
	// -------------------------
	U32Stats groups_meshletsPerGroup;
	U32Stats groups_childBucketsPerGroup;

	std::vector<uint32_t> groupsPerDepth(lodLevelCount, 0);
	std::vector<uint64_t> meshletsPerDepth(lodLevelCount, 0);

	uint64_t totalMeshlets = 0;
	for (const ClusterLODGroup& g : m_clodGroups)
	{
		const uint32_t d = uint32_t(g.depth);
		if (d < lodLevelCount)
		{
			groupsPerDepth[d] += 1;
			meshletsPerDepth[d] += g.meshletCount;
		}

		groups_meshletsPerGroup.add(g.meshletCount);
		groups_childBucketsPerGroup.add(g.childCount);
		totalMeshlets += g.meshletCount;
	}

	// -------------------------
	// Child-bucket stats
	// -------------------------
	U32Stats child_localMeshletsPerBucket;
	uint32_t terminalBuckets = 0; // refinedGroup == -1
	uint32_t refinedBuckets = 0; // refinedGroup != -1

	for (const ClusterLODChild& c : m_clodChildren)
	{
		child_localMeshletsPerBucket.add(c.localMeshletCount);
		if (c.refinedGroup < 0) terminalBuckets++;
		else refinedBuckets++;
	}

	// -------------------------
	// Traversal-node stats
	// -------------------------
	const uint32_t nodeCount = uint32_t(m_clodNodes.size());

	uint32_t leafGroupNodes = 0;
	uint32_t interiorNodes = 0;
	U32Stats interior_childCount;
	uint32_t maxInteriorChildCount = 0;

	for (uint32_t i = 0; i < nodeCount; ++i)
	{
		const ClusterLODNode& n = m_clodNodes[i];

		if (n.range.isGroup)
		{
			leafGroupNodes++;
		}
		else
		{
			interiorNodes++;
			const uint32_t childCount = uint32_t(n.range.countMinusOne) + 1u;
			interior_childCount.add(childCount);
			maxInteriorChildCount = std::max(maxInteriorChildCount, childCount);
		}
	}

	// -------------------------
	// Log header / overview
	// -------------------------
	spdlog::info("ClusterLOD stats:");
	spdlog::info("  groups={} | meshlets={} | depths={} (0..{})",
		uint32_t(m_clodGroups.size()),
		totalMeshlets,
		lodLevelCount,
		maxDepth);

	const uint32_t duplicatedVertexCount = m_clodDuplicatedVertices.empty()
		? 0u
		: uint32_t(m_clodDuplicatedVertices.size() / m_perMeshBufferData.vertexByteSize);
	spdlog::info("  children: entries={} | duplicatedGroupVertices={}",
		uint32_t(m_clodChildren.size()),
		duplicatedVertexCount);

	spdlog::info("  traversal nodes: total={} | interior={} | leafGroupNodes={}",
		nodeCount, interiorNodes, leafGroupNodes);

	// -------------------------
	// Log distributions
	// -------------------------
	if (groups_meshletsPerGroup.any())
	{
		spdlog::info("  meshlets per group: min={} | avg={:.2f} | max={}",
			groups_meshletsPerGroup.minv, groups_meshletsPerGroup.avg(), groups_meshletsPerGroup.maxv);
	}

	if (groups_childBucketsPerGroup.any())
	{
		spdlog::info("  child buckets per group: min={} | avg={:.2f} | max={}",
			groups_childBucketsPerGroup.minv, groups_childBucketsPerGroup.avg(), groups_childBucketsPerGroup.maxv);
	}

	if (child_localMeshletsPerBucket.any())
	{
		spdlog::info("  local meshlets per child bucket: min={} | avg={:.2f} | max={}",
			child_localMeshletsPerBucket.minv, child_localMeshletsPerBucket.avg(), child_localMeshletsPerBucket.maxv);
	}

	spdlog::info("  child bucket types: terminal(refined=-1)={} | refined(refined>=0)={}",
		terminalBuckets, refinedBuckets);

	if (interior_childCount.any())
	{
		spdlog::info("  interior node child count: min={} | avg={:.2f} | max={}",
			interior_childCount.minv, interior_childCount.avg(), interior_childCount.maxv);
	}

	spdlog::info("  BVH max interior fanout observed={}", maxInteriorChildCount);

	// -------------------------
	// Per-depth summary
	// -------------------------
	spdlog::info("  per-depth summary:");
	for (uint32_t d = 0; d < lodLevelCount; ++d)
	{
		const uint32_t depthRootIndex = 1 + d;

		// Nodes for this depth tree = 1 root + range.count (range excludes that root by your allocator)
		uint32_t depthNodes = 1;
		if (d < m_clodLodNodeRanges.size())
			depthNodes += m_clodLodNodeRanges[d].count;

		const bool depthRootIsLeaf = (depthRootIndex < nodeCount) ? (m_clodNodes[depthRootIndex].range.isGroup != 0) : false;

		spdlog::info("    depth {}: groups={} | meshlets={} | traversalNodes={} | depthRootIsLeaf={}",
			d,
			groupsPerDepth[d],
			(uint64_t)meshletsPerDepth[d],
			depthNodes,
			depthRootIsLeaf);
	}

	// Top root info (node 0)
	if (nodeCount > 0)
	{
		const ClusterLODNode& top = m_clodNodes[0];
		const uint32_t topChildren = top.range.isGroup ? 0u : (uint32_t(top.range.countMinusOne) + 1u);
		spdlog::info("  top root: node={} | childCount={} | childOffset={}",
			m_clodTopRootNode,
			topChildren,
			uint32_t(top.range.indexOrOffset));
	}
}

void Mesh::BuildClusterLODTraversalHierarchy(uint32_t preferredNodeWidth)
{
	if (m_clodGroups.empty())
		return;

	preferredNodeWidth = std::max(2u, preferredNodeWidth);

	// Gather groups by depth + compute max depth
	m_clodMaxDepth = 0;
	for (const ClusterLODGroup& g : m_clodGroups)
		m_clodMaxDepth = std::max(m_clodMaxDepth, uint32_t(g.depth));

	const uint32_t lodLevelCount = m_clodMaxDepth + 1;

	std::vector<std::vector<uint32_t>> groupsByDepth(lodLevelCount);
	groupsByDepth.shrink_to_fit();
	for (uint32_t groupID = 0; groupID < uint32_t(m_clodGroups.size()); ++groupID)
	{
		const uint32_t d = uint32_t(m_clodGroups[groupID].depth);
		groupsByDepth[d].push_back(groupID);
	}

	// Sanity: every depth should exist
	for (uint32_t d = 0; d < lodLevelCount; ++d)
	{
		if (groupsByDepth[d].empty())
		{
			throw std::runtime_error("Cluster LOD: missing groups for an intermediate depth; compact depths or handle gaps.");
		}
	}

	// Allocate node layout like NVIDIA:
	// [0] top root
	// [1..lodLevelCount] per-depth roots
	// [rest] per-depth interior nodes + leaves (except per-depth root)
	m_clodLodNodeRanges.assign(lodLevelCount, {});
	m_clodLodLevelRoots.resize(lodLevelCount);
	for (uint32_t d = 0; d < lodLevelCount; ++d) {
		m_clodLodLevelRoots[d] = 1 + d;
	}

	uint32_t nodeOffset = 1 + lodLevelCount;

	for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
	{
		const uint32_t leafCount = uint32_t(groupsByDepth[depth].size());

		// leaves + all interior layers including root
		uint32_t nodeCount = leafCount;
		uint32_t iterCount = leafCount;

		while (iterCount > 1)
		{
			iterCount = (iterCount + preferredNodeWidth - 1) / preferredNodeWidth;
			nodeCount += iterCount;
		}

		// subtract root (stored at index 1+depth)
		nodeCount--;

		m_clodLodNodeRanges[depth].offset = nodeOffset;
		m_clodLodNodeRanges[depth].count = nodeCount;
		nodeOffset += nodeCount;
	}

	m_clodNodes.clear();
	m_clodNodes.resize(nodeOffset);

	// Build per-depth trees
	for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
	{
		const auto& groupIDs = groupsByDepth[depth];
		const uint32_t leafCount = uint32_t(groupIDs.size());
		const ClusterLODNodeRangeAlloc& range = m_clodLodNodeRanges[depth];

		uint32_t writeOffset = range.offset;
		uint32_t lastLayerOffset = writeOffset;

		// Leaves (groups)
		for (uint32_t i = 0; i < leafCount; ++i)
		{
			const uint32_t groupID = groupIDs[i];
			const ClusterLODGroup& grp = m_clodGroups[groupID];

			ClusterLODNode& node = (leafCount == 1) ? m_clodNodes[1 + depth] : m_clodNodes[writeOffset++];

			node = {};
			node.range.isGroup = 1;
			node.range.indexOrOffset = groupID;
			node.range.countMinusOne = (grp.meshletCount > 0) ? (grp.meshletCount - 1) : 0;

			node.traversalMetric.boundingSphereX = grp.bounds.center[0];
			node.traversalMetric.boundingSphereY = grp.bounds.center[1];
			node.traversalMetric.boundingSphereZ = grp.bounds.center[2];
			node.traversalMetric.boundingSphereRadius = grp.bounds.radius;
			node.traversalMetric.maxQuadricError = grp.bounds.error;
		}

		// Special case: single leaf stored into root section.
		if (leafCount == 1) {
			writeOffset++;
		}

		// Interior layers
		uint32_t iterCount = leafCount;

		std::vector<uint32_t> partitioned;
		std::vector<ClusterLODNode> scratch;

		while (iterCount > 1)
		{
			const uint32_t lastCount = iterCount;
			ClusterLODNode* lastNodes = &m_clodNodes[lastLayerOffset];

			// Partition children into spatially coherent buckets of preferredNodeWidth
			partitioned.resize(lastCount);
			meshopt_spatialClusterPoints(
				partitioned.data(),
				&lastNodes->traversalMetric.boundingSphereX,
				lastCount,
				sizeof(ClusterLODNode),
				preferredNodeWidth);

			// Reorder last layer by partition result
			scratch.assign(lastNodes, lastNodes + lastCount);
			for (uint32_t n = 0; n < lastCount; ++n)
				lastNodes[n] = scratch[partitioned[n]];

			// Build next layer
			iterCount = (lastCount + preferredNodeWidth - 1) / preferredNodeWidth;

			ClusterLODNode* newNodes = (iterCount == 1) ? &m_clodNodes[1 + depth] : &m_clodNodes[writeOffset];

			for (uint32_t n = 0; n < iterCount; ++n)
			{
				ClusterLODNode& node = newNodes[n];

				const uint32_t childBegin = n * preferredNodeWidth;
				const uint32_t childEnd = std::min(childBegin + preferredNodeWidth, lastCount);
				const uint32_t childCount = childEnd - childBegin;

				ClusterLODNode* children = &lastNodes[childBegin];

				node = {};
				node.range.isGroup = 0;
				node.range.indexOrOffset = lastLayerOffset + childBegin; // childOffset
				node.range.countMinusOne = childCount - 1;

				// max error
				float maxErr = 0.f;
				for (uint32_t c = 0; c < childCount; ++c)
					maxErr = std::max(maxErr, children[c].traversalMetric.maxQuadricError);
				node.traversalMetric.maxQuadricError = maxErr;

				// merged sphere
				meshopt_Bounds merged = meshopt_computeSphereBounds(
					&children[0].traversalMetric.boundingSphereX,
					childCount,
					sizeof(ClusterLODNode),
					&children[0].traversalMetric.boundingSphereRadius,
					sizeof(ClusterLODNode));

				node.traversalMetric.boundingSphereX = merged.center[0];
				node.traversalMetric.boundingSphereY = merged.center[1];
				node.traversalMetric.boundingSphereZ = merged.center[2];
				node.traversalMetric.boundingSphereRadius = merged.radius;
			}

			lastLayerOffset = writeOffset;
			writeOffset += iterCount;
		}

		// Match NVIDIA's bookkeeping: writeOffset ends one past, then they -- and assert
		writeOffset--;
		if (range.offset + range.count != writeOffset) {
			throw std::runtime_error("Cluster LOD: traversal node allocation mismatch (range/count).");
		}
	}

	// Top hierarchy over per-depth roots [1..lodLevelCount], constrained to preferredNodeWidth fanout.
	{
		auto BuildInternalNode = [&](uint32_t childOffset, uint32_t childCount) -> ClusterLODNode {
			if (childCount == 0)
				throw std::runtime_error("Cluster LOD: internal node with zero children");

			ClusterLODNode node{};
			node.range.isGroup = 0;
			node.range.indexOrOffset = childOffset;
			node.range.countMinusOne = childCount - 1;

			const ClusterLODNode* children = &m_clodNodes[childOffset];

			float maxErr = 0.f;
			for (uint32_t c = 0; c < childCount; ++c)
				maxErr = std::max(maxErr, children[c].traversalMetric.maxQuadricError);
			node.traversalMetric.maxQuadricError = maxErr;

			meshopt_Bounds merged = meshopt_computeSphereBounds(
				&children[0].traversalMetric.boundingSphereX,
				childCount,
				sizeof(ClusterLODNode),
				&children[0].traversalMetric.boundingSphereRadius,
				sizeof(ClusterLODNode));

			node.traversalMetric.boundingSphereX = merged.center[0];
			node.traversalMetric.boundingSphereY = merged.center[1];
			node.traversalMetric.boundingSphereZ = merged.center[2];
			node.traversalMetric.boundingSphereRadius = merged.radius;

			return node;
		};

		std::vector<uint32_t> currentLayer;
		currentLayer.reserve(lodLevelCount);
		for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
			currentLayer.push_back(m_clodLodLevelRoots[depth]);

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
				const uint32_t parentId = uint32_t(m_clodNodes.size());
				m_clodNodes.push_back(parent);
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

		ClusterLODNode& root = m_clodNodes[0];
		root = BuildInternalNode(rootChildOffset, rootChildCount);

		m_clodTopRootNode = 0;
	}
}


void Mesh::BuildClusterLOD(const std::vector<UINT32>& indices)
{
	// Clear any previous data
	m_clodGroups.clear();
	m_clodChildren.clear();
	m_clodDuplicatedVertices.clear();
	m_clodDuplicatedSkinningVertices.clear();
	m_clodGroupChunks.clear();
	m_clodGroupVertexChunks.clear();
	m_clodGroupSkinningVertexChunks.clear();
	m_clodGroupMeshletVertexChunks.clear();
	m_clodGroupCompressedPositionWordChunks.clear();
	m_clodGroupCompressedNormalWordChunks.clear();
	m_clodGroupCompressedMeshletVertexWordChunks.clear();
	m_clodGroupMeshletChunks.clear();
	m_clodGroupMeshletTriangleChunks.clear();
	m_clodGroupMeshletBoundsChunks.clear();

	// traversal hierarchy storage
	m_clodNodes.clear();
	m_clodLodNodeRanges.clear();
	m_clodLodLevelRoots.clear();
	m_clodTopRootNode = 0;
	m_clodMaxDepth = 0;

	static_assert(sizeof(UINT32) == sizeof(unsigned int), "UINT32 must be 32-bit");
	const unsigned int* idx = reinterpret_cast<const unsigned int*>(indices.data());

	const size_t vertexStrideBytes = m_perMeshBufferData.vertexByteSize;
	const size_t globalVertexCount = m_vertices->size() / vertexStrideBytes;
	const uint32_t meshPositionQuantExp = ComputeMeshQuantizationExponent(*m_vertices, vertexStrideBytes);
	const float meshPositionQuantScale = static_cast<float>(1u << meshPositionQuantExp);

	clodMesh mesh{};
	mesh.indices = idx;
	mesh.index_count = indices.size();
	mesh.vertex_count = globalVertexCount;
	mesh.vertex_positions = reinterpret_cast<const float*>(m_vertices->data());
	mesh.vertex_positions_stride = vertexStrideBytes;

	// positions-only simplification for now
	mesh.vertex_attributes = nullptr;
	mesh.vertex_attributes_stride = 0;
	mesh.vertex_lock = nullptr;
	mesh.attribute_weights = nullptr;
	mesh.attribute_count = 0;
	mesh.attribute_protect_mask = 0;

	clodConfig config = clodDefaultConfig(/*max_triangles=*/MS_MESHLET_SIZE);
	config.max_vertices = MS_MESHLET_SIZE;
	config.max_triangles = MS_MESHLET_SIZE;
	config.min_triangles = MS_MESHLET_MIN_SIZE;
	config.cluster_spatial = true;
	config.cluster_fill_weight = 0.5f;
	config.partition_spatial = true;
	config.partition_sort = true;
	config.optimize_clusters = true;
	config.optimize_bounds = true;

	constexpr uint32_t MaxGroupChildren = 4;
	constexpr uint32_t TraversalNodeFanout = 4;
	constexpr uint32_t TargetBucketClusters = 32;
	constexpr uint32_t MinTargetBucketClusters = 4;
	config.partition_max_refined_groups = MaxGroupChildren;

	uint32_t maxChildrenObserved = 0;
	uint32_t selectedTargetBucketClusters = TargetBucketClusters;

	for (;;)
	{
		m_clodGroups.clear();
		m_clodChildren.clear();
		m_clodDuplicatedVertices.clear();
		m_clodDuplicatedSkinningVertices.clear();
		m_clodGroupChunks.clear();
		m_clodGroupVertexChunks.clear();
		m_clodGroupSkinningVertexChunks.clear();
		m_clodGroupMeshletVertexChunks.clear();
		m_clodGroupCompressedPositionWordChunks.clear();
		m_clodGroupCompressedNormalWordChunks.clear();
		m_clodGroupCompressedMeshletVertexWordChunks.clear();
		m_clodGroupMeshletChunks.clear();
		m_clodGroupMeshletTriangleChunks.clear();
		m_clodGroupMeshletBoundsChunks.clear();
		m_clodNodes.clear();
		m_clodLodNodeRanges.clear();
		m_clodLodLevelRoots.clear();
		m_clodTopRootNode = 0;
		m_clodMaxDepth = 0;

		config.partition_size = std::max<size_t>(1, (selectedTargetBucketClusters * 3) / 4);
		size_t refinedCapSplitPartitionCount = 0;
		config.partition_refined_split_count = &refinedCapSplitPartitionCount;

		struct CaptureOutputContext
		{
			const std::vector<std::byte>* vertices = nullptr;
			size_t vertexStrideBytes = 0;
			const std::vector<std::byte>* skinningVertices = nullptr;
			size_t skinningVertexStrideBytes = 0;
			std::vector<ClusterLODGroup>* groups = nullptr;
			std::vector<ClusterLODChild>* children = nullptr;
			std::vector<std::byte>* duplicatedVertices = nullptr;
			std::vector<std::byte>* duplicatedSkinningVertices = nullptr;
			std::vector<ClusterLODGroupChunk>* groupChunks = nullptr;
			std::vector<std::vector<std::byte>>* groupVertexChunks = nullptr;
			std::vector<std::vector<std::byte>>* groupSkinningVertexChunks = nullptr;
			std::vector<std::vector<uint32_t>>* groupMeshletVertexChunks = nullptr;
			std::vector<std::vector<uint32_t>>* groupCompressedPositionWordChunks = nullptr;
			std::vector<std::vector<uint32_t>>* groupCompressedNormalWordChunks = nullptr;
			std::vector<std::vector<uint32_t>>* groupCompressedMeshletVertexWordChunks = nullptr;
			std::vector<std::vector<meshopt_Meshlet>>* groupMeshletChunks = nullptr;
			std::vector<std::vector<uint8_t>>* groupMeshletTriangleChunks = nullptr;
			std::vector<std::vector<BoundingSphere>>* groupMeshletBoundsChunks = nullptr;
			float meshPositionQuantScale = 1.0f;
			uint32_t meshPositionQuantExp = 0;
			std::atomic<uint32_t> nextGroupId = 0;
			std::mutex finalizeMutex;
			uint32_t cumulativeMeshletCount = 0;
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
					context->vertexStrideBytes,
					context->skinningVertices,
					context->skinningVertexStrideBytes,
					context->meshPositionQuantScale,
					context->meshPositionQuantExp);

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
				ensureIndexedStorage(*context->groupVertexChunks);
				ensureIndexedStorage(*context->groupSkinningVertexChunks);
				ensureIndexedStorage(*context->groupMeshletVertexChunks);
				ensureIndexedStorage(*context->groupCompressedPositionWordChunks);
				ensureIndexedStorage(*context->groupCompressedNormalWordChunks);
				ensureIndexedStorage(*context->groupCompressedMeshletVertexWordChunks);
				ensureIndexedStorage(*context->groupMeshletChunks);
				ensureIndexedStorage(*context->groupMeshletTriangleChunks);
				ensureIndexedStorage(*context->groupMeshletBoundsChunks);

				finalizedGroup.firstMeshlet = context->cumulativeMeshletCount;
				finalizedGroup.firstGroupVertex = static_cast<uint32_t>(context->duplicatedVertices->size() / context->vertexStrideBytes);
				finalizedGroup.firstChild = static_cast<uint32_t>(context->children->size());

				context->cumulativeMeshletCount += finalizedGroup.meshletCount;
				context->children->insert(context->children->end(), output.children.begin(), output.children.end());
				context->duplicatedVertices->insert(context->duplicatedVertices->end(), output.vertexChunk.begin(), output.vertexChunk.end());
				context->duplicatedSkinningVertices->insert(context->duplicatedSkinningVertices->end(), output.skinningVertexChunk.begin(), output.skinningVertexChunk.end());

				(*context->groupVertexChunks)[groupId] = std::move(output.vertexChunk);
				(*context->groupSkinningVertexChunks)[groupId] = std::move(output.skinningVertexChunk);
				(*context->groupMeshletVertexChunks)[groupId] = std::move(output.meshletVertices);
				(*context->groupCompressedPositionWordChunks)[groupId] = std::move(output.compressedPositionWords);
				(*context->groupCompressedNormalWordChunks)[groupId] = std::move(output.compressedNormalWords);
				(*context->groupCompressedMeshletVertexWordChunks)[groupId] = std::move(output.compressedMeshletVertexWords);
				(*context->groupMeshletChunks)[groupId] = std::move(output.groupMeshletChunk);
				(*context->groupMeshletTriangleChunks)[groupId] = std::move(output.groupMeshletTriangleChunk);
				(*context->groupMeshletBoundsChunks)[groupId] = std::move(output.groupMeshletBoundsChunk);

				(*context->groupChunks)[groupId] = output.groupChunk;
				(*context->groups)[groupId] = finalizedGroup;

				context->maxChildrenObserved = std::max(context->maxChildrenObserved, finalizedGroup.childCount);
				context->maxDepthObserved = (std::max)(context->maxDepthObserved, static_cast<uint32_t>(std::max(finalizedGroup.depth, 0)));

				return static_cast<int>(groupId);
			}

			static void Iterate(void* iterationContext, void*, int, size_t taskCount)
			{
				RunParallelFor(taskCount, [&](size_t taskIndex)
					{
						clodBuild_iterationTask(iterationContext, taskIndex, 0);
					});
			}
		};

		CaptureOutputContext captureContext{};
		captureContext.vertices = m_vertices.get();
		captureContext.vertexStrideBytes = vertexStrideBytes;
		captureContext.skinningVertices = m_skinningVertices.get();
		captureContext.skinningVertexStrideBytes = m_skinningVertexSize;
		captureContext.groups = &m_clodGroups;
		captureContext.children = &m_clodChildren;
		captureContext.duplicatedVertices = &m_clodDuplicatedVertices;
		captureContext.duplicatedSkinningVertices = &m_clodDuplicatedSkinningVertices;
		captureContext.groupChunks = &m_clodGroupChunks;
		captureContext.groupVertexChunks = &m_clodGroupVertexChunks;
		captureContext.groupSkinningVertexChunks = &m_clodGroupSkinningVertexChunks;
		captureContext.groupMeshletVertexChunks = &m_clodGroupMeshletVertexChunks;
		captureContext.groupCompressedPositionWordChunks = &m_clodGroupCompressedPositionWordChunks;
		captureContext.groupCompressedNormalWordChunks = &m_clodGroupCompressedNormalWordChunks;
		captureContext.groupCompressedMeshletVertexWordChunks = &m_clodGroupCompressedMeshletVertexWordChunks;
		captureContext.groupMeshletChunks = &m_clodGroupMeshletChunks;
		captureContext.groupMeshletTriangleChunks = &m_clodGroupMeshletTriangleChunks;
		captureContext.groupMeshletBoundsChunks = &m_clodGroupMeshletBoundsChunks;
		captureContext.meshPositionQuantScale = meshPositionQuantScale;
		captureContext.meshPositionQuantExp = meshPositionQuantExp;

		clodBuildParallelConfig parallelConfig{};
		parallelConfig.iteration_callback = &ClodBuildCallbacks::Iterate;
		const clodBuildParallelConfig* parallelConfigPtr = (std::thread::hardware_concurrency() > 1u) ? &parallelConfig : nullptr;

		clodBuildEx(config, mesh, &captureContext, &ClodBuildCallbacks::Output, parallelConfigPtr);

		maxChildrenObserved = captureContext.maxChildrenObserved;
		m_clodMaxDepth = captureContext.maxDepthObserved;

		for (size_t groupIndex = 0; groupIndex < m_clodGroups.size(); ++groupIndex)
		{
			if (m_clodGroups[groupIndex].groupVertexCount != m_clodGroupChunks[groupIndex].groupVertexCount)
			{
				m_clodGroupChunks[groupIndex].groupVertexCount = m_clodGroups[groupIndex].groupVertexCount;
			}
		}

		if (refinedCapSplitPartitionCount > 0)
		{
			spdlog::info(
				"ClusterLOD: refined-group cap split {} partitions at bucket target {}",
				refinedCapSplitPartitionCount,
				selectedTargetBucketClusters);
		}

		if (maxChildrenObserved <= MaxGroupChildren)
		{
			break;
		}

		if (selectedTargetBucketClusters <= MinTargetBucketClusters)
		{
			throw std::runtime_error("Cluster LOD: unable to satisfy maximum children per group while preserving refined-group bucket semantics");
		}

		const uint32_t reducedTarget = std::max<uint32_t>(MinTargetBucketClusters, selectedTargetBucketClusters / 2);
		spdlog::warn(
			"ClusterLOD: child fanout exceeded {} (observed={}) at bucket target {}, retrying with {}",
			MaxGroupChildren,
			maxChildrenObserved,
			selectedTargetBucketClusters,
			reducedTarget);
		selectedTargetBucketClusters = reducedTarget;
	}

	if (maxChildrenObserved > MaxGroupChildren)
	{
		throw std::runtime_error("Exceeded maximum allowed Cluster LOD children per group");
	}

	if (!m_clodDuplicatedVertices.empty())
	{
		m_perMeshBufferData.numVertices = static_cast<uint32_t>(m_clodDuplicatedVertices.size() / m_perMeshBufferData.vertexByteSize);
	}

	{
		std::vector<uint32_t> refinedGroupParentCounts(m_clodGroups.size(), 0);
		for (const ClusterLODChild& child : m_clodChildren)
		{
			if (child.refinedGroup >= 0)
			{
				const uint32_t refinedGroup = static_cast<uint32_t>(child.refinedGroup);
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

		if (groupsWithMultipleParents > 0)
		{
			spdlog::warn(
				"ClusterLOD: {} refined groups have multiple parents (max={} parents). "
				"Work-graph traversal can emit duplicate meshlets unless refined-group traversal is deduplicated.",
				groupsWithMultipleParents,
				maxParentCount);
		}
	}

	BuildClusterLODTraversalHierarchy(/*preferredNodeWidth=*/TraversalNodeFanout);

	for (const ClusterLODNode& node : m_clodNodes)
	{
		if (node.range.isGroup != 0)
			continue;

		const uint32_t childCount = uint32_t(node.range.countMinusOne) + 1u;
		if (childCount > TraversalNodeFanout)
		{
			throw std::runtime_error("Cluster LOD: traversal node fanout exceeded configured maximum");
		}
	}
	LogClusterLODHierarchyStats();
}


void Mesh::CreateMeshlets(const std::vector<UINT32>& indices) {
	unsigned int maxVertices = MS_MESHLET_SIZE; // TODO: Separate config for max vertices and max primitives per meshlet
	unsigned int maxPrimitives = MS_MESHLET_SIZE;
	unsigned int minVertices = MS_MESHLET_MIN_SIZE;
	unsigned int minPrimitives = MS_MESHLET_MIN_SIZE;

	size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, minPrimitives);

	m_meshlets.resize(maxMeshlets);
	m_meshletVertices.resize(maxMeshlets * maxVertices);
	m_meshletTriangles.resize(maxMeshlets * maxPrimitives * 3);
    
	auto meshletCount = meshopt_buildMeshletsSpatial(
		m_meshlets.data(), 
		m_meshletVertices.data(), 
		m_meshletTriangles.data(), 
		indices.data(), 
		indices.size(), 
		(float*)m_vertices->data(), 
		m_vertices->size() / m_perMeshBufferData.vertexByteSize, 
		m_perMeshBufferData.vertexByteSize, 
		maxVertices, 
		minPrimitives,
		maxPrimitives, 
		0.5);

	m_meshlets.resize(meshletCount);
	size_t usedVertexCount = 0;
	size_t usedTriangleByteCount = 0;
	for (const auto& meshlet : m_meshlets) {
		usedVertexCount = std::max(usedVertexCount, static_cast<size_t>(meshlet.vertex_offset + meshlet.vertex_count));
		usedTriangleByteCount = std::max(usedTriangleByteCount, static_cast<size_t>(meshlet.triangle_offset + ((meshlet.triangle_count * 3 + 3) & ~3)));
	}
	m_meshletVertices.resize(usedVertexCount);
	m_meshletTriangles.resize(usedTriangleByteCount);
	m_meshletVertices.shrink_to_fit();
	m_meshletTriangles.shrink_to_fit();
	m_meshletBounds.clear();
	m_meshletBounds.reserve(m_meshlets.size());

	size_t globalVertexCount =
		m_vertices->size() / m_perMeshBufferData.vertexByteSize;

	for (size_t i = 0; i < m_meshlets.size(); ++i) {
		auto& meshlet = m_meshlets[i];

		const unsigned int* verts = m_meshletVertices.data() + meshlet.vertex_offset;
		const unsigned char* tris = m_meshletTriangles.data() + meshlet.triangle_offset;

		meshopt_Bounds bounds = meshopt_computeMeshletBounds(
			verts,
			tris,
			meshlet.triangle_count,
			reinterpret_cast<const float*>(m_vertices->data()),
			globalVertexCount,
			m_perMeshBufferData.vertexByteSize
		);

		m_meshletBounds.push_back({ { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius } });
	}

	m_perMeshBufferData.numMeshlets = static_cast<unsigned int>(m_meshlets.size());
}

void Mesh::ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max) {
	// Initialize min and max vectors
	min.x = min.y = min.z = std::numeric_limits<float>::infinity();
	max.x = max.y = max.z = -std::numeric_limits<float>::infinity();

	unsigned int positionsByteStride = m_perMeshBufferData.vertexByteSize;
	unsigned int positionsOffset = 0;
	for (unsigned int i = 0; i < m_vertices->size(); i += positionsByteStride) {
		XMFLOAT3* position = (XMFLOAT3*)(m_vertices->data() + i + positionsOffset);
		min.x = (std::min)(min.x, position->x);
		min.y = (std::min)(min.y, position->y);
		min.z = (std::min)(min.z, position->z);
		max.x = (std::max)(max.x, position->x);
		max.y = (std::max)(max.y, position->y);
		max.z = (std::max)(max.z, position->z);
	}
}

void Mesh::ComputeBoundingSphere(const std::vector<UINT32>& indices) {
	BoundingSphere sphere = {};

	XMFLOAT3 min, max;

	// Compute the Axis-Aligned Bounding Box
	ComputeAABB(min, max);
	XMFLOAT3 center = Scale(Add(min, max), 0.5f);

	// Compute the radius of the bounding sphere
	XMFLOAT3 diagonal = Subtract(max, min);
	float radius = 0.5f * sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y + diagonal.z * diagonal.z);

	// Set the bounding sphere
	sphere.sphere = DirectX::XMFLOAT4(center.x, center.y, center.z, radius);

	m_perMeshBufferData.boundingSphere = sphere;
}

//void Mesh::CreateMeshletReorderedVertices() {
//	const size_t vertexByteSize  = m_perMeshBufferData.vertexByteSize;
//
//	size_t totalVerts = 0;
//	for (auto& ml : m_meshlets) {
//		totalVerts += ml.vertex_count;
//	}
//
//	m_meshletReorderedVertices.resize(totalVerts * vertexByteSize);
//	m_meshletReorderedSkinningVertices.resize(totalVerts * m_skinningVertexSize);
//
//	// Post-skinning vertices
//	std::byte* dst = m_meshletReorderedVertices.data();
//	for (const auto& ml : m_meshlets) {
//		for (unsigned int i = 0; i < ml.vertex_count; ++i) {
//			unsigned int globalIndex = 
//				m_meshletVertices[ml.vertex_offset + i];
//
//			std::byte* src = m_vertices->data()
//				+ globalIndex * vertexByteSize;
//
//			std::copy_n(src, vertexByteSize, dst);
//			dst += vertexByteSize;
//		}
//	}
//
//	// Skinning vertices, if we have them
//	std::byte* dstSkinning = m_meshletReorderedSkinningVertices.data();
//	if (m_skinningVertices) {
//		for (const auto& ml : m_meshlets) {
//			for (unsigned int i = 0; i < ml.vertex_count; ++i) {
//				unsigned int globalIndex =
//					m_meshletVertices[ml.vertex_offset + i];
//				std::byte* src = m_skinningVertices->data()
//					+ globalIndex * m_skinningVertexSize;
//				std::copy_n(src, m_skinningVertexSize, dstSkinning);
//				dstSkinning += m_skinningVertexSize;
//			}
//		}
//	}
//}

void Mesh::CreateBuffers(const std::vector<UINT32>& indices) {
	if (m_prebuiltClusterLOD.has_value()) {
		ApplyPrebuiltClusterLODData(m_prebuiltClusterLOD.value());
		m_prebuiltClusterLOD.reset();
	}
	else {
		BuildClusterLOD(indices);
	}
	CreateMeshlets(indices);
	//CreateMeshletReorderedVertices();
    CreateVertexBuffer();
	ComputeBoundingSphere(indices);

    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT32));
    m_indexCount = static_cast<uint32_t>(indices.size());

	m_indexBufferHandle = Buffer::CreateShared(rhi::HeapType::DeviceLocal, indexBufferSize);

	BUFFER_UPLOAD(indices.data(), indexBufferSize, rg::runtime::UploadTarget::FromShared(m_indexBufferHandle), 0);

    m_indexBufferView.buffer = m_indexBufferHandle->GetAPIResource().GetHandle();
    m_indexBufferView.format = rhi::Format::R32_UInt;
    m_indexBufferView.sizeBytes = indexBufferSize;
}

rhi::VertexBufferView Mesh::GetVertexBufferView() const {
    return m_vertexBufferView;
}

rhi::IndexBufferView Mesh::GetIndexBufferView() const {
    return m_indexBufferView;
}

UINT Mesh::GetIndexCount() const {
    return m_indexCount;
}

int Mesh::GetNextGlobalIndex() {
    return globalMeshCount.fetch_add(1, std::memory_order_relaxed);
}

uint64_t Mesh::GetGlobalID() const {
	return m_globalMeshID;
}

void Mesh::SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_preSkinningVertexBufferView = std::move(view);
	if (m_preSkinningVertexBufferView != nullptr) {
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_preSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max
	}

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_postSkinningVertexBufferView = std::move(view);
	if (m_postSkinningVertexBufferView != nullptr) {
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_postSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max
	}

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

BufferView* Mesh::GetPreSkinningVertexBufferView() {
	return m_preSkinningVertexBufferView.get();
}

BufferView* Mesh::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
}

void Mesh::SetCLodBufferViews(
	std::unique_ptr<BufferView> clusterLODGroupsView,
	std::unique_ptr<BufferView> clusterLODChildrenView,
	std::unique_ptr<BufferView> clusterLODNodesView
) {
	m_clusterLODGroupsView = std::move(clusterLODGroupsView);
	m_clusterLODChildrenView = std::move(clusterLODChildrenView);
	m_clusterLODNodesView = std::move(clusterLODNodesView);

	auto firstChunkOffsetDiv = [](const auto& chunkViews, uint32_t divisor) -> uint32_t {
		for (const auto& chunkView : chunkViews) {
			if (chunkView != nullptr) {
				return static_cast<uint32_t>(chunkView->GetOffset() / divisor);
			}
		}
		return 0u;
	};
	auto firstChunkOffsetBytes = [](const auto& chunkViews) -> uint32_t {
		for (const auto& chunkView : chunkViews) {
			if (chunkView != nullptr) {
				return static_cast<uint32_t>(chunkView->GetOffset());
			}
		}
		return 0u;
	};

	m_perMeshBufferData.clodMeshletBufferOffset = firstChunkOffsetDiv(m_clodMeshletChunkViews, sizeof(meshopt_Meshlet));
	m_perMeshBufferData.clodMeshletVerticesBufferOffset = firstChunkOffsetDiv(m_clodMeshletVertexChunkViews, sizeof(uint32_t));
	m_perMeshBufferData.clodMeshletTrianglesBufferOffset = firstChunkOffsetBytes(m_clodMeshletTriangleChunkViews); // Intentionally in bytes to index byteaddressbuffer
	uint32_t totalMeshletCount = 0;
	for (const auto& group : m_clodGroups) {
		totalMeshletCount += group.meshletCount;
	}
	m_perMeshBufferData.clodNumMeshlets = totalMeshletCount;
	if (m_pCurrentMeshManager != nullptr && m_perMeshBufferView != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetBaseSkin(std::shared_ptr<Skeleton> skeleton) {
	m_baseSkeleton = skeleton;
}

void Mesh::UpdateVertexCount(bool meshletReorderedVertices) {
	//m_perMeshBufferData.numVertices = static_cast<uint32_t>(meshletReorderedVertices ? m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize : m_vertices->size() / m_perMeshBufferData.vertexByteSize);
	m_perMeshBufferData.numVertices = static_cast<uint32_t>(GetStreamingNumVertices());
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetMaterialDataIndex(unsigned int index) {
	m_perMeshBufferData.materialDataIndex = index;
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
