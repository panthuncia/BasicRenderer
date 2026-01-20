#pragma once

#include <rhi.h>
#include <resource_states.h>
#include <d3d12.h>
#include <cassert>

namespace rhi {

	static inline D3D12_BARRIER_ACCESS ToDX(const ResourceAccessType state) {
		if (state == ResourceAccessType::None) {
			return D3D12_BARRIER_ACCESS_NO_ACCESS;
		}
		D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;
		if (state & ResourceAccessType::IndexBuffer) {
			access |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		}
		if (state & ResourceAccessType::VertexBuffer) {
			access |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		}
		if (state & ResourceAccessType::ConstantBuffer) {
			access |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		}
		if (state & ResourceAccessType::ShaderResource) {
			access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		}
		if (state & ResourceAccessType::RenderTarget) {
			access |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
		}
		if (state & ResourceAccessType::DepthReadWrite) {
			access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
		}
		if (state & ResourceAccessType::DepthRead) {
			access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
		}
		if (state & ResourceAccessType::CopySource) {
			access |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
		}
		if (state & ResourceAccessType::CopyDest) {
			access |= D3D12_BARRIER_ACCESS_COPY_DEST;
		}
		if (state & ResourceAccessType::UnorderedAccess) {
			access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		}
		if (state & ResourceAccessType::IndirectArgument) {
			access |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
		}
		if (state & ResourceAccessType::RaytracingAccelerationStructureRead) {
			access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
		}
		if (state & ResourceAccessType::RaytracingAccelerationStructureWrite) {
			access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
		}
		return access;
	}

	static inline D3D12_BARRIER_LAYOUT ToDX(const ResourceLayout l) {
		switch (l) {
		case ResourceLayout::Undefined: return D3D12_BARRIER_LAYOUT_UNDEFINED;
		case ResourceLayout::Common: return D3D12_BARRIER_LAYOUT_COMMON;
		case ResourceLayout::Present: return D3D12_BARRIER_LAYOUT_PRESENT;
		case ResourceLayout::GenericRead: return D3D12_BARRIER_LAYOUT_GENERIC_READ;
		case ResourceLayout::RenderTarget: return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
		case ResourceLayout::UnorderedAccess: return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		case ResourceLayout::DepthReadWrite: return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		case ResourceLayout::DepthRead: return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
		case ResourceLayout::ShaderResource: return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		case ResourceLayout::CopySource: return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		case ResourceLayout::CopyDest: return D3D12_BARRIER_LAYOUT_COPY_DEST;
		case ResourceLayout::ResolveSource: return D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE;
		case ResourceLayout::ResolveDest: return D3D12_BARRIER_LAYOUT_RESOLVE_DEST;
		case ResourceLayout::ShadingRateSource: return D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE;
		case ResourceLayout::DirectCommon: return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON;
		case ResourceLayout::DirectGenericRead: return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ;
		case ResourceLayout::DirectUnorderedAccess: return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS;
		case ResourceLayout::DirectShaderResource: return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE;
		case ResourceLayout::DirectCopySource: return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE;
		case ResourceLayout::DirectCopyDest: return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST;
		case ResourceLayout::ComputeCommon: return D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON;
		case ResourceLayout::ComputeGenericRead: return D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ;
		case ResourceLayout::ComputeUnorderedAccess: return D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS;
		case ResourceLayout::ComputeShaderResource: return D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE;
		case ResourceLayout::ComputeCopySource: return D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE;
		case ResourceLayout::ComputeCopyDest: return D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST;
		default: return D3D12_BARRIER_LAYOUT_UNDEFINED;
		}
	}

