#include "rhi.h"

#ifdef _WIN32
#include <directx/d3dx12.h>
#include <Windows.h>
#include <string>
#include <dxgi1_6.h>
#include <wrl.h>
#include <vector>
#include <cassert>
#include <spdlog/spdlog.h>

#include "rhi_interop_dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) if(p){ (p)->Release(); (p)=nullptr; }
#endif

std::wstring s2ws(const std::string & s) {
	int buffSize = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring ws(buffSize, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), buffSize);
	return ws;
}

namespace rhi {
	using Microsoft::WRL::ComPtr;

	struct Dx12Buffer { ComPtr<ID3D12Resource> res;};
	struct Dx12Texture { 
		ComPtr<ID3D12Resource> res; 
		DXGI_FORMAT fmt{ DXGI_FORMAT_UNKNOWN }; 
		uint32_t w = 0, h = 0; 
		uint16_t mips = 1;
		uint16_t arraySize = 1; // for 1D/2D/cube (cube arrays should already multiply by 6)
		D3D12_RESOURCE_DIMENSION dim = D3D12_RESOURCE_DIMENSION_UNKNOWN;
		uint16_t depth = 1;
	};
	struct Dx12View { ViewDesc desc; D3D12_CPU_DESCRIPTOR_HANDLE cpu{}; };
	struct Dx12Sampler { SamplerDesc desc; };
	struct Dx12Pipeline { Microsoft::WRL::ComPtr<ID3D12PipelineState> pso; bool isCompute; };
	struct Dx12PipelineLayout {
		std::vector<PushConstantRangeDesc> pcs;
		std::vector<StaticSamplerDesc> staticSamplers;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
	};

	struct Dx12CommandSignature {
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> sig;
		uint32_t stride = 0;
	};

	struct Dx12Allocator {
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc; 
		D3D12_COMMAND_LIST_TYPE type{}; 
	};
	struct Dx12Device;
	struct Dx12CommandList { 
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> cl; 
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
		D3D12_COMMAND_LIST_TYPE type{}; 
		Dx12Device* dev; 
	};

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
		if (flags & AllowRenderTarget)
			f |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if (flags & AllowDepthStencil)
			f |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (flags & AllowUnorderedAccess)
			f |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		if (flags & DenyShaderResource)
			f |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		if (flags & AllowCrossAdapter)
			f |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
		if (flags & AllowSimultaneousAccess)
			f |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
		if (flags & RaytracingAccelerationStructure)
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

	static D3D12_BARRIER_SUBRESOURCE_RANGE ToDX(const rhi::TextureSubresourceRange& r) {
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

	// Build D3D12_RESOURCE_DESC1 for buffers
	static D3D12_RESOURCE_DESC1 MakeBufferDesc1(uint64_t bytes, D3D12_RESOURCE_FLAGS flags) {
		D3D12_RESOURCE_DESC1 d{};
		d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		d.Alignment = 0;
		d.Width = bytes;
		d.Height = 1;
		d.DepthOrArraySize = 1;
		d.MipLevels = 1;
		d.Format = DXGI_FORMAT_UNKNOWN;
		d.SampleDesc = { 1, 0 };
		d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		d.Flags = flags;
		d.SamplerFeedbackMipRegion = {};
		return d;
	}

	// Build D3D12_RESOURCE_DESC1 for textures
	static D3D12_RESOURCE_DESC1 MakeTexDesc1(const ResourceDesc& td) {
		D3D12_RESOURCE_DESC1 d{};
		d.Alignment = 0;
		d.MipLevels = td.texture.mipLevels;
		d.Format = ToDxgi(td.texture.format);
		d.SampleDesc = { td.texture.sampleCount, 0 };
		d.Flags = ToDX(td.flags);
		d.SamplerFeedbackMipRegion = {};

		switch (td.type) {
		case ResourceType::Texture3D:
			d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			d.Width = td.texture.width;
			d.Height = td.texture.height;
			d.DepthOrArraySize = td.texture.depthOrLayers;
			d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			break;
		case ResourceType::Texture2D:
			d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			d.Width = td.texture.width;
			d.Height = td.texture.height;
			d.DepthOrArraySize = td.texture.depthOrLayers; // for Cube/CubeArray pass N*6 here
			d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			break;
		case ResourceType::Texture1D:
			d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
			d.Width = td.texture.width;
			d.Height = 1;
			d.DepthOrArraySize = td.texture.depthOrLayers;
			d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			break;
		}
		return d;
	}

	// tiny handle registry
	
	template<class Obj> struct HandleFor;  // no default

	template<> struct HandleFor<Dx12Buffer> { using type = rhi::ResourceHandle; };
	template<> struct HandleFor<Dx12Texture> { using type = rhi::ResourceHandle; };
	template<> struct HandleFor<Dx12View> { using type = rhi::ViewHandle; };
	template<> struct HandleFor<Dx12Sampler> { using type = rhi::SamplerHandle; };
	template<> struct HandleFor<Dx12PipelineLayout> { using type = rhi::PipelineLayoutHandle; };
	template<> struct HandleFor<Dx12Pipeline> { using type = rhi::PipelineHandle; };
	template<> struct HandleFor<Dx12CommandSignature> { using type = rhi::CommandSignatureHandle; };
	template<> struct HandleFor<Dx12DescHeap> { using type = rhi::DescriptorHeapHandle; };
	template<> struct HandleFor<Dx12Timeline> { using type = rhi::TimelineHandle; };
	template<> struct HandleFor<Dx12Allocator> { using type = rhi::CommandAllocatorHandle; };
	template<> struct HandleFor<Dx12CommandList> { using type = rhi::CommandListHandle; };

	template<typename T>
	struct Slot { T obj{}; uint32_t generation{ 1 }; bool alive{ false }; };

	// Generic registry, automatically picks the correct handle for T via HandleFor<T>.
	template<typename T>
	struct Registry {
		using HandleT = typename HandleFor<T>::type;

		std::vector<Slot<T>> slots;
		std::vector<uint32_t> freelist;

		HandleT alloc(const T& v) {
			if (!freelist.empty()) {
				const uint32_t i = freelist.back(); freelist.pop_back();
				auto& s = slots[i]; s.obj = v; s.alive = true; ++s.generation;
				return HandleT{ i, s.generation };
			}
			const uint32_t i = (uint32_t)slots.size();
			slots.push_back({ v, 1u, true });
			return HandleT{ i, 1u };
		}

		void free(HandleT h) {
			uint32_t i = h.index;
			if (i >= slots.size()) return;
			auto& s = slots[i];
			if (!s.alive || s.generation != h.generation) return;
			s.alive = false; freelist.push_back(i);
		}

		T* get(HandleT h) {
			uint32_t i = h.index;
			if (i >= slots.size()) return nullptr;
			auto& s = slots[i];
			if (!s.alive || s.generation != h.generation) return nullptr;
			return &s.obj;
		}
	};

	struct Dx12Timeline { Microsoft::WRL::ComPtr<ID3D12Fence> fence; };

	struct Dx12QueueState {
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> q;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence; UINT64 value = 0;
		Dx12Device* owner{};
	};

	struct Dx12Swapchain {
		ComPtr<IDXGISwapChain3> sc; DXGI_FORMAT fmt{}; UINT w{}, h{}, count{}; UINT current{};
		ComPtr<ID3D12DescriptorHeap> rtvHeap; UINT rtvInc{}; std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvCPU;
		std::vector<ComPtr<ID3D12Resource>> images;
		std::vector<ResourceHandle> imageHandles; std::vector<ViewHandle> rtvHandles;
	};

	struct Dx12Heap { Microsoft::WRL::ComPtr<ID3D12Heap> heap; uint64_t size{}; };

	struct Dx12QueryPool {
		Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
		D3D12_QUERY_HEAP_TYPE type{};
		uint32_t count = 0;

		// For pipeline stats, remember if we used *_STATISTICS1 (mesh/task) or legacy
		bool usePSO1 = false;
	};


	struct Dx12Device {
		Device self{};
		ComPtr<IDXGIFactory7> factory; 
		ComPtr<ID3D12Device10> dev;
		ComPtr<IDXGIAdapter4>  adapter;

		rhi::dx12::PFN_UpgradeInterface upgradeFn = nullptr;
		Microsoft::WRL::ComPtr<IDXGIFactory7>  slFactory;   // upgraded proxy
		Microsoft::WRL::ComPtr<ID3D12Device>   slDeviceBase;// upgraded base iface

		Registry<Dx12Buffer> buffers;
		Registry<Dx12Texture> textures;
		Registry<Dx12View> views;
		Registry<Dx12Sampler> samplers;
		Registry<Dx12PipelineLayout> pipelineLayouts;
		Registry<Dx12Pipeline> pipelines;
		Registry<Dx12CommandSignature> commandSignatures;
		Registry<Dx12DescHeap> descHeaps;
		Registry<Dx12Allocator> allocators;
		Registry<Dx12CommandList> commandLists;
		Registry<Dx12Timeline> timelines;
		Registry<Dx12Heap> heaps;
		Registry<Dx12QueryPool> queryPools;

		Dx12QueueState gfx{}, comp{}, copy{};
	};

	struct Dx12DescHeap {
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		D3D12_DESCRIPTOR_HEAP_TYPE type{};
		UINT inc{ 0 };
		bool shaderVisible{ false };
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
	};

	// ---------------- VTables forward ----------------
	extern const DeviceVTable g_devvt; 
	extern const QueueVTable g_qvt; 
	extern const CommandListVTable g_clvt; 
	extern const SwapchainVTable g_scvt; 
	extern const CommandAllocatorVTable g_calvt;
	extern const ResourceVTable g_buf_rvt;
	extern const ResourceVTable g_tex_rvt;

	// ---------------- Helpers ----------------
	static void EnableDebug(ID3D12Device* device) {
#ifdef _DEBUG
		ComPtr<ID3D12InfoQueue> iq; if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&iq)))) {
			D3D12_MESSAGE_ID blocked[] = { (D3D12_MESSAGE_ID)1356, (D3D12_MESSAGE_ID)1328, (D3D12_MESSAGE_ID)1008 };
			D3D12_INFO_QUEUE_FILTER f{}; f.DenyList.NumIDs = (UINT)_countof(blocked); f.DenyList.pIDList = blocked; iq->AddStorageFilterEntries(&f);
		}
