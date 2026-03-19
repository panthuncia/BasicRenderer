#pragma once

// Header containing CLod-related types originally defined in
// ShaderBuffers.h.  These types only depend on DirectXMath and the
// clusterlod.h header, so they can be consumed by headless code.

#include <cstdint>
#include <DirectXMath.h>
#include "ThirdParty/meshoptimizer/clusterlod.h"

// Define to use legacy per-refined-group segment splitting with
// segment-level LOD condition 2 + streaming fallback.
// When undefined (default), segments are one-per-page and condition 2
// is evaluated per-meshlet in ClusterCullBuckets.
// #define CLOD_PER_REFINED_GROUP_SEGMENTS

struct BoundingSphere {
	DirectX::XMFLOAT4 sphere;
};

// Embedded at byte 0 of each page-tile in the page pool.
// Self-contained: carries all stream offsets + compression params.
// 32 x uint32 = 128 bytes, aligned to GPU cache lines.
struct CLodPageHeader
{
	uint32_t vertexCount = 0;
	uint32_t meshletCount = 0;
	uint32_t meshletVertexCount = 0;
	uint32_t meshletTrianglesByteCount = 0;

	uint32_t compressedPositionWordCount = 0;
	uint32_t compressedNormalWordCount = 0;
	uint32_t compressedMeshletVertexWordCount = 0;

	// Byte offsets of each stream relative to page start (after this header)
	uint32_t vertexOffset = 0;
	uint32_t meshletVertexOffset = 0;
	uint32_t triangleOffset = 0;
	uint32_t compPosOffset = 0;
	uint32_t compNormOffset = 0;
	uint32_t compMeshletVertOffset = 0;
	uint32_t meshletStructOffset = 0;
	uint32_t boundsOffset = 0;

	// Compression parameters (duplicated per-page for self-containment)
	uint32_t compressedPositionBitsX = 0;
	uint32_t compressedPositionBitsY = 0;
	uint32_t compressedPositionBitsZ = 0;
	uint32_t compressedPositionQuantExp = 0;
	int32_t  compressedPositionMinQx = 0;
	int32_t  compressedPositionMinQy = 0;
	int32_t  compressedPositionMinQz = 0;
	uint32_t compressedMeshletVertexBits = 0;
	uint32_t compressedFlags = 0;

	uint32_t refinedGroupIdOffset = 0; // byte offset of per-meshlet refined group ID stream (int32, -1 = terminal)
	uint32_t reserved[7] = {}; // pad to 128 bytes (32 x uint32)
};
static_assert(sizeof(CLodPageHeader) == 128, "CLodPageHeader must be 128 bytes");

// Runtime-filled entry mapping a group-local page index to its physical slab location.
struct GroupPageMapEntry
{
	uint32_t slabDescriptorIndex = 0; // Descriptor-heap index of the slab BAB
	uint32_t slabByteOffset = 0;      // Byte offset of page start in slab
};

// Legacy: retained for CPU-side streaming state tracking only.
// No longer uploaded as a GPU StructuredBuffer - replaced by
// CLodPageHeader (embedded in pages) + GroupPageMapEntry (runtime indirection).
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
// A segment references a contiguous run of meshlets within a single page.
struct ClusterLODGroupSegment
{
	int32_t  refinedGroup;              // group id to refine into, or -1
	uint32_t firstMeshletInPage;         // page-local start meshlet index
	uint32_t meshletCount;               // number of meshlets in this segment
	uint32_t pageIndex = 0;              // group-local page index (0..pageCount-1)
};

struct ClusterLODGroup
{
	clodBounds bounds; // 5 floats
	uint32_t firstMeshlet = 0;
	uint32_t meshletCount = 0;
	int32_t depth = 0;

	uint32_t firstGroupVertex = 0;
	uint32_t groupVertexCount = 0;
	uint32_t firstSegment = 0;    // offset into m_clodSegments
	uint32_t segmentCount = 0;    // number of ClusterLODGroupSegment entries for this group

	uint32_t terminalSegmentCount = 0;
	uint32_t flags = 0;         // Bit 0: IS_VOXEL_GROUP
	uint32_t pageMapBase = 0;   // absolute index into GroupPageMap buffer
	uint32_t pageCount = 0;     // number of pages for this group
	int32_t  parentGroupId = -1; // mesh-local group index of the parent group (-1 for root)
};

static constexpr uint32_t CLOD_GROUP_FLAG_IS_VOXEL = 1u << 0;
