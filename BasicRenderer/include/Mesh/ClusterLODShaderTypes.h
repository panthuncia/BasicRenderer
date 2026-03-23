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

static constexpr uint32_t CLOD_PAGE_ATTRIBUTE_NORMAL = 1u << 0;

// Embedded at byte 0 of each page-tile in the page pool.
// Compression params moved to per-meshlet descriptors.
// 16 x uint32 = 64 bytes.
struct CLodPageHeader
{
	uint32_t meshletCount = 0;            // [0] number of meshlets in this page
	uint32_t compressedPositionQuantExp = 0; // [1] mesh-wide quantization exponent
	uint32_t attributeMask = 0;           // [2] page-wide optional non-UV attribute mask
	uint32_t uvSetCount = 0;              // [3] UV set count packed into this page

	uint32_t descriptorOffset = 0;        // [4] byte offset to CLodMeshletDescriptor array
	uint32_t uvDescriptorOffset = 0;      // [5] byte offset to CLodMeshletUvDescriptor table
	uint32_t positionBitstreamOffset = 0; // [6] byte offset to position bitstream
	uint32_t normalArrayOffset = 0;       // [7] byte offset to normal array (oct-encoded uint32 per vertex)
	uint32_t uvBitstreamDirectoryOffset = 0; // [8] byte offset to UV bitstream offset table
	uint32_t triangleStreamOffset = 0;    // [9] byte offset to triangle byte stream
	uint32_t reserved[6] = {};            // [10-15] pad to 64 bytes
};
static_assert(sizeof(CLodPageHeader) == 64, "CLodPageHeader must be 64 bytes");

// Per-meshlet descriptor in an SoA page format.
// Self-contained: each meshlet carries its own non-UV compression params, bounds, and LOD metadata.
struct CLodMeshletDescriptor
{
	// Stream offsets within the page
	uint32_t positionBitOffset = 0;       // [0] bit offset into page position bitstream
	uint32_t normalWordOffset = 0;        // [1] word offset into page normal array
	uint32_t triangleByteOffset = 0;      // [2] byte offset into page triangle stream
	uint32_t reserved0 = 0;               // [3]

	// Per-meshlet compression parameters
	int32_t  minQx = 0;                   // [4] quantization offset X
	int32_t  minQy = 0;                   // [5] quantization offset Y
	int32_t  minQz = 0;                   // [6] quantization offset Z

	// Packed: bitsX:8 | bitsY:8 | bitsZ:8 | vertexCount:8
	uint32_t bitsAndVertexCount = 0;      // [7]
	// Packed: triangleCount:16 | refinedGroupId+1:16 (0 = terminal, >0 = groupId+1)
	uint32_t triangleCountAndRefinedGroup = 0; // [8]
	uint32_t reserved1 = 0;               // [9]
	uint32_t reserved2 = 0;               // [10]
	uint32_t reserved3 = 0;               // [11]

	// Bounding sphere (object space)
	DirectX::XMFLOAT4 bounds = {};        // [12-15] {cx, cy, cz, radius}
};
static_assert(sizeof(CLodMeshletDescriptor) == 64, "CLodMeshletDescriptor must be 64 bytes");

// Per-(meshlet, uv-set) descriptor in an SoA page format.
// 8 x uint32 = 32 bytes = 2 x Load4 on GPU.
struct CLodMeshletUvDescriptor
{
	uint32_t uvBitOffset = 0;             // [0] bit offset into this UV set's page-local bitstream
	float    uvMinU = 0.0f;               // [1]
	float    uvMinV = 0.0f;               // [2]
	float    uvScaleU = 0.0f;             // [3]
	float    uvScaleV = 0.0f;             // [4]
	uint32_t uvBits = 0;                  // [5] bitsU:8 | bitsV:8
	uint32_t reserved0 = 0;               // [6]
	uint32_t reserved1 = 0;               // [7]
};
static_assert(sizeof(CLodMeshletUvDescriptor) == 32, "CLodMeshletUvDescriptor must be 32 bytes");

// Runtime-filled entry mapping a group-local page index to its physical slab location.
struct GroupPageMapEntry
{
	uint32_t slabDescriptorIndex = 0; // Descriptor-heap index of the slab BAB
	uint32_t slabByteOffset = 0;      // Byte offset of page start in slab
};

// Retained for CPU-side streaming state tracking.
struct ClusterLODGroupChunk
{
	uint32_t groupVertexCount = 0;
	uint32_t meshletCount = 0;
	uint32_t meshletTrianglesByteCount = 0;
	uint32_t compressedPositionQuantExp = 0;
	uint32_t compressedFlags = 0;
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
