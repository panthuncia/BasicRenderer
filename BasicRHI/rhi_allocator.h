#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "rhi.h"

// Backend-agnostic GPU memory allocator API (D3D12MA-inspired) expressed in terms of rhi::* types.
// Design notes:
// - No DX12 / DXGI / Windows headers are required.
// - Result codes use rhi::Result instead of HRESULT.
// - Names are UTF-8 const char* (instead of WCHAR/LPCWSTR).
// - Some D3D12MA defines are left as-is: TODO rename them to RHI_MA_*

#ifndef D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
    /** \brief
    When defined to value other than 0, the library will try to use
    `D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT` or `D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT`
    for created textures when possible, which can save memory because some small textures
    may get their alignment 4 KB and their size a multiply of 4 KB instead of 64 KB.

    - `#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 0` -
      Disables small texture alignment.
    - `#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 1` (the default) -
      Enables conservative algorithm that will use small alignment only for some textures
      that are surely known to support it.
    - `#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 2` -
      Enables query for small alignment to D3D12 (based on Microsoft sample) which will
      enable small alignment for more textures, but will also generate D3D Debug Layer
      error #721 on call to `ID3D12Device::GetResourceAllocationInfo`, which you should just
      ignore.
    */
#define D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT 1
#endif

#ifndef D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS
    /// Set of flags recommended for use in D3D12MA::ALLOCATOR_DESC::Flags for optimal performance.
#define D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS (D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED | D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED)
#endif

#ifndef D3D12MA_RECOMMENDED_HEAP_FLAGS
#if D3D12MA_CREATE_NOT_ZEROED_AVAILABLE
#define D3D12MA_RECOMMENDED_HEAP_FLAGS (D3D12_HEAP_FLAG_CREATE_NOT_ZEROED)
#else
    /// Set of flags recommended for use in D3D12MA::POOL_DESC::HeapFlags for optimal performance.
#define D3D12MA_RECOMMENDED_HEAP_FLAGS (D3D12_HEAP_FLAG_NONE)
#endif
#endif

#ifndef D3D12MA_RECOMMENDED_POOL_FLAGS
    /// Set of flags recommended for use in D3D12MA::POOL_DESC::Flags for optimal performance.
#define D3D12MA_RECOMMENDED_POOL_FLAGS (D3D12MA::POOL_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED)
#endif


namespace rhi::ma
{

    // ---------------- Common types ----------------

    namespace detail {
        template<class T>
        struct Releaser {
            void operator()(T* p) const noexcept { if (p) p->ReleaseThis(); }
        };
    } // namespace detail

    template<class T, class Deleter>
    class Unique {
    public:
        Unique() = default;
        explicit Unique(T* p) noexcept : p_(p) {}
        ~Unique() { Reset(); }

        Unique(Unique&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
        Unique& operator=(Unique&& o) noexcept {
            if (this != &o) { Reset(); p_ = std::exchange(o.p_, nullptr); }
            return *this;
        }

        Unique(const Unique&) = delete;
        Unique& operator=(const Unique&) = delete;

        T* Get() const noexcept { return p_; }
        T* operator->() const noexcept { return p_; }
        explicit operator bool() const noexcept { return p_ != nullptr; }

        void Reset() noexcept {
            if (p_) Deleter{}(p_);
            p_ = nullptr;
        }

        T** Put() noexcept { Reset(); return &p_; }

    private:
        T* p_ = nullptr;
    };


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

    enum AllocationFlags : uint32_t
    {
        AllocationFlagNone = 0,
        AllocationFlagCommitted = 0x1,
        AllocationFlagNeverAllocate = 0x2,
        AllocationFlagWithinBudget = 0x4,
        AllocationFlagUpperAddress = 0x8,
        AllocationFlagCanAlias = 0x10,

        AllocationFlagStrategyMinMemory = 0x00010000,
        AllocationFlagStrategyMinTime = 0x00020000,
        AllocationFlagStrategyMinOffset = 0x00040000,

        AllocationFlagStrategyBestFit = AllocationFlagStrategyMinMemory,
        AllocationFlagStrategyFirstFit = AllocationFlagStrategyMinTime,

