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

#include "rhi_allocator.h"

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
// Configuration Begin
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
#ifndef _D3D12MA_CONFIGURATION

#ifndef D3D12MA_SORT
#define D3D12MA_SORT(beg, end, cmp)  std::sort(beg, end, cmp)
#endif

#ifndef D3D12MA_ASSERT
#include <cassert>
#define D3D12MA_ASSERT(cond) assert(cond)
#endif

// Assert that will be called very often, like inside data structures e.g. operator[].
// Making it non-empty can make program slow.
#ifndef D3D12MA_HEAVY_ASSERT
#ifdef _DEBUG
#define D3D12MA_HEAVY_ASSERT(expr)   //D3D12MA_ASSERT(expr)
#else
#define D3D12MA_HEAVY_ASSERT(expr)
#endif
#endif

#ifndef D3D12MA_DEFAULT_ALIGNMENT
	/*
	Default alignment of allocations in default pools and custom pools with MinAllocationAlignment == 0.
	Can be lowered for custom pools by specifying custom MinAllocationAlignment > 0.
	*/
#define D3D12MA_DEFAULT_ALIGNMENT 256 // D3D12 default, larger than VK minimum
#endif

#ifndef D3D12MA_DEBUG_ALIGNMENT
	/*
	Minimum alignment of all allocations, in bytes.
	Set to more than 1 for debugging purposes only. Must be power of two.
	*/
#define D3D12MA_DEBUG_ALIGNMENT (1)
#endif

#ifndef D3D12MA_DEBUG_MARGIN
	// Minimum margin before and after every allocation, in bytes.
	// Set nonzero for debugging purposes only.
#define D3D12MA_DEBUG_MARGIN (0)
#endif

#ifndef D3D12MA_DEBUG_GLOBAL_MUTEX
	/*
	Set this to 1 for debugging purposes only, to enable single mutex protecting all
	entry calls to the library. Can be useful for debugging multithreading issues.
	*/
#define D3D12MA_DEBUG_GLOBAL_MUTEX (0)
#endif

	/*
	Define this macro for debugging purposes only to force specific D3D12_RESOURCE_HEAP_TIER,
	especially to test compatibility with D3D12_RESOURCE_HEAP_TIER_1 on modern GPUs.
	*/
	//#define D3D12MA_FORCE_RESOURCE_HEAP_TIER D3D12_RESOURCE_HEAP_TIER_1

#ifndef D3D12MA_DEFAULT_BLOCK_SIZE
   /// Default size of a block allocated as single ID3D12Heap.
#define D3D12MA_DEFAULT_BLOCK_SIZE (64ull * 1024 * 1024)
#endif

#ifndef D3D12MA_TIGHT_ALIGNMENT_SUPPORTED
#if D3D12_SDK_VERSION >= 618
#define D3D12MA_TIGHT_ALIGNMENT_SUPPORTED 1
#else
#define D3D12MA_TIGHT_ALIGNMENT_SUPPORTED 0
#endif
#endif

#ifndef D3D12MA_OPTIONS16_SUPPORTED
#if D3D12_SDK_VERSION >= 610
#define D3D12MA_OPTIONS16_SUPPORTED 1
#else
#define D3D12MA_OPTIONS16_SUPPORTED 0
#endif
#endif

#ifndef D3D12MA_DEBUG_LOG
#define D3D12MA_DEBUG_LOG(format, ...)
/*
#define D3D12MA_DEBUG_LOG(format, ...) do { \
	wprintf(format, __VA_ARGS__); \
	wprintf(L"\n"); \
} while(false)
*/
#endif

#endif // _D3D12MA_CONFIGURATION

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Configuration End
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