	static inline D3D12_BARRIER_SYNC ToDX(const ResourceSyncState state) {
		switch (state) {
		case ResourceSyncState::None:
			return D3D12_BARRIER_SYNC_NONE;
		case ResourceSyncState::All:
			return D3D12_BARRIER_SYNC_ALL;
		case ResourceSyncState::Draw:
			return D3D12_BARRIER_SYNC_DRAW;
		case ResourceSyncState::IndexInput:
			return D3D12_BARRIER_SYNC_INDEX_INPUT;
		case ResourceSyncState::VertexShading:
			return D3D12_BARRIER_SYNC_VERTEX_SHADING;
		case ResourceSyncState::PixelShading:
			return D3D12_BARRIER_SYNC_PIXEL_SHADING;
		case ResourceSyncState::DepthStencil:
			return D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		case ResourceSyncState::RenderTarget:
			return D3D12_BARRIER_SYNC_RENDER_TARGET;
		case ResourceSyncState::ComputeShading:
			return D3D12_BARRIER_SYNC_COMPUTE_SHADING;
		case ResourceSyncState::Raytracing:
			return D3D12_BARRIER_SYNC_RAYTRACING;
		case ResourceSyncState::Copy:
			return D3D12_BARRIER_SYNC_COPY;
		case ResourceSyncState::Resolve:
			return D3D12_BARRIER_SYNC_RESOLVE;
		case ResourceSyncState::ExecuteIndirect:
			return D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		case ResourceSyncState::Predication:
			return D3D12_BARRIER_SYNC_PREDICATION;
		case ResourceSyncState::AllShading:
			return D3D12_BARRIER_SYNC_ALL_SHADING;
		case ResourceSyncState::NonPixelShading:
			return D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
		case ResourceSyncState::EmitRaytracingAccelerationStructurePostbuildInfo:
			return D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO;
		case ResourceSyncState::ClearUnorderedAccessView:
			return D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
		case ResourceSyncState::VideoDecode:
			return D3D12_BARRIER_SYNC_VIDEO_DECODE;
		case ResourceSyncState::VideoProcess:
			return D3D12_BARRIER_SYNC_VIDEO_PROCESS;
		case ResourceSyncState::VideoEncode:
			return D3D12_BARRIER_SYNC_VIDEO_ENCODE;
		case ResourceSyncState::BuildRaytracingAccelerationStructure:
			return D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
		case ResourceSyncState::CopyRatracingAccelerationStructure:
			return D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
		case ResourceSyncState::SyncSplit:
			return D3D12_BARRIER_SYNC_SPLIT;
		}
		return D3D12_BARRIER_SYNC_ALL;
	}