        AllocationFlagStrategyMask = AllocationFlagStrategyMinMemory | AllocationFlagStrategyMinTime | AllocationFlagStrategyMinOffset
    };
    inline AllocationFlags operator|(AllocationFlags a, AllocationFlags b) noexcept
    {
        return static_cast<AllocationFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline AllocationFlags& operator|=(AllocationFlags& a, AllocationFlags b) noexcept { a = a | b; return a; }

    inline AllocationFlags operator&(AllocationFlags a, AllocationFlags b) noexcept
    {
        return static_cast<AllocationFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

    enum PoolFlags : uint32_t
    {
        PoolFlagsNone = 0,
        PoolFlagsAlgorithmLinear = 0x1,
        PoolFlagsMsaaTexturesAlwaysCommitted = 0x2,
        PoolFlagsAlwaysCommitted = 0x4,

        PoolFlagsAlgorithmMask = PoolFlagsAlgorithmLinear
    };
    inline PoolFlags operator|(PoolFlags a, PoolFlags b) noexcept
    {
        return static_cast<PoolFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline PoolFlags& operator|=(PoolFlags& a, PoolFlags b) noexcept { a = a | b; return a; }

    inline PoolFlags operator&(PoolFlags a, PoolFlags b) noexcept
    {
        return static_cast<PoolFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

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

    inline AllocatorFlags operator&(AllocatorFlags a, AllocatorFlags b) noexcept
    {
        return static_cast<AllocatorFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // ---------------- Descriptors ----------------

    struct AllocationDesc
    {
        AllocationFlags flags = AllocationFlags::AllocationFlagNone;

        // Ignored if customPool != nullptr.
        rhi::HeapType  heapType = rhi::HeapType::DeviceLocal;
        rhi::HeapFlags extraHeapFlags = rhi::HeapFlags::None;

        Pool* customPool = nullptr;

        void* privateData = nullptr;
    };

    struct PoolDesc
    {
        PoolFlags flags = PoolFlagsNone;

        rhi::HeapType heapType = rhi::HeapType::DeviceLocal;
        rhi::HeapFlags heapFlags = rhi::HeapFlags::None;

        uint64_t blockSize = 0;
        uint32_t minBlockCount = 0;
        uint32_t maxBlockCount = 0;

        uint64_t minAllocationAlignment = 0;

        // Optional backend-specific fields (kept generic).
        void* protectedSession = nullptr;
        ResidencyPriority residencyPriority = ResidencyPriorityNormal;

        const char* debugName = nullptr;
    };

    struct AllocatorDesc
    {
        AllocatorFlags flags = AllocatorFlags::None;

        // Device that allocator uses to create heaps/resources.
        // Lifetime must outlive the allocator.
        Device device;

        uint64_t preferredBlockSize = 0;

        // Optional
        const AllocationCallbacks* allocationCallbacks = nullptr;

        //void* nativeAdapter = nullptr;
    };

    // ---------------- Defragmentation ----------------

    enum DefragmentationFlags : uint32_t
    {
        None = 0,
        DefragmentationFlagsAlgorithmFast = 0x1,
        DefragmentationFlagsAlgorithmBalanced = 0x2,
        DefragmentationFlagsAlgorithmFull = 0x4,

        DefragmentationFlagsAlgorithmMask = DefragmentationFlagsAlgorithmFast | DefragmentationFlagsAlgorithmBalanced | DefragmentationFlagsAlgorithmFull
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

    inline VirtualBlockFlags operator&(VirtualBlockFlags a, VirtualBlockFlags b) noexcept
    {
        return static_cast<VirtualBlockFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

    struct VirtualBlockDesc
    {
        VirtualBlockFlags flags = VirtualBlockFlags::None;
        uint64_t size = 0;
        const AllocationCallbacks* allocationCallbacks = nullptr;
    };

    enum VirtualAllocationFlags : uint32_t
    {
        VirtualAllocationFlagsNone = AllocationFlagNone,
        VirtualAllocationFlagsUpperAddress = AllocationFlagUpperAddress,

        VirtualAllocationFlagsStrategyMinMemory = AllocationFlagStrategyMinMemory,
        VirtualAllocationFlagsStrategyMinTime = AllocationFlagStrategyMinTime,
        VirtualAllocationFlagsStrategyMinOffset = AllocationFlagStrategyMinOffset,

        VirtualAllocationFlagsStrategyMask = AllocationFlagStrategyMask
    };
    inline VirtualAllocationFlags operator|(VirtualAllocationFlags a, VirtualAllocationFlags b) noexcept
    {
        return static_cast<VirtualAllocationFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline VirtualAllocationFlags& operator|=(VirtualAllocationFlags& a, VirtualAllocationFlags b) noexcept { a = a | b; return a; }

    struct VirtualAllocationDesc
    {
        VirtualAllocationFlags flags = VirtualAllocationFlagsNone;
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
        PoolFlagsMsaaTexturesAlwaysCommitted;


    /// \cond INTERNAL
    class DefragmentationContextPimpl;
    class AllocatorPimpl;
    class PoolPimpl;
    class NormalBlock;
    class BlockVector;
    class CommittedAllocationList;
    class JsonWriter;
    class VirtualBlockPimpl;
    /// \endcond


    class Allocation
    {
    public:

        uint64_t GetOffset() const;
        uint64_t GetAlignment()const noexcept { return m_alignment; }
        uint64_t GetSize() const noexcept { return m_size; }

        rhi::HeapHandle GetHeap() const noexcept;
        rhi::Resource& GetResource() noexcept { return m_resource.Get(); }
        void SetResource(rhi::ResourcePtr r);

        void SetPrivateData(void* p) noexcept { m_privateData = p; }
        void* GetPrivateData() const noexcept { return m_privateData; };

        void SetName(const char* n) noexcept;
        const char* GetName() const noexcept { return m_name; }
    protected:
        void ReleaseThis();
    private:
        friend class BlockMetadata_Linear;
        friend class JsonWriter;
        friend struct CommittedAllocationListItemTraits;
        friend class AllocatorPimpl;
        friend class AllocationObjectAllocator;
        template<typename T> friend class PoolAllocator;
        friend class BlockVector;
        friend class DefragmentationContextPimpl;
        template<class> friend struct rhi::ma::detail::Releaser;

        enum Type
        {
            TYPE_COMMITTED,
            TYPE_PLACED,
            TYPE_HEAP,
            TYPE_COUNT
        };

        union
        {
            struct
            {
                CommittedAllocationList* list;
                Allocation* prev;
                Allocation* next;
            } m_Committed;

            struct
            {
                AllocHandle allocHandle;
                NormalBlock* block;
            } m_Placed;

            struct
            {
                // Beginning must be compatible with m_Committed.
                CommittedAllocationList* list;
                Allocation* prev;
                Allocation* next;
                HeapPtr heap;
            } m_Heap;
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

        ResourcePtr m_resource; // Owning
        const char* m_name;
        AllocatorPimpl* m_allocator;
        uint64_t m_size;
        uint64_t m_alignment;
        void* m_privateData;

        Allocation(AllocatorPimpl* allocator, UINT64 size, UINT64 alignment);
        virtual ~Allocation() {};
        void InitCommitted(CommittedAllocationList* list);
        void InitPlaced(AllocHandle allocHandle, NormalBlock* block);
        void InitHeap(CommittedAllocationList* list, HeapPtr heap);
        void SwapBlockAllocation(Allocation* allocation);

        AllocHandle GetAllocHandle() const noexcept;
        NormalBlock* GetBlock();
        void SetResourcePointer(ResourcePtr resource, const ResourceDesc* pResourceDesc);
        void FreeName();
        D3D12MA_CLASS_NO_COPY(Allocation)
    };

    /// Single move of an allocation to be done for defragmentation.
    struct DefragmentationMove
    {
        /** \brief Operation to be performed on the allocation by DefragmentationContext::EndPass().
        Default value is #DEFRAGMENTATION_MOVE_OPERATION_COPY. You can modify it.
        */
        DefragmentationMoveOperation operation;
        /// %Allocation that should be moved.
        Allocation* pSrcAllocation;
        /** \brief Temporary allocation pointing to destination memory that will replace `pSrcAllocation`.

        Use it to retrieve new `ID3D12Heap` and offset to create new `ID3D12Resource` and then store it here via Allocation::SetResource().

        \warning Do not store this allocation in your data structures! It exists only temporarily, for the duration of the defragmentation pass,
        to be used for storing newly created resource. DefragmentationContext::EndPass() will destroy it and make `pSrcAllocation` point to this memory.
        */
        Allocation* pDstTmpAllocation;
    };

    /** \brief Parameters for incremental defragmentation steps.

    To be used with function DefragmentationContext::BeginPass().
    */
	struct DefragmentationPassMoveInfo
    {
        /// Number of elements in the `pMoves` array.
        UINT32 moveCount;
        /** \brief Array of moves to be performed by the user in the current defragmentation pass.

        Pointer to an array of `MoveCount` elements, owned by %D3D12MA, created in DefragmentationContext::BeginPass(), destroyed in DefragmentationContext::EndPass().

        For each element, you should:

        1. Create a new resource in the place pointed by `pMoves[i].pDstTmpAllocation->GetHeap()` + `pMoves[i].pDstTmpAllocation->GetOffset()`.
        2. Store new resource in `pMoves[i].pDstTmpAllocation` by using Allocation::SetResource(). It will later replace old resource from `pMoves[i].pSrcAllocation`.
        3. Copy data from the `pMoves[i].pSrcAllocation` e.g. using `D3D12GraphicsCommandList::CopyResource`.
        4. Make sure these commands finished executing on the GPU.

        Only then you can finish defragmentation pass by calling DefragmentationContext::EndPass().
        After this call, the allocation will point to the new place in memory.

        Alternatively, if you cannot move specific allocation,
        you can set DEFRAGMENTATION_MOVE::Operation to D3D12MA::DEFRAGMENTATION_MOVE_OPERATION_IGNORE.

        Alternatively, if you decide you want to completely remove the allocation,
        set DEFRAGMENTATION_MOVE::Operation to D3D12MA::DEFRAGMENTATION_MOVE_OPERATION_DESTROY.
        Then, after DefragmentationContext::EndPass() the allocation will be released.
        */
        DefragmentationMove* pMoves;
    };

    class Pool
    {
    public:
        PoolPimpl* m_Pimpl{};
        Pool(Allocator* allocator, const PoolDesc& desc);
        ~Pool();
        PoolDesc GetDesc() const noexcept;
        void GetStatistics(Statistics* s) noexcept;
        void CalculateStatistics(DetailedStatistics* s) noexcept;
        Result BeginDefragmentation(const DefragmentationDesc* pDesc, DefragmentationContext** ppContext) noexcept;
        void SetName(const char* n) noexcept;
        const char* GetName() const noexcept;
        void ReleaseThis();
    };

    class DefragmentationContext
    {
    public:
        DefragmentationContextPimpl* m_Pimpl{};
        DefragmentationContext(AllocatorPimpl* allocator,
            const DefragmentationDesc& desc,
            BlockVector* poolVector);
        ~DefragmentationContext();

        rhi::Result BeginPass(DefragmentationPassMoveInfo* outInfo) noexcept;
        rhi::Result EndPass(DefragmentationPassMoveInfo* inOutInfo) noexcept;

        void GetStats(DefragmentationStats* s) noexcept;
        void ReleaseThis();
        D3D12MA_CLASS_NO_COPY(DefragmentationContext)

    };

    class VirtualBlock
    {
    public:
        VirtualBlockPimpl* m_Pimpl{};

        VirtualBlock(const AllocationCallbacks& allocationCallbacks, const VirtualBlockDesc& desc);
        ~VirtualBlock();

        void GetAllocationInfo(VirtualAllocation a, VirtualAllocationInfo* outInfo) const noexcept;

        rhi::Result Allocate(const VirtualAllocationDesc* pDesc, VirtualAllocation* pAllocation, UINT64* pOffset) noexcept;

        void FreeAllocation(VirtualAllocation a) noexcept;
        void Clear() noexcept;

        void SetAllocationPrivateData(VirtualAllocation a, void* p) noexcept;

        void GetStatistics(Statistics* s) const noexcept;
        void CalculateStatistics(DetailedStatistics* s) const noexcept;

        void BuildStatsString(char** outJson) const noexcept;
        void FreeStatsString(char* json) const noexcept;
        BOOL IsEmpty() const;
        void ReleaseThis();

    };

    struct AllocatorCaps
    {
        bool isUma = false;
        bool isCacheCoherentUma = false;
        bool isGpuUploadHeapSupported = false;
        bool isTightAlignmentSupported = false;
    };

    using AllocationPtr = Unique<Allocation, detail::Releaser<Allocation>>;

    class Allocator
    {
    public:
        Allocator(const AllocationCallbacks& allocationCallbacks, const AllocatorDesc& desc);
        ~Allocator();

        bool IsUMA() const;
        bool IsCacheCoherentUMA() const;
        bool IsGPUUploadHeapSupported() const;
        bool IsTightAlignmentSupported() const;
        uint64_t GetMemoryCapacity(MemorySegmentGroup memorySegmentGroup) const;

        Result CreateResource(
            const AllocationDesc* pAllocDesc,
            const ResourceDesc* pResourceDesc,
            UINT32 NumCastableFormats,
            const Format* pCastableFormats,
            AllocationPtr& outAllocation) noexcept;

        rhi::Result AllocateMemory(const AllocationDesc& a, const rhi::ResourceAllocationInfo& info, Allocation* outAlloc) noexcept;

        Result CreateAliasingResource(
            Allocation* pAllocation,
            UINT64 AllocationLocalOffset,
            const ResourceDesc* pResourceDesc,
            UINT32 NumCastableFormats,
            const Format* pCastableFormats,
            ResourcePtr& out);

        rhi::Result CreatePool(
            const PoolDesc* pPoolDesc,
            Pool** ppPool) noexcept;

        void GetBudget(Budget* pLocalBudget, Budget* pNonLocalBudget) noexcept;
        void CalculateStatistics(TotalStatistics* s) noexcept;
        void BuildStatsString(char** outJson, bool detailedMap) const noexcept;
        void FreeStatsString(char* json) const noexcept;
        void BeginDefragmentation(const DefragmentationDesc* pDesc, DefragmentationContext** ppContext)noexcept;
        AllocatorPimpl* m_Pimpl{};

		void ReleaseThis(); // TODO: Make AllocatorPtr and make this private
    private:
        template<class> friend struct rhi::ma::detail::Releaser;

        void SetCurrentFrameIndex(uint32_t frameIndex) noexcept;
        D3D12MA_CLASS_NO_COPY(Allocator)
    };

    using AllocationPtr = Unique<Allocation, detail::Releaser<Allocation>>;
	
	// ---------------- Creation entry points ----------------

    // Creates an allocator instance using the provided configuration.
    rhi::Result CreateAllocator(const AllocatorDesc* desc, Allocator** outAllocator) noexcept;

    // Creates a standalone virtual allocator block (no GPU memory). Implemented in shared module.
    rhi::Result CreateVirtualBlock(const VirtualBlockDesc* desc, VirtualBlock* outBlock) noexcept;

    // ---------------- Optional helper "C*" descriptors ----------------

#ifndef RHI_MA_NO_HELPERS

    struct CAllocationDesc : public AllocationDesc
    {
        CAllocationDesc() = default;
        explicit CAllocationDesc(const AllocationDesc& o) noexcept : AllocationDesc(o) {}

        explicit CAllocationDesc(Pool* pool,
            AllocationFlags f = AllocationFlags::AllocationFlagNone,
            void* priv = nullptr) noexcept
        {
            flags = f;
            heapType = (rhi::HeapType)0;
            extraHeapFlags = rhi::HeapFlags::None;
            customPool = pool;
            privateData = priv;
        }

        explicit CAllocationDesc(rhi::HeapType ht,
            AllocationFlags f = AllocationFlags::AllocationFlagNone,
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
            VirtualAllocationFlags f = VirtualAllocationFlagsNone,
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