namespace rhi::ma {
		static constexpr UINT HEAP_TYPE_COUNT = 5;
		static constexpr UINT STANDARD_HEAP_TYPE_COUNT = 4; // Only DEFAULT, UPLOAD, READBACK, GPU_UPLOAD.
		static constexpr UINT DEFAULT_POOL_MAX_COUNT = STANDARD_HEAP_TYPE_COUNT * 3;
		static const UINT NEW_BLOCK_SIZE_SHIFT_MAX = 3;
		// Minimum size of a free suballocation to register it in the free suballocation collection.
		static const UINT64 MIN_FREE_SUBALLOCATION_SIZE_TO_REGISTER = 16;

		static const WCHAR* const HeapTypeNames[] =
		{
			L"DEFAULT",
			L"UPLOAD",
			L"READBACK",
			L"CUSTOM",
			L"GPU_UPLOAD",
		};
		static const WCHAR* const StandardHeapTypeNames[] =
		{
			L"DEFAULT",
			L"UPLOAD",
			L"READBACK",
			L"GPU_UPLOAD",
		};

		static const HeapFlags RESOURCE_CLASS_HEAP_FLAGS =
			HeapFlags::DenyBuffers | HeapFlags::DenyRtDsTextures | HeapFlags::DenyNonRtDsTextures;

		static const ResidencyPriority D3D12_RESIDENCY_PRIORITY_NONE = ResidencyPriority::Normal;
		static const HeapType D3D12_HEAP_TYPE_GPU_UPLOAD_COPY = HeapType::HostVisibleDeviceLocal;
		static const ResourceFlags D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT_COPY = ResourceFlags::RF_UseTightAlignment;

#ifndef _D3D12MA_ENUM_DECLARATIONS

		// Local copy of this enum, as it is provided only by <dxgi1_4.h>, so it may not be available.
		enum DXGI_MEMORY_SEGMENT_GROUP_COPY
		{
			DXGI_MEMORY_SEGMENT_GROUP_LOCAL_COPY = 0,
			DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL_COPY = 1,
			DXGI_MEMORY_SEGMENT_GROUP_COUNT
		};

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
                Free(allocs, memory);
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
                (resDesc.flags & (ResourceFlags::RF_AllowRenderTarget | ResourceFlags::RF_AllowDepthStencil)) != 0;
            return isRenderTargetOrDepthStencil ? ResourceClass::RT_DS_Texture : ResourceClass::Non_RT_DS_Texture;
        }

        // This algorithm is overly conservative.
        static bool CanUseSmallAlignment(const ResourceDesc& resourceDesc)
        {
            if (resourceDesc.type != ResourceType::Texture2D)
                return false;
            if ((resourceDesc.flags & (ResourceFlags::RF_AllowRenderTarget | ResourceFlags::RF_AllowDepthStencil)) != 0)
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

#ifndef _D3D12MA_STRING_BUILDER
        class StringBuilder
        {
        public:
            StringBuilder(const AllocationCallbacks& allocationCallbacks) : m_Data(allocationCallbacks) {}

            size_t GetLength() const { return m_Data.size(); }
            LPCWSTR GetData() const { return m_Data.data(); }

            void Add(WCHAR ch) { m_Data.push_back(ch); }
            void Add(LPCWSTR str);
            void AddNewLine() { Add(L'\n'); }
            void AddNumber(UINT num);
            void AddNumber(UINT64 num);
            void AddPointer(const void* ptr);

        private:
            Vector<WCHAR> m_Data;
        };

#ifndef _D3D12MA_STRING_BUILDER_FUNCTIONS
        void StringBuilder::Add(LPCWSTR str)
        {
            const size_t len = wcslen(str);
            if (len > 0)
            {
                const size_t oldCount = m_Data.size();
                m_Data.resize(oldCount + len);
                memcpy(m_Data.data() + oldCount, str, len * sizeof(WCHAR));
            }
        }

        void StringBuilder::AddNumber(UINT num)
        {
            WCHAR buf[11];
            buf[10] = L'\0';
            WCHAR* p = &buf[10];
            do
            {
                *--p = L'0' + (num % 10);
                num /= 10;
            } while (num);
            Add(p);
        }

        void StringBuilder::AddNumber(UINT64 num)
        {
            WCHAR buf[21];
            buf[20] = L'\0';
            WCHAR* p = &buf[20];
            do
            {
                *--p = L'0' + (num % 10);
                num /= 10;
            } while (num);
            Add(p);
        }

        void StringBuilder::AddPointer(const void* ptr)
        {
            WCHAR buf[21];
            uintptr_t num = (uintptr_t)ptr;
            buf[20] = L'\0';
            WCHAR* p = &buf[20];
            do
            {
                *--p = HexDigitToChar((UINT8)(num & 0xF));
                num >>= 4;
            } while (num);
            Add(p);
        }

#endif // _D3D12MA_STRING_BUILDER_FUNCTIONS
#endif // _D3D12MA_STRING_BUILDER

        inline std::wstring s2ws(const std::string& s) {
            int buffSize = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
            std::wstring ws(buffSize, 0);
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), buffSize);
            return ws;
        }

