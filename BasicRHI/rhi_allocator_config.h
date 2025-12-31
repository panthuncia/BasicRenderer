#pragma once

#include <algorithm>

#define D3D12_SDK_VERSION 618 // TODO: hack

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

#if !defined(D3D12MA_ATOMIC_UINT32) || !defined(D3D12MA_ATOMIC_UINT64)
#include <atomic>
#endif

#ifndef D3D12MA_ATOMIC_UINT32
#define D3D12MA_ATOMIC_UINT32 std::atomic<UINT>
#endif

#ifndef D3D12MA_ATOMIC_UINT64
#define D3D12MA_ATOMIC_UINT64 std::atomic<UINT64>
#endif

#endif // _D3D12MA_CONFIGURATION