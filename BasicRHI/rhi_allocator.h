#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "rhi.h"

// Backend-agnostic GPU memory allocator API (D3D12MA-inspired) expressed in terms of rhi::* types.
// Declaration-only: concrete implementations live in backend modules.
//
// Design notes:
// - Uses the same “POD wrapper + function-pointer vtable” pattern as rhi::Resource, rhi::Heap, etc.
// - Provides optional RAII helpers (Unique<T>) for convenience.
// - No DX12 / DXGI / Windows headers are required.
// - Result codes use rhi::Result instead of HRESULT.
// - Names are UTF-8 const char* (instead of WCHAR/LPCWSTR).

namespace rhi::ma
{
    // ---------------- ABI versioning ----------------
    inline constexpr uint32_t RHI_MA_ALLOCATOR_ABI_MIN = 1;
    inline constexpr uint32_t RHI_MA_POOL_ABI_MIN = 1;
    inline constexpr uint32_t RHI_MA_ALLOCATION_ABI_MIN = 1;
    inline constexpr uint32_t RHI_MA_DEFRAG_ABI_MIN = 1;
    inline constexpr uint32_t RHI_MA_VBLOCK_ABI_MIN = 1;

    // ---------------- Convenience RAII wrapper ----------------
    template<class TObject>
    class Unique
    {
    public:
        Unique() = default;
        explicit Unique(TObject obj) noexcept : obj_(obj) {}
        ~Unique() { Reset(); }

        Unique(const Unique&) = delete;
        Unique& operator=(const Unique&) = delete;

        Unique(Unique&& o) noexcept : obj_(o.obj_) { o.obj_ = {}; }
        Unique& operator=(Unique&& o) noexcept
        {
            if (this != &o)
            {
                Reset();
                obj_ = o.obj_;
                o.obj_ = {};
            }
            return *this;
        }

        TObject* operator->() noexcept { return &obj_; }
        const TObject* operator->() const noexcept { return &obj_; }
        TObject& Get() noexcept { return obj_; }
        const TObject& Get() const noexcept { return obj_; }
        explicit operator bool() const noexcept { return static_cast<bool>(obj_); }

        void Reset() noexcept
        {
            if (static_cast<bool>(obj_))
                obj_.Destroy();
            obj_ = {};
        }

        TObject Release() noexcept
        {
            return std::exchange(obj_, {});
        }

    private:
        TObject obj_{};
    };

    // ---------------- Common types ----------------

    using AllocHandle = uint64_t;

    using AllocateFuncPtr = void* (*)(size_t size, size_t alignment, void* userData);
    using FreeFuncPtr = void  (*)(void* memory, void* userData);

    struct AllocationCallbacks
    {
        AllocateFuncPtr pAllocate = nullptr;
        FreeFuncPtr     pFree = nullptr;
        void* pUserData = nullptr;
    };

#define D3D12MA_CLASS_NO_COPY(className) \
    private: \
        className(const className&) = delete; \
        className(className&&) = delete; \
        className& operator=(const className&) = delete; \
        className& operator=(className&&) = delete;

    // ---------------- Statistics ----------------

    struct Statistics
    {
        uint32_t blockCount = 0;
        uint32_t allocationCount = 0;
        uint64_t blockBytes = 0;
        uint64_t allocationBytes = 0;
    };

    struct DetailedStatistics
    {
        Statistics stats{};
        uint32_t   unusedRangeCount = 0;
        uint64_t   allocationSizeMin = (std::numeric_limits<uint64_t>::max)();
        uint64_t   allocationSizeMax = 0;
        uint64_t   unusedRangeSizeMin = (std::numeric_limits<uint64_t>::max)();
        uint64_t   unusedRangeSizeMax = 0;
    };

    enum class MemorySegmentGroup : uint32_t
    {
        Local = 0,
        NonLocal = 1
    };

    struct TotalStatistics
    {
        // rhi::HeapType currently has 6 logical values (DeviceLocal, HostVisibleCoherent, HostVisibleCached, HostCached, GPUUpload, Custom)
        static constexpr uint32_t heapTypeCount = 6;
        DetailedStatistics heapType[heapTypeCount]{};