	static inline D3D12_FILL_MODE ToDX(const FillMode f) { return f == FillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID; }
	static inline D3D12_CULL_MODE  ToDX(const CullMode c) {
		switch (c) {
		case CullMode::None:return D3D12_CULL_MODE_NONE;
		case CullMode::Front:return D3D12_CULL_MODE_FRONT;
		default:return D3D12_CULL_MODE_BACK;
		}
	}
	static inline DXGI_FORMAT ToDxgi(const Format f) {
		switch (f)
		{
		case Format::Unknown: return DXGI_FORMAT_UNKNOWN;
		case Format::R32G32B32A32_Typeless: return DXGI_FORMAT_R32G32B32A32_TYPELESS;
		case Format::R32G32B32A32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case Format::R32G32B32A32_UInt: return DXGI_FORMAT_R32G32B32A32_UINT;
		case Format::R32G32B32A32_SInt: return DXGI_FORMAT_R32G32B32A32_SINT;
		case Format::R32G32B32_Typeless: return DXGI_FORMAT_R32G32B32_TYPELESS;
		case Format::R32G32B32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
		case Format::R32G32B32_UInt: return DXGI_FORMAT_R32G32B32_UINT;
		case Format::R32G32B32_SInt: return DXGI_FORMAT_R32G32B32_SINT;
		case Format::R16G16B16A16_Typeless: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
		case Format::R16G16B16A16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case Format::R16G16B16A16_UNorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
		case Format::R16G16B16A16_UInt: return DXGI_FORMAT_R16G16B16A16_UINT;
		case Format::R16G16B16A16_SNorm: return DXGI_FORMAT_R16G16B16A16_SNORM;
		case Format::R16G16B16A16_SInt: return DXGI_FORMAT_R16G16B16A16_SINT;
		case Format::R32G32_Typeless: return DXGI_FORMAT_R32G32_TYPELESS;
		case Format::R32G32_Float: return DXGI_FORMAT_R32G32_FLOAT;
		case Format::R32G32_UInt: return DXGI_FORMAT_R32G32_UINT;
		case Format::R32G32_SInt: return DXGI_FORMAT_R32G32_SINT;
		case Format::R10G10B10A2_Typeless: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
		case Format::R10G10B10A2_UNorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
		case Format::R10G10B10A2_UInt: return DXGI_FORMAT_R10G10B10A2_UINT;
		case Format::R11G11B10_Float: return DXGI_FORMAT_R11G11B10_FLOAT;
		case Format::R8G8B8A8_Typeless: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case Format::R8G8B8A8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case Format::R8G8B8A8_UNorm_sRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case Format::R8G8B8A8_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
		case Format::R8G8B8A8_SNorm: return DXGI_FORMAT_R8G8B8A8_SNORM;
		case Format::R8G8B8A8_SInt: return DXGI_FORMAT_R8G8B8A8_SINT;
		case Format::R16G16_Typeless: return DXGI_FORMAT_R16G16_TYPELESS;
		case Format::R16G16_Float: return DXGI_FORMAT_R16G16_FLOAT;
		case Format::R16G16_UNorm: return DXGI_FORMAT_R16G16_UNORM;
		case Format::R16G16_UInt: return DXGI_FORMAT_R16G16_UINT;
		case Format::R16G16_SNorm: return DXGI_FORMAT_R16G16_SNORM;
		case Format::R16G16_SInt: return DXGI_FORMAT_R16G16_SINT;
		case Format::R32_Typeless: return DXGI_FORMAT_R32_TYPELESS;
		case Format::D32_Float: return DXGI_FORMAT_D32_FLOAT;
		case Format::R32_Float: return DXGI_FORMAT_R32_FLOAT;
		case Format::R32_UInt: return DXGI_FORMAT_R32_UINT;
		case Format::R32_SInt: return DXGI_FORMAT_R32_SINT;
		case Format::R8G8_Typeless: return DXGI_FORMAT_R8G8_TYPELESS;
		case Format::R8G8_UNorm: return DXGI_FORMAT_R8G8_UNORM;
		case Format::R8G8_UInt: return DXGI_FORMAT_R8G8_UINT;
		case Format::R8G8_SNorm: return DXGI_FORMAT_R8G8_SNORM;
		case Format::R8G8_SInt: return DXGI_FORMAT_R8G8_SINT;
		case Format::R16_Typeless: return DXGI_FORMAT_R16_TYPELESS;
		case Format::R16_Float: return DXGI_FORMAT_R16_FLOAT;
		case Format::R16_UNorm: return DXGI_FORMAT_R16_UNORM;
		case Format::R16_UInt: return DXGI_FORMAT_R16_UINT;
		case Format::R16_SNorm: return DXGI_FORMAT_R16_SNORM;
		case Format::R16_SInt: return DXGI_FORMAT_R16_SINT;
		case Format::R8_Typeless: return DXGI_FORMAT_R8_TYPELESS;
		case Format::R8_UNorm: return DXGI_FORMAT_R8_UNORM;
		case Format::R8_UInt: return DXGI_FORMAT_R8_UINT;
		case Format::R8_SNorm: return DXGI_FORMAT_R8_SNORM;
		case Format::R8_SInt: return DXGI_FORMAT_R8_SINT;
		case Format::BC1_Typeless: return DXGI_FORMAT_BC1_TYPELESS;
		case Format::BC1_UNorm: return DXGI_FORMAT_BC1_UNORM;
		case Format::BC1_UNorm_sRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
		case Format::BC2_Typeless: return DXGI_FORMAT_BC2_TYPELESS;
		case Format::BC2_UNorm: return DXGI_FORMAT_BC2_UNORM;
		case Format::BC2_UNorm_sRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
		case Format::BC3_Typeless: return DXGI_FORMAT_BC3_TYPELESS;
		case Format::BC3_UNorm: return DXGI_FORMAT_BC3_UNORM;
		case Format::BC3_UNorm_sRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
		case Format::BC4_Typeless: return DXGI_FORMAT_BC4_TYPELESS;
		case Format::BC4_UNorm: return DXGI_FORMAT_BC4_UNORM;
		case Format::BC4_SNorm: return DXGI_FORMAT_BC4_SNORM;
		case Format::BC5_Typeless: return DXGI_FORMAT_BC5_TYPELESS;
		case Format::BC5_UNorm: return DXGI_FORMAT_BC5_UNORM;
		case Format::BC5_SNorm: return DXGI_FORMAT_BC5_SNORM;
		case Format::B8G8R8A8_Typeless: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case Format::B8G8R8A8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case Format::B8G8R8A8_UNorm_sRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case Format::BC6H_Typeless: return DXGI_FORMAT_BC6H_TYPELESS;
		case Format::BC6H_UF16: return DXGI_FORMAT_BC6H_UF16;
		case Format::BC6H_SF16: return DXGI_FORMAT_BC6H_SF16;
		case Format::BC7_Typeless: return DXGI_FORMAT_BC7_TYPELESS;
		case Format::BC7_UNorm: return DXGI_FORMAT_BC7_UNORM;
		case Format::BC7_UNorm_sRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
		default: return DXGI_FORMAT_UNKNOWN;
		}
	}