#ifndef _D3D12MA_JSON_WRITER
        /*
        Allows to conveniently build a correct JSON document to be written to the
        StringBuilder passed to the constructor.
        */
        class JsonWriter
        {
        public:
            // stringBuilder - string builder to write the document to. Must remain alive for the whole lifetime of this object.
            JsonWriter(const AllocationCallbacks& allocationCallbacks, StringBuilder& stringBuilder);
            ~JsonWriter();

            // Begins object by writing "{".
            // Inside an object, you must call pairs of WriteString and a value, e.g.:
            // j.BeginObject(true); j.WriteString("A"); j.WriteNumber(1); j.WriteString("B"); j.WriteNumber(2); j.EndObject();
            // Will write: { "A": 1, "B": 2 }
            void BeginObject(bool singleLine = false);
            // Ends object by writing "}".
            void EndObject();

            // Begins array by writing "[".
            // Inside an array, you can write a sequence of any values.
            void BeginArray(bool singleLine = false);
            // Ends array by writing "[".
            void EndArray();

            // Writes a string value inside "".
            // pStr can contain any UTF-16 characters, including '"', new line etc. - they will be properly escaped.
            void WriteString(LPCWSTR pStr);

            // Begins writing a string value.
            // Call BeginString, ContinueString, ContinueString, ..., EndString instead of
            // WriteString to conveniently build the string content incrementally, made of
            // parts including numbers.
            void BeginString(LPCWSTR pStr = NULL);
            // Posts next part of an open string.
            void ContinueString(LPCWSTR pStr);
            // Posts next part of an open string. The number is converted to decimal characters.
            void ContinueString(UINT num);
            void ContinueString(UINT64 num);
            void ContinueString_Pointer(const void* ptr);
            // Posts next part of an open string. Pointer value is converted to characters
            // using "%p" formatting - shown as hexadecimal number, e.g.: 000000081276Ad00
            // void ContinueString_Pointer(const void* ptr);
            // Ends writing a string value by writing '"'.
            void EndString(LPCWSTR pStr = NULL);

            // Writes a number value.
            void WriteNumber(UINT num);
            void WriteNumber(UINT64 num);
            // Writes a boolean value - false or true.
            void WriteBool(bool b);
            // Writes a null value.
            void WriteNull();

            void AddAllocationToObject(const Allocation& alloc);
            void AddDetailedStatisticsInfoObject(const DetailedStatistics& stats);

        private:
            static const WCHAR* const INDENT;

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
        };

