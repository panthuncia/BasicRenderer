// Heavily modified version of D3D12MemAlloc.cpp: https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator/blob/master/src/D3D12MemAlloc.cpp

//
// Copyright (c) 2019-2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "rhi.h"
#include "rhi_allocator.h"
#include "rhi_allocator_config.h"

#include <combaseapi.h>
#include <mutex>
#include <algorithm>
#include <utility>
#include <cstdlib>
#include <cstdint>
#include <malloc.h> // for _aligned_malloc, _aligned_free
#include <bit>

#ifndef _WIN32
#include <shared_mutex>
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Configuration End
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#define	SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT	( 65536 ) // TODO: Validate for Vulkan
#define	SMALL_RESOURCE_PLACEMENT_ALIGNMENT	( 4096 )

namespace rhi::ma {
    static constexpr UINT HEAP_TYPE_COUNT = 5;
    static constexpr UINT STANDARD_HEAP_TYPE_COUNT = 4; // Only DEFAULT, UPLOAD, READBACK, GPU_UPLOAD.
    static constexpr UINT DEFAULT_POOL_MAX_COUNT = STANDARD_HEAP_TYPE_COUNT * 3;
    static const UINT NEW_BLOCK_SIZE_SHIFT_MAX = 3;
    // Minimum size of a free suballocation to register it in the free suballocation collection.
    static const UINT64 MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER = 16;

    static const char* const HeapTypeNames[] =
    {
        "DEFAULT",
        "UPLOAD",
        "READBACK",
        "CUSTOM",
        "GPU_UPLOAD",
    };
    static const char* const StandardHeapTypeNames[] =
    {
        "DEFAULT",
        "UPLOAD",
        "READBACK",
        "GPU_UPLOAD",
    };

    static const HeapFlags RESOURCE_CLASS_HEAP_FLAGS =
        HeapFlags::DenyBuffers | HeapFlags::DenyRtDsTextures | HeapFlags::DenyNonRtDsTextures;

    static const ResidencyPriority D3D12_RESIDENCY_PRIORITY_NONE = static_cast<ResidencyPriority>(0);
    static const HeapType D3D12_HEAP_TYPE_GPU_UPLOAD_COPY = HeapType::HostVisibleDeviceLocal;
    static const ResourceFlags D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT_COPY = ResourceFlags::RF_UseTightAlignment;

#ifndef _D3D12MA_ENUM_DECLARATIONS

    enum class ResourceClass
    {
        Unknown, Buffer, Non_RT_DS_Texture, RT_DS_Texture
    };

    enum SuballocationType
    {
        SUBALLOCATION_TYPE_FREE = 0,
        SUBALLOCATION_TYPE_ALLOCATION = 1,
    };

#endif // _D3D12MA_ENUM_DECLARATIONS

#ifndef _D3D12MA_FUNCTIONS

    static void* DefaultAllocate(size_t Size, size_t Alignment, void* /*pPrivateData*/)
    {
#ifdef _WIN32
        return _aligned_malloc(Size, Alignment);
#else
        return aligned_alloc(Alignment, Size);
#endif
    }
    static void DefaultFree(void* pMemory, void* /*pPrivateData*/)
    {
#ifdef _WIN32
        return _aligned_free(pMemory);
#else
        return free(pMemory);
#endif
    }

    static void* Malloc(const AllocationCallbacks& allocs, size_t size, size_t alignment)
    {
        void* const result = (*allocs.pAllocate)(size, alignment, allocs.pUserData);
        D3D12MA_ASSERT(result);
        return result;
    }
    static void Free(const AllocationCallbacks& allocs, void* memory)
    {
        (*allocs.pFree)(memory, allocs.pUserData);
    }

    template<typename T>
    static T* Allocate(const AllocationCallbacks& allocs)
    {
        return (T*)Malloc(allocs, sizeof(T), __alignof(T));
    }
    template<typename T>
    static T* AllocateArray(const AllocationCallbacks& allocs, size_t count)
    {
        return (T*)Malloc(allocs, sizeof(T) * count, __alignof(T));
    }

#define D3D12MA_NEW(allocs, type) new(rhi::ma::Allocate<type>(allocs))(type)
#define D3D12MA_NEW_ARRAY(allocs, type, count) new(rhi::ma::AllocateArray<type>((allocs), (count)))(type)

    template<typename T>
    void D3D12MA_DELETE(const AllocationCallbacks& allocs, T* memory)
    {
        if (memory)
        {
            memory->~T();
            Free(allocs, memory);
        }
    }
    template<typename T>
    void D3D12MA_DELETE_ARRAY(const AllocationCallbacks& allocs, T* memory, size_t count)
    {
        if (memory)
        {
            for (size_t i = count; i--; )
            {
                memory[i].~T();
            }
            Free(allocs, (void*)memory);
        }
    }

    static void SetupAllocationCallbacks(AllocationCallbacks& outAllocs, const AllocationCallbacks* allocationCallbacks)
    {
        if (allocationCallbacks)
        {
            outAllocs = *allocationCallbacks;
            D3D12MA_ASSERT(outAllocs.pAllocate != NULL && outAllocs.pFree != NULL);
        }
        else
        {
            outAllocs.pAllocate = &DefaultAllocate;
            outAllocs.pFree = &DefaultFree;
            outAllocs.pUserData = NULL;
        }
    }

#define SAFE_RELEASE(ptr)   do { if(ptr) { (ptr)->Release(); (ptr) = NULL; } } while(false)

#define D3D12MA_VALIDATE(cond) do { if(!(cond)) { \
    D3D12MA_ASSERT(0 && "Validation failed: " #cond); \
    return false; \
} } while(false)

    template<typename T>
    static T D3D12MA_MIN(const T& a, const T& b) { return a <= b ? a : b; }
    template<typename T>
    static T D3D12MA_MAX(const T& a, const T& b) { return a <= b ? b : a; }

    template<typename T>
    static void D3D12MA_SWAP(T& a, T& b) { T tmp = a; a = b; b = tmp; }

    template<typename T>
    static void MOVE_SWAP(T& a, T& b) { T tmp = std::move(a); a = std::move(b); b = std::move(tmp); }

    // Scans integer for index of first nonzero bit from the Least Significant Bit (LSB). If mask is 0 then returns UINT8_MAX
    static UINT8 BitScanLSB(UINT64 mask)
    {
#if defined(_MSC_VER) && defined(_WIN64)
        unsigned long pos;
        if (_BitScanForward64(&pos, mask))
            return static_cast<UINT8>(pos);
        return UINT8_MAX;
#elif D3D12MA_CPP20
        if (mask != 0)
            return static_cast<uint8_t>(std::countr_zero(mask));
        return UINT8_MAX;
#elif defined __GNUC__ || defined __clang__
        return static_cast<UINT8>(__builtin_ffsll(mask)) - 1U;
#else
        UINT8 pos = 0;
        UINT64 bit = 1;
        do
        {
            if (mask & bit)
                return pos;
            bit <<= 1;
        } while (pos++ < 63);
        return UINT8_MAX;
#endif
    }
    // Scans integer for index of first nonzero bit from the Least Significant Bit (LSB). If mask is 0 then returns UINT8_MAX
    static UINT8 BitScanLSB(UINT32 mask)
    {
#ifdef _MSC_VER
        unsigned long pos;
        if (_BitScanForward(&pos, mask))
            return static_cast<UINT8>(pos);
        return UINT8_MAX;
#elif D3D12MA_CPP20
        if (mask != 0)
            return static_cast<uint8_t>(std::countr_zero(mask));
        return UINT8_MAX;
#elif defined __GNUC__ || defined __clang__
        return static_cast<UINT8>(__builtin_ffs(mask)) - 1U;
#else
        UINT8 pos = 0;
        UINT32 bit = 1;
        do
        {
            if (mask & bit)
                return pos;
            bit <<= 1;
        } while (pos++ < 31);
        return UINT8_MAX;
#endif
    }

    // Scans integer for index of first nonzero bit from the Most Significant Bit (MSB). If mask is 0 then returns UINT8_MAX
    static UINT8 BitScanMSB(UINT64 mask)
    {
#if defined(_MSC_VER) && defined(_WIN64)
        unsigned long pos;
        if (_BitScanReverse64(&pos, mask))
            return static_cast<UINT8>(pos);
#elif D3D12MA_CPP20
        if (mask != 0)
            return 63 - static_cast<uint8_t>(std::countl_zero(mask));
#elif defined __GNUC__ || defined __clang__
        if (mask)
            return 63 - static_cast<UINT8>(__builtin_clzll(mask));
#else
        UINT8 pos = 63;
        UINT64 bit = 1ULL << 63;
        do
        {
            if (mask & bit)
                return pos;
            bit >>= 1;
        } while (pos-- > 0);
#endif
        return UINT8_MAX;
    }
    // Scans integer for index of first nonzero bit from the Most Significant Bit (MSB). If mask is 0 then returns UINT8_MAX
    static UINT8 BitScanMSB(UINT32 mask)
    {
#ifdef _MSC_VER
        unsigned long pos;
        if (_BitScanReverse(&pos, mask))
            return static_cast<UINT8>(pos);
#elif D3D12MA_CPP20
        if (mask != 0)
            return 31 - static_cast<uint8_t>(std::countl_zero(mask));
#elif defined __GNUC__ || defined __clang__
        if (mask)
            return 31 - static_cast<UINT8>(__builtin_clz(mask));
#else
        UINT8 pos = 31;
        UINT32 bit = 1UL << 31;
        do
        {
            if (mask & bit)
                return pos;
            bit >>= 1;
        } while (pos-- > 0);
#endif
        return UINT8_MAX;
    }

    /*
    Returns true if given number is a power of two.
    T must be unsigned integer number or signed integer but always nonnegative.
    For 0 returns true.
    */
    template <typename T>
    static bool IsPow2(T x) { return (x & (x - 1)) == 0; }

    // Aligns given value up to nearest multiply of align value. For example: AlignUp(11, 8) = 16.
    // Use types like UINT, uint64_t as T.
    template <typename T>
    static T AlignUp(T val, T alignment)
    {
        D3D12MA_HEAVY_ASSERT(IsPow2(alignment));
        return (val + alignment - 1) & ~(alignment - 1);
    }
    // Aligns given value down to nearest multiply of align value. For example: AlignUp(11, 8) = 8.
    // Use types like UINT, uint64_t as T.
    template <typename T>
    static T AlignDown(T val, T alignment)
    {
        D3D12MA_HEAVY_ASSERT(IsPow2(alignment));
        return val & ~(alignment - 1);
    }

    // Division with mathematical rounding to nearest number.
    template <typename T>
    static T RoundDiv(T x, T y) { return (x + (y / (T)2)) / y; }
    template <typename T>
    static T DivideRoundingUp(T x, T y) { return (x + y - 1) / y; }

    static WCHAR HexDigitToChar(UINT8 digit)
    {
        if (digit < 10)
            return L'0' + digit;
        else
            return L'A' + (digit - 10);
    }

    /*
    Performs binary search and returns iterator to first element that is greater or
    equal to `key`, according to comparison `cmp`.

    Cmp should return true if first argument is less than second argument.

    Returned value is the found element, if present in the collection or place where
    new element with value (key) should be inserted.
    */
    template <typename CmpLess, typename IterT, typename KeyT>
    static IterT BinaryFindFirstNotLess(IterT beg, IterT end, const KeyT& key, const CmpLess& cmp)
    {
        size_t down = 0, up = (end - beg);
        while (down < up)
        {
            const size_t mid = (down + up) / 2;
            if (cmp(*(beg + mid), key))
            {
                down = mid + 1;
            }
            else
            {
                up = mid;
            }
        }
        return beg + down;
    }

    /*
    Performs binary search and returns iterator to an element that is equal to `key`,
    according to comparison `cmp`.

    Cmp should return true if first argument is less than second argument.

    Returned value is the found element, if present in the collection or end if not
    found.
    */
    template<typename CmpLess, typename IterT, typename KeyT>
    static IterT BinaryFindSorted(const IterT& beg, const IterT& end, const KeyT& value, const CmpLess& cmp)
    {
        IterT it = BinaryFindFirstNotLess<CmpLess, IterT, KeyT>(beg, end, value, cmp);
        if (it == end ||
            (!cmp(*it, value) && !cmp(value, *it)))
        {
            return it;
        }
        return end;
    }

    static UINT StandardHeapTypeToIndex(HeapType type)
    {
        switch (type)
        {
        case HeapType::DeviceLocal:  return 0;
        case HeapType::Upload:   return 1;
        case HeapType::Readback: return 2;
        case HeapType::GPUUpload: return 3;
        default: D3D12MA_ASSERT(0); return UINT_MAX;
        }
    }

    static HeapType IndexToStandardHeapType(UINT heapTypeIndex)
    {
        switch (heapTypeIndex)
        {
        case 0: return HeapType::DeviceLocal;
        case 1: return HeapType::Upload;
        case 2: return HeapType::Readback;
        case 3: return HeapType::GPUUpload;
        default: D3D12MA_ASSERT(0); return HeapType::Custom;
        }
    }

    static UINT64 HeapFlagsToAlignment(HeapFlags flags, bool denyMsaaTextures)
    {
        /*
        Documentation of D3D12_HEAP_DESC structure says:

        - D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT   defined as 64KB.
        - D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT   defined as 4MB. An
        application must decide whether the heap will contain multi-sample
        anti-aliasing (MSAA), in which case, the application must choose [this flag].

        https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/ns-d3d12-d3d12_heap_desc
        */

        if (denyMsaaTextures)
            return DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

        const HeapFlags denyAllTexturesFlags =
            HeapFlags::DenyNonRtDsTextures | HeapFlags::DenyRtDsTextures;
        const bool canContainAnyTextures =
            (flags & denyAllTexturesFlags) != denyAllTexturesFlags;
        return canContainAnyTextures ?
            DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    }

    static ResourceClass HeapFlagsToResourceClass(HeapFlags heapFlags)
    {
        const bool allowBuffers = (heapFlags & HeapFlags::DenyBuffers) == 0;
        const bool allowRtDsTextures = (heapFlags & HeapFlags::DenyRtDsTextures) == 0;
        const bool allowNonRtDsTextures = (heapFlags & HeapFlags::DenyNonRtDsTextures) == 0;

        const uint8_t allowedGroupCount = (allowBuffers ? 1 : 0) + (allowRtDsTextures ? 1 : 0) + (allowNonRtDsTextures ? 1 : 0);
        if (allowedGroupCount != 1)
            return ResourceClass::Unknown;

        if (allowRtDsTextures)
            return ResourceClass::RT_DS_Texture;
        if (allowNonRtDsTextures)
            return ResourceClass::Non_RT_DS_Texture;
        return ResourceClass::Buffer;
    }

    static bool IsHeapTypeStandard(HeapType type)
    {
        return type == HeapType::DeviceLocal ||
            type == HeapType::Upload ||
            type == HeapType::Readback ||
            type == HeapType::GPUUpload;
    }

    static HeapProperties StandardHeapTypeToHeapProperties(HeapType type)
    {
        D3D12MA_ASSERT(IsHeapTypeStandard(type));
        HeapProperties result = {};
        result.type = type;
        return result;
    }

    static bool IsFormatCompressed(Format format)
    {
        switch (format)
        {
        case Format::BC1_Typeless:
        case Format::BC1_UNorm:
        case Format::BC1_UNorm_sRGB:
        case Format::BC2_Typeless:
        case Format::BC2_UNorm:
        case Format::BC2_UNorm_sRGB:
        case Format::BC3_Typeless:
        case Format::BC3_UNorm:
        case Format::BC3_UNorm_sRGB:
        case Format::BC4_Typeless:
        case Format::BC4_UNorm:
        case Format::BC4_SNorm:
        case Format::BC5_Typeless:
        case Format::BC5_UNorm:
        case Format::BC5_SNorm:
        case Format::BC6H_Typeless:
        case Format::BC6H_UF16:
        case Format::BC6H_SF16:
        case Format::BC7_Typeless:
        case Format::BC7_UNorm:
        case Format::BC7_UNorm_sRGB:
            return true;
        default:
            return false;
        }
    }

    // Only some formats are supported. For others it returns 0.
    static UINT GetBitsPerPixel(Format format)
    {
        switch (format)
        {
        case Format::R32G32B32A32_Typeless:
        case Format::R32G32B32A32_Float:
        case Format::R32G32B32A32_UInt:
        case Format::R32G32B32A32_SInt:
            return 128;
        case Format::R32G32B32_Typeless:
        case Format::R32G32B32_Float:
        case Format::R32G32B32_UInt:
        case Format::R32G32B32_SInt:
            return 96;
        case Format::R16G16B16A16_Typeless:
        case Format::R16G16B16A16_Float:
        case Format::R16G16B16A16_UNorm:
        case Format::R16G16B16A16_UInt:
        case Format::R16G16B16A16_SNorm:
        case Format::R16G16B16A16_SInt:
            return 64;
        case Format::R32G32_Typeless:
        case Format::R32G32_Float:
        case Format::R32G32_UInt:
        case Format::R32G32_SInt:
            return 64;
            //case Format::R32G8X24_Typeless: // TODO
            //case Format::D32_Float_S8X24_UInt:
            //case Format::R32_Float_X8X24_Typeless:
            //case Format::X32_Typeless_G8X24_UInt    :
            return 64;
        case Format::R10G10B10A2_Typeless:
        case Format::R10G10B10A2_UNorm:
        case Format::R10G10B10A2_UInt:
        case Format::R11G11B10_Float:
            return 32;
        case Format::R8G8B8A8_Typeless:
        case Format::R8G8B8A8_UNorm:
        case Format::R8G8B8A8_UNorm_sRGB:
        case Format::R8G8B8A8_UInt:
        case Format::R8G8B8A8_SNorm:
        case Format::R8G8B8A8_SInt:
            return 32;
        case Format::R16G16_Typeless:
        case Format::R16G16_Float:
        case Format::R16G16_UNorm:
        case Format::R16G16_UInt:
        case Format::R16G16_SNorm:
        case Format::R16G16_SInt:
            return 32;
        case Format::R32_Typeless:
        case Format::D32_Float:
        case Format::R32_Float:
        case Format::R32_UInt:
        case Format::R32_SInt:
            return 32;
            //case Format::R24G8_Typeless: // TODO
            //case Format::D24_UNorm_S8_UInt:
            //case Format::R24_UNorm_X8_Typeless:
            //case Format::X24_Typeless_G8_UInt:
            return 32;
        case Format::R8G8_Typeless:
        case Format::R8G8_UNorm:
        case Format::R8G8_UInt:
        case Format::R8G8_SNorm:
        case Format::R8G8_SInt:
            return 16;
        case Format::R16_Typeless:
        case Format::R16_Float:
            //case Format::D16_UNorm: // TODO
        case Format::R16_UNorm:
        case Format::R16_UInt:
        case Format::R16_SNorm:
        case Format::R16_SInt:
            return 16;
        case Format::R8_Typeless:
        case Format::R8_UNorm:
        case Format::R8_UInt:
        case Format::R8_SNorm:
        case Format::R8_SInt:
            //case Format::A8_UNorm: // TODO
            return 8;
        case Format::BC1_Typeless:
        case Format::BC1_UNorm:
        case Format::BC1_UNorm_sRGB:
            return 4;
        case Format::BC2_Typeless:
        case Format::BC2_UNorm:
        case Format::BC2_UNorm_sRGB:
            return 8;
        case Format::BC3_Typeless:
        case Format::BC3_UNorm:
        case Format::BC3_UNorm_sRGB:
            return 8;
        case Format::BC4_Typeless:
        case Format::BC4_UNorm:
        case Format::BC4_SNorm:
            return 4;
        case Format::BC5_Typeless:
        case Format::BC5_UNorm:
        case Format::BC5_SNorm:
            return 8;
        case Format::BC6H_Typeless:
        case Format::BC6H_UF16:
        case Format::BC6H_SF16:
            return 8;
        case Format::BC7_Typeless:
        case Format::BC7_UNorm:
        case Format::BC7_UNorm_sRGB:
            return 8;
        default:
            return 0;
        }
    }

    static ResourceClass ResourceDescToResourceClass(const ResourceDesc& resDesc)
    {
        if (resDesc.type == ResourceType::Buffer)
            return ResourceClass::Buffer;
        // Else: it's surely a texture.
        const bool isRenderTargetOrDepthStencil =
            (resDesc.resourceFlags & (ResourceFlags::RF_AllowRenderTarget | ResourceFlags::RF_AllowDepthStencil)) != 0;
        return isRenderTargetOrDepthStencil ? ResourceClass::RT_DS_Texture : ResourceClass::Non_RT_DS_Texture;
    }

    // This algorithm is overly conservative.
    static bool CanUseSmallAlignment(const ResourceDesc& resourceDesc)
    {
        if (resourceDesc.type != ResourceType::Texture2D)
            return false;
        if ((resourceDesc.resourceFlags & (ResourceFlags::RF_AllowRenderTarget | ResourceFlags::RF_AllowDepthStencil)) != 0)
            return false;
        if (resourceDesc.texture.sampleCount > 1)
            return false;
        if (resourceDesc.texture.depthOrLayers != 1)
            return false;

        UINT sizeX = (UINT)resourceDesc.texture.width;
        UINT sizeY = (UINT)resourceDesc.texture.height;
        UINT bitsPerPixel = GetBitsPerPixel(resourceDesc.texture.format);
        if (bitsPerPixel == 0)
            return false;

        if (IsFormatCompressed(resourceDesc.texture.format))
        {
            sizeX = DivideRoundingUp(sizeX, 4u);
            sizeY = DivideRoundingUp(sizeY, 4u);
            bitsPerPixel *= 16;
        }

        UINT tileSizeX = 0, tileSizeY = 0;
        switch (bitsPerPixel)
        {
        case   8: tileSizeX = 64; tileSizeY = 64; break;
        case  16: tileSizeX = 64; tileSizeY = 32; break;
        case  32: tileSizeX = 32; tileSizeY = 32; break;
        case  64: tileSizeX = 32; tileSizeY = 16; break;
        case 128: tileSizeX = 16; tileSizeY = 16; break;
        default: return false;
        }

        const UINT tileCount = DivideRoundingUp(sizeX, tileSizeX) * DivideRoundingUp(sizeY, tileSizeY);
        return tileCount <= 16;
    }

    static bool ValidateAllocateMemoryParameters(
        const AllocationDesc* pAllocDesc,
        const ResourceAllocationInfo* pAllocInfo,
        Allocation** ppAllocation)
    {
        return pAllocDesc &&
            pAllocInfo &&
            ppAllocation &&
            IsPow2(pAllocInfo->alignment) &&
            pAllocInfo->sizeInBytes > 0 &&
            pAllocInfo->sizeInBytes % 4 == 0;
    }

#endif // _D3D12MA_FUNCTIONS

#ifndef _D3D12MA_STATISTICS_FUNCTIONS

    static void ClearStatistics(Statistics& outStats)
    {
        outStats.blockCount = 0;
        outStats.allocationCount = 0;
        outStats.blockBytes = 0;
        outStats.allocationBytes = 0;
    }

    static void ClearDetailedStatistics(DetailedStatistics& outStats)
    {
        ClearStatistics(outStats.stats);
        outStats.unusedRangeCount = 0;
        outStats.allocationSizeMin = UINT64_MAX;
        outStats.allocationSizeMax = 0;
        outStats.unusedRangeSizeMin = UINT64_MAX;
        outStats.unusedRangeSizeMax = 0;
    }

    static void AddStatistics(Statistics& inoutStats, const Statistics& src)
    {
        inoutStats.blockCount += src.blockCount;
        inoutStats.allocationCount += src.allocationCount;
        inoutStats.blockBytes += src.blockBytes;
        inoutStats.allocationBytes += src.allocationBytes;
    }

    static void AddDetailedStatistics(DetailedStatistics& inoutStats, const DetailedStatistics& src)
    {
        AddStatistics(inoutStats.stats, src.stats);
        inoutStats.unusedRangeCount += src.unusedRangeCount;
        inoutStats.allocationSizeMin = D3D12MA_MIN(inoutStats.allocationSizeMin, src.allocationSizeMin);
        inoutStats.allocationSizeMax = D3D12MA_MAX(inoutStats.allocationSizeMax, src.allocationSizeMax);
        inoutStats.unusedRangeSizeMin = D3D12MA_MIN(inoutStats.unusedRangeSizeMin, src.unusedRangeSizeMin);
        inoutStats.unusedRangeSizeMax = D3D12MA_MAX(inoutStats.unusedRangeSizeMax, src.unusedRangeSizeMax);
    }

    static void AddDetailedStatisticsAllocation(DetailedStatistics& inoutStats, UINT64 size)
    {
        inoutStats.stats.allocationCount++;
        inoutStats.stats.allocationBytes += size;
        inoutStats.allocationSizeMin = D3D12MA_MIN(inoutStats.allocationSizeMin, size);
        inoutStats.allocationSizeMax = D3D12MA_MAX(inoutStats.allocationSizeMax, size);
    }

    static void AddDetailedStatisticsUnusedRange(DetailedStatistics& inoutStats, UINT64 size)
    {
        inoutStats.unusedRangeCount++;
        inoutStats.unusedRangeSizeMin = D3D12MA_MIN(inoutStats.unusedRangeSizeMin, size);
        inoutStats.unusedRangeSizeMax = D3D12MA_MAX(inoutStats.unusedRangeSizeMax, size);
    }

#endif // _D3D12MA_STATISTICS_FUNCTIONS

#ifndef _D3D12MA_MUTEX

#ifndef D3D12MA_MUTEX
    class Mutex
    {
    public:
        void Lock() { m_Mutex.lock(); }
        void Unlock() { m_Mutex.unlock(); }

    private:
        std::mutex m_Mutex;
    };
#define D3D12MA_MUTEX Mutex
#endif

#ifndef D3D12MA_RW_MUTEX
#ifdef _WIN32
    class RWMutex
    {
    public:
        RWMutex() { InitializeSRWLock(&m_Lock); }
        void LockRead() { AcquireSRWLockShared(&m_Lock); }
        void UnlockRead() { ReleaseSRWLockShared(&m_Lock); }
        void LockWrite() { AcquireSRWLockExclusive(&m_Lock); }
        void UnlockWrite() { ReleaseSRWLockExclusive(&m_Lock); }

    private:
        SRWLOCK m_Lock;
    };
#else // #ifdef _WIN32
    class RWMutex
    {
    public:
        RWMutex() {}
        void LockRead() { m_Mutex.lock_shared(); }
        void UnlockRead() { m_Mutex.unlock_shared(); }
        void LockWrite() { m_Mutex.lock(); }
        void UnlockWrite() { m_Mutex.unlock(); }

    private:
        std::shared_timed_mutex m_Mutex;
    };
#endif // #ifdef _WIN32
#define D3D12MA_RW_MUTEX RWMutex
#endif // #ifndef D3D12MA_RW_MUTEX

    // Helper RAII class to lock a mutex in constructor and unlock it in destructor (at the end of scope).
    struct MutexLock
    {
        D3D12MA_CLASS_NO_COPY(MutexLock);
    public:
        MutexLock(D3D12MA_MUTEX& mutex, bool useMutex = true) :
            m_pMutex(useMutex ? &mutex : NULL)
        {
            if (m_pMutex) m_pMutex->Lock();
        }
        ~MutexLock() { if (m_pMutex) m_pMutex->Unlock(); }

    private:
        D3D12MA_MUTEX* m_pMutex;
    };

    // Helper RAII class to lock a RW mutex in constructor and unlock it in destructor (at the end of scope), for reading.
    struct MutexLockRead
    {
        D3D12MA_CLASS_NO_COPY(MutexLockRead);
    public:
        MutexLockRead(D3D12MA_RW_MUTEX& mutex, bool useMutex)
            : m_pMutex(useMutex ? &mutex : NULL)
        {
            if (m_pMutex)
            {
                m_pMutex->LockRead();
            }
        }
        ~MutexLockRead() { if (m_pMutex) m_pMutex->UnlockRead(); }

    private:
        D3D12MA_RW_MUTEX* m_pMutex;
    };

    // Helper RAII class to lock a RW mutex in constructor and unlock it in destructor (at the end of scope), for writing.
    struct MutexLockWrite
    {
        D3D12MA_CLASS_NO_COPY(MutexLockWrite);
    public:
        MutexLockWrite(D3D12MA_RW_MUTEX& mutex, bool useMutex)
            : m_pMutex(useMutex ? &mutex : NULL)
        {
            if (m_pMutex) m_pMutex->LockWrite();
        }
        ~MutexLockWrite() { if (m_pMutex) m_pMutex->UnlockWrite(); }

    private:
        D3D12MA_RW_MUTEX* m_pMutex;
    };

#if D3D12MA_DEBUG_GLOBAL_MUTEX
    static D3D12MA_MUTEX g_DebugGlobalMutex;
#define D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK MutexLock debugGlobalMutexLock(g_DebugGlobalMutex, true);
#else
#define D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
#endif
#endif // _D3D12MA_MUTEX

#ifndef _D3D12MA_VECTOR
    /*
    Dynamically resizing continuous array. Class with interface similar to std::vector.
    T must be POD because constructors and destructors are not called and memcpy is
    used for these objects.
    */
    template<typename T>
    class Vector
    {
    public:
        using value_type = T;
        using iterator = T*;
        using const_iterator = const T*;

        // allocationCallbacks externally owned, must outlive this object.
        Vector(const AllocationCallbacks& allocationCallbacks);
        Vector(size_t count, const AllocationCallbacks& allocationCallbacks);
        Vector(const Vector<T>& src);
        ~Vector();

        const AllocationCallbacks& GetAllocs() const { return m_AllocationCallbacks; }
        bool empty() const { return m_Count == 0; }
        size_t size() const { return m_Count; }
        T* data() { return m_pArray; }
        const T* data() const { return m_pArray; }
        void clear(bool freeMemory = false) { resize(0, freeMemory); }

        iterator begin() { return m_pArray; }
        iterator end() { return m_pArray + m_Count; }
        const_iterator cbegin() const { return m_pArray; }
        const_iterator cend() const { return m_pArray + m_Count; }
        const_iterator begin() const { return cbegin(); }
        const_iterator end() const { return cend(); }

        void push_front(const T& src) { insert(0, src); }
        void push_back(const T& src);
        void pop_front();
        void pop_back();

        T& front();
        T& back();
        const T& front() const;
        const T& back() const;

        void reserve(size_t newCapacity, bool freeMemory = false);
        void resize(size_t newCount, bool freeMemory = false);
        void insert(size_t index, const T& src);
        void remove(size_t index);

        template<typename CmpLess>
        size_t InsertSorted(const T& value, const CmpLess& cmp);
        template<typename CmpLess>
        bool RemoveSorted(const T& value, const CmpLess& cmp);

        Vector& operator=(const Vector<T>& rhs);
        T& operator[](size_t index);
        const T& operator[](size_t index) const;

    private:
        const AllocationCallbacks& m_AllocationCallbacks;
        T* m_pArray;
        size_t m_Count;
        size_t m_Capacity;
    };

#ifndef _D3D12MA_VECTOR_FUNCTIONS
    template<typename T>
    Vector<T>::Vector(const AllocationCallbacks& allocationCallbacks)
        : m_AllocationCallbacks(allocationCallbacks),
        m_pArray(NULL),
        m_Count(0),
        m_Capacity(0) {
    }

    template<typename T>
    Vector<T>::Vector(size_t count, const AllocationCallbacks& allocationCallbacks)
        : m_AllocationCallbacks(allocationCallbacks),
        m_pArray(count ? AllocateArray<T>(allocationCallbacks, count) : NULL),
        m_Count(count),
        m_Capacity(count) {
    }

    template<typename T>
    Vector<T>::Vector(const Vector<T>& src)
        : m_AllocationCallbacks(src.m_AllocationCallbacks),
        m_pArray(src.m_Count ? AllocateArray<T>(src.m_AllocationCallbacks, src.m_Count) : NULL),
        m_Count(src.m_Count),
        m_Capacity(src.m_Count)
    {
        if (m_Count > 0)
        {
            memcpy(m_pArray, src.m_pArray, m_Count * sizeof(T));
        }
    }

    template<typename T>
    Vector<T>::~Vector()
    {
        Free(m_AllocationCallbacks, m_pArray);
    }

    template<typename T>
    void Vector<T>::push_back(const T& src)
    {
        const size_t newIndex = size();
        resize(newIndex + 1);
        m_pArray[newIndex] = src;
    }

    template<typename T>
    void Vector<T>::pop_front()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        remove(0);
    }

    template<typename T>
    void Vector<T>::pop_back()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        resize(size() - 1);
    }

    template<typename T>
    T& Vector<T>::front()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        return m_pArray[0];
    }

    template<typename T>
    T& Vector<T>::back()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        return m_pArray[m_Count - 1];
    }

    template<typename T>
    const T& Vector<T>::front() const
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        return m_pArray[0];
    }

    template<typename T>
    const T& Vector<T>::back() const
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        return m_pArray[m_Count - 1];
    }

    template<typename T>
    void Vector<T>::reserve(size_t newCapacity, bool freeMemory)
    {
        newCapacity = D3D12MA_MAX(newCapacity, m_Count);

        if ((newCapacity < m_Capacity) && !freeMemory)
        {
            newCapacity = m_Capacity;
        }

        if (newCapacity != m_Capacity)
        {
            T* const newArray = newCapacity ? AllocateArray<T>(m_AllocationCallbacks, newCapacity) : NULL;
            if (m_Count != 0)
            {
                memcpy(newArray, m_pArray, m_Count * sizeof(T));
            }
            Free(m_AllocationCallbacks, m_pArray);
            m_Capacity = newCapacity;
            m_pArray = newArray;
        }
    }

    template<typename T>
    void Vector<T>::resize(size_t newCount, bool freeMemory)
    {
        size_t newCapacity = m_Capacity;
        if (newCount > m_Capacity)
        {
            newCapacity = D3D12MA_MAX(newCount, D3D12MA_MAX(m_Capacity * 3 / 2, (size_t)8));
        }
        else if (freeMemory)
        {
            newCapacity = newCount;
        }

        if (newCapacity != m_Capacity)
        {
            T* const newArray = newCapacity ? AllocateArray<T>(m_AllocationCallbacks, newCapacity) : NULL;
            const size_t elementsToCopy = D3D12MA_MIN(m_Count, newCount);
            if (elementsToCopy != 0)
            {
                memcpy(newArray, m_pArray, elementsToCopy * sizeof(T));
            }
            Free(m_AllocationCallbacks, m_pArray);
            m_Capacity = newCapacity;
            m_pArray = newArray;
        }

        m_Count = newCount;
    }

    template<typename T>
    void Vector<T>::insert(size_t index, const T& src)
    {
        D3D12MA_HEAVY_ASSERT(index <= m_Count);
        const size_t oldCount = size();
        resize(oldCount + 1);
        if (index < oldCount)
        {
            memmove(m_pArray + (index + 1), m_pArray + index, (oldCount - index) * sizeof(T));
        }
        m_pArray[index] = src;
    }

    template<typename T>
    void Vector<T>::remove(size_t index)
    {
        D3D12MA_HEAVY_ASSERT(index < m_Count);
        const size_t oldCount = size();
        if (index < oldCount - 1)
        {
            memmove(m_pArray + index, m_pArray + (index + 1), (oldCount - index - 1) * sizeof(T));
        }
        resize(oldCount - 1);
    }

    template<typename T> template<typename CmpLess>
    size_t Vector<T>::InsertSorted(const T& value, const CmpLess& cmp)
    {
        const size_t indexToInsert = BinaryFindFirstNotLess<CmpLess, iterator, T>(
            m_pArray,
            m_pArray + m_Count,
            value,
            cmp) - m_pArray;
        insert(indexToInsert, value);
        return indexToInsert;
    }

    template<typename T> template<typename CmpLess>
    bool Vector<T>::RemoveSorted(const T& value, const CmpLess& cmp)
    {
        const iterator it = BinaryFindFirstNotLess(
            m_pArray,
            m_pArray + m_Count,
            value,
            cmp);
        if ((it != end()) && !cmp(*it, value) && !cmp(value, *it))
        {
            size_t indexToRemove = it - begin();
            remove(indexToRemove);
            return true;
        }
        return false;
    }

    template<typename T>
    Vector<T>& Vector<T>::operator=(const Vector<T>& rhs)
    {
        if (&rhs != this)
        {
            resize(rhs.m_Count);
            if (m_Count != 0)
            {
                memcpy(m_pArray, rhs.m_pArray, m_Count * sizeof(T));
            }
        }
        return *this;
    }

    template<typename T>
    T& Vector<T>::operator[](size_t index)
    {
        D3D12MA_HEAVY_ASSERT(index < m_Count);
        return m_pArray[index];
    }

    template<typename T>
    const T& Vector<T>::operator[](size_t index) const
    {
        D3D12MA_HEAVY_ASSERT(index < m_Count);
        return m_pArray[index];
    }
#endif // _D3D12MA_VECTOR_FUNCTIONS
#endif // _D3D12MA_VECTOR

#ifndef _D3D12MA_STRING_BUILDER_UTF8
    class StringBuilder
    {
    public:
        StringBuilder(const AllocationCallbacks& allocationCallbacks)
            : m_Data(allocationCallbacks)
        {
        }

        size_t GetLength() const { return m_Data.size(); }
        const char* GetData() const { return m_Data.data(); }

        void Add(char ch) { m_Data.push_back(ch); }

        // Adds a null-terminated UTF-8 string.
        void Add(const char* str);

        // Adds an arbitrary byte span (not required, but very useful).
        void Add(const char* data, size_t len);

        void AddNewLine() { Add('\n'); }

        void AddNumber(UINT num);
        void AddNumber(UINT64 num);

        // Pointer formatted as lowercase/uppercase hex digits (no 0x prefix),
        // matching your old behavior.
        void AddPointer(const void* ptr);

    private:
        static char HexDigitToChar(UINT8 v);

        Vector<char> m_Data;
    };

#ifndef _D3D12MA_STRING_BUILDER_UTF8_FUNCTIONS

    void StringBuilder::Add(const char* str)
    {
        if (!str) return;
        const size_t len = strlen(str);
        Add(str, len);
    }

    void StringBuilder::Add(const char* data, size_t len)
    {
        if (!data || len == 0) return;

        const size_t oldCount = m_Data.size();
        m_Data.resize(oldCount + len);
        memcpy(m_Data.data() + oldCount, data, len);
    }

    void StringBuilder::AddNumber(UINT num)
    {
        // Max UINT32 is 10 digits.
        char buf[11];
        buf[10] = '\0';
        char* p = &buf[10];

        do
        {
            *--p = char('0' + (num % 10));
            num /= 10;
        } while (num);

        Add(p);
    }

    void StringBuilder::AddNumber(UINT64 num)
    {
        // Max UINT64 is 20 digits.
        char buf[21];
        buf[20] = '\0';
        char* p = &buf[20];

        do
        {
            *--p = char('0' + (num % 10));
            num /= 10;
        } while (num);

        Add(p);
    }

    char StringBuilder::HexDigitToChar(UINT8 v)
    {
        v &= 0xF;
        return (v < 10) ? char('0' + v) : char('A' + (v - 10));
    }

    void StringBuilder::AddPointer(const void* ptr)
    {
        // Enough for 64-bit pointer in hex (16 digits) + '\0' (extra slack kept from original).
        char buf[21];
        uintptr_t num = (uintptr_t)ptr;

        buf[20] = '\0';
        char* p = &buf[20];

        do
        {
            *--p = HexDigitToChar((UINT8)(num & 0xF));
            num >>= 4;
        } while (num);

        Add(p);
    }

#endif // _D3D12MA_STRING_BUILDER_UTF8_FUNCTIONS
#endif // _D3D12MA_STRING_BUILDER_UTF8


    inline std::wstring s2ws(const std::string& s) {
        int buffSize = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
        std::wstring ws(buffSize, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), buffSize);
        return ws;
    }

#ifndef _D3D12MA_JSON_WRITER_UTF8
    /*
    Allows to conveniently build a correct JSON document to be written to the
    StringBuilder passed to the constructor.

    This UTF-8 version:
    - Accepts UTF-8 input strings (const char*).
    - Emits UTF-8 JSON.
    - Escapes JSON control characters and quotes/backslashes.
    - Leaves valid non-ASCII UTF-8 bytes as-is (JSON is UTF-8 by default).
    */
    class JsonWriter
    {
    public:
        JsonWriter(const AllocationCallbacks& allocationCallbacks, StringBuilder& stringBuilder);
        ~JsonWriter();

        void BeginObject(bool singleLine = false);
        void EndObject();

        void BeginArray(bool singleLine = false);
        void EndArray();

        void WriteString(const char* pStr);

        void BeginString(const char* pStr = NULL);
        void ContinueString(const char* pStr);
        void ContinueString(UINT num);
        void ContinueString(UINT64 num);
        void ContinueString_Pointer(const void* ptr);
        void EndString(const char* pStr = NULL);

        void WriteNumber(UINT num);
        void WriteNumber(UINT64 num);
        void WriteBool(bool b);
        void WriteNull();

        void AddAllocationToObject(const Allocation& alloc);
        void AddDetailedStatisticsInfoObject(const DetailedStatistics& stats);

    private:
        static const char* const INDENT;

        enum CollectionType
        {
            COLLECTION_TYPE_OBJECT,
            COLLECTION_TYPE_ARRAY,
        };
        struct StackItem
        {
            CollectionType type;
            UINT valueCount;
            bool singleLineMode;
        };

        StringBuilder& m_SB;
        Vector<StackItem> m_Stack;
        bool m_InsideString;

        void BeginValue(bool isString);
        void WriteIndent(bool oneLess = false);

        // UTF-8 helpers
        static bool DecodeUtf8CodePoint(const unsigned char* s, size_t* outLen, uint32_t* outCp);
        void AppendEscapedUtf8(const char* s);
    };

#ifndef _D3D12MA_JSON_WRITER_UTF8_FUNCTIONS
    const char* const JsonWriter::INDENT = "  ";

    JsonWriter::JsonWriter(const AllocationCallbacks& allocationCallbacks, StringBuilder& stringBuilder)
        : m_SB(stringBuilder),
        m_Stack(allocationCallbacks),
        m_InsideString(false)
    {
    }

    JsonWriter::~JsonWriter()
    {
        D3D12MA_ASSERT(!m_InsideString);
        D3D12MA_ASSERT(m_Stack.empty());
    }

    void JsonWriter::BeginObject(bool singleLine)
    {
        D3D12MA_ASSERT(!m_InsideString);

        BeginValue(false);
        m_SB.Add('{');

        StackItem stackItem;
        stackItem.type = COLLECTION_TYPE_OBJECT;
        stackItem.valueCount = 0;
        stackItem.singleLineMode = singleLine;
        m_Stack.push_back(stackItem);
    }

    void JsonWriter::EndObject()
    {
        D3D12MA_ASSERT(!m_InsideString);
        D3D12MA_ASSERT(!m_Stack.empty() && m_Stack.back().type == COLLECTION_TYPE_OBJECT);
        D3D12MA_ASSERT(m_Stack.back().valueCount % 2 == 0);

        WriteIndent(true);
        m_SB.Add('}');

        m_Stack.pop_back();
    }

    void JsonWriter::BeginArray(bool singleLine)
    {
        D3D12MA_ASSERT(!m_InsideString);

        BeginValue(false);
        m_SB.Add('[');

        StackItem stackItem;
        stackItem.type = COLLECTION_TYPE_ARRAY;
        stackItem.valueCount = 0;
        stackItem.singleLineMode = singleLine;
        m_Stack.push_back(stackItem);
    }

    void JsonWriter::EndArray()
    {
        D3D12MA_ASSERT(!m_InsideString);
        D3D12MA_ASSERT(!m_Stack.empty() && m_Stack.back().type == COLLECTION_TYPE_ARRAY);

        WriteIndent(true);
        m_SB.Add(']');

        m_Stack.pop_back();
    }

    void JsonWriter::WriteString(const char* pStr)
    {
        BeginString(pStr);
        EndString();
    }

    void JsonWriter::BeginString(const char* pStr)
    {
        D3D12MA_ASSERT(!m_InsideString);

        BeginValue(true);
        m_InsideString = true;
        m_SB.Add('"');

        if (pStr != NULL)
            ContinueString(pStr);
    }

    void JsonWriter::ContinueString(const char* pStr)
    {
        D3D12MA_ASSERT(m_InsideString);
        D3D12MA_ASSERT(pStr);

        AppendEscapedUtf8(pStr);
    }

    void JsonWriter::ContinueString(UINT num)
    {
        D3D12MA_ASSERT(m_InsideString);
        m_SB.AddNumber(num);
    }

    void JsonWriter::ContinueString(UINT64 num)
    {
        D3D12MA_ASSERT(m_InsideString);
        m_SB.AddNumber(num);
    }

    void JsonWriter::ContinueString_Pointer(const void* ptr)
    {
        D3D12MA_ASSERT(m_InsideString);
        m_SB.AddPointer(ptr);
    }

    void JsonWriter::EndString(const char* pStr)
    {
        D3D12MA_ASSERT(m_InsideString);

        if (pStr)
            ContinueString(pStr);

        m_SB.Add('"');
        m_InsideString = false;
    }

    void JsonWriter::WriteNumber(UINT num)
    {
        D3D12MA_ASSERT(!m_InsideString);
        BeginValue(false);
        m_SB.AddNumber(num);
    }

    void JsonWriter::WriteNumber(UINT64 num)
    {
        D3D12MA_ASSERT(!m_InsideString);
        BeginValue(false);
        m_SB.AddNumber(num);
    }

    void JsonWriter::WriteBool(bool b)
    {
        D3D12MA_ASSERT(!m_InsideString);
        BeginValue(false);
        m_SB.Add(b ? "true" : "false");
    }

    void JsonWriter::WriteNull()
    {
        D3D12MA_ASSERT(!m_InsideString);
        BeginValue(false);
        m_SB.Add("null");
    }

    void JsonWriter::AddAllocationToObject(const Allocation& alloc)
    {
        WriteString("Type");
        switch (alloc.m_PackedData.GetResourceDimension()) {
        case ResourceType::Unknown:   WriteString("UNKNOWN");  break;
        case ResourceType::Buffer:    WriteString("BUFFER");   break;
        case ResourceType::Texture1D: WriteString("TEXTURE1D"); break;
        case ResourceType::Texture2D: WriteString("TEXTURE2D"); break;
        case ResourceType::Texture3D: WriteString("TEXTURE3D"); break;
        default: D3D12MA_ASSERT(0); break;
        }

        WriteString("Size");
        WriteNumber(alloc.GetSize());

        WriteString("Usage");
        WriteNumber((UINT)alloc.m_PackedData.GetResourceFlags());

        void* privateData = alloc.GetPrivateData();
        if (privateData)
        {
            WriteString("CustomData");
            BeginString();
            ContinueString_Pointer(privateData);
            EndString();
        }

        // Assuming alloc.GetName() is UTF-8 already (std::string).
        const std::string& nameStr = alloc.GetName();
        if (!nameStr.empty())
        {
            WriteString("Name");
            WriteString(nameStr.c_str());
        }

        WriteString("Layout");
        WriteNumber((UINT)alloc.m_PackedData.GetTextureLayout());
    }

    void JsonWriter::AddDetailedStatisticsInfoObject(const DetailedStatistics& stats)
    {
        BeginObject();

        WriteString("BlockCount");        WriteNumber(stats.stats.blockCount);
        WriteString("BlockBytes");        WriteNumber(stats.stats.blockBytes);
        WriteString("AllocationCount");   WriteNumber(stats.stats.allocationCount);
        WriteString("AllocationBytes");   WriteNumber(stats.stats.allocationBytes);
        WriteString("UnusedRangeCount");  WriteNumber(stats.unusedRangeCount);

        if (stats.stats.allocationCount > 1)
        {
            WriteString("AllocationSizeMin"); WriteNumber(stats.allocationSizeMin);
            WriteString("AllocationSizeMax"); WriteNumber(stats.allocationSizeMax);
        }
        if (stats.unusedRangeCount > 1)
        {
            WriteString("UnusedRangeSizeMin"); WriteNumber(stats.unusedRangeSizeMin);
            WriteString("UnusedRangeSizeMax"); WriteNumber(stats.unusedRangeSizeMax);
        }

        EndObject();
    }

    void JsonWriter::BeginValue(bool isString)
    {
        if (!m_Stack.empty())
        {
            StackItem& currItem = m_Stack.back();
            if (currItem.type == COLLECTION_TYPE_OBJECT && currItem.valueCount % 2 == 0)
            {
                D3D12MA_ASSERT(isString); // object keys must be strings
            }

            if (currItem.type == COLLECTION_TYPE_OBJECT && currItem.valueCount % 2 == 1)
            {
                m_SB.Add(':'); m_SB.Add(' ');
            }
            else if (currItem.valueCount > 0)
            {
                m_SB.Add(','); m_SB.Add(' ');
                WriteIndent();
            }
            else
            {
                WriteIndent();
            }
            ++currItem.valueCount;
        }
    }

    void JsonWriter::WriteIndent(bool oneLess)
    {
        if (!m_Stack.empty() && !m_Stack.back().singleLineMode)
        {
            m_SB.AddNewLine();

            size_t count = m_Stack.size();
            if (count > 0 && oneLess)
                --count;

            for (size_t i = 0; i < count; ++i)
                m_SB.Add(INDENT);
        }
    }

    // ---- UTF-8 escaping ----
    //
    // We only *need* to escape: control chars (< 0x20), backslash, quote.
    // We also escape U+2028/U+2029 for JS-safety (optional but common).
    //
    // For invalid UTF-8 sequences, this implementation:
    // - asserts in debug
    // - emits \uFFFD replacement in release-like behavior.

    bool JsonWriter::DecodeUtf8CodePoint(const unsigned char* s, size_t* outLen, uint32_t* outCp)
    {
        // Returns true if valid; outLen is 1..4.
        const unsigned char b0 = s[0];
        if (b0 < 0x80)
        {
            *outLen = 1;
            *outCp = b0;
            return true;
        }

        auto isCont = [](unsigned char b) { return (b & 0xC0) == 0x80; };

        if ((b0 & 0xE0) == 0xC0) // 2 bytes
        {
            const unsigned char b1 = s[1];
            if (!isCont(b1)) return false;
            uint32_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
            if (cp < 0x80) return false; // overlong
            *outLen = 2; *outCp = cp; return true;
        }
        if ((b0 & 0xF0) == 0xE0) // 3 bytes
        {
            const unsigned char b1 = s[1], b2 = s[2];
            if (!isCont(b1) || !isCont(b2)) return false;
            uint32_t cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            if (cp < 0x800) return false;                 // overlong
            if (0xD800 <= cp && cp <= 0xDFFF) return false; // UTF-16 surrogate range invalid in UTF-8
            *outLen = 3; *outCp = cp; return true;
        }
        if ((b0 & 0xF8) == 0xF0) // 4 bytes
        {
            const unsigned char b1 = s[1], b2 = s[2], b3 = s[3];
            if (!isCont(b1) || !isCont(b2) || !isCont(b3)) return false;
            uint32_t cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            if (cp < 0x10000) return false; // overlong
            if (cp > 0x10FFFF) return false;
            *outLen = 4; *outCp = cp; return true;
        }

        return false;
    }

    void JsonWriter::AppendEscapedUtf8(const char* s)
    {
        const unsigned char* p = (const unsigned char*)s;

        while (*p)
        {
            const unsigned char b = *p;

            // Fast-path ASCII
            if (b < 0x80)
            {
                switch (b)
                {
                case '\"': m_SB.Add('\\'); m_SB.Add('\"'); break;
                case '\\': m_SB.Add('\\'); m_SB.Add('\\'); break;
                case '/':  m_SB.Add('\\'); m_SB.Add('/');  break;
                case '\b': m_SB.Add('\\'); m_SB.Add('b');  break;
                case '\f': m_SB.Add('\\'); m_SB.Add('f');  break;
                case '\n': m_SB.Add('\\'); m_SB.Add('n');  break;
                case '\r': m_SB.Add('\\'); m_SB.Add('r');  break;
                case '\t': m_SB.Add('\\'); m_SB.Add('t');  break;
                default:
                    if (b < 0x20)
                    {
                        // \u00XX
                        static const char hex[] = "0123456789ABCDEF";
                        m_SB.Add('\\'); m_SB.Add('u'); m_SB.Add('0'); m_SB.Add('0');
                        m_SB.Add(hex[(b >> 4) & 0xF]);
                        m_SB.Add(hex[b & 0xF]);
                    }
                    else
                    {
                        m_SB.Add((char)b);
                    }
                    break;
                }
                ++p;
                continue;
            }

            // Non-ASCII: validate and copy UTF-8 sequence as-is (or escape some code points)
            size_t len = 0;
            uint32_t cp = 0;
            if (!DecodeUtf8CodePoint(p, &len, &cp))
            {
                D3D12MA_ASSERT(false && "Invalid UTF-8 in JsonWriter::ContinueString");
                // Emit U+FFFD replacement: \uFFFD
                m_SB.Add('\\'); m_SB.Add('u'); m_SB.Add('F'); m_SB.Add('F'); m_SB.Add('F'); m_SB.Add('D');
                ++p; // advance one byte to avoid infinite loop
                continue;
            }

            // Optional JS-safety: escape U+2028 and U+2029
            if (cp == 0x2028 || cp == 0x2029)
            {
                m_SB.Add('\\'); m_SB.Add('u'); m_SB.Add('2'); m_SB.Add('0'); m_SB.Add('2');
                m_SB.Add(cp == 0x2028 ? '8' : '9');
            }
            else
            {
                // Copy original bytes
                for (size_t i = 0; i < len; ++i)
                    m_SB.Add((char)p[i]);
            }

            p += len;
        }
    }
#endif // _D3D12MA_JSON_WRITER_UTF8_FUNCTIONS
#endif // _D3D12MA_JSON_WRITER_UTF8

#ifndef _D3D12MA_POOL_ALLOCATOR
    /*
    Allocator for objects of type T using a list of arrays (pools) to speed up
    allocation. Number of elements that can be allocated is not bounded because
    allocator can create multiple blocks.
    T should be POD because constructor and destructor is not called in Alloc or
    Free.
    */
    template<typename T>
    class PoolAllocator
    {
        D3D12MA_CLASS_NO_COPY(PoolAllocator)
    public:
        // allocationCallbacks externally owned, must outlive this object.
        PoolAllocator(const AllocationCallbacks& allocationCallbacks, UINT firstBlockCapacity);
        ~PoolAllocator() { Clear(); }

        void Clear();
        template<typename... Types>
        T* Alloc(Types... args);
        void Free(T* ptr);

    private:
        union Item
        {
            UINT NextFreeIndex; // UINT32_MAX means end of list.
            alignas(T) char Value[sizeof(T)];
        };

        struct ItemBlock
        {
            Item* pItems;
            UINT Capacity;
            UINT FirstFreeIndex;
        };

        const AllocationCallbacks& m_AllocationCallbacks;
        const UINT m_FirstBlockCapacity;
        Vector<ItemBlock> m_ItemBlocks;

        ItemBlock& CreateNewBlock();
    };

#ifndef _D3D12MA_POOL_ALLOCATOR_FUNCTIONS
    template<typename T>
    PoolAllocator<T>::PoolAllocator(const AllocationCallbacks& allocationCallbacks, UINT firstBlockCapacity)
        : m_AllocationCallbacks(allocationCallbacks),
        m_FirstBlockCapacity(firstBlockCapacity),
        m_ItemBlocks(allocationCallbacks)
    {
        D3D12MA_ASSERT(m_FirstBlockCapacity > 1);
    }

    template<typename T>
    void PoolAllocator<T>::Clear()
    {
        for (size_t i = m_ItemBlocks.size(); i--; )
        {
            D3D12MA_DELETE_ARRAY(m_AllocationCallbacks, m_ItemBlocks[i].pItems, m_ItemBlocks[i].Capacity);
        }
        m_ItemBlocks.clear(true);
    }

    template<typename T> template<typename... Types>
    T* PoolAllocator<T>::Alloc(Types... args)
    {
        for (size_t i = m_ItemBlocks.size(); i--; )
        {
            ItemBlock& block = m_ItemBlocks[i];
            // This block has some free items: Use first one.
            if (block.FirstFreeIndex != UINT32_MAX)
            {
                Item* const pItem = &block.pItems[block.FirstFreeIndex];
                block.FirstFreeIndex = pItem->NextFreeIndex;
                T* result = (T*)&pItem->Value;
                new(result)T(std::forward<Types>(args)...); // Explicit constructor call.
                return result;
            }
        }

        // No block has free item: Create new one and use it.
        ItemBlock& newBlock = CreateNewBlock();
        Item* const pItem = &newBlock.pItems[0];
        newBlock.FirstFreeIndex = pItem->NextFreeIndex;
        T* result = (T*)pItem->Value;
        new(result)T(std::forward<Types>(args)...); // Explicit constructor call.
        return result;
    }

    template<typename T>
    void PoolAllocator<T>::Free(T* ptr)
    {
        // Search all memory blocks to find ptr.
        for (size_t i = m_ItemBlocks.size(); i--; )
        {
            ItemBlock& block = m_ItemBlocks[i];

            Item* pItemPtr;
            memcpy(&pItemPtr, &ptr, sizeof(pItemPtr));

            // Check if pItemPtr is in address range of this block.
            if ((pItemPtr >= block.pItems) && (pItemPtr < block.pItems + block.Capacity))
            {
                ptr->~T(); // Explicit destructor call.
                const UINT index = static_cast<UINT>(pItemPtr - block.pItems);
                pItemPtr->NextFreeIndex = block.FirstFreeIndex;
                block.FirstFreeIndex = index;
                return;
            }
        }
        D3D12MA_ASSERT(0 && "Pointer doesn't belong to this memory pool.");
    }

    template<typename T>
    typename PoolAllocator<T>::ItemBlock& PoolAllocator<T>::CreateNewBlock()
    {
        const UINT newBlockCapacity = m_ItemBlocks.empty() ?
            m_FirstBlockCapacity : m_ItemBlocks.back().Capacity * 3 / 2;

        const ItemBlock newBlock = {
            D3D12MA_NEW_ARRAY(m_AllocationCallbacks, Item, newBlockCapacity),
            newBlockCapacity,
            0 };

        m_ItemBlocks.push_back(newBlock);

        // Setup singly-linked list of all free items in this block.
        for (UINT i = 0; i < newBlockCapacity - 1; ++i)
        {
            newBlock.pItems[i].NextFreeIndex = i + 1;
        }
        newBlock.pItems[newBlockCapacity - 1].NextFreeIndex = UINT32_MAX;
        return m_ItemBlocks.back();
    }
#endif // _D3D12MA_POOL_ALLOCATOR_FUNCTIONS
#endif // _D3D12MA_POOL_ALLOCATOR

#ifndef _D3D12MA_LIST
    /*
    Doubly linked list, with elements allocated out of PoolAllocator.
    Has custom interface, as well as STL-style interface, including iterator and
    const_iterator.
    */
    template<typename T>
    class List
    {
        D3D12MA_CLASS_NO_COPY(List)
    public:
        struct Item
        {
            Item* pPrev;
            Item* pNext;
            T Value;
        };

        class reverse_iterator;
        class const_reverse_iterator;
        class iterator
        {
            friend class List<T>;
            friend class const_iterator;

        public:
            iterator() = default;
            iterator(const reverse_iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }

            T& operator*() const;
            T* operator->() const;

            iterator& operator++();
            iterator& operator--();
            iterator operator++(int);
            iterator operator--(int);

            bool operator==(const iterator& rhs) const;
            bool operator!=(const iterator& rhs) const;

        private:
            List<T>* m_pList = NULL;
            Item* m_pItem = NULL;

            iterator(List<T>* pList, Item* pItem) : m_pList(pList), m_pItem(pItem) {}
        };

        class reverse_iterator
        {
            friend class List<T>;
            friend class const_reverse_iterator;

        public:
            reverse_iterator() = default;
            reverse_iterator(const iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }

            T& operator*() const;
            T* operator->() const;

            reverse_iterator& operator++();
            reverse_iterator& operator--();
            reverse_iterator operator++(int);
            reverse_iterator operator--(int);

            bool operator==(const reverse_iterator& rhs) const;
            bool operator!=(const reverse_iterator& rhs) const;

        private:
            List<T>* m_pList = NULL;
            Item* m_pItem = NULL;

            reverse_iterator(List<T>* pList, Item* pItem)
                : m_pList(pList), m_pItem(pItem) {
            }
        };

        class const_iterator
        {
            friend class List<T>;

        public:
            const_iterator() = default;
            const_iterator(const iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }
            const_iterator(const reverse_iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }
            const_iterator(const const_reverse_iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }

            iterator dropConst() const;
            const T& operator*() const;
            const T* operator->() const;

            const_iterator& operator++();
            const_iterator& operator--();
            const_iterator operator++(int);
            const_iterator operator--(int);

            bool operator==(const const_iterator& rhs) const;
            bool operator!=(const const_iterator& rhs) const;

        private:
            const List<T>* m_pList = NULL;
            const Item* m_pItem = NULL;

            const_iterator(const List<T>* pList, const Item* pItem)
                : m_pList(pList), m_pItem(pItem) {
            }
        };

        class const_reverse_iterator
        {
            friend class List<T>;

        public:
            const_reverse_iterator() = default;
            const_reverse_iterator(const iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }
            const_reverse_iterator(const reverse_iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }
            const_reverse_iterator(const const_iterator& src)
                : m_pList(src.m_pList), m_pItem(src.m_pItem) {
            }

            reverse_iterator dropConst() const;
            const T& operator*() const;
            const T* operator->() const;

            const_reverse_iterator& operator++();
            const_reverse_iterator& operator--();
            const_reverse_iterator operator++(int);
            const_reverse_iterator operator--(int);

            bool operator==(const const_reverse_iterator& rhs) const;
            bool operator!=(const const_reverse_iterator& rhs) const;

        private:
            const List<T>* m_pList = NULL;
            const Item* m_pItem = NULL;

            const_reverse_iterator(const List<T>* pList, const Item* pItem)
                : m_pList(pList), m_pItem(pItem) {
            }
        };

        // allocationCallbacks externally owned, must outlive this object.
        List(const AllocationCallbacks& allocationCallbacks);
        // Intentionally not calling Clear, because that would be unnecessary
        // computations to return all items to m_ItemAllocator as free.
        ~List() = default;

        size_t GetCount() const { return m_Count; }
        bool IsEmpty() const { return m_Count == 0; }

        Item* Front() { return m_pFront; }
        const Item* Front() const { return m_pFront; }
        Item* Back() { return m_pBack; }
        const Item* Back() const { return m_pBack; }

        bool empty() const { return IsEmpty(); }
        size_t size() const { return GetCount(); }
        void push_back(const T& value) { PushBack(value); }
        iterator insert(iterator it, const T& value) { return iterator(this, InsertBefore(it.m_pItem, value)); }
        void clear() { Clear(); }
        void erase(iterator it) { Remove(it.m_pItem); }

        iterator begin() { return iterator(this, Front()); }
        iterator end() { return iterator(this, NULL); }
        reverse_iterator rbegin() { return reverse_iterator(this, Back()); }
        reverse_iterator rend() { return reverse_iterator(this, NULL); }

        const_iterator cbegin() const { return const_iterator(this, Front()); }
        const_iterator cend() const { return const_iterator(this, NULL); }
        const_iterator begin() const { return cbegin(); }
        const_iterator end() const { return cend(); }

        const_reverse_iterator crbegin() const { return const_reverse_iterator(this, Back()); }
        const_reverse_iterator crend() const { return const_reverse_iterator(this, NULL); }
        const_reverse_iterator rbegin() const { return crbegin(); }
        const_reverse_iterator rend() const { return crend(); }

        Item* PushBack();
        Item* PushFront();
        Item* PushBack(const T& value);
        Item* PushFront(const T& value);
        void PopBack();
        void PopFront();

        // Item can be null - it means PushBack.
        Item* InsertBefore(Item* pItem);
        // Item can be null - it means PushFront.
        Item* InsertAfter(Item* pItem);
        Item* InsertBefore(Item* pItem, const T& value);
        Item* InsertAfter(Item* pItem, const T& value);

        void Clear();
        void Remove(Item* pItem);

    private:
        const AllocationCallbacks& m_AllocationCallbacks;
        PoolAllocator<Item> m_ItemAllocator;
        Item* m_pFront;
        Item* m_pBack;
        size_t m_Count;
    };

#ifndef _D3D12MA_LIST_ITERATOR_FUNCTIONS
    template<typename T>
    T& List<T>::iterator::operator*() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return m_pItem->Value;
    }

    template<typename T>
    T* List<T>::iterator::operator->() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return &m_pItem->Value;
    }

    template<typename T>
    typename List<T>::iterator& List<T>::iterator::operator++()
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        m_pItem = m_pItem->pNext;
        return *this;
    }

    template<typename T>
    typename List<T>::iterator& List<T>::iterator::operator--()
    {
        if (m_pItem != NULL)
        {
            m_pItem = m_pItem->pPrev;
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(!m_pList->IsEmpty());
            m_pItem = m_pList->Back();
        }
        return *this;
    }

    template<typename T>
    typename List<T>::iterator List<T>::iterator::operator++(int)
    {
        iterator result = *this;
        ++*this;
        return result;
    }

    template<typename T>
    typename List<T>::iterator List<T>::iterator::operator--(int)
    {
        iterator result = *this;
        --*this;
        return result;
    }

    template<typename T>
    bool List<T>::iterator::operator==(const iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem == rhs.m_pItem;
    }

    template<typename T>
    bool List<T>::iterator::operator!=(const iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem != rhs.m_pItem;
    }
#endif // _D3D12MA_LIST_ITERATOR_FUNCTIONS

#ifndef _D3D12MA_LIST_REVERSE_ITERATOR_FUNCTIONS
    template<typename T>
    T& List<T>::reverse_iterator::operator*() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return m_pItem->Value;
    }

    template<typename T>
    T* List<T>::reverse_iterator::operator->() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return &m_pItem->Value;
    }

    template<typename T>
    typename List<T>::reverse_iterator& List<T>::reverse_iterator::operator++()
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        m_pItem = m_pItem->pPrev;
        return *this;
    }

    template<typename T>
    typename List<T>::reverse_iterator& List<T>::reverse_iterator::operator--()
    {
        if (m_pItem != NULL)
        {
            m_pItem = m_pItem->pNext;
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(!m_pList->IsEmpty());
            m_pItem = m_pList->Front();
        }
        return *this;
    }

    template<typename T>
    typename List<T>::reverse_iterator List<T>::reverse_iterator::operator++(int)
    {
        reverse_iterator result = *this;
        ++*this;
        return result;
    }

    template<typename T>
    typename List<T>::reverse_iterator List<T>::reverse_iterator::operator--(int)
    {
        reverse_iterator result = *this;
        --*this;
        return result;
    }

    template<typename T>
    bool List<T>::reverse_iterator::operator==(const reverse_iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem == rhs.m_pItem;
    }

    template<typename T>
    bool List<T>::reverse_iterator::operator!=(const reverse_iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem != rhs.m_pItem;
    }
#endif // _D3D12MA_LIST_REVERSE_ITERATOR_FUNCTIONS

#ifndef _D3D12MA_LIST_CONST_ITERATOR_FUNCTIONS
    template<typename T>
    typename List<T>::iterator List<T>::const_iterator::dropConst() const
    {
        return iterator(const_cast<List<T>*>(m_pList), const_cast<Item*>(m_pItem));
    }

    template<typename T>
    const T& List<T>::const_iterator::operator*() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return m_pItem->Value;
    }

    template<typename T>
    const T* List<T>::const_iterator::operator->() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return &m_pItem->Value;
    }

    template<typename T>
    typename List<T>::const_iterator& List<T>::const_iterator::operator++()
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        m_pItem = m_pItem->pNext;
        return *this;
    }

    template<typename T>
    typename List<T>::const_iterator& List<T>::const_iterator::operator--()
    {
        if (m_pItem != NULL)
        {
            m_pItem = m_pItem->pPrev;
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(!m_pList->IsEmpty());
            m_pItem = m_pList->Back();
        }
        return *this;
    }

    template<typename T>
    typename List<T>::const_iterator List<T>::const_iterator::operator++(int)
    {
        const_iterator result = *this;
        ++*this;
        return result;
    }

    template<typename T>
    typename List<T>::const_iterator List<T>::const_iterator::operator--(int)
    {
        const_iterator result = *this;
        --*this;
        return result;
    }

    template<typename T>
    bool List<T>::const_iterator::operator==(const const_iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem == rhs.m_pItem;
    }

    template<typename T>
    bool List<T>::const_iterator::operator!=(const const_iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem != rhs.m_pItem;
    }
#endif // _D3D12MA_LIST_CONST_ITERATOR_FUNCTIONS

#ifndef _D3D12MA_LIST_CONST_REVERSE_ITERATOR_FUNCTIONS
    template<typename T>
    typename List<T>::reverse_iterator List<T>::const_reverse_iterator::dropConst() const
    {
        return reverse_iterator(const_cast<List<T>*>(m_pList), const_cast<Item*>(m_pItem));
    }

    template<typename T>
    const T& List<T>::const_reverse_iterator::operator*() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return m_pItem->Value;
    }

    template<typename T>
    const T* List<T>::const_reverse_iterator::operator->() const
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        return &m_pItem->Value;
    }

    template<typename T>
    typename List<T>::const_reverse_iterator& List<T>::const_reverse_iterator::operator++()
    {
        D3D12MA_HEAVY_ASSERT(m_pItem != NULL);
        m_pItem = m_pItem->pPrev;
        return *this;
    }

    template<typename T>
    typename List<T>::const_reverse_iterator& List<T>::const_reverse_iterator::operator--()
    {
        if (m_pItem != NULL)
        {
            m_pItem = m_pItem->pNext;
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(!m_pList->IsEmpty());
            m_pItem = m_pList->Front();
        }
        return *this;
    }

    template<typename T>
    typename List<T>::const_reverse_iterator List<T>::const_reverse_iterator::operator++(int)
    {
        const_reverse_iterator result = *this;
        ++*this;
        return result;
    }

    template<typename T>
    typename List<T>::const_reverse_iterator List<T>::const_reverse_iterator::operator--(int)
    {
        const_reverse_iterator result = *this;
        --*this;
        return result;
    }

    template<typename T>
    bool List<T>::const_reverse_iterator::operator==(const const_reverse_iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem == rhs.m_pItem;
    }

    template<typename T>
    bool List<T>::const_reverse_iterator::operator!=(const const_reverse_iterator& rhs) const
    {
        D3D12MA_HEAVY_ASSERT(m_pList == rhs.m_pList);
        return m_pItem != rhs.m_pItem;
    }
#endif // _D3D12MA_LIST_CONST_REVERSE_ITERATOR_FUNCTIONS

#ifndef _D3D12MA_LIST_FUNCTIONS
    template<typename T>
    List<T>::List(const AllocationCallbacks& allocationCallbacks)
        : m_AllocationCallbacks(allocationCallbacks),
        m_ItemAllocator(allocationCallbacks, 128),
        m_pFront(NULL),
        m_pBack(NULL),
        m_Count(0) {
    }

    template<typename T>
    void List<T>::Clear()
    {
        if (!IsEmpty())
        {
            Item* pItem = m_pBack;
            while (pItem != NULL)
            {
                Item* const pPrevItem = pItem->pPrev;
                m_ItemAllocator.Free(pItem);
                pItem = pPrevItem;
            }
            m_pFront = NULL;
            m_pBack = NULL;
            m_Count = 0;
        }
    }

    template<typename T>
    typename List<T>::Item* List<T>::PushBack()
    {
        Item* const pNewItem = m_ItemAllocator.Alloc();
        pNewItem->pNext = NULL;
        if (IsEmpty())
        {
            pNewItem->pPrev = NULL;
            m_pFront = pNewItem;
            m_pBack = pNewItem;
            m_Count = 1;
        }
        else
        {
            pNewItem->pPrev = m_pBack;
            m_pBack->pNext = pNewItem;
            m_pBack = pNewItem;
            ++m_Count;
        }
        return pNewItem;
    }

    template<typename T>
    typename List<T>::Item* List<T>::PushFront()
    {
        Item* const pNewItem = m_ItemAllocator.Alloc();
        pNewItem->pPrev = NULL;
        if (IsEmpty())
        {
            pNewItem->pNext = NULL;
            m_pFront = pNewItem;
            m_pBack = pNewItem;
            m_Count = 1;
        }
        else
        {
            pNewItem->pNext = m_pFront;
            m_pFront->pPrev = pNewItem;
            m_pFront = pNewItem;
            ++m_Count;
        }
        return pNewItem;
    }

    template<typename T>
    typename List<T>::Item* List<T>::PushBack(const T& value)
    {
        Item* const pNewItem = PushBack();
        pNewItem->Value = value;
        return pNewItem;
    }

    template<typename T>
    typename List<T>::Item* List<T>::PushFront(const T& value)
    {
        Item* const pNewItem = PushFront();
        pNewItem->Value = value;
        return pNewItem;
    }

    template<typename T>
    void List<T>::PopBack()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        Item* const pBackItem = m_pBack;
        Item* const pPrevItem = pBackItem->pPrev;
        if (pPrevItem != NULL)
        {
            pPrevItem->pNext = NULL;
        }
        m_pBack = pPrevItem;
        m_ItemAllocator.Free(pBackItem);
        --m_Count;
    }

    template<typename T>
    void List<T>::PopFront()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        Item* const pFrontItem = m_pFront;
        Item* const pNextItem = pFrontItem->pNext;
        if (pNextItem != NULL)
        {
            pNextItem->pPrev = NULL;
        }
        m_pFront = pNextItem;
        m_ItemAllocator.Free(pFrontItem);
        --m_Count;
    }

    template<typename T>
    void List<T>::Remove(Item* pItem)
    {
        D3D12MA_HEAVY_ASSERT(pItem != NULL);
        D3D12MA_HEAVY_ASSERT(m_Count > 0);

        if (pItem->pPrev != NULL)
        {
            pItem->pPrev->pNext = pItem->pNext;
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(m_pFront == pItem);
            m_pFront = pItem->pNext;
        }

        if (pItem->pNext != NULL)
        {
            pItem->pNext->pPrev = pItem->pPrev;
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(m_pBack == pItem);
            m_pBack = pItem->pPrev;
        }

        m_ItemAllocator.Free(pItem);
        --m_Count;
    }

    template<typename T>
    typename List<T>::Item* List<T>::InsertBefore(Item* pItem)
    {
        if (pItem != NULL)
        {
            Item* const prevItem = pItem->pPrev;
            Item* const newItem = m_ItemAllocator.Alloc();
            newItem->pPrev = prevItem;
            newItem->pNext = pItem;
            pItem->pPrev = newItem;
            if (prevItem != NULL)
            {
                prevItem->pNext = newItem;
            }
            else
            {
                D3D12MA_HEAVY_ASSERT(m_pFront == pItem);
                m_pFront = newItem;
            }
            ++m_Count;
            return newItem;
        }
        else
        {
            return PushBack();
        }
    }

    template<typename T>
    typename List<T>::Item* List<T>::InsertAfter(Item* pItem)
    {
        if (pItem != NULL)
        {
            Item* const nextItem = pItem->pNext;
            Item* const newItem = m_ItemAllocator.Alloc();
            newItem->pNext = nextItem;
            newItem->pPrev = pItem;
            pItem->pNext = newItem;
            if (nextItem != NULL)
            {
                nextItem->pPrev = newItem;
            }
            else
            {
                D3D12MA_HEAVY_ASSERT(m_pBack == pItem);
                m_pBack = newItem;
            }
            ++m_Count;
            return newItem;
        }
        else
            return PushFront();
    }

    template<typename T>
    typename List<T>::Item* List<T>::InsertBefore(Item* pItem, const T& value)
    {
        Item* const newItem = InsertBefore(pItem);
        newItem->Value = value;
        return newItem;
    }

    template<typename T>
    typename List<T>::Item* List<T>::InsertAfter(Item* pItem, const T& value)
    {
        Item* const newItem = InsertAfter(pItem);
        newItem->Value = value;
        return newItem;
    }
#endif // _D3D12MA_LIST_FUNCTIONS
#endif // _D3D12MA_LIST

#ifndef _D3D12MA_INTRUSIVE_LINKED_LIST
    /*
    Expected interface of ItemTypeTraits:
    struct MyItemTypeTraits
    {
        using ItemType = MyItem;
        static ItemType* GetPrev(const ItemType* item) { return item->myPrevPtr; }
        static ItemType* GetNext(const ItemType* item) { return item->myNextPtr; }
        static ItemType*& AccessPrev(ItemType* item) { return item->myPrevPtr; }
        static ItemType*& AccessNext(ItemType* item) { return item->myNextPtr; }
    };
    */
    template<typename ItemTypeTraits>
    class IntrusiveLinkedList
    {
    public:
        using ItemType = typename ItemTypeTraits::ItemType;
        static ItemType* GetPrev(const ItemType* item) { return ItemTypeTraits::GetPrev(item); }
        static ItemType* GetNext(const ItemType* item) { return ItemTypeTraits::GetNext(item); }

        // Movable, not copyable.
        IntrusiveLinkedList() = default;
        IntrusiveLinkedList(const IntrusiveLinkedList&) = delete;
        IntrusiveLinkedList(IntrusiveLinkedList&& src);
        IntrusiveLinkedList& operator=(const IntrusiveLinkedList&) = delete;
        IntrusiveLinkedList& operator=(IntrusiveLinkedList&& src);
        ~IntrusiveLinkedList() { D3D12MA_HEAVY_ASSERT(IsEmpty()); }

        size_t GetCount() const { return m_Count; }
        bool IsEmpty() const { return m_Count == 0; }

        ItemType* Front() { return m_Front; }
        ItemType* Back() { return m_Back; }
        const ItemType* Front() const { return m_Front; }
        const ItemType* Back() const { return m_Back; }

        void PushBack(ItemType* item);
        void PushFront(ItemType* item);
        ItemType* PopBack();
        ItemType* PopFront();

        // MyItem can be null - it means PushBack.
        void InsertBefore(ItemType* existingItem, ItemType* newItem);
        // MyItem can be null - it means PushFront.
        void InsertAfter(ItemType* existingItem, ItemType* newItem);

        void Remove(ItemType* item);
        void RemoveAll();

    private:
        ItemType* m_Front = NULL;
        ItemType* m_Back = NULL;
        size_t m_Count = 0;
    };

#ifndef _D3D12MA_INTRUSIVE_LINKED_LIST_FUNCTIONS
    template<typename ItemTypeTraits>
    IntrusiveLinkedList<ItemTypeTraits>::IntrusiveLinkedList(IntrusiveLinkedList&& src)
        : m_Front(src.m_Front), m_Back(src.m_Back), m_Count(src.m_Count)
    {
        src.m_Front = src.m_Back = NULL;
        src.m_Count = 0;
    }

    template<typename ItemTypeTraits>
    IntrusiveLinkedList<ItemTypeTraits>& IntrusiveLinkedList<ItemTypeTraits>::operator=(IntrusiveLinkedList&& src)
    {
        if (&src != this)
        {
            D3D12MA_HEAVY_ASSERT(IsEmpty());
            m_Front = src.m_Front;
            m_Back = src.m_Back;
            m_Count = src.m_Count;
            src.m_Front = src.m_Back = NULL;
            src.m_Count = 0;
        }
        return *this;
    }

    template<typename ItemTypeTraits>
    void IntrusiveLinkedList<ItemTypeTraits>::PushBack(ItemType* item)
    {
        D3D12MA_HEAVY_ASSERT(ItemTypeTraits::GetPrev(item) == NULL && ItemTypeTraits::GetNext(item) == NULL);
        if (IsEmpty())
        {
            m_Front = item;
            m_Back = item;
            m_Count = 1;
        }
        else
        {
            ItemTypeTraits::AccessPrev(item) = m_Back;
            ItemTypeTraits::AccessNext(m_Back) = item;
            m_Back = item;
            ++m_Count;
        }
    }

    template<typename ItemTypeTraits>
    void IntrusiveLinkedList<ItemTypeTraits>::PushFront(ItemType* item)
    {
        D3D12MA_HEAVY_ASSERT(ItemTypeTraits::GetPrev(item) == NULL && ItemTypeTraits::GetNext(item) == NULL);
        if (IsEmpty())
        {
            m_Front = item;
            m_Back = item;
            m_Count = 1;
        }
        else
        {
            ItemTypeTraits::AccessNext(item) = m_Front;
            ItemTypeTraits::AccessPrev(m_Front) = item;
            m_Front = item;
            ++m_Count;
        }
    }

    template<typename ItemTypeTraits>
    typename IntrusiveLinkedList<ItemTypeTraits>::ItemType* IntrusiveLinkedList<ItemTypeTraits>::PopBack()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        ItemType* const backItem = m_Back;
        ItemType* const prevItem = ItemTypeTraits::GetPrev(backItem);
        if (prevItem != NULL)
        {
            ItemTypeTraits::AccessNext(prevItem) = NULL;
        }
        m_Back = prevItem;
        --m_Count;
        ItemTypeTraits::AccessPrev(backItem) = NULL;
        ItemTypeTraits::AccessNext(backItem) = NULL;
        return backItem;
    }

    template<typename ItemTypeTraits>
    typename IntrusiveLinkedList<ItemTypeTraits>::ItemType* IntrusiveLinkedList<ItemTypeTraits>::PopFront()
    {
        D3D12MA_HEAVY_ASSERT(m_Count > 0);
        ItemType* const frontItem = m_Front;
        ItemType* const nextItem = ItemTypeTraits::GetNext(frontItem);
        if (nextItem != NULL)
        {
            ItemTypeTraits::AccessPrev(nextItem) = NULL;
        }
        m_Front = nextItem;
        --m_Count;
        ItemTypeTraits::AccessPrev(frontItem) = NULL;
        ItemTypeTraits::AccessNext(frontItem) = NULL;
        return frontItem;
    }

    template<typename ItemTypeTraits>
    void IntrusiveLinkedList<ItemTypeTraits>::InsertBefore(ItemType* existingItem, ItemType* newItem)
    {
        D3D12MA_HEAVY_ASSERT(newItem != NULL && ItemTypeTraits::GetPrev(newItem) == NULL && ItemTypeTraits::GetNext(newItem) == NULL);
        if (existingItem != NULL)
        {
            ItemType* const prevItem = ItemTypeTraits::GetPrev(existingItem);
            ItemTypeTraits::AccessPrev(newItem) = prevItem;
            ItemTypeTraits::AccessNext(newItem) = existingItem;
            ItemTypeTraits::AccessPrev(existingItem) = newItem;
            if (prevItem != NULL)
            {
                ItemTypeTraits::AccessNext(prevItem) = newItem;
            }
            else
            {
                D3D12MA_HEAVY_ASSERT(m_Front == existingItem);
                m_Front = newItem;
            }
            ++m_Count;
        }
        else
            PushBack(newItem);
    }

    template<typename ItemTypeTraits>
    void IntrusiveLinkedList<ItemTypeTraits>::InsertAfter(ItemType* existingItem, ItemType* newItem)
    {
        D3D12MA_HEAVY_ASSERT(newItem != NULL && ItemTypeTraits::GetPrev(newItem) == NULL && ItemTypeTraits::GetNext(newItem) == NULL);
        if (existingItem != NULL)
        {
            ItemType* const nextItem = ItemTypeTraits::GetNext(existingItem);
            ItemTypeTraits::AccessNext(newItem) = nextItem;
            ItemTypeTraits::AccessPrev(newItem) = existingItem;
            ItemTypeTraits::AccessNext(existingItem) = newItem;
            if (nextItem != NULL)
            {
                ItemTypeTraits::AccessPrev(nextItem) = newItem;
            }
            else
            {
                D3D12MA_HEAVY_ASSERT(m_Back == existingItem);
                m_Back = newItem;
            }
            ++m_Count;
        }
        else
            return PushFront(newItem);
    }

    template<typename ItemTypeTraits>
    void IntrusiveLinkedList<ItemTypeTraits>::Remove(ItemType* item)
    {
        D3D12MA_HEAVY_ASSERT(item != NULL && m_Count > 0);
        if (ItemTypeTraits::GetPrev(item) != NULL)
        {
            ItemTypeTraits::AccessNext(ItemTypeTraits::AccessPrev(item)) = ItemTypeTraits::GetNext(item);
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(m_Front == item);
            m_Front = ItemTypeTraits::GetNext(item);
        }

        if (ItemTypeTraits::GetNext(item) != NULL)
        {
            ItemTypeTraits::AccessPrev(ItemTypeTraits::AccessNext(item)) = ItemTypeTraits::GetPrev(item);
        }
        else
        {
            D3D12MA_HEAVY_ASSERT(m_Back == item);
            m_Back = ItemTypeTraits::GetPrev(item);
        }
        ItemTypeTraits::AccessPrev(item) = NULL;
        ItemTypeTraits::AccessNext(item) = NULL;
        --m_Count;
    }

    template<typename ItemTypeTraits>
    void IntrusiveLinkedList<ItemTypeTraits>::RemoveAll()
    {
        if (!IsEmpty())
        {
            ItemType* item = m_Back;
            while (item != NULL)
            {
                ItemType* const prevItem = ItemTypeTraits::AccessPrev(item);
                ItemTypeTraits::AccessPrev(item) = NULL;
                ItemTypeTraits::AccessNext(item) = NULL;
                item = prevItem;
            }
            m_Front = NULL;
            m_Back = NULL;
            m_Count = 0;
        }
    }
#endif // _D3D12MA_INTRUSIVE_LINKED_LIST_FUNCTIONS
#endif // _D3D12MA_INTRUSIVE_LINKED_LIST

#ifndef _D3D12MA_ALLOCATION_OBJECT_ALLOCATOR
    /*
    Thread-safe wrapper over PoolAllocator free list, for allocation of Allocation objects.
    */
    class AllocationObjectAllocator
    {
        D3D12MA_CLASS_NO_COPY(AllocationObjectAllocator);
    public:
        AllocationObjectAllocator(const AllocationCallbacks& allocationCallbacks, bool useMutex)
            : m_UseMutex(useMutex), m_Allocator(allocationCallbacks, 1024) {
        }

        template<typename... Types>
        Allocation* Allocate(Types... args);
        void Free(Allocation* alloc);

    private:
        D3D12MA_MUTEX m_Mutex;
        bool m_UseMutex;
        PoolAllocator<Allocation> m_Allocator;
    };

#ifndef _D3D12MA_ALLOCATION_OBJECT_ALLOCATOR_FUNCTIONS
    template<typename... Types>
    Allocation* AllocationObjectAllocator::Allocate(Types... args)
    {
        MutexLock mutexLock(m_Mutex, m_UseMutex);
        return m_Allocator.Alloc(std::forward<Types>(args)...);
    }

    void AllocationObjectAllocator::Free(Allocation* alloc)
    {
        MutexLock mutexLock(m_Mutex, m_UseMutex);
        m_Allocator.Free(alloc);
    }
#endif // _D3D12MA_ALLOCATION_OBJECT_ALLOCATOR_FUNCTIONS
#endif // _D3D12MA_ALLOCATION_OBJECT_ALLOCATOR

#ifndef _D3D12MA_SUBALLOCATION
    /*
    Represents a region of NormalBlock that is either assigned and returned as
    allocated memory block or free.
    */
    struct Suballocation
    {
        UINT64 offset;
        UINT64 size;
        void* privateData;
        SuballocationType type;
    };
    using SuballocationList = List<Suballocation>;

    // Comparator for offsets.
    struct SuballocationOffsetLess
    {
        bool operator()(const Suballocation& lhs, const Suballocation& rhs) const
        {
            return lhs.offset < rhs.offset;
        }
    };

    struct SuballocationOffsetGreater
    {
        bool operator()(const Suballocation& lhs, const Suballocation& rhs) const
        {
            return lhs.offset > rhs.offset;
        }
    };

    struct SuballocationItemSizeLess
    {
        bool operator()(const SuballocationList::iterator lhs, const SuballocationList::iterator rhs) const
        {
            return lhs->size < rhs->size;
        }
        bool operator()(const SuballocationList::iterator lhs, UINT64 rhsSize) const
        {
            return lhs->size < rhsSize;
        }
    };
#endif // _D3D12MA_SUBALLOCATION

#ifndef _D3D12MA_ALLOCATION_REQUEST
    /*
    Parameters of planned allocation inside a NormalBlock.
    */
    struct AllocationRequest
    {
        AllocHandle allocHandle;
        UINT64 size;
        UINT64 algorithmData;
        UINT64 sumFreeSize; // Sum size of free items that overlap with proposed allocation.
        UINT64 sumItemSize; // Sum size of items to make lost that overlap with proposed allocation.
        SuballocationList::iterator item;
    };
#endif // _D3D12MA_ALLOCATION_REQUEST

#ifndef _D3D12MA_BLOCK_METADATA
    /*
    Data structure used for bookkeeping of allocations and unused ranges of memory
    in a single ID3D12Heap memory block.
    */
    class BlockMetadata
    {
    public:
        BlockMetadata(const AllocationCallbacks* allocationCallbacks, bool isVirtual);
        virtual ~BlockMetadata() = default;

        virtual void Init(UINT64 size) { m_Size = size; }
        // Validates all data structures inside this object. If not valid, returns false.
        virtual bool Validate() const = 0;
        UINT64 GetSize() const { return m_Size; }
        bool IsVirtual() const { return m_IsVirtual; }
        virtual size_t GetAllocationCount() const = 0;
        virtual size_t GetFreeRegionsCount() const = 0;
        virtual UINT64 GetSumFreeSize() const = 0;
        virtual UINT64 GetAllocationOffset(AllocHandle allocHandle) const = 0;
        // Returns true if this block is empty - contains only single free suballocation.
        virtual bool IsEmpty() const = 0;

        virtual void GetAllocationInfo(AllocHandle allocHandle, VirtualAllocationInfo& outInfo) const = 0;

        // Tries to find a place for suballocation with given parameters inside this block.
        // If succeeded, fills pAllocationRequest and returns true.
        // If failed, returns false.
        virtual bool CreateAllocationRequest(
            UINT64 allocSize,
            UINT64 allocAlignment,
            bool upperAddress,
            UINT32 strategy,
            AllocationRequest* pAllocationRequest) = 0;

        // Makes actual allocation based on request. Request must already be checked and valid.
        virtual void Alloc(
            const AllocationRequest& request,
            UINT64 allocSize,
            void* PrivateData) = 0;

        virtual void Free(AllocHandle allocHandle) = 0;
        // Frees all allocations.
        // Careful! Don't call it if there are Allocation objects owned by pPrivateData of of cleared allocations!
        virtual void Clear() = 0;

        virtual AllocHandle GetAllocationListBegin() const = 0;
        virtual AllocHandle GetNextAllocation(AllocHandle prevAlloc) const = 0;
        virtual UINT64 GetNextFreeRegionSize(AllocHandle alloc) const = 0;
        virtual void* GetAllocationPrivateData(AllocHandle allocHandle) const = 0;
        virtual void SetAllocationPrivateData(AllocHandle allocHandle, void* privateData) = 0;

        virtual void AddStatistics(Statistics& inoutStats) const = 0;
        virtual void AddDetailedStatistics(DetailedStatistics& inoutStats) const = 0;
        virtual void WriteAllocationInfoToJson(JsonWriter& json) const = 0;
        virtual void DebugLogAllAllocations() const = 0;

    protected:
        const AllocationCallbacks* GetAllocs() const { return m_pAllocationCallbacks; }
        UINT64 GetDebugMargin() const { return IsVirtual() ? 0 : D3D12MA_DEBUG_MARGIN; }

        void DebugLogAllocation(UINT64 offset, UINT64 size, void* privateData) const;
        void PrintDetailedMap_Begin(JsonWriter& json,
            UINT64 unusedBytes,
            size_t allocationCount,
            size_t unusedRangeCount) const;
        void PrintDetailedMap_Allocation(JsonWriter& json,
            UINT64 offset, UINT64 size, void* privateData) const;
        void PrintDetailedMap_UnusedRange(JsonWriter& json,
            UINT64 offset, UINT64 size) const;
        void PrintDetailedMap_End(JsonWriter& json) const;

    private:
        UINT64 m_Size;
        bool m_IsVirtual;
        const AllocationCallbacks* m_pAllocationCallbacks;

        D3D12MA_CLASS_NO_COPY(BlockMetadata);
    };

#ifndef _D3D12MA_BLOCK_METADATA_FUNCTIONS
    BlockMetadata::BlockMetadata(const AllocationCallbacks* allocationCallbacks, bool isVirtual)
        : m_Size(0),
        m_IsVirtual(isVirtual),
        m_pAllocationCallbacks(allocationCallbacks)
    {
        D3D12MA_ASSERT(allocationCallbacks);
    }

    void BlockMetadata::DebugLogAllocation(UINT64 offset, UINT64 size, void* privateData) const
    {
        if (IsVirtual())
        {
            D3D12MA_DEBUG_LOG(L"UNFREED VIRTUAL ALLOCATION; Offset: %llu; Size: %llu; PrivateData: %p", offset, size, privateData);
        }
        else
        {
            D3D12MA_ASSERT(privateData != NULL);
            Allocation* allocation = reinterpret_cast<Allocation*>(privateData);

            privateData = allocation->GetPrivateData();

            auto str = s2ws(allocation->GetName());
            LPCWSTR name = str.c_str();

            D3D12MA_DEBUG_LOG(L"UNFREED ALLOCATION; Offset: %llu; Size: %llu; PrivateData: %p; Name: %s",
                offset, size, privateData, name ? name : L"");
        }
    }

    void BlockMetadata::PrintDetailedMap_Begin(JsonWriter& json,
        UINT64 unusedBytes, size_t allocationCount, size_t unusedRangeCount) const
    {
        json.WriteString("TotalBytes");
        json.WriteNumber(GetSize());

        json.WriteString("UnusedBytes");
        json.WriteNumber(unusedBytes);

        json.WriteString("Allocations");
        json.WriteNumber((UINT64)allocationCount);

        json.WriteString("UnusedRanges");
        json.WriteNumber((UINT64)unusedRangeCount);

        json.WriteString("Suballocations");
        json.BeginArray();
    }

    void BlockMetadata::PrintDetailedMap_Allocation(JsonWriter& json,
        UINT64 offset, UINT64 size, void* privateData) const
    {
        json.BeginObject(true);

        json.WriteString("Offset");
        json.WriteNumber(offset);

        if (IsVirtual())
        {
            json.WriteString("Size");
            json.WriteNumber(size);
            if (privateData)
            {
                json.WriteString("CustomData");
                json.WriteNumber((uintptr_t)privateData);
            }
        }
        else
        {
            const Allocation* const alloc = (const Allocation*)privateData;
            D3D12MA_ASSERT(alloc);
            json.AddAllocationToObject(*alloc);
        }
        json.EndObject();
    }

    void BlockMetadata::PrintDetailedMap_UnusedRange(JsonWriter& json,
        UINT64 offset, UINT64 size) const
    {
        json.BeginObject(true);

        json.WriteString("Offset");
        json.WriteNumber(offset);

        json.WriteString("Type");
        json.WriteString("FREE");

        json.WriteString("Size");
        json.WriteNumber(size);

        json.EndObject();
    }

    void BlockMetadata::PrintDetailedMap_End(JsonWriter& json) const
    {
        json.EndArray();
    }
#endif // _D3D12MA_BLOCK_METADATA_FUNCTIONS
#endif // _D3D12MA_BLOCK_METADATA

#ifndef _D3D12MA_BLOCK_METADATA_LINEAR
    class BlockMetadata_Linear : public BlockMetadata
    {
    public:
        BlockMetadata_Linear(const AllocationCallbacks* allocationCallbacks, bool isVirtual);
        virtual ~BlockMetadata_Linear() = default;

        UINT64 GetSumFreeSize() const override { return m_SumFreeSize; }
        bool IsEmpty() const override { return GetAllocationCount() == 0; }
        UINT64 GetAllocationOffset(AllocHandle allocHandle) const override { return (UINT64)allocHandle - 1; };

        void Init(UINT64 size) override;
        bool Validate() const override;
        size_t GetAllocationCount() const override;
        size_t GetFreeRegionsCount() const override;
        void GetAllocationInfo(AllocHandle allocHandle, VirtualAllocationInfo& outInfo) const override;

        bool CreateAllocationRequest(
            UINT64 allocSize,
            UINT64 allocAlignment,
            bool upperAddress,
            UINT32 strategy,
            AllocationRequest* pAllocationRequest) override;

        void Alloc(
            const AllocationRequest& request,
            UINT64 allocSize,
            void* privateData) override;

        void Free(AllocHandle allocHandle) override;
        void Clear() override;

        AllocHandle GetAllocationListBegin() const override;
        AllocHandle GetNextAllocation(AllocHandle prevAlloc) const override;
        UINT64 GetNextFreeRegionSize(AllocHandle alloc) const override;
        void* GetAllocationPrivateData(AllocHandle allocHandle) const override;
        void SetAllocationPrivateData(AllocHandle allocHandle, void* privateData) override;

        void AddStatistics(Statistics& inoutStats) const override;
        void AddDetailedStatistics(DetailedStatistics& inoutStats) const override;
        void WriteAllocationInfoToJson(JsonWriter& json) const override;
        void DebugLogAllAllocations() const override;

    private:
        /*
        There are two suballocation vectors, used in ping-pong way.
        The one with index m_1stVectorIndex is called 1st.
        The one with index (m_1stVectorIndex ^ 1) is called 2nd.
        2nd can be non-empty only when 1st is not empty.
        When 2nd is not empty, m_2ndVectorMode indicates its mode of operation.
        */
        typedef Vector<Suballocation> SuballocationVectorType;

        enum ALLOC_REQUEST_TYPE
        {
            ALLOC_REQUEST_UPPER_ADDRESS,
            ALLOC_REQUEST_END_OF_1ST,
            ALLOC_REQUEST_END_OF_2ND,
        };

        enum SECOND_VECTOR_MODE
        {
            SECOND_VECTOR_EMPTY,
            /*
            Suballocations in 2nd vector are created later than the ones in 1st, but they
            all have smaller offset.
            */
            SECOND_VECTOR_RING_BUFFER,
            /*
            Suballocations in 2nd vector are upper side of double stack.
            They all have offsets higher than those in 1st vector.
            Top of this stack means smaller offsets, but higher indices in this vector.
            */
            SECOND_VECTOR_DOUBLE_STACK,
        };

        UINT64 m_SumFreeSize;
        SuballocationVectorType m_Suballocations0, m_Suballocations1;
        UINT32 m_1stVectorIndex;
        SECOND_VECTOR_MODE m_2ndVectorMode;
        // Number of items in 1st vector with hAllocation = null at the beginning.
        size_t m_1stNullItemsBeginCount;
        // Number of other items in 1st vector with hAllocation = null somewhere in the middle.
        size_t m_1stNullItemsMiddleCount;
        // Number of items in 2nd vector with hAllocation = null.
        size_t m_2ndNullItemsCount;

        SuballocationVectorType& AccessSuballocations1st() { return m_1stVectorIndex ? m_Suballocations1 : m_Suballocations0; }
        SuballocationVectorType& AccessSuballocations2nd() { return m_1stVectorIndex ? m_Suballocations0 : m_Suballocations1; }
        const SuballocationVectorType& AccessSuballocations1st() const { return m_1stVectorIndex ? m_Suballocations1 : m_Suballocations0; }
        const SuballocationVectorType& AccessSuballocations2nd() const { return m_1stVectorIndex ? m_Suballocations0 : m_Suballocations1; }

        Suballocation& FindSuballocation(UINT64 offset) const;
        bool ShouldCompact1st() const;
        void CleanupAfterFree();

        bool CreateAllocationRequest_LowerAddress(
            UINT64 allocSize,
            UINT64 allocAlignment,
            AllocationRequest* pAllocationRequest);
        bool CreateAllocationRequest_UpperAddress(
            UINT64 allocSize,
            UINT64 allocAlignment,
            AllocationRequest* pAllocationRequest);

        D3D12MA_CLASS_NO_COPY(BlockMetadata_Linear)
    };

#ifndef _D3D12MA_BLOCK_METADATA_LINEAR_FUNCTIONS
    BlockMetadata_Linear::BlockMetadata_Linear(const AllocationCallbacks* allocationCallbacks, bool isVirtual)
        : BlockMetadata(allocationCallbacks, isVirtual),
        m_SumFreeSize(0),
        m_Suballocations0(*allocationCallbacks),
        m_Suballocations1(*allocationCallbacks),
        m_1stVectorIndex(0),
        m_2ndVectorMode(SECOND_VECTOR_EMPTY),
        m_1stNullItemsBeginCount(0),
        m_1stNullItemsMiddleCount(0),
        m_2ndNullItemsCount(0)
    {
        D3D12MA_ASSERT(allocationCallbacks);
    }

    void BlockMetadata_Linear::Init(UINT64 size)
    {
        BlockMetadata::Init(size);
        m_SumFreeSize = size;
    }

    bool BlockMetadata_Linear::Validate() const
    {
        D3D12MA_VALIDATE(GetSumFreeSize() <= GetSize());
        const SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        const SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();

        D3D12MA_VALIDATE(suballocations2nd.empty() == (m_2ndVectorMode == SECOND_VECTOR_EMPTY));
        D3D12MA_VALIDATE(!suballocations1st.empty() ||
            suballocations2nd.empty() ||
            m_2ndVectorMode != SECOND_VECTOR_RING_BUFFER);

        if (!suballocations1st.empty())
        {
            // Null item at the beginning should be accounted into m_1stNullItemsBeginCount.
            D3D12MA_VALIDATE(suballocations1st[m_1stNullItemsBeginCount].type != SUBALLOCATION_TYPE_FREE);
            // Null item at the end should be just pop_back().
            D3D12MA_VALIDATE(suballocations1st.back().type != SUBALLOCATION_TYPE_FREE);
        }
        if (!suballocations2nd.empty())
        {
            // Null item at the end should be just pop_back().
            D3D12MA_VALIDATE(suballocations2nd.back().type != SUBALLOCATION_TYPE_FREE);
        }

        D3D12MA_VALIDATE(m_1stNullItemsBeginCount + m_1stNullItemsMiddleCount <= suballocations1st.size());
        D3D12MA_VALIDATE(m_2ndNullItemsCount <= suballocations2nd.size());

        UINT64 sumUsedSize = 0;
        const size_t suballoc1stCount = suballocations1st.size();
        UINT64 offset = 0;

        if (m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER)
        {
            const size_t suballoc2ndCount = suballocations2nd.size();
            size_t nullItem2ndCount = 0;
            for (size_t i = 0; i < suballoc2ndCount; ++i)
            {
                const Suballocation& suballoc = suballocations2nd[i];
                const bool currFree = (suballoc.type == SUBALLOCATION_TYPE_FREE);

                const Allocation* alloc = (Allocation*)suballoc.privateData;
                if (!IsVirtual())
                {
                    D3D12MA_VALIDATE(currFree == (alloc == NULL));
                }
                D3D12MA_VALIDATE(suballoc.offset >= offset);

                if (!currFree)
                {
                    if (!IsVirtual())
                    {
                        D3D12MA_VALIDATE(GetAllocationOffset(alloc->GetAllocHandle()) == suballoc.offset);
                        D3D12MA_VALIDATE(alloc->GetSize() == suballoc.size);
                    }
                    sumUsedSize += suballoc.size;
                }
                else
                {
                    ++nullItem2ndCount;
                }

                offset = suballoc.offset + suballoc.size + GetDebugMargin();
            }

            D3D12MA_VALIDATE(nullItem2ndCount == m_2ndNullItemsCount);
        }

        for (size_t i = 0; i < m_1stNullItemsBeginCount; ++i)
        {
            const Suballocation& suballoc = suballocations1st[i];
            D3D12MA_VALIDATE(suballoc.type == SUBALLOCATION_TYPE_FREE &&
                suballoc.privateData == NULL);
        }

        size_t nullItem1stCount = m_1stNullItemsBeginCount;

        for (size_t i = m_1stNullItemsBeginCount; i < suballoc1stCount; ++i)
        {
            const Suballocation& suballoc = suballocations1st[i];
            const bool currFree = (suballoc.type == SUBALLOCATION_TYPE_FREE);

            const Allocation* alloc = (Allocation*)suballoc.privateData;
            if (!IsVirtual())
            {
                D3D12MA_VALIDATE(currFree == (alloc == NULL));
            }
            D3D12MA_VALIDATE(suballoc.offset >= offset);
            D3D12MA_VALIDATE(i >= m_1stNullItemsBeginCount || currFree);

            if (!currFree)
            {
                if (!IsVirtual())
                {
                    D3D12MA_VALIDATE(GetAllocationOffset(alloc->GetAllocHandle()) == suballoc.offset);
                    D3D12MA_VALIDATE(alloc->GetSize() == suballoc.size);
                }
                sumUsedSize += suballoc.size;
            }
            else
            {
                ++nullItem1stCount;
            }

            offset = suballoc.offset + suballoc.size + GetDebugMargin();
        }
        D3D12MA_VALIDATE(nullItem1stCount == m_1stNullItemsBeginCount + m_1stNullItemsMiddleCount);

        if (m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK)
        {
            const size_t suballoc2ndCount = suballocations2nd.size();
            size_t nullItem2ndCount = 0;
            for (size_t i = suballoc2ndCount; i--; )
            {
                const Suballocation& suballoc = suballocations2nd[i];
                const bool currFree = (suballoc.type == SUBALLOCATION_TYPE_FREE);

                const Allocation* alloc = (Allocation*)suballoc.privateData;
                if (!IsVirtual())
                {
                    D3D12MA_VALIDATE(currFree == (alloc == NULL));
                }
                D3D12MA_VALIDATE(suballoc.offset >= offset);

                if (!currFree)
                {
                    if (!IsVirtual())
                    {
                        D3D12MA_VALIDATE(GetAllocationOffset(alloc->GetAllocHandle()) == suballoc.offset);
                        D3D12MA_VALIDATE(alloc->GetSize() == suballoc.size);
                    }
                    sumUsedSize += suballoc.size;
                }
                else
                {
                    ++nullItem2ndCount;
                }

                offset = suballoc.offset + suballoc.size + GetDebugMargin();
            }

            D3D12MA_VALIDATE(nullItem2ndCount == m_2ndNullItemsCount);
        }

        D3D12MA_VALIDATE(offset <= GetSize());
        D3D12MA_VALIDATE(m_SumFreeSize == GetSize() - sumUsedSize);

        return true;
    }

    size_t BlockMetadata_Linear::GetAllocationCount() const
    {
        return AccessSuballocations1st().size() - m_1stNullItemsBeginCount - m_1stNullItemsMiddleCount +
            AccessSuballocations2nd().size() - m_2ndNullItemsCount;
    }

    size_t BlockMetadata_Linear::GetFreeRegionsCount() const
    {
        // Function only used for defragmentation, which is disabled for this algorithm
        D3D12MA_ASSERT(0);
        return SIZE_MAX;
    }

    void BlockMetadata_Linear::GetAllocationInfo(AllocHandle allocHandle, VirtualAllocationInfo& outInfo) const
    {
        const Suballocation& suballoc = FindSuballocation((UINT64)allocHandle - 1);
        outInfo.offset = suballoc.offset;
        outInfo.size = suballoc.size;
        outInfo.privateData = suballoc.privateData;
    }

    bool BlockMetadata_Linear::CreateAllocationRequest(
        UINT64 allocSize,
        UINT64 allocAlignment,
        bool upperAddress,
        UINT32 strategy,
        AllocationRequest* pAllocationRequest)
    {
        D3D12MA_ASSERT(allocSize > 0 && "Cannot allocate empty block!");
        D3D12MA_ASSERT(pAllocationRequest != NULL);
        D3D12MA_HEAVY_ASSERT(Validate());

        if (allocSize > GetSize())
            return false;

        pAllocationRequest->size = allocSize;
        return upperAddress ?
            CreateAllocationRequest_UpperAddress(
                allocSize, allocAlignment, pAllocationRequest) :
            CreateAllocationRequest_LowerAddress(
                allocSize, allocAlignment, pAllocationRequest);
    }

    void BlockMetadata_Linear::Alloc(
        const AllocationRequest& request,
        UINT64 allocSize,
        void* privateData)
    {
        UINT64 offset = (UINT64)request.allocHandle - 1;
        const Suballocation newSuballoc = { offset, request.size, privateData, SUBALLOCATION_TYPE_ALLOCATION };

        switch (request.algorithmData)
        {
        case ALLOC_REQUEST_UPPER_ADDRESS:
        {
            D3D12MA_ASSERT(m_2ndVectorMode != SECOND_VECTOR_RING_BUFFER &&
                "CRITICAL ERROR: Trying to use linear allocator as double stack while it was already used as ring buffer.");
            SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();
            suballocations2nd.push_back(newSuballoc);
            m_2ndVectorMode = SECOND_VECTOR_DOUBLE_STACK;
            break;
        }
        case ALLOC_REQUEST_END_OF_1ST:
        {
            SuballocationVectorType& suballocations1st = AccessSuballocations1st();

            D3D12MA_ASSERT(suballocations1st.empty() ||
                offset >= suballocations1st.back().offset + suballocations1st.back().size);
            // Check if it fits before the end of the block.
            D3D12MA_ASSERT(offset + request.size <= GetSize());

            suballocations1st.push_back(newSuballoc);
            break;
        }
        case ALLOC_REQUEST_END_OF_2ND:
        {
            SuballocationVectorType& suballocations1st = AccessSuballocations1st();
            // New allocation at the end of 2-part ring buffer, so before first allocation from 1st vector.
            D3D12MA_ASSERT(!suballocations1st.empty() &&
                offset + request.size <= suballocations1st[m_1stNullItemsBeginCount].offset);
            SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();

            switch (m_2ndVectorMode)
            {
            case SECOND_VECTOR_EMPTY:
                // First allocation from second part ring buffer.
                D3D12MA_ASSERT(suballocations2nd.empty());
                m_2ndVectorMode = SECOND_VECTOR_RING_BUFFER;
                break;
            case SECOND_VECTOR_RING_BUFFER:
                // 2-part ring buffer is already started.
                D3D12MA_ASSERT(!suballocations2nd.empty());
                break;
            case SECOND_VECTOR_DOUBLE_STACK:
                D3D12MA_ASSERT(0 && "CRITICAL ERROR: Trying to use linear allocator as ring buffer while it was already used as double stack.");
                break;
            default:
                D3D12MA_ASSERT(0);
            }

            suballocations2nd.push_back(newSuballoc);
            break;
        }
        default:
            D3D12MA_ASSERT(0 && "CRITICAL INTERNAL ERROR.");
        }
        m_SumFreeSize -= newSuballoc.size;
    }

    void BlockMetadata_Linear::Free(AllocHandle allocHandle)
    {
        SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();
        UINT64 offset = (UINT64)allocHandle - 1;

        if (!suballocations1st.empty())
        {
            // First allocation: Mark it as next empty at the beginning.
            Suballocation& firstSuballoc = suballocations1st[m_1stNullItemsBeginCount];
            if (firstSuballoc.offset == offset)
            {
                firstSuballoc.type = SUBALLOCATION_TYPE_FREE;
                firstSuballoc.privateData = NULL;
                m_SumFreeSize += firstSuballoc.size;
                ++m_1stNullItemsBeginCount;
                CleanupAfterFree();
                return;
            }
        }

        // Last allocation in 2-part ring buffer or top of upper stack (same logic).
        if (m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER ||
            m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK)
        {
            Suballocation& lastSuballoc = suballocations2nd.back();
            if (lastSuballoc.offset == offset)
            {
                m_SumFreeSize += lastSuballoc.size;
                suballocations2nd.pop_back();
                CleanupAfterFree();
                return;
            }
        }
        // Last allocation in 1st vector.
        else if (m_2ndVectorMode == SECOND_VECTOR_EMPTY)
        {
            Suballocation& lastSuballoc = suballocations1st.back();
            if (lastSuballoc.offset == offset)
            {
                m_SumFreeSize += lastSuballoc.size;
                suballocations1st.pop_back();
                CleanupAfterFree();
                return;
            }
        }

        Suballocation refSuballoc;
        refSuballoc.offset = offset;
        // Rest of members stays uninitialized intentionally for better performance.

        // Item from the middle of 1st vector.
        {
            const SuballocationVectorType::iterator it = BinaryFindSorted(
                suballocations1st.begin() + m_1stNullItemsBeginCount,
                suballocations1st.end(),
                refSuballoc,
                SuballocationOffsetLess());
            if (it != suballocations1st.end())
            {
                it->type = SUBALLOCATION_TYPE_FREE;
                it->privateData = NULL;
                ++m_1stNullItemsMiddleCount;
                m_SumFreeSize += it->size;
                CleanupAfterFree();
                return;
            }
        }

        if (m_2ndVectorMode != SECOND_VECTOR_EMPTY)
        {
            // Item from the middle of 2nd vector.
            const SuballocationVectorType::iterator it = m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER ?
                BinaryFindSorted(suballocations2nd.begin(), suballocations2nd.end(), refSuballoc, SuballocationOffsetLess()) :
                BinaryFindSorted(suballocations2nd.begin(), suballocations2nd.end(), refSuballoc, SuballocationOffsetGreater());
            if (it != suballocations2nd.end())
            {
                it->type = SUBALLOCATION_TYPE_FREE;
                it->privateData = NULL;
                ++m_2ndNullItemsCount;
                m_SumFreeSize += it->size;
                CleanupAfterFree();
                return;
            }
        }

        D3D12MA_ASSERT(0 && "Allocation to free not found in linear allocator!");
    }

    void BlockMetadata_Linear::Clear()
    {
        m_SumFreeSize = GetSize();
        m_Suballocations0.clear();
        m_Suballocations1.clear();
        // Leaving m_1stVectorIndex unchanged - it doesn't matter.
        m_2ndVectorMode = SECOND_VECTOR_EMPTY;
        m_1stNullItemsBeginCount = 0;
        m_1stNullItemsMiddleCount = 0;
        m_2ndNullItemsCount = 0;
    }

    AllocHandle BlockMetadata_Linear::GetAllocationListBegin() const
    {
        // Function only used for defragmentation, which is disabled for this algorithm
        D3D12MA_ASSERT(0);
        return (AllocHandle)0;
    }

    AllocHandle BlockMetadata_Linear::GetNextAllocation(AllocHandle prevAlloc) const
    {
        // Function only used for defragmentation, which is disabled for this algorithm
        D3D12MA_ASSERT(0);
        return (AllocHandle)0;
    }

    UINT64 BlockMetadata_Linear::GetNextFreeRegionSize(AllocHandle alloc) const
    {
        // Function only used for defragmentation, which is disabled for this algorithm
        D3D12MA_ASSERT(0);
        return 0;
    }

    void* BlockMetadata_Linear::GetAllocationPrivateData(AllocHandle allocHandle) const
    {
        return FindSuballocation((UINT64)allocHandle - 1).privateData;
    }

    void BlockMetadata_Linear::SetAllocationPrivateData(AllocHandle allocHandle, void* privateData)
    {
        Suballocation& suballoc = FindSuballocation((UINT64)allocHandle - 1);
        suballoc.privateData = privateData;
    }

    void BlockMetadata_Linear::AddStatistics(Statistics& inoutStats) const
    {
        inoutStats.blockCount++;
        inoutStats.allocationCount += (UINT)GetAllocationCount();
        inoutStats.blockBytes += GetSize();
        inoutStats.allocationBytes += GetSize() - m_SumFreeSize;
    }

    void BlockMetadata_Linear::AddDetailedStatistics(DetailedStatistics& inoutStats) const
    {
        inoutStats.stats.blockCount++;
        inoutStats.stats.blockBytes += GetSize();

        const UINT64 size = GetSize();
        const SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        const SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();
        const size_t suballoc1stCount = suballocations1st.size();
        const size_t suballoc2ndCount = suballocations2nd.size();

        UINT64 lastOffset = 0;
        if (m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER)
        {
            const UINT64 freeSpace2ndTo1stEnd = suballocations1st[m_1stNullItemsBeginCount].offset;
            size_t nextAlloc2ndIndex = 0;
            while (lastOffset < freeSpace2ndTo1stEnd)
            {
                // Find next non-null allocation or move nextAllocIndex to the end.
                while (nextAlloc2ndIndex < suballoc2ndCount &&
                    suballocations2nd[nextAlloc2ndIndex].privateData == NULL)
                {
                    ++nextAlloc2ndIndex;
                }

                // Found non-null allocation.
                if (nextAlloc2ndIndex < suballoc2ndCount)
                {
                    const Suballocation& suballoc = suballocations2nd[nextAlloc2ndIndex];

                    // 1. Process free space before this allocation.
                    if (lastOffset < suballoc.offset)
                    {
                        // There is free space from lastOffset to suballoc.offset.
                        const UINT64 unusedRangeSize = suballoc.offset - lastOffset;
                        AddDetailedStatisticsUnusedRange(inoutStats, unusedRangeSize);
                    }

                    // 2. Process this allocation.
                    // There is allocation with suballoc.offset, suballoc.size.
                    AddDetailedStatisticsAllocation(inoutStats, suballoc.size);

                    // 3. Prepare for next iteration.
                    lastOffset = suballoc.offset + suballoc.size;
                    ++nextAlloc2ndIndex;
                }
                // We are at the end.
                else
                {
                    // There is free space from lastOffset to freeSpace2ndTo1stEnd.
                    if (lastOffset < freeSpace2ndTo1stEnd)
                    {
                        const UINT64 unusedRangeSize = freeSpace2ndTo1stEnd - lastOffset;
                        AddDetailedStatisticsUnusedRange(inoutStats, unusedRangeSize);
                    }

                    // End of loop.
                    lastOffset = freeSpace2ndTo1stEnd;
                }
            }
        }

        size_t nextAlloc1stIndex = m_1stNullItemsBeginCount;
        const UINT64 freeSpace1stTo2ndEnd =
            m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK ? suballocations2nd.back().offset : size;
        while (lastOffset < freeSpace1stTo2ndEnd)
        {
            // Find next non-null allocation or move nextAllocIndex to the end.
            while (nextAlloc1stIndex < suballoc1stCount &&
                suballocations1st[nextAlloc1stIndex].privateData == NULL)
            {
                ++nextAlloc1stIndex;
            }

            // Found non-null allocation.
            if (nextAlloc1stIndex < suballoc1stCount)
            {
                const Suballocation& suballoc = suballocations1st[nextAlloc1stIndex];

                // 1. Process free space before this allocation.
                if (lastOffset < suballoc.offset)
                {
                    // There is free space from lastOffset to suballoc.offset.
                    const UINT64 unusedRangeSize = suballoc.offset - lastOffset;
                    AddDetailedStatisticsUnusedRange(inoutStats, unusedRangeSize);
                }

                // 2. Process this allocation.
                // There is allocation with suballoc.offset, suballoc.size.
                AddDetailedStatisticsAllocation(inoutStats, suballoc.size);

                // 3. Prepare for next iteration.
                lastOffset = suballoc.offset + suballoc.size;
                ++nextAlloc1stIndex;
            }
            // We are at the end.
            else
            {
                // There is free space from lastOffset to freeSpace1stTo2ndEnd.
                if (lastOffset < freeSpace1stTo2ndEnd)
                {
                    const UINT64 unusedRangeSize = freeSpace1stTo2ndEnd - lastOffset;
                    AddDetailedStatisticsUnusedRange(inoutStats, unusedRangeSize);
                }

                // End of loop.
                lastOffset = freeSpace1stTo2ndEnd;
            }
        }

        if (m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK)
        {
            size_t nextAlloc2ndIndex = suballocations2nd.size() - 1;
            while (lastOffset < size)
            {
                // Find next non-null allocation or move nextAllocIndex to the end.
                while (nextAlloc2ndIndex != SIZE_MAX &&
                    suballocations2nd[nextAlloc2ndIndex].privateData == NULL)
                {
                    --nextAlloc2ndIndex;
                }

                // Found non-null allocation.
                if (nextAlloc2ndIndex != SIZE_MAX)
                {
                    const Suballocation& suballoc = suballocations2nd[nextAlloc2ndIndex];

                    // 1. Process free space before this allocation.
                    if (lastOffset < suballoc.offset)
                    {
                        // There is free space from lastOffset to suballoc.offset.
                        const UINT64 unusedRangeSize = suballoc.offset - lastOffset;
                        AddDetailedStatisticsUnusedRange(inoutStats, unusedRangeSize);
                    }

                    // 2. Process this allocation.
                    // There is allocation with suballoc.offset, suballoc.size.
                    AddDetailedStatisticsAllocation(inoutStats, suballoc.size);

                    // 3. Prepare for next iteration.
                    lastOffset = suballoc.offset + suballoc.size;
                    --nextAlloc2ndIndex;
                }
                // We are at the end.
                else
                {
                    // There is free space from lastOffset to size.
                    if (lastOffset < size)
                    {
                        const UINT64 unusedRangeSize = size - lastOffset;
                        AddDetailedStatisticsUnusedRange(inoutStats, unusedRangeSize);
                    }

                    // End of loop.
                    lastOffset = size;
                }
            }
        }
    }

    void BlockMetadata_Linear::WriteAllocationInfoToJson(JsonWriter& json) const
    {
        const UINT64 size = GetSize();
        const SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        const SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();
        const size_t suballoc1stCount = suballocations1st.size();
        const size_t suballoc2ndCount = suballocations2nd.size();

        // FIRST PASS

        size_t unusedRangeCount = 0;
        UINT64 usedBytes = 0;

        UINT64 lastOffset = 0;

        size_t alloc2ndCount = 0;
        if (m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER)
        {
            const UINT64 freeSpace2ndTo1stEnd = suballocations1st[m_1stNullItemsBeginCount].offset;
            size_t nextAlloc2ndIndex = 0;
            while (lastOffset < freeSpace2ndTo1stEnd)
            {
                // Find next non-null allocation or move nextAlloc2ndIndex to the end.
                while (nextAlloc2ndIndex < suballoc2ndCount &&
                    suballocations2nd[nextAlloc2ndIndex].privateData == NULL)
                {
                    ++nextAlloc2ndIndex;
                }

                // Found non-null allocation.
                if (nextAlloc2ndIndex < suballoc2ndCount)
                {
                    const Suballocation& suballoc = suballocations2nd[nextAlloc2ndIndex];

                    // 1. Process free space before this allocation.
                    if (lastOffset < suballoc.offset)
                    {
                        // There is free space from lastOffset to suballoc.offset.
                        ++unusedRangeCount;
                    }

                    // 2. Process this allocation.
                    // There is allocation with suballoc.offset, suballoc.size.
                    ++alloc2ndCount;
                    usedBytes += suballoc.size;

                    // 3. Prepare for next iteration.
                    lastOffset = suballoc.offset + suballoc.size;
                    ++nextAlloc2ndIndex;
                }
                // We are at the end.
                else
                {
                    if (lastOffset < freeSpace2ndTo1stEnd)
                    {
                        // There is free space from lastOffset to freeSpace2ndTo1stEnd.
                        ++unusedRangeCount;
                    }

                    // End of loop.
                    lastOffset = freeSpace2ndTo1stEnd;
                }
            }
        }

        size_t nextAlloc1stIndex = m_1stNullItemsBeginCount;
        size_t alloc1stCount = 0;
        const UINT64 freeSpace1stTo2ndEnd =
            m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK ? suballocations2nd.back().offset : size;
        while (lastOffset < freeSpace1stTo2ndEnd)
        {
            // Find next non-null allocation or move nextAllocIndex to the end.
            while (nextAlloc1stIndex < suballoc1stCount &&
                suballocations1st[nextAlloc1stIndex].privateData == NULL)
            {
                ++nextAlloc1stIndex;
            }

            // Found non-null allocation.
            if (nextAlloc1stIndex < suballoc1stCount)
            {
                const Suballocation& suballoc = suballocations1st[nextAlloc1stIndex];

                // 1. Process free space before this allocation.
                if (lastOffset < suballoc.offset)
                {
                    // There is free space from lastOffset to suballoc.offset.
                    ++unusedRangeCount;
                }

                // 2. Process this allocation.
                // There is allocation with suballoc.offset, suballoc.size.
                ++alloc1stCount;
                usedBytes += suballoc.size;

                // 3. Prepare for next iteration.
                lastOffset = suballoc.offset + suballoc.size;
                ++nextAlloc1stIndex;
            }
            // We are at the end.
            else
            {
                if (lastOffset < size)
                {
                    // There is free space from lastOffset to freeSpace1stTo2ndEnd.
                    ++unusedRangeCount;
                }

                // End of loop.
                lastOffset = freeSpace1stTo2ndEnd;
            }
        }

        if (m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK)
        {
            size_t nextAlloc2ndIndex = suballocations2nd.size() - 1;
            while (lastOffset < size)
            {
                // Find next non-null allocation or move nextAlloc2ndIndex to the end.
                while (nextAlloc2ndIndex != SIZE_MAX &&
                    suballocations2nd[nextAlloc2ndIndex].privateData == NULL)
                {
                    --nextAlloc2ndIndex;
                }

                // Found non-null allocation.
                if (nextAlloc2ndIndex != SIZE_MAX)
                {
                    const Suballocation& suballoc = suballocations2nd[nextAlloc2ndIndex];

                    // 1. Process free space before this allocation.
                    if (lastOffset < suballoc.offset)
                    {
                        // There is free space from lastOffset to suballoc.offset.
                        ++unusedRangeCount;
                    }

                    // 2. Process this allocation.
                    // There is allocation with suballoc.offset, suballoc.size.
                    ++alloc2ndCount;
                    usedBytes += suballoc.size;

                    // 3. Prepare for next iteration.
                    lastOffset = suballoc.offset + suballoc.size;
                    --nextAlloc2ndIndex;
                }
                // We are at the end.
                else
                {
                    if (lastOffset < size)
                    {
                        // There is free space from lastOffset to size.
                        ++unusedRangeCount;
                    }

                    // End of loop.
                    lastOffset = size;
                }
            }
        }

        const UINT64 unusedBytes = size - usedBytes;
        PrintDetailedMap_Begin(json, unusedBytes, alloc1stCount + alloc2ndCount, unusedRangeCount);

        // SECOND PASS
        lastOffset = 0;
        if (m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER)
        {
            const UINT64 freeSpace2ndTo1stEnd = suballocations1st[m_1stNullItemsBeginCount].offset;
            size_t nextAlloc2ndIndex = 0;
            while (lastOffset < freeSpace2ndTo1stEnd)
            {
                // Find next non-null allocation or move nextAlloc2ndIndex to the end.
                while (nextAlloc2ndIndex < suballoc2ndCount &&
                    suballocations2nd[nextAlloc2ndIndex].privateData == NULL)
                {
                    ++nextAlloc2ndIndex;
                }

                // Found non-null allocation.
                if (nextAlloc2ndIndex < suballoc2ndCount)
                {
                    const Suballocation& suballoc = suballocations2nd[nextAlloc2ndIndex];

                    // 1. Process free space before this allocation.
                    if (lastOffset < suballoc.offset)
                    {
                        // There is free space from lastOffset to suballoc.offset.
                        const UINT64 unusedRangeSize = suballoc.offset - lastOffset;
                        PrintDetailedMap_UnusedRange(json, lastOffset, unusedRangeSize);
                    }

                    // 2. Process this allocation.
                    // There is allocation with suballoc.offset, suballoc.size.
                    PrintDetailedMap_Allocation(json, suballoc.offset, suballoc.size, suballoc.privateData);

                    // 3. Prepare for next iteration.
                    lastOffset = suballoc.offset + suballoc.size;
                    ++nextAlloc2ndIndex;
                }
                // We are at the end.
                else
                {
                    if (lastOffset < freeSpace2ndTo1stEnd)
                    {
                        // There is free space from lastOffset to freeSpace2ndTo1stEnd.
                        const UINT64 unusedRangeSize = freeSpace2ndTo1stEnd - lastOffset;
                        PrintDetailedMap_UnusedRange(json, lastOffset, unusedRangeSize);
                    }

                    // End of loop.
                    lastOffset = freeSpace2ndTo1stEnd;
                }
            }
        }

        nextAlloc1stIndex = m_1stNullItemsBeginCount;
        while (lastOffset < freeSpace1stTo2ndEnd)
        {
            // Find next non-null allocation or move nextAllocIndex to the end.
            while (nextAlloc1stIndex < suballoc1stCount &&
                suballocations1st[nextAlloc1stIndex].privateData == NULL)
            {
                ++nextAlloc1stIndex;
            }

            // Found non-null allocation.
            if (nextAlloc1stIndex < suballoc1stCount)
            {
                const Suballocation& suballoc = suballocations1st[nextAlloc1stIndex];

                // 1. Process free space before this allocation.
                if (lastOffset < suballoc.offset)
                {
                    // There is free space from lastOffset to suballoc.offset.
                    const UINT64 unusedRangeSize = suballoc.offset - lastOffset;
                    PrintDetailedMap_UnusedRange(json, lastOffset, unusedRangeSize);
                }

                // 2. Process this allocation.
                // There is allocation with suballoc.offset, suballoc.size.
                PrintDetailedMap_Allocation(json, suballoc.offset, suballoc.size, suballoc.privateData);

                // 3. Prepare for next iteration.
                lastOffset = suballoc.offset + suballoc.size;
                ++nextAlloc1stIndex;
            }
            // We are at the end.
            else
            {
                if (lastOffset < freeSpace1stTo2ndEnd)
                {
                    // There is free space from lastOffset to freeSpace1stTo2ndEnd.
                    const UINT64 unusedRangeSize = freeSpace1stTo2ndEnd - lastOffset;
                    PrintDetailedMap_UnusedRange(json, lastOffset, unusedRangeSize);
                }

                // End of loop.
                lastOffset = freeSpace1stTo2ndEnd;
            }
        }

        if (m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK)
        {
            size_t nextAlloc2ndIndex = suballocations2nd.size() - 1;
            while (lastOffset < size)
            {
                // Find next non-null allocation or move nextAlloc2ndIndex to the end.
                while (nextAlloc2ndIndex != SIZE_MAX &&
                    suballocations2nd[nextAlloc2ndIndex].privateData == NULL)
                {
                    --nextAlloc2ndIndex;
                }

                // Found non-null allocation.
                if (nextAlloc2ndIndex != SIZE_MAX)
                {
                    const Suballocation& suballoc = suballocations2nd[nextAlloc2ndIndex];

                    // 1. Process free space before this allocation.
                    if (lastOffset < suballoc.offset)
                    {
                        // There is free space from lastOffset to suballoc.offset.
                        const UINT64 unusedRangeSize = suballoc.offset - lastOffset;
                        PrintDetailedMap_UnusedRange(json, lastOffset, unusedRangeSize);
                    }

                    // 2. Process this allocation.
                    // There is allocation with suballoc.offset, suballoc.size.
                    PrintDetailedMap_Allocation(json, suballoc.offset, suballoc.size, suballoc.privateData);

                    // 3. Prepare for next iteration.
                    lastOffset = suballoc.offset + suballoc.size;
                    --nextAlloc2ndIndex;
                }
                // We are at the end.
                else
                {
                    if (lastOffset < size)
                    {
                        // There is free space from lastOffset to size.
                        const UINT64 unusedRangeSize = size - lastOffset;
                        PrintDetailedMap_UnusedRange(json, lastOffset, unusedRangeSize);
                    }

                    // End of loop.
                    lastOffset = size;
                }
            }
        }

        PrintDetailedMap_End(json);
    }

    void BlockMetadata_Linear::DebugLogAllAllocations() const
    {
        const SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        for (auto it = suballocations1st.begin() + m_1stNullItemsBeginCount; it != suballocations1st.end(); ++it)
            if (it->type != SUBALLOCATION_TYPE_FREE)
                DebugLogAllocation(it->offset, it->size, it->privateData);

        const SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();
        for (auto it = suballocations2nd.begin(); it != suballocations2nd.end(); ++it)
            if (it->type != SUBALLOCATION_TYPE_FREE)
                DebugLogAllocation(it->offset, it->size, it->privateData);
    }

    Suballocation& BlockMetadata_Linear::FindSuballocation(UINT64 offset) const
    {
        const SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        const SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();

        Suballocation refSuballoc;
        refSuballoc.offset = offset;
        // Rest of members stays uninitialized intentionally for better performance.

        // Item from the 1st vector.
        {
            const SuballocationVectorType::const_iterator it = BinaryFindSorted(
                suballocations1st.begin() + m_1stNullItemsBeginCount,
                suballocations1st.end(),
                refSuballoc,
                SuballocationOffsetLess());
            if (it != suballocations1st.end())
            {
                return const_cast<Suballocation&>(*it);
            }
        }

        if (m_2ndVectorMode != SECOND_VECTOR_EMPTY)
        {
            // Rest of members stays uninitialized intentionally for better performance.
            const SuballocationVectorType::const_iterator it = m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER ?
                BinaryFindSorted(suballocations2nd.begin(), suballocations2nd.end(), refSuballoc, SuballocationOffsetLess()) :
                BinaryFindSorted(suballocations2nd.begin(), suballocations2nd.end(), refSuballoc, SuballocationOffsetGreater());
            if (it != suballocations2nd.end())
            {
                return const_cast<Suballocation&>(*it);
            }
        }

        D3D12MA_ASSERT(0 && "Allocation not found in linear allocator!");
        return const_cast<Suballocation&>(suballocations1st.back()); // Should never occur.
    }

    bool BlockMetadata_Linear::ShouldCompact1st() const
    {
        const size_t nullItemCount = m_1stNullItemsBeginCount + m_1stNullItemsMiddleCount;
        const size_t suballocCount = AccessSuballocations1st().size();
        return suballocCount > 32 && nullItemCount * 2 >= (suballocCount - nullItemCount) * 3;
    }

    void BlockMetadata_Linear::CleanupAfterFree()
    {
        SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();

        if (IsEmpty())
        {
            suballocations1st.clear();
            suballocations2nd.clear();
            m_1stNullItemsBeginCount = 0;
            m_1stNullItemsMiddleCount = 0;
            m_2ndNullItemsCount = 0;
            m_2ndVectorMode = SECOND_VECTOR_EMPTY;
        }
        else
        {
            const size_t suballoc1stCount = suballocations1st.size();
            const size_t nullItem1stCount = m_1stNullItemsBeginCount + m_1stNullItemsMiddleCount;
            D3D12MA_ASSERT(nullItem1stCount <= suballoc1stCount);

            // Find more null items at the beginning of 1st vector.
            while (m_1stNullItemsBeginCount < suballoc1stCount &&
                suballocations1st[m_1stNullItemsBeginCount].type == SUBALLOCATION_TYPE_FREE)
            {
                ++m_1stNullItemsBeginCount;
                --m_1stNullItemsMiddleCount;
            }

            // Find more null items at the end of 1st vector.
            while (m_1stNullItemsMiddleCount > 0 &&
                suballocations1st.back().type == SUBALLOCATION_TYPE_FREE)
            {
                --m_1stNullItemsMiddleCount;
                suballocations1st.pop_back();
            }

            // Find more null items at the end of 2nd vector.
            while (m_2ndNullItemsCount > 0 &&
                suballocations2nd.back().type == SUBALLOCATION_TYPE_FREE)
            {
                --m_2ndNullItemsCount;
                suballocations2nd.pop_back();
            }

            // Find more null items at the beginning of 2nd vector.
            while (m_2ndNullItemsCount > 0 &&
                suballocations2nd[0].type == SUBALLOCATION_TYPE_FREE)
            {
                --m_2ndNullItemsCount;
                suballocations2nd.remove(0);
            }

            if (ShouldCompact1st())
            {
                const size_t nonNullItemCount = suballoc1stCount - nullItem1stCount;
                size_t srcIndex = m_1stNullItemsBeginCount;
                for (size_t dstIndex = 0; dstIndex < nonNullItemCount; ++dstIndex)
                {
                    while (suballocations1st[srcIndex].type == SUBALLOCATION_TYPE_FREE)
                    {
                        ++srcIndex;
                    }
                    if (dstIndex != srcIndex)
                    {
                        suballocations1st[dstIndex] = suballocations1st[srcIndex];
                    }
                    ++srcIndex;
                }
                suballocations1st.resize(nonNullItemCount);
                m_1stNullItemsBeginCount = 0;
                m_1stNullItemsMiddleCount = 0;
            }

            // 2nd vector became empty.
            if (suballocations2nd.empty())
            {
                m_2ndVectorMode = SECOND_VECTOR_EMPTY;
            }

            // 1st vector became empty.
            if (suballocations1st.size() - m_1stNullItemsBeginCount == 0)
            {
                suballocations1st.clear();
                m_1stNullItemsBeginCount = 0;

                if (!suballocations2nd.empty() && m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER)
                {
                    // Swap 1st with 2nd. Now 2nd is empty.
                    m_2ndVectorMode = SECOND_VECTOR_EMPTY;
                    m_1stNullItemsMiddleCount = m_2ndNullItemsCount;
                    while (m_1stNullItemsBeginCount < suballocations2nd.size() &&
                        suballocations2nd[m_1stNullItemsBeginCount].type == SUBALLOCATION_TYPE_FREE)
                    {
                        ++m_1stNullItemsBeginCount;
                        --m_1stNullItemsMiddleCount;
                    }
                    m_2ndNullItemsCount = 0;
                    m_1stVectorIndex ^= 1;
                }
            }
        }

        D3D12MA_HEAVY_ASSERT(Validate());
    }

    bool BlockMetadata_Linear::CreateAllocationRequest_LowerAddress(
        UINT64 allocSize,
        UINT64 allocAlignment,
        AllocationRequest* pAllocationRequest)
    {
        const UINT64 blockSize = GetSize();
        SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();

        if (m_2ndVectorMode == SECOND_VECTOR_EMPTY || m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK)
        {
            // Try to allocate at the end of 1st vector.

            UINT64 resultBaseOffset = 0;
            if (!suballocations1st.empty())
            {
                const Suballocation& lastSuballoc = suballocations1st.back();
                resultBaseOffset = lastSuballoc.offset + lastSuballoc.size + GetDebugMargin();
            }

            // Start from offset equal to beginning of free space.
            UINT64 resultOffset = resultBaseOffset;
            // Apply alignment.
            resultOffset = AlignUp(resultOffset, allocAlignment);

            const UINT64 freeSpaceEnd = m_2ndVectorMode == SECOND_VECTOR_DOUBLE_STACK ?
                suballocations2nd.back().offset : blockSize;

            // There is enough free space at the end after alignment.
            if (resultOffset + allocSize + GetDebugMargin() <= freeSpaceEnd)
            {
                // All tests passed: Success.
                pAllocationRequest->allocHandle = (AllocHandle)(resultOffset + 1);
                // pAllocationRequest->item, customData unused.
                pAllocationRequest->algorithmData = ALLOC_REQUEST_END_OF_1ST;
                return true;
            }
        }

        // Wrap-around to end of 2nd vector. Try to allocate there, watching for the
        // beginning of 1st vector as the end of free space.
        if (m_2ndVectorMode == SECOND_VECTOR_EMPTY || m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER)
        {
            D3D12MA_ASSERT(!suballocations1st.empty());

            UINT64 resultBaseOffset = 0;
            if (!suballocations2nd.empty())
            {
                const Suballocation& lastSuballoc = suballocations2nd.back();
                resultBaseOffset = lastSuballoc.offset + lastSuballoc.size + GetDebugMargin();
            }

            // Start from offset equal to beginning of free space.
            UINT64 resultOffset = resultBaseOffset;

            // Apply alignment.
            resultOffset = AlignUp(resultOffset, allocAlignment);

            size_t index1st = m_1stNullItemsBeginCount;
            // There is enough free space at the end after alignment.
            if ((index1st == suballocations1st.size() && resultOffset + allocSize + GetDebugMargin() <= blockSize) ||
                (index1st < suballocations1st.size() && resultOffset + allocSize + GetDebugMargin() <= suballocations1st[index1st].offset))
            {
                // All tests passed: Success.
                pAllocationRequest->allocHandle = (AllocHandle)(resultOffset + 1);
                pAllocationRequest->algorithmData = ALLOC_REQUEST_END_OF_2ND;
                // pAllocationRequest->item, customData unused.
                return true;
            }
        }
        return false;
    }

    bool BlockMetadata_Linear::CreateAllocationRequest_UpperAddress(
        UINT64 allocSize,
        UINT64 allocAlignment,
        AllocationRequest* pAllocationRequest)
    {
        const UINT64 blockSize = GetSize();
        SuballocationVectorType& suballocations1st = AccessSuballocations1st();
        SuballocationVectorType& suballocations2nd = AccessSuballocations2nd();

        if (m_2ndVectorMode == SECOND_VECTOR_RING_BUFFER)
        {
            D3D12MA_ASSERT(0 && "Trying to use pool with linear algorithm as double stack, while it is already being used as ring buffer.");
            return false;
        }

        // Try to allocate before 2nd.back(), or end of block if 2nd.empty().
        if (allocSize > blockSize)
        {
            return false;
        }
        UINT64 resultBaseOffset = blockSize - allocSize;
        if (!suballocations2nd.empty())
        {
            const Suballocation& lastSuballoc = suballocations2nd.back();
            resultBaseOffset = lastSuballoc.offset - allocSize;
            if (allocSize > lastSuballoc.offset)
            {
                return false;
            }
        }

        // Start from offset equal to end of free space.
        UINT64 resultOffset = resultBaseOffset;
        // Apply debugMargin at the end.
        if (GetDebugMargin() > 0)
        {
            if (resultOffset < GetDebugMargin())
            {
                return false;
            }
            resultOffset -= GetDebugMargin();
        }

        // Apply alignment.
        resultOffset = AlignDown(resultOffset, allocAlignment);
        // There is enough free space.
        const UINT64 endOf1st = !suballocations1st.empty() ?
            suballocations1st.back().offset + suballocations1st.back().size : 0;

        if (endOf1st + GetDebugMargin() <= resultOffset)
        {
            // All tests passed: Success.
            pAllocationRequest->allocHandle = (AllocHandle)(resultOffset + 1);
            // pAllocationRequest->item unused.
            pAllocationRequest->algorithmData = ALLOC_REQUEST_UPPER_ADDRESS;
            return true;
        }
        return false;
    }
#endif // _D3D12MA_BLOCK_METADATA_LINEAR_FUNCTIONS
#endif // _D3D12MA_BLOCK_METADATA_LINEAR

#ifndef _D3D12MA_BLOCK_METADATA_TLSF
    class BlockMetadata_TLSF : public BlockMetadata
    {
    public:
        BlockMetadata_TLSF(const AllocationCallbacks* allocationCallbacks, bool isVirtual);
        virtual ~BlockMetadata_TLSF();

        size_t GetAllocationCount() const override { return m_AllocCount; }
        size_t GetFreeRegionsCount() const override { return m_BlocksFreeCount + 1; }
        UINT64 GetSumFreeSize() const override { return m_BlocksFreeSize + m_NullBlock->size; }
        bool IsEmpty() const override { return m_NullBlock->offset == 0; }
        UINT64 GetAllocationOffset(AllocHandle allocHandle) const override { return ((Block*)allocHandle)->offset; };

        void Init(UINT64 size) override;
        bool Validate() const override;
        void GetAllocationInfo(AllocHandle allocHandle, VirtualAllocationInfo& outInfo) const override;

        bool CreateAllocationRequest(
            UINT64 allocSize,
            UINT64 allocAlignment,
            bool upperAddress,
            UINT32 strategy,
            AllocationRequest* pAllocationRequest) override;

        void Alloc(
            const AllocationRequest& request,
            UINT64 allocSize,
            void* privateData) override;

        void Free(AllocHandle allocHandle) override;
        void Clear() override;

        AllocHandle GetAllocationListBegin() const override;
        AllocHandle GetNextAllocation(AllocHandle prevAlloc) const override;
        UINT64 GetNextFreeRegionSize(AllocHandle alloc) const override;
        void* GetAllocationPrivateData(AllocHandle allocHandle) const override;
        void SetAllocationPrivateData(AllocHandle allocHandle, void* privateData) override;

        void AddStatistics(Statistics& inoutStats) const override;
        void AddDetailedStatistics(DetailedStatistics& inoutStats) const override;
        void WriteAllocationInfoToJson(JsonWriter& json) const override;
        void DebugLogAllAllocations() const override;

    private:
        // According to original paper it should be preferable 4 or 5:
        // M. Masmano, I. Ripoll, A. Crespo, and J. Real "TLSF: a New Dynamic Memory Allocator for Real-Time Systems"
        // http://www.gii.upv.es/tlsf/files/ecrts04_tlsf.pdf
        static const UINT8 SECOND_LEVEL_INDEX = 5;
        static const UINT16 SMALL_BUFFER_SIZE = 256;
        static const UINT INITIAL_BLOCK_ALLOC_COUNT = 16;
        static const UINT8 MEMORY_CLASS_SHIFT = 7;
        static const UINT8 MAX_MEMORY_CLASSES = 65 - MEMORY_CLASS_SHIFT;

        class Block
        {
        public:
            UINT64 offset;
            UINT64 size;
            Block* prevPhysical;
            Block* nextPhysical;

            void MarkFree() { prevFree = NULL; }
            void MarkTaken() { prevFree = this; }
            bool IsFree() const { return prevFree != this; }
            void*& PrivateData() { D3D12MA_HEAVY_ASSERT(!IsFree()); return privateData; }
            Block*& PrevFree() { return prevFree; }
            Block*& NextFree() { D3D12MA_HEAVY_ASSERT(IsFree()); return nextFree; }

        private:
            Block* prevFree; // Address of the same block here indicates that block is taken
            union
            {
                Block* nextFree;
                void* privateData;
            };
        };

        size_t m_AllocCount = 0;
        // Total number of free blocks besides null block
        size_t m_BlocksFreeCount = 0;
        // Total size of free blocks excluding null block
        UINT64 m_BlocksFreeSize = 0;
        UINT32 m_IsFreeBitmap = 0;
        UINT8 m_MemoryClasses = 0;
        UINT32 m_InnerIsFreeBitmap[MAX_MEMORY_CLASSES];
        UINT32 m_ListsCount = 0;
        /*
        * 0: 0-3 lists for small buffers
        * 1+: 0-(2^SLI-1) lists for normal buffers
        */
        Block** m_FreeList = NULL;
        PoolAllocator<Block> m_BlockAllocator;
        Block* m_NullBlock = NULL;

        UINT8 SizeToMemoryClass(UINT64 size) const;
        UINT16 SizeToSecondIndex(UINT64 size, UINT8 memoryClass) const;
        UINT32 GetListIndex(UINT8 memoryClass, UINT16 secondIndex) const;
        UINT32 GetListIndex(UINT64 size) const;

        void RemoveFreeBlock(Block* block);
        void InsertFreeBlock(Block* block);
        void MergeBlock(Block* block, Block* prev);

        Block* FindFreeBlock(UINT64 size, UINT32& listIndex) const;
        bool CheckBlock(
            Block& block,
            UINT32 listIndex,
            UINT64 allocSize,
            UINT64 allocAlignment,
            AllocationRequest* pAllocationRequest);

        D3D12MA_CLASS_NO_COPY(BlockMetadata_TLSF)
    };

#ifndef _D3D12MA_BLOCK_METADATA_TLSF_FUNCTIONS
    BlockMetadata_TLSF::BlockMetadata_TLSF(const AllocationCallbacks* allocationCallbacks, bool isVirtual)
        : BlockMetadata(allocationCallbacks, isVirtual),
        m_BlockAllocator(*allocationCallbacks, INITIAL_BLOCK_ALLOC_COUNT)
    {
        D3D12MA_ASSERT(allocationCallbacks);
    }

    BlockMetadata_TLSF::~BlockMetadata_TLSF()
    {
        D3D12MA_DELETE_ARRAY(*GetAllocs(), m_FreeList, m_ListsCount);
    }

    void BlockMetadata_TLSF::Init(UINT64 size)
    {
        BlockMetadata::Init(size);

        m_NullBlock = m_BlockAllocator.Alloc();
        m_NullBlock->size = size;
        m_NullBlock->offset = 0;
        m_NullBlock->prevPhysical = NULL;
        m_NullBlock->nextPhysical = NULL;
        m_NullBlock->MarkFree();
        m_NullBlock->NextFree() = NULL;
        m_NullBlock->PrevFree() = NULL;
        UINT8 memoryClass = SizeToMemoryClass(size);
        UINT16 sli = SizeToSecondIndex(size, memoryClass);
        m_ListsCount = (memoryClass == 0 ? 0 : (memoryClass - 1) * (1UL << SECOND_LEVEL_INDEX) + sli) + 1;
        if (IsVirtual())
            m_ListsCount += 1UL << SECOND_LEVEL_INDEX;
        else
            m_ListsCount += 4;

        m_MemoryClasses = memoryClass + 2;
        memset(m_InnerIsFreeBitmap, 0, MAX_MEMORY_CLASSES * sizeof(UINT32));

        m_FreeList = D3D12MA_NEW_ARRAY(*GetAllocs(), Block*, m_ListsCount);
        memset(m_FreeList, 0, m_ListsCount * sizeof(Block*));
    }

    bool BlockMetadata_TLSF::Validate() const
    {
        D3D12MA_VALIDATE(GetSumFreeSize() <= GetSize());

        UINT64 calculatedSize = m_NullBlock->size;
        UINT64 calculatedFreeSize = m_NullBlock->size;
        size_t allocCount = 0;
        size_t freeCount = 0;

        // Check integrity of free lists
        for (UINT32 list = 0; list < m_ListsCount; ++list)
        {
            Block* block = m_FreeList[list];
            if (block != NULL)
            {
                D3D12MA_VALIDATE(block->IsFree());
                D3D12MA_VALIDATE(block->PrevFree() == NULL);
                while (block->NextFree())
                {
                    D3D12MA_VALIDATE(block->NextFree()->IsFree());
                    D3D12MA_VALIDATE(block->NextFree()->PrevFree() == block);
                    block = block->NextFree();
                }
            }
        }

        D3D12MA_VALIDATE(m_NullBlock->nextPhysical == NULL);
        if (m_NullBlock->prevPhysical)
        {
            D3D12MA_VALIDATE(m_NullBlock->prevPhysical->nextPhysical == m_NullBlock);
        }

        // Check all blocks
        UINT64 nextOffset = m_NullBlock->offset;
        for (Block* prev = m_NullBlock->prevPhysical; prev != NULL; prev = prev->prevPhysical)
        {
            D3D12MA_VALIDATE(prev->offset + prev->size == nextOffset);
            nextOffset = prev->offset;
            calculatedSize += prev->size;

            UINT32 listIndex = GetListIndex(prev->size);
            if (prev->IsFree())
            {
                ++freeCount;
                // Check if free block belongs to free list
                Block* freeBlock = m_FreeList[listIndex];
                D3D12MA_VALIDATE(freeBlock != NULL);

                bool found = false;
                do
                {
                    if (freeBlock == prev)
                        found = true;

                    freeBlock = freeBlock->NextFree();
                } while (!found && freeBlock != NULL);

                D3D12MA_VALIDATE(found);
                calculatedFreeSize += prev->size;
            }
            else
            {
                ++allocCount;
                // Check if taken block is not on a free list
                Block* freeBlock = m_FreeList[listIndex];
                while (freeBlock)
                {
                    D3D12MA_VALIDATE(freeBlock != prev);
                    freeBlock = freeBlock->NextFree();
                }
            }

            if (prev->prevPhysical)
            {
                D3D12MA_VALIDATE(prev->prevPhysical->nextPhysical == prev);
            }
        }

        D3D12MA_VALIDATE(nextOffset == 0);
        D3D12MA_VALIDATE(calculatedSize == GetSize());
        D3D12MA_VALIDATE(calculatedFreeSize == GetSumFreeSize());
        D3D12MA_VALIDATE(allocCount == m_AllocCount);
        D3D12MA_VALIDATE(freeCount == m_BlocksFreeCount);

        return true;
    }

    void BlockMetadata_TLSF::GetAllocationInfo(AllocHandle allocHandle, VirtualAllocationInfo& outInfo) const
    {
        Block* block = (Block*)allocHandle;
        D3D12MA_ASSERT(!block->IsFree() && "Cannot get allocation info for free block!");
        outInfo.offset = block->offset;
        outInfo.size = block->size;
        outInfo.privateData = block->PrivateData();
    }

    bool BlockMetadata_TLSF::CreateAllocationRequest(
        UINT64 allocSize,
        UINT64 allocAlignment,
        bool upperAddress,
        UINT32 strategy,
        AllocationRequest* pAllocationRequest)
    {
        D3D12MA_ASSERT(allocSize > 0 && "Cannot allocate empty block!");
        D3D12MA_ASSERT(!upperAddress && "ALLOCATION_FLAG_UPPER_ADDRESS can be used only with linear algorithm.");
        D3D12MA_ASSERT(pAllocationRequest != NULL);
        D3D12MA_HEAVY_ASSERT(Validate());

        allocSize += GetDebugMargin();
        // Quick check for too small pool
        if (allocSize > GetSumFreeSize())
            return false;

        // If no free blocks in pool then check only null block
        if (m_BlocksFreeCount == 0)
            return CheckBlock(*m_NullBlock, m_ListsCount, allocSize, allocAlignment, pAllocationRequest);

        // Round up to the next block
        UINT64 sizeForNextList = allocSize;
        UINT16 smallSizeStep = SMALL_BUFFER_SIZE / (IsVirtual() ? 1 << SECOND_LEVEL_INDEX : 4);
        if (allocSize > SMALL_BUFFER_SIZE)
        {
            sizeForNextList += (1ULL << (BitScanMSB(allocSize) - SECOND_LEVEL_INDEX));
        }
        else if (allocSize > SMALL_BUFFER_SIZE - smallSizeStep)
            sizeForNextList = SMALL_BUFFER_SIZE + 1;
        else
            sizeForNextList += smallSizeStep;

        UINT32 nextListIndex = 0;
        UINT32 prevListIndex = 0;
        Block* nextListBlock = NULL;
        Block* prevListBlock = NULL;

        // Check blocks according to strategies
        if ((strategy & AllocationFlagStrategyMinTime) != 0)
        {
            // Quick check for larger block first
            nextListBlock = FindFreeBlock(sizeForNextList, nextListIndex);
            if (nextListBlock != NULL && CheckBlock(*nextListBlock, nextListIndex, allocSize, allocAlignment, pAllocationRequest))
                return true;

            // If not fitted then null block
            if (CheckBlock(*m_NullBlock, m_ListsCount, allocSize, allocAlignment, pAllocationRequest))
                return true;

            // Null block failed, search larger bucket
            while (nextListBlock)
            {
                if (CheckBlock(*nextListBlock, nextListIndex, allocSize, allocAlignment, pAllocationRequest))
                    return true;
                nextListBlock = nextListBlock->NextFree();
            }

            // Failed again, check best fit bucket
            prevListBlock = FindFreeBlock(allocSize, prevListIndex);
            while (prevListBlock)
            {
                if (CheckBlock(*prevListBlock, prevListIndex, allocSize, allocAlignment, pAllocationRequest))
                    return true;
                prevListBlock = prevListBlock->NextFree();
            }
        }
        else if ((strategy & AllocationFlagStrategyMinMemory) != 0)
        {
            // Check best fit bucket
            prevListBlock = FindFreeBlock(allocSize, prevListIndex);
            while (prevListBlock)
            {
                if (CheckBlock(*prevListBlock, prevListIndex, allocSize, allocAlignment, pAllocationRequest))
                    return true;
                prevListBlock = prevListBlock->NextFree();
            }

            // If failed check null block
            if (CheckBlock(*m_NullBlock, m_ListsCount, allocSize, allocAlignment, pAllocationRequest))
                return true;

            // Check larger bucket
            nextListBlock = FindFreeBlock(sizeForNextList, nextListIndex);
            while (nextListBlock)
            {
                if (CheckBlock(*nextListBlock, nextListIndex, allocSize, allocAlignment, pAllocationRequest))
                    return true;
                nextListBlock = nextListBlock->NextFree();
            }
        }
        else if ((strategy & AllocationFlagStrategyMinOffset) != 0)
        {
            // Perform search from the start
            Vector<Block*> blockList(m_BlocksFreeCount, *GetAllocs());

            size_t i = m_BlocksFreeCount;
            for (Block* block = m_NullBlock->prevPhysical; block != NULL; block = block->prevPhysical)
            {
                if (block->IsFree() && block->size >= allocSize)
                    blockList[--i] = block;
            }

            for (; i < m_BlocksFreeCount; ++i)
            {
                Block& block = *blockList[i];
                if (CheckBlock(block, GetListIndex(block.size), allocSize, allocAlignment, pAllocationRequest))
                    return true;
            }

            // If failed check null block
            if (CheckBlock(*m_NullBlock, m_ListsCount, allocSize, allocAlignment, pAllocationRequest))
                return true;

            // Whole range searched, no more memory
            return false;
        }
        else
        {
            // Check larger bucket
            nextListBlock = FindFreeBlock(sizeForNextList, nextListIndex);
            while (nextListBlock)
            {
                if (CheckBlock(*nextListBlock, nextListIndex, allocSize, allocAlignment, pAllocationRequest))
                    return true;
                nextListBlock = nextListBlock->NextFree();
            }

            // If failed check null block
            if (CheckBlock(*m_NullBlock, m_ListsCount, allocSize, allocAlignment, pAllocationRequest))
                return true;

            // Check best fit bucket
            prevListBlock = FindFreeBlock(allocSize, prevListIndex);
            while (prevListBlock)
            {
                if (CheckBlock(*prevListBlock, prevListIndex, allocSize, allocAlignment, pAllocationRequest))
                    return true;
                prevListBlock = prevListBlock->NextFree();
            }
        }

        // Worst case, full search has to be done
        while (++nextListIndex < m_ListsCount)
        {
            nextListBlock = m_FreeList[nextListIndex];
            while (nextListBlock)
            {
                if (CheckBlock(*nextListBlock, nextListIndex, allocSize, allocAlignment, pAllocationRequest))
                    return true;
                nextListBlock = nextListBlock->NextFree();
            }
        }

        // No more memory sadly
        return false;
    }

    void BlockMetadata_TLSF::Alloc(
        const AllocationRequest& request,
        UINT64 allocSize,
        void* privateData)
    {
        // Get block and pop it from the free list
        Block* currentBlock = (Block*)request.allocHandle;
        UINT64 offset = request.algorithmData;
        D3D12MA_ASSERT(currentBlock != NULL);
        D3D12MA_ASSERT(currentBlock->offset <= offset);

        if (currentBlock != m_NullBlock)
            RemoveFreeBlock(currentBlock);

        // Append missing alignment to prev block or create new one
        UINT64 misssingAlignment = offset - currentBlock->offset;
        if (misssingAlignment)
        {
            Block* prevBlock = currentBlock->prevPhysical;
            D3D12MA_ASSERT(prevBlock != NULL && "There should be no missing alignment at offset 0!");

            if (prevBlock->IsFree() && prevBlock->size != GetDebugMargin())
            {
                UINT32 oldList = GetListIndex(prevBlock->size);
                prevBlock->size += misssingAlignment;
                // Check if new size crosses list bucket
                if (oldList != GetListIndex(prevBlock->size))
                {
                    prevBlock->size -= misssingAlignment;
                    RemoveFreeBlock(prevBlock);
                    prevBlock->size += misssingAlignment;
                    InsertFreeBlock(prevBlock);
                }
                else
                    m_BlocksFreeSize += misssingAlignment;
            }
            else
            {
                Block* newBlock = m_BlockAllocator.Alloc();
                currentBlock->prevPhysical = newBlock;
                prevBlock->nextPhysical = newBlock;
                newBlock->prevPhysical = prevBlock;
                newBlock->nextPhysical = currentBlock;
                newBlock->size = misssingAlignment;
                newBlock->offset = currentBlock->offset;
                newBlock->MarkTaken();

                InsertFreeBlock(newBlock);
            }

            currentBlock->size -= misssingAlignment;
            currentBlock->offset += misssingAlignment;
        }

        UINT64 size = request.size + GetDebugMargin();
        if (currentBlock->size == size)
        {
            if (currentBlock == m_NullBlock)
            {
                // Setup new null block
                m_NullBlock = m_BlockAllocator.Alloc();
                m_NullBlock->size = 0;
                m_NullBlock->offset = currentBlock->offset + size;
                m_NullBlock->prevPhysical = currentBlock;
                m_NullBlock->nextPhysical = NULL;
                m_NullBlock->MarkFree();
                m_NullBlock->PrevFree() = NULL;
                m_NullBlock->NextFree() = NULL;
                currentBlock->nextPhysical = m_NullBlock;
                currentBlock->MarkTaken();
            }
        }
        else
        {
            D3D12MA_ASSERT(currentBlock->size > size && "Proper block already found, shouldn't find smaller one!");

            // Create new free block
            Block* newBlock = m_BlockAllocator.Alloc();
            newBlock->size = currentBlock->size - size;
            newBlock->offset = currentBlock->offset + size;
            newBlock->prevPhysical = currentBlock;
            newBlock->nextPhysical = currentBlock->nextPhysical;
            currentBlock->nextPhysical = newBlock;
            currentBlock->size = size;

            if (currentBlock == m_NullBlock)
            {
                m_NullBlock = newBlock;
                m_NullBlock->MarkFree();
                m_NullBlock->NextFree() = NULL;
                m_NullBlock->PrevFree() = NULL;
                currentBlock->MarkTaken();
            }
            else
            {
                newBlock->nextPhysical->prevPhysical = newBlock;
                newBlock->MarkTaken();
                InsertFreeBlock(newBlock);
            }
        }
        currentBlock->PrivateData() = privateData;

        if (GetDebugMargin() > 0)
        {
            currentBlock->size -= GetDebugMargin();
            Block* newBlock = m_BlockAllocator.Alloc();
            newBlock->size = GetDebugMargin();
            newBlock->offset = currentBlock->offset + currentBlock->size;
            newBlock->prevPhysical = currentBlock;
            newBlock->nextPhysical = currentBlock->nextPhysical;
            newBlock->MarkTaken();
            currentBlock->nextPhysical->prevPhysical = newBlock;
            currentBlock->nextPhysical = newBlock;
            InsertFreeBlock(newBlock);
        }
        ++m_AllocCount;
    }

    void BlockMetadata_TLSF::Free(AllocHandle allocHandle)
    {
        Block* block = (Block*)allocHandle;
        Block* next = block->nextPhysical;
        D3D12MA_ASSERT(!block->IsFree() && "Block is already free!");

        --m_AllocCount;
        if (GetDebugMargin() > 0)
        {
            RemoveFreeBlock(next);
            MergeBlock(next, block);
            block = next;
            next = next->nextPhysical;
        }

        // Try merging
        Block* prev = block->prevPhysical;
        if (prev != NULL && prev->IsFree() && prev->size != GetDebugMargin())
        {
            RemoveFreeBlock(prev);
            MergeBlock(block, prev);
        }

        if (!next->IsFree())
            InsertFreeBlock(block);
        else if (next == m_NullBlock)
            MergeBlock(m_NullBlock, block);
        else
        {
            RemoveFreeBlock(next);
            MergeBlock(next, block);
            InsertFreeBlock(next);
        }
    }

    void BlockMetadata_TLSF::Clear()
    {
        m_AllocCount = 0;
        m_BlocksFreeCount = 0;
        m_BlocksFreeSize = 0;
        m_IsFreeBitmap = 0;
        m_NullBlock->offset = 0;
        m_NullBlock->size = GetSize();
        Block* block = m_NullBlock->prevPhysical;
        m_NullBlock->prevPhysical = NULL;
        while (block)
        {
            Block* prev = block->prevPhysical;
            m_BlockAllocator.Free(block);
            block = prev;
        }
        memset(m_FreeList, 0, m_ListsCount * sizeof(Block*));
        memset(m_InnerIsFreeBitmap, 0, m_MemoryClasses * sizeof(UINT32));
    }

    AllocHandle BlockMetadata_TLSF::GetAllocationListBegin() const
    {
        if (m_AllocCount == 0)
            return (AllocHandle)0;

        for (Block* block = m_NullBlock->prevPhysical; block; block = block->prevPhysical)
        {
            if (!block->IsFree())
                return (AllocHandle)block;
        }
        D3D12MA_ASSERT(false && "If m_AllocCount > 0 then should find any allocation!");
        return (AllocHandle)0;
    }

    AllocHandle BlockMetadata_TLSF::GetNextAllocation(AllocHandle prevAlloc) const
    {
        Block* startBlock = (Block*)prevAlloc;
        D3D12MA_ASSERT(!startBlock->IsFree() && "Incorrect block!");

        for (Block* block = startBlock->prevPhysical; block; block = block->prevPhysical)
        {
            if (!block->IsFree())
                return (AllocHandle)block;
        }
        return (AllocHandle)0;
    }

    UINT64 BlockMetadata_TLSF::GetNextFreeRegionSize(AllocHandle alloc) const
    {
        Block* block = (Block*)alloc;
        D3D12MA_ASSERT(!block->IsFree() && "Incorrect block!");

        if (block->prevPhysical)
            return block->prevPhysical->IsFree() ? block->prevPhysical->size : 0;
        return 0;
    }

    void* BlockMetadata_TLSF::GetAllocationPrivateData(AllocHandle allocHandle) const
    {
        Block* block = (Block*)allocHandle;
        D3D12MA_ASSERT(!block->IsFree() && "Cannot get user data for free block!");
        return block->PrivateData();
    }

    void BlockMetadata_TLSF::SetAllocationPrivateData(AllocHandle allocHandle, void* privateData)
    {
        Block* block = (Block*)allocHandle;
        D3D12MA_ASSERT(!block->IsFree() && "Trying to set user data for not allocated block!");
        block->PrivateData() = privateData;
    }

    void BlockMetadata_TLSF::AddStatistics(Statistics& inoutStats) const
    {
        inoutStats.blockCount++;
        inoutStats.allocationCount += static_cast<UINT>(m_AllocCount);
        inoutStats.blockBytes += GetSize();
        inoutStats.allocationBytes += GetSize() - GetSumFreeSize();
    }

    void BlockMetadata_TLSF::AddDetailedStatistics(DetailedStatistics& inoutStats) const
    {
        inoutStats.stats.blockCount++;
        inoutStats.stats.blockBytes += GetSize();

        for (Block* block = m_NullBlock->prevPhysical; block != NULL; block = block->prevPhysical)
        {
            if (block->IsFree())
                AddDetailedStatisticsUnusedRange(inoutStats, block->size);
            else
                AddDetailedStatisticsAllocation(inoutStats, block->size);
        }

        if (m_NullBlock->size > 0)
            AddDetailedStatisticsUnusedRange(inoutStats, m_NullBlock->size);
    }

    void BlockMetadata_TLSF::WriteAllocationInfoToJson(JsonWriter& json) const
    {
        size_t blockCount = m_AllocCount + m_BlocksFreeCount;
        Vector<Block*> blockList(blockCount, *GetAllocs());

        size_t i = blockCount;
        if (m_NullBlock->size > 0)
        {
            ++blockCount;
            blockList.push_back(m_NullBlock);
        }
        for (Block* block = m_NullBlock->prevPhysical; block != NULL; block = block->prevPhysical)
        {
            blockList[--i] = block;
        }
        D3D12MA_ASSERT(i == 0);

        PrintDetailedMap_Begin(json, GetSumFreeSize(), GetAllocationCount(), m_BlocksFreeCount +
            (m_NullBlock->size > 0 ? 1 : 0));
        for (; i < blockCount; ++i)
        {
            Block* block = blockList[i];
            if (block->IsFree())
                PrintDetailedMap_UnusedRange(json, block->offset, block->size);
            else
                PrintDetailedMap_Allocation(json, block->offset, block->size, block->PrivateData());
        }
        PrintDetailedMap_End(json);
    }

    void BlockMetadata_TLSF::DebugLogAllAllocations() const
    {
        for (Block* block = m_NullBlock->prevPhysical; block != NULL; block = block->prevPhysical)
        {
            if (!block->IsFree())
            {
                DebugLogAllocation(block->offset, block->size, block->PrivateData());
            }
        }
    }

    UINT8 BlockMetadata_TLSF::SizeToMemoryClass(UINT64 size) const
    {
        if (size > SMALL_BUFFER_SIZE)
            return BitScanMSB(size) - MEMORY_CLASS_SHIFT;
        return 0;
    }

    UINT16 BlockMetadata_TLSF::SizeToSecondIndex(UINT64 size, UINT8 memoryClass) const
    {
        if (memoryClass == 0)
        {
            if (IsVirtual())
                return static_cast<UINT16>((size - 1) / 8);
            else
                return static_cast<UINT16>((size - 1) / 64);
        }
        return static_cast<UINT16>((size >> (memoryClass + MEMORY_CLASS_SHIFT - SECOND_LEVEL_INDEX)) ^ (1U << SECOND_LEVEL_INDEX));
    }

    UINT32 BlockMetadata_TLSF::GetListIndex(UINT8 memoryClass, UINT16 secondIndex) const
    {
        if (memoryClass == 0)
            return secondIndex;

        const UINT32 index = static_cast<UINT32>(memoryClass - 1) * (1 << SECOND_LEVEL_INDEX) + secondIndex;
        if (IsVirtual())
            return index + (1 << SECOND_LEVEL_INDEX);
        else
            return index + 4;
    }

    UINT32 BlockMetadata_TLSF::GetListIndex(UINT64 size) const
    {
        UINT8 memoryClass = SizeToMemoryClass(size);
        return GetListIndex(memoryClass, SizeToSecondIndex(size, memoryClass));
    }

    void BlockMetadata_TLSF::RemoveFreeBlock(Block* block)
    {
        D3D12MA_ASSERT(block != m_NullBlock);
        D3D12MA_ASSERT(block->IsFree());

        if (block->NextFree() != NULL)
            block->NextFree()->PrevFree() = block->PrevFree();
        if (block->PrevFree() != NULL)
            block->PrevFree()->NextFree() = block->NextFree();
        else
        {
            UINT8 memClass = SizeToMemoryClass(block->size);
            UINT16 secondIndex = SizeToSecondIndex(block->size, memClass);
            UINT32 index = GetListIndex(memClass, secondIndex);
            m_FreeList[index] = block->NextFree();
            if (block->NextFree() == NULL)
            {
                m_InnerIsFreeBitmap[memClass] &= ~(1U << secondIndex);
                if (m_InnerIsFreeBitmap[memClass] == 0)
                    m_IsFreeBitmap &= ~(1UL << memClass);
            }
        }
        block->MarkTaken();
        block->PrivateData() = NULL;
        --m_BlocksFreeCount;
        m_BlocksFreeSize -= block->size;
    }

    void BlockMetadata_TLSF::InsertFreeBlock(Block* block)
    {
        D3D12MA_ASSERT(block != m_NullBlock);
        D3D12MA_ASSERT(!block->IsFree() && "Cannot insert block twice!");

        UINT8 memClass = SizeToMemoryClass(block->size);
        UINT16 secondIndex = SizeToSecondIndex(block->size, memClass);
        UINT32 index = GetListIndex(memClass, secondIndex);
        block->PrevFree() = NULL;
        block->NextFree() = m_FreeList[index];
        m_FreeList[index] = block;
        if (block->NextFree() != NULL)
            block->NextFree()->PrevFree() = block;
        else
        {
            m_InnerIsFreeBitmap[memClass] |= 1U << secondIndex;
            m_IsFreeBitmap |= 1UL << memClass;
        }
        ++m_BlocksFreeCount;
        m_BlocksFreeSize += block->size;
    }

    void BlockMetadata_TLSF::MergeBlock(Block* block, Block* prev)
    {
        D3D12MA_ASSERT(block->prevPhysical == prev && "Cannot merge seperate physical regions!");
        D3D12MA_ASSERT(!prev->IsFree() && "Cannot merge block that belongs to free list!");

        block->offset = prev->offset;
        block->size += prev->size;
        block->prevPhysical = prev->prevPhysical;
        if (block->prevPhysical)
            block->prevPhysical->nextPhysical = block;
        m_BlockAllocator.Free(prev);
    }

    BlockMetadata_TLSF::Block* BlockMetadata_TLSF::FindFreeBlock(UINT64 size, UINT32& listIndex) const
    {
        UINT8 memoryClass = SizeToMemoryClass(size);
        UINT32 innerFreeMap = m_InnerIsFreeBitmap[memoryClass] & (~0U << SizeToSecondIndex(size, memoryClass));
        if (!innerFreeMap)
        {
            // Check higher levels for avaiable blocks
            UINT32 freeMap = m_IsFreeBitmap & (~0UL << (memoryClass + 1));
            if (!freeMap)
                return NULL; // No more memory avaible

            // Find lowest free region
            memoryClass = BitScanLSB(freeMap);
            innerFreeMap = m_InnerIsFreeBitmap[memoryClass];
            D3D12MA_ASSERT(innerFreeMap != 0);
        }
        // Find lowest free subregion
        listIndex = GetListIndex(memoryClass, BitScanLSB(innerFreeMap));
        return m_FreeList[listIndex];
    }

    bool BlockMetadata_TLSF::CheckBlock(
        Block& block,
        UINT32 listIndex,
        UINT64 allocSize,
        UINT64 allocAlignment,
        AllocationRequest* pAllocationRequest)
    {
        D3D12MA_ASSERT(block.IsFree() && "Block is already taken!");

        UINT64 alignedOffset = AlignUp(block.offset, allocAlignment);
        if (block.size < allocSize + alignedOffset - block.offset)
            return false;

        // Alloc successful
        pAllocationRequest->allocHandle = (AllocHandle)&block;
        pAllocationRequest->size = allocSize - GetDebugMargin();
        pAllocationRequest->algorithmData = alignedOffset;

        // Place block at the start of list if it's normal block
        if (listIndex != m_ListsCount && block.PrevFree())
        {
            block.PrevFree()->NextFree() = block.NextFree();
            if (block.NextFree())
                block.NextFree()->PrevFree() = block.PrevFree();
            block.PrevFree() = NULL;
            block.NextFree() = m_FreeList[listIndex];
            m_FreeList[listIndex] = &block;
            if (block.NextFree())
                block.NextFree()->PrevFree() = &block;
        }

        return true;
    }
#endif // _D3D12MA_BLOCK_METADATA_TLSF_FUNCTIONS
#endif // _D3D12MA_BLOCK_METADATA_TLSF


#ifndef _D3D12MA_MEMORY_BLOCK

    // Represents a single block of device memory (heap).
    // Base class for inheritance.
    // Thread-safety: This class must be externally synchronized.

    class MemoryBlock
    {
    public:
        // Creates the ID3D12Heap.
        MemoryBlock(
            AllocatorPimpl* allocator,
            const HeapProperties& heapProps,
            HeapFlags heapFlags,
            UINT64 size,
            UINT id);
        virtual ~MemoryBlock();

        const HeapProperties& GetHeapProperties() const { return m_HeapProps; }
        HeapFlags GetHeapFlags() const { return m_HeapFlags; }
        UINT64 GetSize() const { return m_Size; }
        UINT GetId() const { return m_Id; }
        HeapHandle GetHeap() const { return m_Heap->GetHandle(); }

    protected:
        AllocatorPimpl* const m_Allocator;
        const HeapProperties m_HeapProps;
        const HeapFlags m_HeapFlags;
        const UINT64 m_Size;
        const UINT m_Id;

        Result Init(bool denyMsaaTextures);

    private:
        HeapPtr m_Heap;

        D3D12MA_CLASS_NO_COPY(MemoryBlock)
    };
#endif // _D3D12MA_MEMORY_BLOCK

#ifndef _D3D12MA_NORMAL_BLOCK
    /*
    Represents a single block of device memory (heap) with all the data about its
    regions (aka suballocations, Allocation), assigned and free.
    Thread-safety: This class must be externally synchronized.
    */
    class NormalBlock : public MemoryBlock
    {
    public:
        BlockMetadata* m_pMetadata;

        NormalBlock(
            AllocatorPimpl* allocator,
            BlockVector* blockVector,
            const HeapProperties& heapProps,
            HeapFlags heapFlags,
            UINT64 size,
            UINT id);
        virtual ~NormalBlock();

        BlockVector* GetBlockVector() const { return m_BlockVector; }

        // 'algorithm' should be one of the *_ALGORITHM_* flags in enums POOL_FLAGS or VIRTUAL_BLOCK_FLAGS
        Result Init(UINT32 algorithm, bool denyMsaaTextures);

        // Validates all data structures inside this object. If not valid, returns false.
        bool Validate() const;

    private:
        BlockVector* m_BlockVector;

        D3D12MA_CLASS_NO_COPY(NormalBlock)
    };
#endif // _D3D12MA_NORMAL_BLOCK

#ifndef _D3D12MA_COMMITTED_ALLOCATION_LIST_ITEM_TRAITS
    struct CommittedAllocationListItemTraits
    {
        using ItemType = Allocation;

        static ItemType* GetPrev(const ItemType* item)
        {
            D3D12MA_ASSERT(item->m_PackedData.GetType() == Allocation::TYPE_COMMITTED || item->m_PackedData.GetType() == Allocation::TYPE_HEAP);
            return item->m_Committed.prev;
        }
        static ItemType* GetNext(const ItemType* item)
        {
            D3D12MA_ASSERT(item->m_PackedData.GetType() == Allocation::TYPE_COMMITTED || item->m_PackedData.GetType() == Allocation::TYPE_HEAP);
            return item->m_Committed.next;
        }
        static ItemType*& AccessPrev(ItemType* item)
        {
            D3D12MA_ASSERT(item->m_PackedData.GetType() == Allocation::TYPE_COMMITTED || item->m_PackedData.GetType() == Allocation::TYPE_HEAP);
            return item->m_Committed.prev;
        }
        static ItemType*& AccessNext(ItemType* item)
        {
            D3D12MA_ASSERT(item->m_PackedData.GetType() == Allocation::TYPE_COMMITTED || item->m_PackedData.GetType() == Allocation::TYPE_HEAP);
            return item->m_Committed.next;
        }
    };
#endif // _D3D12MA_COMMITTED_ALLOCATION_LIST_ITEM_TRAITS

#ifndef _D3D12MA_COMMITTED_ALLOCATION_LIST
    /*
    Stores linked list of Allocation objects that are of TYPE_COMMITTED or TYPE_HEAP.
    Thread-safe, synchronized internally.
    */
    class CommittedAllocationList
    {
    public:
        CommittedAllocationList() = default;
        void Init(bool useMutex, HeapType heapType, PoolPimpl* pool);
        ~CommittedAllocationList();

        [[nodiscard]] HeapType GetHeapType() const { return m_HeapType; }
        [[nodiscard]] PoolPimpl* GetPool() const { return m_Pool; }
        UINT GetMemorySegmentGroup(AllocatorPimpl* allocator) const;

        void AddStatistics(Statistics& inoutStats);
        void AddDetailedStatistics(DetailedStatistics& inoutStats);
        // Writes JSON array with the list of allocations.
        void BuildStatsString(JsonWriter& json);

        void Register(Allocation* alloc);
        void Unregister(Allocation* alloc);

    private:
        using CommittedAllocationLinkedList = IntrusiveLinkedList<CommittedAllocationListItemTraits>;

        bool m_UseMutex = true;
        HeapType m_HeapType = HeapType::Custom;
        PoolPimpl* m_Pool = nullptr;

        D3D12MA_RW_MUTEX m_Mutex;
        CommittedAllocationLinkedList m_AllocationList;
    };
#endif // _D3D12MA_COMMITTED_ALLOCATION_LIST

#ifndef _D3D12M_COMMITTED_ALLOCATION_PARAMETERS
    struct CommittedAllocationParameters
    {
        CommittedAllocationList* m_List = nullptr;
        HeapProperties m_HeapProperties = {};
        HeapFlags m_HeapFlags = HeapFlags::None;
        //ID3D12ProtectedResourceSession* m_ProtectedSession = NULL;
        bool m_CanAlias = false;
        ResidencyPriority m_ResidencyPriority = D3D12_RESIDENCY_PRIORITY_NONE;

        [[nodiscard]] bool IsValid() const { return m_List != nullptr; }
    };
#endif // _D3D12M_COMMITTED_ALLOCATION_PARAMETERS

    struct CreateResourceParams
    {
        CreateResourceParams() = delete;
        //#ifdef __ID3D12Device10_INTERFACE_DEFINED__
        CreateResourceParams(
            const ResourceDesc* pResourceDesc,
            ResourceLayout InitialLayout,
            const ClearValue* pOptimizedClearValue,
            UINT32 NumCastableFormats,
            const Format* pCastableFormats)
            : pResourceDesc(pResourceDesc)
            , initialLayout(InitialLayout)
            , pOptimizedClearValue(pOptimizedClearValue)
            , numCastableFormats(NumCastableFormats)
            , pCastableFormats(pCastableFormats)
        {
        }
        [[nodiscard]] const ClearValue* GetOptimizedClearValue() const
        {
            return pOptimizedClearValue;
        }
        [[nodiscard]] const ResourceDesc* GetResourceDesc() const
        {
            return pResourceDesc;
        }
        const ResourceDesc*& AccessResourceDesc()
        {
            return pResourceDesc;
        }
        [[nodiscard]] ResourceLayout GetInitialLayout() const
        {
            return initialLayout;
        }
        [[nodiscard]] UINT32 GetNumCastableFormats() const
        {
            return numCastableFormats;
        }
        [[nodiscard]] const Format* GetCastableFormats() const
        {
            return pCastableFormats;
        }
    private:
        const ResourceDesc* pResourceDesc;
        ResourceLayout initialLayout;
        const ClearValue* pOptimizedClearValue;
        UINT32 numCastableFormats;
        const Format* pCastableFormats;
    };
    //#endif

#ifndef _D3D12MA_BLOCK_VECTOR
/*
Sequence of NormalBlock. Represents memory blocks allocated for a specific
heap type and possibly resource type (if only Tier 1 is supported).

Synchronized internally with a mutex.
*/
    class BlockVector
    {
        friend class DefragmentationContextPimpl;
        D3D12MA_CLASS_NO_COPY(BlockVector)
    public:
        BlockVector(
            AllocatorPimpl* hAllocator,
            const HeapProperties& heapProps,
            HeapFlags heapFlags,
            UINT64 preferredBlockSize,
            size_t minBlockCount,
            size_t maxBlockCount,
            bool explicitBlockSize,
            UINT64 minAllocationAlignment,
            UINT32 algorithm,
            bool denyMsaaTextures,
            //ID3D12ProtectedResourceSession* pProtectedSession,
            ResidencyPriority residencyPriority);
        ~BlockVector();
        [[nodiscard]] ResidencyPriority GetResidencyPriority() const { return m_ResidencyPriority; }

        [[nodiscard]] const HeapProperties& GetHeapProperties() const { return m_HeapProps; }
        [[nodiscard]] HeapFlags GetHeapFlags() const { return m_HeapFlags; }
        [[nodiscard]] UINT64 GetPreferredBlockSize() const { return m_PreferredBlockSize; }
        [[nodiscard]] UINT32 GetAlgorithm() const { return m_Algorithm; }
        [[nodiscard]] bool DeniesMsaaTextures() const { return m_DenyMsaaTextures; }
        // To be used only while the m_Mutex is locked. Used during defragmentation.
        [[nodiscard]] size_t GetBlockCount() const { return m_Blocks.size(); }
        // To be used only while the m_Mutex is locked. Used during defragmentation.
        [[nodiscard]] NormalBlock* GetBlock(size_t index) const { return m_Blocks[index]; }
        D3D12MA_RW_MUTEX& GetMutex() { return m_Mutex; }

        Result CreateMinBlocks();
        bool IsEmpty();

        Result Allocate(
            UINT64 size,
            UINT64 alignment,
            const AllocationDesc& allocDesc,
            bool committedAllowed,
            size_t allocationCount,
            Allocation** pAllocations);

        void Free(Allocation* hAllocation);

        Result CreateResource(
            UINT64 size,
            UINT64 alignment,
            const AllocationDesc& allocDesc,
            const CreateResourceParams& createParams,
            bool committedAllowed,
            Allocation** ppOutAllocation,
            ResourcePtr ptr);

        void AddStatistics(Statistics& inoutStats);
        void AddDetailedStatistics(DetailedStatistics& inoutStats);

        void WriteBlockInfoToJson(JsonWriter& json);

    private:
        AllocatorPimpl* const m_hAllocator;
        const HeapProperties m_HeapProps;
        const HeapFlags m_HeapFlags;
        const UINT64 m_PreferredBlockSize;
        const size_t m_MinBlockCount;
        const size_t m_MaxBlockCount;
        const bool m_ExplicitBlockSize;
        const UINT64 m_MinAllocationAlignment;
        const UINT32 m_Algorithm;
        const bool m_DenyMsaaTextures;
        //ID3D12ProtectedResourceSession* const m_ProtectedSession;
        const ResidencyPriority m_ResidencyPriority;
        /* There can be at most one allocation that is completely empty - a
        hysteresis to avoid pessimistic case of alternating creation and destruction
        of a ID3D12Heap. */
        bool m_HasEmptyBlock;
        D3D12MA_RW_MUTEX m_Mutex;
        // Incrementally sorted by sumFreeSize, ascending.
        Vector<NormalBlock*> m_Blocks;
        UINT m_NextBlockId;
        bool m_IncrementalSort = true;

        // Disable incremental sorting when freeing allocations
        void SetIncrementalSort(bool val) { m_IncrementalSort = val; }

        UINT64 CalcSumBlockSize() const;
        UINT64 CalcMaxBlockSize() const;

        // Finds and removes given block from vector.
        void Remove(NormalBlock* pBlock);

        // Performs single step in sorting m_Blocks. They may not be fully sorted
        // after this call.
        void IncrementallySortBlocks();
        void SortByFreeSize();

        Result AllocatePage(
            UINT64 size,
            UINT64 alignment,
            const AllocationDesc& allocDesc,
            bool committedAllowed,
            Allocation** pAllocation);

        Result AllocateFromBlock(
            NormalBlock* pBlock,
            UINT64 size,
            UINT64 alignment,
            AllocationFlags allocFlags,
            void* pPrivateData,
            UINT32 strategy,
            Allocation** pAllocation);

        Result CommitAllocationRequest(
            AllocationRequest& allocRequest,
            NormalBlock* pBlock,
            UINT64 size,
            UINT64 alignment,
            void* pPrivateData,
            Allocation** pAllocation);

        Result CreateBlock(
            UINT64 blockSize,
            size_t* pNewBlockIndex);
    };
#endif // _D3D12MA_BLOCK_VECTOR

#ifndef _D3D12MA_CURRENT_BUDGET_DATA
    class CurrentBudgetData
    {
    public:
        bool ShouldUpdateBudget() const { return m_OperationsSinceBudgetFetch >= 30; }

        void GetStatistics(Statistics& outStats, UINT group) const;
        void GetBudget(bool useMutex,
            UINT64* outLocalUsage, UINT64* outLocalBudget,
            UINT64* outNonLocalUsage, UINT64* outNonLocalBudget);

        Result UpdateBudget(const Device& d, bool useMutex);

        void AddAllocation(UINT group, UINT64 allocationBytes);
        void RemoveAllocation(UINT group, UINT64 allocationBytes);

        void AddBlock(UINT group, UINT64 blockBytes);
        void RemoveBlock(UINT group, UINT64 blockBytes);

    private:
        D3D12MA_ATOMIC_UINT32 m_BlockCount[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count)] = {};
        D3D12MA_ATOMIC_UINT32 m_AllocationCount[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count)] = {};
        D3D12MA_ATOMIC_UINT64 m_BlockBytes[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count)] = {};
        D3D12MA_ATOMIC_UINT64 m_AllocationBytes[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count)] = {};

        D3D12MA_ATOMIC_UINT32 m_OperationsSinceBudgetFetch = { 0 };
        D3D12MA_RW_MUTEX m_BudgetMutex;
        UINT64 m_D3D12Usage[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count)] = {};
        UINT64 m_D3D12Budget[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count)] = {};
        UINT64 m_BlockBytesAtD3D12Fetch[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count)] = {};
    };

#ifndef _D3D12MA_CURRENT_BUDGET_DATA_FUNCTIONS
    void CurrentBudgetData::GetStatistics(Statistics& outStats, UINT group) const
    {
        outStats.blockCount = m_BlockCount[group];
        outStats.allocationCount = m_AllocationCount[group];
        outStats.blockBytes = m_BlockBytes[group];
        outStats.allocationBytes = m_AllocationBytes[group];
    }

    void CurrentBudgetData::GetBudget(bool useMutex,
        UINT64* outLocalUsage, UINT64* outLocalBudget,
        UINT64* outNonLocalUsage, UINT64* outNonLocalBudget)
    {
        MutexLockRead lockRead(m_BudgetMutex, useMutex);

        if (outLocalUsage)
        {
            const UINT64 D3D12Usage = m_D3D12Usage[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local)];
            const UINT64 blockBytes = m_BlockBytes[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local)];
            const UINT64 blockBytesAtD3D12Fetch = m_BlockBytesAtD3D12Fetch[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local)];
            *outLocalUsage = D3D12Usage + blockBytes > blockBytesAtD3D12Fetch ?
                D3D12Usage + blockBytes - blockBytesAtD3D12Fetch : 0;
        }
        if (outLocalBudget)
            *outLocalBudget = m_D3D12Budget[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local)];

        if (outNonLocalUsage)
        {
            const UINT64 D3D12Usage = m_D3D12Usage[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::NonLocal)];
            const UINT64 blockBytes = m_BlockBytes[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::NonLocal)];
            const UINT64 blockBytesAtD3D12Fetch = m_BlockBytesAtD3D12Fetch[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::NonLocal)];
            *outNonLocalUsage = D3D12Usage + blockBytes > blockBytesAtD3D12Fetch ?
                D3D12Usage + blockBytes - blockBytesAtD3D12Fetch : 0;
        }
        if (outNonLocalBudget) {
            *outNonLocalBudget = m_D3D12Budget[static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::NonLocal)];
        }
    }

    Result CurrentBudgetData::UpdateBudget(const Device& d, bool useMutex)
    {

        //DXGI_QUERY_VIDEO_MEMORY_INFO infoLocal = {};
        //DXGI_QUERY_VIDEO_MEMORY_INFO infoNonLocal = {};
        //const Result hrLocal = adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &infoLocal);
        //if (Failed(hrLocal))
        //{
        //    return hrLocal;
        //}
        //const Result hrNonLocal = adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &infoNonLocal);
        //if (Failed(hrNonLocal))
        //{
        //    return hrNonLocal;
        //}
        VideoMemoryInfo infoLocal;
        if (const Result q = d.QueryVideoMemoryInfo(0, rhi::MemorySegmentGroup::Local, infoLocal); Failed(q)) {
            return q;
        }
		VideoMemoryInfo infoNonLocal;
        if (const Result qn = d.QueryVideoMemoryInfo(0, rhi::MemorySegmentGroup::NonLocal, infoNonLocal); Failed(qn)) {
            return qn;
		}

        {
            MutexLockWrite lockWrite(m_BudgetMutex, useMutex);

            m_D3D12Usage[0] = infoLocal.currentUsageBytes;
            m_D3D12Budget[0] = infoLocal.budgetBytes;

            m_D3D12Usage[1] = infoNonLocal.currentUsageBytes;
            m_D3D12Budget[1] = infoNonLocal.budgetBytes;

            m_BlockBytesAtD3D12Fetch[0] = m_BlockBytes[0];
            m_BlockBytesAtD3D12Fetch[1] = m_BlockBytes[1];
            m_OperationsSinceBudgetFetch = 0;
        }

        return Result::Ok;
    }

    void CurrentBudgetData::AddAllocation(UINT group, UINT64 allocationBytes)
    {
        ++m_AllocationCount[group];
        m_AllocationBytes[group] += allocationBytes;
        ++m_OperationsSinceBudgetFetch;
    }

    void CurrentBudgetData::RemoveAllocation(UINT group, UINT64 allocationBytes)
    {
        D3D12MA_ASSERT(m_AllocationBytes[group] >= allocationBytes);
        D3D12MA_ASSERT(m_AllocationCount[group] > 0);
        m_AllocationBytes[group] -= allocationBytes;
        --m_AllocationCount[group];
        ++m_OperationsSinceBudgetFetch;
    }

    void CurrentBudgetData::AddBlock(UINT group, UINT64 blockBytes)
    {
        ++m_BlockCount[group];
        m_BlockBytes[group] += blockBytes;
        ++m_OperationsSinceBudgetFetch;
    }

    void CurrentBudgetData::RemoveBlock(UINT group, UINT64 blockBytes)
    {
        D3D12MA_ASSERT(m_BlockBytes[group] >= blockBytes);
        D3D12MA_ASSERT(m_BlockCount[group] > 0);
        m_BlockBytes[group] -= blockBytes;
        --m_BlockCount[group];
        ++m_OperationsSinceBudgetFetch;
    }
#endif // _D3D12MA_CURRENT_BUDGET_DATA_FUNCTIONS
#endif // _D3D12MA_CURRENT_BUDGET_DATA

#ifndef _D3D12MA_DEFRAGMENTATION_CONTEXT_PIMPL
    class DefragmentationContextPimpl
    {
        D3D12MA_CLASS_NO_COPY(DefragmentationContextPimpl)
    public:
        DefragmentationContextPimpl(
            AllocatorPimpl* hAllocator,
            const DefragmentationDesc& desc,
            BlockVector* poolVector);
        ~DefragmentationContextPimpl();

        void GetStats(DefragmentationStats& outStats) { outStats = m_GlobalStats; }
        const AllocationCallbacks& GetAllocs() const { return m_Moves.GetAllocs(); }

        Result DefragmentPassBegin(DefragmentationPassMoveInfo& moveInfo);
        Result DefragmentPassEnd(DefragmentationPassMoveInfo& moveInfo);

    private:
        // Max number of allocations to ignore due to size constraints before ending single pass
        static const UINT8 MAX_ALLOCS_TO_IGNORE = 16;
        enum class CounterStatus { Pass, Ignore, End };

        struct FragmentedBlock
        {
            UINT32 data;
            NormalBlock* block;
        };
        struct StateBalanced
        {
            UINT64 avgFreeSize = 0;
            UINT64 avgAllocSize = UINT64_MAX;
        };
        struct MoveAllocationData
        {
            UINT64 size;
            UINT64 alignment;
            AllocationFlags flags;
            DefragmentationMove move = {};
        };

        const UINT64 m_MaxPassBytes;
        const UINT32 m_MaxPassAllocations;

        Vector<DefragmentationMove> m_Moves;

        UINT8 m_IgnoredAllocs = 0;
        UINT32 m_Algorithm;
        UINT32 m_BlockVectorCount;
        BlockVector* m_PoolBlockVector;
        BlockVector** m_pBlockVectors;
        size_t m_ImmovableBlockCount = 0;
        DefragmentationStats m_GlobalStats = { 0 };
        DefragmentationStats m_PassStats = { 0 };
        void* m_AlgorithmState = NULL;

        static MoveAllocationData GetMoveData(AllocHandle handle, BlockMetadata* metadata);
        CounterStatus CheckCounters(UINT64 bytes);
        bool IncrementCounters(UINT64 bytes);
        bool ReallocWithinBlock(BlockVector& vector, NormalBlock* block);
        bool AllocInOtherBlock(size_t start, size_t end, MoveAllocationData& data, BlockVector& vector);

        bool ComputeDefragmentation(BlockVector& vector, size_t index);
        bool ComputeDefragmentation_Fast(BlockVector& vector);
        bool ComputeDefragmentation_Balanced(BlockVector& vector, size_t index, bool update);
        bool ComputeDefragmentation_Full(BlockVector& vector);

        void UpdateVectorStatistics(BlockVector& vector, StateBalanced& state);
    };
#endif // _D3D12MA_DEFRAGMENTATION_CONTEXT_PIMPL
#ifndef _D3D12MA_POOL_PIMPL
    class PoolPimpl
    {
        friend class Allocator;
        friend struct PoolListItemTraits;
    public:
        PoolPimpl(AllocatorPimpl* allocator, const PoolDesc& desc);
        ~PoolPimpl();

        [[nodiscard]] AllocatorPimpl* GetAllocator() const { return m_Allocator; }
        [[nodiscard]] const PoolDesc& GetDesc() const { return m_Desc; }
        [[nodiscard]] bool AlwaysCommitted() const { return (m_Desc.flags & PoolFlagsAlwaysCommitted) != PoolFlagsNone; }
        [[nodiscard]] bool SupportsCommittedAllocations() const { return m_Desc.blockSize == 0; }
        [[nodiscard]] const char* GetName() const { return m_Name; }

        BlockVector* GetBlockVector() { return m_BlockVector; }
        CommittedAllocationList* GetCommittedAllocationList() { return SupportsCommittedAllocations() ? &m_CommittedAllocations : nullptr; }

        Result Init();
        void GetStatistics(Statistics& outStats);
        void CalculateStatistics(DetailedStatistics& outStats);
        void AddDetailedStatistics(DetailedStatistics& inoutStats);
        void SetName(const char* Name);

    private:
        AllocatorPimpl* m_Allocator; // Externally owned object.
        PoolDesc m_Desc;
        BlockVector* m_BlockVector; // Owned object.
        CommittedAllocationList m_CommittedAllocations;
        char* m_Name;
        PoolPimpl* m_PrevPool = nullptr;
        PoolPimpl* m_NextPool = nullptr;

        void FreeName();
    };

    struct PoolListItemTraits
    {
        using ItemType = PoolPimpl;
        static ItemType* GetPrev(const ItemType* item) { return item->m_PrevPool; }
        static ItemType* GetNext(const ItemType* item) { return item->m_NextPool; }
        static ItemType*& AccessPrev(ItemType* item) { return item->m_PrevPool; }
        static ItemType*& AccessNext(ItemType* item) { return item->m_NextPool; }
    };
#endif // _D3D12MA_POOL_PIMPL


#ifndef _D3D12MA_ALLOCATOR_PIMPL
    class AllocatorPimpl
    {
        friend class Allocator;
        friend class Pool;
    public:
        std::atomic_uint32_t m_RefCount = { 1 };
        CurrentBudgetData m_Budget;

        AllocatorPimpl(const AllocationCallbacks& allocationCallbacks, const AllocatorDesc& desc);
        ~AllocatorPimpl();

        const Device& GetDevice() const { return m_Device; }
//#ifdef __ID3D12Device12_INTERFACE_DEFINED__ // TODO?
//        ID3D12Device12* GetDevice12() const { return m_Device12; }
//#endif
        // Shortcut for "Allocation Callbacks", because this function is called so often.
        const AllocationCallbacks& GetAllocs() const { return m_AllocationCallbacks; }
        //const D3D12_FEATURE_DATA_D3D12_OPTIONS& GetD3D12Options() const { return m_D3D12Options; }
        BOOL IsUMA() const { return m_UMA; }
        BOOL IsCacheCoherentUMA() const { return m_CacheCoherentUMA; }
        //bool SupportsResourceHeapTier2() const { return m_D3D12Options.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2; }
        bool IsGPUUploadHeapSupported() const { return m_GPUUploadHeapSupported != FALSE; }
        bool IsTightAlignmentSupported() const { return m_TightAlignmentSupported != FALSE; }
        bool IsTightAlignmentEnabled() const { return IsTightAlignmentSupported() && m_UseTightAlignment; }
        bool UseMutex() const { return m_UseMutex; }
        AllocationObjectAllocator& GetAllocationObjectAllocator() { return m_AllocationObjectAllocator; }
        UINT GetCurrentFrameIndex() const { return m_CurrentFrameIndex.load(); }
        /*
        If SupportsResourceHeapTier2():
            0: D3D12_HEAP_TYPE_DEFAULT
            1: D3D12_HEAP_TYPE_UPLOAD
            2: D3D12_HEAP_TYPE_READBACK
            3: D3D12_HEAP_TYPE_GPU_UPLOAD
        else:
            0: D3D12_HEAP_TYPE_DEFAULT + buffer
            1: D3D12_HEAP_TYPE_DEFAULT + texture
            2: D3D12_HEAP_TYPE_DEFAULT + texture RT or DS
            3: D3D12_HEAP_TYPE_UPLOAD + buffer
            4: D3D12_HEAP_TYPE_UPLOAD + texture
            5: D3D12_HEAP_TYPE_UPLOAD + texture RT or DS
            6: D3D12_HEAP_TYPE_READBACK + buffer
            7: D3D12_HEAP_TYPE_READBACK + texture
            8: D3D12_HEAP_TYPE_READBACK + texture RT or DS
            9: D3D12_HEAP_TYPE_GPU_UPLOAD + buffer
            10: D3D12_HEAP_TYPE_GPU_UPLOAD + texture
            11: D3D12_HEAP_TYPE_GPU_UPLOAD + texture RT or DS
        */
        UINT GetDefaultPoolCount() const
        {
            return 4; // TODO
	        //return SupportsResourceHeapTier2() ? 4 : 12;
        }
        BlockVector** GetDefaultPools() { return m_BlockVectors; }

        Result Init(const AllocatorDesc& desc);
        bool HeapFlagsFulfillResourceHeapTier(HeapFlags flags) const;
        UINT StandardHeapTypeToMemorySegmentGroup(HeapType heapType) const;
        UINT HeapPropertiesToMemorySegmentGroup(const HeapProperties& heapProps) const;
        UINT64 GetMemoryCapacity(MemorySegmentGroup memorySegmentGroup) const;

        Result CreatePlacedResourceWrap(
            HeapHandle pHeap,
            UINT64 HeapOffset,
            const CreateResourceParams& createParams,
            ResourcePtr& out);

        Result CreateResource(
            const AllocationDesc* pAllocDesc,
            const CreateResourceParams& createParams,
            Allocation** ppAllocation,
            ResourcePtr out);

        Result CreateAliasingResource(
            Allocation* pAllocation,
            UINT64 AllocationLocalOffset,
            const CreateResourceParams& createParams,
            ResourcePtr& out);

        Result AllocateMemory(
            const AllocationDesc* pAllocDesc,
            const ResourceAllocationInfo* pAllocInfo,
            Allocation** ppAllocation);

        // Unregisters allocation from the collection of dedicated allocations.
        // Allocation object must be deleted externally afterwards.
        void FreeCommittedMemory(Allocation* allocation);
        // Unregisters allocation from the collection of placed allocations.
        // Allocation object must be deleted externally afterwards.
        void FreePlacedMemory(Allocation* allocation);
        // Unregisters allocation from the collection of dedicated allocations and destroys associated heap.
        // Allocation object must be deleted externally afterwards.
        void FreeHeapMemory(Allocation* allocation);

        void SetResidencyPriority(PageableRef obj, ResidencyPriority priority) const;

        void SetCurrentFrameIndex(UINT frameIndex);
        // For more deailed stats use outCustomHeaps to access statistics divided into L0 and L1 group
        void CalculateStatistics(TotalStatistics& outStats, DetailedStatistics outCustomHeaps[2] = NULL);

        void GetBudget(Budget* outLocalBudget, Budget* outNonLocalBudget);
        void GetBudgetForHeapType(Budget& outBudget, HeapType heapType);

        void BuildStatsString(char** ppStatsString, BOOL detailedMap);
        void FreeStatsString(char* pStatsString);

    private:
        using PoolList = IntrusiveLinkedList<PoolListItemTraits>;

        const bool m_UseMutex;
        const bool m_AlwaysCommitted;
        const bool m_MsaaAlwaysCommitted;
        bool m_PreferSmallBuffersCommitted;
        const bool m_UseTightAlignment;
        bool m_DefaultPoolsNotZeroed = false;
		bool m_UnifiedResourceHeaps = false;
        bool m_UMA = false;
		bool m_CacheCoherentUMA = false;
        bool m_tileBasedRenderer = false;
        AdapterFeatureInfo m_adapterFeatureInfo = {};
        Device m_Device;
        UINT64 m_PreferredBlockSize;
        AllocationCallbacks m_AllocationCallbacks;
        D3D12MA_ATOMIC_UINT32 m_CurrentFrameIndex;
        //DXGI_ADAPTER_DESC m_AdapterDesc;
        //D3D12_FEATURE_DATA_D3D12_OPTIONS m_D3D12Options;
        BOOL m_GPUUploadHeapSupported = FALSE;
        BOOL m_TightAlignmentSupported = FALSE;
        //D3D12_FEATURE_DATA_ARCHITECTURE m_D3D12Architecture;
        AllocationObjectAllocator m_AllocationObjectAllocator;

        D3D12MA_RW_MUTEX m_PoolsMutex[HEAP_TYPE_COUNT];
        PoolList m_Pools[HEAP_TYPE_COUNT];
        // Default pools.
        BlockVector* m_BlockVectors[DEFAULT_POOL_MAX_COUNT];
        CommittedAllocationList m_CommittedAllocations[STANDARD_HEAP_TYPE_COUNT];

        /*
        Heuristics that decides whether a resource should better be placed in its own,
        dedicated allocation (committed resource rather than placed resource).
        */
        //template<typename D3D12_RESOURCE_DESC_T>
        bool PrefersCommittedAllocation(const ResourceDesc& resourceDesc,
            AllocationFlags strategy);

        // Allocates and registers new committed resource with implicit heap, as dedicated allocation.
        // Creates and returns Allocation object and optionally D3D12 resource.
        Result AllocateCommittedResource(
            const CommittedAllocationParameters& committedAllocParams,
            UINT64 resourceSize, bool withinBudget, void* pPrivateData,
            const CreateResourceParams& createParams,
            Allocation** ppOutAllocation,
            ResourcePtr ptr);

        // Allocates and registers new heap without any resources placed in it, as dedicated allocation.
        // Creates and returns Allocation object.
        Result AllocateHeap(
            const CommittedAllocationParameters& committedAllocParams,
            const ResourceAllocationInfo& allocInfo, bool withinBudget,
            void* pPrivateData, Allocation** ppAllocation);

        //template<typename D3D12_RESOURCE_DESC_T>
        Result CalcAllocationParams(const AllocationDesc& allocDesc, UINT64 allocSize,
            const ResourceDesc* resDesc, // Optional
            BlockVector*& outBlockVector, CommittedAllocationParameters& outCommittedAllocationParams, bool& outPreferCommitted);

        // Returns UINT32_MAX if index cannot be calculcated.
        UINT CalcDefaultPoolIndex(const AllocationDesc& allocDesc, ResourceClass resourceClass) const;
        void CalcDefaultPoolParams(HeapType& outHeapType, HeapFlags& outHeapFlags, UINT index) const;

        // Registers Pool object in m_Pools.
        void RegisterPool(Pool* pool, HeapType heapType);
        // Unregisters Pool object from m_Pools.
        void UnregisterPool(Pool* pool, HeapType heapType);

        Result UpdateD3D12Budget();

        ResourceAllocationInfo GetResourceAllocationInfoNative(const ResourceDesc& resourceDesc) const;
        Result GetResourceAllocationInfoMiddle(ResourceDesc& inOutResourceDesc,
            UINT32 NumCastableFormats, const Format* pCastableFormats,
            ResourceAllocationInfo& outAllocInfo) const;

#ifdef __ID3D12Device8_INTERFACE_DEFINED__
        D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo2Native(const D3D12_RESOURCE_DESC1& resourceDesc) const;
        Result GetResourceAllocationInfoMiddle(D3D12_RESOURCE_DESC1& inOutResourceDesc,
            UINT32 NumCastableFormats, const DXGI_FORMAT* pCastableFormats,
            D3D12_RESOURCE_ALLOCATION_INFO& outAllocInfo) const;
#endif

#ifdef __ID3D12Device12_INTERFACE_DEFINED__
        D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo3Native(const D3D12_RESOURCE_DESC1& resourceDesc,
            UINT32 NumCastableFormats, const DXGI_FORMAT* pCastableFormats) const;
#endif

        Result GetResourceAllocationInfo(ResourceDesc& inOutResourceDesc,
            UINT32 NumCastableFormats, const Format* pCastableFormats,
            ResourceAllocationInfo& outAllocInfo) const;

        bool NewAllocationWithinBudget(HeapType heapType, UINT64 size);

        // Writes object { } with data of given budget.
        static void WriteBudgetToJson(JsonWriter& json, const Budget& budget);
    };
#ifndef _D3D12MA_ALLOCATOR_PIMPL_FUNCTINOS
    AllocatorPimpl::AllocatorPimpl(const AllocationCallbacks& allocationCallbacks, const AllocatorDesc& desc)
        : m_UseMutex((desc.flags& AllocatorFlags::SingleThreaded) == AllocatorFlags::None),
        m_AlwaysCommitted((desc.flags& AllocatorFlags::AlwaysCommitted) != AllocatorFlags::None),
        m_MsaaAlwaysCommitted((desc.flags& AllocatorFlags::MsaaTexturesAlwaysCommitted) != AllocatorFlags::None),
        m_PreferSmallBuffersCommitted((desc.flags& AllocatorFlags::DontPreferSmallBuffersCommitted) == AllocatorFlags::None),
        m_UseTightAlignment((desc.flags& AllocatorFlags::DontUseTightAlignment) == AllocatorFlags::None),
        m_Device(desc.device),
        //m_Adapter(desc.nativeAdapter),
        m_PreferredBlockSize(desc.preferredBlockSize != 0 ? desc.preferredBlockSize : D3D12MA_DEFAULT_BLOCK_SIZE),
        m_AllocationCallbacks(allocationCallbacks),
        m_CurrentFrameIndex(0),
        // Below this line don't use allocationCallbacks but m_AllocationCallbacks!!!
        m_AllocationObjectAllocator(m_AllocationCallbacks, m_UseMutex)
    {
        // desc.pAllocationCallbacks intentionally ignored here, preprocessed by CreateAllocator.
        //ZeroMemory(&m_D3D12Options, sizeof(m_D3D12Options));
        //ZeroMemory(&m_D3D12Architecture, sizeof(m_D3D12Architecture));

        ZeroMemory(m_BlockVectors, sizeof(m_BlockVectors));

        for (UINT i = 0; i < STANDARD_HEAP_TYPE_COUNT; ++i)
        {
            m_CommittedAllocations[i].Init(
                m_UseMutex,
                IndexToStandardHeapType(i),
                NULL); // pool
        }

        //m_Device->AddRef();
        //m_Adapter->AddRef();
    }

    Result AllocatorPimpl::Init(const AllocatorDesc& desc)
    {
        // RHI device is the only "native" thing we keep now.
        m_Device = desc.device;
        if (!m_Device) {
            return Result::InvalidArgument;
        }

        // --- Query everything we need in one shot ---
        AdapterFeatureInfo        adapter{};
        ArchitectureFeatureInfo   arch{};
        ShaderFeatureInfo         shader{};
        MeshShaderFeatureInfo     mesh{};
        RayTracingFeatureInfo     rt{};
        ShadingRateFeatureInfo    vrs{};
        EnhancedBarriersFeatureInfo eb{};
        ResourceAllocationFeatureInfo      allocCaps{};

        adapter.header.pNext = &arch.header;
        arch.header.pNext = &shader.header;
        shader.header.pNext = &mesh.header;
        mesh.header.pNext = &rt.header;
        rt.header.pNext = &vrs.header;
        vrs.header.pNext = &eb.header;
        eb.header.pNext = &allocCaps.header;

        const Result q = m_Device.QueryFeatureInfo(&adapter.header);
        if (q != Result::Ok) {
            return q;
        }

		m_adapterFeatureInfo = adapter;
        //m_AdapterDesc.VendorId = adapter.vendorId;
        //m_AdapterDesc.DeviceId = adapter.deviceId;

        // D3D12_OPTIONS replacements
        // D3D12MA uses ResourceHeapTier heavily; in RHI we turned that into a semantic bool.
        m_UnifiedResourceHeaps = shader.unifiedResourceHeaps;

#ifdef D3D12MA_FORCE_RESOURCE_HEAP_TIER
        // If FORCE == 2 -> unified heaps; else not.
        m_UnifiedResourceHeaps = (D3D12MA_FORCE_RESOURCE_HEAP_TIER == 2);
#endif

        // GPU upload heaps (OPTIONS16)
        m_GPUUploadHeapSupported = allocCaps.gpuUploadHeapSupported;

        // Tight alignment
        m_TightAlignmentSupported = allocCaps.tightAlignmentSupported;
#if D3D12MA_TIGHT_ALIGNMENT_SUPPORTED
        if (m_TightAlignmentSupported)
        {
            // Same behavior as upstream: if tight alignment is enabled, stop preferring committed small buffers.
            if (IsTightAlignmentEnabled()) {
                m_PreferSmallBuffersCommitted = false;
            }
        }
#endif

        // Architecture
        // Upstream falls back to UMA=false on failure; query backend should default to false if unsupported.
        m_UMA = arch.uma;
        m_CacheCoherentUMA = arch.cacheCoherentUMA;
        m_tileBasedRenderer = arch.tileBasedRenderer;

        // Default pools not zeroed
        // Upstream: only enabled if user flag set AND the capability exists.
        m_DefaultPoolsNotZeroed = false;
        if ((desc.flags & AllocatorFlags::DefaultPoolsNotZeroed) != AllocatorFlags::None) {
            if (allocCaps.createNotZeroedHeapSupported) {
                m_DefaultPoolsNotZeroed = true;
            }
        }

        // --- Create default block vectors ---
        const uint32_t defaultPoolCount = GetDefaultPoolCount(); // update this to use m_UnifiedResourceHeaps
        for (uint32_t i = 0; i < defaultPoolCount; ++i)
        {
            rhi::HeapType heapType{};
            rhi::HeapFlags heapFlags{};

            // Port of CalcDefaultPoolParams: output RHI heapType + heapFlags for pool index i.
            CalcDefaultPoolParams(heapType, heapFlags, i);

			HeapProperties heapProps{};
			heapProps.type = heapType;

#if D3D12MA_CREATE_NOT_ZEROED_AVAILABLE
            if (m_DefaultPoolsNotZeroed) {
                heapFlags |= rhi::HeapFlags::CreateNotZeroed;
            }
#endif

            m_BlockVectors[i] = D3D12MA_NEW(GetAllocs(), BlockVector)(
                this,
                heapProps,
                heapFlags,
                m_PreferredBlockSize,
                0,          // minBlockCount
                SIZE_MAX,    // maxBlockCount
                false,       // explicitBlockSize
                (uint64_t)D3D12MA_DEFAULT_ALIGNMENT,
                0,           // default algorithm
                m_MsaaAlwaysCommitted,
                rhi::ResidencyPriority::ResidencyPriorityNormal
                );
        }

        UpdateD3D12Budget();

        return Result::Ok;
    }

    AllocatorPimpl::~AllocatorPimpl()
    {
        for (UINT i = DEFAULT_POOL_MAX_COUNT; i--; )
        {
            D3D12MA_DELETE(GetAllocs(), m_BlockVectors[i]);
        }

        for (UINT i = HEAP_TYPE_COUNT; i--; )
        {
            if (!m_Pools[i].IsEmpty())
            {
                D3D12MA_ASSERT(0 && "Unfreed pools found!");
            }
        }
    }

    bool AllocatorPimpl::HeapFlagsFulfillResourceHeapTier(HeapFlags flags) const
    {
        if (true/*SupportsResourceHeapTier2()*/)
        {
            return true;
        }
        else
        {
            const bool allowBuffers = (flags & HeapFlags::DenyBuffers) == 0;
            const bool allowRtDsTextures = (flags & HeapFlags::DenyRtDsTextures) == 0;
            const bool allowNonRtDsTextures = (flags & HeapFlags::DenyNonRtDsTextures) == 0;
            const uint8_t allowedGroupCount = (allowBuffers ? 1 : 0) + (allowRtDsTextures ? 1 : 0) + (allowNonRtDsTextures ? 1 : 0);
            return allowedGroupCount == 1;
        }
    }

    UINT AllocatorPimpl::StandardHeapTypeToMemorySegmentGroup(HeapType heapType) const
    {
        D3D12MA_ASSERT(IsHeapTypeStandard(heapType));
        if (IsUMA()) {
            return static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local);
        }
        return (heapType == HeapType::DeviceLocal || heapType == D3D12_HEAP_TYPE_GPU_UPLOAD_COPY) ?
            static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local) : static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::NonLocal);
    }

    UINT AllocatorPimpl::HeapPropertiesToMemorySegmentGroup(const HeapProperties& heapProps) const
    {
        if (IsUMA()) {
            return static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local);
        }
        if (true) // TODO: Support custom pool preference
            return StandardHeapTypeToMemorySegmentGroup(heapProps.type);
        //return heapProps.MemoryPoolPreference == D3D12_MEMORY_POOL_L1 ?
            //DXGI_MEMORY_SEGMENT_GROUP_LOCAL_COPY : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL_COPY;
    }

    UINT64 AllocatorPimpl::GetMemoryCapacity(MemorySegmentGroup memorySegmentGroup) const
    {
        switch (memorySegmentGroup)
        {
        case MemorySegmentGroup::Local:
            return IsUMA() ?
                m_adapterFeatureInfo.dedicatedVideoMemory + m_adapterFeatureInfo.sharedSystemMemory : m_adapterFeatureInfo.dedicatedVideoMemory;
        case MemorySegmentGroup::NonLocal:
            return IsUMA() ? 0 : m_adapterFeatureInfo.sharedSystemMemory;
        default:
            D3D12MA_ASSERT(0);
            return UINT64_MAX;
        }
    }

    Result AllocatorPimpl::CreatePlacedResourceWrap( // TODO: Is this necessary anymore?
        HeapHandle heap,
        UINT64 HeapOffset,
        const CreateResourceParams& createParams,
        ResourcePtr& out){
            return m_Device.CreatePlacedResource(heap, HeapOffset,
                *createParams.GetResourceDesc(), out);
    }

    Result AllocatorPimpl::CreateResource(
        const AllocationDesc* pAllocDesc,
        const CreateResourceParams& createParams,
        Allocation** ppAllocation,
        ResourcePtr out)
    {
        D3D12MA_ASSERT(pAllocDesc && createParams.GetResourceDesc() && ppAllocation);

        *ppAllocation = nullptr;

        Result hr = Result::NoInterface;
        CreateResourceParams finalCreateParams = createParams;
        ResourceDesc finalResourceDesc = *createParams.GetResourceDesc();
        finalCreateParams.AccessResourceDesc() = &finalResourceDesc;
        ResourceAllocationInfo resAllocInfo;
        hr = GetResourceAllocationInfo(finalResourceDesc,
            createParams.GetNumCastableFormats(), createParams.GetCastableFormats(), resAllocInfo);

        if (Failed(hr)) {
            return hr;
        }

        D3D12MA_ASSERT(IsPow2(resAllocInfo.alignment));
        // We've seen UINT64_MAX returned when the call to GetResourceAllocationInfo was invalid.
        D3D12MA_ASSERT(resAllocInfo.sizeInBytes != UINT64_MAX);
        D3D12MA_ASSERT(resAllocInfo.sizeInBytes > 0);

        BlockVector* blockVector = nullptr;
        CommittedAllocationParameters committedAllocationParams = {};
        bool preferCommitted = false;

#ifdef __ID3D12Device8_INTERFACE_DEFINED__
        if (createParams.Variant >= CREATE_RESOURCE_PARAMS::VARIANT_WITH_STATE_AND_DESC1)
        {
            hr = CalcAllocationParams<D3D12_RESOURCE_DESC1>(*pAllocDesc, resAllocInfo.SizeInBytes,
                createParams.GetResourceDesc1(),
                blockVector, committedAllocationParams, preferCommitted);
        }
        else
#endif
        {
            hr = CalcAllocationParams(*pAllocDesc, resAllocInfo.sizeInBytes,
                createParams.GetResourceDesc(),
                blockVector, committedAllocationParams, preferCommitted);
        }
        if (Failed(hr)) {
            return hr;
        }

        const bool withinBudget = (pAllocDesc->flags & AllocationFlagWithinBudget) != AllocationFlagNone;
        hr = Result::InvalidArgument;
        if (committedAllocationParams.IsValid() && preferCommitted)
        {
            hr = AllocateCommittedResource(committedAllocationParams,
                resAllocInfo.sizeInBytes, withinBudget, pAllocDesc->privateData,
                finalCreateParams, ppAllocation, std::move(out));
            if (IsOk(hr)) {
                return hr;
            }
        }
        if (blockVector != nullptr)
        {
            hr = blockVector->CreateResource(resAllocInfo.sizeInBytes, resAllocInfo.alignment,
                *pAllocDesc, finalCreateParams, committedAllocationParams.IsValid(),
                ppAllocation, std::move(out));
            if (IsOk(hr)) {
                return hr;
            }
        }
        if (committedAllocationParams.IsValid() && !preferCommitted)
        {
            hr = AllocateCommittedResource(committedAllocationParams,
                resAllocInfo.sizeInBytes, withinBudget, pAllocDesc->privateData,
                finalCreateParams, ppAllocation, std::move(out));
            if (IsOk(hr)) {
                return hr;
            }
        }
        return hr;
    }

    Result AllocatorPimpl::AllocateMemory(
        const AllocationDesc* pAllocDesc,
        const ResourceAllocationInfo* pAllocInfo,
        Allocation** ppAllocation)
    {
        *ppAllocation = nullptr;

        BlockVector* blockVector = nullptr;
        CommittedAllocationParameters committedAllocationParams = {};
        bool preferCommitted = false;
        Result hr = CalcAllocationParams(*pAllocDesc, pAllocInfo->sizeInBytes,
            nullptr, // pResDesc
            blockVector, committedAllocationParams, preferCommitted);
        if (Failed(hr))
            return hr;

        const bool withinBudget = (pAllocDesc->flags & AllocationFlagWithinBudget) != AllocationFlagNone;
        hr = Result::InvalidArgument;
        if (committedAllocationParams.IsValid() && preferCommitted)
        {
            hr = AllocateHeap(committedAllocationParams, *pAllocInfo, withinBudget, pAllocDesc->privateData, ppAllocation);
            if (IsOk(hr)) {
                return hr;
            }
        }
        if (blockVector != nullptr)
        {
            hr = blockVector->Allocate(pAllocInfo->sizeInBytes, pAllocInfo->alignment,
                *pAllocDesc, committedAllocationParams.IsValid(), 1, (Allocation**)ppAllocation);
            if (IsOk(hr)) {
                return hr;
            }
        }
        if (committedAllocationParams.IsValid() && !preferCommitted)
        {
            hr = AllocateHeap(committedAllocationParams, *pAllocInfo, withinBudget, pAllocDesc->privateData, ppAllocation);
            if (IsOk(hr)) {
                return hr;
            }
        }
        return hr;
    }

    Result AllocatorPimpl::CreateAliasingResource(
        Allocation* pAllocation,
        UINT64 AllocationLocalOffset,
        const CreateResourceParams& createParams,
        ResourcePtr& out)
    {

        Result hr = Result::NoInterface;
        CreateResourceParams finalCreateParams = createParams;
        ResourceDesc finalResourceDesc = *createParams.GetResourceDesc();
        ResourceAllocationInfo resAllocInfo;

        finalCreateParams.AccessResourceDesc() = &finalResourceDesc;
        hr = GetResourceAllocationInfo(finalResourceDesc,
            createParams.GetNumCastableFormats(), createParams.GetCastableFormats(), resAllocInfo);

        if (Failed(hr))
            return hr;

        D3D12MA_ASSERT(IsPow2(resAllocInfo.alignment));
        D3D12MA_ASSERT(resAllocInfo.sizeInBytes > 0);

        HeapHandle const existingHeap = pAllocation->GetHeap();
        const UINT64 existingOffset = pAllocation->GetOffset();
        const UINT64 existingSize = pAllocation->GetSize();
        const UINT64 newOffset = existingOffset + AllocationLocalOffset;

        if ((!existingHeap.valid()) ||
            AllocationLocalOffset + resAllocInfo.sizeInBytes > existingSize ||
            newOffset % resAllocInfo.alignment != 0)
        {
            return Result::InvalidArgument;
        }

        return CreatePlacedResourceWrap(existingHeap, newOffset, finalCreateParams, out);
    }

    void AllocatorPimpl::FreeCommittedMemory(Allocation* allocation)
    {
        D3D12MA_ASSERT(allocation && allocation->m_PackedData.GetType() == Allocation::TYPE_COMMITTED);

        CommittedAllocationList* const allocList = allocation->m_Committed.list;
        allocList->Unregister(allocation);

        const UINT memSegmentGroup = allocList->GetMemorySegmentGroup(this);
        const UINT64 allocSize = allocation->GetSize();
        m_Budget.RemoveAllocation(memSegmentGroup, allocSize);
        m_Budget.RemoveBlock(memSegmentGroup, allocSize);
    }

    void AllocatorPimpl::FreePlacedMemory(Allocation* allocation)
    {
        D3D12MA_ASSERT(allocation && allocation->m_PackedData.GetType() == Allocation::TYPE_PLACED);

        NormalBlock* const block = allocation->m_Placed.block;
        D3D12MA_ASSERT(block);
        BlockVector* const blockVector = block->GetBlockVector();
        D3D12MA_ASSERT(blockVector);
        m_Budget.RemoveAllocation(HeapPropertiesToMemorySegmentGroup(block->GetHeapProperties()), allocation->GetSize());
        blockVector->Free(allocation);
    }

    void AllocatorPimpl::FreeHeapMemory(Allocation* allocation)
    {
        D3D12MA_ASSERT(allocation && allocation->m_PackedData.GetType() == Allocation::TYPE_HEAP);

        CommittedAllocationList* const allocList = allocation->m_Committed.list;
        allocList->Unregister(allocation);
        //SAFE_RELEASE(allocation->m_Heap.heap);

        const UINT memSegmentGroup = allocList->GetMemorySegmentGroup(this);
        const UINT64 allocSize = allocation->GetSize();
        m_Budget.RemoveAllocation(memSegmentGroup, allocSize);
        m_Budget.RemoveBlock(memSegmentGroup, allocSize);
    }

    void AllocatorPimpl::SetResidencyPriority(PageableRef obj, ResidencyPriority priority) const
    {
        if (priority != D3D12_RESIDENCY_PRIORITY_NONE)
        {
            // Intentionally ignoring the result.
            Span resources(&obj, 1);
            m_Device.SetResidencyPriority(resources, priority);
        }
    }

    void AllocatorPimpl::SetCurrentFrameIndex(UINT frameIndex)
    {
        m_CurrentFrameIndex.store(frameIndex);

#if D3D12MA_DXGI_1_4
        UpdateD3D12Budget();
#endif
    }

    void AllocatorPimpl::CalculateStatistics(TotalStatistics& outStats, DetailedStatistics outCustomHeaps[2])
    {
        // Init stats
        for (size_t i = 0; i < HEAP_TYPE_COUNT; i++) {
            ClearDetailedStatistics(outStats.heapType[i]);
        }
        for (size_t i = 0; i < static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Count); i++) {
            ClearDetailedStatistics(outStats.memorySegmentGroup[i]);
        }
        ClearDetailedStatistics(outStats.total);
        if (outCustomHeaps)
        {
            ClearDetailedStatistics(outCustomHeaps[0]);
            ClearDetailedStatistics(outCustomHeaps[1]);
        }

        // Process default pools. 4 standard heap types only. Add them to outStats.HeapType[i].
        if (true/*(SupportsResourceHeapTier2()*/) // TODO: How to tell in a cross-API way?
        {
            // DEFAULT, UPLOAD, READBACK, GPU_UPLOAD.
            for (size_t heapTypeIndex = 0; heapTypeIndex < STANDARD_HEAP_TYPE_COUNT; ++heapTypeIndex)
            {
                BlockVector* const pBlockVector = m_BlockVectors[heapTypeIndex];
                D3D12MA_ASSERT(pBlockVector);
                const size_t outputIndex = heapTypeIndex < 3 ? heapTypeIndex : 4; // GPU_UPLOAD 3 -> 4
                pBlockVector->AddDetailedStatistics(outStats.heapType[outputIndex]);
            }
        }
        else
        {
            // DEFAULT, UPLOAD, READBACK.
            for (size_t heapTypeIndex = 0; heapTypeIndex < STANDARD_HEAP_TYPE_COUNT; ++heapTypeIndex)
            {
                for (size_t heapSubType = 0; heapSubType < 3; ++heapSubType)
                {
                    BlockVector* const pBlockVector = m_BlockVectors[heapTypeIndex * 3 + heapSubType];
                    D3D12MA_ASSERT(pBlockVector);

                    const size_t outputIndex = heapTypeIndex < 3 ? heapTypeIndex : 4; // GPU_UPLOAD 3 -> 4
                    pBlockVector->AddDetailedStatistics(outStats.heapType[outputIndex]);
                }
            }
        }

        // Sum them up to memory segment groups.
        AddDetailedStatistics(
            outStats.memorySegmentGroup[StandardHeapTypeToMemorySegmentGroup(HeapType::DeviceLocal)],
            outStats.heapType[0]);
        AddDetailedStatistics(
            outStats.memorySegmentGroup[StandardHeapTypeToMemorySegmentGroup(HeapType::Upload)],
            outStats.heapType[1]);
        AddDetailedStatistics(
            outStats.memorySegmentGroup[StandardHeapTypeToMemorySegmentGroup(HeapType::Readback)],
            outStats.heapType[2]);
        AddDetailedStatistics(
            outStats.memorySegmentGroup[StandardHeapTypeToMemorySegmentGroup(D3D12_HEAP_TYPE_GPU_UPLOAD_COPY)],
            outStats.heapType[4]);

        // Process custom pools.
        DetailedStatistics tmpStats;
        for (size_t heapTypeIndex = 0; heapTypeIndex < HEAP_TYPE_COUNT; ++heapTypeIndex)
        {
            MutexLockRead lock(m_PoolsMutex[heapTypeIndex], m_UseMutex);
            PoolList& poolList = m_Pools[heapTypeIndex];
            for (PoolPimpl* pool = poolList.Front(); pool != NULL; pool = poolList.GetNext(pool))
            {
                const HeapProperties& poolHeapProps = { pool->GetDesc().heapType };
                ClearDetailedStatistics(tmpStats);
                pool->AddDetailedStatistics(tmpStats);
                AddDetailedStatistics(
                    outStats.heapType[heapTypeIndex], tmpStats);

                UINT memorySegment = HeapPropertiesToMemorySegmentGroup(poolHeapProps);
                AddDetailedStatistics(
                    outStats.memorySegmentGroup[memorySegment], tmpStats);

                if (outCustomHeaps)
                    AddDetailedStatistics(outCustomHeaps[memorySegment], tmpStats);
            }
        }

        // Process committed allocations. standard heap types only.
        for (UINT heapTypeIndex = 0; heapTypeIndex < STANDARD_HEAP_TYPE_COUNT; ++heapTypeIndex)
        {
            ClearDetailedStatistics(tmpStats);
            m_CommittedAllocations[heapTypeIndex].AddDetailedStatistics(tmpStats);
            const size_t outputIndex = heapTypeIndex < 3 ? heapTypeIndex : 4; // GPU_UPLOAD 3 -> 4
            AddDetailedStatistics(
                outStats.heapType[outputIndex], tmpStats);
            AddDetailedStatistics(
                outStats.memorySegmentGroup[StandardHeapTypeToMemorySegmentGroup(IndexToStandardHeapType(heapTypeIndex))], tmpStats);
        }

        // Sum up memory segment groups to totals.
        AddDetailedStatistics(outStats.total, outStats.memorySegmentGroup[0]);
        AddDetailedStatistics(outStats.total, outStats.memorySegmentGroup[1]);

        D3D12MA_ASSERT(outStats.total.stats.blockCount ==
            outStats.memorySegmentGroup[0].stats.blockCount + outStats.memorySegmentGroup[1].stats.blockCount);
        D3D12MA_ASSERT(outStats.total.stats.allocationCount ==
            outStats.memorySegmentGroup[0].stats.allocationCount + outStats.memorySegmentGroup[1].stats.allocationCount);
        D3D12MA_ASSERT(outStats.total.stats.blockBytes ==
            outStats.memorySegmentGroup[0].stats.blockBytes + outStats.memorySegmentGroup[1].stats.blockBytes);
        D3D12MA_ASSERT(outStats.total.stats.allocationBytes ==
            outStats.memorySegmentGroup[0].stats.allocationBytes + outStats.memorySegmentGroup[1].stats.allocationBytes);
        D3D12MA_ASSERT(outStats.total.unusedRangeCount ==
            outStats.memorySegmentGroup[0].unusedRangeCount + outStats.memorySegmentGroup[1].unusedRangeCount);

        D3D12MA_ASSERT(outStats.total.stats.blockCount ==
            outStats.heapType[0].stats.blockCount + outStats.heapType[1].stats.blockCount +
            outStats.heapType[2].stats.blockCount + outStats.heapType[3].stats.blockCount +
            outStats.heapType[4].stats.blockCount);
        D3D12MA_ASSERT(outStats.total.stats.allocationCount ==
            outStats.heapType[0].stats.allocationCount + outStats.heapType[1].stats.allocationCount +
            outStats.heapType[2].stats.allocationCount + outStats.heapType[3].stats.allocationCount +
            outStats.heapType[4].stats.allocationCount);
        D3D12MA_ASSERT(outStats.total.stats.blockBytes ==
            outStats.heapType[0].stats.blockBytes + outStats.heapType[1].stats.blockBytes +
            outStats.heapType[2].stats.blockBytes + outStats.heapType[3].stats.blockBytes +
            outStats.heapType[4].stats.blockBytes);
        D3D12MA_ASSERT(outStats.total.stats.allocationBytes ==
            outStats.heapType[0].stats.allocationBytes + outStats.heapType[1].stats.allocationBytes +
            outStats.heapType[2].stats.allocationBytes + outStats.heapType[3].stats.allocationBytes +
            outStats.heapType[4].stats.allocationBytes);
        D3D12MA_ASSERT(outStats.total.unusedRangeCount ==
            outStats.heapType[0].unusedRangeCount + outStats.heapType[1].unusedRangeCount +
            outStats.heapType[2].unusedRangeCount + outStats.heapType[3].unusedRangeCount +
            outStats.heapType[4].unusedRangeCount);
    }

    void AllocatorPimpl::GetBudget(Budget* outLocalBudget, Budget* outNonLocalBudget)
    {
        if (outLocalBudget)
            m_Budget.GetStatistics(outLocalBudget->stats, static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local));
        if (outNonLocalBudget)
            m_Budget.GetStatistics(outNonLocalBudget->stats, static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::NonLocal));


        if (!m_Budget.ShouldUpdateBudget())
        {
            m_Budget.GetBudget(m_UseMutex,
                outLocalBudget ? &outLocalBudget->usageBytes : nullptr,
                outLocalBudget ? &outLocalBudget->budgetBytes : nullptr,
                outNonLocalBudget ? &outNonLocalBudget->usageBytes : nullptr,
                outNonLocalBudget ? &outNonLocalBudget->budgetBytes : nullptr);
            return;
        }

        if (IsOk(UpdateD3D12Budget()))
        {
            GetBudget(outLocalBudget, outNonLocalBudget); // Recursion.
            return;
        }

        // Fallback path - manual calculation, not real budget.
        //if (outLocalBudget)
        //{
        //    outLocalBudget->usageBytes = outLocalBudget->stats.blockBytes;
        //    outLocalBudget->budgetBytes = GetMemoryCapacity(MemorySegmentGroup::Local) * 8 / 10; // 80% heuristics.
        //}
        //if (outNonLocalBudget)
        //{
        //    outNonLocalBudget->usageBytes = outNonLocalBudget->stats.blockBytes;
        //    outNonLocalBudget->budgetBytes = GetMemoryCapacity(MemorySegmentGroup::NonLocal) * 8 / 10; // 80% heuristics.
        //}
    }

    void AllocatorPimpl::GetBudgetForHeapType(Budget& outBudget, HeapType heapType)
    {
        const bool isLocal = StandardHeapTypeToMemorySegmentGroup(heapType) ==
            static_cast<std::underlying_type_t<MemorySegmentGroup>>(MemorySegmentGroup::Local);
        if (isLocal)
        {
            GetBudget(&outBudget, nullptr);
        }
        else
        {
            GetBudget(nullptr, &outBudget);
        }
    }

    void AllocatorPimpl::BuildStatsString(char** ppStatsString, BOOL detailedMap)
    {
        StringBuilder sb(GetAllocs());
        {
            Budget localBudget = {}, nonLocalBudget = {};
            GetBudget(&localBudget, &nonLocalBudget);

            TotalStatistics stats;
            DetailedStatistics customHeaps[2];
            CalculateStatistics(stats, customHeaps);

            JsonWriter json(GetAllocs(), sb);
            json.BeginObject();
            {
                json.WriteString("General");
                json.BeginObject();
                {
                    json.WriteString("API");
                    json.WriteString("Direct3D 12");

                    json.WriteString("GPU");
                    json.WriteString(m_adapterFeatureInfo.name);

                    json.WriteString("DedicatedVideoMemory");
                    json.WriteNumber((UINT64)m_adapterFeatureInfo.dedicatedVideoMemory);
                    json.WriteString("DedicatedSystemMemory");
                    json.WriteNumber((UINT64)m_adapterFeatureInfo.dedicatedSystemMemory);
                    json.WriteString("SharedSystemMemory");
                    json.WriteNumber((UINT64)m_adapterFeatureInfo.sharedSystemMemory);

                    //json.WriteString(L"ResourceHeapTier");
                    //json.WriteNumber(static_cast<UINT>(m_D3D12Options.ResourceHeapTier));

                    //json.WriteString(L"ResourceBindingTier");
                    //json.WriteNumber(static_cast<UINT>(m_D3D12Options.ResourceBindingTier));

                    //json.WriteString(L"TiledResourcesTier");
                    //json.WriteNumber(static_cast<UINT>(m_D3D12Options.TiledResourcesTier));

                    json.WriteString("TileBasedRenderer");
                    json.WriteBool(m_tileBasedRenderer);

                    json.WriteString("UMA");
                    json.WriteBool(m_UMA);
                    json.WriteString("CacheCoherentUMA");
                    json.WriteBool(m_CacheCoherentUMA);

                    json.WriteString("GPUUploadHeapSupported");
                    json.WriteBool(m_GPUUploadHeapSupported != FALSE);

                    json.WriteString("TightAlignmentSupported");
                    json.WriteBool(m_TightAlignmentSupported != FALSE);
                }
                json.EndObject();
            }
            {
                json.WriteString("Total");
                json.AddDetailedStatisticsInfoObject(stats.total);
            }
            {
                json.WriteString("MemoryInfo");
                json.BeginObject();
                {
                    json.WriteString("L0");
                    json.BeginObject();
                    {
                        json.WriteString("Budget");
                        WriteBudgetToJson(json, IsUMA() ? localBudget : nonLocalBudget); // When UMA device only L0 present as local

                        json.WriteString("Stats");
                        json.AddDetailedStatisticsInfoObject(stats.memorySegmentGroup[!IsUMA()]);

                        json.WriteString("MemoryPools");
                        json.BeginObject();
                        {
                            if (IsUMA())
                            {
                                json.WriteString("DEFAULT");
                                json.BeginObject();
                                {
                                    json.WriteString("Stats");
                                    json.AddDetailedStatisticsInfoObject(stats.heapType[0]);
                                }
                                json.EndObject();

                                if (IsGPUUploadHeapSupported())
                                {
                                    json.WriteString("GPU_UPLOAD");
                                    json.BeginObject();
                                    {
                                        json.WriteString("Stats");
                                        json.AddDetailedStatisticsInfoObject(stats.heapType[4]);
                                    }
                                    json.EndObject();
                                }
                            }
                            json.WriteString("UPLOAD");
                            json.BeginObject();
                            {
                                json.WriteString("Stats");
                                json.AddDetailedStatisticsInfoObject(stats.heapType[1]);
                            }
                            json.EndObject();

                            json.WriteString("READBACK");
                            json.BeginObject();
                            {
                                json.WriteString("Stats");
                                json.AddDetailedStatisticsInfoObject(stats.heapType[2]);
                            }
                            json.EndObject();

                            json.WriteString("CUSTOM");
                            json.BeginObject();
                            {
                                json.WriteString("Stats");
                                json.AddDetailedStatisticsInfoObject(customHeaps[!IsUMA()]);
                            }
                            json.EndObject();
                        }
                        json.EndObject();
                    }
                    json.EndObject();
                    if (!IsUMA())
                    {
                        json.WriteString("L1");
                        json.BeginObject();
                        {
                            json.WriteString("Budget");
                            WriteBudgetToJson(json, localBudget);

                            json.WriteString("Stats");
                            json.AddDetailedStatisticsInfoObject(stats.memorySegmentGroup[0]);

                            json.WriteString("MemoryPools");
                            json.BeginObject();
                            {
                                json.WriteString("DEFAULT");
                                json.BeginObject();
                                {
                                    json.WriteString("Stats");
                                    json.AddDetailedStatisticsInfoObject(stats.heapType[0]);
                                }
                                json.EndObject();

                                if (IsGPUUploadHeapSupported())
                                {
                                    json.WriteString("GPU_UPLOAD");
                                    json.BeginObject();
                                    {
                                        json.WriteString("Stats");
                                        json.AddDetailedStatisticsInfoObject(stats.heapType[4]);
                                    }
                                    json.EndObject();
                                }

                                json.WriteString("CUSTOM");
                                json.BeginObject();
                                {
                                    json.WriteString("Stats");
                                    json.AddDetailedStatisticsInfoObject(customHeaps[0]);
                                }
                                json.EndObject();
                            }
                            json.EndObject();
                        }
                        json.EndObject();
                    }
                }
                json.EndObject();
            }

            if (detailedMap)
            {
                const auto writeHeapInfo = [&](BlockVector* blockVector, CommittedAllocationList* committedAllocs, bool customHeap)
                    {
                        D3D12MA_ASSERT(blockVector);

                        HeapFlags flags = blockVector->GetHeapFlags();
                        json.WriteString("Flags");
                        json.BeginArray(true);
                        {
                            if (Any(flags & HeapFlags::Shared))
                                json.WriteString("HEAP_FLAG_SHARED");
                            if (Any(flags & HeapFlags::AllowDisplay))
                                json.WriteString("HEAP_FLAG_ALLOW_DISPLAY");
                            if (Any(flags & HeapFlags::SharedCrossAdapter))
                                json.WriteString("HEAP_FLAG_CROSS_ADAPTER");
                            if (Any(flags & HeapFlags::HardwareProtected))
                                json.WriteString("HEAP_FLAG_HARDWARE_PROTECTED");
                            if (Any(flags & HeapFlags::AllowWriteWatch))
                                json.WriteString("HEAP_FLAG_ALLOW_WRITE_WATCH");
                            if (Any(flags & HeapFlags::AllowCrossAdapterShaderAtomics))
                                json.WriteString("HEAP_FLAG_ALLOW_SHADER_ATOMICS");
#ifdef __ID3D12Device8_INTERFACE_DEFINED__
                            if (Any(flags & HeapFlags::CreateNotResident))
                                json.WriteString(L"HEAP_FLAG_CREATE_NOT_RESIDENT");
                            if (Any(flags & HeapFlags::CreateNotZeroed))
                                json.WriteString(L"HEAP_FLAG_CREATE_NOT_ZEROED");
#endif

                            if (Any(flags & HeapFlags::DenyBuffers))
                                json.WriteString("HEAP_FLAG_DENY_BUFFERS");
                            if (Any(flags & HeapFlags::DenyRtDsTextures))
                                json.WriteString("HEAP_FLAG_DENY_RT_DS_TEXTURES");
                            if (Any(flags & HeapFlags::DenyNonRtDsTextures))
                                json.WriteString("HEAP_FLAG_DENY_NON_RT_DS_TEXTURES");

                            flags &= ~(HeapFlags::Shared
                                | HeapFlags::DenyBuffers
                                | HeapFlags::AllowDisplay
                                | HeapFlags::SharedCrossAdapter
                                | HeapFlags::DenyRtDsTextures
                                | HeapFlags::DenyNonRtDsTextures
                                | HeapFlags::HardwareProtected
                                | HeapFlags::AllowWriteWatch
                                | HeapFlags::AllowCrossAdapterShaderAtomics);
#ifdef __ID3D12Device8_INTERFACE_DEFINED__
                            flags &= ~(D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT
                                | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED);
#endif
                            if (flags != 0)
                                json.WriteNumber((UINT)flags);

                            if (customHeap)
                            {
                                const HeapProperties& properties = blockVector->GetHeapProperties();
                                json.WriteString("MEMORY_POOL_UNKNOWN");
                                json.WriteString("CPU_PAGE_PROPERTY_UNKNOWN");
                                //switch (properties.memoryPoolPreference)
                                //{
                                //default:
                                //    D3D12MA_ASSERT(0);
                                //case D3D12_MEMORY_POOL_UNKNOWN:
                                //    json.WriteString(L"MEMORY_POOL_UNKNOWN");
                                //    break;
                                //case D3D12_MEMORY_POOL_L0:
                                //    json.WriteString(L"MEMORY_POOL_L0");
                                //    break;
                                //case D3D12_MEMORY_POOL_L1:
                                //    json.WriteString(L"MEMORY_POOL_L1");
                                //    break;
                                //}
                                //switch (properties.CPUPageProperty)
                                //{
                                //default:
                                //    D3D12MA_ASSERT(0);
                                //case D3D12_CPU_PAGE_PROPERTY_UNKNOWN:
                                //    json.WriteString(L"CPU_PAGE_PROPERTY_UNKNOWN");
                                //    break;
                                //case D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE:
                                //    json.WriteString(L"CPU_PAGE_PROPERTY_NOT_AVAILABLE");
                                //    break;
                                //case D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE:
                                //    json.WriteString(L"CPU_PAGE_PROPERTY_WRITE_COMBINE");
                                //    break;
                                //case D3D12_CPU_PAGE_PROPERTY_WRITE_BACK:
                                //    json.WriteString(L"CPU_PAGE_PROPERTY_WRITE_BACK");
                                //    break;
                                //}
                            }
                        }
                        json.EndArray();

                        json.WriteString("PreferredBlockSize");
                        json.WriteNumber(blockVector->GetPreferredBlockSize());

                        json.WriteString("Blocks");
                        blockVector->WriteBlockInfoToJson(json);

                        json.WriteString("DedicatedAllocations");
                        json.BeginArray();
                        if (committedAllocs)
                            committedAllocs->BuildStatsString(json);
                        json.EndArray();
                    };

                json.WriteString("DefaultPools");
                json.BeginObject();
                {
                    if (true/*SupportsResourceHeapTier2()*/) // TODO
                    {
                        for (uint8_t heapType = 0; heapType < STANDARD_HEAP_TYPE_COUNT; ++heapType)
                        {
                            json.WriteString(StandardHeapTypeNames[heapType]);
                            json.BeginObject();
                            writeHeapInfo(m_BlockVectors[heapType], m_CommittedAllocations + heapType, false);
                            json.EndObject();
                        }
                    }
                    else
                    {
                        for (uint8_t heapType = 0; heapType < STANDARD_HEAP_TYPE_COUNT; ++heapType)
                        {
                            for (uint8_t heapSubType = 0; heapSubType < 3; ++heapSubType)
                            {
                                static const char* const heapSubTypeName[] = {
                                    " - Buffers",
                                    " - Textures",
                                    " - Textures RT/DS",
                                };
                                json.BeginString(StandardHeapTypeNames[heapType]);
                                json.EndString(heapSubTypeName[heapSubType]);

                                json.BeginObject();
                                writeHeapInfo(m_BlockVectors[heapType * 3 + heapSubType], m_CommittedAllocations + heapType, false);
                                json.EndObject();
                            }
                        }
                    }
                }
                json.EndObject();

                json.WriteString("CustomPools");
                json.BeginObject();
                for (uint8_t heapTypeIndex = 0; heapTypeIndex < HEAP_TYPE_COUNT; ++heapTypeIndex)
                {
                    MutexLockRead mutex(m_PoolsMutex[heapTypeIndex], m_UseMutex);
                    auto* item = m_Pools[heapTypeIndex].Front();
                    if (item != nullptr)
                    {
                        size_t index = 0;
                        json.WriteString(HeapTypeNames[heapTypeIndex]);
                        json.BeginArray();
                        do
                        {
                            json.BeginObject();
                            json.WriteString("Name");
                            json.BeginString();
                            json.ContinueString(index++);
                            if (item->GetName())
                            {
                                json.ContinueString(" - ");
                                json.ContinueString(item->GetName());
                            }
                            json.EndString();

                            writeHeapInfo(item->GetBlockVector(), item->GetCommittedAllocationList(), heapTypeIndex == 3);
                            json.EndObject();
                        } while ((item = PoolList::GetNext(item)) != nullptr);
                        json.EndArray();
                    }
                }
                json.EndObject();
            }
            json.EndObject();
        }

        const size_t length = sb.GetLength();
        char* result = AllocateArray<char>(GetAllocs(), length + 2);
        result[0] = static_cast<char>(0xEF);
        result[1] = static_cast<char>(0xBB);
        result[2] = static_cast<char>(0xBF);
        memcpy(result + 3, sb.GetData(), length);
        result[length + 3] = '\0';

        *ppStatsString = result;
    }

    void AllocatorPimpl::FreeStatsString(char* pStatsString)
    {
        D3D12MA_ASSERT(pStatsString);
        Free(GetAllocs(), pStatsString);
    }

    bool AllocatorPimpl::PrefersCommittedAllocation(const ResourceDesc& resourceDesc,
        AllocationFlags strategy)
    {
        // Prefer creating small buffers <= 32 KB as committed, because drivers pack them better,
        // while placed buffers require 64 KB alignment.
        if (resourceDesc.type == ResourceType::Buffer &&
            resourceDesc.buffer.sizeBytes <= DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT / 2 &&
            strategy != AllocationFlagStrategyMinTime &&  // Creating as committed would be slower.
            m_PreferSmallBuffersCommitted)
        {
            return true;
        }

        // Intentional. It may change in the future.
        return false;
    }

    Result AllocatorPimpl::AllocateCommittedResource(
        const CommittedAllocationParameters& committedAllocParams,
        UINT64 resourceSize, bool withinBudget, void* pPrivateData,
        const CreateResourceParams& createParams,
        Allocation** ppOutAllocation,
        ResourcePtr ptr)
    {
        D3D12MA_ASSERT(committedAllocParams.IsValid());

        Result hr;
        // Allocate aliasing memory with explicit heap
        if (committedAllocParams.m_CanAlias)
        {
            ResourceAllocationInfo heapAllocInfo = {};
            heapAllocInfo.sizeInBytes = resourceSize;
            heapAllocInfo.alignment = HeapFlagsToAlignment(committedAllocParams.m_HeapFlags, m_MsaaAlwaysCommitted);
            hr = AllocateHeap(committedAllocParams, heapAllocInfo, withinBudget, pPrivateData, ppOutAllocation);
            if (IsOk(hr))
            {
                hr = CreatePlacedResourceWrap((*ppOutAllocation)->GetHeap(), 0,
                    createParams, ptr);
                if (IsOk(hr))
                {
                    if (IsOk(hr))
                    {
                        (*ppOutAllocation)->SetResourcePointer(std::move(ptr), createParams.GetResourceDesc());
                        return hr;
                    }
                    ptr.Reset();
                }
                FreeHeapMemory(*ppOutAllocation);
            }
            return hr;
        }

        if (withinBudget &&
            !NewAllocationWithinBudget(committedAllocParams.m_HeapProperties.type, resourceSize))
        {
            return Result::OutOfMemory;
        }

        /* D3D12 ERROR:
         * ID3D12Device::CreateCommittedResource:
         * When creating a committed resource, D3D12_HEAP_FLAGS must not have either
         *      D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES,
         *      D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES,
         *      nor D3D12_HEAP_FLAG_DENY_BUFFERS set.
         * These flags will be set automatically to correspond with the committed resource type.
         *
         * [ STATE_CREATION ERROR #640: CREATERESOURCEANDHEAP_INVALIDHEAPMISCFLAGS]
        */



        hr = m_Device.CreateCommittedResource(*createParams.GetResourceDesc(),ptr);

        if (IsOk(hr))
        {
            SetResidencyPriority(ptr->GetHandle(), committedAllocParams.m_ResidencyPriority);

            // TODO: Original MA wanted alignment from the desc struct, but we don't specify that. 
            // Is there any reason using output from GetResourceAllocationInfo would be suboptimal?
            ResourceAllocationInfo info;
            m_Device.GetResourceAllocationInfo(createParams.GetResourceDesc(), 1, &info);

            Allocation* alloc = m_AllocationObjectAllocator.Allocate(
                this, resourceSize, info.alignment);
            alloc->InitCommitted(committedAllocParams.m_List);
            alloc->SetResourcePointer(std::move(ptr), createParams.GetResourceDesc());
            alloc->SetPrivateData(pPrivateData);

            *ppOutAllocation = alloc;

            committedAllocParams.m_List->Register(alloc);

            const UINT memSegmentGroup = HeapPropertiesToMemorySegmentGroup(committedAllocParams.m_HeapProperties);
            m_Budget.AddBlock(memSegmentGroup, resourceSize);
            m_Budget.AddAllocation(memSegmentGroup, resourceSize);
        }
        return hr;
    }

    Result AllocatorPimpl::AllocateHeap(
        const CommittedAllocationParameters& committedAllocParams,
        const ResourceAllocationInfo& allocInfo, bool withinBudget,
        void* pPrivateData, Allocation** ppAllocation)
    {
        D3D12MA_ASSERT(committedAllocParams.IsValid());

        *ppAllocation = nullptr;

        if (withinBudget &&
            !NewAllocationWithinBudget(committedAllocParams.m_HeapProperties.type, allocInfo.sizeInBytes))
        {
            return Result::OutOfMemory;
        }

        HeapDesc heapDesc = {};
        heapDesc.sizeBytes = allocInfo.sizeInBytes;
        heapDesc.memory = committedAllocParams.m_HeapProperties.type;
        heapDesc.alignment = allocInfo.alignment;
        heapDesc.flags = committedAllocParams.m_HeapFlags;

        HeapPtr heap;
        Result hr = m_Device.CreateHeap(heapDesc, heap); Failed(hr);

        if (IsOk(hr))
        {
            SetResidencyPriority(heap->GetHandle(), committedAllocParams.m_ResidencyPriority);
            (*ppAllocation) = m_AllocationObjectAllocator.Allocate(this, allocInfo.sizeInBytes, allocInfo.alignment);
            (*ppAllocation)->InitHeap(committedAllocParams.m_List, std::move(heap));
            (*ppAllocation)->SetPrivateData(pPrivateData);
            committedAllocParams.m_List->Register(*ppAllocation);

            const UINT memSegmentGroup = HeapPropertiesToMemorySegmentGroup(committedAllocParams.m_HeapProperties);
            m_Budget.AddBlock(memSegmentGroup, allocInfo.sizeInBytes);
            m_Budget.AddAllocation(memSegmentGroup, allocInfo.sizeInBytes);
        }
        return hr;
    }

    Result AllocatorPimpl::CalcAllocationParams(const AllocationDesc& allocDesc, UINT64 allocSize,
        const ResourceDesc* resDesc,
        BlockVector*& outBlockVector, CommittedAllocationParameters& outCommittedAllocationParams, bool& outPreferCommitted)
    {
        outBlockVector = nullptr;
        outCommittedAllocationParams = CommittedAllocationParameters();
        outPreferCommitted = false;

        if (allocDesc.heapType == D3D12_HEAP_TYPE_GPU_UPLOAD_COPY && !IsGPUUploadHeapSupported()) {
            return Result::NotImplemented;
        }

        bool msaaAlwaysCommitted;
        if (allocDesc.customPool != nullptr)
        {
            PoolPimpl* const pool = allocDesc.customPool->m_Pimpl;

            msaaAlwaysCommitted = pool->GetBlockVector()->DeniesMsaaTextures();
            if (!pool->AlwaysCommitted())
                outBlockVector = pool->GetBlockVector();

            const auto& desc = pool->GetDesc();
            //outCommittedAllocationParams.m_ProtectedSession = desc.pProtectedSession;
            outCommittedAllocationParams.m_HeapProperties = { desc.heapType };
            outCommittedAllocationParams.m_HeapFlags = desc.heapFlags;
            outCommittedAllocationParams.m_List = pool->GetCommittedAllocationList();
            outCommittedAllocationParams.m_ResidencyPriority = pool->GetDesc().residencyPriority;
        }
        else
        {
            if (!IsHeapTypeStandard(allocDesc.heapType))
            {
                return Result::InvalidArgument;
            }
            msaaAlwaysCommitted = m_MsaaAlwaysCommitted;

            outCommittedAllocationParams.m_HeapProperties = StandardHeapTypeToHeapProperties(allocDesc.heapType);
            outCommittedAllocationParams.m_HeapFlags = allocDesc.extraHeapFlags;
            outCommittedAllocationParams.m_List = &m_CommittedAllocations[StandardHeapTypeToIndex(allocDesc.heapType)];
            // outCommittedAllocationParams.m_ResidencyPriority intentionally left with default value.

            const ResourceClass resourceClass = (resDesc != nullptr) ?
                ResourceDescToResourceClass(*resDesc) : HeapFlagsToResourceClass(allocDesc.extraHeapFlags);
            const UINT defaultPoolIndex = CalcDefaultPoolIndex(allocDesc, resourceClass);
            if (defaultPoolIndex != UINT32_MAX)
            {
                outBlockVector = m_BlockVectors[defaultPoolIndex];
                const UINT64 preferredBlockSize = outBlockVector->GetPreferredBlockSize();
                if (allocSize > preferredBlockSize)
                {
                    outBlockVector = nullptr;
                }
                else if (allocSize > preferredBlockSize / 2)
                {
                    // Heuristics: Allocate committed memory if requested size if greater than half of preferred block size.
                    outPreferCommitted = true;
                }
            }
        }

        if ((allocDesc.flags & AllocationFlagCommitted) != 0 ||
            m_AlwaysCommitted)
        {
            outBlockVector = nullptr;
        }
        if ((allocDesc.flags & AllocationFlagNeverAllocate) != 0)
        {
            outCommittedAllocationParams.m_List = nullptr;
        }
        outCommittedAllocationParams.m_CanAlias = allocDesc.flags & AllocationFlagCanAlias;

        if (resDesc != nullptr)
        {
            if (resDesc->texture.sampleCount > 1 && msaaAlwaysCommitted)
                outBlockVector = nullptr;
            if (!outPreferCommitted && PrefersCommittedAllocation(*resDesc, allocDesc.flags & AllocationFlagStrategyMask))
                outPreferCommitted = true;
        }

        return (outBlockVector != nullptr || outCommittedAllocationParams.m_List != nullptr) ? Result::Ok : Result::InvalidArgument;
    }

    UINT AllocatorPimpl::CalcDefaultPoolIndex(const AllocationDesc& allocDesc, ResourceClass resourceClass) const
    {
        HeapFlags extraHeapFlags = allocDesc.extraHeapFlags & ~RESOURCE_CLASS_HEAP_FLAGS;

#if D3D12MA_CREATE_NOT_ZEROED_AVAILABLE
        extraHeapFlags &= ~D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
#endif

        if (extraHeapFlags != 0)
        {
            return UINT32_MAX;
        }

        UINT poolIndex = UINT_MAX;
        switch (allocDesc.heapType)
        {
        case HeapType::DeviceLocal:  poolIndex = 0; break;
        case HeapType::Upload:   poolIndex = 1; break;
        case HeapType::Readback: poolIndex = 2; break;
        case HeapType::GPUUpload: poolIndex = 3; break;
        default: D3D12MA_ASSERT(0);
        }

        if (true/*SupportsResourceHeapTier2()*/) // TODO
            return poolIndex;
        else
        {
            switch (resourceClass)
            {
            case ResourceClass::Buffer:
                return poolIndex * 3;
            case ResourceClass::Non_RT_DS_Texture:
                return poolIndex * 3 + 1;
            case ResourceClass::RT_DS_Texture:
                return poolIndex * 3 + 2;
            default:
                return UINT32_MAX;
            }
        }
    }

    void AllocatorPimpl::CalcDefaultPoolParams(HeapType& outHeapType, HeapFlags& outHeapFlags, UINT index) const
    {
        outHeapType = HeapType::DeviceLocal;
        outHeapFlags = HeapFlags::None;

		if (!true/*SupportsResourceHeapTier2()*/)// TODO
        {
            switch (index % 3)
            {
            case 0:
                outHeapFlags = HeapFlags::DenyRtDsTextures | HeapFlags::DenyNonRtDsTextures;
                break;
            case 1:
                outHeapFlags = HeapFlags::DenyBuffers | HeapFlags::DenyRtDsTextures;
                break;
            case 2:
                outHeapFlags = HeapFlags::DenyBuffers | HeapFlags::DenyNonRtDsTextures;
                break;
            }

            index /= 3;
        }

        switch (index)
        {
        case 0:
            outHeapType = HeapType::DeviceLocal;
            break;
        case 1:
            outHeapType = HeapType::Upload;
            break;
        case 2:
            outHeapType = HeapType::Readback;
            break;
        case 3:
            outHeapType = D3D12_HEAP_TYPE_GPU_UPLOAD_COPY;
            break;
        default:
            D3D12MA_ASSERT(0);
        }
    }

    void AllocatorPimpl::RegisterPool(Pool* pool, HeapType heapType)
    {
        const UINT heapTypeIndex = (UINT)heapType - 1;

        MutexLockWrite lock(m_PoolsMutex[heapTypeIndex], m_UseMutex);
        m_Pools[heapTypeIndex].PushBack(pool->m_Pimpl);
    }

    void AllocatorPimpl::UnregisterPool(Pool* pool, HeapType heapType)
    {
        const UINT heapTypeIndex = (UINT)heapType - 1;

        MutexLockWrite lock(m_PoolsMutex[heapTypeIndex], m_UseMutex);
        m_Pools[heapTypeIndex].Remove(pool->m_Pimpl);
    }

    Result AllocatorPimpl::UpdateD3D12Budget()
    {
    	return m_Budget.UpdateBudget(m_Device, m_UseMutex);
    }

    ResourceAllocationInfo AllocatorPimpl::GetResourceAllocationInfoNative(const ResourceDesc& resourceDesc) const
    {
        ResourceAllocationInfo info;
        m_Device.GetResourceAllocationInfo(&resourceDesc, 1, &info);
		return info;
    }

    Result AllocatorPimpl::GetResourceAllocationInfoMiddle(
        ResourceDesc& inOutResourceDesc,
        UINT32 NumCastableFormats, const Format* pCastableFormats,
        ResourceAllocationInfo& outAllocInfo) const
    {
        if (NumCastableFormats > 0)
        {
            return Result::NotImplemented;
        }

        outAllocInfo = GetResourceAllocationInfoNative(inOutResourceDesc);
        return outAllocInfo.sizeInBytes != UINT64_MAX ? Result::Ok : Result::InvalidArgument;
    }

    Result AllocatorPimpl::GetResourceAllocationInfo(
        ResourceDesc& inOutResourceDesc,
        UINT32 NumCastableFormats, const Format* pCastableFormats,
        ResourceAllocationInfo& outAllocInfo) const
    {

#if D3D12MA_TIGHT_ALIGNMENT_SUPPORTED
        if (IsTightAlignmentEnabled() &&
            // Don't allow USE_TIGHT_ALIGNMENT together with ALLOW_CROSS_ADAPTER as there is a D3D Debug Layer error:
            // D3D12 ERROR: ID3D12Device::GetResourceAllocationInfo: D3D12_RESOURCE_DESC::Flag D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT will be ignored since D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER is set. [ STATE_CREATION ERROR #599: CREATERESOURCE_INVALIDMISCFLAGS]
            (inOutResourceDesc.resourceFlags & ResourceFlags::RF_AllowCrossAdapter) == 0)
        {
            inOutResourceDesc.resourceFlags |= ResourceFlags::RF_UseTightAlignment;
        }
#endif // #if D3D12MA_TIGHT_ALIGNMENT_SUPPORTED

        /* Optional optimization: Microsoft documentation of the ID3D12Device::
        GetResourceAllocationInfo function says:

        Your application can forgo using GetResourceAllocationInfo for buffer resources
        (D3D12_RESOURCE_DIMENSION_BUFFER). Buffers have the same size on all adapters,
        which is merely the smallest multiple of 64KB that's greater or equal to
        D3D12_RESOURCE_DESC::Width.
        */

        // Query alignment
		ResourceAllocationInfo defaultInfo = GetResourceAllocationInfoNative(inOutResourceDesc);

        //if (defaultInfo.alignment == 0 &&
        //    inOutResourceDesc.type == ResourceType::Buffer &&
        //    !IsTightAlignmentEnabled())
        //{
        //    outAllocInfo = {
        //        AlignUp<UINT64>(inOutResourceDesc.buffer.sizeBytes, DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), // SizeInBytes
        //        DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT }; // Alignment
        //    return Result::Ok;
        //}


        Result hr = Result::Ok;

#if D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
        if (//inOutResourceDesc.Alignment == 0 &&
            (inOutResourceDesc.resourceFlags & D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT_COPY) == 0 &&
            (inOutResourceDesc.type == ResourceType::Texture1D ||
                inOutResourceDesc.type == ResourceType::Texture2D ||
                inOutResourceDesc.type == ResourceType::Texture3D) &&
            (inOutResourceDesc.resourceFlags & (ResourceFlags::RF_AllowRenderTarget | ResourceFlags::RF_AllowDepthStencil)) == 0
#if D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT == 1
            && CanUseSmallAlignment(inOutResourceDesc)
#endif
            )
        {
            /*
            The algorithm here is based on Microsoft sample: "Small Resources Sample"
            https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12SmallResources
            */
            const UINT64 smallAlignmentToTry = inOutResourceDesc.texture.sampleCount > 1 ?
                SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT :
                SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
            //inOutResourceDesc.Alignment = smallAlignmentToTry;
            hr = GetResourceAllocationInfoMiddle(
                inOutResourceDesc, NumCastableFormats, pCastableFormats, outAllocInfo);
            // Check if alignment requested has been granted.
            if (IsOk(hr) && outAllocInfo.alignment == smallAlignmentToTry)
            {
                return Result::Ok;
            }
            //inOutResourceDesc.Alignment = 0; // Restore original
        }
#endif // #if D3D12MA_USE_SMALL_RESOURCE_PLACEMENT_ALIGNMENT

        return GetResourceAllocationInfoMiddle(
            inOutResourceDesc, NumCastableFormats, pCastableFormats, outAllocInfo);
    }

    bool AllocatorPimpl::NewAllocationWithinBudget(HeapType heapType, UINT64 size)
    {
        Budget budget = {};
        GetBudgetForHeapType(budget, heapType);
        return budget.usageBytes + size <= budget.budgetBytes;
    }

    void AllocatorPimpl::WriteBudgetToJson(JsonWriter& json, const Budget& budget)
    {
        json.BeginObject();
        {
            json.WriteString("BudgetBytes");
            json.WriteNumber(budget.budgetBytes);
            json.WriteString("UsageBytes");
            json.WriteNumber(budget.usageBytes);
        }
        json.EndObject();
    }

#endif // _D3D12MA_ALLOCATOR_PIMPL_FUNCTIONS
#endif // _D3D12MA_ALLOCATOR_PIMPL

#ifndef _D3D12MA_VIRTUAL_BLOCK_PIMPL
    class VirtualBlockPimpl
    {
    public:
        const AllocationCallbacks m_AllocationCallbacks;
        const UINT64 m_Size;
        BlockMetadata* m_Metadata;

        VirtualBlockPimpl(const AllocationCallbacks& allocationCallbacks, const VirtualBlockDesc& desc);
        ~VirtualBlockPimpl();
    };

#ifndef _D3D12MA_VIRTUAL_BLOCK_PIMPL_FUNCTIONS
    VirtualBlockPimpl::VirtualBlockPimpl(const AllocationCallbacks& allocationCallbacks, const VirtualBlockDesc& desc)
        : m_AllocationCallbacks(allocationCallbacks), m_Size(desc.size)
    {
        switch (desc.flags & VirtualBlockFlags::AlgorithmMask)
        {
        case VirtualBlockFlags::AlgorithmLinear:
            m_Metadata = D3D12MA_NEW(allocationCallbacks, BlockMetadata_Linear)(&m_AllocationCallbacks, true);
            break;
        case VirtualBlockFlags::None :
            m_Metadata = D3D12MA_NEW(allocationCallbacks, BlockMetadata_TLSF)(&m_AllocationCallbacks, true);
            break;
        default:
            D3D12MA_ASSERT(0);
        }
        m_Metadata->Init(m_Size);
    }

    VirtualBlockPimpl::~VirtualBlockPimpl()
    {
        D3D12MA_DELETE(m_AllocationCallbacks, m_Metadata);
    }
#endif // _D3D12MA_VIRTUAL_BLOCK_PIMPL_FUNCTIONS
#endif // _D3D12MA_VIRTUAL_BLOCK_PIMPL


#ifndef _D3D12MA_MEMORY_BLOCK_FUNCTIONS
    MemoryBlock::MemoryBlock(
        AllocatorPimpl* allocator,
        const HeapProperties& heapProps,
        HeapFlags heapFlags,
        UINT64 size,
        UINT id)
        : m_Allocator(allocator),
        m_HeapProps(heapProps),
        m_HeapFlags(heapFlags),
        m_Size(size),
        m_Id(id) {
    }

    MemoryBlock::~MemoryBlock()
    {
        if (m_Heap->IsValid())
        {
            m_Heap.Reset();
            m_Allocator->m_Budget.RemoveBlock(
                m_Allocator->HeapPropertiesToMemorySegmentGroup(m_HeapProps), m_Size);
        }
    }

    Result MemoryBlock::Init(/*ID3D12ProtectedResourceSession* pProtectedSession, */bool denyMsaaTextures)
    {
        D3D12MA_ASSERT((!m_Heap->IsValid()) && m_Size > 0);

        HeapDesc heapDesc = {};
        heapDesc.sizeBytes = m_Size;
        heapDesc.memory = m_HeapProps.type;
        heapDesc.alignment = HeapFlagsToAlignment(m_HeapFlags, denyMsaaTextures);
        heapDesc.flags = m_HeapFlags;

        Result hr = m_Allocator->GetDevice().CreateHeap(heapDesc, m_Heap);

        if (IsOk(hr))
        {
            m_Allocator->m_Budget.AddBlock(
                m_Allocator->HeapPropertiesToMemorySegmentGroup(m_HeapProps), m_Size);
        }
        return hr;
    }
#endif // _D3D12MA_MEMORY_BLOCK_FUNCTIONS

#ifndef _D3D12MA_NORMAL_BLOCK_FUNCTIONS
    NormalBlock::NormalBlock(
        AllocatorPimpl* allocator,
        BlockVector* blockVector,
        const HeapProperties& heapProps,
        HeapFlags heapFlags,
        UINT64 size,
        UINT id)
        : MemoryBlock(allocator, heapProps, heapFlags, size, id),
        m_pMetadata(NULL),
        m_BlockVector(blockVector) {
    }

    NormalBlock::~NormalBlock()
    {
        if (m_pMetadata != NULL)
        {
            // Define macro D3D12MA_DEBUG_LOG to receive the list of the unfreed allocations.
            if (!m_pMetadata->IsEmpty())
                m_pMetadata->DebugLogAllAllocations();

            // THIS IS THE MOST IMPORTANT ASSERT IN THE ENTIRE LIBRARY!
            // Hitting it means you have some memory leak - unreleased Allocation objects.
            D3D12MA_ASSERT(m_pMetadata->IsEmpty() && "Some allocations were not freed before destruction of this memory block!");

            D3D12MA_DELETE(m_Allocator->GetAllocs(), m_pMetadata);
        }
    }

    Result NormalBlock::Init(UINT32 algorithm,/* ID3D12ProtectedResourceSession* pProtectedSession,*/ bool denyMsaaTextures)
    {
        auto hr = MemoryBlock::Init(/*pProtectedSession,*/ denyMsaaTextures);
        if (Failed(hr))
        {
            return hr;
        }

        switch (algorithm)
        {
        case PoolFlagsAlgorithmLinear:
            m_pMetadata = D3D12MA_NEW(m_Allocator->GetAllocs(), BlockMetadata_Linear)(&m_Allocator->GetAllocs(), false);
            break;
        default:
            D3D12MA_ASSERT(0);
        case 0:
            m_pMetadata = D3D12MA_NEW(m_Allocator->GetAllocs(), BlockMetadata_TLSF)(&m_Allocator->GetAllocs(), false);
            break;
        }
        m_pMetadata->Init(m_Size);

        return hr;
    }

    bool NormalBlock::Validate() const
    {
        D3D12MA_VALIDATE(GetHeap().valid() &&
            m_pMetadata &&
            m_pMetadata->GetSize() != 0 &&
            m_pMetadata->GetSize() == GetSize());
        return m_pMetadata->Validate();
    }
#endif // _D3D12MA_NORMAL_BLOCK_FUNCTIONS

#ifndef _D3D12MA_COMMITTED_ALLOCATION_LIST_FUNCTIONS
    void CommittedAllocationList::Init(bool useMutex, HeapType heapType, PoolPimpl* pool)
    {
        m_UseMutex = useMutex;
        m_HeapType = heapType;
        m_Pool = pool;
    }

    CommittedAllocationList::~CommittedAllocationList()
    {
        if (!m_AllocationList.IsEmpty())
        {
            D3D12MA_ASSERT(0 && "Unfreed committed allocations found!");
        }
    }

    UINT CommittedAllocationList::GetMemorySegmentGroup(AllocatorPimpl* allocator) const
    {
        if (m_Pool)
            return allocator->HeapPropertiesToMemorySegmentGroup({ m_Pool->GetDesc().heapType });
        else
            return allocator->StandardHeapTypeToMemorySegmentGroup(m_HeapType);
    }

    void CommittedAllocationList::AddStatistics(Statistics& inoutStats)
    {
        MutexLockRead lock(m_Mutex, m_UseMutex);

        for (Allocation* alloc = m_AllocationList.Front();
            alloc != nullptr; alloc = m_AllocationList.GetNext(alloc))
        {
            const UINT64 size = alloc->GetSize();
            inoutStats.blockCount++;
            inoutStats.allocationCount++;
            inoutStats.blockBytes += size;
            inoutStats.allocationBytes += size;
        }
    }

    void CommittedAllocationList::AddDetailedStatistics(DetailedStatistics& inoutStats)
    {
        MutexLockRead lock(m_Mutex, m_UseMutex);

        for (Allocation* alloc = m_AllocationList.Front();
            alloc != nullptr; alloc = m_AllocationList.GetNext(alloc))
        {
            const UINT64 size = alloc->GetSize();
            inoutStats.stats.blockCount++;
            inoutStats.stats.blockBytes += size;
            AddDetailedStatisticsAllocation(inoutStats, size);
        }
    }

    void CommittedAllocationList::BuildStatsString(JsonWriter& json)
    {
        MutexLockRead lock(m_Mutex, m_UseMutex);

        for (Allocation* alloc = m_AllocationList.Front();
            alloc != nullptr; alloc = m_AllocationList.GetNext(alloc))
        {
            json.BeginObject(true);
            json.AddAllocationToObject(*alloc);
            json.EndObject();
        }
    }

    void CommittedAllocationList::Register(Allocation* alloc)
    {
        MutexLockWrite lock(m_Mutex, m_UseMutex);
        m_AllocationList.PushBack(alloc);
    }

    void CommittedAllocationList::Unregister(Allocation* alloc)
    {
        MutexLockWrite lock(m_Mutex, m_UseMutex);
        m_AllocationList.Remove(alloc);
    }
#endif // _D3D12MA_COMMITTED_ALLOCATION_LIST_FUNCTIONS

#ifndef _D3D12MA_BLOCK_VECTOR_FUNCTIONS
    BlockVector::BlockVector(
        AllocatorPimpl* hAllocator,
        const HeapProperties& heapProps,
        HeapFlags heapFlags,
        UINT64 preferredBlockSize,
        size_t minBlockCount,
        size_t maxBlockCount,
        bool explicitBlockSize,
        UINT64 minAllocationAlignment,
        UINT32 algorithm,
        bool denyMsaaTextures,
        //ID3D12ProtectedResourceSession* pProtectedSession,
        ResidencyPriority residencyPriority)
        : m_hAllocator(hAllocator),
        m_HeapProps(heapProps),
        m_HeapFlags(heapFlags),
        m_PreferredBlockSize(preferredBlockSize),
        m_MinBlockCount(minBlockCount),
        m_MaxBlockCount(maxBlockCount),
        m_ExplicitBlockSize(explicitBlockSize),
        m_MinAllocationAlignment(D3D12MA_MAX(minAllocationAlignment, static_cast<UINT64>(D3D12MA_DEBUG_ALIGNMENT))),
        m_Algorithm(algorithm),
        m_DenyMsaaTextures(denyMsaaTextures),
        //m_ProtectedSession(pProtectedSession),
        m_ResidencyPriority(residencyPriority),
        m_HasEmptyBlock(false),
        m_Blocks(hAllocator->GetAllocs()),
        m_NextBlockId(0) {
    }

    BlockVector::~BlockVector()
    {
        for (size_t i = m_Blocks.size(); i--; )
        {
            D3D12MA_DELETE(m_hAllocator->GetAllocs(), m_Blocks[i]);
        }
    }

    Result BlockVector::CreateMinBlocks()
    {
        for (size_t i = 0; i < m_MinBlockCount; ++i)
        {
            Result hr = CreateBlock(m_PreferredBlockSize, nullptr);
            if (Failed(hr))
            {
                return hr;
            }
        }
        return Result::Ok;
    }

    bool BlockVector::IsEmpty()
    {
        MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());
        return m_Blocks.empty();
    }

    Result BlockVector::Allocate(
        UINT64 size,
        UINT64 alignment,
        const AllocationDesc& allocDesc,
        bool committedAllowed,
        size_t allocationCount,
        Allocation** pAllocations)
    {
        size_t allocIndex;
        Result hr = Result::Ok;

        {
            MutexLockWrite lock(m_Mutex, m_hAllocator->UseMutex());
            for (allocIndex = 0; allocIndex < allocationCount; ++allocIndex)
            {
                hr = AllocatePage(
                    size,
                    alignment,
                    allocDesc,
                    committedAllowed,
                    pAllocations + allocIndex);
                if (Failed(hr))
                {
                    break;
                }
            }
        }

        if (Failed(hr))
        {
            // Free all already created allocations.
            while (allocIndex--)
            {
                Free(pAllocations[allocIndex]);
            }
            ZeroMemory(pAllocations, sizeof(Allocation*) * allocationCount);
        }

        return hr;
    }

    void BlockVector::Free(Allocation* hAllocation)
    {
        NormalBlock* pBlockToDelete = nullptr;

        bool budgetExceeded = false;
        if (IsHeapTypeStandard(m_HeapProps.type))
        {
            Budget budget = {};
            m_hAllocator->GetBudgetForHeapType(budget, m_HeapProps.type);
            budgetExceeded = budget.usageBytes >= budget.budgetBytes;
        }

        // Scope for lock.
        {
            MutexLockWrite lock(m_Mutex, m_hAllocator->UseMutex());

            NormalBlock* pBlock = hAllocation->m_Placed.block;

            pBlock->m_pMetadata->Free(hAllocation->GetAllocHandle());
            D3D12MA_HEAVY_ASSERT(pBlock->Validate());

            const size_t blockCount = m_Blocks.size();
            // pBlock became empty after this deallocation.
            if (pBlock->m_pMetadata->IsEmpty())
            {
                // Already has empty Allocation. We don't want to have two, so delete this one.
                if ((m_HasEmptyBlock || budgetExceeded) &&
                    blockCount > m_MinBlockCount)
                {
                    pBlockToDelete = pBlock;
                    Remove(pBlock);
                }
                // We now have first empty block.
                else
                {
                    m_HasEmptyBlock = true;
                }
            }
            // pBlock didn't become empty, but we have another empty block - find and free that one.
            // (This is optional, heuristics.)
            else if (m_HasEmptyBlock && blockCount > m_MinBlockCount)
            {
                NormalBlock* pLastBlock = m_Blocks.back();
                if (pLastBlock->m_pMetadata->IsEmpty())
                {
                    pBlockToDelete = pLastBlock;
                    m_Blocks.pop_back();
                    m_HasEmptyBlock = false;
                }
            }

            IncrementallySortBlocks();
        }

        // Destruction of a free Allocation. Deferred until this point, outside of mutex
        // lock, for performance reason.
        if (pBlockToDelete != nullptr)
        {
            D3D12MA_DELETE(m_hAllocator->GetAllocs(), pBlockToDelete);
        }
    }

    Result BlockVector::CreateResource(
        UINT64 size,
        UINT64 alignment,
        const AllocationDesc& allocDesc,
        const CreateResourceParams& createParams,
        bool committedAllowed,
        Allocation** ppOutAllocation,
        ResourcePtr ptr)
    {
        Result hr = Allocate(size, alignment, allocDesc, committedAllowed, 1, ppOutAllocation);
        if (Failed(hr))
        {
            return hr;
        }

        hr = m_hAllocator->CreatePlacedResourceWrap(
            (*ppOutAllocation)->m_Placed.block->GetHeap(),
            (*ppOutAllocation)->GetOffset(),
            createParams,
            ptr);
        if (IsOk(hr))
        {
                (*ppOutAllocation)->SetResourcePointer(std::move(ptr), createParams.GetResourceDesc());
        }
        return hr;
    }

    void BlockVector::AddStatistics(Statistics& inoutStats)
    {
        MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());

        for (const NormalBlock* const pBlock : m_Blocks)
        {
            D3D12MA_ASSERT(pBlock);
            D3D12MA_HEAVY_ASSERT(pBlock->Validate());
            pBlock->m_pMetadata->AddStatistics(inoutStats);
        }
    }

    void BlockVector::AddDetailedStatistics(DetailedStatistics& inoutStats)
    {
        MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());

        for (const NormalBlock* const pBlock : m_Blocks)
        {
            D3D12MA_ASSERT(pBlock);
            D3D12MA_HEAVY_ASSERT(pBlock->Validate());
            pBlock->m_pMetadata->AddDetailedStatistics(inoutStats);
        }
    }

    void BlockVector::WriteBlockInfoToJson(JsonWriter& json)
    {
        MutexLockRead lock(m_Mutex, m_hAllocator->UseMutex());

        json.BeginObject();

        for (const NormalBlock* const pBlock : m_Blocks)
        {
            D3D12MA_ASSERT(pBlock);
            D3D12MA_HEAVY_ASSERT(pBlock->Validate());
            json.BeginString();
            json.ContinueString(pBlock->GetId());
            json.EndString();

            json.BeginObject();
            pBlock->m_pMetadata->WriteAllocationInfoToJson(json);
            json.EndObject();
        }

        json.EndObject();
    }

    UINT64 BlockVector::CalcSumBlockSize() const
    {
        UINT64 result = 0;
        for (size_t i = m_Blocks.size(); i--; )
        {
            result += m_Blocks[i]->m_pMetadata->GetSize();
        }
        return result;
    }

    UINT64 BlockVector::CalcMaxBlockSize() const
    {
        UINT64 result = 0;
        for (size_t i = m_Blocks.size(); i--; )
        {
            result = D3D12MA_MAX(result, m_Blocks[i]->m_pMetadata->GetSize());
            if (result >= m_PreferredBlockSize)
            {
                break;
            }
        }
        return result;
    }

    void BlockVector::Remove(NormalBlock* pBlock)
    {
        for (size_t blockIndex = 0; blockIndex < m_Blocks.size(); ++blockIndex)
        {
            if (m_Blocks[blockIndex] == pBlock)
            {
                m_Blocks.remove(blockIndex);
                return;
            }
        }
        D3D12MA_ASSERT(0);
    }

    void BlockVector::IncrementallySortBlocks()
    {
        if (!m_IncrementalSort)
            return;
        // Bubble sort only until first swap.
        for (size_t i = 1; i < m_Blocks.size(); ++i)
        {
            if (m_Blocks[i - 1]->m_pMetadata->GetSumFreeSize() > m_Blocks[i]->m_pMetadata->GetSumFreeSize())
            {
                D3D12MA_SWAP(m_Blocks[i - 1], m_Blocks[i]);
                return;
            }
        }
    }

    void BlockVector::SortByFreeSize()
    {
        D3D12MA_SORT(m_Blocks.begin(), m_Blocks.end(),
            [](auto* b1, auto* b2)
            {
                return b1->m_pMetadata->GetSumFreeSize() < b2->m_pMetadata->GetSumFreeSize();
            });
    }

    Result BlockVector::AllocatePage(
        UINT64 size,
        UINT64 alignment,
        const AllocationDesc& allocDesc,
        bool committedAllowed,
        Allocation** pAllocation)
    {
        // Early reject: requested allocation size is larger that maximum block size for this block vector.
        if (size + D3D12MA_DEBUG_MARGIN > m_PreferredBlockSize)
        {
            return Result::OutOfMemory;
        }

        UINT64 freeMemory = UINT64_MAX;
        if (IsHeapTypeStandard(m_HeapProps.type))
        {
            Budget budget = {};
            m_hAllocator->GetBudgetForHeapType(budget, m_HeapProps.type);
            freeMemory = (budget.usageBytes < budget.budgetBytes) ? (budget.budgetBytes - budget.usageBytes) : 0;
        }

        const bool canExceedFreeMemory = !committedAllowed;

        bool canCreateNewBlock =
            ((allocDesc.flags & AllocationFlagNeverAllocate) == 0) &&
            (m_Blocks.size() < m_MaxBlockCount);

        // Even if we don't have to stay within budget with this allocation, when the
        // budget would be exceeded, we don't want to allocate new blocks, but always
        // create resources as committed.
        if (freeMemory < size && !canExceedFreeMemory)
        {
            canCreateNewBlock = false;
        }

        // 1. Search existing allocations
        {
            // Forward order in m_Blocks - prefer blocks with smallest amount of free space.
            for (size_t blockIndex = 0; blockIndex < m_Blocks.size(); ++blockIndex)
            {
                NormalBlock* const pCurrBlock = m_Blocks[blockIndex];
                D3D12MA_ASSERT(pCurrBlock);
                auto hr = AllocateFromBlock(
                    pCurrBlock,
                    size,
                    alignment,
                    allocDesc.flags,
                    allocDesc.privateData,
                    allocDesc.flags & AllocationFlagStrategyMask,
                    pAllocation);
                if (IsOk(hr))
                {
                    return hr;
                }
            }
        }

        // 2. Try to create new block.
        if (canCreateNewBlock)
        {
            // Calculate optimal size for new block.
            UINT64 newBlockSize = m_PreferredBlockSize;
            UINT newBlockSizeShift = 0;

            if (!m_ExplicitBlockSize)
            {
                // Allocate 1/8, 1/4, 1/2 as first blocks.
                const UINT64 maxExistingBlockSize = CalcMaxBlockSize();
                for (UINT i = 0; i < NEW_BLOCK_SIZE_SHIFT_MAX; ++i)
                {
                    const UINT64 smallerNewBlockSize = newBlockSize / 2;
                    if (smallerNewBlockSize > maxExistingBlockSize && smallerNewBlockSize >= size * 2)
                    {
                        newBlockSize = smallerNewBlockSize;
                        ++newBlockSizeShift;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            size_t newBlockIndex = SIZE_MAX;
            Result hr = Result::OutOfMemory;
            if (newBlockSize <= freeMemory || canExceedFreeMemory)
            {
                hr = CreateBlock(newBlockSize, &newBlockIndex);
            }
            // Allocation of this size failed? Try 1/2, 1/4, 1/8 of m_PreferredBlockSize.
            if (!m_ExplicitBlockSize)
            {
                while (Failed(hr) && newBlockSizeShift < NEW_BLOCK_SIZE_SHIFT_MAX)
                {
                    const UINT64 smallerNewBlockSize = newBlockSize / 2;
                    if (smallerNewBlockSize < size)
                    {
                        break;
                    }

                    newBlockSize = smallerNewBlockSize;
                    ++newBlockSizeShift;
                    if (newBlockSize <= freeMemory || canExceedFreeMemory)
                    {
                        hr = CreateBlock(newBlockSize, &newBlockIndex);
                    }
                }
            }

            if (IsOk(hr))
            {
                NormalBlock* const pBlock = m_Blocks[newBlockIndex];
                D3D12MA_ASSERT(pBlock->m_pMetadata->GetSize() >= size);

                hr = AllocateFromBlock(
                    pBlock,
                    size,
                    alignment,
                    allocDesc.flags,
                    allocDesc.privateData,
                    allocDesc.flags & AllocationFlagStrategyMask,
                    pAllocation);
                if (IsOk(hr))
                {
                    return hr;
                }
                else
                {
                    // Allocation from new block failed, possibly due to D3D12MA_DEBUG_MARGIN or alignment.
                    return Result::OutOfMemory;
                }
            }
        }

        return Result::OutOfMemory;
    }

    Result BlockVector::AllocateFromBlock(
        NormalBlock* pBlock,
        UINT64 size,
        UINT64 alignment,
        AllocationFlags allocFlags,
        void* pPrivateData,
        UINT32 strategy,
        Allocation** pAllocation)
    {
        alignment = D3D12MA_MAX(alignment, m_MinAllocationAlignment);

        AllocationRequest currRequest = {};
        if (pBlock->m_pMetadata->CreateAllocationRequest(
            size,
            alignment,
            allocFlags & AllocationFlagUpperAddress,
            strategy,
            &currRequest))
        {
            return CommitAllocationRequest(currRequest, pBlock, size, alignment, pPrivateData, pAllocation);
        }
        return Result::OutOfMemory;
    }

    Result BlockVector::CommitAllocationRequest(
        AllocationRequest& allocRequest,
        NormalBlock* pBlock,
        UINT64 size,
        UINT64 alignment,
        void* pPrivateData,
        Allocation** pAllocation)
    {
        // We no longer have an empty Allocation.
        if (pBlock->m_pMetadata->IsEmpty())
            m_HasEmptyBlock = false;

        *pAllocation = m_hAllocator->GetAllocationObjectAllocator().Allocate(m_hAllocator, size, alignment);
        pBlock->m_pMetadata->Alloc(allocRequest, size, *pAllocation);

        (*pAllocation)->InitPlaced(allocRequest.allocHandle, pBlock);
        (*pAllocation)->SetPrivateData(pPrivateData);

        D3D12MA_HEAVY_ASSERT(pBlock->Validate());
        m_hAllocator->m_Budget.AddAllocation(m_hAllocator->HeapPropertiesToMemorySegmentGroup(m_HeapProps), size);

        return Result::Ok;
    }

    Result BlockVector::CreateBlock(
        UINT64 blockSize,
        size_t* pNewBlockIndex)
    {
        NormalBlock* const pBlock = D3D12MA_NEW(m_hAllocator->GetAllocs(), NormalBlock)(
            m_hAllocator,
            this,
            m_HeapProps,
            m_HeapFlags,
            blockSize,
            m_NextBlockId++);
        auto hr = pBlock->Init(m_Algorithm/*, m_ProtectedSession*/, m_DenyMsaaTextures);
        if (Failed(hr))
        {
            D3D12MA_DELETE(m_hAllocator->GetAllocs(), pBlock);
            return hr;
        }

        m_hAllocator->SetResidencyPriority(pBlock->GetHeap(), m_ResidencyPriority);

        m_Blocks.push_back(pBlock);
        if (pNewBlockIndex != NULL)
        {
            *pNewBlockIndex = m_Blocks.size() - 1;
        }

        return hr;
    }
#endif // _D3D12MA_BLOCK_VECTOR_FUNCTIONS

#ifndef _D3D12MA_DEFRAGMENTATION_CONTEXT_PIMPL_FUNCTIONS
    DefragmentationContextPimpl::DefragmentationContextPimpl(
        AllocatorPimpl* hAllocator,
        const DefragmentationDesc& desc,
        BlockVector* poolVector)
        : m_MaxPassBytes(desc.maxBytesPerPass == 0 ? UINT64_MAX : desc.maxBytesPerPass),
        m_MaxPassAllocations(desc.maxAllocationsPerPass == 0 ? UINT32_MAX : desc.maxAllocationsPerPass),
        m_Moves(hAllocator->GetAllocs())
    {
        m_Algorithm = desc.flags & DefragmentationFlagsAlgorithmMask;

        if (poolVector != NULL)
        {
            m_BlockVectorCount = 1;
            m_PoolBlockVector = poolVector;
            m_pBlockVectors = &m_PoolBlockVector;
            m_PoolBlockVector->SetIncrementalSort(false);
            m_PoolBlockVector->SortByFreeSize();
        }
        else
        {
            m_BlockVectorCount = hAllocator->GetDefaultPoolCount();
            m_PoolBlockVector = NULL;
            m_pBlockVectors = hAllocator->GetDefaultPools();
            for (UINT32 i = 0; i < m_BlockVectorCount; ++i)
            {
                BlockVector* vector = m_pBlockVectors[i];
                if (vector != NULL)
                {
                    vector->SetIncrementalSort(false);
                    vector->SortByFreeSize();
                }
            }
        }

        switch (m_Algorithm)
        {
        case 0: // Default algorithm
            m_Algorithm = DefragmentationFlagsAlgorithmBalanced;
        case DefragmentationFlagsAlgorithmBalanced:
        {
            m_AlgorithmState = D3D12MA_NEW_ARRAY(hAllocator->GetAllocs(), StateBalanced, m_BlockVectorCount);
            break;
        }
        }
    }

    DefragmentationContextPimpl::~DefragmentationContextPimpl()
    {
        if (m_PoolBlockVector != NULL)
            m_PoolBlockVector->SetIncrementalSort(true);
        else
        {
            for (UINT32 i = 0; i < m_BlockVectorCount; ++i)
            {
                BlockVector* vector = m_pBlockVectors[i];
                if (vector != NULL)
                    vector->SetIncrementalSort(true);
            }
        }

        if (m_AlgorithmState)
        {
            switch (m_Algorithm)
            {
            case DefragmentationFlagsAlgorithmBalanced:
                D3D12MA_DELETE_ARRAY(m_Moves.GetAllocs(), reinterpret_cast<StateBalanced*>(m_AlgorithmState), m_BlockVectorCount);
                break;
            default:
                D3D12MA_ASSERT(0);
            }
        }
    }

    Result DefragmentationContextPimpl::DefragmentPassBegin(DefragmentationPassMoveInfo& moveInfo)
    {
        if (m_PoolBlockVector != NULL)
        {
            MutexLockWrite lock(m_PoolBlockVector->GetMutex(), m_PoolBlockVector->m_hAllocator->UseMutex());

            if (m_PoolBlockVector->GetBlockCount() > 1)
                ComputeDefragmentation(*m_PoolBlockVector, 0);
            else if (m_PoolBlockVector->GetBlockCount() == 1)
                ReallocWithinBlock(*m_PoolBlockVector, m_PoolBlockVector->GetBlock(0));

            // Setup index into block vector
            for (size_t i = 0; i < m_Moves.size(); ++i)
                m_Moves[i].pDstTmpAllocation->SetPrivateData(0);
        }
        else
        {
            for (UINT32 i = 0; i < m_BlockVectorCount; ++i)
            {
                if (m_pBlockVectors[i] != NULL)
                {
                    MutexLockWrite lock(m_pBlockVectors[i]->GetMutex(), m_pBlockVectors[i]->m_hAllocator->UseMutex());

                    bool end = false;
                    size_t movesOffset = m_Moves.size();
                    if (m_pBlockVectors[i]->GetBlockCount() > 1)
                    {
                        end = ComputeDefragmentation(*m_pBlockVectors[i], i);
                    }
                    else if (m_pBlockVectors[i]->GetBlockCount() == 1)
                    {
                        end = ReallocWithinBlock(*m_pBlockVectors[i], m_pBlockVectors[i]->GetBlock(0));
                    }

                    // Setup index into block vector
                    for (; movesOffset < m_Moves.size(); ++movesOffset)
                        m_Moves[movesOffset].pDstTmpAllocation->SetPrivateData(reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

                    if (end)
                        break;
                }
            }
        }

        moveInfo.moveCount = static_cast<UINT32>(m_Moves.size());
        if (moveInfo.moveCount > 0)
        {
            moveInfo.pMoves = m_Moves.data();
            return Result::False;
        }

        moveInfo.pMoves = nullptr;
        return Result::Ok;
    }

    Result DefragmentationContextPimpl::DefragmentPassEnd(DefragmentationPassMoveInfo& moveInfo)
    {
        D3D12MA_ASSERT(moveInfo.moveCount > 0 ? moveInfo.pMoves != NULL : true);

        Result result = Result::Ok;
        Vector<FragmentedBlock> immovableBlocks(m_Moves.GetAllocs());

        for (uint32_t i = 0; i < moveInfo.moveCount; ++i)
        {
            DefragmentationMove& move = moveInfo.pMoves[i];
            size_t prevCount = 0, currentCount = 0;
            UINT64 freedBlockSize = 0;

            UINT32 vectorIndex;
            BlockVector* vector;
            if (m_PoolBlockVector != nullptr)
            {
                vectorIndex = 0;
                vector = m_PoolBlockVector;
            }
            else
            {
                vectorIndex = static_cast<UINT32>(reinterpret_cast<uintptr_t>(move.pDstTmpAllocation->GetPrivateData()));
                vector = m_pBlockVectors[vectorIndex];
                D3D12MA_ASSERT(vector != NULL);
            }

            switch (move.operation)
            {
            case DefragmentationMoveOperation::Copy:
            {
                move.pSrcAllocation->SwapBlockAllocation(move.pDstTmpAllocation);

                // Scope for locks, Free have it's own lock
                {
                    MutexLockRead lock(vector->GetMutex(), vector->m_hAllocator->UseMutex());
                    prevCount = vector->GetBlockCount();
                    freedBlockSize = move.pDstTmpAllocation->GetBlock()->m_pMetadata->GetSize();
                }
                move.pDstTmpAllocation->ReleaseThis(); // TODO: Are these ReleaseThis calls acceptable? Original uses refcounted ptrs.
                {
                    MutexLockRead lock(vector->GetMutex(), vector->m_hAllocator->UseMutex());
                    currentCount = vector->GetBlockCount();
                }

                result = Result::False;
                break;
            }
            case DefragmentationMoveOperation::Ignore:
            {
                m_PassStats.bytesMoved -= move.pSrcAllocation->GetSize();
                --m_PassStats.allocationsMoved;
                move.pDstTmpAllocation->ReleaseThis();

                NormalBlock* newBlock = move.pSrcAllocation->GetBlock();
                bool notPresent = true;
                for (const FragmentedBlock& block : immovableBlocks)
                {
                    if (block.block == newBlock)
                    {
                        notPresent = false;
                        break;
                    }
                }
                if (notPresent)
                    immovableBlocks.push_back({ vectorIndex, newBlock });
                break;
            }
            case DefragmentationMoveOperation::Destroy:
            {
                m_PassStats.bytesMoved -= move.pSrcAllocation->GetSize();
                --m_PassStats.allocationsMoved;
                // Scope for locks, Free have it's own lock
                {
                    MutexLockRead lock(vector->GetMutex(), vector->m_hAllocator->UseMutex());
                    prevCount = vector->GetBlockCount();
                    freedBlockSize = move.pSrcAllocation->GetBlock()->m_pMetadata->GetSize();
                }
                move.pSrcAllocation->ReleaseThis();
                {
                    MutexLockRead lock(vector->GetMutex(), vector->m_hAllocator->UseMutex());
                    currentCount = vector->GetBlockCount();
                }
                freedBlockSize *= prevCount - currentCount;

                UINT64 dstBlockSize;
                {
                    MutexLockRead lock(vector->GetMutex(), vector->m_hAllocator->UseMutex());
                    dstBlockSize = move.pDstTmpAllocation->GetBlock()->m_pMetadata->GetSize();
                }
                move.pDstTmpAllocation->ReleaseThis();
                {
                    MutexLockRead lock(vector->GetMutex(), vector->m_hAllocator->UseMutex());
                    freedBlockSize += dstBlockSize * (currentCount - vector->GetBlockCount());
                    currentCount = vector->GetBlockCount();
                }

                result = Result::False;
                break;
            }
            default:
                D3D12MA_ASSERT(0);
            }

            if (prevCount > currentCount)
            {
                size_t freedBlocks = prevCount - currentCount;
                m_PassStats.heapsFreed += static_cast<UINT32>(freedBlocks);
                m_PassStats.bytesFreed += freedBlockSize;
            }
        }
        moveInfo.moveCount = 0;
        moveInfo.pMoves = nullptr;
        m_Moves.clear();

        // Update stats
        m_GlobalStats.allocationsMoved += m_PassStats.allocationsMoved;
        m_GlobalStats.bytesFreed += m_PassStats.bytesFreed;
        m_GlobalStats.bytesMoved += m_PassStats.bytesMoved;
        m_GlobalStats.heapsFreed += m_PassStats.heapsFreed;
        m_PassStats = { 0 };

        // Move blocks with immovable allocations according to algorithm
        if (immovableBlocks.size() > 0)
        {
            // Move to the begining
            for (const FragmentedBlock& block : immovableBlocks)
            {
                BlockVector* vector = m_pBlockVectors[block.data];
                MutexLockWrite lock(vector->GetMutex(), vector->m_hAllocator->UseMutex());

                for (size_t i = m_ImmovableBlockCount; i < vector->GetBlockCount(); ++i)
                {
                    if (vector->GetBlock(i) == block.block)
                    {
                        D3D12MA_SWAP(vector->m_Blocks[i], vector->m_Blocks[m_ImmovableBlockCount++]);
                        break;
                    }
                }
            }
        }
        return result;
    }

    bool DefragmentationContextPimpl::ComputeDefragmentation(BlockVector& vector, size_t index)
    {
        switch (m_Algorithm)
        {
        case DefragmentationFlagsAlgorithmFast:
            return ComputeDefragmentation_Fast(vector);
        default:
            D3D12MA_ASSERT(0);
        case DefragmentationFlagsAlgorithmBalanced:
            return ComputeDefragmentation_Balanced(vector, index, true);
        case DefragmentationFlagsAlgorithmFull:
            return ComputeDefragmentation_Full(vector);
        }
    }

    DefragmentationContextPimpl::MoveAllocationData DefragmentationContextPimpl::GetMoveData(
        AllocHandle handle, BlockMetadata* metadata)
    {
        MoveAllocationData moveData;
        moveData.move.pSrcAllocation = (Allocation*)metadata->GetAllocationPrivateData(handle);
        moveData.size = moveData.move.pSrcAllocation->GetSize();
        moveData.alignment = moveData.move.pSrcAllocation->GetAlignment();
        moveData.flags = AllocationFlagNone;

        return moveData;
    }

    DefragmentationContextPimpl::CounterStatus DefragmentationContextPimpl::CheckCounters(UINT64 bytes)
    {
        // Ignore allocation if will exceed max size for copy
        if (m_PassStats.bytesMoved + bytes > m_MaxPassBytes)
        {
            if (++m_IgnoredAllocs < MAX_ALLOCS_TO_IGNORE)
                return CounterStatus::Ignore;
            else
                return CounterStatus::End;
        }
        return CounterStatus::Pass;
    }

    bool DefragmentationContextPimpl::IncrementCounters(UINT64 bytes)
    {
        m_PassStats.bytesMoved += bytes;
        // Early return when max found
        if (++m_PassStats.allocationsMoved >= m_MaxPassAllocations || m_PassStats.bytesMoved >= m_MaxPassBytes)
        {
            D3D12MA_ASSERT((m_PassStats.allocationsMoved == m_MaxPassAllocations ||
                m_PassStats.bytesMoved == m_MaxPassBytes) && "Exceeded maximal pass threshold!");
            return true;
        }
        return false;
    }

    bool DefragmentationContextPimpl::ReallocWithinBlock(BlockVector& vector, NormalBlock* block)
    {
        BlockMetadata* metadata = block->m_pMetadata;

        for (AllocHandle handle = metadata->GetAllocationListBegin();
            handle != (AllocHandle)0;
            handle = metadata->GetNextAllocation(handle))
        {
            MoveAllocationData moveData = GetMoveData(handle, metadata);
            // Ignore newly created allocations by defragmentation algorithm
            if (moveData.move.pSrcAllocation->GetPrivateData() == this)
                continue;
            switch (CheckCounters(moveData.move.pSrcAllocation->GetSize()))
            {
            case CounterStatus::Ignore:
                continue;
            case CounterStatus::End:
                return true;
            default:
                D3D12MA_ASSERT(0);
            case CounterStatus::Pass:
                break;
            }

            UINT64 offset = moveData.move.pSrcAllocation->GetOffset();
            if (offset != 0 && metadata->GetSumFreeSize() >= moveData.size)
            {
                AllocationRequest request = {};
                if (metadata->CreateAllocationRequest(
                    moveData.size,
                    moveData.alignment,
                    false,
                    AllocationFlagStrategyMinOffset,
                    &request))
                {
                    if (metadata->GetAllocationOffset(request.allocHandle) < offset)
                    {
                        if (SUCCEEDED(vector.CommitAllocationRequest(
                            request,
                            block,
                            moveData.size,
                            moveData.alignment,
                            this,
                            &moveData.move.pDstTmpAllocation)))
                        {
                            m_Moves.push_back(moveData.move);
                            if (IncrementCounters(moveData.size))
                                return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    bool DefragmentationContextPimpl::AllocInOtherBlock(size_t start, size_t end, MoveAllocationData& data, BlockVector& vector)
    {
        for (; start < end; ++start)
        {
            NormalBlock* dstBlock = vector.GetBlock(start);
            if (dstBlock->m_pMetadata->GetSumFreeSize() >= data.size)
            {
                if (IsOk(vector.AllocateFromBlock(dstBlock,
                    data.size,
                    data.alignment,
                    data.flags,
                    this,
                    0,
                    &data.move.pDstTmpAllocation)))
                {
                    m_Moves.push_back(data.move);
                    if (IncrementCounters(data.size))
                        return true;
                    break;
                }
            }
        }
        return false;
    }

    bool DefragmentationContextPimpl::ComputeDefragmentation_Fast(BlockVector& vector)
    {
        // Move only between blocks

        // Go through allocations in last blocks and try to fit them inside first ones
        for (size_t i = vector.GetBlockCount() - 1; i > m_ImmovableBlockCount; --i)
        {
            BlockMetadata* metadata = vector.GetBlock(i)->m_pMetadata;

            for (AllocHandle handle = metadata->GetAllocationListBegin();
                handle != (AllocHandle)0;
                handle = metadata->GetNextAllocation(handle))
            {
                MoveAllocationData moveData = GetMoveData(handle, metadata);
                // Ignore newly created allocations by defragmentation algorithm
                if (moveData.move.pSrcAllocation->GetPrivateData() == this)
                    continue;
                switch (CheckCounters(moveData.move.pSrcAllocation->GetSize()))
                {
                case CounterStatus::Ignore:
                    continue;
                case CounterStatus::End:
                    return true;
                default:
                    D3D12MA_ASSERT(0);
                case CounterStatus::Pass:
                    break;
                }

                // Check all previous blocks for free space
                if (AllocInOtherBlock(0, i, moveData, vector))
                    return true;
            }
        }
        return false;
    }

    bool DefragmentationContextPimpl::ComputeDefragmentation_Balanced(BlockVector& vector, size_t index, bool update)
    {
        // Go over every allocation and try to fit it in previous blocks at lowest offsets,
        // if not possible: realloc within single block to minimize offset (exclude offset == 0),
        // but only if there are noticable gaps between them (some heuristic, ex. average size of allocation in block)
        D3D12MA_ASSERT(m_AlgorithmState != NULL);

        StateBalanced& vectorState = reinterpret_cast<StateBalanced*>(m_AlgorithmState)[index];
        if (update && vectorState.avgAllocSize == UINT64_MAX)
            UpdateVectorStatistics(vector, vectorState);

        const size_t startMoveCount = m_Moves.size();
        UINT64 minimalFreeRegion = vectorState.avgFreeSize / 2;
        for (size_t i = vector.GetBlockCount() - 1; i > m_ImmovableBlockCount; --i)
        {
            NormalBlock* block = vector.GetBlock(i);
            BlockMetadata* metadata = block->m_pMetadata;
            UINT64 prevFreeRegionSize = 0;

            for (AllocHandle handle = metadata->GetAllocationListBegin();
                handle != (AllocHandle)0;
                handle = metadata->GetNextAllocation(handle))
            {
                MoveAllocationData moveData = GetMoveData(handle, metadata);
                // Ignore newly created allocations by defragmentation algorithm
                if (moveData.move.pSrcAllocation->GetPrivateData() == this)
                    continue;
                switch (CheckCounters(moveData.move.pSrcAllocation->GetSize()))
                {
                case CounterStatus::Ignore:
                    continue;
                case CounterStatus::End:
                    return true;
                default:
                    D3D12MA_ASSERT(0);
                case CounterStatus::Pass:
                    break;
                }

                // Check all previous blocks for free space
                const size_t prevMoveCount = m_Moves.size();
                if (AllocInOtherBlock(0, i, moveData, vector))
                    return true;

                UINT64 nextFreeRegionSize = metadata->GetNextFreeRegionSize(handle);
                // If no room found then realloc within block for lower offset
                UINT64 offset = moveData.move.pSrcAllocation->GetOffset();
                if (prevMoveCount == m_Moves.size() && offset != 0 && metadata->GetSumFreeSize() >= moveData.size)
                {
                    // Check if realloc will make sense
                    if (prevFreeRegionSize >= minimalFreeRegion ||
                        nextFreeRegionSize >= minimalFreeRegion ||
                        moveData.size <= vectorState.avgFreeSize ||
                        moveData.size <= vectorState.avgAllocSize)
                    {
                        AllocationRequest request = {};
                        if (metadata->CreateAllocationRequest(
                            moveData.size,
                            moveData.alignment,
                            false,
                            AllocationFlagStrategyMinOffset,
                            &request))
                        {
                            if (metadata->GetAllocationOffset(request.allocHandle) < offset)
                            {
                                if (IsOk(vector.CommitAllocationRequest(
                                    request,
                                    block,
                                    moveData.size,
                                    moveData.alignment,
                                    this,
                                    &moveData.move.pDstTmpAllocation)))
                                {
                                    m_Moves.push_back(moveData.move);
                                    if (IncrementCounters(moveData.size))
                                        return true;
                                }
                            }
                        }
                    }
                }
                prevFreeRegionSize = nextFreeRegionSize;
            }
        }

        // No moves perfomed, update statistics to current vector state
        if (startMoveCount == m_Moves.size() && !update)
        {
            vectorState.avgAllocSize = UINT64_MAX;
            return ComputeDefragmentation_Balanced(vector, index, false);
        }
        return false;
    }

    bool DefragmentationContextPimpl::ComputeDefragmentation_Full(BlockVector& vector)
    {
        // Go over every allocation and try to fit it in previous blocks at lowest offsets,
        // if not possible: realloc within single block to minimize offset (exclude offset == 0)

        for (size_t i = vector.GetBlockCount() - 1; i > m_ImmovableBlockCount; --i)
        {
            NormalBlock* block = vector.GetBlock(i);
            BlockMetadata* metadata = block->m_pMetadata;

            for (AllocHandle handle = metadata->GetAllocationListBegin();
                handle != (AllocHandle)0;
                handle = metadata->GetNextAllocation(handle))
            {
                MoveAllocationData moveData = GetMoveData(handle, metadata);
                // Ignore newly created allocations by defragmentation algorithm
                if (moveData.move.pSrcAllocation->GetPrivateData() == this)
                    continue;
                switch (CheckCounters(moveData.move.pSrcAllocation->GetSize()))
                {
                case CounterStatus::Ignore:
                    continue;
                case CounterStatus::End:
                    return true;
                default:
                    D3D12MA_ASSERT(0);
                case CounterStatus::Pass:
                    break;
                }

                // Check all previous blocks for free space
                const size_t prevMoveCount = m_Moves.size();
                if (AllocInOtherBlock(0, i, moveData, vector))
                    return true;

                // If no room found then realloc within block for lower offset
                UINT64 offset = moveData.move.pSrcAllocation->GetOffset();
                if (prevMoveCount == m_Moves.size() && offset != 0 && metadata->GetSumFreeSize() >= moveData.size)
                {
                    AllocationRequest request = {};
                    if (metadata->CreateAllocationRequest(
                        moveData.size,
                        moveData.alignment,
                        false,
                        AllocationFlagStrategyMinOffset,
                        &request))
                    {
                        if (metadata->GetAllocationOffset(request.allocHandle) < offset)
                        {
                            if (IsOk(vector.CommitAllocationRequest(
                                request,
                                block,
                                moveData.size,
                                moveData.alignment,
                                this,
                                &moveData.move.pDstTmpAllocation)))
                            {
                                m_Moves.push_back(moveData.move);
                                if (IncrementCounters(moveData.size))
                                    return true;
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    void DefragmentationContextPimpl::UpdateVectorStatistics(BlockVector& vector, StateBalanced& state)
    {
        size_t allocCount = 0;
        size_t freeCount = 0;
        state.avgFreeSize = 0;
        state.avgAllocSize = 0;

        for (size_t i = 0; i < vector.GetBlockCount(); ++i)
        {
            BlockMetadata* metadata = vector.GetBlock(i)->m_pMetadata;

            allocCount += metadata->GetAllocationCount();
            freeCount += metadata->GetFreeRegionsCount();
            state.avgFreeSize += metadata->GetSumFreeSize();
            state.avgAllocSize += metadata->GetSize();
        }

        state.avgAllocSize = (state.avgAllocSize - state.avgFreeSize) / allocCount;
        state.avgFreeSize /= freeCount;
    }
#endif // _D3D12MA_DEFRAGMENTATION_CONTEXT_PIMPL_FUNCTIONS

#ifndef _D3D12MA_POOL_PIMPL_FUNCTIONS
    PoolPimpl::PoolPimpl(AllocatorPimpl* allocator, const PoolDesc& desc)
        : m_Allocator(allocator),
        m_Desc(desc),
        m_BlockVector(nullptr),
        m_Name(nullptr)
    {
        const bool explicitBlockSize = desc.blockSize != 0;
        const UINT64 preferredBlockSize = explicitBlockSize ? desc.blockSize : D3D12MA_DEFAULT_BLOCK_SIZE;
        UINT maxBlockCount = desc.maxBlockCount != 0 ? desc.maxBlockCount : UINT_MAX;

        //D3D12MA_ASSERT(m_Desc.protectedSession == nullptr);

        const UINT64 minAlignment = desc.minAllocationAlignment > 0 ? desc.minAllocationAlignment : D3D12MA_DEFAULT_ALIGNMENT;

        m_BlockVector = D3D12MA_NEW(allocator->GetAllocs(), BlockVector)(
            allocator, { desc.heapType }, desc.heapFlags,
            preferredBlockSize,
            desc.minBlockCount, maxBlockCount,
            explicitBlockSize,
            minAlignment,
            (desc.flags & PoolFlagsAlgorithmMask) != 0,
            (desc.flags & PoolFlagsMsaaTexturesAlwaysCommitted) != 0,
            //desc.pProtectedSession,
            desc.residencyPriority);
    }

    PoolPimpl::~PoolPimpl()
    {
        D3D12MA_ASSERT(m_PrevPool == NULL && m_NextPool == NULL);
        FreeName();
        D3D12MA_DELETE(m_Allocator->GetAllocs(), m_BlockVector);
    }

    Result PoolPimpl::Init()
    {
        m_CommittedAllocations.Init(m_Allocator->UseMutex(), m_Desc.heapType, this);
        return m_BlockVector->CreateMinBlocks();
    }

    void PoolPimpl::GetStatistics(Statistics& outStats)
    {
        ClearStatistics(outStats);
        m_BlockVector->AddStatistics(outStats);
        m_CommittedAllocations.AddStatistics(outStats);
    }

    void PoolPimpl::CalculateStatistics(DetailedStatistics& outStats)
    {
        ClearDetailedStatistics(outStats);
        AddDetailedStatistics(outStats);
    }

    void PoolPimpl::AddDetailedStatistics(DetailedStatistics& inoutStats)
    {
        m_BlockVector->AddDetailedStatistics(inoutStats);
        m_CommittedAllocations.AddDetailedStatistics(inoutStats);
    }

    void PoolPimpl::SetName(const char* Name)
    {
        FreeName();

        if (Name)
        {
            const size_t nameCharCount = strlen(Name) + 1;
            m_Name = D3D12MA_NEW_ARRAY(m_Allocator->GetAllocs(), char, nameCharCount);
            memcpy(m_Name, Name, nameCharCount * sizeof(char));
        }
    }

    void PoolPimpl::FreeName()
    {
        if (m_Name)
        {
            const size_t nameCharCount = strlen(m_Name) + 1;
            D3D12MA_DELETE_ARRAY(m_Allocator->GetAllocs(), m_Name, nameCharCount);
            m_Name = NULL;
        }
    }
#endif // _D3D12MA_POOL_PIMPL_FUNCTIONS


#ifndef _D3D12MA_PUBLIC_INTERFACE
    Result CreateAllocator(const AllocatorDesc* pDesc, Allocator** ppAllocator) noexcept
    {
        if (!pDesc || !ppAllocator || !pDesc->device.IsValid() ||
            !(pDesc->preferredBlockSize == 0 || (pDesc->preferredBlockSize >= 16 && pDesc->preferredBlockSize < 0x10000000000ull)))
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to CreateAllocator.");
            return Result::InvalidArgument;
        }

        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

            AllocationCallbacks allocationCallbacks;
        SetupAllocationCallbacks(allocationCallbacks, pDesc->allocationCallbacks);

        *ppAllocator = D3D12MA_NEW(allocationCallbacks, Allocator)(allocationCallbacks, *pDesc);
        Result hr = (*ppAllocator)->m_Pimpl->Init(*pDesc);
        if (Failed(hr))
        {
            D3D12MA_DELETE(allocationCallbacks, *ppAllocator);
            *ppAllocator = nullptr;
        }
        return hr;
    }

    HRESULT CreateVirtualBlock(const VirtualBlockDesc* pDesc, VirtualBlock** ppVirtualBlock)
    {
        if (!pDesc || !ppVirtualBlock)
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to CreateVirtualBlock.");
            return E_INVALIDARG;
        }

        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

            AllocationCallbacks allocationCallbacks;
        SetupAllocationCallbacks(allocationCallbacks, pDesc->allocationCallbacks);

        *ppVirtualBlock = D3D12MA_NEW(allocationCallbacks, VirtualBlock)(allocationCallbacks, *pDesc);
        return S_OK;
    }

#ifndef _D3D12MA_ALLOCATION_FUNCTIONS
    void Allocation::PackedData::SetType(Type type)
    {
        const UINT u = (UINT)type;
        D3D12MA_ASSERT(u < (1u << 2));
        m_Type = u;
    }

    void Allocation::PackedData::SetResourceDimension(ResourceType resourceDimension)
    {
        const UINT u = static_cast<UINT>(resourceDimension);
        //D3D12MA_ASSERT(u < (1u << 3));
        m_ResourceDimension = u;
    }

    void Allocation::PackedData::SetResourceFlags(ResourceFlags resourceFlags)
    {
        const UINT u = static_cast<UINT>(resourceFlags);
        D3D12MA_ASSERT(u < (1u << 24));
        m_ResourceFlags = u;
    }

    void Allocation::PackedData::SetTextureLayout(ResourceLayout textureLayout)
    {
        const UINT u = static_cast<UINT>(textureLayout);
        D3D12MA_ASSERT(u < (1u << 9));
        m_TextureLayout = u;
    }

    uint64_t Allocation::GetOffset() const
    {
        switch (m_PackedData.GetType())
        {
        case TYPE_COMMITTED:
        case TYPE_HEAP:
            return 0;
        case TYPE_PLACED:
            return m_Placed.block->m_pMetadata->GetAllocationOffset(m_Placed.allocHandle);
        default:
            D3D12MA_ASSERT(0);
            return 0;
        }
    }

    void Allocation::SetResource(ResourcePtr pResource)
    {
		m_resource = std::move(pResource);
    }

    HeapHandle Allocation::GetHeap() const noexcept
    {
        switch (m_PackedData.GetType())
        {
        case TYPE_COMMITTED:
            return {};
        case TYPE_PLACED:
            return m_Placed.block->GetHeap();
        case TYPE_HEAP:
            return m_Heap.heap->GetHandle();
        default:
            D3D12MA_ASSERT(0);
            return {};
        }
    }

    void Allocation::SetName(const char* Name) noexcept
    {
        FreeName();

        if (Name)
        {
            const size_t nameCharCount = strlen(Name) + 1;
            m_name = D3D12MA_NEW_ARRAY(m_allocator->GetAllocs(), char, nameCharCount);
            memcpy((void*)m_name, Name, nameCharCount * sizeof(WCHAR));
        }
    }

    void Allocation::ReleaseThis()
    {
        //SAFE_RELEASE(m_Resource);

        switch (m_PackedData.GetType())
        {
        case TYPE_COMMITTED:
            m_allocator->FreeCommittedMemory(this);
            break;
        case TYPE_PLACED:
            m_allocator->FreePlacedMemory(this);
            break;
        case TYPE_HEAP:
            m_allocator->FreeHeapMemory(this);
            break;
        }

        FreeName();

        m_allocator->GetAllocationObjectAllocator().Free(this);
    }

    Allocation::Allocation(AllocatorPimpl* allocator, UINT64 size, UINT64 alignment)
        : m_allocator{ allocator },
        m_size{ size },
        m_alignment{ alignment },
        m_resource{ },
        //m_pPrivateData{ NULL },
        m_name{ NULL }
    {
        D3D12MA_ASSERT(allocator);

        m_PackedData.SetType(TYPE_COUNT);
        m_PackedData.SetResourceDimension(ResourceType::Unknown);
        m_PackedData.SetResourceFlags(ResourceFlags::RF_None);
        m_PackedData.SetTextureLayout(ResourceLayout::Undefined);
    }

    void Allocation::InitCommitted(CommittedAllocationList* list)
    {
        m_PackedData.SetType(TYPE_COMMITTED);
        m_Committed.list = list;
        m_Committed.prev = nullptr;
        m_Committed.next = nullptr;
    }

    void Allocation::InitPlaced(AllocHandle allocHandle, NormalBlock* block)
    {
        m_PackedData.SetType(TYPE_PLACED);
        m_Placed.allocHandle = allocHandle;
        m_Placed.block = block;
    }

    void Allocation::InitHeap(CommittedAllocationList* list, HeapPtr heap)
    {
        m_PackedData.SetType(TYPE_HEAP);
        m_Heap.list = list;
        m_Committed.prev = nullptr;
        m_Committed.next = nullptr;
        m_Heap.heap = std::move(heap);
    }

    void Allocation::SwapBlockAllocation(Allocation* allocation)
    {
        D3D12MA_ASSERT(allocation != nullptr);
        D3D12MA_ASSERT(m_PackedData.GetType() == TYPE_PLACED);
        D3D12MA_ASSERT(allocation->m_PackedData.GetType() == TYPE_PLACED);

        MOVE_SWAP(m_resource, allocation->m_resource);
        m_Placed.block->m_pMetadata->SetAllocationPrivateData(m_Placed.allocHandle, allocation);
        MOVE_SWAP(m_Placed, allocation->m_Placed);
        m_Placed.block->m_pMetadata->SetAllocationPrivateData(m_Placed.allocHandle, this);
    }

    AllocHandle Allocation::GetAllocHandle() const noexcept
    {
        switch (m_PackedData.GetType())
        {
        case TYPE_COMMITTED:
        case TYPE_HEAP:
            return (AllocHandle)0;
        case TYPE_PLACED:
            return m_Placed.allocHandle;
        default:
            D3D12MA_ASSERT(0);
            return (AllocHandle)0;
        }
    }

    NormalBlock* Allocation::GetBlock()
    {
        switch (m_PackedData.GetType())
        {
        case TYPE_COMMITTED:
        case TYPE_HEAP:
            return NULL;
        case TYPE_PLACED:
            return m_Placed.block;
        default:
            D3D12MA_ASSERT(0);
            return NULL;
        }
    }

    void Allocation::SetResourcePointer(ResourcePtr resource, const ResourceDesc* pResourceDesc)
    {
        D3D12MA_ASSERT(!m_resource->IsValid() && pResourceDesc);
        m_resource = std::move(resource);
        m_PackedData.SetResourceDimension(pResourceDesc->type);
        m_PackedData.SetResourceFlags(pResourceDesc->resourceFlags);
        m_PackedData.SetTextureLayout(pResourceDesc->texture.initialLayout);
    }

    void Allocation::FreeName()
    {
        if (m_name)
        {
            const size_t nameCharCount = strlen(m_name) + 1;
            D3D12MA_DELETE_ARRAY(m_allocator->GetAllocs(), m_name, nameCharCount);
            m_name = nullptr;
        }
    }
#endif // _D3D12MA_ALLOCATION_FUNCTIONS

#ifndef _D3D12MA_DEFRAGMENTATION_CONTEXT_FUNCTIONS
    Result DefragmentationContext::BeginPass(DefragmentationPassMoveInfo* pPassInfo) noexcept
    {
        D3D12MA_ASSERT(pPassInfo);
        return m_Pimpl->DefragmentPassBegin(*pPassInfo);
    }

    Result DefragmentationContext::EndPass(DefragmentationPassMoveInfo* pPassInfo) noexcept
    {
        D3D12MA_ASSERT(pPassInfo);
        return m_Pimpl->DefragmentPassEnd(*pPassInfo);
    }

    void DefragmentationContext::GetStats(DefragmentationStats* pStats) noexcept
    {
        D3D12MA_ASSERT(pStats);
        m_Pimpl->GetStats(*pStats);
    }

    void DefragmentationContext::ReleaseThis()
    {
        D3D12MA_DELETE(m_Pimpl->GetAllocs(), this);
    }

    DefragmentationContext::DefragmentationContext(AllocatorPimpl* allocator,
        const DefragmentationDesc& desc,
        BlockVector* poolVector)
        : m_Pimpl(D3D12MA_NEW(allocator->GetAllocs(), DefragmentationContextPimpl)(allocator, desc, poolVector)) {
    }

    DefragmentationContext::~DefragmentationContext()
    {
        D3D12MA_DELETE(m_Pimpl->GetAllocs(), m_Pimpl);
    }
#endif // _D3D12MA_DEFRAGMENTATION_CONTEXT_FUNCTIONS

#ifndef _D3D12MA_POOL_FUNCTIONS
    PoolDesc Pool::GetDesc() const noexcept
    {
        return m_Pimpl->GetDesc();
    }

    void Pool::GetStatistics(Statistics* pStats) noexcept
    {
        D3D12MA_ASSERT(pStats);
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->GetStatistics(*pStats);
    }

    void Pool::CalculateStatistics(DetailedStatistics* pStats) noexcept
    {
        D3D12MA_ASSERT(pStats);
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->CalculateStatistics(*pStats);
    }

    void Pool::SetName(const char* Name) noexcept
    {
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->SetName(Name);
    }

    const char* Pool::GetName() const noexcept
    {
        return m_Pimpl->GetName();
    }

    Result Pool::BeginDefragmentation(const DefragmentationDesc* pDesc, DefragmentationContext** ppContext) noexcept
    {
        D3D12MA_ASSERT(pDesc && ppContext);

        // Check for support
        if (m_Pimpl->GetBlockVector()->GetAlgorithm() & PoolFlagsAlgorithmLinear)
            return Result::NoInterface;
        if (m_Pimpl->AlwaysCommitted())
            return Result::NoInterface;

        AllocatorPimpl* allocator = m_Pimpl->GetAllocator();
        *ppContext = D3D12MA_NEW(allocator->GetAllocs(), DefragmentationContext)(allocator, *pDesc, m_Pimpl->GetBlockVector());
        return Result::Ok;
    }

    void Pool::ReleaseThis()
    {
        D3D12MA_DELETE(m_Pimpl->GetAllocator()->GetAllocs(), this);
    }

    Pool::Pool(Allocator* allocator, const PoolDesc& desc)
        : m_Pimpl(D3D12MA_NEW(allocator->m_Pimpl->GetAllocs(), PoolPimpl)(allocator->m_Pimpl, desc)) {
    }

    Pool::~Pool()
    {
        m_Pimpl->GetAllocator()->UnregisterPool(this, m_Pimpl->GetDesc().heapType);

        D3D12MA_DELETE(m_Pimpl->GetAllocator()->GetAllocs(), m_Pimpl);
    }
#endif // _D3D12MA_POOL_FUNCTIONS

#ifndef _D3D12MA_ALLOCATOR_FUNCTIONS
    //const  D3D12_FEATURE_DATA_D3D12_OPTIONS& Allocator::GetD3D12Options() const
    //{
    //    return m_Pimpl->GetD3D12Options();
    //}

    bool Allocator::IsUMA() const
    {
        return m_Pimpl->IsUMA();
    }

    bool Allocator::IsCacheCoherentUMA() const
    {
        return m_Pimpl->IsCacheCoherentUMA();
    }

    bool Allocator::IsGPUUploadHeapSupported() const
    {
        return m_Pimpl->IsGPUUploadHeapSupported();
    }

    bool Allocator::IsTightAlignmentSupported() const
    {
        return m_Pimpl->IsTightAlignmentSupported();
    }

    UINT64 Allocator::GetMemoryCapacity(MemorySegmentGroup memorySegmentGroup) const
    {
        return m_Pimpl->GetMemoryCapacity(memorySegmentGroup);
    }


    Result Allocator::CreateResource(
        const AllocationDesc* pAllocDesc,
        const ResourceDesc* pResourceDesc,
        ResourceLayout InitialLayout,
        const ClearValue* pOptimizedClearValue,
        UINT32 NumCastableFormats,
        const Format* pCastableFormats,
        AllocationPtr& outAllocation) noexcept
    {
        if (!pAllocDesc || !pResourceDesc)
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::CreateResource3.");
            return Result::InvalidArgument;
        }
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            ResourcePtr ptr;
            return m_Pimpl->CreateResource(
                pAllocDesc,
                CreateResourceParams(pResourceDesc, InitialLayout, pOptimizedClearValue, NumCastableFormats, pCastableFormats),
                outAllocation.Put(),
                std::move(ptr));
    }

    Result Allocator::AllocateMemory(
        const AllocationDesc& pAllocDesc,
        const ResourceAllocationInfo& pAllocInfo,
        Allocation* ppAllocation) noexcept
    {
        if (!ValidateAllocateMemoryParameters(&pAllocDesc, &pAllocInfo, &ppAllocation))
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::AllocateMemory.");
            return Result::InvalidArgument;
        }
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            return m_Pimpl->AllocateMemory(&pAllocDesc, &pAllocInfo, &ppAllocation);
    }

    Result Allocator::CreateAliasingResource(
        Allocation* pAllocation,
        UINT64 AllocationLocalOffset,
        const ResourceDesc* pResourceDesc,
        ResourceLayout InitialLayout,
        const ClearValue* pOptimizedClearValue,
        UINT32 NumCastableFormats,
        const Format* pCastableFormats,
        ResourcePtr& out)
    {
        if (!pAllocation || !pResourceDesc)
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::CreateAliasingResource.");
            return Result::InvalidArgument;
        }
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            return m_Pimpl->CreateAliasingResource(
                pAllocation,
                AllocationLocalOffset,
                CreateResourceParams(pResourceDesc, InitialLayout, pOptimizedClearValue, NumCastableFormats, pCastableFormats),
                out);
    }

    Result Allocator::CreatePool(
        const PoolDesc* pPoolDesc,
        Pool** ppPool) noexcept
    {
        if (!pPoolDesc || !ppPool ||
            (pPoolDesc->maxBlockCount > 0 && pPoolDesc->maxBlockCount < pPoolDesc->minBlockCount) ||
            (pPoolDesc->minAllocationAlignment > 0 && !IsPow2(pPoolDesc->minAllocationAlignment)))
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::CreatePool.");
            return Result::InvalidArgument;
        }
        if ((pPoolDesc->flags & PoolFlagsAlwaysCommitted) != 0 &&
            (pPoolDesc->blockSize != 0 || pPoolDesc->minBlockCount > 0))
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to Allocator::CreatePool while POOL_FLAG_ALWAYS_COMMITTED is specified.");
            return Result::InvalidArgument;
        }
        if (!m_Pimpl->HeapFlagsFulfillResourceHeapTier(pPoolDesc->heapFlags))
        {
            D3D12MA_ASSERT(0 && "Invalid pPoolDesc->HeapFlags passed to Allocator::CreatePool. Did you forget to handle ResourceHeapTier=1?");
            return Result::InvalidArgument;
        }
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            * ppPool = D3D12MA_NEW(m_Pimpl->GetAllocs(), Pool)(this, *pPoolDesc);
        Result hr = (*ppPool)->m_Pimpl->Init();
        if (SUCCEEDED(hr))
        {
            m_Pimpl->RegisterPool(*ppPool, pPoolDesc->heapType);
        }
        else
        {
            D3D12MA_DELETE(m_Pimpl->GetAllocs(), *ppPool);
            *ppPool = NULL;
        }
        return hr;
    }

    void Allocator::SetCurrentFrameIndex(UINT frameIndex) noexcept
    {
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->SetCurrentFrameIndex(frameIndex);
    }

    void Allocator::GetBudget(Budget* pLocalBudget, Budget* pNonLocalBudget) noexcept
    {
        if (pLocalBudget == nullptr && pNonLocalBudget == nullptr)
        {
            return;
        }
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->GetBudget(pLocalBudget, pNonLocalBudget);
    }

    void Allocator::CalculateStatistics(TotalStatistics* pStats) noexcept
    {
        D3D12MA_ASSERT(pStats);
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->CalculateStatistics(*pStats);
    }

    void Allocator::BuildStatsString(char** ppStatsString, bool detailedMap) const noexcept
    {
        D3D12MA_ASSERT(ppStatsString);
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->BuildStatsString(ppStatsString, detailedMap);
    }

    void Allocator::FreeStatsString(char* pStatsString) const noexcept
    {
        if (pStatsString != NULL)
        {
            D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
                m_Pimpl->FreeStatsString(pStatsString);
        }
    }

    void Allocator::BeginDefragmentation(const DefragmentationDesc* pDesc, DefragmentationContext** ppContext) noexcept
    {
        D3D12MA_ASSERT(pDesc && ppContext);

        *ppContext = D3D12MA_NEW(m_Pimpl->GetAllocs(), DefragmentationContext)(m_Pimpl, *pDesc, NULL);
    }

    void Allocator::ReleaseThis()
    {
        // Copy is needed because otherwise we would call destructor and invalidate the structure with callbacks before using it to free memory.
        const AllocationCallbacks allocationCallbacksCopy = m_Pimpl->GetAllocs();
        D3D12MA_DELETE(allocationCallbacksCopy, this);
    }

    Allocator::Allocator(const AllocationCallbacks& allocationCallbacks, const AllocatorDesc& desc)
        : m_Pimpl(D3D12MA_NEW(allocationCallbacks, AllocatorPimpl)(allocationCallbacks, desc)) {
    }

    Allocator::~Allocator()
    {
        D3D12MA_DELETE(m_Pimpl->GetAllocs(), m_Pimpl);
    }
#endif // _D3D12MA_ALLOCATOR_FUNCTIONS

#ifndef _D3D12MA_VIRTUAL_BLOCK_FUNCTIONS
    BOOL VirtualBlock::IsEmpty() const
    {
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            return m_Pimpl->m_Metadata->IsEmpty() ? TRUE : FALSE;
    }

    void VirtualBlock::GetAllocationInfo(VirtualAllocation allocation, VirtualAllocationInfo* pInfo) const noexcept
    {
        D3D12MA_ASSERT(allocation.handle != (AllocHandle)0 && pInfo);

        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->m_Metadata->GetAllocationInfo(allocation.handle, *pInfo);
    }

    Result VirtualBlock::Allocate(const VirtualAllocationDesc* pDesc, VirtualAllocation* pAllocation, UINT64* pOffset) noexcept
    {
        if (!pDesc || !pAllocation || pDesc->size == 0 || !IsPow2(pDesc->alignment))
        {
            D3D12MA_ASSERT(0 && "Invalid arguments passed to VirtualBlock::Allocate.");
            return Result::InvalidArgument;
        }

        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

            const UINT64 alignment = pDesc->alignment != 0 ? pDesc->alignment : 1;
        AllocationRequest allocRequest = {};
        if (m_Pimpl->m_Metadata->CreateAllocationRequest(
            pDesc->size,
            alignment,
            pDesc->flags & VirtualAllocationFlagsUpperAddress,
            pDesc->flags & VirtualAllocationFlagsStrategyMask,
            &allocRequest))
        {
            m_Pimpl->m_Metadata->Alloc(allocRequest, pDesc->size, pDesc->privateData);
            D3D12MA_HEAVY_ASSERT(m_Pimpl->m_Metadata->Validate());
            pAllocation->handle = allocRequest.allocHandle;

            if (pOffset)
                *pOffset = m_Pimpl->m_Metadata->GetAllocationOffset(allocRequest.allocHandle);
            return Result::Ok;
        }

        pAllocation->handle = (AllocHandle)0;
        if (pOffset)
            *pOffset = UINT64_MAX;

        return Result::OutOfMemory;
    }

    void VirtualBlock::FreeAllocation(VirtualAllocation allocation) noexcept
    {
        if (allocation.handle == (AllocHandle)0)
            return;

        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

            m_Pimpl->m_Metadata->Free(allocation.handle);
        D3D12MA_HEAVY_ASSERT(m_Pimpl->m_Metadata->Validate());
    }

    void VirtualBlock::Clear() noexcept
    {
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

            m_Pimpl->m_Metadata->Clear();
        D3D12MA_HEAVY_ASSERT(m_Pimpl->m_Metadata->Validate());
    }

    void VirtualBlock::SetAllocationPrivateData(VirtualAllocation allocation, void* pPrivateData) noexcept
    {
        D3D12MA_ASSERT(allocation.handle != (AllocHandle)0);

        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            m_Pimpl->m_Metadata->SetAllocationPrivateData(allocation.handle, pPrivateData);
    }

    void VirtualBlock::GetStatistics(Statistics* pStats) const noexcept
    {
        D3D12MA_ASSERT(pStats);
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            D3D12MA_HEAVY_ASSERT(m_Pimpl->m_Metadata->Validate());
        ClearStatistics(*pStats);
        m_Pimpl->m_Metadata->AddStatistics(*pStats);
    }

    void VirtualBlock::CalculateStatistics(DetailedStatistics* pStats) const noexcept
    {
        D3D12MA_ASSERT(pStats);
        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
            D3D12MA_HEAVY_ASSERT(m_Pimpl->m_Metadata->Validate());
        ClearDetailedStatistics(*pStats);
        m_Pimpl->m_Metadata->AddDetailedStatistics(*pStats);
    }

    void VirtualBlock::BuildStatsString(char** ppStatsString) const noexcept
    {
        D3D12MA_ASSERT(ppStatsString);

        D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK

            StringBuilder sb(m_Pimpl->m_AllocationCallbacks);
        {
            JsonWriter json(m_Pimpl->m_AllocationCallbacks, sb);
            D3D12MA_HEAVY_ASSERT(m_Pimpl->m_Metadata->Validate());
            json.BeginObject();
            m_Pimpl->m_Metadata->WriteAllocationInfoToJson(json);
            json.EndObject();
        } // Scope for JsonWriter

        const size_t length = sb.GetLength();
        char* result = AllocateArray<char>(m_Pimpl->m_AllocationCallbacks, length + 1);
        memcpy(result, sb.GetData(), length * sizeof(char));
        result[length] = L'\0';
        *ppStatsString = result;
    }

    void VirtualBlock::FreeStatsString(char* pStatsString) const noexcept
    {
        if (pStatsString != nullptr)
        {
            D3D12MA_DEBUG_GLOBAL_MUTEX_LOCK
                Free(m_Pimpl->m_AllocationCallbacks, pStatsString);
        }
    }

    void VirtualBlock::ReleaseThis()
    {
        // Copy is needed because otherwise we would call destructor and invalidate the structure with callbacks before using it to free memory.
        const AllocationCallbacks allocationCallbacksCopy = m_Pimpl->m_AllocationCallbacks;
        D3D12MA_DELETE(allocationCallbacksCopy, this);
    }

    VirtualBlock::VirtualBlock(const AllocationCallbacks& allocationCallbacks, const VirtualBlockDesc& desc)
        : m_Pimpl(D3D12MA_NEW(allocationCallbacks, VirtualBlockPimpl)(allocationCallbacks, desc)) {
    }

    VirtualBlock::~VirtualBlock()
    {
        // THIS IS AN IMPORTANT ASSERT!
        // Hitting it means you have some memory leak - unreleased allocations in this virtual block.
        D3D12MA_ASSERT(m_Pimpl->m_Metadata->IsEmpty() && "Some allocations were not freed before destruction of this virtual block!");

        D3D12MA_DELETE(m_Pimpl->m_AllocationCallbacks, m_Pimpl);
    }
#endif // _D3D12MA_VIRTUAL_BLOCK_FUNCTIONS
#endif // _D3D12MA_PUBLIC_INTERFACE
} // namespace rhi::ma