#endif
	}

	// ---------------- Device vtable funcs ----------------

	static uint8_t getWriteMask(rhi::ColorWriteEnable e) {
		return static_cast<uint8_t>(e);
	}

	static PipelinePtr d_createPipelineFromStream(Device* d,
		const PipelineStreamItem* items,
		uint32_t count) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);

		// Collect RHI subobjects
		ID3D12RootSignature* root = nullptr;
		D3D12_SHADER_BYTECODE cs{}, vs{}, ps{}, as{}, ms{};
		bool hasCS = false, hasGfx = false;

		CD3DX12_RASTERIZER_DESC rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		CD3DX12_BLEND_DESC      blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		CD3DX12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		D3D12_RT_FORMAT_ARRAY rtv{}; rtv.NumRenderTargets = 0;
		DXGI_FORMAT dsv = DXGI_FORMAT_UNKNOWN;
		DXGI_SAMPLE_DESC sample{ 1,0 };

		for (uint32_t i = 0; i < count; i++) {
			switch (items[i].type) {
			case PsoSubobj::Layout: {
				auto& L = *static_cast<const SubobjLayout*>(items[i].data);
				auto* pl = impl->pipelineLayouts.get(L.layout);
				if (!pl || !pl->root) return {};
				root = pl->root.Get();
			} break;
			case PsoSubobj::Shader: {
				auto& S = *static_cast<const SubobjShader*>(items[i].data);
				D3D12_SHADER_BYTECODE bc{ S.bytecode.data, S.bytecode.size };
				switch (S.stage) {
				case ShaderStage::Compute: cs = bc; hasCS = true; break;
				case ShaderStage::Vertex: vs = bc; hasGfx = true; break;
				case ShaderStage::Pixel: ps = bc; hasGfx = true; break;
				case ShaderStage::Task: as = bc; hasGfx = true; break;
				case ShaderStage::Mesh: ms = bc; hasGfx = true; break;
				default: break;
				}
			} break;
			case PsoSubobj::Rasterizer: {
				auto& R = *static_cast<const SubobjRaster*>(items[i].data);
				rast.FillMode = ToDx(R.rs.fill);
				rast.CullMode = ToDx(R.rs.cull);
				rast.FrontCounterClockwise = R.rs.frontCCW;
				rast.DepthBias = R.rs.depthBias;
				rast.DepthBiasClamp = R.rs.depthBiasClamp;
				rast.SlopeScaledDepthBias = R.rs.slopeScaledDepthBias;
			} break;
			case PsoSubobj::Blend: {
				auto& B = *static_cast<const SubobjBlend*>(items[i].data);
				blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
				blend.AlphaToCoverageEnable = B.bs.alphaToCoverage;
				blend.IndependentBlendEnable = B.bs.independentBlend;
				for (uint32_t a = 0; a < B.bs.numAttachments && a < 8; a++) {
					const auto& src = B.bs.attachments[a];
					auto& dst = blend.RenderTarget[a];
					dst.BlendEnable = src.enable;
					dst.RenderTargetWriteMask = src.writeMask;
					dst.BlendOp = ToDx(src.colorOp);
					dst.SrcBlend = ToDx(src.srcColor);
					dst.DestBlend = ToDx(src.dstColor);
					dst.BlendOpAlpha = ToDx(src.alphaOp);
					dst.SrcBlendAlpha = ToDx(src.srcAlpha);
					dst.DestBlendAlpha = ToDx(src.dstAlpha);
				}
			} break;
			case PsoSubobj::DepthStencil: {
				auto& D = *static_cast<const SubobjDepth*>(items[i].data);
				depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
				depth.DepthEnable = D.ds.depthEnable;
				depth.DepthWriteMask = D.ds.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
				depth.DepthFunc = ToDx(D.ds.depthFunc);
			} break;
			case PsoSubobj::RTVFormats: {
				auto& R = *static_cast<const SubobjRTVs*>(items[i].data);
				rtv.NumRenderTargets = R.rt.count;
				for (uint32_t k = 0; k < R.rt.count && k < 8; k++) rtv.RTFormats[k] = ToDxgi(R.rt.formats[k]);
			} break;
			case PsoSubobj::DSVFormat: {
				auto& Z = *static_cast<const SubobjDSV*>(items[i].data);
				dsv = ToDxgi(Z.dsv);
			} break;
			case PsoSubobj::Sample: {
				auto& S = *static_cast<const SubobjSample*>(items[i].data);
				sample = { S.sd.count, S.sd.quality };
			} break;
			default: break;
			}
		}

		// Validate & decide kind
		if (hasCS && hasGfx) return {};          // invalid mix
		if (!hasCS && !hasGfx) return {};        // no shaders
		const bool isCompute = hasCS;

		// Build a raw stream buffer by concatenating typed subobjects
		// (order doesn't matter, DX12 parses headers)
		struct AnySub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type; }; // header matches CD3DX12 helpers
		std::vector<uint8_t> stream; stream.reserve(1024);

		auto push = [&](auto subobj) {
			size_t before = stream.size();
			stream.resize(before + sizeof(subobj));
			std::memcpy(stream.data() + before, &subobj, sizeof(subobj));
			};

		// Root signature is required
		if (!root) return {};
		push(CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE(root));

		if (isCompute) {
			// Compute pipeline only needs CS (+ optional flags)
			if (!(cs.pShaderBytecode && cs.BytecodeLength)) { return {}; }
			push(CD3DX12_PIPELINE_STATE_STREAM_CS(cs));
		}
		else {
			// Graphics / Mesh pipeline
			if (as.pShaderBytecode && as.BytecodeLength) {
				push(CD3DX12_PIPELINE_STATE_STREAM_AS(as));
			}
			if (ms.pShaderBytecode && ms.BytecodeLength) {
				push(CD3DX12_PIPELINE_STATE_STREAM_MS(ms));
			}
			if (vs.pShaderBytecode && vs.BytecodeLength) {
				push(CD3DX12_PIPELINE_STATE_STREAM_VS(vs));
			}
			if (ps.pShaderBytecode && ps.BytecodeLength) {
				push(CD3DX12_PIPELINE_STATE_STREAM_PS(ps));
			}
			push(CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER(rast));
			push(CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC(blend));
			push(CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL(depth));
			push(CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS(rtv));
			push(CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT(dsv));
			push(CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC(sample));
		}

		D3D12_PIPELINE_STATE_STREAM_DESC sd{};
		sd.pPipelineStateSubobjectStream = stream.data();
		sd.SizeInBytes = (UINT)stream.size();

		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		if (FAILED(impl->dev->CreatePipelineState(&sd, IID_PPV_ARGS(&pso)))) return {};

		auto handle = impl->pipelines.alloc(Dx12Pipeline{ pso, isCompute });
		return MakePipelinePtr(d, handle);
	}

	static void d_destroyBuffer(Device* d, ResourceHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->buffers.free(h); }
	static void d_destroyTexture(Device* d, ResourceHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->textures.free(h); }
	static void d_destroyView(Device* d, ViewHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->views.free(h); }
	static void d_destroySampler(Device* d, SamplerHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->samplers.free(h); }
	static void d_destroyPipeline(Device* d, PipelineHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->pipelines.free(h); }

	static void d_destroyCommandList(Device* d, CommandList* p) noexcept {
		if (!d || !p || !p->IsValid()) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		impl->commandLists.free(p->GetHandle());
		p->Reset();
	}

	static Queue d_getQueue(Device* d, QueueKind qk) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Queue out{}; out.vt = &g_qvt;
		Dx12QueueState* s = (qk == QueueKind::Graphics ? &impl->gfx : qk == QueueKind::Compute ? &impl->comp : &impl->copy);
		s->owner = impl;
		out.impl = s;
		return out;
	}

	static Result d_waitIdle(Device* d) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		impl->gfx.q->Signal(impl->gfx.fence.Get(), ++impl->gfx.value);
		if (impl->gfx.fence->GetCompletedValue() < impl->gfx.value) {
			HANDLE e = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			impl->gfx.fence->SetEventOnCompletion(impl->gfx.value, e);
			WaitForSingleObject(e, INFINITE);
			CloseHandle(e);
		}
		return Result::Ok;
	}
	static void   d_flushDeletionQueue(Device*) noexcept {}

	// Swapchain create/destroy
	static SwapchainPtr d_createSwapchain(Device* d, void* hwnd, uint32_t w, uint32_t h, Format fmt, uint32_t bufferCount, bool allowTearing) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.BufferCount = bufferCount;
		desc.Width = w;
		desc.Height = h;
		desc.Format = ToDxgi(fmt);
		desc.SampleDesc = { 1,0 };
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		desc.Flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		ComPtr<IDXGISwapChain1> sc1;
		IDXGIFactory7* facForCreate = impl->upgradeFn && impl->slFactory ? impl->slFactory.Get()
			: impl->factory.Get();
		facForCreate->CreateSwapChainForHwnd(impl->gfx.q.Get(), (HWND)hwnd, &desc, nullptr, nullptr, &sc1);
		ComPtr<IDXGISwapChain3> sc; 
		sc1.As(&sc);

		// RTV heap for backbuffers
		ComPtr<ID3D12DescriptorHeap> rtvHeap;
		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		hd.NumDescriptors = bufferCount; impl->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap));
		UINT inc = impl->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		auto cpuStart = rtvHeap->GetCPUDescriptorHandleForHeapStart();

		std::vector<ComPtr<ID3D12Resource>> imgs(bufferCount);
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvCPU(bufferCount);
		std::vector<ResourceHandle> imgHandles(bufferCount);
		std::vector<ViewHandle> rtvHandles(bufferCount);

		for (UINT i = 0; i < bufferCount; i++) {
			sc->GetBuffer(i, IID_PPV_ARGS(&imgs[i]));
			// Register as a TextureHandle
			Dx12Texture t{};
			t.res = imgs[i];
			t.fmt = desc.Format;
			t.w = w;
			t.h = h;
			t.mips = 1;
			t.arraySize = 1;
			t.dim = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			t.depth = 1;
			imgHandles[i] = impl->textures.alloc(t);
			// Create RTV in the heap
			auto cpu = cpuStart;
			cpu.ptr += SIZE_T(i) * inc; rtvCPU[i] = cpu;
			D3D12_RENDER_TARGET_VIEW_DESC r{};
			r.Format = desc.Format;
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			impl->dev->CreateRenderTargetView(imgs[i].Get(), &r, cpu);
			// Also register a ViewHandle so RHI passes can reference it directly
			Dx12View v{};
			v.desc = ViewDesc{ ViewKind::RTV, imgHandles[i], {}, Format::Unknown };
			v.cpu = cpu;
			rtvHandles[i] = impl->views.alloc(v);
		}

		auto* scWrap = new Dx12Swapchain{};
		scWrap->sc = sc; scWrap->fmt = desc.Format;
		scWrap->w = w; scWrap->h = h;
		scWrap->count = bufferCount;
		scWrap->current = sc->GetCurrentBackBufferIndex();
		scWrap->rtvHeap = rtvHeap;
		scWrap->rtvInc = inc; scWrap->rtvCPU = rtvCPU; scWrap->images = imgs;
		scWrap->imageHandles = imgHandles;
		scWrap->rtvHandles = rtvHandles;

		Swapchain out{};
		out.impl = scWrap;
		out.vt = &g_scvt;
		return MakeSwapchainPtr(d, out);
	}

	static void d_destroySwapchain(Device*, Swapchain* sc) noexcept {
		auto* s = static_cast<Dx12Swapchain*>(sc->impl);
		delete s; sc->impl = nullptr;
		sc->vt = nullptr;
	}

	static void d_destroyDevice(Device* d) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		delete impl; d->impl = nullptr;
		d->vt = nullptr;
	}

	static D3D12_SHADER_VISIBILITY ToDx12Vis(ShaderStage s) {
		switch (s) {
		case ShaderStage::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
		case ShaderStage::Pixel:  return D3D12_SHADER_VISIBILITY_PIXEL;
		case ShaderStage::Mesh:  return D3D12_SHADER_VISIBILITY_MESH;
		case ShaderStage::Task:  return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
		case ShaderStage::Compute: return D3D12_SHADER_VISIBILITY_ALL;
		default:                  return D3D12_SHADER_VISIBILITY_ALL;
		}
	}

	static PipelineLayoutPtr d_createPipelineLayout(Device* d, const PipelineLayoutDesc& ld) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);

		// Root parameters: push constants only (bindless tables omitted for brevity)
		std::vector<D3D12_ROOT_PARAMETER1> params;
		params.reserve(ld.pushConstants.size);
		for (uint32_t i = 0; i < ld.pushConstants.size; ++i) {
			const auto& pc = ld.pushConstants.data[i];
			D3D12_ROOT_PARAMETER1 p{};
			p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			p.Constants.Num32BitValues = pc.num32BitValues;
			p.Constants.ShaderRegister = pc.binding;   // binding -> ShaderRegister
			p.Constants.RegisterSpace = pc.set;       // set     -> RegisterSpace
			p.ShaderVisibility = ToDx12Vis(pc.visibility);
			params.push_back(p);
		}

		// Static samplers
		std::vector<D3D12_STATIC_SAMPLER_DESC> ssmps;
		ssmps.reserve(ld.staticSamplers.size);
		for (uint32_t i = 0; i < ld.staticSamplers.size; ++i) {
			const auto& ss = ld.staticSamplers.data[i];
			D3D12_STATIC_SAMPLER_DESC s{};
			// Map SamplerDesc -> D3D12 fields TODO: complete
			s.Filter = (ss.sampler.maxAniso > 1) ? D3D12_FILTER_ANISOTROPIC
				: D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			s.MipLODBias = 0.0f;
			s.MaxAnisotropy = ss.sampler.maxAniso;
			s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			s.MinLOD = 0.0f;
			s.MaxLOD = D3D12_FLOAT32_MAX;
			s.ShaderRegister = ss.binding; // binding -> ShaderRegister
			s.RegisterSpace = ss.set;     // set -> RegisterSpace
			s.ShaderVisibility = ToDx12Vis(ss.visibility);
			s.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
			ssmps.push_back(s);
			// (arrayCount>1: add multiple entries or extend StaticSamplerDesc to carry per-binding arrays)
		}

		// Root signature flags
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs{};
		rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		rs.Desc_1_1.NumParameters = (UINT)params.size();
		rs.Desc_1_1.pParameters = params.data();
		rs.Desc_1_1.NumStaticSamplers = (UINT)ssmps.size();
		rs.Desc_1_1.pStaticSamplers = ssmps.data();
		rs.Desc_1_1.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
			D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

		if (ld.flags & rhi::PipelineLayoutFlags::AllowInputAssembler) {
			rs.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		}


		Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3DX12SerializeVersionedRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err)))
			return {};
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
		if (FAILED(impl->dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
			IID_PPV_ARGS(&root))))
			return {};

		Dx12PipelineLayout L{};
		if (ld.pushConstants.size && ld.pushConstants.data)
			L.pcs.assign(ld.pushConstants.data, ld.pushConstants.data + ld.pushConstants.size);
		if (ld.staticSamplers.size && ld.staticSamplers.data)
			L.staticSamplers.assign(ld.staticSamplers.data, ld.staticSamplers.data + ld.staticSamplers.size);
		L.root = root;
		auto handle = impl->pipelineLayouts.alloc(std::move(L));
		return MakePipelineLayoutPtr(d, handle);
	}


	static void d_destroyPipelineLayout(Device* d, PipelineLayoutHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->pipelineLayouts.free(h);
	}

	static bool FillDx12Arg(const IndirectArg& a, D3D12_INDIRECT_ARGUMENT_DESC& out) {
		switch (a.kind) {
		case IndirectArgKind::Constant:
			out.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
			out.Constant.RootParameterIndex = a.u.rootConstants.rootIndex;
			out.Constant.DestOffsetIn32BitValues = a.u.rootConstants.destOffset32;
			out.Constant.Num32BitValuesToSet = a.u.rootConstants.num32;
			return true;
		case IndirectArgKind::DispatchMesh:
			out.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
			return true;
		case IndirectArgKind::Dispatch:
			out.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
			return true;
		case IndirectArgKind::Draw:
			out.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
			return true;
		case IndirectArgKind::DrawIndexed:
			out.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
			return true;
		case IndirectArgKind::VertexBuffer:
			out.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
			out.VertexBuffer.Slot = a.u.vertexBuffer.slot; return true;
		case IndirectArgKind::IndexBuffer:
			out.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
			return true;
		default: return false;
		}
	}

	static bool FillDx12Arg(const IndirectArg& a, D3D12_INDIRECT_ARGUMENT_DESC& out);

	static CommandSignaturePtr d_createCommandSignature(Device* d,
		const CommandSignatureDesc& cd,
		PipelineLayoutHandle layout) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);

		std::vector<D3D12_INDIRECT_ARGUMENT_DESC> dxArgs(cd.args.size);
		bool hasRoot = false;
		for (uint32_t i = 0; i < cd.args.size; ++i) {
			if (!FillDx12Arg(cd.args.data[i], dxArgs[i])) return {};
			hasRoot |= (cd.args.data[i].kind == IndirectArgKind::Constant);
		}

		ID3D12RootSignature* rs = nullptr;
		if (hasRoot) {
			auto* L = impl->pipelineLayouts.get(layout);
			if (!L || !L->root) return {};
			rs = L->root.Get();
		}

		D3D12_COMMAND_SIGNATURE_DESC desc{};
		desc.pArgumentDescs = dxArgs.data();
		desc.NumArgumentDescs = (UINT)dxArgs.size();
		desc.ByteStride = cd.byteStride;

		Microsoft::WRL::ComPtr<ID3D12CommandSignature> cs;
		if (FAILED(impl->dev->CreateCommandSignature(&desc, rs, IID_PPV_ARGS(&cs)))) return {};
		Dx12CommandSignature S{ cs, cd.byteStride };
		auto handle = impl->commandSignatures.alloc(std::move(S));
		return MakeCommandSignaturePtr(d, handle);
	}

	static void d_destroyCommandSignature(Device* d, CommandSignatureHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->commandSignatures.free(h);
	}

	static DescriptorHeapPtr d_createDescriptorHeap(Device* d, const DescriptorHeapDesc& hd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);

		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = ToDX(hd.type);
		desc.NumDescriptors = hd.capacity;
		desc.Flags = hd.shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		if (FAILED(impl->dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)))) return {};

		Dx12DescHeap H{};
		H.heap = heap; H.type = desc.Type;
		H.inc = impl->dev->GetDescriptorHandleIncrementSize(desc.Type);
		H.shaderVisible = (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
		H.cpuStart = heap->GetCPUDescriptorHandleForHeapStart();
		if (H.shaderVisible) H.gpuStart = heap->GetGPUDescriptorHandleForHeapStart();

		auto handle = impl->descHeaps.alloc(std::move(H));
		return MakeDescriptorHeapPtr(d, handle);
	}
	static void d_destroyDescriptorHeap(Device* d, DescriptorHeapHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->descHeaps.free(h);
	}
	static bool DxGetDstCpu(Dx12Device* impl, DescriptorSlot s, D3D12_CPU_DESCRIPTOR_HANDLE& out, D3D12_DESCRIPTOR_HEAP_TYPE expect) {
		auto* H = impl->descHeaps.get(s.heap);
		if (!H || H->type != expect) return false;
		out = H->cpuStart;
		out.ptr += SIZE_T(s.index) * H->inc;
		return true;
	}

	static bool DxGetDstGpu(Dx12Device* impl, DescriptorSlot s,
		D3D12_GPU_DESCRIPTOR_HANDLE& out,
		D3D12_DESCRIPTOR_HEAP_TYPE expect) {
		auto* H = impl->descHeaps.get(s.heap);
		if (!H || H->type != expect || !H->shaderVisible) return false;
		out = H->gpuStart;
		out.ptr += UINT64(s.index) * H->inc;
		return true;
	}

	static Result d_createShaderResourceView(Device* d, DescriptorSlot s, const SrvDesc& dv) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
			return Result::InvalidArg;

		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Shader4ComponentMapping = (dv.componentMapping == 0)
			? D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
			: dv.componentMapping;

		switch (dv.dim) {
		case SrvDim::Buffer: {
			auto* B = impl->buffers.get(dv.resource);
			if (!B || !B->res) return Result::InvalidArg;

			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			switch (dv.buffer.kind) {
			case BufferViewKind::Raw:
				desc.Format = DXGI_FORMAT_R32_TYPELESS;
				desc.Buffer.FirstElement = (UINT)dv.buffer.firstElement; // in 32-bit units
				desc.Buffer.NumElements = dv.buffer.numElements;
				desc.Buffer.StructureByteStride = 0;
				desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
				break;
			case BufferViewKind::Structured:
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.Buffer.FirstElement = (UINT)dv.buffer.firstElement;
				desc.Buffer.NumElements = dv.buffer.numElements;
				desc.Buffer.StructureByteStride = dv.buffer.structureByteStride;
				desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
				break;
			case BufferViewKind::Typed:
				desc.Format = ToDxgi(dv.formatOverride);
				desc.Buffer.FirstElement = (UINT)dv.buffer.firstElement;
				desc.Buffer.NumElements = dv.buffer.numElements;
				desc.Buffer.StructureByteStride = 0;
				desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
				break;
			}
			impl->dev->CreateShaderResourceView(B->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture1D: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			desc.Texture1D.MostDetailedMip = dv.tex1D.mostDetailedMip;
			desc.Texture1D.MipLevels = dv.tex1D.mipLevels;
			desc.Texture1D.ResourceMinLODClamp = dv.tex1D.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture1DArray: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			desc.Texture1DArray.MostDetailedMip = dv.tex1DArray.mostDetailedMip;
			desc.Texture1DArray.MipLevels = dv.tex1DArray.mipLevels;
			desc.Texture1DArray.FirstArraySlice = dv.tex1DArray.firstArraySlice;
			desc.Texture1DArray.ArraySize = dv.tex1DArray.arraySize;
			desc.Texture1DArray.ResourceMinLODClamp = dv.tex1DArray.minLodClamp;
			auto* R = T->res.Get();
			impl->dev->CreateShaderResourceView(R, &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2D: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MostDetailedMip = dv.tex2D.mostDetailedMip;
			desc.Texture2D.MipLevels = dv.tex2D.mipLevels;
			desc.Texture2D.PlaneSlice = dv.tex2D.planeSlice;
			desc.Texture2D.ResourceMinLODClamp = dv.tex2D.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2DArray: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MostDetailedMip = dv.tex2DArray.mostDetailedMip;
			desc.Texture2DArray.MipLevels = dv.tex2DArray.mipLevels;
			desc.Texture2DArray.FirstArraySlice = dv.tex2DArray.firstArraySlice;
			desc.Texture2DArray.ArraySize = dv.tex2DArray.arraySize;
			desc.Texture2DArray.PlaneSlice = dv.tex2DArray.planeSlice;
			desc.Texture2DArray.ResourceMinLODClamp = dv.tex2DArray.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2DMS: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2DMSArray: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
			desc.Texture2DMSArray.FirstArraySlice = dv.tex2DMSArray.firstArraySlice;
			desc.Texture2DMSArray.ArraySize = dv.tex2DMSArray.arraySize;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture3D: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MostDetailedMip = dv.tex3D.mostDetailedMip;
			desc.Texture3D.MipLevels = dv.tex3D.mipLevels;
			desc.Texture3D.ResourceMinLODClamp = dv.tex3D.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::TextureCube: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			desc.TextureCube.MostDetailedMip = dv.cube.mostDetailedMip;
			desc.TextureCube.MipLevels = dv.cube.mipLevels;
			desc.TextureCube.ResourceMinLODClamp = dv.cube.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::TextureCubeArray: {
			auto* T = impl->textures.get(dv.resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			desc.TextureCubeArray.MostDetailedMip = dv.cubeArray.mostDetailedMip;
			desc.TextureCubeArray.MipLevels = dv.cubeArray.mipLevels;
			desc.TextureCubeArray.First2DArrayFace = dv.cubeArray.first2DArrayFace;
			desc.TextureCubeArray.NumCubes = dv.cubeArray.numCubes;
			desc.TextureCubeArray.ResourceMinLODClamp = dv.cubeArray.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::AccelerationStruct: {
			// AS is stored in a buffer with ResourceFlags::RaytracingAccelerationStructure
			auto* B = impl->buffers.get(dv.resource); if (!B || !B->res) return Result::InvalidArg;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			desc.RaytracingAccelerationStructure.Location = B->res->GetGPUVirtualAddress();
			impl->dev->CreateShaderResourceView(B->res.Get(), &desc, dst);
			return Result::Ok;
		}

		default: break;
		}

		return Result::InvalidArg;
	}

	static Result d_createUnorderedAccessView(Device* d, DescriptorSlot s, const UavDesc& dv) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) return Result::Failed;

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
			return Result::InvalidArg;

		D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
		ID3D12Resource* pResource = nullptr;
		ID3D12Resource* pCounterResource = nullptr; // optional, for structured append/consume

		switch (dv.texDim)
		{
			// ========================= Buffer UAV =========================
		case UavDim::Buffer:
		{
			auto* B = impl->buffers.get(dv.resource);
			if (!B || !B->res) return Result::InvalidArg;

			pResource = B->res.Get();
			desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			desc.Buffer.FirstElement = (UINT)dv.buffer.firstElement;
			desc.Buffer.NumElements = dv.buffer.numElements;
			desc.Buffer.CounterOffsetInBytes = (UINT)dv.buffer.counterOffsetInBytes;

			switch (dv.buffer.kind)
			{
			case BufferViewKind::Raw:
				desc.Format = DXGI_FORMAT_R32_TYPELESS;
				desc.Buffer.StructureByteStride = 0;
				desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
				break;

			case BufferViewKind::Structured:
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.Buffer.StructureByteStride = dv.buffer.structureByteStride;
				desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
				// If caller provided a counter offset, assume the counter is in the same buffer.
				if (dv.buffer.counterOffsetInBytes != 0)
					pCounterResource = pResource;
				break;

			case BufferViewKind::Typed:
				return Result::Unsupported; // TODO
			}

			impl->dev->CreateUnorderedAccessView(pResource, pCounterResource, &desc, dst);
			return Result::Ok;
		}

		// ========================= Texture UAVs =========================
		case UavDim::Texture1D:
		{
			auto* T = impl->textures.get(dv.resource);
			if (!T || !T->res) return Result::InvalidArg;
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			desc.Texture1D.MipSlice = dv.tex1D.mipSlice;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture1DArray:
		{
			auto* T = impl->textures.get(dv.resource);
			if (!T || !T->res) return Result::InvalidArg;
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			desc.Texture1DArray.MipSlice = dv.tex1DArray.mipSlice;
			desc.Texture1DArray.FirstArraySlice = dv.tex1DArray.firstArraySlice;
			desc.Texture1DArray.ArraySize = dv.tex1DArray.arraySize;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2D:
		{
			auto* T = impl->textures.get(dv.resource);
			if (!T || !T->res) return Result::InvalidArg;
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = dv.tex2D.mipSlice;
			desc.Texture2D.PlaneSlice = dv.tex2D.planeSlice;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2DArray:
		{
			auto* T = impl->textures.get(dv.resource);
			if (!T || !T->res) return Result::InvalidArg;
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = dv.tex2DArray.mipSlice;
			desc.Texture2DArray.FirstArraySlice = dv.tex2DArray.firstArraySlice;
			desc.Texture2DArray.ArraySize = dv.tex2DArray.arraySize;
			desc.Texture2DArray.PlaneSlice = dv.tex2DArray.planeSlice;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture3D:
		{
			auto* T = impl->textures.get(dv.resource);
			if (!T || !T->res) return Result::InvalidArg;
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MipSlice = dv.tex3D.mipSlice;
			desc.Texture3D.FirstWSlice = dv.tex3D.firstWSlice;
			desc.Texture3D.WSize = (dv.tex3D.wSize == 0) ? UINT(-1) : dv.tex3D.wSize;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2DMS:
		case UavDim::Texture2DMSArray:
			// UAVs for MSAA textures are not supported by D3D12
			return Result::Unsupported;
		}

		return Result::InvalidArg;
	}

	static Result d_createConstantBufferView(Device* d, DescriptorSlot s, ResourceHandle bh, const CbvDesc& dv) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return Result::InvalidArg;
		auto* B = impl->buffers.get(bh); if (!B) return Result::InvalidArg;

		D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
		desc.BufferLocation = B->res->GetGPUVirtualAddress() + dv.byteOffset;
		desc.SizeInBytes = (UINT)((dv.byteSize + 255) & ~255u);
		impl->dev->CreateConstantBufferView(&desc, dst);
		return Result::Ok;
	}

	static Result d_createSampler(Device* d, DescriptorSlot s, const SamplerDesc& sd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)) return Result::InvalidArg;

		D3D12_SAMPLER_DESC desc{};
		desc.Filter = (sd.maxAniso > 1) ? D3D12_FILTER_ANISOTROPIC : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.MaxAnisotropy = sd.maxAniso;
		desc.MinLOD = 0.0f; desc.MaxLOD = D3D12_FLOAT32_MAX;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		impl->dev->CreateSampler(&desc, dst);
		return Result::Ok;
	}

	static Result d_createRenderTargetView(Device* d, DescriptorSlot s, const RtvDesc& rd) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) return Result::Failed;

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_RTV))
			return Result::InvalidArg;

		// For texture RTVs we expect a texture resource
		auto* T = impl->textures.get(rd.texture);
		if (!T && rd.dim != RtvDim::Buffer) return Result::InvalidArg;

		D3D12_RENDER_TARGET_VIEW_DESC r{};
		ID3D12Resource* pRes = nullptr;

		switch (rd.dim)
		{
		case RtvDim::Texture1D:
		{
			pRes = T->res.Get();
			r.Format = (rd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(rd.formatOverride);
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			r.Texture1D.MipSlice = rd.range.baseMip;
			break;
		}

		case RtvDim::Texture1DArray:
		{
			pRes = T->res.Get();
			r.Format = (rd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(rd.formatOverride);
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			r.Texture1DArray.MipSlice = rd.range.baseMip;
			r.Texture1DArray.FirstArraySlice = rd.range.baseLayer;
			r.Texture1DArray.ArraySize = rd.range.layerCount;
			break;
		}

		case RtvDim::Texture2D:
		{
			pRes = T->res.Get();
			r.Format = (rd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(rd.formatOverride);
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			r.Texture2D.MipSlice = rd.range.baseMip;
			r.Texture2D.PlaneSlice = 0; // no plane in desc -> default to 0
			break;
		}

		case RtvDim::Texture2DArray:
		{
			pRes = T->res.Get();
			r.Format = (rd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(rd.formatOverride);
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			r.Texture2DArray.MipSlice = rd.range.baseMip;
			r.Texture2DArray.FirstArraySlice = rd.range.baseLayer;
			r.Texture2DArray.ArraySize = rd.range.layerCount;
			r.Texture2DArray.PlaneSlice = 0;
			break;
		}

		case RtvDim::Texture2DMS:
		{
			pRes = T->res.Get();
			r.Format = (rd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(rd.formatOverride);
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
			break;
		}

		case RtvDim::Texture2DMSArray:
		{
			pRes = T->res.Get();
			r.Format = (rd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(rd.formatOverride);
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			r.Texture2DMSArray.FirstArraySlice = rd.range.baseLayer;
			r.Texture2DMSArray.ArraySize = rd.range.layerCount;
			break;
		}

		case RtvDim::Texture3D:
		{
			pRes = T->res.Get();
			r.Format = (rd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(rd.formatOverride);
			r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			r.Texture3D.MipSlice = rd.range.baseMip;
			// Reuse range.baseLayer/layerCount to address Z-slices of the 3D subresource.
			r.Texture3D.FirstWSlice = rd.range.baseLayer;
			r.Texture3D.WSize = (rd.range.layerCount == 0) ? UINT(-1) : rd.range.layerCount;
			break;
		}

		case RtvDim::Buffer:
		{
			// TODO: What is this?
			return Result::Unsupported;
		}

		default:
			return Result::Unsupported;
		}

		impl->dev->CreateRenderTargetView(pRes, &r, dst);
		return Result::Ok;
	}

	static Result d_createDepthStencilView(Device* d, DescriptorSlot s, const DsvDesc& dd) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) return Result::Failed;

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_DSV))
			return Result::InvalidArg;

		auto* T = impl->textures.get(dd.texture);
		if (!T) return Result::InvalidArg;

		D3D12_DEPTH_STENCIL_VIEW_DESC z{};
		z.Format = (dd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dd.formatOverride);
		z.Flags = (dd.readOnlyDepth ? D3D12_DSV_FLAG_READ_ONLY_DEPTH : (D3D12_DSV_FLAGS)0) |
			(dd.readOnlyStencil ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : (D3D12_DSV_FLAGS)0);

		switch (dd.dim)
		{
		case DsvDim::Texture1D:
			z.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			z.Texture1D.MipSlice = dd.range.baseMip;
			break;

		case DsvDim::Texture1DArray:
			z.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			z.Texture1DArray.MipSlice = dd.range.baseMip;
			z.Texture1DArray.FirstArraySlice = dd.range.baseLayer;
			z.Texture1DArray.ArraySize = dd.range.layerCount;
			break;

		case DsvDim::Texture2D:
			z.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			z.Texture2D.MipSlice = dd.range.baseMip;
			break;

		case DsvDim::Texture2DArray:
			z.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			z.Texture2DArray.MipSlice = dd.range.baseMip;
			z.Texture2DArray.FirstArraySlice = dd.range.baseLayer;
			z.Texture2DArray.ArraySize = dd.range.layerCount;
			break;

		case DsvDim::Texture2DMS:
			z.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
			break;

		case DsvDim::Texture2DMSArray:
			z.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
			z.Texture2DMSArray.FirstArraySlice = dd.range.baseLayer;
			z.Texture2DMSArray.ArraySize = dd.range.layerCount;
			break;

		default:
			return Result::Unsupported;
		}

		impl->dev->CreateDepthStencilView(T->res.Get(), &z, dst);
		return Result::Ok;
	}


	static D3D12_COMMAND_LIST_TYPE ToDx(QueueKind q) {
		return q == QueueKind::Graphics ? D3D12_COMMAND_LIST_TYPE_DIRECT
			: q == QueueKind::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE
			: D3D12_COMMAND_LIST_TYPE_COPY;
	}

	static CommandAllocatorPtr d_createCommandAllocator(Device* d, QueueKind q) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> a;
		if (FAILED(impl->dev->CreateCommandAllocator(ToDx(q), IID_PPV_ARGS(&a)))) return {};

		Dx12Allocator A{}; A.alloc = a; A.type = ToDx(q);
		auto h = impl->allocators.alloc(std::move(A));

		extern const CommandAllocatorVTable g_cavt; // vtable defined below
		CommandAllocator out{h};
		out.impl = impl->allocators.get(h);
		out.vt = &g_cavt;
		return MakeCommandAllocatorPtr(d, out);
	}

	static void d_destroyCommandAllocator(Device* d, CommandAllocator* ca) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		impl->allocators.free(ca->GetHandle());
	}

	static CommandListPtr d_createCommandList(Device* d, QueueKind q, CommandAllocator ca) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* A = static_cast<Dx12Allocator*>(ca.impl);
		if (!A) return {};

		Dx12CommandList rec{};
		if (FAILED(impl->dev->CreateCommandList(0, A->type, A->alloc.Get(), nullptr, IID_PPV_ARGS(&rec.cl)))) return {};
		rec.cl->Close();
		rec.dev = impl;
		rec.alloc = A->alloc;
		rec.type = A->type;

		auto h = impl->commandLists.alloc(std::move(rec));

		CommandList out{h};
		out.impl = impl->commandLists.get(h);
		out.vt = &g_clvt;
		return MakeCommandListPtr(d, out);
	}

	static ResourcePtr d_createCommittedBuffer(Device* d, const ResourceDesc& bd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->dev || bd.buffer.sizeBytes == 0) return {};

		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = ToDx(bd.memory);
		hp.CreationNodeMask = 1;
		hp.VisibleNodeMask = 1;

		const D3D12_RESOURCE_FLAGS flags = ToDX(bd.flags);
		const D3D12_RESOURCE_DESC1  desc = MakeBufferDesc1(bd.buffer.sizeBytes, flags);

		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		// Buffers must use UNDEFINED layout per spec
		const D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;

		HRESULT hr = impl->dev->CreateCommittedResource3(
			&hp,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			initialLayout,
			/*pOptimizedClearValue*/ nullptr,        // buffers: must be null
			/*pProtectedSession*/   nullptr,
			/*NumCastableFormats*/   0,
			/*pCastableFormats*/     nullptr,
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) return {};

		if (bd.debugName) res->SetName(std::wstring(bd.debugName, bd.debugName + ::strlen(bd.debugName)).c_str());

		Dx12Buffer B{};
		B.res = std::move(res);
		auto handle = impl->buffers.alloc(std::move(B));

		Resource out{ handle, false };
		out.impl = impl->buffers.get(handle);
		out.vt = &g_buf_rvt;
		return MakeBufferPtr(d, out);
	}

	static ResourcePtr d_createCommittedTexture(Device* d, const ResourceDesc& td) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->dev || td.texture.width == 0 || td.texture.height == 0 || td.texture.format == Format::Unknown)
			return {};

		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = ToDx(td.memory);
		hp.CreationNodeMask = 1;
		hp.VisibleNodeMask = 1;

		const D3D12_RESOURCE_DESC1 desc = MakeTexDesc1(td);

		D3D12_CLEAR_VALUE* pClear = nullptr;
		D3D12_CLEAR_VALUE clear;
		if (td.texture.optimizedClear) {
			clear = ToDX(*td.texture.optimizedClear);
			pClear = &clear;
		}
		// Textures can specify InitialLayout (enhanced barriers)
		const D3D12_BARRIER_LAYOUT initialLayout = ToDX(td.texture.initialLayout);

		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		HRESULT hr = impl->dev->CreateCommittedResource3(
			&hp,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			initialLayout,
			pClear,
			/*pProtectedSession*/ nullptr,
			/*NumCastableFormats*/ 0,
			/*pCastableFormats*/   nullptr,
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) return {};

		if (td.debugName) res->SetName(std::wstring(td.debugName, td.debugName + ::strlen(td.debugName)).c_str());

		Dx12Texture T{};
		T.res = std::move(res);
		T.fmt = desc.Format;
		T.w = td.texture.width; T.h = td.texture.height;
		T.mips = td.texture.mipLevels;
		T.dim = (td.type == ResourceType::Texture3D)
			? D3D12_RESOURCE_DIMENSION_TEXTURE3D
			: (td.type == ResourceType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
				: D3D12_RESOURCE_DIMENSION_TEXTURE1D);
		if (td.type == ResourceType::Texture3D) {
			T.depth = td.texture.depthOrLayers;
			T.arraySize = 1;
		}
		else {
			T.depth = 1;
			T.arraySize = td.texture.depthOrLayers; // NOTE: for cube/cube array, pass 6*N here at creation time
		}
		auto handle = impl->textures.alloc(std::move(T));

		Resource out{ handle, true };
		out.impl = impl->textures.get(handle);
		out.vt = &g_tex_rvt;

		return MakeTexturePtr(d, out);
	}

	static ResourcePtr d_createCommittedResource(Device* d, const ResourceDesc& td) noexcept {
		switch (td.type) {
		case ResourceType::Buffer:  return d_createCommittedBuffer(d, td);
		case ResourceType::Texture3D:
		case ResourceType::Texture2D:
		case ResourceType::Texture1D:
			return d_createCommittedTexture(d, td);
		case ResourceType::Unknown: return {};
		}
		return {};
	}

	static uint32_t d_getDescriptorHandleIncrementSize(Device* d, DescriptorHeapType type) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->dev) return 0;
		const D3D12_DESCRIPTOR_HEAP_TYPE t = ToDX(type);
		return (uint32_t)impl->dev->GetDescriptorHandleIncrementSize(t);
	}

	static TimelinePtr d_createTimeline(Device* d, uint64_t initial, const char* dbg) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Microsoft::WRL::ComPtr<ID3D12Fence> f;
		if (FAILED(impl->dev->CreateFence(initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f)))) return {};
		if (dbg) { std::wstring w(dbg, dbg + ::strlen(dbg)); f->SetName(w.c_str()); }
		Dx12Timeline T{ f };
		auto h = impl->timelines.alloc(std::move(T));
		return MakeTimelinePtr(d, h);
	}


	static void d_destroyTimeline(Device* d, TimelineHandle t) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		impl->timelines.free(t);
	}

	static uint64_t d_timelineCompletedValue(Device* d, TimelineHandle t) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* TL = impl->timelines.get(t)) return TL->fence->GetCompletedValue();
		return 0;
	}

	static Result d_timelineHostWait(Device* d, const TimelinePoint& p) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* TL = impl->timelines.get(p.t); if (!TL) return Result::InvalidArg;
		HANDLE e = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!e) return Result::Failed;
		HRESULT hr = TL->fence->SetEventOnCompletion(p.value, e);
		if (FAILED(hr)) { CloseHandle(e); return Result::Failed; }
		WaitForSingleObject(e, INFINITE);
		CloseHandle(e);
		return Result::Ok;
	}

	static HeapPtr d_createHeap(Device* d, const HeapDesc& hd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->dev || hd.sizeBytes == 0) return {};

		D3D12_HEAP_PROPERTIES props{};
		props.Type = ToDx(hd.memory);         // same helper you already have (Upload/Readback/Default)
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		props.CreationNodeMask = 1;
		props.VisibleNodeMask = 1;

		D3D12_HEAP_DESC desc{};
		desc.SizeInBytes = hd.sizeBytes;
		desc.Alignment = (hd.alignment ? hd.alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		desc.Properties = props;
		desc.Flags = ToDX(hd.flags);

		Microsoft::WRL::ComPtr<ID3D12Heap> heap;
		if (FAILED(impl->dev->CreateHeap(&desc, IID_PPV_ARGS(&heap)))) return {};

#ifdef _WIN32
		if (hd.debugName) { std::wstring w(hd.debugName, hd.debugName + ::strlen(hd.debugName)); heap->SetName(w.c_str()); }
#endif

		Dx12Heap H{ heap, hd.sizeBytes };
		auto h = impl->heaps.alloc(std::move(H));
		return MakeHeapPtr(d, HeapHandle{ h.index, h.generation });
	}

	static void d_destroyHeap(Device* d, HeapHandle h) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) return;
		impl->heaps.free(h);
	}

	static void d_setNameBuffer(Device* d, ResourceHandle b, const char* n) noexcept {
		if (!n) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* B = impl->buffers.get(b)) {
			std::wstring w(n, n + ::strlen(n));
			B->res->SetName(w.c_str());
		}
	}
	
	static void d_setNameTexture(Device* d, ResourceHandle t, const char* n) noexcept {
		if (!n) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* T = impl->textures.get(t)) {
			std::wstring w(n, n + ::strlen(n));
			T->res->SetName(w.c_str());
		}
	}

	static void d_setNameSampler(Device* d, SamplerHandle s, const char* n) noexcept {
		return; // TODO?
	}

	static void d_setNamePipelineLayout(Device* d, PipelineLayoutHandle p, const char* n) noexcept {
		return; // TODO?
	}

	static void d_setNamePipeline(Device* d, PipelineHandle p, const char* n) noexcept {
		if (!n) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* P = impl->pipelines.get(p)) {
			std::wstring w(n, n + ::strlen(n));
			P->pso->SetName(w.c_str());
		}
	}

	static void d_setNameCommandSignature(Device* d, CommandSignatureHandle cs, const char* n) noexcept {
		if (!n) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* CS = impl->commandSignatures.get(cs)) {
			std::wstring w(n, n + ::strlen(n));
			CS->sig->SetName(w.c_str());
		}
	}

	static void d_setNameDescriptorHeap(Device* d, DescriptorHeapHandle dh, const char* n) noexcept {
		if (!n) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* DH = impl->descHeaps.get(dh)) {
			std::wstring w(n, n + ::strlen(n));
			DH->heap->SetName(w.c_str());
		}
	}

	static void d_setNameTimeline(Device* d, TimelineHandle t, const char* n) noexcept {
		if (!n) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* TL = impl->timelines.get(t)) {
			std::wstring w(n, n + ::strlen(n));
			TL->fence->SetName(w.c_str());
		}
	}

	static void d_setNameHeap(Device* d, HeapHandle h, const char* n) noexcept {
		if (!n) return;
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* H = impl->heaps.get(h)) {
			std::wstring w(n, n + ::strlen(n));
			H->heap->SetName(w.c_str());
		}
	}

	static ResourcePtr d_createPlacedTexture(Device* d, HeapHandle hh, uint64_t offset, const ResourceDesc& td) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) return {};
		auto* H = impl->heaps.get(hh); if (!H || !H->heap) return {};
		if (td.texture.width == 0 || td.texture.height == 0 || td.texture.format == Format::Unknown)
			return {};
		const D3D12_RESOURCE_DESC1 desc = MakeTexDesc1(td);
		D3D12_CLEAR_VALUE* pClear = nullptr;
		D3D12_CLEAR_VALUE clear;
		if (td.texture.optimizedClear) {
			clear = ToDX(*td.texture.optimizedClear);
			pClear = &clear;
		}
		// Textures can specify InitialLayout (enhanced barriers)
		const D3D12_BARRIER_LAYOUT initialLayout = ToDX(td.texture.initialLayout);
		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		HRESULT hr = impl->dev->CreatePlacedResource2(
			H->heap.Get(),
			offset,
			&desc,
			initialLayout,
			pClear,
			/*numCastableFormats*/ 0,
			/*pProtectedSession*/ nullptr,
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) return {};
		if (td.debugName) res->SetName(std::wstring(td.debugName, td.debugName + ::strlen(td.debugName)).c_str());
		Dx12Texture T{};
		T.res = std::move(res);
		T.fmt = desc.Format;
		T.w = td.texture.width; T.h = td.texture.height;
		auto handle = impl->textures.alloc(std::move(T));
		return MakeTexturePtr(d, handle);
	}

	static ResourcePtr d_createPlacedBuffer(Device* d, HeapHandle hh, uint64_t offset, const ResourceDesc& bd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) return {};
		auto* H = impl->heaps.get(hh); if (!H || !H->heap) return {};
		if (bd.buffer.sizeBytes == 0) return {};
		const D3D12_RESOURCE_FLAGS flags = ToDX(bd.flags);
		const D3D12_RESOURCE_DESC1  desc = MakeBufferDesc1(bd.buffer.sizeBytes, flags);
		// Buffers must use UNDEFINED layout per spec
		const D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		HRESULT hr = impl->dev->CreatePlacedResource2(
			H->heap.Get(),
			offset,
			&desc,
			initialLayout,
			/*pOptimizedClearValue*/ nullptr,        // buffers: must be null
			/*numCastableFormats*/   0,
			/*pProtectedSession*/   nullptr,
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) return {};
		if (bd.debugName) res->SetName(std::wstring(bd.debugName, bd.debugName + ::strlen(bd.debugName)).c_str());
		Dx12Buffer B{};
		B.res = std::move(res);
		auto handle = impl->buffers.alloc(std::move(B));
		return MakeBufferPtr(d, handle);
	}

	static ResourcePtr d_createPlacedResource(Device* d, HeapHandle hh, uint64_t offset, const ResourceDesc& rd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) return {};
		auto* H = impl->heaps.get(hh); if (!H || !H->heap) return {};
		switch (rd.type) {
		case ResourceType::Buffer:  return d_createPlacedBuffer(d, hh, offset, rd);
		case ResourceType::Texture3D:
		case ResourceType::Texture2D:
		case ResourceType::Texture1D:
			return d_createPlacedTexture(d, hh, offset, rd);
		case ResourceType::Unknown: return {};
		}

	}

	static QueryPoolPtr d_createQueryPool(Device* d, const QueryPoolDesc& qd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || qd.count == 0) return {};

		Dx12QueryPool qp{};
		D3D12_QUERY_HEAP_DESC desc{};
		desc.Count = qd.count;

		switch (qd.type) {
		case QueryType::Timestamp:
			desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			qp.type = desc.Type; break;

		case QueryType::Occlusion:
			desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
			qp.type = desc.Type; break;

		case QueryType::PipelineStatistics: {
			// If mesh/task bits requested and supported -> use *_STATISTICS1
			bool needMesh = (qd.statsMask & (PS_TaskInvocations | PS_MeshInvocations | PS_MeshPrimitives)) != 0;

			D3D12_FEATURE_DATA_D3D12_OPTIONS9 opts9{};
			bool haveOpt9 = SUCCEEDED(impl->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &opts9, sizeof(opts9)));
			bool canMeshStats = haveOpt9 && !!opts9.MeshShaderPipelineStatsSupported;

			if (needMesh && !canMeshStats && qd.requireAllStats)
				return {}; // Unsupported

			desc.Type = needMesh && canMeshStats
				? D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1
				: D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
			qp.type = desc.Type;
			qp.usePSO1 = (desc.Type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1);
		} break;
		}

		Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
		if (FAILED(impl->dev->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap)))) return {};
		qp.heap = heap;
		qp.count = qd.count;

		auto handle = impl->queryPools.alloc(std::move(qp));
		return QueryPoolPtr(d, handle,
			[](Device* dev, QueryPoolHandle hh) noexcept {
				static_cast<Dx12Device*>(dev->impl)->queryPools.free(hh);
			}
		);
	}

	static void d_destroyQueryPool(Device* d, QueryPoolHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->queryPools.free(h);
	}

	static void d_destroyQueryPool(Device* d, QueryPoolHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->queryPools.free(h);
	}

	static TimestampCalibration d_getTimestampCalibration(Device* d, QueueKind q) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* s = (q == QueueKind::Graphics) ? &impl->gfx : (q == QueueKind::Compute ? &impl->comp : &impl->copy);
		UINT64 freq = 0;
		if (s->q) s->q->GetTimestampFrequency(&freq);
		return { freq };
	}

	static QueryResultInfo d_getQueryResultInfo(Device* d, QueryPoolHandle h) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* P = impl->queryPools.get(h);
		QueryResultInfo out{};
		if (!P) return out;
		out.count = P->count;

		switch (P->type) {
		case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
			out.type = QueryType::Timestamp;
			out.elementSize = sizeof(uint64_t);
			break;
		case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
			out.type = QueryType::Occlusion;
			out.elementSize = sizeof(uint64_t);
			break;
		case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
			out.type = QueryType::PipelineStatistics;
			out.elementSize = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
			break;
		case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1:
			out.type = QueryType::PipelineStatistics;
			out.elementSize = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1);
			break;
		}
		return out;
	}

	static PipelineStatsLayout d_getPipelineStatsLayout(Device* d, QueryPoolHandle h,
		PipelineStatsFieldDesc* outBuf,
		uint32_t cap) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* P = impl->queryPools.get(h);
		PipelineStatsLayout L{};
		if (!P) return L;

		L.info = d_getQueryResultInfo(d, h);

		// Build a local vector, then copy to outBuf
		std::vector<PipelineStatsFieldDesc> tmp;
		tmp.reserve(16);

		if (!P->usePSO1) {
			using S = D3D12_QUERY_DATA_PIPELINE_STATISTICS;
			auto push = [&](PipelineStatTypes f, size_t off) {
				tmp.push_back({ f, uint32_t(off), uint32_t(sizeof(uint64_t)), true });
				};
			push(PipelineStatTypes::IAVertices, offsetof(S, IAVertices));
			push(PipelineStatTypes::IAPrimitives, offsetof(S, IAPrimitives));
			push(PipelineStatTypes::VSInvocations, offsetof(S, VSInvocations));
			push(PipelineStatTypes::GSInvocations, offsetof(S, GSInvocations));
			push(PipelineStatTypes::GSPrimitives, offsetof(S, GSPrimitives));
			push(PipelineStatTypes::TSControlInvocations, offsetof(S, HSInvocations));
			push(PipelineStatTypes::TSEvaluationInvocations, offsetof(S, DSInvocations));
			push(PipelineStatTypes::PSInvocations, offsetof(S, PSInvocations));
			push(PipelineStatTypes::CSInvocations, offsetof(S, CSInvocations));
			// Mesh/Task not supported here
			tmp.push_back({ PipelineStatTypes::TaskInvocations, 0, 0, false });
			tmp.push_back({ PipelineStatTypes::MeshInvocations, 0, 0, false });
			tmp.push_back({ PipelineStatTypes::MeshPrimitives,  0, 0, false });
		}
		else {
			using S = D3D12_QUERY_DATA_PIPELINE_STATISTICS1;
			auto push = [&](PipelineStatTypes f, size_t off) {
				tmp.push_back({ f, uint32_t(off), uint32_t(sizeof(uint64_t)), true });
				};
			push(PipelineStatTypes::IAVertices, offsetof(S, IAVertices));
			push(PipelineStatTypes::IAPrimitives, offsetof(S, IAPrimitives));
			push(PipelineStatTypes::VSInvocations, offsetof(S, VSInvocations));
			push(PipelineStatTypes::GSInvocations, offsetof(S, GSInvocations));
			push(PipelineStatTypes::GSPrimitives, offsetof(S, GSPrimitives));
			push(PipelineStatTypes::TSControlInvocations, offsetof(S, HSInvocations));
			push(PipelineStatTypes::TSEvaluationInvocations, offsetof(S, DSInvocations));
			push(PipelineStatTypes::PSInvocations, offsetof(S, PSInvocations));
			push(PipelineStatTypes::CSInvocations, offsetof(S, CSInvocations));
			// Mesh/Task present:
			push(PipelineStatTypes::TaskInvocations, offsetof(S, ASInvocations));
			push(PipelineStatTypes::MeshInvocations, offsetof(S, MSInvocations));
			push(PipelineStatTypes::MeshPrimitives, offsetof(S, MSPrimitives));
		}

		// Copy out
		const uint32_t n = std::min<uint32_t>(cap, (uint32_t)tmp.size());
		if (outBuf && n) std::memcpy(outBuf, tmp.data(), n * sizeof(tmp[0]));
		// Return layout header: info + fields span (caller knows cap, we return size via .fields.size)
		L.fields = { outBuf, n };
		return L;
	}

	// ---------------- Queue vtable funcs ----------------
	static Result q_submit(Queue* q, Span<CommandList> lists, const SubmitDesc& s) noexcept {
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		auto* dev = qs->owner; if (!dev) return Result::Failed;

		// Pre-waits
		for (auto& w : s.waits) {
			auto* TL = dev->timelines.get(w.t); if (!TL) return Result::InvalidArg;
			if (FAILED(qs->q->Wait(TL->fence.Get(), w.value))) return Result::Failed;
		}

		// Execute command lists
		std::vector<ID3D12CommandList*> native; native.reserve(lists.size);
		for (auto& L : lists) {
			auto* w = static_cast<Dx12CommandList*>(L.impl);
			native.push_back(w->cl.Get());
		}
		if (!native.empty()) qs->q->ExecuteCommandLists((UINT)native.size(), native.data());

		// Post-signals
		for (auto& sgn : s.signals) {
			auto* TL = dev->timelines.get(sgn.t); if (!TL) return Result::InvalidArg;
			if (FAILED(qs->q->Signal(TL->fence.Get(), sgn.value))) return Result::Failed;
		}
		return Result::Ok;
	}

	static Result q_signal(Queue* q, const TimelinePoint& p) noexcept {
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		auto* dev = qs->owner; if (!dev) return Result::Failed;
		auto* TL = dev->timelines.get(p.t); if (!TL) return Result::InvalidArg;
		return SUCCEEDED(qs->q->Signal(TL->fence.Get(), p.value)) ? Result::Ok : Result::Failed;
	}

	static Result q_wait(Queue* q, const TimelinePoint& p) noexcept {
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		auto* dev = qs->owner; if (!dev) return Result::Failed;
		auto* TL = dev->timelines.get(p.t); if (!TL) return Result::InvalidArg;
		return SUCCEEDED(qs->q->Wait(TL->fence.Get(), p.value)) ? Result::Ok : Result::Failed;
	}

	// ---------------- CommandList vtable funcs ----------------

	static inline UINT MipDim(UINT base, UINT mip) {
		return (std::max)(1u, base >> mip);
	}

	static inline UINT CalcSubresourceFor(const rhi::Dx12Texture& T, UINT mip, UINT arraySlice) {
		// PlaneSlice = 0 (non-planar). TODO: support planar formats
		return D3D12CalcSubresource(mip, arraySlice, 0, T.mips, T.arraySize);
	}

	static void cl_end(CommandList* cl) noexcept {
		auto* w = static_cast<Dx12CommandList*>(cl->impl);
		w->cl->Close();
	}
	static void cl_reset(CommandList* cl, CommandAllocator& ca) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* a = static_cast<Dx12Allocator*>(ca.impl);
		if (!l) return;
		if (!a) return;
		l->cl->Reset(a->alloc.Get(), nullptr);
	}
	static void cl_beginPass(CommandList* cl, const PassBeginInfo& p) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		if (!l) return;

		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
		rtvs.reserve(p.colors.size);
		for (uint32_t i = 0; i < p.colors.size; ++i) {
			D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
			if (DxGetDstCpu(l->dev, p.colors.data[i].rtv, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_RTV)) {
				rtvs.push_back(cpu);
				if (p.colors.data[i].loadOp == LoadOp::Clear) {
					l->cl->ClearRenderTargetView(cpu, p.colors.data[i].clear.rgba, 0, nullptr);
				}
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
		const D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
		if (p.depth) {
			if (DxGetDstCpu(l->dev, p.depth->dsv, dsv, D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) {
				pDsv = &dsv;
				if (p.depth->depthLoad == LoadOp::Clear || p.depth->stencilLoad == LoadOp::Clear) {
					const auto& c = p.depth->clear;
					l->cl->ClearDepthStencilView(
						dsv,
						(p.depth->depthLoad == LoadOp::Clear ? D3D12_CLEAR_FLAG_DEPTH : (D3D12_CLEAR_FLAGS)0) |
						(p.depth->stencilLoad == LoadOp::Clear ? D3D12_CLEAR_FLAG_STENCIL : (D3D12_CLEAR_FLAGS)0),
						c.depthStencil.depth, c.depthStencil.stencil, 0, nullptr);
				}
			}
		}

		l->cl->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), FALSE, pDsv);
		D3D12_VIEWPORT vp{ 0,0,(float)p.width,(float)p.height,0.0f,1.0f };
		D3D12_RECT sc{ 0,0,(LONG)p.width,(LONG)p.height };
		l->cl->RSSetViewports(1, &vp);
		l->cl->RSSetScissorRects(1, &sc);
	}

	static void cl_endPass(CommandList* /*cl*/) noexcept {} // nothing to do in DX12
	static void cl_bindLayout(CommandList* cl, PipelineLayoutHandle layoutH) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto dev = l->dev;
		if (auto* L = dev->pipelineLayouts.get(layoutH)) {
			ID3D12RootSignature* rs = L->root.Get();
			// TODO: Is this allowed?
			l->cl->SetGraphicsRootSignature(rs);
			l->cl->SetComputeRootSignature(rs);
		}
	}
	static void cl_bindPipeline(CommandList* cl, PipelineHandle psoH) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev;
		if (auto* P = dev->pipelines.get(psoH)) {
			l->cl->SetPipelineState(P->pso.Get());
		}
	}
	static void cl_setVB(CommandList* cl, uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		std::vector<D3D12_VERTEX_BUFFER_VIEW> views; views.resize(numViews);
		auto* dev = l->dev;
		for (uint32_t i = 0; i < numViews; ++i) {
			if (auto* B = dev->buffers.get(pBufferViews[i].buffer)) {
				views[i].BufferLocation = B->res->GetGPUVirtualAddress() + pBufferViews[i].offset;
				views[i].SizeInBytes = pBufferViews[i].sizeBytes;
				views[i].StrideInBytes = pBufferViews[i].stride;
			}
		}
		l->cl->IASetVertexBuffers(startSlot, (UINT)numViews, views.data());
	}
	static void cl_setIB(CommandList* cl, ResourceHandle b, uint64_t offset, uint32_t sizeBytes, bool idx32) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev;
		if (auto* B = dev->buffers.get(b)) {
			D3D12_INDEX_BUFFER_VIEW ibv{};
			ibv.BufferLocation = B->res->GetGPUVirtualAddress() + offset;
			ibv.SizeInBytes = sizeBytes;
			ibv.Format = idx32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
			l->cl->IASetIndexBuffer(&ibv);
		}
	}
	static void cl_draw(CommandList* cl, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		l->cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		l->cl->DrawInstanced(vc, ic, fv, fi);
	}
	static void cl_drawIndexed(CommandList* cl, uint32_t ic, uint32_t inst, uint32_t firstIdx, int32_t vtxOff, uint32_t firstInst) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl); 
		l->cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		l->cl->DrawIndexedInstanced(ic, inst, firstIdx, vtxOff, firstInst);
	}
	static void cl_dispatch(CommandList* cl, uint32_t x, uint32_t y, uint32_t z) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		l->cl->Dispatch(x, y, z);
	}
	static void cl_clearView(CommandList* cl, ViewHandle v, const ClearValue& c) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dv = l->dev; 
		auto* view = dv->views.get(v);
		l->cl->ClearRenderTargetView(view->cpu, c.rgba, 0, nullptr);
	}
	static void cl_executeIndirect(
		CommandList* cl,
		CommandSignatureHandle sigH,
		ResourceHandle argBufH, uint64_t argOff,
		ResourceHandle cntBufH, uint64_t cntOff,
		uint32_t maxCount) noexcept
	{
		if (!cl || !cl->impl) return;

		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev;
		if (!dev) return;

		auto* S = dev->commandSignatures.get(sigH);
		if (!S || !S->sig) return;

		auto* argB = dev->buffers.get(argBufH);
		if (!argB || !argB->res) return;

		ID3D12Resource* cntRes = nullptr;
		if (cntBufH.valid()) {
			auto* c = dev->buffers.get(cntBufH);
			if (c && c->res) cntRes = c->res.Get();
		}

		l->cl->ExecuteIndirect(
			S->sig.Get(),
			maxCount,
			argB->res.Get(), argOff,
			cntRes, cntOff);
	}
	static void cl_setDescriptorHeaps(CommandList* cl, DescriptorHeapHandle csu, DescriptorHeapHandle samp) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev;

		ID3D12DescriptorHeap* heaps[2]{};
		UINT n = 0;
		if (auto* H = dev->descHeaps.get(csu))  heaps[n++] = H->heap.Get();
		if (auto* H = dev->descHeaps.get(samp)) heaps[n++] = H->heap.Get();
		if (n) {
			l->cl->SetDescriptorHeaps(n, heaps);
		}
	}

	static void cl_barrier(rhi::CommandList* cl, const rhi::BarrierBatch& b) noexcept {
		if (!cl || !cl->impl) return;
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev;
		if (!dev) return;

		std::vector<D3D12_TEXTURE_BARRIER> tex;
		std::vector<D3D12_BUFFER_BARRIER>  buf;
		std::vector<D3D12_GLOBAL_BARRIER>  glob;

		tex.reserve(b.textures.size);
		buf.reserve(b.buffers.size);
		glob.reserve(b.globals.size);

		// Textures
		for (uint32_t i = 0; i < b.textures.size; ++i) {
			const auto& t = b.textures.data[i];
			auto* T = dev->textures.get(t.texture);
			if (!T || !T->res) continue;

			D3D12_TEXTURE_BARRIER tb{};
			tb.SyncBefore = ToDX(t.beforeSync);
			tb.SyncAfter = ToDX(t.afterSync);
			tb.AccessBefore = ToDX(t.beforeAccess);
			tb.AccessAfter = ToDX(t.afterAccess);
			tb.LayoutBefore = ToDX(t.beforeLayout);
			tb.LayoutAfter = ToDX(t.afterLayout);
			tb.pResource = T->res.Get();
			tb.Subresources = ToDX(t.range);
			tex.push_back(tb);
		}

		// Buffers
		for (uint32_t i = 0; i < b.buffers.size; ++i) {
			const auto& br = b.buffers.data[i];
			auto* B = dev->buffers.get(br.buffer);
			if (!B || !B->res) continue;

			D3D12_BUFFER_BARRIER bb{};
			bb.SyncBefore = ToDX(br.beforeSync);
			bb.SyncAfter = ToDX(br.afterSync);
			bb.AccessBefore = ToDX(br.beforeAccess);
			bb.AccessAfter = ToDX(br.afterAccess);
			bb.pResource = B->res.Get();
			bb.Offset = br.offset;
			bb.Size = br.size;
			buf.push_back(bb);
		}

		// Globals
		for (uint32_t i = 0; i < b.globals.size; ++i) {
			const auto& g = b.globals.data[i];
			D3D12_GLOBAL_BARRIER gb{};
			gb.SyncBefore = ToDX(g.beforeSync);
			gb.SyncAfter = ToDX(g.afterSync);
			gb.AccessBefore = ToDX(g.beforeAccess);
			gb.AccessAfter = ToDX(g.afterAccess);
			glob.push_back(gb);
		}

		// Build groups (one per kind if non-empty)
		std::vector<D3D12_BARRIER_GROUP> groups;
		groups.reserve(3);
		if (!buf.empty()) {
			D3D12_BARRIER_GROUP g{};
			g.Type = D3D12_BARRIER_TYPE_BUFFER;
			g.NumBarriers = (UINT)buf.size();
			g.pBufferBarriers = buf.data();
			groups.push_back(g);
		}
		if (!tex.empty()) {
			D3D12_BARRIER_GROUP g{};
			g.Type = D3D12_BARRIER_TYPE_TEXTURE;
			g.NumBarriers = (UINT)tex.size();
			g.pTextureBarriers = tex.data();
			groups.push_back(g);
		}
		if (!glob.empty()) {
			D3D12_BARRIER_GROUP g{};
			g.Type = D3D12_BARRIER_TYPE_GLOBAL;
			g.NumBarriers = (UINT)glob.size();
			g.pGlobalBarriers = glob.data();
			groups.push_back(g);
		}

		if (!groups.empty()) {
			l->cl->Barrier((UINT)groups.size(), groups.data());
		}
	}

	static void cl_clearUavUint(rhi::CommandList* cl,
		const rhi::UavClearInfo& u,
		const rhi::UavClearUint& v) noexcept
	{
		auto* rec = static_cast<rhi::Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev : nullptr;
		if (!rec || !impl) return;

		// Resolve the two matching descriptors
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		if (!DxGetDstCpu(impl, u.cpuVisible, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return;
		if (!DxGetDstGpu(impl, u.shaderVisible, gpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return;

		// Resource to clear
		
		ID3D12Resource* res = nullptr;
		if (u.resource.IsTexture()) {
			res = impl->textures.get(u.resource.GetHandle())->res.Get();
		} else {
			res = impl->buffers.get(u.resource.GetHandle())->res.Get();
		}
		if (!res) return;

		// NOTE: caller must have bound the shader-visible heap via SetDescriptorHeaps()
		// and transitioned 'res' to UAV/UNORDERED_ACCESS with your enhanced barriers.
		rec->cl->ClearUnorderedAccessViewUint(gpu, cpu, res, v.v, 0, nullptr);
	}

	static void cl_clearUavFloat(rhi::CommandList* cl,
		const rhi::UavClearInfo& u,
		const rhi::UavClearFloat& v) noexcept
	{
		auto* rec = static_cast<rhi::Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev : nullptr;
		if (!rec || !impl) return;

		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		if (!DxGetDstCpu(impl, u.cpuVisible, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return;
		if (!DxGetDstGpu(impl, u.shaderVisible, gpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return;

		ID3D12Resource* res = nullptr;
		if (u.resource.IsTexture()) {
			res = impl->textures.get(u.resource.GetHandle())->res.Get();
		}
		else {
			res = impl->buffers.get(u.resource.GetHandle())->res.Get();
		}
		if (!res) return;

		rec->cl->ClearUnorderedAccessViewFloat(gpu, cpu, res, v.v, 0, nullptr);
	}

	static void cl_copyBufferToTexture(CommandList* cl, const TextureCopyRegion& dst, const BufferTextureCopy& src) noexcept {
		if (!cl || !cl->impl) return;
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = rec->dev;
		if (!dev) return;

		// Resolve destination texture & source buffer
		auto* T = dev->textures.get(dst.texture);
		auto* B = dev->buffers.get(src.buffer);
		if (!T || !T->res || !B || !B->res) return;

		// Compute subresource index (plane slice = 0 for non-planar formats)
		const UINT plane = 0;
		UINT subresource = 0;
		if (T->dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
			subresource = D3D12CalcSubresource(dst.mip, /*arraySlice*/0, plane, T->mips, /*ArraySize*/1);
		}
		else {
			// 1D/2D/2DArray/Cube/CubeArray: array slice is meaningful
			subresource = D3D12CalcSubresource(dst.mip, dst.arraySlice, plane, T->mips, T->arraySize);
		}

		// Destination: texture subresource
		D3D12_TEXTURE_COPY_LOCATION dstLoc{};
		dstLoc.pResource = T->res.Get();
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = subresource;

		// Source: buffer with placed footprint
		D3D12_TEXTURE_COPY_LOCATION srcLoc{};
		srcLoc.pResource = B->res.Get();
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.PlacedFootprint.Offset = src.offset;
		srcLoc.PlacedFootprint.Footprint.Format = T->fmt;
		srcLoc.PlacedFootprint.Footprint.Width = dst.width;
		srcLoc.PlacedFootprint.Footprint.Height = dst.height;
		srcLoc.PlacedFootprint.Footprint.Depth = dst.depth;   // usually 1 for 2D uploads; >1 for 3D slices
		srcLoc.PlacedFootprint.Footprint.RowPitch = src.rowPitch; // must satisfy D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256)

		// pSrcBox must be nullptr when source is a buffer (footprint defines size)
		rec->cl->CopyTextureRegion(&dstLoc, dst.x, dst.y, dst.z, &srcLoc, nullptr);
	}

	static void cl_copyTextureRegion(
		rhi::CommandList* cl,
		const rhi::TextureCopyRegion& dst,
		const rhi::TextureCopyRegion& src) noexcept
	{
		auto* rec = static_cast<rhi::Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev : nullptr;
		if (!rec || !impl) return;

		auto* DstT = impl->textures.get(dst.texture);
		auto* SrcT = impl->textures.get(src.texture);
		if (!DstT || !SrcT || !DstT->res || !SrcT->res) return;

		// Build D3D12 copy locations
		D3D12_TEXTURE_COPY_LOCATION dxDst{};
		dxDst.pResource = DstT->res.Get();
		dxDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dxDst.SubresourceIndex = CalcSubresourceFor(*DstT, dst.mip,
			(DstT->dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 0u : dst.arraySlice);

		D3D12_TEXTURE_COPY_LOCATION dxSrc{};
		dxSrc.pResource = SrcT->res.Get();
		dxSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dxSrc.SubresourceIndex = CalcSubresourceFor(*SrcT, src.mip,
			(SrcT->dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 0u : src.arraySlice);

		// If width/height/depth are zero, treat them as "copy full mip slice from src starting at (src.x,src.y,src.z)"
		UINT srcW = src.width ? src.width : MipDim(SrcT->w, src.mip);
		UINT srcH = src.height ? src.height : MipDim(SrcT->h, src.mip);
		UINT srcD = src.depth ? src.depth : ((SrcT->dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			? MipDim(SrcT->depth, src.mip) : 1u);

		// Clamp box to the src subresource bounds just in case (optional; D3D will also validate)
		if (SrcT->dim != D3D12_RESOURCE_DIMENSION_TEXTURE3D) { srcD = 1; }

		D3D12_BOX srcBox{};
		srcBox.left = src.x;
		srcBox.top = src.y;
		srcBox.front = src.z;
		srcBox.right = src.x + srcW;
		srcBox.bottom = src.y + srcH;
		srcBox.back = src.z + srcD;

		// Perform the copy
		// NOTE: Resources must already be in COPY_SOURCE / COPY_DEST layouts respectively.
		rec->cl->CopyTextureRegion(
			&dxDst,
			dst.x, dst.y, dst.z,   // destination offsets
			&dxSrc,
			&srcBox);
	}
	static void cl_setName(CommandList* cl, const char* n) noexcept {
		if (!n) return;
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		if (!l || !l->cl) return;
		l->cl->SetName(std::wstring(n, n + ::strlen(n)).c_str());
	}

	static void cl_writeTimestamp(CommandList* cl, QueryPoolHandle pool, uint32_t index) noexcept {
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev : nullptr;
		if (!impl) return;

		auto* P = impl->queryPools.get(pool);
		if (!P || P->type != D3D12_QUERY_HEAP_TYPE_TIMESTAMP) return;

		rec->cl->EndQuery(P->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
	}

	static void cl_beginQuery(CommandList* cl, QueryPoolHandle pool, uint32_t index) noexcept {
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev : nullptr;
		if (!impl) return;

		auto* P = impl->queryPools.get(pool);
		if (!P) return;

		if (P->type == D3D12_QUERY_HEAP_TYPE_OCCLUSION) {
			rec->cl->BeginQuery(P->heap.Get(), D3D12_QUERY_TYPE_OCCLUSION, index);
		}
		else if (P->type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) {
			rec->cl->BeginQuery(P->heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, index);
		}
		else if (P->type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1) {
			rec->cl->BeginQuery(P->heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, index);
		}
	}

	static void cl_endQuery(CommandList* cl, QueryPoolHandle pool, uint32_t index) noexcept {
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev : nullptr;
		if (!impl) return;

		auto* P = impl->queryPools.get(pool);
		if (!P) return;

		if (P->type == D3D12_QUERY_HEAP_TYPE_OCCLUSION) {
			rec->cl->EndQuery(P->heap.Get(), D3D12_QUERY_TYPE_OCCLUSION, index);
		}
		else if (P->type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) {
			rec->cl->EndQuery(P->heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, index);
		}
		else if (P->type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1) {
			rec->cl->EndQuery(P->heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, index);
		}
		else if (P->type == D3D12_QUERY_HEAP_TYPE_TIMESTAMP) {
			// no-op; timestamps use writeTimestamp (EndQuery(TIMESTAMP))
		}
	}

	static void cl_resolveQueryData(CommandList* cl,
		QueryPoolHandle pool,
		uint32_t firstQuery, uint32_t queryCount,
		ResourceHandle dst, uint64_t dstOffset) noexcept
	{
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev : nullptr;
		if (!impl) return;

		auto* P = impl->queryPools.get(pool);
		if (!P) return;

		// Resolve to the given buffer (assumed COPY_DEST)
		auto* B = impl->buffers.get(dst);
		if (!B || !B->res) return;

		D3D12_QUERY_TYPE type{};
		switch (P->type) {
		case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:               type = D3D12_QUERY_TYPE_TIMESTAMP; break;
		case D3D12_QUERY_HEAP_TYPE_OCCLUSION:               type = D3D12_QUERY_TYPE_OCCLUSION; break;
		case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:     type = D3D12_QUERY_TYPE_PIPELINE_STATISTICS; break;
		case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1:    type = D3D12_QUERY_TYPE_PIPELINE_STATISTICS1; break;
		default: return;
		}

		rec->cl->ResolveQueryData(P->heap.Get(), type, firstQuery, queryCount, B->res.Get(), dstOffset);
	}

	static void cl_resetQueries(CommandList* /*cl*/, QueryPoolHandle /*pool*/, uint32_t /*first*/, uint32_t /*count*/) noexcept {
		// D3D12 does not require resets; Vulkan impl will fill this.
	}


	// ---------------- Swapchain vtable funcs ----------------
	static uint32_t sc_count(Swapchain* sc) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->count; }
	static uint32_t sc_curr(Swapchain* sc) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->sc->GetCurrentBackBufferIndex(); }
	static ViewHandle sc_rtv(Swapchain* sc, uint32_t i) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->rtvHandles[i]; }
	static ResourceHandle sc_img(Swapchain* sc, uint32_t i) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->imageHandles[i]; }
	static Result sc_present(Swapchain* sc, bool vsync) noexcept {
		auto* s = static_cast<Dx12Swapchain*>(sc->impl);
		UINT sync = vsync ? 1 : 0; UINT flags = 0;
		return s->sc->Present(sync, flags) == S_OK ? Result::Ok : Result::Failed;
	}
	static Result sc_resizeBuffers(Swapchain* sc, uint32_t numBuffers, uint32_t w, uint32_t h, Format newFormat, uint32_t flags) noexcept {
		auto* s = static_cast<Dx12Swapchain*>(sc->impl);
		s->fmt = ToDxgi(newFormat);
		s->count = numBuffers;
		s->sc->ResizeBuffers(s->count, w, h, s->fmt, flags); // TODO: Is there anything else to do here?
	}
	static void sc_setName(Swapchain* sc, const char* n) noexcept {} // Cannot name IDXGISwapChain
	// ---------------- Resource vtable funcs ----------------

	static void buf_map(Resource* r, void** data, uint64_t offset, uint64_t size) noexcept {
		if (!r || !data) return;
		auto* B = static_cast<Dx12Buffer*>(r->impl);
		if (!B || !B->res) { *data = nullptr; return; }

		D3D12_RANGE readRange{};
		D3D12_RANGE* pRange = nullptr;
		if (size != ~0ull) {
			readRange.Begin = SIZE_T(offset);
			readRange.End = SIZE_T(offset + size);
			pRange = &readRange;
		}

		void* ptr = nullptr;
		HRESULT hr = B->res->Map(0, pRange, &ptr);
		*data = SUCCEEDED(hr) ? ptr : nullptr;
	}

	static void buf_unmap(Resource* r, uint64_t writeOffset, uint64_t writeSize) noexcept {
		auto* B = static_cast<Dx12Buffer*>(r->impl);
		if (!B || !B->res) return;
		D3D12_RANGE range{};
		if (writeSize != ~0ull) {
			range.Begin = SIZE_T(writeOffset);
			range.End = SIZE_T(writeOffset + writeSize);
		}
		B->res->Unmap(0, (writeSize != ~0ull) ? &range : nullptr);
	}

	static void buf_setName(Resource* r, const char* n) noexcept {
		if (!n) return;
		auto* B = static_cast<Dx12Buffer*>(r->impl);
		if (!B || !B->res) return;
		B->res->SetName(s2ws(n).c_str());
	}

	static void tex_map(Resource* r, void** data, uint64_t /*offset*/, uint64_t /*size*/) noexcept {
		if (!r || !data) return;
		auto* T = static_cast<Dx12Texture*>(r->impl);
		if (!T || !T->res) { *data = nullptr; return; }

		// NOTE: Texture mapping is only valid on UPLOAD/READBACK heaps.
		// This returns a pointer to subresource 0 memory. Caller must compute
		// row/slice offsets via GetCopyableFootprints.
		void* ptr = nullptr;
		HRESULT hr = T->res->Map(0, nullptr, &ptr);
		*data = SUCCEEDED(hr) ? ptr : nullptr;
	}
	static void tex_unmap(Resource* r, uint64_t writeOffset, uint64_t writeSize) noexcept {
		auto* T = static_cast<Dx12Texture*>(r->impl);
		if (T && T->res) T->res->Unmap(0, nullptr);
	}

	static void tex_setName(Resource* r, const char* n) noexcept {
		if (!n) return;
		auto* T = static_cast<Dx12Texture*>(r->impl);
		if (!T || !T->res) return;
		T->res->SetName(s2ws(n).c_str());
	}

	DevicePtr CreateD3D12Device(const DeviceCreateInfo& ci) noexcept {
		UINT flags = 0;
#ifdef _DEBUG
		if (ci.enableDebug) { ComPtr<ID3D12Debug> dbg; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer(), flags |= DXGI_CREATE_FACTORY_DEBUG; }
#endif
		auto* impl = new Dx12Device();
		CreateDXGIFactory2(flags, IID_PPV_ARGS(&impl->factory));

		// Select default adapter. TODO: expose adapter selection
		ComPtr<IDXGIAdapter1> adapter; 
		impl->factory->EnumAdapters1(0, &adapter);

		adapter.As(&impl->adapter);

		D3D12CreateDevice(impl->adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&impl->dev));
		EnableDebug(impl->dev.Get());

#if BUILD_TYPE == BUILD_TYPE_DEBUG
		ComPtr<ID3D12InfoQueue1> infoQueue;
		if (SUCCEEDED(impl->dev->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

			DWORD callbackCookie = 0;
			infoQueue->RegisterMessageCallback([](D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context) {
				// Log or print the debug messages,
				spdlog::error("D3D12 Debug Message: {}", description);
				}, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &callbackCookie);
		}
#endif

		// Disable unwanted warnings
		ComPtr<ID3D12InfoQueue> warningInfoQueue;
		if (SUCCEEDED(impl->dev->QueryInterface(IID_PPV_ARGS(&warningInfoQueue))))
		{
			D3D12_INFO_QUEUE_FILTER filter = {};
			D3D12_MESSAGE_ID blockedIDs[] = {
				(D3D12_MESSAGE_ID)1356, // Barrier-only command lists
				(D3D12_MESSAGE_ID)1328, // ps output type mismatch
				(D3D12_MESSAGE_ID)1008 }; // RESOURCE_BARRIER_DUPLICATE_SUBRESOURCE_TRANSITIONS
			filter.DenyList.NumIDs = _countof(blockedIDs);
			filter.DenyList.pIDList = blockedIDs;

			warningInfoQueue->AddStorageFilterEntries(&filter);
		}

		auto makeQ = [&](D3D12_COMMAND_LIST_TYPE t, Dx12QueueState& out) {
			D3D12_COMMAND_QUEUE_DESC qd{};
			qd.Type = t;
			impl->dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&out.q));
			impl->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&out.fence));
			out.value = 0;
			};
		makeQ(D3D12_COMMAND_LIST_TYPE_DIRECT, impl->gfx);
		makeQ(D3D12_COMMAND_LIST_TYPE_COMPUTE, impl->comp);
		makeQ(D3D12_COMMAND_LIST_TYPE_COPY, impl->copy);

		Device d{};
		d.impl = impl;
		d.vt = &g_devvt;
		return MakeDevicePtr(d);
	}

	// ------------------ Allocator vtable funcs ----------------
	static void ca_reset(CommandAllocator* ca) noexcept {
		if (!ca || !ca->impl) return;
		auto* A = static_cast<Dx12Allocator*>(ca->impl);
		A->alloc->Reset(); // ID3D12CommandAllocator::Reset()
	}

} // namespace rhi

#endif // _WIN32