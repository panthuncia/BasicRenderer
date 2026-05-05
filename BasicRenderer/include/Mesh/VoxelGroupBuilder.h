#pragma once

#include <cstdint>
#include <vector>
#include <directxmath.h>

#include "Mesh/ClusterLODTypes.h"

// Input: triangle-based source geometry to voxelize into a single group.
struct VoxelizeTrianglesInput
{
	// Source vertices (interleaved, position at offset 0 as float3).
	const std::vector<std::byte>* vertices = nullptr;
	size_t vertexStrideBytes = 0;

	// Source triangle indices into the vertex buffer (3 per triangle).
	const std::vector<uint32_t>* triangleIndices = nullptr;

	// World-space AABB of the geometry to voxelize.
	DirectX::XMFLOAT3 aabbMin{};
	DirectX::XMFLOAT3 aabbMax{};

	// Resolution (cells per axis) for the output voxel grid.
	uint32_t resolution = 32;

	// Number of rays cast per active cell for opacity sampling.
	uint32_t raysPerCell = 64;
};

// Voxelize a triangle set into a single VoxelGroupPayload.
// Rasterizes triangles into a 3D grid via triangle-AABB overlap (SAT),
// then casts rays for per-cell opacity sampling.
VoxelGroupPayload VoxelizeTriangles(const VoxelizeTrianglesInput& input);

struct PackVoxelGroupInput
{
	const VoxelGroupPayload* payload = nullptr;
	float voxelError = 0.0f;
	float opacityThreshold = 0.0f;
	uint32_t dominantBoneIndex = CLOD_VOXEL_STATIC_BONE_INDEX;
	uint32_t firstCube = 0;
};

struct PackedVoxelGroupBuildResult
{
	CLodVoxelGroupDescriptor descriptor{};
	std::vector<CLodVoxelCubeRecord> cubeRecords;
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
