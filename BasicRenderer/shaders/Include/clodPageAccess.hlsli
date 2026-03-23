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
    uint4 d2 = slab.Load4(pageByteOffset + 32);

    CLodPageHeader hdr;
    hdr.meshletCount               = d0.x;
    hdr.compressedPositionQuantExp = d0.y;
    hdr.attributeMask              = d0.z;
    hdr.descriptorOffset           = d0.w;

    hdr.positionBitstreamOffset    = d1.x;
    hdr.normalArrayOffset          = d1.y;
    hdr.uvBitstreamOffset          = d1.z;
    hdr.triangleStreamOffset       = d1.w;
    hdr.reserved0 = d2.x;
    hdr.reserved1 = d2.y;
    hdr.reserved2 = d2.z;
    hdr.reserved3 = d2.w;
    hdr.reserved4 = 0;
    hdr.reserved5 = 0;
    hdr.reserved6 = 0;
    hdr.reserved7 = 0;

    return hdr;
}

// Load a CLodMeshletDescriptor from a slab ByteAddressBuffer.
CLodMeshletDescriptor LoadMeshletDescriptor(uint slabDescriptorIndex, uint pageByteOffset, uint descriptorOffset, uint meshletIndex)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[slabDescriptorIndex];
    uint addr = pageByteOffset + descriptorOffset + meshletIndex * 80u;
    uint4 d0 = slab.Load4(addr +  0);
    uint4 d1 = slab.Load4(addr + 16);
    uint4 d2 = slab.Load4(addr + 32);
    uint4 d3 = slab.Load4(addr + 48);
    uint4 d4 = slab.Load4(addr + 64);

    CLodMeshletDescriptor desc;
    desc.positionBitOffset           = d0.x;
    desc.normalWordOffset            = d0.y;
    desc.uvBitOffset                 = d0.z;
    desc.triangleByteOffset          = d0.w;
    desc.minQx                       = asint(d1.x);
    desc.minQy                       = asint(d1.y);
    desc.minQz                       = asint(d1.z);
    desc.bitsAndVertexCount          = d1.w;
    desc.triangleCountAndRefinedGroup = d2.x;
    desc.uvMinU                      = asfloat(d2.y);
    desc.uvMinV                      = asfloat(d2.z);
    desc.uvScaleU                    = asfloat(d2.w);
    desc.uvScaleV                    = asfloat(d3.x);
    desc.uvBits                      = d3.y;
    desc.reserved0                   = d3.z;
    desc.reserved1                   = d3.w;
    desc.bounds                      = asfloat(d4);

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
