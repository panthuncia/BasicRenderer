#ifndef CLOD_PAGE_ACCESS_HLSLI
#define CLOD_PAGE_ACCESS_HLSLI

#include "clodStructs.hlsli"

// Load a CLodPageHeader from a slab ByteAddressBuffer at a given byte offset.
// The header occupies 32 x uint32 = 128 bytes at the start of each page tile.
CLodPageHeader LoadPageHeader(uint slabDescriptorIndex, uint pageByteOffset)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[slabDescriptorIndex];
    uint4 d0 = slab.Load4(pageByteOffset +  0);
    uint4 d1 = slab.Load4(pageByteOffset + 16);
    uint4 d2 = slab.Load4(pageByteOffset + 32);
    uint4 d3 = slab.Load4(pageByteOffset + 48);
    uint4 d4 = slab.Load4(pageByteOffset + 64);
    uint4 d5 = slab.Load4(pageByteOffset + 80);

    CLodPageHeader hdr;
    hdr.vertexCount                      = d0.x;
    hdr.meshletCount                     = d0.y;
    hdr.meshletVertexCount               = d0.z;
    hdr.meshletTrianglesByteCount        = d0.w;

    hdr.compressedPositionWordCount      = d1.x;
    hdr.compressedNormalWordCount        = d1.y;
    hdr.compressedMeshletVertexWordCount = d1.z;

    hdr.vertexOffset                     = d1.w;
    hdr.meshletVertexOffset              = d2.x;
    hdr.triangleOffset                   = d2.y;
    hdr.compPosOffset                    = d2.z;
    hdr.compNormOffset                   = d2.w;
    hdr.compMeshletVertOffset            = d3.x;
    hdr.meshletStructOffset              = d3.y;
    hdr.boundsOffset                     = d3.z;

    hdr.compressedPositionBitsX          = d3.w;
    hdr.compressedPositionBitsY          = d4.x;
    hdr.compressedPositionBitsZ          = d4.y;
    hdr.compressedPositionQuantExp       = d4.z;
    hdr.compressedPositionMinQx          = asint(d4.w);
    hdr.compressedPositionMinQy          = asint(d5.x);
    hdr.compressedPositionMinQz          = asint(d5.y);
    hdr.compressedMeshletVertexBits      = d5.z;
    hdr.compressedFlags                  = d5.w;

    hdr.reserved0 = 0;
    hdr.reserved1 = 0;
    hdr.reserved2 = 0;
    hdr.reserved3 = 0;
    hdr.reserved4 = 0;
    hdr.reserved5 = 0;
    hdr.reserved6 = 0;
    hdr.reserved7 = 0;

    return hdr;
}

// Load only the boundsOffset field from the page header (field [14], byte offset 56).
uint LoadPageBoundsOffset(uint slabDescriptorIndex, uint pageByteOffset)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[slabDescriptorIndex];
    return slab.Load(pageByteOffset + 56); // boundsOffset is uint[14]
}

// Resolve a group-local page index to a physical slab location via the GroupPageMap buffer.
GroupPageMapEntry LoadGroupPageMapEntry(uint pageMapBase, uint pageIndex)
{
    StructuredBuffer<GroupPageMapEntry> pageMap =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::GroupPageMap)];
    return pageMap[pageMapBase + pageIndex];
}

#endif // CLOD_PAGE_ACCESS_HLSLI
