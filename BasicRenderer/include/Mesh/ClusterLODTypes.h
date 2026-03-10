#pragma once

// Standalone header containing all ClusterLOD data types and the
// MeshIngestBuilder class.  No GPU / RHI / DeletionManager / BufferView
// dependencies — only standard library, DirectXMath, meshoptimizer, and the
// CLod shader types header.

#include <cstdint>
#include <directxmath.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <meshoptimizer.h>

#include "Mesh/ClusterLODShaderTypes.h"
#include "Mesh/VertexFlags.h"

// Forward declarations for GPU-side types only needed by MeshIngestBuilder::Build()
class Material;
class Mesh;

enum class MeshCpuDataPolicy {
	Retain,
	ReleaseAfterUpload,
};

// Traversal types

struct ClusterLODTraversalMetric
{
	float boundingSphereX = 0;
	float boundingSphereY = 0;
	float boundingSphereZ = 0;
	float boundingSphereRadius = 0;

	float maxQuadricError = 0;
	float padding[3] = { 0,0,0 };
};

struct ClusterLODNodeRange
{
	uint32_t isGroup = 0;
	uint32_t indexOrOffset = 0;
	uint32_t countMinusOne = 0;
	uint32_t padding = 0;
};

struct ClusterLODNode
{
	ClusterLODNodeRange        range{};
	ClusterLODTraversalMetric  traversalMetric{};
};

struct ClusterLODNodeRangeAlloc
{
	uint32_t offset = 0;
	uint32_t count = 0;
};

// Disk / Cache types

struct ClusterLODDiskChunkSpan
{
	uint64_t offset = 0;
	uint64_t sizeBytes = 0;
};

struct ClusterLODGroupDiskLocator
{
	uint64_t blobOffset = 0;
	uint32_t blobSizeBytes = 0;
	uint32_t reserved = 0;
};

struct ClusterLODCacheSource
{
	std::string sourceIdentifier;
	std::string primPath;
	std::string subsetName;
	uint64_t buildConfigHash = 0;
	std::wstring containerFileName;
};

// Voxel group data model

struct VoxelCell
{
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t z = 0;
	float    opacity = 0.0f;
};

struct VoxelGroupPayload
{
	uint32_t resolution = 0;
	DirectX::XMFLOAT3 aabbMin{};
	DirectX::XMFLOAT3 aabbMax{};
	std::vector<VoxelCell> activeCells;
};

struct VoxelGroupMapping
{
	std::vector<int32_t> groupToPayloadIndex;
	std::vector<VoxelGroupPayload> payloads;
};

// Prebuilt / Cache payload types

struct ClusterLODPrebuiltData
{
	std::vector<ClusterLODGroup> groups;
	std::vector<ClusterLODChild> children;
	BoundingSphere objectBoundingSphere{};
	std::vector<ClusterLODGroupChunk> groupChunks;
	std::vector<ClusterLODGroupDiskLocator> groupDiskLocators;
	ClusterLODCacheSource cacheSource;
	std::vector<ClusterLODNode> nodes;
	VoxelGroupMapping voxelGroupMapping;
};

struct ClusterLODCacheBuildPayload
{
	const std::vector<std::vector<std::byte>>* groupVertexChunks = nullptr;
	const std::vector<std::vector<std::byte>>* groupSkinningVertexChunks = nullptr;
	const std::vector<std::vector<uint32_t>>* groupMeshletVertexChunks = nullptr;
	const std::vector<std::vector<uint32_t>>* groupCompressedPositionWordChunks = nullptr;
	const std::vector<std::vector<uint32_t>>* groupCompressedNormalWordChunks = nullptr;
	const std::vector<std::vector<uint32_t>>* groupCompressedMeshletVertexWordChunks = nullptr;
	const std::vector<std::vector<meshopt_Meshlet>>* groupMeshletChunks = nullptr;
	const std::vector<std::vector<uint8_t>>* groupMeshletTriangleChunks = nullptr;
	const std::vector<std::vector<BoundingSphere>>* groupMeshletBoundsChunks = nullptr;
};

struct ClusterLODCacheBuildOwnedData
{
	std::vector<std::vector<std::byte>> groupVertexChunks;
	std::vector<std::vector<std::byte>> groupSkinningVertexChunks;
	std::vector<std::vector<uint32_t>> groupMeshletVertexChunks;
	std::vector<std::vector<uint32_t>> groupCompressedPositionWordChunks;
	std::vector<std::vector<uint32_t>> groupCompressedNormalWordChunks;
	std::vector<std::vector<uint32_t>> groupCompressedMeshletVertexWordChunks;
	std::vector<std::vector<meshopt_Meshlet>> groupMeshletChunks;
	std::vector<std::vector<uint8_t>> groupMeshletTriangleChunks;
	std::vector<std::vector<BoundingSphere>> groupMeshletBoundsChunks;

	ClusterLODCacheBuildPayload AsPayload() const {
		ClusterLODCacheBuildPayload payload{};
		payload.groupVertexChunks = &groupVertexChunks;
		payload.groupSkinningVertexChunks = &groupSkinningVertexChunks;
		payload.groupMeshletVertexChunks = &groupMeshletVertexChunks;
		payload.groupCompressedPositionWordChunks = &groupCompressedPositionWordChunks;
		payload.groupCompressedNormalWordChunks = &groupCompressedNormalWordChunks;
		payload.groupCompressedMeshletVertexWordChunks = &groupCompressedMeshletVertexWordChunks;
		payload.groupMeshletChunks = &groupMeshletChunks;
		payload.groupMeshletTriangleChunks = &groupMeshletTriangleChunks;
		payload.groupMeshletBoundsChunks = &groupMeshletBoundsChunks;
		return payload;
	}
};

