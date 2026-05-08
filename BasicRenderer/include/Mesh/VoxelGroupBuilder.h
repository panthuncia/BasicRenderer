#pragma once

#include <cstdint>
#include <vector>
#include <directxmath.h>

#include "Mesh/ClusterLODTypes.h"

struct VoxelSourceCandidatePayload
{
	const VoxelGroupPayload* payload = nullptr;
	float expansionRadius = 0.0f;
};

// Input: triangle-based source geometry to voxelize into a single group.
struct VoxelizeTrianglesInput
{
	// Source vertices (interleaved, position at offset 0 as float3).
	const std::vector<std::byte>* vertices = nullptr;
	size_t vertexStrideBytes = 0;
	const std::vector<std::byte>* skinningVertices = nullptr;
	size_t skinningVertexStrideBytes = 0;

	// Source triangle indices into the vertex buffer (3 per triangle).
	const std::vector<uint32_t>* triangleIndices = nullptr;
	const std::vector<int32_t>* triangleRefinedGroupIds = nullptr;

	// Optional already-voxelized sources. These are re-sampled as volumes when
	// building a coarser voxel parent.
	const std::vector<const VoxelGroupPayload*>* sourceVoxelPayloads = nullptr;

	// Optional already-voxelized sources used only to define candidate output
	// cells. Coverage for these candidates is evaluated from triangle sources.
	const std::vector<VoxelSourceCandidatePayload>* candidateVoxelPayloads = nullptr;
	bool keepZeroCoverageSourceCells = false;

	// World-space AABB of the geometry to voxelize.
	DirectX::XMFLOAT3 aabbMin{};
	DirectX::XMFLOAT3 aabbMax{};
	float voxelWidth = 0.0f;

	// Resolution (cells per axis) for the output voxel grid.
	uint32_t resolution = 32;

	// Number of rays cast per active cell for opacity sampling.
	uint32_t raysPerCell = 64;
};

struct VoxelizeTrianglesResult
{
	// Cells used for rendering this group after coverage pruning.
	VoxelGroupPayload renderPayload;
	// Pre-prune candidate cells retained so coarser parents can reintroduce
	// cells trimmed from this group's render payload.
	VoxelGroupPayload sourcePayload;
	uint32_t triangleCandidateCellCount = 0;
	uint32_t voxelCandidateCellCount = 0;
	uint32_t candidateCellCount = 0;
	uint32_t positiveCoverageCellCount = 0;
	float totalCoverage = 0.0f;
	float maxCoverage = 0.0f;
	uint32_t prunedCellCount = 0;
};

// Voxelize a triangle set into a single VoxelGroupPayload.
// Rasterizes triangles into a 3D grid via triangle-AABB overlap (SAT),
// then casts rays for per-cell opacity sampling.
VoxelGroupPayload VoxelizeTriangles(const VoxelizeTrianglesInput& input);
VoxelizeTrianglesResult VoxelizeTrianglesDetailed(const VoxelizeTrianglesInput& input);

struct PackVoxelGroupInput
{
	const VoxelGroupPayload* payload = nullptr;
	float voxelError = 0.0f;
	float opacityThreshold = 0.0f;
	// Fallback for static content or cells with no usable skinning data.
	uint32_t dominantBoneIndex = CLOD_VOXEL_STATIC_BONE_INDEX;
	uint32_t firstCube = 0;
	uint32_t firstAttribute = 0;
};

struct PackedVoxelGroupBuildResult
{
	CLodVoxelGroupDescriptor descriptor{};
	std::vector<CLodVoxelCubeRecord> cubeRecords;
	std::vector<CLodVoxelAttributeSample> attributeSamples;
};

PackedVoxelGroupBuildResult PackVoxelGroupToCubes(const PackVoxelGroupInput& input);

// Morton sorting: returns a permutation of [0, count) that places positions
// in 3D Morton (Z-order) order within the given AABB.
std::vector<uint32_t> MortonSort(
	const DirectX::XMFLOAT3* positions,
	uint32_t count,
	const DirectX::XMFLOAT3& aabbMin,
	const DirectX::XMFLOAT3& aabbMax);

// Spatial merging: given a Morton-sorted list of group indices and their AABBs,
// greedily merge consecutive groups into batches of <= maxFanout.
// Returns: vector of batches, each batch is a vector of original group indices.
std::vector<std::vector<uint32_t>> MergeGroupsSpatial(
	const std::vector<uint32_t>& sortedGroupIndices,
	uint32_t maxFanout);