	static inline D3D12_BLEND ToDX(BlendFactor f) {
		using B = D3D12_BLEND;
		switch (f) {
		case BlendFactor::Zero:return B::D3D12_BLEND_ZERO; case BlendFactor::One:return B::D3D12_BLEND_ONE;
		case BlendFactor::SrcColor:return B::D3D12_BLEND_SRC_COLOR; case BlendFactor::InvSrcColor:return B::D3D12_BLEND_INV_SRC_COLOR;
		case BlendFactor::SrcAlpha:return B::D3D12_BLEND_SRC_ALPHA; case BlendFactor::InvSrcAlpha:return B::D3D12_BLEND_INV_SRC_ALPHA;
		case BlendFactor::DstColor:return B::D3D12_BLEND_DEST_COLOR; case BlendFactor::InvDstColor:return B::D3D12_BLEND_INV_DEST_COLOR;
		case BlendFactor::DstAlpha:return B::D3D12_BLEND_DEST_ALPHA; case BlendFactor::InvDstAlpha:return B::D3D12_BLEND_INV_DEST_ALPHA;
		}
		return B::D3D12_BLEND_ONE;
	}
	static inline D3D12_BLEND_OP ToDX(const BlendOp o) {
		switch (o) {
		case BlendOp::Add:
			return D3D12_BLEND_OP::D3D12_BLEND_OP_ADD;
		case BlendOp::Sub:
			return D3D12_BLEND_OP::D3D12_BLEND_OP_SUBTRACT;
		case BlendOp::RevSub:
			return D3D12_BLEND_OP::D3D12_BLEND_OP_REV_SUBTRACT;
		case BlendOp::Min:
			return D3D12_BLEND_OP::D3D12_BLEND_OP_MIN;
		case BlendOp::Max:
			return D3D12_BLEND_OP::D3D12_BLEND_OP_MAX;
		} return D3D12_BLEND_OP::D3D12_BLEND_OP_ADD;
	}


	inline static D3D12_HEAP_TYPE ToDX(const HeapType m) {
		switch (m) {
		case HeapType::DeviceLocal:    return D3D12_HEAP_TYPE_DEFAULT;
		case HeapType::HostVisibleCoherent: return D3D12_HEAP_TYPE_UPLOAD;
		case HeapType::HostVisibleCached: return D3D12_HEAP_TYPE_READBACK;
		default: return D3D12_HEAP_TYPE_CUSTOM;
		}
	}

	inline static D3D12_RESOURCE_FLAGS ToDX(const ResourceFlags flags) {
		D3D12_RESOURCE_FLAGS f = D3D12_RESOURCE_FLAG_NONE;
		if (flags & RF_AllowRenderTarget)
			f |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if (flags & RF_AllowDepthStencil)
			f |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (flags & RF_AllowUnorderedAccess)
			f |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		if (flags & RF_DenyShaderResource)
			f |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		if (flags & RF_AllowCrossAdapter)
			f |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
		if (flags & RF_AllowSimultaneousAccess)
			f |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
		if (flags & RF_RaytracingAccelerationStructure)
			f |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
		return f;
	}

	inline static D3D12_DESCRIPTOR_HEAP_TYPE ToDX(const DescriptorHeapType t) {
		switch (t) {
		case DescriptorHeapType::Sampler: return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		case DescriptorHeapType::CbvSrvUav: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		case DescriptorHeapType::RTV: return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		case DescriptorHeapType::DSV: return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		default:
			assert(false && "Invalid DescriptorType");
			return D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		}
	}

	inline static D3D12_BARRIER_SUBRESOURCE_RANGE ToDX(const TextureSubresourceRange& r) {
		D3D12_BARRIER_SUBRESOURCE_RANGE o{};
		o.IndexOrFirstMipLevel = r.baseMip;
		o.NumMipLevels = r.mipCount;
		o.FirstArraySlice = r.baseLayer;
		o.NumArraySlices = r.layerCount;
		o.FirstPlane = 0;
		o.NumPlanes = 1;
		return o;
	}

	inline static D3D12_CLEAR_VALUE ToDX(const ClearValue& cv) {
		D3D12_CLEAR_VALUE v{};
		v.Format = ToDxgi(cv.format);
		if (cv.type == ClearValueType::Color) {
			for (int i = 0; i < 4; ++i) v.Color[i] = cv.rgba[i];
		}
		else {
			v.DepthStencil.Depth = cv.depthStencil.depth;
			v.DepthStencil.Stencil = cv.depthStencil.stencil;
		}
		return v;
	}

	inline static D3D12_HEAP_FLAGS ToDX(HeapFlags f) {
		D3D12_HEAP_FLAGS out = D3D12_HEAP_FLAG_NONE;
		auto test = [&](HeapFlags b) { return (static_cast<uint32_t>(f) & static_cast<uint32_t>(b)) != 0; };
		if (test(HeapFlags::AllowOnlyBuffers))            out |= D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
		if (test(HeapFlags::AllowOnlyNonRtDsTextures))    out |= D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
		if (test(HeapFlags::AllowOnlyRtDsTextures))       out |= D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
		if (test(HeapFlags::DenyBuffers))                 out |= D3D12_HEAP_FLAG_DENY_BUFFERS;
		if (test(HeapFlags::DenyRtDsTextures))            out |= D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
		if (test(HeapFlags::DenyNonRtDsTextures))         out |= D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;
		if (test(HeapFlags::Shared))                      out |= D3D12_HEAP_FLAG_SHARED;
		if (test(HeapFlags::SharedCrossAdapter))          out |= D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
		if (test(HeapFlags::CreateNotResident))           out |= D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT;
		if (test(HeapFlags::CreateNotZeroed))             out |= D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
		if (test(HeapFlags::AllowAllBuffersAndTextures))  out |= D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
		if (test(HeapFlags::AllowDisplay))                out |= D3D12_HEAP_FLAG_ALLOW_DISPLAY;
		if (test(HeapFlags::HardwareProtected))           out |= D3D12_HEAP_FLAG_HARDWARE_PROTECTED;
		if (test(HeapFlags::AllowWriteWatch))				out |= D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH;
		if (test(HeapFlags::AllowCrossAdapterShaderAtomics))			out |= D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS;
		return out;
	}