struct ClusterLODPrebuildArtifacts
{
	ClusterLODPrebuiltData prebuiltData;
	ClusterLODCacheBuildOwnedData cacheBuildData;
};

// Builder settings

struct ClusterLODBuilderSettings
{
	bool disableSloppyFallback = false;
	float lodErrorMergePrevious = 1.5f;
	float lodErrorMergeAdditive = 0.0f;
	uint32_t partitionSizeFloor = 8u;
	bool allowOverflowTerminalMerge = true;
	bool preserveImportedNormals = true;
	bool enableNormalAttributeSimplification = true;
	float normalAttributeWeight = 1.0f;
	float simplifyTangentWeight = 0.01f;
	float simplifyTangentSignWeight = 0.5f;

	bool enableVoxelFallback = false;
	uint32_t voxelGridBaseResolution = 32u;
	uint32_t voxelMinResolution = 2u;
	uint32_t voxelRaysPerCell = 64u;
	float    voxelTerminalErrorMultiplier = 2.0f;
	uint32_t voxelCoarsenFactor = 2u;
};

// Runtime summary

struct ClusterLODRuntimeSummary
{
	struct GroupChunkHint
	{
		uint32_t groupVertexCount = 0;
		uint32_t meshletVertexCount = 0;
		uint32_t meshletCount = 0;
		uint32_t meshletTrianglesByteCount = 0;
		uint32_t meshletBoundsCount = 0;
		uint32_t compressedPositionWordCount = 0;
		uint32_t compressedNormalWordCount = 0;
		uint32_t compressedMeshletVertexWordCount = 0;
	};

	struct GroupRange
	{
		uint32_t firstGroup = 0;
		uint32_t groupCount = 0;
	};

	std::vector<GroupChunkHint> groupChunkHints;
	std::vector<int32_t> parentGroupByLocal;
	std::vector<uint32_t> firstGroupVertexByLocal;
	std::vector<GroupRange> coarsestRanges;
};

// MeshIngestBuilder

// Accumulates raw vertex/index data and produces ClusterLOD artifacts
// or GPU Mesh objects (renderer-side).  The Build() method is only implemented
// in the renderer; BuildClusterLODArtifacts() has no GPU dependencies.
class MeshIngestBuilder {
public:
	MeshIngestBuilder(
		unsigned int vertexSize,
		unsigned int skinningVertexSize,
		unsigned int flags,
		ClusterLODBuilderSettings clusterLODBuilderSettings = {})
		: m_vertexSize(vertexSize),
		  m_skinningVertexSize(skinningVertexSize),
		  m_flags(flags),
		  m_clusterLODBuilderSettings(std::move(clusterLODBuilderSettings)) {}

	void ReserveVertices(size_t vertexCount) {
		m_vertices.reserve(vertexCount * static_cast<size_t>(m_vertexSize));
	}

	void ReserveIndices(size_t indexCount) {
		m_indices.reserve(indexCount);
	}

	void AppendVertexBytes(const std::byte* data, size_t byteCount) {
		if (byteCount != m_vertexSize) {
			throw std::runtime_error("MeshIngestBuilder vertex byte size mismatch");
		}
		m_vertices.insert(m_vertices.end(), data, data + byteCount);
	}

	void AppendSkinningVertexBytes(const std::byte* data, size_t byteCount) {
		if (m_skinningVertexSize == 0) {
			throw std::runtime_error("MeshIngestBuilder has no skinning vertex format");
		}
		if (byteCount != m_skinningVertexSize) {
			throw std::runtime_error("MeshIngestBuilder skinning vertex byte size mismatch");
		}
		m_skinningVertices.insert(m_skinningVertices.end(), data, data + byteCount);
	}

	void AppendIndex(uint32_t index) {
		m_indices.push_back(index);
	}

	void AppendIndices(const uint32_t* data, size_t count) {
		m_indices.insert(m_indices.end(), data, data + count);
	}

	// GPU-side: creates a Mesh object with buffer views.
	// Only implemented in the renderer (Mesh.cpp); not available in headless builds.
	std::shared_ptr<Mesh> Build(
		const std::shared_ptr<Material>& material,
		std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD = std::nullopt,
		MeshCpuDataPolicy cpuDataPolicy = MeshCpuDataPolicy::Retain);

	// Headless: runs the full ClusterLOD build pipeline (CPU-only).
	ClusterLODPrebuildArtifacts BuildClusterLODArtifacts() const;

	void SetClusterLODBuilderSettings(const ClusterLODBuilderSettings& settings) {
		m_clusterLODBuilderSettings = settings;
	}

	const ClusterLODBuilderSettings& GetClusterLODBuilderSettings() const {
		return m_clusterLODBuilderSettings;
	}

private:
	unsigned int m_vertexSize = 0;
	unsigned int m_skinningVertexSize = 0;
	unsigned int m_flags = 0;
	std::vector<std::byte> m_vertices;
	std::vector<std::byte> m_skinningVertices;
	std::vector<uint32_t> m_indices;
	ClusterLODBuilderSettings m_clusterLODBuilderSettings{};
};
