#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <limits>
#include <directx/d3dcommon.h>
#include <optional>
#include <array>
#include <unordered_map>
#include <memory>
#include <utility> // C++23: std::to_underlying

#include "resource_states.h"
#include "rhi_feature_Info.h"

namespace rhi {

	inline void BreakIfDebugging() {
#if BUILD_TYPE == BUILD_DEBUG
		if (IsDebuggerPresent()) {
			__debugbreak();
		}
#endif
	}
#define RHI_FAIL(x) do { rhi::BreakIfDebugging(); return (x); } while(0)

	inline constexpr uint32_t RHI_DEVICE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_QUEUE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_CL_ABI_MIN = 1;
	inline constexpr uint32_t RHI_SC_ABI_MIN = 1;
	inline constexpr uint32_t RHI_CA_ABI_MIN = 1;
	inline constexpr uint32_t RHI_RESOURCE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_HEAP_ABI_MIN = 1;
	inline constexpr uint32_t RHI_QUERYPOOL_ABI_MIN = 1;
	inline constexpr uint32_t RHI_PIPELINE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_PIPELINELAYOUT_ABI_MIN = 1;
	inline constexpr uint32_t RHI_COMMANDSIGNATURE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_SAMPLER_ABI_MIN = 1;
	inline constexpr uint32_t RHI_DESCRIPTORHEAP_ABI_MIN = 1;
	inline constexpr uint32_t RHI_TIMELINE_ABI_MIN = 1;

	class Device;

	namespace detail {
		// tag types for each handle family
		struct HResource {}; // "either buffer or texture" handle
		struct HView {};
		struct HSampler {};
		struct HPipeline {};
		struct HCmdSig {};
		struct HPipelineLayout {};
		struct HDescHeap {};
		struct HCmdAlloc {};
		struct HTimeline {};
		struct HCommandAllocator {};
		struct HCommandList {};
		struct HHeap {};
		struct HQueryPool {};
		struct HSwapChain {};

		// forward-declared trait; default is no-op (safe in release)
		template<class Tag> struct NameOps {
			static inline void Set(Device*, uint32_t /*index*/, uint32_t /*gen*/, const char*) noexcept {}
		};
	} // namespace detail

	template<class Tag>
	struct Handle {
		uint32_t index{ 0xFFFFFFFFu };
		uint32_t generation{ 0 };
		constexpr bool valid() const noexcept { return index != 0xFFFFFFFFu && generation != 0; }
	};

	// hash for handles
	template<class H>
	struct HandleHash {
		size_t operator()(const H& h) const noexcept {
			// mix index + generation into one 64-bit value
			uint64_t x = (uint64_t(h.index) << 32) | uint64_t(h.generation);
			// finalizer (splitmix64-style)
			x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
			x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
			x ^= x >> 33;
			return static_cast<size_t>(x);
		}
	};
	// equality for handles
	template<class H>
	struct HandleEqual {
		bool operator()(const H& a, const H& b) const noexcept {
			return a.index == b.index && a.generation == b.generation;
		}
	};

	using ResourceHandle = Handle<detail::HResource>;
	using ViewHandle = Handle<detail::HView>;
	using SamplerHandle = Handle<detail::HSampler>;
	using PipelineHandle = Handle<detail::HPipeline>;
	using CommandSignatureHandle = Handle<detail::HCmdSig>;
	using PipelineLayoutHandle = Handle<detail::HPipelineLayout>;
	using DescriptorHeapHandle = Handle<detail::HDescHeap>;
	using TimelineHandle = Handle<detail::HTimeline>;
	using CommandAllocatorHandle = Handle<detail::HCommandAllocator>;
	using CommandListHandle = Handle<detail::HCommandList>;
	using HeapHandle = Handle<detail::HHeap>;
	using QueryPoolHandle = Handle<detail::HQueryPool>;
	using SwapChainHandle = Handle<detail::HSwapChain>;

	// ---------------- Enums & structs ----------------

	enum class Backend : uint32_t { Null, D3D12, Vulkan };
	enum class QueueKind : uint32_t { Graphics, Compute, Copy };

	enum class Result : uint32_t
	{
		Ok = 0, // S_OK
		False = 1, // S_FALSE
		Unknown = 2, // Should not happen

		// "Success with info" (mostly Present / Desktop Duplication scenarios)
		PresentOccluded,              // DXGI_STATUS_OCCLUDED
		PresentClipped,               // DXGI_STATUS_CLIPPED
		PresentUnoccluded,            // DXGI_STATUS_UNOCCLUDED
		PresentRequired,              // DXGI_STATUS_PRESENT_REQUIRED
		NoRedirection,                // DXGI_STATUS_NO_REDIRECTION
		NoDesktopAccess,              // DXGI_STATUS_NO_DESKTOP_ACCESS
		VidPnSourceInUse,             // DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE
		ModeChanged,                  // DXGI_STATUS_MODE_CHANGED
		ModeChangeInProgress,         // DXGI_STATUS_MODE_CHANGE_IN_PROGRESS
		DdaWasStillDrawing,           // DXGI_STATUS_DDA_WAS_STILL_DRAWING

		// ---------------------------------------------------------------------
		// Generic failures
		// ---------------------------------------------------------------------
		Failed,                       // E_FAIL (generic)
		Unexpected,                   // E_UNEXPECTED
		Aborted,                      // E_ABORT
		AccessDenied,                 // E_ACCESSDENIED
		InvalidArgument,              // E_INVALIDARG
		InvalidNativeHandle,          // E_HANDLE / HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)
		InvalidNativePointer,         // E_POINTER
		NoInterface,                  // E_NOINTERFACE
		NotImplemented,               // E_NOTIMPL
		OutOfMemory,                  // E_OUTOFMEMORY

		// ---------------------------------------------------------------------
		// DXGI / D3D12 call correctness & capability
		// ---------------------------------------------------------------------
		InvalidCall,                  // DXGI_ERROR_INVALID_CALL
		Unsupported,                  // DXGI_ERROR_UNSUPPORTED
		SdkComponentMissing,          // DXGI_ERROR_SDK_COMPONENT_MISSING
		DynamicCodePolicyViolation,   // DXGI_ERROR_DYNAMIC_CODE_POLICY_VIOLATION

		// ---------------------------------------------------------------------
		// Existence / sizing / uniqueness
		// ---------------------------------------------------------------------
		NotFound,                     // DXGI_ERROR_NOT_FOUND
		MoreData,                     // DXGI_ERROR_MORE_DATA
		AlreadyExists,                // DXGI_ERROR_ALREADY_EXISTS
		NameAlreadyExists,            // DXGI_ERROR_NAME_ALREADY_EXISTS

		// ---------------------------------------------------------------------
		// Device / driver loss (important for recovery decisions)
		// ---------------------------------------------------------------------
		DeviceLost,                   // umbrella 
		DeviceRemoved,                // DXGI_ERROR_DEVICE_REMOVED
		DeviceHung,                   // DXGI_ERROR_DEVICE_HUNG
		DeviceReset,                  // DXGI_ERROR_DEVICE_RESET
		DriverInternalError,          // DXGI_ERROR_DRIVER_INTERNAL_ERROR

		// ---------------------------------------------------------------------
		// "Try again later" / synchronization
		// ---------------------------------------------------------------------
		StillDrawing,                 // DXGI_ERROR_WAS_STILL_DRAWING
		WaitTimeout,                  // DXGI_ERROR_WAIT_TIMEOUT (+ WAIT_TIMEOUT from Win32 waits)

		// ---------------------------------------------------------------------
		// Presentation / session / output lifetime
		// ---------------------------------------------------------------------
		NotCurrent,                   // DXGI_ERROR_NOT_CURRENT
		ModeChangeBlocked,            // DXGI_ERROR_MODE_CHANGE_IN_PROGRESS (retry later)
		SessionDisconnected,          // DXGI_ERROR_SESSION_DISCONNECTED
		RemoteClientDisconnected,     // DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED
		RestrictToOutputStale,        // DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE
		NonCompositedUi,              // DXGI_ERROR_NON_COMPOSITED_UI
		PresentationLost,             // PRESENTATION_ERROR_LOST
		SetDisplayModeRequired,       // DXGI_ERROR_SETDISPLAYMODE_REQUIRED
		FrameStatisticsDisjoint,      // DXGI_ERROR_FRAME_STATISTICS_DISJOINT

		// ---------------------------------------------------------------------
		// Sharing / keyed mutex / global counters
		// ---------------------------------------------------------------------
		AccessLost,                   // DXGI_ERROR_ACCESS_LOST
		NonExclusive,                 // DXGI_ERROR_NONEXCLUSIVE

		// ---------------------------------------------------------------------
		// Content protection / protected memory
		// ---------------------------------------------------------------------
		CannotProtectContent,         // DXGI_ERROR_CANNOT_PROTECT_CONTENT
		HwProtectionOutOfMemory,      // DXGI_ERROR_HW_PROTECTION_OUTOFMEMORY

		// ---------------------------------------------------------------------
		// Shader cache
		// ---------------------------------------------------------------------
		CacheCorrupt,                 // DXGI_ERROR_CACHE_CORRUPT
		CacheFull,                    // DXGI_ERROR_CACHE_FULL
		CacheHashCollision,           // DXGI_ERROR_CACHE_HASH_COLLISION

		// ---------------------------------------------------------------------
		// D3D12-specific "configuration mismatch" errors
		// ---------------------------------------------------------------------
		AdapterNotFound,              // D3D12_ERROR_ADAPTER_NOT_FOUND
		DriverVersionMismatch,        // D3D12_ERROR_DRIVER_VERSION_MISMATCH
		InvalidRedistributable,       // D3D12_ERROR_INVALID_REDIST

		// ---------------------------------------------------------------------
		// Rare
		// ---------------------------------------------------------------------
		MpoUnpinned,                  // DXGI_ERROR_MPO_UNPINNED
		RemoteOutOfMemory,            // DXGI_ERROR_REMOTE_OUTOFMEMORY

		// ---------------------------------------------------------------------
		// Non-DXGI Win32 errors
		// (shader loading/includes, file mapping, waits, etc.)
		// ---------------------------------------------------------------------
		FileNotFound,                 // HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
		PathNotFound,                 // HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)
		InvalidData,                  // HRESULT_FROM_WIN32(ERROR_INVALID_DATA)
		DiskFull,                     // HRESULT_FROM_WIN32(ERROR_DISK_FULL)
		SharingViolation,             // HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)
	};

	constexpr bool IsOk(Result r) noexcept
	{
		switch (r)
		{
		case Result::Ok:
		case Result::False:
		case Result::PresentOccluded:
		case Result::PresentClipped:
		case Result::PresentUnoccluded:
		case Result::PresentRequired:
		case Result::NoRedirection:
		case Result::NoDesktopAccess:
		case Result::VidPnSourceInUse:
		case Result::ModeChanged:
		case Result::ModeChangeInProgress:
		case Result::DdaWasStillDrawing:
			return true;

		default:
			return false;
		}
	}

	constexpr bool Failed(Result r) noexcept { return !IsOk(r); }


	template<typename T>
	struct Span {
		constexpr Span() noexcept = default;
		constexpr Span(const T* d, uint32_t s) noexcept
			: data(d), size(s) {
		}
		constexpr Span(const T* d) noexcept
			: data(d), size(d ? 1 : 0) {
		}
		const T* data{};
		uint32_t size{};
		const T* begin() const noexcept { return data; }
		const T* end()   const noexcept { return data + size; }
		const T& operator[](size_t index) const noexcept {
			return data[index];
		}
	};

	// ---------------- Formats & resource descriptors (trimmed) ----------------

	enum class Format : uint32_t {
		Unknown,
		R32G32B32A32_Typeless, R32G32B32A32_Float, R32G32B32A32_UInt, R32G32B32A32_SInt,
		R32G32B32_Typeless, R32G32B32_Float, R32G32B32_UInt, R32G32B32_SInt,
		R16G16B16A16_Typeless, R16G16B16A16_Float, R16G16B16A16_UNorm, R16G16B16A16_UInt, R16G16B16A16_SNorm, R16G16B16A16_SInt,
		R32G32_Typeless, R32G32_Float, R32G32_UInt, R32G32_SInt,
		R10G10B10A2_Typeless, R10G10B10A2_UNorm, R10G10B10A2_UInt,
		R11G11B10_Float,
		R8G8B8A8_Typeless, R8G8B8A8_UNorm, R8G8B8A8_UNorm_sRGB, R8G8B8A8_UInt, R8G8B8A8_SNorm, R8G8B8A8_SInt,
		R16G16_Typeless, R16G16_Float, R16G16_UNorm, R16G16_UInt, R16G16_SNorm, R16G16_SInt,
		R32_Typeless, D32_Float, R32_Float, R32_UInt, R32_SInt,
		R8G8_Typeless, R8G8_UNorm, R8G8_UInt, R8G8_SNorm, R8G8_SInt,
		R16_Typeless, R16_Float, R16_UNorm, R16_UInt, R16_SNorm, R16_SInt,
		R8_Typeless, R8_UNorm, R8_UInt, R8_SNorm, R8_SInt,
		BC1_Typeless, BC1_UNorm, BC1_UNorm_sRGB,
		BC2_Typeless, BC2_UNorm, BC2_UNorm_sRGB,
		BC3_Typeless, BC3_UNorm, BC3_UNorm_sRGB,
		BC4_Typeless, BC4_UNorm, BC4_SNorm,
		BC5_Typeless, BC5_UNorm, BC5_SNorm,
		B8G8R8A8_Typeless, B8G8R8A8_UNorm, B8G8R8A8_UNorm_sRGB,
		BC6H_Typeless, BC6H_UF16, BC6H_SF16,
		BC7_Typeless, BC7_UNorm, BC7_UNorm_sRGB,
	};