	inline static D3D12_FILTER_TYPE ToDX(Filter f) noexcept {
		return (f == Filter::Linear) ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
	}
	inline static D3D12_FILTER_REDUCTION_TYPE ToDX(rhi::ReductionMode r, bool compareEnable) noexcept {
		if (compareEnable) return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
		switch (r) {
		case rhi::ReductionMode::Standard: return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
		case rhi::ReductionMode::Min:      return D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
		case rhi::ReductionMode::Max:      return D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
		case rhi::ReductionMode::Comparison: return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
		}
		return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
	}
	inline static D3D12_TEXTURE_ADDRESS_MODE ToDX(AddressMode m) noexcept {
		switch (m) {
		case AddressMode::Wrap:       return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case AddressMode::Mirror:     return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case AddressMode::Clamp:      return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case AddressMode::Border:     return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		case AddressMode::MirrorOnce: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
		}
		return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	}
	inline static D3D12_COMPARISON_FUNC ToDX(CompareOp op) noexcept {
		switch (op) {
		case CompareOp::Never:        return D3D12_COMPARISON_FUNC_NEVER;
		case CompareOp::Less:         return D3D12_COMPARISON_FUNC_LESS;
		case CompareOp::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
		case CompareOp::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareOp::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
		case CompareOp::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case CompareOp::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
		}
		return D3D12_COMPARISON_FUNC_ALWAYS;
	}
	inline static void FillDxBorderColor(const SamplerDesc& sd, float out[4]) noexcept {
		switch (sd.borderPreset) {
		case BorderPreset::TransparentBlack: out[0] = out[1] = out[2] = 0.f; out[3] = 0.f; break;
		case BorderPreset::OpaqueBlack:      out[0] = out[1] = out[2] = 0.f; out[3] = 1.f; break;
		case BorderPreset::OpaqueWhite:      out[0] = out[1] = out[2] = 1.f; out[3] = 1.f; break;
		case BorderPreset::Custom:           out[0] = sd.borderColor[0]; out[1] = sd.borderColor[1];
			out[2] = sd.borderColor[2]; out[3] = sd.borderColor[3]; break;
		}
	}
	inline static D3D12_FILTER BuildDxFilter(const SamplerDesc& sd) noexcept {
		const auto red = ToDX(sd.reduction, sd.compareEnable);

		// Anisotropy dominates when >1
		if (sd.maxAnisotropy > 1) {
			switch (red) {
			default:
			case D3D12_FILTER_REDUCTION_TYPE_STANDARD:   return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_STANDARD);
			case D3D12_FILTER_REDUCTION_TYPE_COMPARISON: return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_COMPARISON);
			case D3D12_FILTER_REDUCTION_TYPE_MINIMUM:    return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_MINIMUM);
			case D3D12_FILTER_REDUCTION_TYPE_MAXIMUM:    return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_MAXIMUM);
			}
		}

		const auto minT = ToDX(sd.minFilter);
		const auto magT = ToDX(sd.magFilter);
		const auto mipT = (sd.mipFilter == MipFilter::Linear) ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;

		return D3D12_ENCODE_BASIC_FILTER(minT, magT, mipT, red);
	}
	inline static D3D12_PRIMITIVE_TOPOLOGY ToDX(const PrimitiveTopology t) noexcept {
		switch (t) {
		case PrimitiveTopology::PointList:        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		case PrimitiveTopology::LineList:         return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case PrimitiveTopology::LineStrip:        return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case PrimitiveTopology::TriangleList:     return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case PrimitiveTopology::TriangleStrip:    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case PrimitiveTopology::TriangleFan:      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN;
		default: return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		}
	}
	inline void ToDx12InputLayout(
		const rhi::FinalizedInputLayout& il,
		std::vector<D3D12_INPUT_ELEMENT_DESC>& out)
	{
		out.clear();
		out.reserve(il.attributes.size());

		for (const auto& a : il.attributes) {
			D3D12_INPUT_ELEMENT_DESC d{};
			// Semantic name/index are required by DX12. If user didn't provide, you can map from location.
			d.SemanticName = a.semanticName ? a.semanticName : "TEXCOORD";
			d.SemanticIndex = a.semanticIndex;
			d.Format = ToDxgi(a.format);                   // your existing converter
			d.InputSlot = a.binding;
			d.AlignedByteOffset = a.offset;                           // we resolved APPEND_ALIGNED already
			// Binding classification:
			const auto& b = il.bindings[a.binding];
			d.InputSlotClass = (b.rate == rhi::InputRate::PerInstance)
				? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
				: D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			d.InstanceDataStepRate = (b.rate == rhi::InputRate::PerInstance) ? b.instanceStepRate : 0;
			out.push_back(d);
		}
	}
	inline static D3D12_COMMAND_LIST_TYPE ToDX(QueueKind q) {
		return q == QueueKind::Graphics ? D3D12_COMMAND_LIST_TYPE_DIRECT
			: q == QueueKind::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE
			: D3D12_COMMAND_LIST_TYPE_COPY;
	}

	inline static D3D12_RESOURCE_DESC1 ToDX(const ResourceDesc& desc) {
		switch (desc.type) {
		case ResourceType::Buffer: {
			D3D12_RESOURCE_DESC1 d{};
			d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			d.Alignment = 0;
			d.Width = desc.buffer.sizeBytes;
			d.Height = 1;
			d.DepthOrArraySize = 1;
			d.MipLevels = 1;
			d.Format = DXGI_FORMAT_UNKNOWN;
			d.SampleDesc.Count = 1;
			d.SampleDesc.Quality = 0;
			d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			d.Flags = ToDX(desc.resourceFlags);
			return d;
		} break;
		default: {
			D3D12_RESOURCE_DESC1 d{};
			d.Dimension = desc.type == ResourceType::Texture1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
				: desc.type == ResourceType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
				: D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			d.Alignment = 0;
			d.Width = desc.texture.width;
			d.Height = desc.texture.height;
			d.DepthOrArraySize = desc.texture.depthOrLayers;
			d.MipLevels = desc.texture.mipLevels;
			d.Format = ToDxgi(desc.texture.format);
			d.SampleDesc.Count = desc.texture.sampleCount;
			d.SampleDesc.Quality = 0;
			d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			d.Flags = ToDX(desc.resourceFlags);
			return d;
		} break;
		}
	}
	inline static ShaderModel ToRHI(D3D_SHADER_MODEL model)
	{
		switch (model)
		{
			case D3D_SHADER_MODEL_6_9: return ShaderModel::SM_6_9;
			case D3D_SHADER_MODEL_6_8: return ShaderModel::SM_6_8;
			case D3D_SHADER_MODEL_6_7: return ShaderModel::SM_6_7;
			case D3D_SHADER_MODEL_6_6: return ShaderModel::SM_6_6;
			case D3D_SHADER_MODEL_6_5: return ShaderModel::SM_6_5;
			case D3D_SHADER_MODEL_6_4: return ShaderModel::SM_6_4;
			case D3D_SHADER_MODEL_6_3: return ShaderModel::SM_6_3;
			case D3D_SHADER_MODEL_6_2: return ShaderModel::SM_6_2;
			case D3D_SHADER_MODEL_6_1: return ShaderModel::SM_6_1;
			case D3D_SHADER_MODEL_6_0: return ShaderModel::SM_6_0;
			default: return ShaderModel::Unknown;
		}
	}
	inline static Result ToRHI(HRESULT hr)
	{
		if (hr == S_OK)
			return Result::Ok;

		switch (hr)
		{
		// -----------------------------------------------------------------
		// Success-with-info (mostly Present-related)
		// -----------------------------------------------------------------
		case DXGI_STATUS_OCCLUDED:                      return Result::PresentOccluded;
		case DXGI_STATUS_CLIPPED:                       return Result::PresentClipped;
		case DXGI_STATUS_UNOCCLUDED:                    return Result::PresentUnoccluded;
		case DXGI_STATUS_PRESENT_REQUIRED:              return Result::PresentRequired;
		case DXGI_STATUS_NO_REDIRECTION:                return Result::NoRedirection;
		case DXGI_STATUS_NO_DESKTOP_ACCESS:             return Result::NoDesktopAccess;
		case DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE:  return Result::VidPnSourceInUse;
		case DXGI_STATUS_MODE_CHANGED:                  return Result::ModeChanged;
		case DXGI_STATUS_MODE_CHANGE_IN_PROGRESS:       return Result::ModeChangeInProgress;
		case DXGI_STATUS_DDA_WAS_STILL_DRAWING:         return Result::DdaWasStillDrawing;

		// -----------------------------------------------------------------
		// Generic HRESULTs
		// -----------------------------------------------------------------
		case E_FAIL:            return Result::Failed;
		case E_UNEXPECTED:      return Result::Unexpected;
		case E_ABORT:           return Result::Aborted;
		case E_ACCESSDENIED:    return Result::AccessDenied;
		case E_INVALIDARG:      return Result::InvalidArgument;
		case E_POINTER:         return Result::InvalidNativePointer;
		case E_NOINTERFACE:     return Result::NoInterface;
		case E_NOTIMPL:         return Result::NotImplemented;
		case E_OUTOFMEMORY:     return Result::OutOfMemory;
		case E_HANDLE:          return Result::InvalidNativeHandle;

		// -----------------------------------------------------------------
		// DXGI / D3D12 / Presentation errors
		// -----------------------------------------------------------------
		case DXGI_ERROR_INVALID_CALL:                   return Result::InvalidCall;
		case DXGI_ERROR_UNSUPPORTED:                    return Result::Unsupported;
		case DXGI_ERROR_SDK_COMPONENT_MISSING:          return Result::SdkComponentMissing;
		case DXGI_ERROR_DYNAMIC_CODE_POLICY_VIOLATION:  return Result::DynamicCodePolicyViolation;

		case DXGI_ERROR_NOT_FOUND:                      return Result::NotFound;
		case DXGI_ERROR_MORE_DATA:                      return Result::MoreData;
		case DXGI_ERROR_ALREADY_EXISTS:                 return Result::AlreadyExists;
		case DXGI_ERROR_NAME_ALREADY_EXISTS:            return Result::NameAlreadyExists;

		case DXGI_ERROR_DEVICE_REMOVED:                 return Result::DeviceRemoved;
		case DXGI_ERROR_DEVICE_HUNG:                    return Result::DeviceHung;
		case DXGI_ERROR_DEVICE_RESET:                   return Result::DeviceReset;
		case DXGI_ERROR_DRIVER_INTERNAL_ERROR:          return Result::DriverInternalError;

		case DXGI_ERROR_WAS_STILL_DRAWING:              return Result::StillDrawing;
		case DXGI_ERROR_WAIT_TIMEOUT:                   return Result::WaitTimeout;

		case DXGI_ERROR_NOT_CURRENT:                    return Result::NotCurrent;
		case DXGI_ERROR_MODE_CHANGE_IN_PROGRESS:        return Result::ModeChangeBlocked;
		case DXGI_ERROR_SESSION_DISCONNECTED:           return Result::SessionDisconnected;
		case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:     return Result::RemoteClientDisconnected;
		case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:       return Result::RestrictToOutputStale;
		case DXGI_ERROR_NON_COMPOSITED_UI:              return Result::NonCompositedUi;
		case DXGI_ERROR_SETDISPLAYMODE_REQUIRED:        return Result::SetDisplayModeRequired;
		case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:      return Result::FrameStatisticsDisjoint;

		case DXGI_ERROR_ACCESS_LOST:                    return Result::AccessLost;
		case DXGI_ERROR_NONEXCLUSIVE:                   return Result::NonExclusive;

		case DXGI_ERROR_CANNOT_PROTECT_CONTENT:         return Result::CannotProtectContent;
		case DXGI_ERROR_HW_PROTECTION_OUTOFMEMORY:      return Result::HwProtectionOutOfMemory;

		case DXGI_ERROR_CACHE_CORRUPT:                  return Result::CacheCorrupt;
		case DXGI_ERROR_CACHE_FULL:                     return Result::CacheFull;
		case DXGI_ERROR_CACHE_HASH_COLLISION:           return Result::CacheHashCollision;

		case D3D12_ERROR_ADAPTER_NOT_FOUND:             return Result::AdapterNotFound;
		case D3D12_ERROR_DRIVER_VERSION_MISMATCH:       return Result::DriverVersionMismatch;
		case D3D12_ERROR_INVALID_REDIST:                return Result::InvalidRedistributable;

		case DXGI_ERROR_MPO_UNPINNED:                   return Result::MpoUnpinned;
		case DXGI_ERROR_REMOTE_OUTOFMEMORY:             return Result::RemoteOutOfMemory;

		case PRESENTATION_ERROR_LOST:                   return Result::PresentationLost;

		// DXGI has a specific AccessDenied too (shared resources etc.)
		case DXGI_ERROR_ACCESS_DENIED:                  return Result::AccessDenied;

		// -----------------------------------------------------------------
		// Non-DXGI Win32-derived HRESULTs (shader/file IO, waits, etc.)
		// -----------------------------------------------------------------
		//case HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE):  return Result::InvalidNativeHandle;

		case HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND):  return Result::FileNotFound;
		case HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND):  return Result::PathNotFound;
		case HRESULT_FROM_WIN32(ERROR_INVALID_DATA):    return Result::InvalidData;
		case HRESULT_FROM_WIN32(ERROR_DISK_FULL):       return Result::DiskFull;
		case HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION): return Result::SharingViolation;

		// Win32 waits can also be surfaced as HRESULTs in some layers.
		case HRESULT_FROM_WIN32(WAIT_TIMEOUT):          return Result::WaitTimeout;

		default:
			// If it's a success code we didn't explicitly model, treat as Ok.
			// Otherwise use the caller's generic "unmapped HRESULT" bucket.
			return SUCCEEDED(hr) ? Result::Ok : Result::Unknown;
		}
	}

	inline static D3D12_RESIDENCY_PRIORITY ToDX(ResidencyPriority p) {
		switch (p) {
		case ResidencyPriority::ResidencyPriorityMinimum:      return D3D12_RESIDENCY_PRIORITY_MINIMUM;
		case ResidencyPriority::ResidencyPriorityLow:          return D3D12_RESIDENCY_PRIORITY_LOW;
		case ResidencyPriority::ResidencyPriorityNormal:       return D3D12_RESIDENCY_PRIORITY_NORMAL;
		case ResidencyPriority::ResidencyPriorityHigh:         return D3D12_RESIDENCY_PRIORITY_HIGH;
		case ResidencyPriority::ResidencyPriorityMaximum:      return D3D12_RESIDENCY_PRIORITY_MAXIMUM;
		default: return D3D12_RESIDENCY_PRIORITY_NORMAL;
		}
	}

	inline static D3D12_WORK_GRAPH_FLAGS ToDX(WorkGraphFlags f) noexcept {
		D3D12_WORK_GRAPH_FLAGS out = D3D12_WORK_GRAPH_FLAG_NONE;
		if ((f & WorkGraphFlags::IncludeAllAvailableNodes) != WorkGraphFlags::None) {
			out |= D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES;
		}
		if ((f & WorkGraphFlags::EntrypointGraphicsNodesRasterizeInOrder) != WorkGraphFlags::None) {
			// Not yet supported in d3d12.h
			//out |= D3D12_WORK_GRAPH_FLAG_ENTRYPOINT_GRAPHICS_NODES_RASTERIZE_IN_ORDER;
		}
		return out;
	}

	inline static D3D12_NODE_OVERRIDES_TYPE ToDX(NodeOverridesType t) noexcept {
		switch (t) {
		case NodeOverridesType::None: return D3D12_NODE_OVERRIDES_TYPE_NONE;
		case NodeOverridesType::BroadcastingLaunch: return D3D12_NODE_OVERRIDES_TYPE_BROADCASTING_LAUNCH;
		case NodeOverridesType::CoalescingLaunch: return D3D12_NODE_OVERRIDES_TYPE_COALESCING_LAUNCH;
		case NodeOverridesType::ThreadLaunch: return D3D12_NODE_OVERRIDES_TYPE_THREAD_LAUNCH;
		case NodeOverridesType::CommonCompute: return D3D12_NODE_OVERRIDES_TYPE_COMMON_COMPUTE;
		default: return D3D12_NODE_OVERRIDES_TYPE_NONE;
		}
	}

	inline static D3D12_DISPATCH_MODE ToDX(WorkGraphDispatchMode m) noexcept {
		switch (m) {
		case WorkGraphDispatchMode::NodeCpuInput: return D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
		case WorkGraphDispatchMode::NodeGpuInput: return D3D12_DISPATCH_MODE_NODE_GPU_INPUT;
		case WorkGraphDispatchMode::MultiNodeCpuInput: return D3D12_DISPATCH_MODE_MULTI_NODE_CPU_INPUT;
		case WorkGraphDispatchMode::MultiNodeGpuInput: return D3D12_DISPATCH_MODE_MULTI_NODE_GPU_INPUT;
		default: return D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
		}
	}

	inline static D3D12_NODE_CPU_INPUT ToDX(const WorkGraphNodeCpuInput& input) noexcept {
		D3D12_NODE_CPU_INPUT d{};
		d.EntrypointIndex = input.entryPointIndex;
		d.NumRecords = input.numRecords;
		d.pRecords = input.pRecords;
		d.RecordStrideInBytes = input.recordByteStride;
		return d;
	}

	inline static D3D12_MULTI_NODE_CPU_INPUT ToDX(const WorkGraphMultiNodeCpuInput& input, std::vector<D3D12_NODE_CPU_INPUT>& outNodeInputs) noexcept {
		D3D12_MULTI_NODE_CPU_INPUT d{};
		d.NodeInputStrideInBytes = input.nodeInputByteStride;
		d.NumNodeInputs = input.numNodeInputs;
		for (uint32_t i = 0; i < input.numNodeInputs; ++i) {
			outNodeInputs.push_back(ToDX(input.nodeInputs[i]));
		}
		return d;
	}
}