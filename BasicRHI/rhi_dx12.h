#include "rhi.h"

#ifdef _WIN32
#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <vector>
#include <cassert>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) if(p){ (p)->Release(); (p)=nullptr; }
#endif

namespace rhi {
	using Microsoft::WRL::ComPtr;

	struct Dx12Buffer { ComPtr<ID3D12Resource> res; }; // trimmed for demo
	struct Dx12Texture { ComPtr<ID3D12Resource> res; DXGI_FORMAT fmt{ DXGI_FORMAT_UNKNOWN }; uint32_t w = 0, h = 0; };
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

static D3D12_FILL_MODE  ToDx(FillMode f) { return f == FillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID; }
static D3D12_CULL_MODE  ToDx(CullMode c) { 
	switch (c) { 
		case CullMode::None:return D3D12_CULL_MODE_NONE; 
		case CullMode::Front:return D3D12_CULL_MODE_FRONT; 
		default:return D3D12_CULL_MODE_BACK; 
	} 
}
static D3D12_COMPARISON_FUNC ToDx(CompareOp c) { 
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
static DXGI_FORMAT ToDxgi(Format f) { 
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

static D3D12_BLEND ToDx(BlendFactor f) {
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
static D3D12_BLEND_OP ToDx(BlendOp o) { using O = D3D12_BLEND_OP; switch (o) { case BlendOp::Add:return O::D3D12_BLEND_OP_ADD; case BlendOp::Sub:return O::D3D12_BLEND_OP_SUBTRACT; case BlendOp::RevSub:return O::D3D12_BLEND_OP_REV_SUBTRACT; case BlendOp::Min:return O::D3D12_BLEND_OP_MIN; case BlendOp::Max:return O::D3D12_BLEND_OP_MAX; } return O::D3D12_BLEND_OP_ADD; }


	// tiny handle registry
	template<typename T>
	struct Slot { T obj{}; uint32_t generation{ 1 }; bool alive{ false }; };

	template<typename T>
	struct Registry {
		std::vector<Slot<T>> slots; std::vector<uint32_t> freelist;
		Handle32 alloc(const T& v) {
			if (!freelist.empty()) {
				auto i = freelist.back();
				freelist.pop_back();
				auto& s = slots[i];
				s.obj = v;
				s.alive = true;
				++s.generation;
				return { i,s.generation };
			}
			auto i = (uint32_t)slots.size();
			slots.push_back({ v,1u,true });
			return { i,1u };
		}
		void free(Handle32 h) {
			if (h.index >= slots.size()) {
				return;
			}
			auto& s = slots[h.index];
			if (!s.alive || s.generation != h.generation) {
				return;
			}
			s.alive = false;
			freelist.push_back(h.index);
		}
		T* get(Handle32 h) {
			if (h.index >= slots.size()) {
				return nullptr;
			}
			auto& s = slots[h.index];
			if (!s.alive || s.generation != h.generation) {
				return nullptr;
			}
			return &s.obj;
		}
	};

	struct Dx12QueueState {
		ComPtr<ID3D12CommandQueue> q;
		ComPtr<ID3D12Fence> fence; UINT64 value = 0;
	};

	struct Dx12Swapchain {
		ComPtr<IDXGISwapChain3> sc; DXGI_FORMAT fmt{}; UINT w{}, h{}, count{}; UINT current{};
		ComPtr<ID3D12DescriptorHeap> rtvHeap; UINT rtvInc{}; std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvCPU;
		std::vector<ComPtr<ID3D12Resource>> images;
		std::vector<TextureHandle> imageHandles; std::vector<ViewHandle> rtvHandles;
	};

	struct Dx12Device {
		Device self{};
		ComPtr<IDXGIFactory7> factory; ComPtr<ID3D12Device10> dev;
		Registry<Dx12Buffer> buffers;
		Registry<Dx12Texture> textures;
		Registry<Dx12View> views;
		Registry<Dx12Sampler> samplers;
		Registry<Dx12PipelineLayout> pipelineLayouts;
		Registry<Dx12Pipeline> pipelines;
		Registry<Dx12CommandSignature> commandSignatures;

		Dx12QueueState gfx{}, comp{}, copy{};

		// Simple CL pool: one allocator per CL for the demo
		struct CL { ComPtr<ID3D12CommandAllocator> alloc; ComPtr<ID3D12GraphicsCommandList7> cl; };
		std::vector<CL> liveCLs; // demo only
	};



	// ---------------- CommandList wrapper impl ----------------
	struct Dx12CLWrap { Dx12Device* dev{}; QueueKind kind{}; Dx12Device::CL* rec{}; bool recording = false; };

	// ---------------- VTables forward ----------------
	extern const DeviceVTable g_devvt; extern const QueueVTable g_qvt; extern const CommandListVTable g_clvt; extern const SwapchainVTable g_scvt;

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
	static BufferHandle d_createBuffer(Device* d, const BufferDesc& bd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		// Minimal: allocate an upload/committed resource if requested; otherwise stub
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = (bd.memory == Memory::DeviceLocal) ? D3D12_HEAP_TYPE_DEFAULT : (bd.memory == Memory::Upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK);
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = bd.size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc = { 1,0 };
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ComPtr<ID3D12Resource> res;
		HRESULT hr = impl->dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			(heap.Type == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_GENERIC_READ), nullptr, IID_PPV_ARGS(&res));
		if (FAILED(hr)) return {};
		return impl->buffers.alloc(Dx12Buffer{ res });
	}

	static TextureHandle d_createTexture(Device* d, const TextureDesc& td) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = td.width; desc.Height = td.height;
		desc.DepthOrArraySize = (UINT16)td.arrayLayers;
		desc.MipLevels = (UINT16)td.mipLevels;
		desc.SampleDesc = { 1,0 };
		desc.Format = ToDxgi(td.format);
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		ComPtr<ID3D12Resource> res;
		if (FAILED(impl->dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&res)))) {
			return {};
		}
		Dx12Texture t{};
		t.res = res;
		t.fmt = desc.Format;
		t.w = td.width;
		t.h = td.height;
		return impl->textures.alloc(t);
	}

	static ViewHandle d_createView(Device* d, const ViewDesc& vd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* tex = impl->textures.get(vd.texture); if (!tex) return {};

		// Create per-type CPU descriptor on a throwaway heap (engine should manage heaps!) Todo: Abstract heaps
		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.NumDescriptors = 1;
		hd.Type = (vd.kind == ViewKind::RTV ? D3D12_DESCRIPTOR_HEAP_TYPE_RTV : (vd.kind == ViewKind::DSV ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
		ComPtr<ID3D12DescriptorHeap> heap; if (FAILED(impl->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return {};
		auto cpu = heap->GetCPUDescriptorHandleForHeapStart();

		if (vd.kind == ViewKind::RTV) {
			D3D12_RENDER_TARGET_VIEW_DESC rtv{};
			rtv.Format = tex->fmt;
			rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			impl->dev->CreateRenderTargetView(tex->res.Get(), &rtv, cpu);
		}
		else if (vd.kind == ViewKind::DSV) {
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
			dsv.Format = tex->fmt;
			dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			impl->dev->CreateDepthStencilView(tex->res.Get(), &dsv, cpu);
		}
		else {
			//TODO: // SRV/UAV/etc
		}

		Dx12View v{}; v.desc = vd; v.cpu = cpu; return impl->views.alloc(v);
	}

	static SamplerHandle d_createSampler(Device* d, const SamplerDesc& sd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		(void)impl;
		return impl->samplers.alloc(Dx12Sampler{ sd });
	}

	static PipelineHandle d_createPipelineFromStream(Device* d,
		const PipelineStreamItem* items,
		uint32_t count) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);

		// Collect RHI subobjects
		ID3D12RootSignature* root = nullptr;
		D3D12_SHADER_BYTECODE cs{}, vs{}, ps{}, as{}, ms{};
		bool hasCS = false, hasGfx = false;

		D3D12_RASTERIZER_DESC rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		D3D12_BLEND_DESC      blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
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
				for (uint32_t a = 0; a < B.bs.numAttachments && a < 8; a++) {
					const auto& src = B.bs.attachments[a];
					auto& dst = blend.RenderTarget[a];
					dst.BlendEnable = src.enable;
					dst.RenderTargetWriteMask = src.writeMask;
					// (map factors/ops here)
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
			push(rast);
			push(blend);
			push(depth);
			push(rtv);
			push(dsv);
			push(sample);
		}

		D3D12_PIPELINE_STATE_STREAM_DESC sd{};
		sd.pPipelineStateSubobjectStream = stream.data();
		sd.SizeInBytes = (UINT)stream.size();

		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		if (FAILED(impl->dev->CreatePipelineState(&sd, IID_PPV_ARGS(&pso)))) return {};

		return impl->pipelines.alloc(Dx12Pipeline{ pso, isCompute });
	}

	static void d_destroyPipeline(Device* d, PipelineHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->pipelines.free(h);
	}

	static void d_destroyBuffer(Device* d, BufferHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->buffers.free(h); }
	static void d_destroyTexture(Device* d, TextureHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->textures.free(h); }
	static void d_destroyView(Device* d, ViewHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->views.free(h); }
	static void d_destroySampler(Device* d, SamplerHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->samplers.free(h); }
	static void d_destroyPipeline(Device* d, PipelineHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->pipelines.free(h); }

	static CommandList d_createCommandList(Device* d, QueueKind q) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Dx12Device::CL cl{}; impl->dev->CreateCommandAllocator(
			q == QueueKind::Graphics ? D3D12_COMMAND_LIST_TYPE_DIRECT : q == QueueKind::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_COPY,
			IID_PPV_ARGS(&cl.alloc));
		impl->dev->CreateCommandList(0,
			q == QueueKind::Graphics ? D3D12_COMMAND_LIST_TYPE_DIRECT : q == QueueKind::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_COPY,
			cl.alloc.Get(), nullptr, IID_PPV_ARGS(&cl.cl));
		cl.cl->Close();
		impl->liveCLs.push_back(std::move(cl));
		auto* rec = &impl->liveCLs.back();

		auto* wrap = new Dx12CLWrap{ impl, q, rec, false };
		CommandList out{}; 
		out.impl = wrap; 
		out.vt = &g_clvt; 
		return out;
	}

	static void d_resetCommandList(Device*, CommandList* cl) noexcept {
		auto* w = static_cast<Dx12CLWrap*>(cl->impl); 
		w->rec->alloc->Reset(); 
		w->rec->cl->Reset(w->rec->alloc.Get(), nullptr); 
		w->recording = false;
	}
	static void d_destroyCommandList(Device*, CommandList* cl) noexcept { 
		auto* w = static_cast<Dx12CLWrap*>(cl->impl); 
		delete w; 
		cl->impl = nullptr; 
		cl->vt = nullptr; 
	}

	static Queue d_getQueue(Device* d, QueueKind q) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Queue out{};
		out.vt = &g_qvt;
		out.impl = (q == QueueKind::Graphics ? &impl->gfx : q == QueueKind::Compute ? &impl->comp : &impl->copy);
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
	static Swapchain d_createSwapchain(Device* d, void* hwnd, uint32_t w, uint32_t h, Format fmt, uint32_t bufferCount, bool allowTearing) noexcept {
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

		ComPtr<IDXGISwapChain1> sc1; impl->factory->CreateSwapChainForHwnd(impl->gfx.q.Get(), (HWND)hwnd, &desc, nullptr, nullptr, &sc1);
		ComPtr<IDXGISwapChain3> sc; sc1.As(&sc);

		// RTV heap for backbuffers
		ComPtr<ID3D12DescriptorHeap> rtvHeap;
		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		hd.NumDescriptors = bufferCount; impl->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap));
		UINT inc = impl->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		auto cpuStart = rtvHeap->GetCPUDescriptorHandleForHeapStart();

		std::vector<ComPtr<ID3D12Resource>> imgs(bufferCount);
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvCPU(bufferCount);
		std::vector<TextureHandle> imgHandles(bufferCount);
		std::vector<ViewHandle> rtvHandles(bufferCount);

		for (UINT i = 0; i < bufferCount; i++) {
			sc->GetBuffer(i, IID_PPV_ARGS(&imgs[i]));
			// Register as a TextureHandle
			Dx12Texture t{}; 
			t.res = imgs[i]; 
			t.fmt = desc.Format; 
			t.w = w; 
			t.h = h; 
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
		return out;
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

	static PipelineLayoutHandle d_createPipelineLayout(Device* d, const PipelineLayoutDesc& ld) noexcept {
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

		// Root signature flags (per your assumption)
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs{};
		rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		rs.Desc_1_1.NumParameters = (UINT)params.size();
		rs.Desc_1_1.pParameters = params.data();
		rs.Desc_1_1.NumStaticSamplers = (UINT)ssmps.size();
		rs.Desc_1_1.pStaticSamplers = ssmps.data();
		rs.Desc_1_1.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
			D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

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
		return impl->pipelineLayouts.alloc(std::move(L));
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

	static CommandSignatureHandle d_createCommandSignature(Device* d,
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
		return impl->commandSignatures.alloc(std::move(S));
	}

	static void d_destroyCommandSignature(Device* d, CommandSignatureHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->commandSignatures.free(h);
	}

	// ---------------- Queue vtable funcs ----------------
	static Result q_submit(Queue* q, Span<CommandList> lists, const SubmitDesc&) noexcept {
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		std::vector<ID3D12CommandList*> native; native.reserve(lists.size);
		for (uint32_t i = 0; i < lists.size; i++) {
			auto* w = static_cast<Dx12CLWrap*>(lists.data[i].impl);
			native.push_back(w->rec->cl.Get());
		}
		if (!native.empty()) {
			qs->q->ExecuteCommandLists((UINT)native.size(), native.data());
		}
		return Result::Ok;
	}
	static Result q_signal(Queue* q, const TimelinePoint&) noexcept { 
		(void)q; 
		return Result::Ok; 
	}
	static Result q_wait(Queue* q, const TimelinePoint&) noexcept { 
		(void)q; 
		return Result::Ok; 
	}

	// ---------------- CommandList vtable funcs ----------------
	static void cl_begin(CommandList* cl, const char* /*name*/) noexcept { 
		auto* w = static_cast<Dx12CLWrap*>(cl->impl); 
		w->rec->alloc->Reset(); 
		w->rec->cl->Reset(w->rec->alloc.Get(), nullptr); 
		w->recording = true; 
	}
	static void cl_end(CommandList* cl) noexcept { 
		auto* w = static_cast<Dx12CLWrap*>(cl->impl); 
		w->rec->cl->Close();
		w->recording = false; 
	}
	static void cl_beginPass(CommandList* cl, const PassBeginInfo& p) noexcept {
		auto* w = static_cast<Dx12CLWrap*>(cl->impl); auto* dev = w->dev;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs; rtvs.reserve(p.colors.size);
		for (uint32_t i = 0; i < p.colors.size; i++) {
			auto* v = dev->views.get(p.colors.data[i].view);
			rtvs.push_back(v->cpu);
			if (p.colors.data[i].loadOp == LoadOp::Clear) {
				w->rec->cl->ClearRenderTargetView(v->cpu, p.colors.data[i].clear.rgba, 0, nullptr);
			}
		}
		w->rec->cl->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), FALSE, nullptr);
		D3D12_VIEWPORT vp{ 0,0,(float)p.width,(float)p.height,0.0f,1.0f };
		D3D12_RECT sc{ 0,0,(LONG)p.width,(LONG)p.height };
		w->rec->cl->RSSetViewports(1, &vp);
		w->rec->cl->RSSetScissorRects(1, &sc);
	}

	static void cl_endPass(CommandList* /*cl*/) noexcept {}
	static void cl_barriers(CommandList*, const BarrierBatch&) noexcept {}
	static void cl_bindLayout(CommandList*, PipelineLayoutHandle) noexcept {}
	static void cl_bindPipeline(CommandList*, PipelineHandle) noexcept {}
	static void cl_setVB(CommandList*, uint32_t, BufferHandle, uint64_t, uint32_t) noexcept {}
	static void cl_setIB(CommandList*, BufferHandle, uint64_t, bool) noexcept {}
	static void cl_draw(CommandList* cl, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) noexcept {
		auto* w = static_cast<Dx12CLWrap*>(cl->impl);
		w->rec->cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		w->rec->cl->DrawInstanced(vc, ic, fv, fi);
	}
	static void cl_drawIndexed(CommandList* cl, uint32_t ic, uint32_t inst, uint32_t firstIdx, int32_t vtxOff, uint32_t firstInst) noexcept {
		auto* w = static_cast<Dx12CLWrap*>(cl->impl); w->rec->cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		w->rec->cl->DrawIndexedInstanced(ic, inst, firstIdx, vtxOff, firstInst);
	}
	static void cl_dispatch(CommandList* cl, uint32_t x, uint32_t y, uint32_t z) noexcept {
		auto* w = static_cast<Dx12CLWrap*>(cl->impl);
		w->rec->cl->Dispatch(x, y, z);
	}
	static void cl_clearView(CommandList* cl, ViewHandle v, const ClearValue& c) noexcept {
		auto* w = static_cast<Dx12CLWrap*>(cl->impl);
		auto* dv = w->dev; auto* view = dv->views.get(v);
		w->rec->cl->ClearRenderTargetView(view->cpu, c.rgba, 0, nullptr);
	}

	// ---------------- Swapchain vtable funcs ----------------
	static uint32_t sc_count(Swapchain* sc) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->count; }
	static uint32_t sc_curr(Swapchain* sc) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->sc->GetCurrentBackBufferIndex(); }
	static ViewHandle sc_rtv(Swapchain* sc, uint32_t i) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->rtvHandles[i]; }
	static TextureHandle sc_img(Swapchain* sc, uint32_t i) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->imageHandles[i]; }
	static Result sc_resize(Swapchain* /*sc*/, uint32_t /*w*/, uint32_t /*h*/) noexcept { return Result::Unsupported; } // demo skips resize
	static Result sc_present(Swapchain* sc, bool vsync) noexcept {
		auto* s = static_cast<Dx12Swapchain*>(sc->impl);
		UINT sync = vsync ? 1 : 0; UINT flags = 0;
		return s->sc->Present(sync, flags) == S_OK ? Result::Ok : Result::Failed;
	}

	Device CreateD3D12Device(const DeviceCreateInfo& ci) noexcept {
		UINT flags = 0;
#ifdef _DEBUG
		if (ci.enableDebug) { ComPtr<ID3D12Debug> dbg; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer(), flags |= DXGI_CREATE_FACTORY_DEBUG; }
#endif
		auto* impl = new Dx12Device();
		CreateDXGIFactory2(flags, IID_PPV_ARGS(&impl->factory));

		// Select default adapter (for brevity). You can port your GetMostPowerfulAdapter here.
		ComPtr<IDXGIAdapter1> adapter; impl->factory->EnumAdapters1(0, &adapter);
		D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&impl->dev));
		EnableDebug(impl->dev.Get());

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
		return d;
	}

} // namespace rhi

#endif // _WIN32