	constexpr uint32_t FormatByteSize(Format f) noexcept {
		switch (f) {
			// 16 bytes (4 x 32-bit)
		case Format::R32G32B32A32_Typeless: case Format::R32G32B32A32_Float:
		case Format::R32G32B32A32_UInt:     case Format::R32G32B32A32_SInt: return 16;

			// 12 bytes (3 x 32-bit)
		case Format::R32G32B32_Typeless: case Format::R32G32B32_Float:
		case Format::R32G32B32_UInt:    case Format::R32G32B32_SInt:        return 12;

			// 8 bytes (4 x 16-bit OR 2 x 32-bit)
		case Format::R16G16B16A16_Typeless: case Format::R16G16B16A16_Float:
		case Format::R16G16B16A16_UNorm:    case Format::R16G16B16A16_UInt:
		case Format::R16G16B16A16_SNorm:    case Format::R16G16B16A16_SInt: return 8;

		case Format::R32G32_Typeless: case Format::R32G32_Float:
		case Format::R32G32_UInt:     case Format::R32G32_SInt:             return 8;

			// 4 bytes (special packed 10:10:10:2, 11:11:10, 4 x 8-bit, single 32-bit)
		case Format::R10G10B10A2_Typeless: case Format::R10G10B10A2_UNorm:
		case Format::R10G10B10A2_UInt:                                          return 4;

		case Format::R11G11B10_Float:                                           return 4;

		case Format::R8G8B8A8_Typeless: case Format::R8G8B8A8_UNorm:
		case Format::R8G8B8A8_UNorm_sRGB: case Format::R8G8B8A8_UInt:
		case Format::R8G8B8A8_SNorm:  case Format::R8G8B8A8_SInt:               return 4;

		case Format::R32_Typeless: case Format::D32_Float:
		case Format::R32_Float:    case Format::R32_UInt:
		case Format::R32_SInt:                                                     return 4;

		case Format::B8G8R8A8_Typeless: case Format::B8G8R8A8_UNorm:
		case Format::B8G8R8A8_UNorm_sRGB:                                          return 4;

			// 2 bytes (2 x 8-bit OR single 16-bit)
		case Format::R8G8_Typeless: case Format::R8G8_UNorm:
		case Format::R8G8_UInt:     case Format::R8G8_SNorm:
		case Format::R8G8_SInt:                                                     return 2;

		case Format::R16_Typeless: case Format::R16_Float:
		case Format::R16_UNorm:    case Format::R16_UInt:
		case Format::R16_SNorm:    case Format::R16_SInt:                           return 2;

			// 1 byte
		case Format::R8_Typeless:  case Format::R8_UNorm:
		case Format::R8_UInt:      case Format::R8_SNorm:
		case Format::R8_SInt:                                                      return 1;

			// Block-compressed formats: return 0 here (use GetBlockInfo for textures)
		case Format::BC1_Typeless: case Format::BC1_UNorm: case Format::BC1_UNorm_sRGB:
		case Format::BC2_Typeless: case Format::BC2_UNorm: case Format::BC2_UNorm_sRGB:
		case Format::BC3_Typeless: case Format::BC3_UNorm: case Format::BC3_UNorm_sRGB:
		case Format::BC4_Typeless: case Format::BC4_UNorm: case Format::BC4_SNorm:
		case Format::BC5_Typeless: case Format::BC5_UNorm: case Format::BC5_SNorm:
		case Format::BC6H_Typeless: case Format::BC6H_UF16: case Format::BC6H_SF16:
		case Format::BC7_Typeless:  case Format::BC7_UNorm: case Format::BC7_UNorm_sRGB:
			return 0;

		case Format::Unknown: default: return 0;
		}
	}

	struct BlockInfo {
		uint32_t blockWidth;     // usually 4
		uint32_t blockHeight;    // usually 4
		uint32_t bytesPerBlock;  // 8 or 16
		bool     isCompressed;
	};

	/// Returns block geometry and bytes for compressed formats.
	/// For uncompressed, returns {1,1, FormatByteSize(f), false}.
	constexpr BlockInfo GetBlockInfo(Format f) noexcept {
		switch (f) {
			// 8 bytes per 4x4
		case Format::BC1_Typeless: case Format::BC1_UNorm: case Format::BC1_UNorm_sRGB:
		case Format::BC4_Typeless: case Format::BC4_UNorm: case Format::BC4_SNorm:
			return { 4, 4, 8, true };

			// 16 bytes per 4x4
		case Format::BC2_Typeless: case Format::BC2_UNorm: case Format::BC2_UNorm_sRGB:
		case Format::BC3_Typeless: case Format::BC3_UNorm: case Format::BC3_UNorm_sRGB:
		case Format::BC5_Typeless: case Format::BC5_UNorm: case Format::BC5_SNorm:
		case Format::BC6H_Typeless: case Format::BC6H_UF16: case Format::BC6H_SF16:
		case Format::BC7_Typeless:  case Format::BC7_UNorm: case Format::BC7_UNorm_sRGB:
			return { 4, 4, 16, true };

		default:
			// Not compressed: single texel 'element size'
			return { 1, 1, FormatByteSize(f), false };
		}
	}

	enum class ViewKind : uint32_t { SRV, UAV, RTV, DSV };
	struct TextureSubresourceRange { uint32_t baseMip = 0, mipCount = 1; uint32_t baseLayer = 0, layerCount = 1; };
	struct ViewDesc { ViewKind kind = ViewKind::SRV; ResourceHandle texture{}; TextureSubresourceRange range{}; Format formatOverride = Format::Unknown; };

	enum class Filter : uint8_t { Nearest, Linear };
	enum class MipFilter : uint8_t { Nearest, Linear };
	enum class AddressMode : uint8_t { Wrap, Mirror, Clamp, Border, MirrorOnce };
	enum class CompareOp : uint8_t {
		Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always
	};
	enum class ReductionMode : uint8_t { Standard, Comparison, Min, Max };

	// Border presets are guaranteed portable across APIs.
	// If we want arbitrary float[4] border on Vulkan, we'll
	// need VK_EXT_custom_border_color
	enum class BorderPreset : uint8_t {
		TransparentBlack, OpaqueBlack, OpaqueWhite, Custom // 'Custom' uses borderColor[]
	};

	struct SamplerDesc {
		// Filtering
		Filter   minFilter{ Filter::Linear };
		Filter   magFilter{ Filter::Linear };
		MipFilter mipFilter{ MipFilter::Linear };

		// Addressing
		AddressMode addressU{ AddressMode::Clamp };
		AddressMode addressV{ AddressMode::Clamp };
		AddressMode addressW{ AddressMode::Clamp };

		// Mip LODs
		float mipLodBias{ 0.f };
		float minLod{ 0.f };
		float maxLod{ (std::numeric_limits<float>::max)() };

		// Anisotropy
		uint32_t maxAnisotropy{ 1 }; // >1 enables anisotropy (clamped to device limit)

		// Comparison / reduction
		bool       compareEnable{ false };
		CompareOp  compareOp{ CompareOp::Always };
		ReductionMode reduction{ ReductionMode::Standard }; // Min/Max map to min/max reduction filters

		// Border color
		BorderPreset borderPreset{ BorderPreset::TransparentBlack };
		float borderColor[4]{ 0.f, 0.f, 0.f, 0.f }; // used when preset==Custom

		// Vulkan-only; DX12 always uses normalized coords
		bool unnormalizedCoordinates{ false };
	};

	enum class IndirectArgKind : uint32_t {
		Draw,           // D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
		DrawIndexed,    // D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
		Dispatch,       // D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH
		VertexBuffer,   // D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW
		IndexBuffer,    // D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW
		Constant,       // D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT
		ConstantBuffer, // D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW
		ShaderResource, // D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW
		UnorderedAccess,// D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW
		DispatchRays,  // DXR: D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS
		DispatchMesh,  // Mesh shaders: D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
		IncrementingConstant, // D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT
	};

	struct IndirectArg {
		IndirectArgKind kind{};
		union {
			struct { uint32_t slot; } vertexBuffer; // for VertexBuffer
			struct { uint32_t rootIndex; uint32_t destOffset32; uint32_t num32; } rootConstants; // RootConstants
			// others need no extra fields
		} u{};
	};

	enum class ShaderStage : uint32_t {
		Vertex = 1,
		Pixel = 2,
		Compute = 4,
		Mesh = 8,
		Task = 16,
		AllGraphics = Vertex | Pixel | Mesh | Task,
		All = 0xFFFFFFFFu
	};

	struct LayoutBindingRange { uint32_t set = 0, binding = 0, count = 1; bool readOnly = true; ShaderStage visibility = ShaderStage::All; };

	struct PushConstantRangeDesc {
		ShaderStage visibility = ShaderStage::All;
		uint32_t num32BitValues = 0;
		uint32_t set = 0;            // maps to RegisterSpace on DX12 (ignored on Vulkan)
		uint32_t binding = 0;        // maps to ShaderRegister on DX12 (ignored on Vulkan)
	};

	struct StaticSamplerDesc {
		SamplerDesc sampler;
		ShaderStage visibility = ShaderStage::All;
		uint32_t set = 0;            // DX12 RegisterSpace / Vulkan set index
		uint32_t binding = 0;        // DX12 ShaderRegister / Vulkan binding
		uint32_t arrayCount = 1;
	};

	enum PipelineLayoutFlags : uint32_t { PF_None = 0, PF_AllowInputAssembler = 1 << 0 }; // only for graphics pipelines

	struct PipelineLayoutDesc {
		Span<LayoutBindingRange> ranges{};
		Span<PushConstantRangeDesc> pushConstants{};
		Span<StaticSamplerDesc> staticSamplers{};
		PipelineLayoutFlags flags;
	};

	struct CommandSignatureDesc {
		Span<IndirectArg> args;
		uint32_t byteStride = 0; // sizeof(struct in argument buffer)
	};

	enum class DescriptorHeapType : uint32_t { CbvSrvUav, Sampler, RTV, DSV };

	struct DescriptorHeapDesc {
		DescriptorHeapType type;      // CbvSrvUav, Sampler, RTV, DSV
		uint32_t           capacity;  // NumDescriptors
		bool               shaderVisible;
		const char* debugName{ nullptr };
	};

	struct DescriptorSlot {
		DescriptorSlot() = default;
		DescriptorSlot(DescriptorHeapHandle h, uint32_t i) : heap(h), index(i) {}
		DescriptorHeapHandle heap{};
		uint32_t index{};
	};

	enum class UavDim : uint32_t {
		Undefined,
		Buffer,
		Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		Texture2DMS,
		Texture2DMSArray,
		Texture3D
	};

	enum class RtvDim : uint32_t {
		Undefined,
		Buffer,
		Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		Texture2DMS,
		Texture2DMSArray,
		Texture3D
	};

	enum class DsvDim : uint32_t {
		Undefined,
		Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		Texture2DMS,
		Texture2DMSArray
	};

	enum class BufferViewKind : uint32_t { Raw, Structured, Typed };

	enum class SrvDim : uint32_t {
		Undefined,
		Buffer,
		Texture1D, Texture1DArray,
		Texture2D, Texture2DArray,
		Texture2DMS, Texture2DMSArray,
		Texture3D,
		TextureCube, TextureCubeArray,
		AccelerationStruct, // DXR TLAS/BLAS SRV (VK_KHR_acceleration_structure)
	};

	// 0 => use API default (DX12_DEFAULT_SHADER_4_COMPONENT_MAPPING / RGBA identity in VK)
	using ComponentMapping = uint32_t;

	struct SrvDesc {
		SrvDim	dimension{ SrvDim::Undefined };
		Format	formatOverride{ Format::Unknown }; // textures + typed buffers
		ComponentMapping componentMapping{ 0 };          // optional, 0 = default

		union {
			struct { // ===== Buffer SRV =====
				BufferViewKind kind{ BufferViewKind::Raw }; // Raw/Structured/Typed
				// RAW: firstElement in 32-bit units; TYPED/STRUCTURED: in elements
				uint64_t firstElement{ 0 };
				uint32_t numElements{ 0 };
				uint32_t structureByteStride{ 0 }; // Structured only
				// typed format comes from formatOverride
			} buffer;