        DetailedStatistics memorySegmentGroup[2]{};
        DetailedStatistics total{};
    };

    struct Budget
    {
        Statistics stats{};
        uint64_t usageBytes = 0;
        uint64_t budgetBytes = 0;
    };

    // ---------------- Forward declarations ----------------
    class Allocator; struct AllocatorVTable;
    class Pool;      struct PoolVTable;
    class Allocation; struct AllocationVTable;
    class DefragmentationContext; struct DefragmentationContextVTable;
    class VirtualBlock; struct VirtualBlockVTable;

    // ---------------- Flags ----------------

    enum class AllocationFlags : uint32_t
    {
        None = 0,
        Committed = 0x1,
        NeverAllocate = 0x2,
        WithinBudget = 0x4,
        UpperAddress = 0x8,
        CanAlias = 0x10,

        StrategyMinMemory = 0x00010000,
        StrategyMinTime = 0x00020000,
        StrategyMinOffset = 0x00040000,

        StrategyBestFit = StrategyMinMemory,
        StrategyFirstFit = StrategyMinTime,

        StrategyMask = StrategyMinMemory | StrategyMinTime | StrategyMinOffset
    };
    inline AllocationFlags operator|(AllocationFlags a, AllocationFlags b) noexcept
    {
        return static_cast<AllocationFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline AllocationFlags& operator|=(AllocationFlags& a, AllocationFlags b) noexcept { a = a | b; return a; }

    enum class PoolFlags : uint32_t
    {
        None = 0,
        AlgorithmLinear = 0x1,
        MsaaTexturesAlwaysCommitted = 0x2,
        AlwaysCommitted = 0x4,

        AlgorithmMask = AlgorithmLinear
    };
    inline PoolFlags operator|(PoolFlags a, PoolFlags b) noexcept
    {
        return static_cast<PoolFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline PoolFlags& operator|=(PoolFlags& a, PoolFlags b) noexcept { a = a | b; return a; }

    enum class AllocatorFlags : uint32_t
    {
        None = 0,
        SingleThreaded = 0x1,
        AlwaysCommitted = 0x2,
        DefaultPoolsNotZeroed = 0x4,
        MsaaTexturesAlwaysCommitted = 0x8,
        DontPreferSmallBuffersCommitted = 0x10,
        DontUseTightAlignment = 0x20
    };
    inline AllocatorFlags operator|(AllocatorFlags a, AllocatorFlags b) noexcept
    {
        return static_cast<AllocatorFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline AllocatorFlags& operator|=(AllocatorFlags& a, AllocatorFlags b) noexcept { a = a | b; return a; }

    // ---------------- Descriptors ----------------

    struct AllocationDesc
    {
        AllocationFlags flags = AllocationFlags::None;

        // Ignored if customPool != nullptr.
        rhi::HeapType  heapType = rhi::HeapType::DeviceLocal;
        rhi::HeapFlags extraHeapFlags = rhi::HeapFlags::None;

        Pool* customPool = nullptr;

        void* privateData = nullptr;
    };

    struct PoolDesc
    {
        PoolFlags flags = PoolFlags::None;

        rhi::HeapType heapType = rhi::HeapType::DeviceLocal;
        rhi::HeapFlags heapFlags = rhi::HeapFlags::None;

        uint64_t blockSize = 0;
        uint32_t minBlockCount = 0;
        uint32_t maxBlockCount = 0;

        uint64_t minAllocationAlignment = 0;

        // Optional backend-specific fields (kept generic).
        void* protectedSession = nullptr;
        uint32_t residencyPriority = 0;

        const char* debugName = nullptr;
    };

    struct AllocatorDesc
    {
        AllocatorFlags flags = AllocatorFlags::None;

        // Device that allocator uses to create heaps/resources.
        // Lifetime must outlive the allocator.
        rhi::Device device{};

        uint64_t preferredBlockSize = 0;

        const AllocationCallbacks* allocationCallbacks = nullptr;

        // Optional backend-native adapter pointer (e.g. IDXGIAdapter*) — treated as opaque.
        void* nativeAdapter = nullptr;
    };

    // ---------------- Defragmentation ----------------

    enum class DefragmentationFlags : uint32_t
    {
        None = 0,
        AlgorithmFast = 0x1,
        AlgorithmBalanced = 0x2,
        AlgorithmFull = 0x4,

        AlgorithmMask = AlgorithmFast | AlgorithmBalanced | AlgorithmFull
    };
    inline DefragmentationFlags operator|(DefragmentationFlags a, DefragmentationFlags b) noexcept
    {
        return static_cast<DefragmentationFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline DefragmentationFlags& operator|=(DefragmentationFlags& a, DefragmentationFlags b) noexcept { a = a | b; return a; }

    struct DefragmentationDesc
    {
        DefragmentationFlags flags = DefragmentationFlags::None;
        uint64_t maxBytesPerPass = 0;
        uint32_t maxAllocationsPerPass = 0;
    };

    enum class DefragmentationMoveOperation : uint32_t
    {
        Copy = 0,
        Ignore = 1,
        Destroy = 2
    };

    struct DefragmentationStats
    {
        uint64_t bytesMoved = 0;
        uint64_t bytesFreed = 0;
        uint32_t allocationsMoved = 0;
        uint32_t heapsFreed = 0;
    };

    enum class DefragmentationPassResult : uint32_t
    {
        Finished, // no more moves possible
        HasMoves  // pass contains moves to execute
    };

    // ---------------- Virtual allocator ----------------

    struct VirtualAllocation
    {
        AllocHandle handle = 0; // 0 means invalid
    };

    enum class VirtualBlockFlags : uint32_t
    {
        None = 0,
        AlgorithmLinear = 0x1,

        AlgorithmMask = AlgorithmLinear
    };
    inline VirtualBlockFlags operator|(VirtualBlockFlags a, VirtualBlockFlags b) noexcept
    {
        return static_cast<VirtualBlockFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline VirtualBlockFlags& operator|=(VirtualBlockFlags& a, VirtualBlockFlags b) noexcept { a = a | b; return a; }

    struct VirtualBlockDesc
    {
        VirtualBlockFlags flags = VirtualBlockFlags::None;
        uint64_t size = 0;
        const AllocationCallbacks* allocationCallbacks = nullptr;
    };

    enum class VirtualAllocationFlags : uint32_t
    {
        None = 0,
        UpperAddress = 0x8,

        StrategyMinMemory = 0x00010000,
        StrategyMinTime = 0x00020000,
        StrategyMinOffset = 0x00040000,

        StrategyMask = StrategyMinMemory | StrategyMinTime | StrategyMinOffset
    };
    inline VirtualAllocationFlags operator|(VirtualAllocationFlags a, VirtualAllocationFlags b) noexcept
    {
        return static_cast<VirtualAllocationFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline VirtualAllocationFlags& operator|=(VirtualAllocationFlags& a, VirtualAllocationFlags b) noexcept { a = a | b; return a; }

    struct VirtualAllocationDesc
    {
        VirtualAllocationFlags flags = VirtualAllocationFlags::None;
        uint64_t size = 0;
        uint64_t alignment = 0; // 0 == 1
        void* privateData = nullptr;
    };

    struct VirtualAllocationInfo
    {
        uint64_t offset = 0;
        uint64_t size = 0;
        void* privateData = nullptr;
    };

    // ---------------- Recommended constants ----------------

    inline AllocatorFlags RecommendedAllocatorFlags =
        AllocatorFlags::DefaultPoolsNotZeroed |
        AllocatorFlags::MsaaTexturesAlwaysCommitted;

    inline rhi::HeapFlags RecommendedHeapFlags =
        rhi::HeapFlags::CreateNotZeroed;

    inline PoolFlags RecommendedPoolFlags =
        PoolFlags::MsaaTexturesAlwaysCommitted;

    // ---------------- Allocation wrapper ----------------

    struct AllocationVTable
    {
        void (*destroy)(Allocation*) noexcept;

        uint64_t(*getOffset)(const Allocation*) noexcept;
        uint64_t(*getAlignment)(const Allocation*) noexcept;
        uint64_t(*getSize)(const Allocation*) noexcept;

        rhi::HeapHandle(*getHeap)(const Allocation*) noexcept;
        rhi::ResourceHandle(*getResource)(const Allocation*) noexcept;
        void (*setResource)(Allocation*, rhi::ResourceHandle) noexcept;

        void (*setPrivateData)(Allocation*, void*) noexcept;
        void* (*getPrivateData)(const Allocation*) noexcept;

        void (*setName)(Allocation*, const char*) noexcept;
        const char* (*getName)(const Allocation*) noexcept;

		AllocHandle(*getAllocHandle)(const Allocation*) noexcept;

        uint32_t abi_version = 1;
    };

    class Allocation
    {
    public:
        void* impl{};
        const AllocationVTable* vt{};

        explicit constexpr operator bool() const noexcept
        {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_MA_ALLOCATION_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }

        void Destroy() noexcept { vt->destroy(this); }

        uint64_t GetOffset() const noexcept { return vt->getOffset(this); }
        uint64_t GetAlignment()const  noexcept { return vt->getAlignment(this); }
        uint64_t GetSize() const noexcept { return vt->getSize(this); }

        rhi::HeapHandle GetHeap() noexcept { return vt->getHeap(this); }
        rhi::ResourceHandle GetResource() noexcept { return vt->getResource(this); }
        void SetResource(rhi::ResourceHandle r) noexcept { vt->setResource(this, r); }

        void SetPrivateData(void* p) noexcept { vt->setPrivateData(this, p); }
        void* GetPrivateData() const noexcept { return vt->getPrivateData(this); }

        void SetName(const char* n) noexcept { vt->setName(this, n); }
        const char* GetName() const noexcept { return vt->getName(this); }

    private:
        friend class BlockMetadata_Linear;
        friend class JsonWriter;

        enum Type
        {
            TYPE_COMMITTED,
            TYPE_PLACED,
            TYPE_HEAP,
            TYPE_COUNT
        };

        struct PackedData
        {
        public:
            PackedData() :
                m_Type(0), m_ResourceDimension(0), m_ResourceFlags(0), m_TextureLayout(0) {
            }

            Type GetType() const { return (Type)m_Type; }
            ResourceType GetResourceDimension() const { return (ResourceType)m_ResourceDimension; }
            ResourceFlags GetResourceFlags() const { return (ResourceFlags)m_ResourceFlags; }
            ResourceLayout GetTextureLayout() const { return (ResourceLayout)m_TextureLayout; }

            void SetType(Type type);
            void SetResourceDimension(ResourceType resourceDimension);
            void SetResourceFlags(ResourceFlags resourceFlags);
            void SetTextureLayout(ResourceLayout textureLayout);

        private:
            UINT m_Type : 2;               // enum Type
            UINT m_ResourceDimension : 3;  // enum ResourceType
            UINT m_ResourceFlags : 24;     // flags ResourceFlags
            UINT m_TextureLayout : 9;      // enum ResourceLayout
        } m_PackedData;

		AllocHandle GetAllocHandle() const noexcept { return vt->getAllocHandle(this); }
    };

    struct DefragmentationMove
    {
        DefragmentationMoveOperation operation = DefragmentationMoveOperation::Copy;
        Allocation srcAllocation{};
        Allocation dstTmpAllocation{};
    };

    struct DefragmentationPassMoveInfo
    {
        uint32_t moveCount = 0;
        DefragmentationMove* moves = nullptr; // owned by allocator/context, valid until EndPass
    };

    using AllocationPtr = Unique<Allocation>;

    // ---------------- Pool wrapper ----------------

    struct PoolVTable
    {
        void (*destroy)(Pool*) noexcept;

        PoolDesc(*getDesc)(Pool*) noexcept;
        void (*getStatistics)(Pool*, Statistics*) noexcept;
        void (*calculateStatistics)(Pool*, DetailedStatistics*) noexcept;

        void (*setName)(Pool*, const char*) noexcept;
        const char* (*getName)(Pool*) noexcept;

        rhi::Result(*beginDefragmentation)(Pool*, const DefragmentationDesc&, DefragmentationContext* outCtx) noexcept;

        uint32_t abi_version = 1;
    };

    class Pool
    {
    public:
        void* impl{};
        const PoolVTable* vt{};

        explicit constexpr operator bool() const noexcept
        {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_MA_POOL_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }

        void Destroy() noexcept { vt->destroy(this); }

        PoolDesc GetDesc() noexcept { return vt->getDesc(this); }
        void GetStatistics(Statistics* s) noexcept { vt->getStatistics(this, s); }
        void CalculateStatistics(DetailedStatistics* s) noexcept { vt->calculateStatistics(this, s); }

        void SetName(const char* n) noexcept { vt->setName(this, n); }
        const char* GetName() noexcept { return vt->getName(this); }

        rhi::Result BeginDefragmentation(const DefragmentationDesc& d, DefragmentationContext* outCtx) noexcept
        {
            return vt->beginDefragmentation(this, d, outCtx);
        }
    };

    using PoolPtr = Unique<Pool>;

    // ---------------- Defragmentation context wrapper ----------------

    struct DefragmentationContextVTable
    {
        void (*destroy)(DefragmentationContext*) noexcept;

        rhi::Result(*beginPass)(DefragmentationContext*, DefragmentationPassMoveInfo* outInfo, DefragmentationPassResult* outResult) noexcept;
        rhi::Result(*endPass)(DefragmentationContext*, DefragmentationPassMoveInfo* inOutInfo, DefragmentationPassResult* outResult) noexcept;

        void (*getStats)(DefragmentationContext*, DefragmentationStats*) noexcept;

        uint32_t abi_version = 1;
    };

    class DefragmentationContext
    {
    public:
        void* impl{};
        const DefragmentationContextVTable* vt{};

        explicit constexpr operator bool() const noexcept
        {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_MA_DEFRAG_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }

        void Destroy() noexcept { vt->destroy(this); }

        rhi::Result BeginPass(DefragmentationPassMoveInfo* outInfo, DefragmentationPassResult* outResult) noexcept
        {
            return vt->beginPass(this, outInfo, outResult);
        }

        rhi::Result EndPass(DefragmentationPassMoveInfo* inOutInfo, DefragmentationPassResult* outResult) noexcept
        {
            return vt->endPass(this, inOutInfo, outResult);
        }

        void GetStats(DefragmentationStats* s) noexcept { vt->getStats(this, s); }
    };

    using DefragmentationContextPtr = Unique<DefragmentationContext>;

    // ---------------- Virtual block wrapper ----------------

    struct VirtualBlockVTable
    {
        void (*destroy)(VirtualBlock*) noexcept;

        bool (*isEmpty)(VirtualBlock*) noexcept;
        void (*getAllocationInfo)(VirtualBlock*, VirtualAllocation, VirtualAllocationInfo*) noexcept;

        rhi::Result(*allocate)(VirtualBlock*, const VirtualAllocationDesc&, VirtualAllocation* outAlloc, uint64_t* outOffset) noexcept;
        void (*freeAllocation)(VirtualBlock*, VirtualAllocation) noexcept;
        void (*clear)(VirtualBlock*) noexcept;

        void (*setAllocationPrivateData)(VirtualBlock*, VirtualAllocation, void*) noexcept;

        void (*getStatistics)(VirtualBlock*, Statistics*) noexcept;
        void (*calculateStatistics)(VirtualBlock*, DetailedStatistics*) noexcept;

        void (*buildStatsString)(VirtualBlock*, char** outStatsString) noexcept;
        void (*freeStatsString)(VirtualBlock*, char* statsString) noexcept;

        uint32_t abi_version = 1;
    };

    class VirtualBlock
    {
    public:
        void* impl{};
        const VirtualBlockVTable* vt{};

        explicit constexpr operator bool() const noexcept
        {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_MA_VBLOCK_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }

        void Destroy() noexcept { vt->destroy(this); }

        bool IsEmpty() noexcept { return vt->isEmpty(this); }
        void GetAllocationInfo(VirtualAllocation a, VirtualAllocationInfo* outInfo) noexcept
        {
            vt->getAllocationInfo(this, a, outInfo);
        }

        rhi::Result Allocate(const VirtualAllocationDesc& d, VirtualAllocation* outAlloc, uint64_t* outOffset = nullptr) noexcept
        {
            return vt->allocate(this, d, outAlloc, outOffset);
        }

        void FreeAllocation(VirtualAllocation a) noexcept { vt->freeAllocation(this, a); }
        void Clear() noexcept { vt->clear(this); }

        void SetAllocationPrivateData(VirtualAllocation a, void* p) noexcept
        {
            vt->setAllocationPrivateData(this, a, p);
        }

        void GetStatistics(Statistics* s) noexcept { vt->getStatistics(this, s); }
        void CalculateStatistics(DetailedStatistics* s) noexcept { vt->calculateStatistics(this, s); }

        void BuildStatsString(char** outJson) noexcept { vt->buildStatsString(this, outJson); }
        void FreeStatsString(char* json) noexcept { vt->freeStatsString(this, json); }
    };

    using VirtualBlockPtr = Unique<VirtualBlock>;

    // ---------------- Allocator wrapper ----------------

    struct AllocatorCaps
    {
        bool isUma = false;
        bool isCacheCoherentUma = false;
        bool isGpuUploadHeapSupported = false;
        bool isTightAlignmentSupported = false;
    };

    struct AllocatorVTable
    {
        void (*destroy)(Allocator*) noexcept;

        AllocatorCaps(*getCaps)(Allocator*) noexcept;
        uint64_t(*getMemoryCapacity)(Allocator*, MemorySegmentGroup) noexcept;

        rhi::Result(*createResource)(
            Allocator*,
            const AllocationDesc&,
            const rhi::ResourceDesc&,
            Allocation* outAllocation,
            rhi::ResourcePtr* outResource /*optional, can be null*/
            ) noexcept;

        rhi::Result(*allocateMemory)(
            Allocator*,
            const AllocationDesc&,
            const rhi::ResourceAllocationInfo&,
            Allocation* outAllocation
            ) noexcept;

        rhi::Result(*createAliasingResource)(
            Allocator*,
            const Allocation& existingAllocation,
            uint64_t allocationLocalOffset,
            const rhi::ResourceDesc&,
            rhi::ResourcePtr* outResource
            ) noexcept;

        rhi::Result(*createPool)(
            Allocator*,
            const PoolDesc&,
            Pool* outPool
            ) noexcept;

        void (*setCurrentFrameIndex)(Allocator*, uint32_t frameIndex) noexcept;

        void (*getBudget)(Allocator*, Budget* outLocal, Budget* outNonLocal) noexcept;
        void (*calculateStatistics)(Allocator*, TotalStatistics* outStats) noexcept;

        void (*buildStatsString)(Allocator*, char** outJson, bool detailedMap) noexcept;
        void (*freeStatsString)(Allocator*, char* json) noexcept;

        rhi::Result(*beginDefragmentation)(Allocator*, const DefragmentationDesc&, DefragmentationContext* outCtx) noexcept;

        uint32_t abi_version = 1;
    };

    class Allocator
    {
    public:
        void* impl{};
        const AllocatorVTable* vt{};

        explicit constexpr operator bool() const noexcept
        {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_MA_ALLOCATOR_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }

        void Destroy() noexcept { vt->destroy(this); }

        AllocatorCaps GetCaps() noexcept { return vt->getCaps(this); }
        uint64_t GetMemoryCapacity(MemorySegmentGroup g) noexcept { return vt->getMemoryCapacity(this, g); }

        rhi::Result CreateResource(const AllocationDesc& a, const rhi::ResourceDesc& rd, Allocation* outAlloc, rhi::ResourcePtr* outRes = nullptr) noexcept
        {
            return vt->createResource(this, a, rd, outAlloc, outRes);
        }

        rhi::Result AllocateMemory(const AllocationDesc& a, const rhi::ResourceAllocationInfo& info, Allocation* outAlloc) noexcept
        {
            return vt->allocateMemory(this, a, info, outAlloc);
        }

        rhi::Result CreateAliasingResource(const Allocation& existing, uint64_t localOffset, const rhi::ResourceDesc& rd, rhi::ResourcePtr* outRes) noexcept
        {
            return vt->createAliasingResource(this, existing, localOffset, rd, outRes);
        }

        rhi::Result CreatePool(const PoolDesc& pd, Pool* outPool) noexcept
        {
            return vt->createPool(this, pd, outPool);
        }

        void SetCurrentFrameIndex(uint32_t frameIndex) noexcept { vt->setCurrentFrameIndex(this, frameIndex); }

        void GetBudget(Budget* local, Budget* nonLocal) noexcept { vt->getBudget(this, local, nonLocal); }
        void CalculateStatistics(TotalStatistics* s) noexcept { vt->calculateStatistics(this, s); }

        void BuildStatsString(char** outJson, bool detailedMap) noexcept { vt->buildStatsString(this, outJson, detailedMap); }
        void FreeStatsString(char* json) noexcept { vt->freeStatsString(this, json); }

        rhi::Result BeginDefragmentation(const DefragmentationDesc& d, DefragmentationContext* outCtx) noexcept
        {
            return vt->beginDefragmentation(this, d, outCtx);
        }
    };

    using AllocatorPtr = Unique<Allocator>;

    // ---------------- Creation entry points ----------------

    // Creates a backend allocator instance using the provided Device and configuration.
    // Implemented by backends (D3D12/Vulkan) or a platform module.
    rhi::Result CreateAllocator(const AllocatorDesc* desc, Allocator* outAllocator) noexcept;

    // Creates a standalone virtual allocator block (no GPU memory). Implemented in shared module.
    rhi::Result CreateVirtualBlock(const VirtualBlockDesc* desc, VirtualBlock* outBlock) noexcept;

    // ---------------- Optional helper “C*” descriptors ----------------

#ifndef RHI_MA_NO_HELPERS

    struct CAllocationDesc : public AllocationDesc
    {
        CAllocationDesc() = default;
        explicit CAllocationDesc(const AllocationDesc& o) noexcept : AllocationDesc(o) {}

        explicit CAllocationDesc(Pool* pool,
            AllocationFlags f = AllocationFlags::None,
            void* priv = nullptr) noexcept
        {
            flags = f;
            heapType = (rhi::HeapType)0;
            extraHeapFlags = rhi::HeapFlags::None;
            customPool = pool;
            privateData = priv;
        }

        explicit CAllocationDesc(rhi::HeapType ht,
            AllocationFlags f = AllocationFlags::None,
            void* priv = nullptr,
            rhi::HeapFlags extra = RecommendedHeapFlags) noexcept
        {
            flags = f;
            heapType = ht;
            extraHeapFlags = extra;
            customPool = nullptr;
            privateData = priv;
        }
    };

    struct CPoolDesc : public PoolDesc
    {
        CPoolDesc() = default;
        explicit CPoolDesc(const PoolDesc& o) noexcept : PoolDesc(o) {}

        explicit CPoolDesc(rhi::HeapType ht,
            rhi::HeapFlags hf,
            PoolFlags f = RecommendedPoolFlags,
            uint64_t blockSz = 0,
            uint32_t minBlocks = 0,
            uint32_t maxBlocks = 0,
            uint64_t minAlign = 0) noexcept
        {
            flags = f;
            heapType = ht;
            heapFlags = hf;
            blockSize = blockSz;
            minBlockCount = minBlocks;
            maxBlockCount = maxBlocks;
            minAllocationAlignment = minAlign;
        }
    };

    struct CVirtualBlockDesc : public VirtualBlockDesc
    {
        CVirtualBlockDesc() = default;
        explicit CVirtualBlockDesc(const VirtualBlockDesc& o) noexcept : VirtualBlockDesc(o) {}

        explicit CVirtualBlockDesc(uint64_t sz,
            VirtualBlockFlags f = VirtualBlockFlags::None,
            const AllocationCallbacks* cb = nullptr) noexcept
        {
            flags = f;
            size = sz;
            allocationCallbacks = cb;
        }
    };

    struct CVirtualAllocationDesc : public VirtualAllocationDesc
    {
        CVirtualAllocationDesc() = default;
        explicit CVirtualAllocationDesc(const VirtualAllocationDesc& o) noexcept : VirtualAllocationDesc(o) {}

        explicit CVirtualAllocationDesc(uint64_t sz,
            uint64_t align = 0,
            VirtualAllocationFlags f = VirtualAllocationFlags::None,
            void* priv = nullptr) noexcept
        {
            flags = f;
            size = sz;
            alignment = align;
            privateData = priv;
        }
    };

#endif // RHI_MA_NO_HELPERS

} // namespace rhi::ma
