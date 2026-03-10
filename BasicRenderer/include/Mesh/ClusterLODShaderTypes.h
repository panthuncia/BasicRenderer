#pragma once

// Header containing CLod-related types originally defined in
// ShaderBuffers.h.  These types only depend on DirectXMath and the
// clusterlod.h header, so they can be consumed by headless code.

#include <cstdint>
#include <DirectXMath.h>
#include "ThirdParty/meshoptimizer/clusterlod.h"

struct BoundingSphere {
	DirectX::XMFLOAT4 sphere;
};

struct ClusterLODGroupChunk
{
	// Group metadata
	uint32_t groupVertexCount = 0;
	uint32_t meshletVertexCount = 0;
	uint32_t meshletCount = 0;
	uint32_t meshletTrianglesByteCount = 0;
	uint32_t meshletBoundsCount = 0;

	// Compressed group-local position stream (u32 bitstream words)
	uint32_t compressedPositionWordCount = 0;
	uint32_t compressedPositionBitsX = 0;
	uint32_t compressedPositionBitsY = 0;
	uint32_t compressedPositionBitsZ = 0;
	uint32_t compressedPositionQuantExp = 0;
	int32_t compressedPositionMinQx = 0;
	int32_t compressedPositionMinQy = 0;
	int32_t compressedPositionMinQz = 0;

	// Compressed group-local normal stream (oct-encoded snorm16x2 packed into u32)
	uint32_t compressedNormalWordCount = 0;

	// Compressed group-local meshlet vertex index stream (u32 bitstream words)
	uint32_t compressedMeshletVertexWordCount = 0;
	uint32_t compressedMeshletVertexBits = 0;
	uint32_t compressedFlags = 0;

	// Page-pool fields 
	// All 8 data streams (vertices, meshlet vertex indices, triangles,
	// compressed positions/normals/meshlet vertices, meshlets, bounds)
	// are stored in a unified slab ByteAddressBuffer.
	//
	// The slab descriptor index + base byte offset are resolved on the CPU
	// and stored here so the shader can bind the slab directly without a
	// page-table lookup.
	uint32_t pagePoolSlabDescriptorIndex = 0;         // Raw descriptor-heap index of the slab BAB.
	uint32_t pagePoolSlabByteOffset = 0;              // Byte offset of allocation start within the slab.
	uint32_t vertexIntraPageByteOffset = 0;           // Byte offset within allocation for vertex data.
	uint32_t meshletVertexIntraPageByteOffset = 0;    // Byte offset within allocation for meshlet vertex indices.
	uint32_t triangleIntraPageByteOffset = 0;         // Byte offset within allocation for triangle data.
	uint32_t compPosIntraPageByteOffset = 0;          // Byte offset within allocation for compressed positions.
	uint32_t compNormIntraPageByteOffset = 0;         // Byte offset within allocation for compressed normals.
	uint32_t compMeshletVertIntraPageByteOffset = 0;  // Byte offset within allocation for compressed meshlet vertex indices.
	uint32_t meshletIntraPageByteOffset = 0;          // Byte offset within allocation for meshlet offsets.
	uint32_t boundsIntraPageByteOffset = 0;           // Byte offset within allocation for meshlet bounds.
};

// Cluster LOD data
// One entry per (group -> refinedGroup) edge.
// refinedGroup == -1 means "terminal meshlets" (original geometry)
struct ClusterLODChild
{
	int32_t  refinedGroup;              // group id to refine into, or -1
	uint32_t firstLocalMeshletIndex;     // group-local start meshlet index for this child bucket
	uint32_t localMeshletCount;          // number of local meshlets in this contiguous child range
	uint32_t pad = 0;
};

struct ClusterLODGroup
{
	clodBounds bounds; // 5 floats
	uint32_t firstMeshlet = 0;
	uint32_t meshletCount = 0;
	int32_t depth = 0;

	uint32_t firstGroupVertex = 0;
	uint32_t groupVertexCount = 0;
	uint32_t firstChild = 0;    // offset into m_clodChildren
	uint32_t childCount = 0;    // number of ClusterLODChild entries for this group

	uint32_t terminalChildCount = 0;
	uint32_t flags = 0;         // Bit 0: IS_VOXEL_GROUP
	uint32_t pad[2] = {0};
};

static constexpr uint32_t CLOD_GROUP_FLAG_IS_VOXEL = 1u << 0;