			struct { // ===== TEX1D =====
				uint32_t mostDetailedMip{ 0 };
				uint32_t mipLevels{ 0 };
				float    minLodClamp{ 0.0f };
			} tex1D;

			struct { // ===== TEX1D ARRAY =====
				uint32_t mostDetailedMip{ 0 };
				uint32_t mipLevels{ 0 };
				uint32_t firstArraySlice{ 0 };
				uint32_t arraySize{ 0 };
				float    minLodClamp{ 0.0f };
			} tex1DArray;

			struct { // ===== TEX2D =====
				uint32_t mostDetailedMip{ 0 };
				uint32_t mipLevels{ 0 };
				uint32_t planeSlice{ 0 };     // for planar formats
				float    minLodClamp{ 0.0f };
			} tex2D;

			struct { // ===== TEX2D ARRAY =====
				uint32_t mostDetailedMip{ 0 };
				uint32_t mipLevels{ 0 };
				uint32_t firstArraySlice{ 0 };
				uint32_t arraySize{ 0 };
				uint32_t planeSlice{ 0 };
				float    minLodClamp{ 0.0f };
			} tex2DArray;

			struct { // ===== TEX2DMS =====
				// no fields
			} tex2DMS;

			struct { // ===== TEX2DMS ARRAY =====
				uint32_t firstArraySlice{ 0 };
				uint32_t arraySize{ 0 };
			} tex2DMSArray;

			struct { // ===== TEX3D =====
				uint32_t mostDetailedMip{ 0 };
				uint32_t mipLevels{ 0 };
				float    minLodClamp{ 0.0f };
			} tex3D;

			struct { // ===== CUBE =====
				uint32_t mostDetailedMip{ 0 };
				uint32_t mipLevels{ 0 };
				float    minLodClamp{ 0.0f };
			} cube;

			struct { // ===== CUBE ARRAY =====
				uint32_t mostDetailedMip{ 0 };
				uint32_t mipLevels{ 0 };
				uint32_t first2DArrayFace{ 0 };
				uint32_t numCubes{ 0 }; // arraySize / 6
				float    minLodClamp{ 0.0f };
			} cubeArray;

			struct { // ===== ACCEL STRUCT =====
				// no fields; resource is the AS buffer
			} accel;
		};
	};

	struct UavDesc {
		UavDim	dimension{ UavDim::Buffer };
		Format	formatOverride{ Format::Unknown }; // textures + typed buffers
		// Texture path
		union {
			struct { // ===== Buffer UAV =====
				BufferViewKind kind{ BufferViewKind::Raw }; // Raw/Structured/Typed
				// RAW: firstElement in 32-bit units; TYPED/STRUCTURED: in elements
				uint64_t firstElement{ 0 };
				uint32_t numElements{ 0 };
				uint32_t structureByteStride{ 0 }; // Structured only
				uint64_t counterOffsetInBytes{ 0 }; // optional, for append/consume buffers
			} buffer;
			struct { // ===== TEX1D =====
				uint32_t mipSlice{ 0 };
			} texture1D;
			struct { // ===== TEX1D ARRAY =====
				uint32_t mipSlice{ 0 };
				uint32_t firstArraySlice{ 0 };
				uint32_t arraySize{ 0 };
			} texture1DArray;
			struct { // ===== TEX2D =====
				uint32_t mipSlice{ 0 };
				uint32_t planeSlice{ 0 }; // for planar formats
			} texture2D;
			struct { // ===== TEX2D ARRAY =====
				uint32_t mipSlice{ 0 };
				uint32_t firstArraySlice{ 0 };
				uint32_t arraySize{ 0 };
				uint32_t planeSlice{ 0 }; // for planar formats
			} texture2DArray;
			struct { // ===== TEX2DMS =====
				// no fields
			} texture2DMS;
			struct { // ===== TEX2DMS ARRAY =====
				uint32_t firstArraySlice{ 0 };
				uint32_t arraySize{ 0 };
			} texture2DMSArray;
			struct { // ===== TEX3D =====
				uint32_t mipSlice{ 0 };
				uint32_t firstWSlice{ 0 };
				uint32_t wSize{ 0 };
			} texture3D;
		};
	};

	struct CbvDesc { uint64_t byteOffset = 0; uint32_t byteSize = 0; /* 256B aligned */ };

	// RTV/DSV descriptions (texture-only)
	struct RtvDesc {
		RtvDim dimension{ RtvDim::Texture2D };
		TextureSubresourceRange range{};
		Format formatOverride{ Format::Unknown };
	};
	struct DsvDesc {
		DsvDim dimension{ DsvDim::Texture2D };
		TextureSubresourceRange range{};
		Format formatOverride{ Format::Unknown };
		bool readOnlyDepth{ false };
		bool readOnlyStencil{ false };
	};

	// Command list binding for shader-visible heaps
	struct BoundDescriptorHeaps {
		DescriptorHeapHandle cbvSrvUav{};
		DescriptorHeapHandle sampler{};
	};

	enum class FillMode : uint32_t { Solid, Wireframe };
	enum class CullMode : uint32_t { None, Front, Back };
	enum class BlendFactor : uint32_t { One, Zero, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DstColor, InvDstColor, DstAlpha, InvDstAlpha };
	enum class BlendOp : uint32_t { Add, Sub, RevSub, Min, Max };
	enum ColorWriteEnable : uint8_t { R = 1, G = 2, B = 4, A = 8, All = 0x0F };

	struct ShaderBinary { const void* data{}; uint32_t size{}; };
	struct RasterState {
		FillMode fill = FillMode::Solid;
		CullMode cull = CullMode::Back;
		bool frontCCW = false;
		int32_t depthBias = 0;
		float   depthBiasClamp = 0;
		float   slopeScaledDepthBias = 0;
		bool    conservative = false;
	};

	struct DepthStencilState {
		bool depthEnable = true;
		bool depthWrite = true;
		CompareOp depthFunc = CompareOp::LessEqual;
		// TODO: stencil
	};

	struct BlendAttachment {
		bool enable = false;
		BlendFactor srcColor = BlendFactor::One;
		BlendFactor dstColor = BlendFactor::Zero;
		BlendOp     colorOp = BlendOp::Add;
		BlendFactor srcAlpha = BlendFactor::One;
		BlendFactor dstAlpha = BlendFactor::Zero;
		BlendOp     alphaOp = BlendOp::Add;
		ColorWriteEnable     writeMask = ColorWriteEnable::All; // RGBA
	};
	struct BlendState {
		bool alphaToCoverage = false;
		bool independentBlend = false;
		uint32_t numAttachments = 1;
		BlendAttachment attachments[8]{};
	};

	struct RenderTargets {
		uint32_t count = 0;
		Format   formats[8]{ Format::Unknown };
	};
	struct SampleDesc { uint32_t count = 1; uint32_t quality = 0; };

	enum class InputRate : uint8_t { PerVertex, PerInstance };

	// Sentinel like D3D12_APPEND_ALIGNED_ELEMENT: auto-place next at natural alignment.
	inline constexpr uint32_t APPEND_ALIGNED = 0xFFFFFFFFu;

	// One vertex buffer binding (D3D12 InputSlot / Vulkan binding).
	struct InputBindingDesc {
		uint32_t  binding = 0;                // 0..N-1
		uint32_t  stride = 0;                 // bytes; 0 means "compute from attributes"
		InputRate rate = InputRate::PerVertex;
		uint32_t  instanceStepRate = 1;       // DX12: InstanceDataStepRate; Vulkan: divisor (needs EXT)
	};

	// One vertex attribute (D3D12 element / Vulkan attribute).
	struct InputAttributeDesc {
		uint32_t  binding = 0;              // which binding this attribute reads from
		uint32_t  offset = APPEND_ALIGNED;  // byte offset within binding
		Format    format = Format::Unknown; // data format
		// Either provide HLSL semantics (for DX12) or GLSL/SPIR-V location (for Vulkan) or both.
		const char* semanticName = nullptr; // e.g., "POSITION","NORMAL","TEXCOORD"
		uint32_t    semanticIndex = 0;      // e.g., TEXCOORD1 -> 1
		uint32_t    location = 0xFFFFFFFFu; // Vulkan location; 0xFFFFFFFFu => auto-assign
	};

	struct InputLayoutDesc {
		const InputBindingDesc* bindings = nullptr;
		uint32_t                  numBindings = 0;
		const InputAttributeDesc* attributes = nullptr;
		uint32_t                  numAttributes = 0;
	};

	// A finalized layout with concrete offsets/strides/locations resolved.
	struct FinalizedInputLayout {
		std::vector<InputBindingDesc>   bindings;
		std::vector<InputAttributeDesc> attributes;
	};

