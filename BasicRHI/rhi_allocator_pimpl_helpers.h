//#pragma once
//// -----------------------------------------------------------------------------
//// rhi_allocator.cpp porting shim for replacing "m_Pimpl" pattern
//// -----------------------------------------------------------------------------
//#include "rhi_allocator.h"
//#include "rhi.h"
//
//namespace rhi::ma::detail
//{
//    // Cast helpers: replacement for old "obj->m_Pimpl".
//    inline AllocatorPimpl* GetPimpl(Allocator a) noexcept
//    {
//        return static_cast<AllocatorPimpl*>(a.impl);
//    }
//    inline const AllocatorPimpl* GetPimpl(const Allocator& a) noexcept
//    {
//        return static_cast<const AllocatorPimpl*>(a.impl);
//    }
//
//    inline PoolPimpl* GetPimpl(Pool p) noexcept
//    {
//        return static_cast<PoolPimpl*>(p.impl);
//    }
//    inline const PoolPimpl* GetPimpl(const Pool& p) noexcept
//    {
//        return static_cast<const PoolPimpl*>(p.impl);
//    }
//
//    inline VirtualBlockPimpl* GetPimpl(VirtualBlock b) noexcept
//    {
//        return static_cast<VirtualBlockPimpl*>(b.impl);
//    }
//    inline const VirtualBlockPimpl* GetPimpl(const VirtualBlock& b) noexcept
//    {
//        return static_cast<const VirtualBlockPimpl*>(b.impl);
//    }
//
//    inline DefragmentationContextPimpl* GetPimpl(DefragmentationContext c) noexcept
//    {
//        return static_cast<DefragmentationContextPimpl*>(c.impl);
//    }
//    inline const DefragmentationContextPimpl* GetPimpl(const DefragmentationContext& c) noexcept
//    {
//        return static_cast<const DefragmentationContextPimpl*>(c.impl);
//    }
//
//    // Optional: helpers to manufacture handles from impl pointers (keeps call sites clean).
//    extern const AllocatorVTable                gAllocatorVTable;
//    extern const PoolVTable                     gPoolVTable;
//    extern const VirtualBlockVTable             gVirtualBlockVTable;
//    extern const DefragmentationContextVTable   gDefragContextVTable;
//    extern const AllocationVTable               gAllocationVTable;
//
//    inline Allocator MakeHandle(AllocatorPimpl* impl) noexcept
//    {
//        Allocator a{};
//        a.impl = impl;
//        a.vt = &gAllocatorVTable;
//        return a;
//    }
//
//    inline Pool MakeHandle(PoolPimpl* impl) noexcept
//    {
//        Pool p{};
//        p.impl = impl;
//        p.vt = &gPoolVTable;
//        return p;
//    }
//
//    inline VirtualBlock MakeHandle(VirtualBlockPimpl* impl) noexcept
//    {
//        VirtualBlock b{};
//        b.impl = impl;
//        b.vt = &gVirtualBlockVTable;
//        return b;
//    }
//
//    inline DefragmentationContext MakeHandle(DefragmentationContextPimpl* impl) noexcept
//    {
//        DefragmentationContext c{};
//        c.impl = impl;
//        c.vt = &gDefragContextVTable;
//        return c;
//    }
//
//    // Allocation in your API is also a vtable-handle, so keep the same pattern.
//    inline Allocation MakeHandle(void* allocationImpl) noexcept
//    {
//        Allocation a{};
//        a.impl = allocationImpl;
//        a.vt = &gAllocationVTable;
//        return a;
//    }
//} // namespace rhi::ma::detail
