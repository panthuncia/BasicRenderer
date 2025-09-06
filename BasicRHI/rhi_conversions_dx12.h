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

	inline D3D12_BARRIER_SYNC ToDX(const ResourceSyncState state) {
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

	static inline D3D12_FILL_MODE ToDx(const FillMode f) { return f == FillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID; }
	static inline D3D12_CULL_MODE  ToDx(const CullMode c) {
		switch (c) {
		case CullMode::None:return D3D12_CULL_MODE_NONE;
		case CullMode::Front:return D3D12_CULL_MODE_FRONT;
		default:return D3D12_CULL_MODE_BACK;
		}
	}
	static inline D3D12_COMPARISON_FUNC ToDx(const CompareOp c) {
		using C = D3D12_COMPARISON_FUNC;
		switch (c) {
		case CompareOp::Never:return C::D3D12_COMPARISON_FUNC_NEVER;
		case CompareOp::Less:return C::D3D12_COMPARISON_FUNC_LESS;
		case CompareOp::Equal:return C::D3D12_COMPARISON_FUNC_EQUAL;
		case CompareOp::LessEqual:return C::D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareOp::Greater:return C::D3D12_COMPARISON_FUNC_GREATER;
		case CompareOp::NotEqual:return C::D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareOp::GreaterEqual:return C::D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		default:return C::D3D12_COMPARISON_FUNC_ALWAYS;
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
		case Format::BC6H_Typeless: return DXGI_FORMAT_BC6H_TYPELESS;
		case Format::BC6H_UF16: return DXGI_FORMAT_BC6H_UF16;
		case Format::BC6H_SF16: return DXGI_FORMAT_BC6H_SF16;
		case Format::BC7_Typeless: return DXGI_FORMAT_BC7_TYPELESS;
		case Format::BC7_UNorm: return DXGI_FORMAT_BC7_UNORM;
		case Format::BC7_UNorm_sRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
		default: return DXGI_FORMAT_UNKNOWN;
		}
	}

	static inline D3D12_BLEND ToDx(BlendFactor f) {
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
	static inline D3D12_BLEND_OP ToDx(const BlendOp o) {
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


	static D3D12_HEAP_TYPE ToDx(const Memory m) {
		switch (m) {
		case Memory::Upload:   return D3D12_HEAP_TYPE_UPLOAD;
		case Memory::Readback: return D3D12_HEAP_TYPE_READBACK;
		default:               return D3D12_HEAP_TYPE_DEFAULT;
		}
	}

	static D3D12_RESOURCE_FLAGS ToDX(const ResourceFlags flags) {
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

	static D3D12_DESCRIPTOR_HEAP_TYPE ToDX(const DescriptorHeapType t) {
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

	static D3D12_BARRIER_SUBRESOURCE_RANGE ToDX(const TextureSubresourceRange& r) {
		D3D12_BARRIER_SUBRESOURCE_RANGE o{};
		o.IndexOrFirstMipLevel = r.baseMip;
		o.NumMipLevels = r.mipCount;
		o.FirstArraySlice = r.baseLayer;
		o.NumArraySlices = r.layerCount;
		o.FirstPlane = 0;
		o.NumPlanes = 1;
		return o;
	}

	D3D12_CLEAR_VALUE ToDX(const ClearValue& cv) {
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

	static D3D12_HEAP_FLAGS ToDX(HeapFlags f) {
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
		return out;
	}
	static D3D12_FILTER_TYPE ToDX(Filter f) noexcept {
		return (f == Filter::Linear) ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
	}
	static D3D12_FILTER_REDUCTION_TYPE ToDX(rhi::ReductionMode r, bool compareEnable) noexcept {
		if (compareEnable) return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
		switch (r) {
		case rhi::ReductionMode::Standard: return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
		case rhi::ReductionMode::Min:      return D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
		case rhi::ReductionMode::Max:      return D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
		case rhi::ReductionMode::Comparison: return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
		}
		return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
	}
	static D3D12_TEXTURE_ADDRESS_MODE ToDX(AddressMode m) noexcept {
		switch (m) {
		case AddressMode::Wrap:       return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case AddressMode::Mirror:     return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case AddressMode::Clamp:      return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case AddressMode::Border:     return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		case AddressMode::MirrorOnce: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
		}
		return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	}
	static D3D12_COMPARISON_FUNC ToDX(CompareOp op) noexcept {
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
	static void FillDxBorderColor(const SamplerDesc& sd, float out[4]) noexcept {
		switch (sd.borderPreset) {
		case BorderPreset::TransparentBlack: out[0] = out[1] = out[2] = 0.f; out[3] = 0.f; break;
		case BorderPreset::OpaqueBlack:      out[0] = out[1] = out[2] = 0.f; out[3] = 1.f; break;
		case BorderPreset::OpaqueWhite:      out[0] = out[1] = out[2] = 1.f; out[3] = 1.f; break;
		case BorderPreset::Custom:           out[0] = sd.borderColor[0]; out[1] = sd.borderColor[1];
			out[2] = sd.borderColor[2]; out[3] = sd.borderColor[3]; break;
		}
	}
	static D3D12_FILTER BuildDxFilter(const SamplerDesc& sd) noexcept {
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
}