	// Align v up to a multiple of a.
	static inline uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + (a - 1)) & ~(a - 1); }

	// Produce concrete offsets (replacing APPEND_ALIGNED), default strides, and auto locations.
	inline FinalizedInputLayout Finalize(const InputLayoutDesc& in)
	{
		FinalizedInputLayout out;
		out.bindings.assign(in.bindings, in.bindings + in.numBindings);
		out.attributes.assign(in.attributes, in.attributes + in.numAttributes);

		// Per-binding running cursor for APPEND_ALIGNED offsets.
		std::unordered_map<uint32_t, uint32_t> cursor;

		// Ensure cursors exist.
		for (const auto& b : out.bindings) cursor[b.binding] = 0;

		// Resolve offsets.
		for (auto& a : out.attributes) {
			const uint32_t elemSize = FormatByteSize(a.format);
			// Natural alignment: commonly min(elemSize, 4) is sufficient; tweak if we need 8/16 for 64/128-bit.
			const uint32_t align = (elemSize >= 4) ? 4 : elemSize;
			if (a.offset == APPEND_ALIGNED) {
				uint32_t& cur = cursor[a.binding];
				cur = AlignUp(cur, align);
				a.offset = cur;
				cur += elemSize;
			}
			else {
				// Keep cursor at least past this attribute for stride computation
				uint32_t& cur = cursor[a.binding];
				cur = (std::max)(cur, AlignUp(a.offset + elemSize, align));
			}
		}

		// Default strides if zero: use cursor (max end) per binding.
		for (auto& b : out.bindings) {
			if (b.stride == 0) b.stride = cursor[b.binding];
		}

		// Auto-assign locations if not provided: assign in declaration order within each binding
		// or globally; here we do global increasing order.
		uint32_t nextLoc = 0;
		for (auto& a : out.attributes) {
			if (a.location == 0xFFFFFFFFu) a.location = nextLoc++;
		}

		return out;
	}

	enum class PsoSubobj : uint32_t {
		Layout,
		Shader,           // AS/MS/VS/PS -> stage inside payload
		Rasterizer,
		Blend,
		DepthStencil,
		RTVFormats,
		DSVFormat,
		Sample,
		Flags,             // optional backend-specific flags bitset
		InputLayout    // optional, graphics only
	};

	struct SubobjLayout { PipelineLayoutHandle layout{}; };
	struct SubobjShader { ShaderStage stage{}; ShaderBinary bytecode{}; };
	struct SubobjRaster { RasterState rs{}; };
	struct SubobjBlend { BlendState bs{}; };
	struct SubobjDepth { DepthStencilState ds{}; };
	struct SubobjRTVs { RenderTargets rt{}; };
	struct SubobjDSV { Format dsv = Format::Unknown; };
	struct SubobjSample { SampleDesc sd{}; };
	struct SubobjInputLayout { FinalizedInputLayout il{}; }; // optional, for graphics
	// struct SubobjFlags { uint64_t mask = 0; }; // optional

	struct PipelineStreamItem {
		PsoSubobj type;
		const void* data;   // points to one of the Subobj* structs above
		uint32_t   size;    // sizeof(that struct)
	};

	// Convenience makers
	inline PipelineStreamItem Make(const SubobjLayout& x) { return { PsoSubobj::Layout, &x,  sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjShader& x) { return { PsoSubobj::Shader, &x,  sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjRaster& x) { return { PsoSubobj::Rasterizer, &x, sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjBlend& x) { return { PsoSubobj::Blend, &x,   sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjDepth& x) { return { PsoSubobj::DepthStencil, &x, sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjRTVs& x) { return { PsoSubobj::RTVFormats, &x, sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjDSV& x) { return { PsoSubobj::DSVFormat, &x, sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjSample& x) { return { PsoSubobj::Sample, &x,  sizeof(x) }; }
	inline PipelineStreamItem Make(const SubobjInputLayout& x) { return { PsoSubobj::InputLayout, &x, sizeof(x) }; }


	// ---------------- Pass & barriers (minimal) ----------------

	enum class LoadOp : uint32_t { Load, Clear, DontCare }; enum class StoreOp : uint32_t { Store, DontCare };
	enum class ClearValueType : uint32_t { Color, DepthStencil };
	struct DepthStencilClearValue { float depth = 1.0f; uint8_t stencil = 0; };
	struct ClearValue {
		ClearValueType type = ClearValueType::Color;
		Format format = Format::Unknown;
		// union
		union {
			float rgba[4]{ 0,0,0,1 };
			DepthStencilClearValue depthStencil;
		};
	};
	struct ColorAttachment {
		DescriptorSlot rtv{};
		LoadOp loadOp = LoadOp::Load;
		StoreOp storeOp = StoreOp::Store;
		ClearValue clear{};
	};

	struct DepthAttachment {
		DescriptorSlot dsv{};
		LoadOp  depthLoad = LoadOp::Load;
		StoreOp depthStore = StoreOp::Store;
		LoadOp  stencilLoad = LoadOp::DontCare;
		StoreOp stencilStore = StoreOp::DontCare;
		ClearValue clear{};
		bool readOnly = false;
	};

	struct PassBeginInfo {
		Span<ColorAttachment> colors{};
		const DepthAttachment* depth{};
		uint32_t width = 0, height = 0;
		const char* debugName = nullptr;
	};
	enum ResourceFlags : uint32_t {
		RF_None = 0,
		RF_AllowRenderTarget = 1 << 0,
		RF_AllowDepthStencil = 1 << 1,
		RF_AllowUnorderedAccess = 1 << 2,
		RF_DenyShaderResource = 1 << 3,
		RF_AllowCrossAdapter = 1 << 4,
		RF_AllowSimultaneousAccess = 1 << 5,
		RF_VideoDecodeReferenceOnly = 1 << 6,
		RF_VideoEncodeReferenceOnly = 1 << 7,
		RF_RaytracingAccelerationStructure = 1 << 8,
		RF_UseTightAlignment = 1 << 9
	};
	// Define |= for ResourceFlags
	inline ResourceFlags operator|(ResourceFlags a, ResourceFlags b) {
		return static_cast<ResourceFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline ResourceFlags& operator|=(ResourceFlags& a, ResourceFlags b) {
		a = a | b;
		return a;
	}
	struct BufferDesc {
		uint64_t     sizeBytes = 0;
	};
	struct TextureDesc {
		Format       format = Format::Unknown;
		uint32_t     width = 1;
		uint32_t     height = 1;
		uint16_t     depthOrLayers = 1;     // depth for 3D, arraySize otherwise
		uint16_t     mipLevels = 1;
		uint32_t     sampleCount = 1;
		ResourceLayout initialLayout = ResourceLayout::Undefined;
		const ClearValue* optimizedClear = nullptr;    // optional, if RTV/DSV
	};

	enum class HeapType {
		DeviceLocal, // D3D12: DEFAULT; Vulkan: DEVICE_LOCAL
		HostVisibleCoherent, // D3D12: UPLOAD; Vulkan: HOST_VISIBLE + HOST_COHERENT
		Upload = HostVisibleCoherent,
		HostVisibleCached, // D3D12: READBACK; Vulkan: HOST_VISIBLE + HOST_CACHED
		Readback = HostVisibleCached,
		HostCached, // D3D12: CUSTOM; Vulkan: HOST_CACHED
		HostVisibleDeviceLocal, // D3D12: GPU_UPLOAD; Vulkan: HOST_VISIBLE + DEVICE_LOCAL
		GPUUpload = HostVisibleDeviceLocal,
		Custom // D3D12: CUSTOM; Vulkan: CUSTOM
	};

	enum class HeapFlags : uint32_t {
		None = 0,
		AllowOnlyBuffers = 1u << 0,  // D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
		AllowOnlyNonRtDsTextures = 1u << 1,  // D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
		AllowOnlyRtDsTextures = 1u << 2,  // D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES
		DenyBuffers = 1u << 3,  // D3D12_HEAP_FLAG_DENY_BUFFERS
		DenyRtDsTextures = 1u << 4,  // D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES
		DenyNonRtDsTextures = 1u << 5,  // D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES
		Shared = 1u << 6,  // D3D12_HEAP_FLAG_SHARED
		SharedCrossAdapter = 1u << 7,  // D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER
		CreateNotResident = 1u << 8,  // D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT
		CreateNotZeroed = 1u << 9,  // D3D12_HEAP_FLAG_CREATE_NOT_ZEROED
		AllowAllBuffersAndTextures = 1u << 10,  // D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES
		AllowDisplay = 1u << 11,  // D3D12_HEAP_FLAG_ALLOW_DISPLAY
		HardwareProtected = 1u << 12,  // D3D12_HEAP_FLAG_HARDWARE_PROTECTED 
		AllowWriteWatch = 1u << 13,  // D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH
		AllowCrossAdapterShaderAtomics = 1u << 14,  // D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS
	};

	inline constexpr HeapFlags operator|(const HeapFlags a, const HeapFlags b) {
		return static_cast<HeapFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline constexpr HeapFlags& operator|=(HeapFlags& a, const HeapFlags b) { a = a | b; return a; }

	inline constexpr uint32_t to_u32(HeapFlags v) {
		return static_cast<uint32_t>(v);
	}

	inline constexpr HeapFlags operator&(HeapFlags a, HeapFlags b) {
		return static_cast<HeapFlags>(to_u32(a) & to_u32(b));
	}
	inline constexpr HeapFlags& operator&=(HeapFlags& a, HeapFlags b) {
		a = a & b;
		return a;
	}

	inline constexpr HeapFlags operator~(HeapFlags a) {
		return static_cast<HeapFlags>(~to_u32(a));
	}
	inline constexpr HeapFlags operator^(HeapFlags a, HeapFlags b) {
		return static_cast<HeapFlags>(to_u32(a) ^ to_u32(b));
	}
	inline constexpr HeapFlags& operator^=(HeapFlags& a, HeapFlags b) {
		a = a ^ b;
		return a;
	}
	// == operator
	inline constexpr bool operator==(HeapFlags a, HeapFlags b) {
		return to_u32(a) == to_u32(b);
	}

	inline constexpr bool operator==(HeapFlags a, uint32_t b) {
		return to_u32(a) == b;
	}

	inline constexpr bool operator!=(HeapFlags a, HeapFlags b) {
		return !(a == b);
	}

	inline constexpr bool operator!=(HeapFlags a, uint32_t b) {
		return !(a == b);
	}

	template <class E>
	constexpr bool Any(E e) noexcept
		requires std::is_enum_v<E>
	{
#if __cpp_lib_to_underlying >= 202102L
		return std::to_underlying(e) != 0;
#else
		return static_cast<std::underlying_type_t<E>>(e) != 0;
#endif
	}

	enum class ResourceType : uint32_t { Unknown, Buffer, Texture1D, Texture2D, Texture3D, AccelerationStructure };
	struct ResourceDesc {
		ResourceType type = ResourceType::Unknown;
		HeapType heapType = HeapType::DeviceLocal;
		HeapFlags heapFlags = HeapFlags::None;
		ResourceFlags resourceFlags;
		const char* debugName = nullptr;
		Span<Format> castableFormats;
		union {
			TextureDesc texture;
			BufferDesc buffer;
		};
	};

	struct HeapDesc {
		uint64_t   sizeBytes = 0; // total heap size
		uint64_t   alignment = 0; // 0 -> choose default; otherwise 64KB or 4MB (MSAA) on DX12
		HeapType     memory = HeapType::DeviceLocal; // maps to HEAP_PROPERTIES.Type
		HeapFlags  flags = HeapFlags::None;
		const char* debugName{ nullptr };
	};

	struct VertexBufferView { ResourceHandle buffer{}; uint64_t offset = 0; uint32_t sizeBytes = 0; uint32_t stride = 0; };
	struct IndexBufferView { ResourceHandle buffer{}; uint32_t sizeBytes = 0; uint64_t offset = 0; Format format; };

	enum class Stage : uint32_t { Top, Draw, Pixel, Compute, Copy, Bottom };

	// ---------------- Submission & timelines (thin) ----------------
	struct TimelinePoint { TimelineHandle t; uint64_t value = 0; };
	struct SubmitDesc { Span<TimelinePoint> waits{}; Span<TimelinePoint> signals{}; };

	// --- Texture / Buffer / Global barrier descriptions ---
	struct TextureBarrier {
		ResourceHandle        texture{};
		TextureSubresourceRange range{}; // baseMip/mipCount, baseLayer/layerCount
		ResourceSyncState    beforeSync{ ResourceSyncState::None };
		ResourceSyncState    afterSync{ ResourceSyncState::None };
		ResourceAccessType   beforeAccess{ ResourceAccessType::None };
		ResourceAccessType   afterAccess{ ResourceAccessType::None };
		ResourceLayout       beforeLayout{ ResourceLayout::Undefined };
		ResourceLayout       afterLayout{ ResourceLayout::Undefined };
		bool discard{ false }; // if true, contents before the barrier are undefined (can skip some sync on certain APIs)
	};

	struct BufferBarrier {
		ResourceHandle       buffer{};
		uint64_t             offset{ 0 };
		uint64_t             size{ ~0ull }; // ~0 -> whole resource from offset
		ResourceSyncState    beforeSync{ ResourceSyncState::None };
		ResourceSyncState    afterSync{ ResourceSyncState::None };
		ResourceAccessType   beforeAccess{ ResourceAccessType::None };
		ResourceAccessType   afterAccess{ ResourceAccessType::None };
	};

	struct GlobalBarrier {
		ResourceSyncState    beforeSync{ ResourceSyncState::None };
		ResourceSyncState    afterSync{ ResourceSyncState::None };
		ResourceAccessType   beforeAccess{ ResourceAccessType::None };
		ResourceAccessType   afterAccess{ ResourceAccessType::None };
	};

	// Batch to submit in one call
	struct BarrierBatch {
		Span<TextureBarrier> textures{};
		Span<BufferBarrier>  buffers{};
		Span<GlobalBarrier>  globals{};
	};

	struct UavClearFloat {
		float v[4];
		UavClearFloat() noexcept : v{ 0.f, 0.f, 0.f, 0.f } {}
		// from C array of exactly 4 floats
		template<std::size_t N>
		constexpr UavClearFloat(const float(&rgba)[N]) noexcept {
			static_assert(N == 4, "UavClearFloat expects exactly 4 floats");
			for (std::size_t i = 0; i < 4; ++i) v[i] = rgba[i];
		}

		// from std::array<float,4>
		constexpr UavClearFloat(const std::array<float, 4>& a) noexcept
			: v{ a[0], a[1], a[2], a[3] } {
		}
	};

	struct UavClearUint {
		uint32_t v[4];
		UavClearUint() noexcept : v{ 0, 0, 0, 0 } {}
		// from C array of exactly 4 uint32_t
		template<std::size_t N>
		constexpr UavClearUint(const uint32_t(&rgba)[N]) noexcept {
			static_assert(N == 4, "UavClearUint expects exactly 4 uint32_t");
			for (std::size_t i = 0; i < 4; ++i) v[i] = rgba[i];
		}
		// from std::array<uint32_t,4>
		constexpr UavClearUint(const std::array<uint32_t, 4>& a) noexcept
			: v{ a[0], a[1], a[2], a[3] } {
		}
	};

	struct TextureCopyRegion {
		ResourceHandle texture{};
		uint32_t mip = 0;
		uint32_t arraySlice = 0;     // face for cubemaps (0..5) or array element
		uint32_t x = 0, y = 0, z = 0;
		uint32_t width = 0, height = 0, depth = 1; // depth slices for 3D
	};

	struct BufferTextureCopy {
		ResourceHandle buffer{};
		uint64_t offset = 0;         // byte offset in buffer
		uint32_t rowPitch = 0;       // bytes (must satisfy backend's row alignment)
		uint32_t slicePitch = 0;     // bytes
	};

	// What kind of queries we support at the RHI level
	enum class QueryType : uint32_t {
		Timestamp,            // DX12: TIMESTAMP heap | Vulkan: VK_QUERY_TYPE_TIMESTAMP
		PipelineStatistics,   // DX12: PIPELINE_STATISTICS / *_STATISTICS1 | Vulkan: VK_QUERY_TYPE_PIPELINE_STATISTICS
		Occlusion             // DX12: OCCLUSION | Vulkan: VK_QUERY_TYPE_OCCLUSION
	};

	// Cross-API pipeline stats bitmask (request only what you need).
	// Backends will mask out unsupported bits; check capabilities at device create.
	enum PipelineStatBits : uint64_t {
		PS_IAVertices = 1ull << 0,
		PS_IAPrimitives = 1ull << 1,
		PS_VSInvocations = 1ull << 2,
		PS_GSInvocations = 1ull << 3,
		PS_GSPrimitives = 1ull << 4,
		PS_CInvocations = 1ull << 5, // TessControl
		PS_CPrimitives = 1ull << 6, // TessControl output
		PS_EInvocations = 1ull << 7, // TessEval
		PS_PSInvocations = 1ull << 8,
		PS_CSInvocations = 1ull << 9,
		// Mesh/Task (DX12 PIPELINE_STATISTICS1; Vulkan requires VK_EXT_mesh_shader)
		PS_TaskInvocations = 1ull << 16,
		PS_MeshInvocations = 1ull << 17,
		PS_MeshPrimitives = 1ull << 18, // DX12 has MSPrimitives; Vulkan may not expose primitive count.
		PS_All = 0xFFFFFFFFFFFFFFFFull
	};
	enum class PipelineStatTypes : uint32_t {
		IAVertices,
		IAPrimitives,
		VSInvocations,
		GSInvocations,
		GSPrimitives,
		TSControlInvocations,   // a.k.a. HS
		TSEvaluationInvocations,// a.k.a. DS
		PSInvocations,
		CSInvocations,
		// Mesh/Task (DX12 *_STATISTICS1, Vulkan needs mesh shader extension)
		TaskInvocations,
		MeshInvocations,
		MeshPrimitives,
	};

	using PipelineStatsMask = uint64_t;

	// Query pool creation
	struct QueryPoolDesc {
		QueryType          type{};
		uint32_t           count = 0;         // total slots in the pool
		PipelineStatsMask  statsMask = 0;     // only for PipelineStatistics
		bool               requireAllStats = false; // if true and backend can’t support all bits -> Unsupported
	};

	struct QueryResultInfo {
		QueryType type{};
		uint32_t  count = 0;            // slots in the pool
		uint32_t  elementSize = 0;      // bytes per query result (native layout in the resolve buffer)
		uint32_t  elementAlignment = 8; // conservative; useful if you choose to pad
		// For timestamps/occlusion this is enough.
	};

	struct PipelineStatsFieldDesc {
		PipelineStatTypes field;
		uint32_t          byteOffset;   // offset within one element
		uint32_t          byteSize;     // usually 8 (u64 counters)
		bool              supported;    // false if backend can’t provide it
	};

	struct PipelineStatsLayout {
		QueryResultInfo   info;
		// Dense list; only supported fields included (or include all with supported=false)
		Span<PipelineStatsFieldDesc> fields;
	};

	// Convenience conversion for timestamps (RHI exposes a uniform "ticks per second")
	struct TimestampCalibration {
		uint64_t ticksPerSecond = 0; // DX12: queue->GetTimestampFrequency(); Vulkan: round(1e9 / timestampPeriod)
	};

	enum class PrimitiveTopology : uint32_t { PointList, LineList, LineStrip, TriangleList, TriangleStrip, TriangleFan };

	struct ResourceAllocationInfo { uint64_t offset; uint64_t alignment; uint64_t sizeInBytes; };
	struct HeapProperties { HeapType type; };
	struct MemoryBudget { uint64_t budgetBytes; uint64_t usageBytes; };

	enum ResidencyPriority : uint32_t { // Same as D3D12_RESIDENCY_PRIORITY
		ResidencyPriorityMinimum = 0x28000000,
		ResidencyPriorityLow = 0x50000000,
		ResidencyPriorityNormal = 0x78000000,
		ResidencyPriorityHigh = 0xA0010000,
		ResidencyPriorityMaximum = 0xC8000000
	};

	enum class PageableKind : uint8_t {
		Resource,
		Heap,
		DescriptorHeap,
		QueryPool,   // maps to ID3D12QueryHeap
		Pipeline,    // maps to ID3D12PipelineState
	};

	struct PageableRef {
		PageableRef(const ResourceHandle& r) : kind(PageableKind::Resource), resource(r) {}
		PageableRef(const HeapHandle& h) : kind(PageableKind::Heap), heap(h) {}
		PageableRef(const DescriptorHeapHandle& d) : kind(PageableKind::DescriptorHeap), descHeap(d) {}
		PageableRef(const QueryPoolHandle& q) : kind(PageableKind::QueryPool), queryPool(q) {}
		PageableRef(const PipelineHandle& p) : kind(PageableKind::Pipeline), pipeline(p) {}

		PageableKind kind;
		union {
			ResourceHandle        resource;
			HeapHandle            heap;
			DescriptorHeapHandle  descHeap;
			QueryPoolHandle       queryPool;
			PipelineHandle        pipeline;
		};
	};

	struct ResidencyPriorityItem {
		PageableRef object;
		ResidencyPriority priority;
	};

#define DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT 65536ull // 64KB // DX12 default requirement
#define DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT 4194304ull // 4MB // DX12 requirement for MSAA textures
	// ---------------- POD wrappers + VTables ----------------
	class Device;       class Queue;       class CommandList;        class Swapchain;       class CommandAllocator;
	struct DeviceVTable; struct QueueVTable; struct CommandListVTable; struct SwapchainVTable; struct CommandAllocatorVTable;
	class Resource;      struct ResourceVTable;
	class QueryPool;    struct QueryPoolVTable;
	class Pipeline;     struct PipelineVTable;
	class PipelineLayout; struct PipelineLayoutVTable;
	class CommandSignature; struct CommandSignatureVTable;
	class DescriptorHeap; struct DescriptorHeapVTable;
	class Sampler;     struct SamplerVTable;
	class Timeline;    struct TimelineVTable;
	class Heap;        struct HeapVTable;

	struct QueryPoolVTable {
		QueryResultInfo(*getQueryResultInfo)(QueryPool*) noexcept;
		PipelineStatsLayout(*getPipelineStatsLayout)(QueryPool*, PipelineStatsFieldDesc* outBuf, uint32_t outCap) noexcept;
		void (*setName)(QueryPool*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class QueryPool {
	public:
		QueryPool() = default;
		explicit QueryPool(QueryPoolHandle h) : handle(h) {}
		void* impl{};
		const QueryPoolVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_QUERYPOOL_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		const QueryPoolHandle& GetHandle() const noexcept { return handle; }
		QueryResultInfo GetQueryResultInfo() noexcept { return vt->getQueryResultInfo(this); }
		PipelineStatsLayout GetPipelineStatsLayout(PipelineStatsFieldDesc* outBuf, uint32_t outCap) noexcept {
			return vt->getPipelineStatsLayout(this, outBuf, outCap);
		}
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		QueryPoolHandle handle;
	};

	struct PipelineVTable {
		void (*setName)(Pipeline*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class Pipeline {
	public:
		Pipeline() = default;
		explicit Pipeline(PipelineHandle h) : handle(h) {}
		void* impl{};
		const PipelineVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_PIPELINE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		const PipelineHandle& GetHandle() const noexcept { return handle; }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		PipelineHandle handle;
	};

	struct PipelineLayoutVTable {
		void (*setName)(PipelineLayout*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class PipelineLayout {
	public:
		PipelineLayout() = default;
		explicit PipelineLayout(PipelineLayoutHandle h) : handle(h) {}
		void* impl{};
		const PipelineLayoutVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_PIPELINELAYOUT_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		const PipelineLayoutHandle& GetHandle() const noexcept { return handle; }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		PipelineLayoutHandle handle;
	};

	struct CommandSignatureVTable {
		void (*setName)(CommandSignature*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class CommandSignature {
	public:
		CommandSignature() = default;
		explicit CommandSignature(CommandSignatureHandle h) : handle(h) {}
		void* impl{};
		const CommandSignatureVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_COMMANDSIGNATURE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		const CommandSignatureHandle& GetHandle() const noexcept { return handle; }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		CommandSignatureHandle handle;
	};

	struct DescriptorHeapVTable {
		void (*setName)(DescriptorHeap*, const char*) noexcept;
		uint32_t abi_version = 1;
	};

	class DescriptorHeap {
	public:
		DescriptorHeap() = default;
		explicit DescriptorHeap(DescriptorHeapHandle h) : handle(h) {}
		void* impl{};
		const DescriptorHeapVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_DESCRIPTORHEAP_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		DescriptorHeapHandle GetHandle() const noexcept { return handle; }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		DescriptorHeapHandle handle;
	};

	struct SamplerVTable {
		void (*setName)(Sampler*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class Sampler {
	public:
		Sampler() = default;
		explicit Sampler(SamplerHandle h) : handle(h) {}
		void* impl{};
		const SamplerVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_SAMPLER_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		const SamplerHandle& GetHandle() const noexcept { return handle; }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		SamplerHandle handle;
	};

	struct TimelineVTable {
		uint64_t(*getCompletedValue)(Timeline*) noexcept;
		Result(*hostWait)(Timeline*, const uint64_t) noexcept; // blocks until reached
		void (*setName)(Timeline*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class Timeline {
	public:
		Timeline() = default;
		explicit Timeline(TimelineHandle h) : handle(h) {}
		void* impl{};
		const TimelineVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_TIMELINE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		const TimelineHandle& GetHandle() const noexcept { return handle; }
		uint64_t GetCompletedValue() noexcept { return vt->getCompletedValue(this); }
		Result HostWait(const uint64_t p) noexcept { return vt->hostWait(this, p); }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		TimelineHandle handle;
	};

	struct HeapVTable {
		void (*setName)(Heap*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class Heap {
	public:
		Heap() = default;
		explicit Heap(HeapHandle h) : handle(h) {}
		void* impl{};
		const HeapVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_HEAP_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		const HeapHandle& GetHandle() const noexcept { return handle; }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		HeapHandle handle;
	};

	struct QueueVTable {
		Result(*submit)(Queue*, Span<CommandList>, const SubmitDesc&) noexcept;
		Result(*signal)(Queue*, const TimelinePoint&) noexcept;
		Result(*wait)(Queue*, const TimelinePoint&) noexcept;
		void (*setName)(Queue*, const char*) noexcept;
		uint32_t abi_version = 1;
	};

	class Queue {
	public:
		Queue() = default;
		explicit Queue(QueueKind k) : kind(k) {}
		void* impl{};
		const QueueVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_QUEUE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		Result Submit(Span<CommandList> lists, const SubmitDesc& s = {}) noexcept;
		Result Signal(const TimelinePoint& p) noexcept;
		Result Wait(const TimelinePoint& p) noexcept;
		QueueKind GetKind() const noexcept { return kind; }
	private:
		QueueKind kind;
	};

	struct ResourceVTable {
		void (*map)(Resource*, void** data, uint64_t offset, uint64_t size) noexcept;
		void (*unmap)(Resource*, uint64_t writeOffset, uint64_t writeSize) noexcept;
		void (*setName)(Resource*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class Resource {
	public:
		Resource() = default;
		explicit Resource(ResourceHandle h, bool isTexture = false) : handle(h), isTexture(isTexture) {}
		void* impl{}; // backend wrap (owns Handle32)
		const ResourceVTable* vt{}; // vtable
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_RESOURCE_ABI_MIN;
		}
		bool IsTexture() const noexcept { return isTexture; }
		const ResourceHandle& GetHandle() const noexcept { return handle; }
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; } // Naming
		inline void Map(void** data, uint64_t offset = 0, uint64_t size = ~0ull) noexcept { vt->map(this, data, offset, size); }
		inline void Unmap(uint64_t writeOffset, uint64_t writeSize) noexcept { vt->unmap(this, writeOffset, writeSize); }
		inline void SetName(const char* n) noexcept {
			vt->setName(this, n);
		}
	private:
		ResourceHandle handle;
		bool isTexture = false;
	};

	struct UavClearInfo {
		DescriptorSlot shaderVisible;   // SRV/UAV heap, shader-visible; REQUIRED on DX12
		DescriptorSlot cpuVisible;      // SRV/UAV heap, non shader-visible; REQUIRED on DX12
		Resource resource;        // the resource whose UAV is being cleared. Stores Resource instead of a handle because the backend may need to know if it's a buffer or texture. TODO: better solution?
	};


	struct CopyableFootprint {
		uint64_t offset;      // byte offset in the staging buffer
		uint32_t rowPitch;    // bytes per row (D3D12 is 256B aligned)
		uint32_t width;       // texels in X for the subresource
		uint32_t height;      // texels in Y for the subresource
		uint32_t depth;       // slices for 3D, else 1
	};

	struct FootprintRangeDesc {
		ResourceHandle texture{};
		uint32_t firstMip{ 0 };
		uint32_t mipCount{ 1 };
		uint32_t firstArraySlice{ 0 };
		uint32_t arraySize{ 1 };
		uint32_t firstPlane{ 0 };  // 0 for non-planar
		uint32_t planeCount{ 1 };  // 1 for non-planar
		uint64_t baseOffset{ 0 };  // starting byte offset into the (readback/upload) buffer
	};

	struct CopyableFootprintsInfo {
		uint32_t count{ 0 };     // number of footprints written
		uint64_t totalBytes{ 0 }; // total size from baseOffset that needs to be allocated
	};

	struct BufferTextureCopyFootprint {
		ResourceHandle texture;
		ResourceHandle buffer;

		uint32_t mip;
		uint32_t arraySlice;
		uint32_t x = 0, y = 0, z = 0;   // usually 0 for full-subresource copies

		// Placed footprint coming from Device::GetCopyableFootprints:
		CopyableFootprint footprint;
	};

	enum class MemorySegmentGroup : uint32_t { Local, NonLocal, Count };

	struct VideoMemoryInfo {
		uint64_t budgetBytes = 0;                  // OS budget for this segment group
		uint64_t currentUsageBytes = 0;            // current usage
		uint64_t availableForReservationBytes = 0; // how much can be reserved
		uint64_t currentReservationBytes = 0;      // current reservation level
	};

	struct CommandListVTable {
		void (*end)(CommandList*) noexcept;
		void (*recycle)(CommandList*, const CommandAllocator& alloc) noexcept;
		void (*beginPass)(CommandList*, const PassBeginInfo&) noexcept;
		void (*endPass)(CommandList*) noexcept;
		void (*barriers)(CommandList*, const BarrierBatch&) noexcept;
		void (*bindLayout)(CommandList*, PipelineLayoutHandle) noexcept;
		void (*bindPipeline)(CommandList*, PipelineHandle) noexcept;
		void (*setVertexBuffers)(CommandList*, uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept;
		void (*setIndexBuffer)(CommandList*, const IndexBufferView& v) noexcept;
		void (*draw)(CommandList*, uint32_t vtxCount, uint32_t instCount, uint32_t firstVtx, uint32_t firstInst) noexcept;
		void (*drawIndexed)(CommandList*, uint32_t idxCount, uint32_t instCount, uint32_t firstIdx, int32_t vtxOffset, uint32_t firstInst) noexcept;
		void (*dispatch)(CommandList*, uint32_t x, uint32_t y, uint32_t z) noexcept;
		void (*clearRenderTargetViewBySlot)(CommandList*, DescriptorSlot, const ClearValue&) noexcept;
		void (*clearDepthStencilViewBySlot)(CommandList*, DescriptorSlot,
			bool clearDepth, bool clearStencil,
			float depth, uint8_t stencil) noexcept;
		void (*executeIndirect)(CommandList*,
			CommandSignatureHandle sig,
			ResourceHandle argumentBuffer, uint64_t argumentOffset,
			ResourceHandle countBuffer, uint64_t countOffset,
			uint32_t   maxCommandCount) noexcept;
		void (*setDescriptorHeaps)(CommandList*, DescriptorHeapHandle cbvSrvUav, std::optional<DescriptorHeapHandle> sampler) noexcept;
		void (*clearUavUint)(CommandList*, const UavClearInfo&, const UavClearUint&) noexcept;
		void (*clearUavFloat)(CommandList*, const UavClearInfo&, const UavClearFloat&) noexcept;
		void (*copyTextureToBuffer)(CommandList*, const BufferTextureCopyFootprint&) noexcept;
		void (*copyBufferToTexture)(CommandList*, const BufferTextureCopyFootprint&) noexcept;
		void (*copyTextureRegion)(CommandList*, const TextureCopyRegion&, const TextureCopyRegion&) noexcept;
		void (*copyBufferRegion)(CommandList*, ResourceHandle dst, uint64_t dstOffset, ResourceHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept;
		void (*writeTimestamp)(CommandList*, QueryPoolHandle, uint32_t index, Stage stageHint) noexcept; // Timestamp: writes at 'index' (stage ignored on DX12, used on Vulkan)
		void (*beginQuery)(CommandList*, QueryPoolHandle, uint32_t index) noexcept; // Begin/End for occlusion & pipeline stats (no-op for timestamps)
		void (*endQuery)  (CommandList*, QueryPoolHandle, uint32_t index) noexcept;
		void (*resolveQueryData)(CommandList*, QueryPoolHandle, uint32_t firstQuery, uint32_t queryCount,
			ResourceHandle dstBuffer, uint64_t dstOffsetBytes) noexcept; // Resolve to a buffer; always 64-bit results (matches both APIs)
		void (*resetQueries)(CommandList*, QueryPoolHandle, uint32_t firstQuery, uint32_t queryCount) noexcept; // Vulkan requires resets before reuse; DX12 can no-op this.
		void (*pushConstants)(CommandList*, ShaderStage stages,
			uint32_t set, uint32_t binding,
			uint32_t dstOffset32, uint32_t num32,
			const void* data) noexcept;
		void (*setPrimitiveTopology)(CommandList*, PrimitiveTopology) noexcept;
		void (*dispatchMesh)(CommandList*, uint32_t x, uint32_t y, uint32_t z) noexcept; // if supported by the backend
		void (*setName)(CommandList*, const char*) noexcept;
		uint32_t abi_version = 1;
	};

	class CommandList {
	public:
		CommandList() = default;
		explicit CommandList(CommandListHandle h) : handle(h) {}
		void* impl{};
		const CommandListVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_CL_ABI_MIN;
		}
		const CommandListHandle& GetHandle() const noexcept { return handle; }
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		void End() noexcept;
		void Recycle(const CommandAllocator& ca) noexcept;
		void BeginPass(const PassBeginInfo& p) noexcept;
		void EndPass() noexcept;
		void Barriers(const BarrierBatch& b) noexcept;
		void BindLayout(PipelineLayoutHandle l) noexcept;
		void BindPipeline(PipelineHandle p) noexcept;
		void SetVertexBuffers(uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept;
		void SetIndexBuffer(const IndexBufferView& v) noexcept;
		void Draw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) noexcept;
		void DrawIndexed(uint32_t i, uint32_t inst, uint32_t firstIdx, int32_t vOff, uint32_t firstI) noexcept;
		void Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept;
		inline void ClearRenderTargetView(DescriptorSlot s, const ClearValue& c) noexcept { vt->clearRenderTargetViewBySlot(this, s, c); }
		inline void ClearDepthStencilView(DescriptorSlot s, bool clearDepth, bool clearStencil, float depth, uint8_t stencil) noexcept {
			vt->clearDepthStencilViewBySlot(this, s, clearDepth, clearStencil, depth, stencil);
		}
		void ExecuteIndirect(CommandSignatureHandle sig,
			ResourceHandle argBuf, uint64_t argOff,
			ResourceHandle cntBuf, uint64_t cntOff,
			uint32_t maxCount) noexcept;
		void SetDescriptorHeaps(DescriptorHeapHandle cbvSrvUav, std::optional<DescriptorHeapHandle> sampler) noexcept;
		void ClearUavUint(const UavClearInfo& u, const UavClearUint& v) noexcept;
		void ClearUavFloat(const UavClearInfo& u, const UavClearFloat& v) noexcept;
		void CopyTextureToBuffer(const BufferTextureCopyFootprint& r) noexcept;
		void CopyBufferToTexture(const BufferTextureCopyFootprint& r) noexcept;
		void CopyTextureRegion(const TextureCopyRegion& dst, const TextureCopyRegion& src) noexcept;
		void CopyBufferRegion(ResourceHandle dst, uint64_t dstOffset, ResourceHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept;
		void WriteTimestamp(QueryPoolHandle p, uint32_t idx, Stage s) noexcept;
		void BeginQuery(QueryPoolHandle p, uint32_t idx) noexcept;
		void EndQuery(QueryPoolHandle p, uint32_t idx) noexcept;
		void ResolveQueryData(QueryPoolHandle p, uint32_t first, uint32_t count, ResourceHandle dst, uint64_t off) noexcept;
		void ResetQueries(QueryPoolHandle p, uint32_t first, uint32_t count) noexcept;
		void SetName(const char* n) noexcept;
		void PushConstants(ShaderStage stages,
			uint32_t set, uint32_t binding,
			uint32_t dstOffset32, uint32_t num32,
			const void* data) noexcept;
		void SetPrimitiveTopology(PrimitiveTopology t) noexcept;
		void DispatchMesh(uint32_t x, uint32_t y, uint32_t z) noexcept;

	private:
		CommandListHandle handle;
	};

	struct SwapchainVTable {
		uint32_t(*imageCount)(Swapchain*) noexcept;
		uint32_t(*currentImageIndex)(Swapchain*) noexcept;
		//ViewHandle(*rtv)(Swapchain*, uint32_t img) noexcept; // RTV per image
		ResourceHandle(*image)(Swapchain*, uint32_t img) noexcept; // texture handle per image
		Result(*present)(Swapchain*, bool vsync) noexcept; // Present
		Result(*resizeBuffers)(Swapchain*, uint32_t bufferCount, uint32_t w, uint32_t h, Format newFormat, uint32_t flags) noexcept;
		void (*setName)(Swapchain*, const char*) noexcept;
		uint32_t abi_version = 1;
	};

	class Swapchain {
	public:
		Swapchain() = default;
		explicit Swapchain(SwapChainHandle handle) : handle(handle){}
		void* impl{};
		const SwapchainVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_SC_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void reset() noexcept { impl = nullptr; vt = nullptr; }
		const SwapChainHandle& GetHandle() const noexcept { return handle; }

		inline uint32_t ImageCount() noexcept;
		inline uint32_t CurrentImageIndex() noexcept;
		//inline ViewHandle RTV(uint32_t i) noexcept;
		inline ResourceHandle Image(uint32_t i) noexcept;
		inline Result Present(bool vsync) noexcept;
		inline Result ResizeBuffers(uint32_t bufferCount, uint32_t w, uint32_t h, Format newFmt, uint32_t flags) noexcept {
			return vt->resizeBuffers ? vt->resizeBuffers(this, bufferCount, w, h, newFmt, flags) : Result::Unsupported;
		}
	private:
		SwapChainHandle handle;
	};


	template<class TObject>
	class ObjectPtr {
	public:
		using DestroyFn = void(*)(Device&, TObject&) noexcept;

		ObjectPtr() = default;
		ObjectPtr(Device dev, TObject obj, DestroyFn dfn) noexcept
			: dev_(dev), obj_(obj), destroy_(dfn) {
		}

		~ObjectPtr() { Reset(); }

		ObjectPtr(const ObjectPtr&) = delete;
		ObjectPtr& operator=(const ObjectPtr&) = delete;

		// MOVE CTOR: steal and null out source
		ObjectPtr(ObjectPtr&& o) noexcept
			: dev_(o.dev_), obj_(o.obj_), destroy_(o.destroy_) {
			o.destroy_ = nullptr;
			o.obj_ = {};
		}

		// MOVE ASSIGN: destroy current, steal, null out source
		ObjectPtr& operator=(ObjectPtr&& o) noexcept {
			if (this != &o) {
				Reset();
				dev_ = o.dev_;
				obj_ = o.obj_;
				destroy_ = o.destroy_;
				o.destroy_ = nullptr;
				o.obj_ = {};
			}
			return *this;
		}

		TObject* operator->() noexcept { return &obj_; }
		const TObject* operator->() const noexcept { return &obj_; }
		explicit operator bool() const noexcept { return static_cast<bool>(obj_); }
		TObject& Get() noexcept { return obj_; }
		const TObject& Get() const noexcept { return obj_; }

		void Reset() noexcept {
			if (destroy_ && static_cast<bool>(obj_)) destroy_(dev_, obj_);
			destroy_ = nullptr;
			obj_ = {};
		}

		TObject Release() noexcept {
			destroy_ = nullptr;
			return std::exchange(obj_, {});
		}

	private:
		Device    dev_{};
		TObject   obj_{};
		DestroyFn destroy_{};
	};


	using CommandAllocatorPtr = ObjectPtr<CommandAllocator>;
	using CommandListPtr = ObjectPtr<CommandList>;
	using SwapchainPtr = ObjectPtr<Swapchain>;
	using ResourcePtr = ObjectPtr<Resource>;
	using QueryPoolPtr = ObjectPtr<QueryPool>;
	using PipelinePtr = ObjectPtr<Pipeline>;
	using PipelineLayoutPtr = ObjectPtr<PipelineLayout>;
	using CommandSignaturePtr = ObjectPtr<CommandSignature>;
	using DescriptorHeapPtr = ObjectPtr<DescriptorHeap>;
	using SamplerPtr = ObjectPtr<Sampler>;
	using TimelinePtr = ObjectPtr<Timeline>;
	using HeapPtr = ObjectPtr<Heap>;
	//using DevicePtr = ObjectPtr<Device>;

	struct DeviceDeletionContext;

	struct DeviceVTable {
		Result(*createPipelineFromStream)(Device*, const PipelineStreamItem*, uint32_t, PipelinePtr&) noexcept;
		Result(*createPipelineLayout)(Device*, const PipelineLayoutDesc&, PipelineLayoutPtr&) noexcept;
		Result(*createCommandSignature)(Device*, const CommandSignatureDesc&, PipelineLayoutHandle /*layoutOrNull*/, CommandSignaturePtr&) noexcept;
		Result(*createCommandAllocator)(Device*, QueueKind, CommandAllocatorPtr&) noexcept;
		Result(*createCommandList)(Device*, QueueKind, CommandAllocator, CommandListPtr&) noexcept;
		Result(*createSwapchain)(Device*, void*, uint32_t, uint32_t, Format, uint32_t, bool, SwapchainPtr&) noexcept;
		Result(*createDescriptorHeap)(Device*, const DescriptorHeapDesc&, DescriptorHeapPtr&) noexcept;

		Result(*createConstantBufferView)(Device*, DescriptorSlot, const ResourceHandle&, const CbvDesc&) noexcept;
		Result(*createShaderResourceView)(Device*, DescriptorSlot, const ResourceHandle&, const SrvDesc&) noexcept;
		Result(*createUnorderedAccessView)(Device*, DescriptorSlot, const ResourceHandle&, const UavDesc&) noexcept;
		Result(*createRenderTargetView)(Device*, DescriptorSlot, const ResourceHandle&, const RtvDesc&) noexcept;
		Result(*createDepthStencilView)(Device*, DescriptorSlot, const ResourceHandle&, const DsvDesc&) noexcept;
		Result(*createSampler)(Device*, DescriptorSlot, const SamplerDesc&) noexcept;
		Result(*createCommittedResource)(Device*, const ResourceDesc&, ResourcePtr&) noexcept;
		Result(*createTimeline)(Device*, uint64_t, const char*, TimelinePtr&) noexcept;
		Result(*createHeap)(const Device*, const HeapDesc&, HeapPtr&) noexcept;
		Result(*createPlacedResource)(Device*, HeapHandle, uint64_t, const ResourceDesc&, ResourcePtr&) noexcept;
		Result(*createQueryPool)(Device*, const QueryPoolDesc&, QueryPoolPtr&) noexcept;

		void (*destroySampler)(DeviceDeletionContext*, SamplerHandle) noexcept;
		void (*destroyPipelineLayout)(DeviceDeletionContext*, PipelineLayoutHandle) noexcept;
		void (*destroyPipeline)(DeviceDeletionContext*, PipelineHandle) noexcept;
		void (*destroyCommandSignature)(DeviceDeletionContext*, CommandSignatureHandle) noexcept;
		void (*destroyCommandAllocator)(DeviceDeletionContext*, CommandAllocator*) noexcept;
		void (*destroyCommandList)(DeviceDeletionContext*, CommandList*) noexcept;
		void (*destroySwapchain)(DeviceDeletionContext*, Swapchain*) noexcept;
		void (*destroyDescriptorHeap)(DeviceDeletionContext*, DescriptorHeapHandle) noexcept;
		void (*destroyBuffer)(DeviceDeletionContext*, ResourceHandle) noexcept;
		void (*destroyTexture)(DeviceDeletionContext*, ResourceHandle) noexcept;
		void (*destroyTimeline)(DeviceDeletionContext*, TimelineHandle) noexcept;
		void (*destroyHeap)(DeviceDeletionContext*, HeapHandle) noexcept;
		void (*destroyQueryPool)(DeviceDeletionContext*, QueryPoolHandle) noexcept;

		Queue(*getQueue)(Device*, QueueKind) noexcept;
		Result(*deviceWaitIdle)(Device*) noexcept;
		void (*flushDeletionQueue)(Device*) noexcept;
		uint32_t(*getDescriptorHandleIncrementSize)(Device*, DescriptorHeapType) noexcept;
		TimestampCalibration(*getTimestampCalibration)(Device*, QueueKind) noexcept;
		CopyableFootprintsInfo(*getCopyableFootprints)(Device*, const FootprintRangeDesc&, CopyableFootprint*, uint32_t) noexcept;
		Result(*getResourceAllocationInfo)(const Device*, const ResourceDesc*, uint32_t, ResourceAllocationInfo*) noexcept;
		Result(*queryFeatureInfo)(const Device*, FeatureInfoHeader*) noexcept;
		Result(*setResidencyPriority)(const Device*, Span<PageableRef>, ResidencyPriority) noexcept; // TODO: I don't think this will map to Vulkan.
		Result(*queryVideoMemoryInfo)(const Device*, uint32_t, MemorySegmentGroup, VideoMemoryInfo&) noexcept;

		// Optional debug name setters (can be nullopt)
		void (*setNameBuffer)(Device*, ResourceHandle, const char*) noexcept;
		void (*setNameTexture)(Device*, ResourceHandle, const char*) noexcept;
		void (*setNameSampler)(Device*, SamplerHandle, const char*) noexcept;
		void (*setNamePipelineLayout)(Device*, PipelineLayoutHandle, const char*) noexcept;
		void (*setNamePipeline)(Device*, PipelineHandle, const char*) noexcept;
		void (*setNameCommandSignature)(Device*, CommandSignatureHandle, const char*) noexcept;
		void (*setNameDescriptorHeap)(Device*, DescriptorHeapHandle, const char*) noexcept;
		void (*setNameTimeline)(Device*, TimelineHandle, const char*) noexcept;
		void (*setNameHeap)(Device*, HeapHandle, const char*) noexcept;

		void (*destroyDevice)(Device*) noexcept;
		uint32_t abi_version = 1;
	};


	struct DeviceDeletionContext {
		void* impl{};
		const DeviceVTable* vt{};
		DeviceDeletionContext() = default;
		DeviceDeletionContext(void* i, const DeviceVTable* v) : impl(i), vt(v) {}
		inline void DestroyCommandList(CommandList* cl) noexcept { vt->destroyCommandList(this, cl); }
		inline void DestroySwapchain(Swapchain* sc) noexcept { vt->destroySwapchain(this, sc); }
		inline void DestroyPipelineLayout(PipelineLayoutHandle h) noexcept { vt->destroyPipelineLayout(this, h); }
		inline void DestroyCommandSignature(CommandSignatureHandle h) noexcept { vt->destroyCommandSignature(this, h); }
		inline void DestroyDescriptorHeap(DescriptorHeapHandle h) noexcept { vt->destroyDescriptorHeap(this, h); }
		inline void DestroyCommandAllocator(CommandAllocator* a) noexcept { vt->destroyCommandAllocator(this, a); }
		inline void DestroySampler(SamplerHandle h) noexcept { vt->destroySampler(this, h); }
		inline void DestroyPipeline(PipelineHandle h) noexcept { vt->destroyPipeline(this, h); }
		inline void DestroyBuffer(ResourceHandle h) noexcept { vt->destroyBuffer(this, h); }
		inline void DestroyTexture(ResourceHandle h) noexcept { vt->destroyTexture(this, h); }
		inline void DestroyTimeline(TimelineHandle t) noexcept { vt->destroyTimeline(this, t); }
		inline void DestroyHeap(HeapHandle h) noexcept { vt->destroyHeap(this, h); }
		inline void DestroyQueryPool(QueryPoolHandle h) noexcept { vt->destroyQueryPool(this, h); }
	};

	struct CommandAllocatorVTable {
		void (*reset)(CommandAllocator*) noexcept;  // allocator Reset()
		uint32_t abi_version = 1;
	};

	class CommandAllocator {
	public:
		CommandAllocator() = default;
		explicit CommandAllocator(CommandAllocatorHandle handle) : handle(handle) {}
		void* impl{}; // backend wrap (owns Handle32)
		const CommandAllocatorVTable* vt{}; // vtable
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_CA_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; } // Naming conflict with vt->reset
		inline void Recycle() noexcept { vt->reset(this); } // GPU-side reset (allocator->Reset)
		const CommandAllocatorHandle& GetHandle() const noexcept { return handle; }

	private:
		CommandAllocatorHandle handle;
	};

	class Device {
	public:
		DeviceDeletionContext deletionContext;
		void* impl{};
		const DeviceVTable* vt{};
		Device() = default;
		Device(void* i, const DeviceVTable* v) : impl(i), vt(v), deletionContext(i, v) {}
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_DEVICE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		Result CreatePipeline(const PipelineStreamItem* items, uint32_t count, PipelinePtr& out) noexcept { return vt->createPipelineFromStream(this, items, count, out); }
		Result CreateCommandList(QueueKind q, CommandAllocator alloc, CommandListPtr& out) noexcept { return vt->createCommandList(this, q, alloc, out); }
		void DestroyCommandList(CommandList* cl) noexcept { deletionContext.DestroyCommandList(cl); }
		Queue GetQueue(QueueKind q) noexcept { return vt->getQueue(this, q); }
		Result WaitIdle() noexcept { return vt->deviceWaitIdle(this); }
		void FlushDeletionQueue() noexcept { vt->flushDeletionQueue(this); }
		Result CreateSwapchain(void* hwnd, const uint32_t w, const uint32_t h, const Format fmt, const uint32_t buffers, const bool allowTearing, SwapchainPtr& out) noexcept { return vt->createSwapchain(this, hwnd, w, h, fmt, buffers, allowTearing, out); }
		void DestroySwapchain(Swapchain* sc) noexcept { deletionContext.DestroySwapchain(sc); }
		Result CreatePipelineLayout(const PipelineLayoutDesc& d, PipelineLayoutPtr& out) noexcept { return vt->createPipelineLayout(this, d, out); }
		void DestroyPipelineLayout(PipelineLayoutHandle h) noexcept { deletionContext.DestroyPipelineLayout(h); }
		Result CreateCommandSignature(const CommandSignatureDesc& d, const PipelineLayoutHandle layout, CommandSignaturePtr& out) noexcept { return vt->createCommandSignature(this, d, layout, out); }
		void DestroyCommandSignature(CommandSignatureHandle h) noexcept { deletionContext.DestroyCommandSignature(h); }
		Result CreateDescriptorHeap(const DescriptorHeapDesc& d, DescriptorHeapPtr& out) noexcept { return vt->createDescriptorHeap(this, d, out); }
		void DestroyDescriptorHeap(DescriptorHeapHandle h) noexcept { deletionContext.DestroyDescriptorHeap(h); }
		Result CreateConstantBufferView(DescriptorSlot s, const ResourceHandle& b, const CbvDesc& d) noexcept { return vt->createConstantBufferView(this, s, b, d); }
		Result CreateShaderResourceView(DescriptorSlot s, const ResourceHandle& resource, const SrvDesc& d) noexcept { return vt->createShaderResourceView(this, s, resource, d); }
		Result CreateUnorderedAccessView(DescriptorSlot s, const ResourceHandle& resource, const UavDesc& d) noexcept { return vt->createUnorderedAccessView(this, s, resource, d); }
		Result CreateSampler(DescriptorSlot s, const SamplerDesc& d) noexcept { return vt->createSampler(this, s, d); }
		Result CreateRenderTargetView(DescriptorSlot s, const ResourceHandle& resource, const RtvDesc& d) noexcept { return vt->createRenderTargetView(this, s, resource, d); }
		Result CreateDepthStencilView(DescriptorSlot s, const ResourceHandle& resource, const DsvDesc& d) noexcept { return vt->createDepthStencilView(this, s, resource, d); }
		Result CreateCommandAllocator(const QueueKind q, CommandAllocatorPtr& out) noexcept { return vt->createCommandAllocator(this, q, out); }
		void DestroyCommandAllocator(CommandAllocator* a) noexcept { deletionContext.DestroyCommandAllocator(a); }
		Result CreateCommittedResource(const ResourceDesc& d, ResourcePtr& out) noexcept { return vt->createCommittedResource(this, d, out); }
		void DestroySampler(SamplerHandle h) noexcept { deletionContext.DestroySampler(h); }
		void DestroyPipeline(PipelineHandle h) noexcept { deletionContext.DestroyPipeline(h); }
		void DestroyBuffer(ResourceHandle h) noexcept { deletionContext.DestroyBuffer(h); }
		void DestroyTexture(ResourceHandle h) noexcept { deletionContext.DestroyTexture(h); }
		uint32_t GetDescriptorHandleIncrementSize(DescriptorHeapType t) noexcept { return vt->getDescriptorHandleIncrementSize(this, t); }
		Result CreateTimeline(TimelinePtr& out, uint64_t initial = 0, const char* name = nullptr) noexcept { return vt->createTimeline(this, initial, name, out); }
		void DestroyTimeline(TimelineHandle t) noexcept { deletionContext.DestroyTimeline(t); }
		Result CreateHeap(const HeapDesc& h, HeapPtr& out) const noexcept { return vt->createHeap(this, h, out); }
		void DestroyHeap(HeapHandle h) noexcept { deletionContext.DestroyHeap(h); }
		Result CreatePlacedResource(const HeapHandle heap, const uint64_t offset, const ResourceDesc& rd, ResourcePtr& out) noexcept { return vt->createPlacedResource(this, heap, offset, rd, out); }
		Result CreateQueryPool(const QueryPoolDesc& d, QueryPoolPtr& out) noexcept { return vt->createQueryPool(this, d, out); }
		void DestroyQueryPool(QueryPoolHandle h) noexcept { deletionContext.DestroyQueryPool(h); }
		TimestampCalibration GetTimestampCalibration(QueueKind q) noexcept { return vt->getTimestampCalibration(this, q); }
		CopyableFootprintsInfo GetCopyableFootprints(const FootprintRangeDesc& r, CopyableFootprint* out, uint32_t outCap) noexcept {
			return vt->getCopyableFootprints(this, r, out, outCap);
		}
		void GetResourceAllocationInfo(const ResourceDesc* resourceDescriptions, uint32_t numResourceDescriptions, ResourceAllocationInfo* outAllocationInfo) const noexcept {
			vt->getResourceAllocationInfo(this, resourceDescriptions, numResourceDescriptions, outAllocationInfo);
		}
		Result QueryFeatureInfo(FeatureInfoHeader* chain) const noexcept { return vt->queryFeatureInfo(this, chain); }
		Result SetResidencyPriority(const Span<PageableRef>& resources, ResidencyPriority p) const noexcept { return vt->setResidencyPriority(this, resources, p); }
		Result QueryVideoMemoryInfo(uint32_t nodeIndex, MemorySegmentGroup segmentGroup, VideoMemoryInfo& out) const noexcept {
			return vt->queryVideoMemoryInfo(this, nodeIndex, segmentGroup, out);
		}
		void Destroy() noexcept { vt->destroyDevice(this); impl = nullptr; vt = nullptr; }
	};

	class DevicePtr : public ObjectPtr<Device> {
	public:
		DevicePtr() = default;
		explicit DevicePtr(Device d, DestroyFn fn, std::shared_ptr<void> backendLifetimeHandle) noexcept : ObjectPtr<Device>(d, d, fn), backendLifetimeHandle_(backendLifetimeHandle) {}
	private:
		std::shared_ptr<void> backendLifetimeHandle_; // keep backend alive while device is alive
	};

	inline Result Queue::Submit(Span<CommandList> lists, const SubmitDesc& s) noexcept { return vt->submit(this, lists, s); }
	inline Result Queue::Signal(const TimelinePoint& p) noexcept { return vt->signal(this, p); }
	inline Result Queue::Wait(const TimelinePoint& p) noexcept { return vt->wait(this, p); }

	inline uint32_t Swapchain::ImageCount() noexcept { return vt->imageCount(this); }
	inline uint32_t Swapchain::CurrentImageIndex() noexcept { return vt->currentImageIndex(this); }
	//inline ViewHandle Swapchain::RTV(uint32_t i) noexcept { return vt->rtv(this, i); }
	inline ResourceHandle Swapchain::Image(uint32_t i) noexcept { return vt->image(this, i); }
	inline Result Swapchain::Present(bool vsync) noexcept { return vt->present(this, vsync); }

	inline void CommandList::End() noexcept { vt->end(this); }
	inline void CommandList::Recycle(const CommandAllocator& alloc) noexcept { vt->recycle(this, alloc); }
	inline void CommandList::BeginPass(const PassBeginInfo& p) noexcept { vt->beginPass(this, p); }
	inline void CommandList::EndPass() noexcept { vt->endPass(this); }
	inline void CommandList::Barriers(const BarrierBatch& b) noexcept { vt->barriers(this, b); }
	inline void CommandList::BindLayout(PipelineLayoutHandle l) noexcept { vt->bindLayout(this, l); }
	inline void CommandList::BindPipeline(PipelineHandle p) noexcept { vt->bindPipeline(this, p); }
	inline void CommandList::SetVertexBuffers(uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept { vt->setVertexBuffers(this, startSlot, numViews, pBufferViews); }
	inline void CommandList::SetIndexBuffer(const IndexBufferView& v) noexcept { vt->setIndexBuffer(this, v); }
	inline void CommandList::Draw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) noexcept { vt->draw(this, v, i, fv, fi); }
	inline void CommandList::DrawIndexed(uint32_t i, uint32_t inst, uint32_t firstIdx, int32_t vOff, uint32_t firstI) noexcept { vt->drawIndexed(this, i, inst, firstIdx, vOff, firstI); }
	inline void CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept { vt->dispatch(this, x, y, z); }
	inline void CommandList::ExecuteIndirect(CommandSignatureHandle sig, ResourceHandle argBuf, uint64_t argOff, ResourceHandle cntBuf, uint64_t cntOff, uint32_t maxCount) noexcept {
		vt->executeIndirect(this, sig, argBuf, argOff, cntBuf, cntOff, maxCount);
	}
	inline void CommandList::SetDescriptorHeaps(DescriptorHeapHandle csu, std::optional<DescriptorHeapHandle> samp) noexcept { vt->setDescriptorHeaps(this, csu, samp); }
	inline void CommandList::ClearUavUint(const UavClearInfo& i, const UavClearUint& v) noexcept { vt->clearUavUint(this, i, v); }
	inline void CommandList::CopyTextureToBuffer(const BufferTextureCopyFootprint& r) noexcept {
		vt->copyTextureToBuffer(this, r);
	}
	inline void CommandList::CopyBufferToTexture(const BufferTextureCopyFootprint& r) noexcept {
		vt->copyBufferToTexture(this, r);
	}    inline void CommandList::ClearUavFloat(const UavClearInfo& i, const UavClearFloat& v) noexcept { vt->clearUavFloat(this, i, v); }
	inline void CommandList::CopyTextureRegion(const TextureCopyRegion& dst, const TextureCopyRegion& src) noexcept { vt->copyTextureRegion(this, dst, src); }
	inline void CommandList::CopyBufferRegion(ResourceHandle dst, uint64_t dstOffset, ResourceHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept {
		vt->copyBufferRegion(this, dst, dstOffset, src, srcOffset, numBytes);
	}
	inline void CommandList::WriteTimestamp(QueryPoolHandle p, uint32_t idx, Stage s) noexcept { vt->writeTimestamp(this, p, idx, s); }
	inline void CommandList::BeginQuery(QueryPoolHandle p, uint32_t idx) noexcept { vt->beginQuery(this, p, idx); }
	inline void CommandList::EndQuery(QueryPoolHandle p, uint32_t idx) noexcept { vt->endQuery(this, p, idx); }
	inline void CommandList::ResolveQueryData(QueryPoolHandle p, uint32_t first, uint32_t count, ResourceHandle dst, uint64_t off) noexcept {
		vt->resolveQueryData(this, p, first, count, dst, off);
	}
	inline void CommandList::ResetQueries(QueryPoolHandle p, uint32_t first, uint32_t count) noexcept {
		vt->resetQueries(this, p, first, count);
	}
	inline void CommandList::PushConstants(ShaderStage stages,
		uint32_t set, uint32_t binding,
		uint32_t dstOffset32, uint32_t num32,
		const void* data) noexcept {
		vt->pushConstants(this, stages, set, binding, dstOffset32, num32, data);
	}
	inline void CommandList::SetPrimitiveTopology(PrimitiveTopology t) noexcept { vt->setPrimitiveTopology(this, t); }
	inline void CommandList::SetName(const char* n) noexcept { vt->setName(this, n); }
	inline void CommandList::DispatchMesh(uint32_t x, uint32_t y, uint32_t z) noexcept {
		vt->dispatchMesh(this, x, y, z);
	}

	struct DeviceCreateInfo { Backend backend = Backend::D3D12; uint32_t framesInFlight = 3; bool enableDebug = true; };

	static inline ShaderBinary DXIL(ID3DBlob* blob) {
		return { blob ? blob->GetBufferPointer() : nullptr,
				 blob ? static_cast<uint32_t>(blob->GetBufferSize()) : 0u };
	}

	inline CommandAllocatorPtr MakeCommandAllocatorPtr(const Device* d, CommandAllocator ca) noexcept {
		return CommandAllocatorPtr(
			*d, ca,
			// Destroy
			[](Device& dev, CommandAllocator& p) noexcept { if (dev && p) dev.DestroyCommandAllocator(&p); }
			// TODO: Name hook for allocator
		);
	}

	inline CommandListPtr MakeCommandListPtr(const Device* d, CommandList cl) noexcept {
		return CommandListPtr(
			*d, cl,
			[](Device& dev, CommandList& p) noexcept { if (dev && p) dev.DestroyCommandList(&p); }
		);
	}

	inline SwapchainPtr MakeSwapchainPtr(const Device* d, Swapchain sc) noexcept {
		return SwapchainPtr(
			*d, sc,
			[](Device& dev, Swapchain& p) noexcept { if (dev && p) dev.DestroySwapchain(&p); }
		);
	}

	inline DevicePtr MakeDevicePtr(const Device* d, std::shared_ptr<void> lifetimeHandle) noexcept {
		return DevicePtr(
			*d,
			// DestroyFn for Device ignores the TObject* and calls destroy on the object itself
			[](Device& /*ignored*/, Device& self) noexcept {
				if (self && self.IsValid()) self.Destroy();
			}, lifetimeHandle
			// TODO: Name hook for Device?
				);
	}
	inline ResourcePtr MakeTexturePtr(const Device* d, Resource r) noexcept {
		return ResourcePtr(
			*d, r,
			[](Device& dev, Resource& p) noexcept { if (dev && p) dev.DestroyTexture(p.GetHandle()); }
		);
	}
	inline ResourcePtr MakeBufferPtr(const Device* d, Resource r) noexcept {
		return ResourcePtr(
			*d, r,
			[](Device& dev, Resource& p) noexcept { if (dev && p) dev.DestroyBuffer(p.GetHandle()); }
		);
	}
	inline QueryPoolPtr MakeQueryPoolPtr(const Device* d, QueryPool h) noexcept {
		return QueryPoolPtr(*d, h,
			[](Device& dev, QueryPool& hh) noexcept { if (dev) dev.DestroyQueryPool(hh.GetHandle()); }
		);
	}
	inline PipelinePtr MakePipelinePtr(const Device* d, Pipeline h) noexcept {
		return PipelinePtr(*d, h,
			[](Device& dev, Pipeline& hh) noexcept { if (dev) dev.DestroyPipeline(hh.GetHandle()); }
		);
	}
	inline PipelineLayoutPtr MakePipelineLayoutPtr(const Device* d, PipelineLayout h) noexcept {
		return PipelineLayoutPtr(*d, h,
			[](Device& dev, PipelineLayout& hh) noexcept { if (dev) dev.DestroyPipelineLayout(hh.GetHandle()); }
		);
	}
	inline CommandSignaturePtr MakeCommandSignaturePtr(const Device* d, CommandSignature h) noexcept {
		return CommandSignaturePtr(*d, h,
			[](Device& dev, CommandSignature& hh) noexcept { if (dev) dev.DestroyCommandSignature(hh.GetHandle()); }
		);
	}
	inline DescriptorHeapPtr MakeDescriptorHeapPtr(const Device* d, DescriptorHeap h) noexcept {
		return DescriptorHeapPtr(*d, h,
			[](Device& dev, DescriptorHeap& hh) noexcept { if (dev) dev.DestroyDescriptorHeap(hh.GetHandle()); }
		);
	}
	inline SamplerPtr MakeSamplerPtr(const Device* d, Sampler h) noexcept {
		return SamplerPtr(*d, h,
			[](Device& dev, Sampler& hh) noexcept { if (dev) dev.DestroySampler(hh.GetHandle()); }
		);
	}
	inline TimelinePtr MakeTimelinePtr(const Device* d, Timeline h) noexcept {
		return TimelinePtr(*d, h,
			[](Device& dev, Timeline& hh) noexcept { if (dev) dev.DestroyTimeline(hh.GetHandle()); }
		);
	}
	inline HeapPtr MakeHeapPtr(const Device* d, Heap h) noexcept {
		return HeapPtr(*d, h,
			[](Device& dev, Heap& hh) noexcept { if (dev) dev.DestroyHeap(hh.GetHandle()); }
		);
	}

	inline void name_buffer(Device* d, ResourceHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNameBuffer) d->vt->setNameBuffer(d, h, n);
	}
	inline void name_texture(Device* d, ResourceHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNameTexture) d->vt->setNameTexture(d, h, n);
	}
	inline void name_pipeline(Device* d, PipelineHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNamePipeline) d->vt->setNamePipeline(d, h, n);
	}
	inline void name_layout(Device* d, PipelineLayoutHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNamePipelineLayout) d->vt->setNamePipelineLayout(d, h, n);
	}
	inline void name_cmdsig(Device* d, CommandSignatureHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNameCommandSignature) d->vt->setNameCommandSignature(d, h, n);
	}
	inline void name_heap(Device* d, DescriptorHeapHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNameDescriptorHeap) d->vt->setNameDescriptorHeap(d, h, n);
	}
	inline void name_sampler(Device* d, SamplerHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNameSampler) d->vt->setNameSampler(d, h, n);
	}
	inline void name_timeline(Device* d, TimelineHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNameTimeline) d->vt->setNameTimeline(d, h, n);
	}
	inline void name_heap(Device* d, HeapHandle h, const char* n) noexcept {
		if (d && d->vt && d->vt->setNameHeap) d->vt->setNameHeap(d, h, n);
	}

	rhi::Result CreateD3D12Device(const DeviceCreateInfo& ci, DevicePtr& outPtr, bool enableStreamlineInterposer = false) noexcept; // implemented in rhi_dx12.cpp

}