#ifndef _D3D12MA_JSON_WRITER_FUNCTIONS
        const WCHAR* const JsonWriter::INDENT = L"  ";

        JsonWriter::JsonWriter(const AllocationCallbacks& allocationCallbacks, StringBuilder& stringBuilder)
            : m_SB(stringBuilder),
            m_Stack(allocationCallbacks),
            m_InsideString(false) {
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
            m_SB.Add(L'{');

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
            m_SB.Add(L'}');

            m_Stack.pop_back();
        }

        void JsonWriter::BeginArray(bool singleLine)
        {
            D3D12MA_ASSERT(!m_InsideString);

            BeginValue(false);
            m_SB.Add(L'[');

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
            m_SB.Add(L']');

            m_Stack.pop_back();
        }

        void JsonWriter::WriteString(LPCWSTR pStr)
        {
            BeginString(pStr);
            EndString();
        }

        void JsonWriter::BeginString(LPCWSTR pStr)
        {
            D3D12MA_ASSERT(!m_InsideString);

            BeginValue(true);
            m_InsideString = true;
            m_SB.Add(L'"');
            if (pStr != NULL)
            {
                ContinueString(pStr);
            }
        }

        void JsonWriter::ContinueString(LPCWSTR pStr)
        {
            D3D12MA_ASSERT(m_InsideString);
            D3D12MA_ASSERT(pStr);

            for (const WCHAR* p = pStr; *p; ++p)
            {
                // the strings we encode are assumed to be in UTF-16LE format, the native
                // windows wide character Unicode format. In this encoding Unicode code
                // points U+0000 to U+D7FF and U+E000 to U+FFFF are encoded in two bytes,
                // and everything else takes more than two bytes. We will reject any
                // multi wchar character encodings for simplicity.
                UINT val = (UINT)*p;
                D3D12MA_ASSERT(((val <= 0xD7FF) || (0xE000 <= val && val <= 0xFFFF)) &&
                    "Character not currently supported.");
                switch (*p)
                {
                case L'"':  m_SB.Add(L'\\'); m_SB.Add(L'"');  break;
                case L'\\': m_SB.Add(L'\\'); m_SB.Add(L'\\'); break;
                case L'/':  m_SB.Add(L'\\'); m_SB.Add(L'/');  break;
                case L'\b': m_SB.Add(L'\\'); m_SB.Add(L'b');  break;
                case L'\f': m_SB.Add(L'\\'); m_SB.Add(L'f');  break;
                case L'\n': m_SB.Add(L'\\'); m_SB.Add(L'n');  break;
                case L'\r': m_SB.Add(L'\\'); m_SB.Add(L'r');  break;
                case L'\t': m_SB.Add(L'\\'); m_SB.Add(L't');  break;
                default:
                    // conservatively use encoding \uXXXX for any Unicode character
                    // requiring more than one byte.
                    if (32 <= val && val < 256)
                        m_SB.Add(*p);
                    else
                    {
                        m_SB.Add(L'\\');
                        m_SB.Add(L'u');
                        for (UINT i = 0; i < 4; ++i)
                        {
                            UINT hexDigit = (val & 0xF000) >> 12;
                            val <<= 4;
                            if (hexDigit < 10)
                                m_SB.Add(L'0' + (WCHAR)hexDigit);
                            else
                                m_SB.Add(L'A' + (WCHAR)hexDigit);
                        }
                    }
                    break;
                }
            }
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

        void JsonWriter::EndString(LPCWSTR pStr)
        {
            D3D12MA_ASSERT(m_InsideString);

            if (pStr)
                ContinueString(pStr);
            m_SB.Add(L'"');
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
            if (b)
                m_SB.Add(L"true");
            else
                m_SB.Add(L"false");
        }

        void JsonWriter::WriteNull()
        {
            D3D12MA_ASSERT(!m_InsideString);
            BeginValue(false);
            m_SB.Add(L"null");
        }

        void JsonWriter::AddAllocationToObject(const Allocation& alloc)
        {
            WriteString(L"Type");
            switch (alloc.m_PackedData.GetResourceDimension()) {
                case ResourceType::Unknown:
                WriteString(L"UNKNOWN");
                break;
            case ResourceType::Buffer:
                WriteString(L"BUFFER");
                break;
            case ResourceType::Texture1D:
                WriteString(L"TEXTURE1D");
                break;
            case ResourceType::Texture2D:
                WriteString(L"TEXTURE2D");
                break;
            case ResourceType::Texture3D:
                WriteString(L"TEXTURE3D");
                break;
            default: D3D12MA_ASSERT(0); break;
            }

            WriteString(L"Size");
            WriteNumber(alloc.GetSize());
            WriteString(L"Usage");
            WriteNumber((UINT)alloc.m_PackedData.GetResourceFlags());

            void* privateData = alloc.GetPrivateData();
            if (privateData)
            {
                WriteString(L"CustomData");
                BeginString();
                ContinueString_Pointer(privateData);
                EndString();
            }

            auto str = s2ws(alloc.GetName());
            LPCWSTR name = str.c_str();
            if (name != NULL)
            {
                WriteString(L"Name");
                WriteString(name);
            }
            //if (alloc.m_PackedData.GetTextureLayout() != 0)
            //{
                WriteString(L"Layout");
                WriteNumber((UINT)alloc.m_PackedData.GetTextureLayout());
            //}
        }

        void JsonWriter::AddDetailedStatisticsInfoObject(const DetailedStatistics& stats)
        {
            BeginObject();

            WriteString(L"BlockCount");
            WriteNumber(stats.stats.blockCount);
            WriteString(L"BlockBytes");
            WriteNumber(stats.stats.blockBytes);
            WriteString(L"AllocationCount");
            WriteNumber(stats.stats.allocationCount);
            WriteString(L"AllocationBytes");
            WriteNumber(stats.stats.allocationBytes);
            WriteString(L"UnusedRangeCount");
            WriteNumber(stats.unusedRangeCount);

            if (stats.stats.allocationCount > 1)
            {
                WriteString(L"AllocationSizeMin");
                WriteNumber(stats.allocationSizeMin);
                WriteString(L"AllocationSizeMax");
                WriteNumber(stats.allocationSizeMax);
            }
            if (stats.unusedRangeCount > 1)
            {
                WriteString(L"UnusedRangeSizeMin");
                WriteNumber(stats.unusedRangeSizeMin);
                WriteString(L"UnusedRangeSizeMax");
                WriteNumber(stats.unusedRangeSizeMax);
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
                    D3D12MA_ASSERT(isString);
                }

                if (currItem.type == COLLECTION_TYPE_OBJECT && currItem.valueCount % 2 == 1)
                {
                    m_SB.Add(L':'); m_SB.Add(L' ');
                }
                else if (currItem.valueCount > 0)
                {
                    m_SB.Add(L','); m_SB.Add(L' ');
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
                {
                    --count;
                }
                for (size_t i = 0; i < count; ++i)
                {
                    m_SB.Add(INDENT);
                }
            }
        }
#endif // _D3D12MA_JSON_WRITER_FUNCTIONS
#endif // _D3D12MA_JSON_WRITER

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
            json.WriteString(L"TotalBytes");
            json.WriteNumber(GetSize());

            json.WriteString(L"UnusedBytes");
            json.WriteNumber(unusedBytes);

            json.WriteString(L"Allocations");
            json.WriteNumber((UINT64)allocationCount);

            json.WriteString(L"UnusedRanges");
            json.WriteNumber((UINT64)unusedRangeCount);

            json.WriteString(L"Suballocations");
            json.BeginArray();
        }

        void BlockMetadata::PrintDetailedMap_Allocation(JsonWriter& json,
            UINT64 offset, UINT64 size, void* privateData) const
        {
            json.BeginObject(true);

            json.WriteString(L"Offset");
            json.WriteNumber(offset);

            if (IsVirtual())
            {
                json.WriteString(L"Size");
                json.WriteNumber(size);
                if (privateData)
                {
                    json.WriteString(L"CustomData");
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

            json.WriteString(L"Offset");
            json.WriteNumber(offset);

            json.WriteString(L"Type");
            json.WriteString(L"FREE");

            json.WriteString(L"Size");
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


} // namespace rhi::ma