#pragma once

#include <cstdint>
#include <vector>
#include <directxmath.h>

#include "Mesh/Mesh.h"

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

// Input: child voxel groups to re-voxelize into a coarser parent group.
// Each child is a VoxelGroupPayload whose cells are treated as box primitives.
struct VoxelizeVoxelsInput
{
	/// Child voxel payloads to merge.
	const std::vector<const VoxelGroupPayload*>* children = nullptr;

	/// Parent AABB (typically union of child AABBs, slightly expanded).
	DirectX::XMFLOAT3 aabbMin{};
	DirectX::XMFLOAT3 aabbMax{};

	/// Resolution (cells per axis) for the parent voxel grid.
	uint32_t resolution = 16;

	/// Number of rays cast per active parent cell for opacity sampling.
	uint32_t raysPerCell = 64;
};

// Re-voxelize child voxel grids into a coarser parent grid.
// Child active cells are treated as axis-aligned box primitives for both
// rasterization (box-AABB overlap) and opacity tracing (ray-slab intersection).
VoxelGroupPayload VoxelizeVoxels(const VoxelizeVoxelsInput& input);

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
