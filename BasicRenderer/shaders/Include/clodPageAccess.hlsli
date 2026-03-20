#ifndef CLOD_PAGE_ACCESS_HLSLI
#define CLOD_PAGE_ACCESS_HLSLI

#include "clodStructs.hlsli"

// Load a CLodPageHeader from a slab ByteAddressBuffer at a given byte offset.
// The header occupies 16 x uint32 = 64 bytes at the start of each page tile.
CLodPageHeader LoadPageHeader(uint slabDescriptorIndex, uint pageByteOffset)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[slabDescriptorIndex];
    uint4 d0 = slab.Load4(pageByteOffset +  0);
    uint4 d1 = slab.Load4(pageByteOffset + 16);

    CLodPageHeader hdr;
    hdr.meshletCount               = d0.x;
    hdr.compressedPositionQuantExp = d0.y;
    hdr.descriptorOffset           = d0.z;
    hdr.positionBitstreamOffset    = d0.w;

    hdr.normalArrayOffset          = d1.x;
    hdr.triangleStreamOffset       = d1.y;
    hdr.reserved0 = 0;
    hdr.reserved1 = 0;
    hdr.reserved2 = 0;
    hdr.reserved3 = 0;
    hdr.reserved4 = 0;
    hdr.reserved5 = 0;
    hdr.reserved6 = 0;
    hdr.reserved7 = 0;
    hdr.reserved8 = 0;
    hdr.reserved9 = 0;

    return hdr;
}

// Load a CLodMeshletDescriptor from a slab ByteAddressBuffer.
CLodMeshletDescriptor LoadMeshletDescriptor(uint slabDescriptorIndex, uint pageByteOffset, uint descriptorOffset, uint meshletIndex)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[slabDescriptorIndex];
    uint addr = pageByteOffset + descriptorOffset + meshletIndex * 48u;
    uint4 d0 = slab.Load4(addr +  0);
    uint4 d1 = slab.Load4(addr + 16);
    uint4 d2 = slab.Load4(addr + 32);

    CLodMeshletDescriptor desc;
    desc.positionBitOffset           = d0.x;
    desc.normalWordOffset            = d0.y;
    desc.triangleByteOffset          = d0.z;
    desc.minQx                       = asint(d0.w);
    desc.minQy                       = asint(d1.x);
    desc.minQz                       = asint(d1.y);
    desc.bitsAndVertexCount          = d1.z;
    desc.triangleCountAndRefinedGroup = d1.w;
    desc.bounds                      = asfloat(d2);

    return desc;
}

// Load only the meshletCount from the page header (first uint32).
uint LoadPageMeshletCount(uint slabDescriptorIndex, uint pageByteOffset)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[slabDescriptorIndex];
    return slab.Load(pageByteOffset); // meshletCount is uint[0]
}

// Resolve a group-local page index to a physical slab location via the GroupPageMap buffer.
GroupPageMapEntry LoadGroupPageMapEntry(uint pageMapBase, uint pageIndex)
{
    StructuredBuffer<GroupPageMapEntry> pageMap =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::GroupPageMap)];
    return pageMap[pageMapBase + pageIndex];
}

#endif // CLOD_PAGE_ACCESS_HLSLI
