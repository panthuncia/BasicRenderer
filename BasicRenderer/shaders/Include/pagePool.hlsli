#ifndef PAGE_POOL_HLSLI
#define PAGE_POOL_HLSLI

// Page-pool constants — must match PagePool::Config on the CPU side.
#define PAGE_POOL_PAGE_SIZE  65536u   // 64 KB

// Resolve a virtual page ID to a byte offset within a slab.
// `pageTable` is a StructuredBuffer<PageTableEntry> bound via
//   ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::PageTable)].
// Returns the slab index and absolute slab byte offset for the given page + intra-page offset.
struct PagePoolAddress {
    uint slabIndex;
    uint slabByteOffset;
};

PagePoolAddress ResolvePageAddress(
    StructuredBuffer<PageTableEntry> pageTable,
    uint pageID,
    uint intraPageByteOffset)
{
    PageTableEntry entry = pageTable[pageID];
    PagePoolAddress addr;
    addr.slabIndex = entry.slabIndex;
    addr.slabByteOffset = entry.slabByteOffset + intraPageByteOffset;
    return addr;
}

// Convenience: resolve and return a concrete ByteAddressBuffer from the slab pool.
// `pagePoolSlabBase` is the descriptor heap base index for the slab array.
// Usage:
//   ByteAddressBuffer slab = GetPagePoolSlab(pagePoolSlabBase, addr.slabIndex);
//   uint4 data = slab.Load4(addr.slabByteOffset);
ByteAddressBuffer GetPagePoolSlab(uint pagePoolSlabBase, uint slabIndex)
{
    return ResourceDescriptorHeap[pagePoolSlabBase + slabIndex];
}

// Full resolve: given a page ID and intra-page byte offset, return
// the ByteAddressBuffer and the byte offset within it.
// Note: returning resource handles from functions can be unreliable,
// so callers should use the two-step pattern:
//  PagePoolAddress addr = ResolvePageAddress(pageTable, pageID, intraPageOffset);
//  ByteAddressBuffer slab = GetPagePoolSlab(slabBase, addr.slabIndex);
//  ... = slab.Load(addr.slabByteOffset);

#endif // PAGE_POOL_HLSLI
