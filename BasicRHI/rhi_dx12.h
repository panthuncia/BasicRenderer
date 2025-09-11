#pragma once
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
#include <optional>
#include <deque>
#include <cstddef>  // offsetof
#include <type_traits>
#include <cstdint>

#include "rhi_interop_dx12.h"
#include "rhi_conversions_dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) if(p){ (p)->Release(); (p)=nullptr; }
#endif

inline void BreakIfDebugging() {
#if NDEBUG
	if (IsDebuggerPresent()) {
		__debugbreak();
	}
#endif
}

inline std::wstring s2ws(const std::string & s) {
	int buffSize = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring ws(buffSize, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), buffSize);
	return ws;
}



namespace rhi {
	using Microsoft::WRL::ComPtr;
	struct Dx12Device;

	struct Dx12Buffer { 
		explicit Dx12Buffer(ComPtr<ID3D12Resource> r, std::shared_ptr<Dx12Device> d) : res(r), dev(d) {}
		ComPtr<ID3D12Resource> res;
		std::shared_ptr<Dx12Device> dev;
	};
	struct Dx12Texture { 
		explicit Dx12Texture(ComPtr<ID3D12Resource> r, DXGI_FORMAT f, uint32_t width, uint32_t height, uint16_t mips, uint16_t arraySize, D3D12_RESOURCE_DIMENSION dim, uint16_t depth, std::shared_ptr<Dx12Device> d) 
			: res(r), fmt(f), w(width), h(height), mips(mips), arraySize(arraySize), dim(dim), depth(depth), dev(d) {
		}
		ComPtr<ID3D12Resource> res; 
		DXGI_FORMAT fmt{ DXGI_FORMAT_UNKNOWN }; 
		uint32_t w = 0, h = 0; 
		uint16_t mips = 1;
		uint16_t arraySize = 1; // for 1D/2D/cube (cube arrays should already multiply by 6)
		D3D12_RESOURCE_DIMENSION dim = D3D12_RESOURCE_DIMENSION_UNKNOWN;
		uint16_t depth = 1;
		std::shared_ptr<Dx12Device> dev;
	};
	struct Dx12Sampler { 
		explicit Dx12Sampler(SamplerDesc d, std::shared_ptr<Dx12Device> device) : desc(d), dev(device) {}
		SamplerDesc desc;
		std::shared_ptr<Dx12Device> dev;
	};
	struct Dx12Pipeline { 
		explicit Dx12Pipeline(Microsoft::WRL::ComPtr<ID3D12PipelineState> p, bool isComp, std::shared_ptr<Dx12Device> device) : pso(p), isCompute(isComp), dev(device) {}
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso; 
		bool isCompute; 
		std::shared_ptr<Dx12Device> dev;
	};
	struct Dx12PipelineLayout {
		explicit Dx12PipelineLayout(const PipelineLayoutDesc& d, std::shared_ptr<Dx12Device> device)
			: desc(d), dev(device) {
			// build root constant param lookup
			for (uint32_t i = 0; i < pcs.size(); ++i) {
				const auto& p = pcs[i];
				RootConstParam rcp;
				rcp.set = p.set;
				rcp.binding = p.binding;
				rcp.num32 = p.num32BitValues;
				rcp.rootIndex = i; // assume order is preserved
				rcParams.push_back(rcp);
			}
		}
		PipelineLayoutDesc desc;
		std::vector<PushConstantRangeDesc> pcs;
		std::vector<StaticSamplerDesc> staticSamplers;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
		struct RootConstParam {
			uint32_t set;
			uint32_t binding;
			uint32_t num32;      // max 32-bit values in this range
			uint32_t rootIndex;  // root parameter index in this RS
		};
		std::vector<RootConstParam> rcParams;
		std::shared_ptr<Dx12Device> dev;
	};

	struct Dx12CommandSignature {
		explicit Dx12CommandSignature(Microsoft::WRL::ComPtr<ID3D12CommandSignature> s, uint32_t str, std::shared_ptr<Dx12Device> device) : sig(s), stride(str), dev(device) {}
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> sig;
		uint32_t stride = 0;
		std::shared_ptr<Dx12Device> dev;
	};

	struct Dx12Allocator {
		explicit Dx12Allocator(Microsoft::WRL::ComPtr<ID3D12CommandAllocator> a, D3D12_COMMAND_LIST_TYPE t, std::shared_ptr<Dx12Device> d) : alloc(a), type(t), dev(d) {}
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc; 
		D3D12_COMMAND_LIST_TYPE type{}; 
		std::shared_ptr<Dx12Device> dev;
	};
	struct Dx12CommandList { 
		explicit Dx12CommandList(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> c, Microsoft::WRL::ComPtr<ID3D12CommandAllocator> a, D3D12_COMMAND_LIST_TYPE t, std::shared_ptr<Dx12Device> d)
			: cl(c), alloc(a), type(t), dev(d) {
		}
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> cl; 
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
		D3D12_COMMAND_LIST_TYPE type{}; 
		PipelineLayoutHandle boundLayout{};
		Dx12PipelineLayout* boundLayoutPtr = nullptr;
		std::shared_ptr<Dx12Device> dev;
	};

	// Build D3D12_RESOURCE_DESC1 for buffers
	static D3D12_RESOURCE_DESC1 MakeBufferDesc1(uint64_t bytes, D3D12_RESOURCE_FLAGS flags) {
		CD3DX12_RESOURCE_DESC1 d = CD3DX12_RESOURCE_DESC1::Buffer(bytes, flags);
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

	struct Dx12DescriptorHeap {
		explicit Dx12DescriptorHeap(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> h, D3D12_DESCRIPTOR_HEAP_TYPE t, UINT incrementSize, bool sv, std::shared_ptr<Dx12Device> d)
			: heap(h), type(t), inc(incrementSize), shaderVisible(sv), dev(d) {
			cpuStart = heap->GetCPUDescriptorHandleForHeapStart();
			if (shaderVisible) {
				gpuStart = heap->GetGPUDescriptorHandleForHeapStart();
			}
		}
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		D3D12_DESCRIPTOR_HEAP_TYPE type{};
		UINT inc{ 0 };
		bool shaderVisible{ false };
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
		std::shared_ptr<Dx12Device> dev;
	};

	struct Dx12Timeline { 
		explicit Dx12Timeline(Microsoft::WRL::ComPtr<ID3D12Fence> f, std::shared_ptr<Dx12Device> d) : fence(f), dev(d) {}
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		std::shared_ptr<Dx12Device> dev;
	};

	struct Dx12QueueState {
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> q;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence; 
		UINT64 value = 0;
		std::shared_ptr<Dx12Device> dev;
	};

	struct Dx12Swapchain {
		explicit Dx12Swapchain(ComPtr<IDXGISwapChain3> s, DXGI_FORMAT f, UINT width, UINT height, UINT c,
			std::vector<ComPtr<ID3D12Resource>> images,
			std::vector<ResourceHandle> imageHandles, 
			std::shared_ptr<Dx12Device> d)
			: sc(s), fmt(f), w(width), h(height), count(c), images(images), imageHandles(imageHandles), dev(d) {
		}
		ComPtr<IDXGISwapChain3> sc; 
		DXGI_FORMAT fmt{}; 
		UINT w{}, h{}, count{}; 
		UINT current{};
		std::vector<ComPtr<ID3D12Resource>> images;
		std::vector<ResourceHandle> imageHandles; 
		std::shared_ptr<Dx12Device> dev;
	};

	struct Dx12Heap { 
		explicit Dx12Heap(Microsoft::WRL::ComPtr<ID3D12Heap> h, uint64_t s, std::shared_ptr<Dx12Device> d) : heap(h), size(s), dev(d) {}
		Microsoft::WRL::ComPtr<ID3D12Heap> heap; 
		uint64_t size{};
		std::shared_ptr<Dx12Device> dev;
	};

	struct Dx12QueryPool {
		explicit Dx12QueryPool(Microsoft::WRL::ComPtr<ID3D12QueryHeap> h, D3D12_QUERY_HEAP_TYPE t, uint32_t c, std::shared_ptr<Dx12Device> d) : heap(h), type(t), count(c), dev(d) {}
		Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
		D3D12_QUERY_HEAP_TYPE type{};
		uint32_t count = 0;

		// For pipeline stats, remember if we used *_STATISTICS1 (mesh/task) or legacy
		bool usePSO1 = false;
		std::shared_ptr<Dx12Device> dev;
	};

	// tiny handle registry
	template<class Obj> struct HandleFor;  // no default

	template<> struct HandleFor<Dx12Buffer> { using type = ResourceHandle; };
	template<> struct HandleFor<Dx12Texture> { using type = ResourceHandle; };
	template<> struct HandleFor<Dx12Sampler> { using type = SamplerHandle; };
	template<> struct HandleFor<Dx12PipelineLayout> { using type = PipelineLayoutHandle; };
	template<> struct HandleFor<Dx12Pipeline> { using type = PipelineHandle; };
	template<> struct HandleFor<Dx12CommandSignature> { using type = CommandSignatureHandle; };
	template<> struct HandleFor<Dx12DescriptorHeap> { using type = DescriptorHeapHandle; };
	template<> struct HandleFor<Dx12Timeline> { using type = TimelineHandle; };
	template<> struct HandleFor<Dx12Allocator> { using type = CommandAllocatorHandle; };
	template<> struct HandleFor<Dx12CommandList> { using type = CommandListHandle; };
	template<> struct HandleFor<Dx12Heap> { using type = HeapHandle; };
	template<> struct HandleFor<Dx12QueryPool> { using type = QueryPoolHandle; };

	template<typename T>
	struct Slot { T obj{}; uint32_t generation{ 1 }; bool alive{ false }; };

	// Generic registry, automatically picks the correct handle for T via HandleFor<T>.
	template<typename T>
	struct Registry {
		using HandleT = typename HandleFor<T>::type;

		std::deque<Slot<T>> slots;
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
		Registry<Dx12Sampler> samplers;
		Registry<Dx12PipelineLayout> pipelineLayouts;
		Registry<Dx12Pipeline> pipelines;
		Registry<Dx12CommandSignature> commandSignatures;
		Registry<Dx12DescriptorHeap> descHeaps;
		Registry<Dx12Allocator> allocators;
		Registry<Dx12CommandList> commandLists;
		Registry<Dx12Timeline> timelines;
		Registry<Dx12Heap> heaps;
		Registry<Dx12QueryPool> queryPools;

		Dx12QueueState gfx{}, comp{}, copy{};

		// lifetime anchor
		std::weak_ptr<Dx12Device> selfWeak;
	};

	// ---------------- VTables forward ----------------
	extern const DeviceVTable g_devvt; 
	extern const QueueVTable g_qvt; 
	extern const CommandListVTable g_clvt; 
	extern const SwapchainVTable g_scvt; 
	extern const CommandAllocatorVTable g_calvt;
	extern const ResourceVTable g_buf_rvt;
	extern const ResourceVTable g_tex_rvt;
	extern const QueryPoolVTable g_qpvt;
	extern const PipelineVTable g_psovt;
	extern const PipelineLayoutVTable g_plvt;
	extern const CommandSignatureVTable g_csvt;
	extern const DescriptorHeapVTable g_dhvt;
	extern const SamplerVTable g_svt;
	extern const TimelineVTable g_tlvt;
	extern const HeapVTable g_hevt;

	// ---------------- Device vtable funcs ----------------

	static uint8_t getWriteMask(ColorWriteEnable e) {
		return static_cast<uint8_t>(e);
	}

	template<typename T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE KType>
	struct alignas(void*) PsoSubobject
	{
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = KType; // Must be first
		T Value; // Payload
	};

	using SO_RootSignature = PsoSubobject<ID3D12RootSignature*, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE>;
	using SO_VS = PsoSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS>;
	using SO_PS = PsoSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS>;
	using SO_AS = PsoSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS>;
	using SO_MS = PsoSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS>;
	using SO_CS = PsoSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS>;
	using SO_Rasterizer = PsoSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>;
	using SO_Blend = PsoSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>;
	using SO_DepthStencil = PsoSubobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL>;
	using SO_RtvFormats = PsoSubobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>;
	using SO_DsvFormat = PsoSubobject<DXGI_FORMAT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>;
	using SO_SampleDesc = PsoSubobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>;
	using SO_PrimTopology = PsoSubobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>;
	using SO_Flags = PsoSubobject<D3D12_PIPELINE_STATE_FLAGS, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS>;
	using SO_NodeMask = PsoSubobject<UINT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK>;
	using SO_InputLayout = PsoSubobject<D3D12_INPUT_LAYOUT_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>;

	struct PsoStreamBuilder
	{
		std::vector<std::byte> buf;

		template<class SO>
		void push(const SO& so)
		{
			static_assert(std::is_trivially_copyable_v<SO>, "SO must be trivially copyable");
			static_assert(std::is_standard_layout_v<SO>, "SO must be standard-layout");

			constexpr size_t off = offsetof(SO, Type);
			static_assert(off == 0, "Type must be the first member");

			static_assert(alignof(SO) == alignof(void*), "SO alignment must equal alignof(void*)");
			static_assert((sizeof(SO) % alignof(void*)) == 0, "SO size must be multiple of alignof(void*)");

			// Align write cursor to pointer alignment
			constexpr size_t kAlign = alignof(void*);
			size_t aligned = (buf.size() + (kAlign - 1)) & ~(kAlign - 1);
			if (aligned != buf.size()) buf.resize(aligned);

			// Append bytes
			size_t offBytes = buf.size();
			buf.resize(offBytes + sizeof(SO));
			std::memcpy(buf.data() + offBytes, &so, sizeof(SO));
		}

		D3D12_PIPELINE_STATE_STREAM_DESC desc()
		{
			D3D12_PIPELINE_STATE_STREAM_DESC d{};
			d.SizeInBytes = buf.size();
			d.pPipelineStateSubobjectStream = buf.data();
			return d;
		}
	};

	static PipelinePtr d_createPipelineFromStream(Device* d,
		const PipelineStreamItem* items,
		uint32_t count) noexcept
	{
		auto* dimpl = static_cast<Dx12Device*>(d->impl);

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
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
		D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{ nullptr, 0 };

		bool hasRast = false, hasBlend = false, hasDepth = false, hasRTV = false, hasDSV = false, hasSample = false, hasInputLayout = false;

		for (uint32_t i = 0; i < count; i++) {
			switch (items[i].type) {
			case PsoSubobj::Layout: {
				auto& L = *static_cast<const SubobjLayout*>(items[i].data);
				auto* pl = dimpl->pipelineLayouts.get(L.layout);
				if (!pl || !pl->root) { 
					BreakIfDebugging();
					return {};
				};
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
				hasRast = true;
				auto& R = *static_cast<const SubobjRaster*>(items[i].data);
				rast.FillMode = ToDx(R.rs.fill);
				rast.CullMode = ToDx(R.rs.cull);
				rast.FrontCounterClockwise = R.rs.frontCCW;
				rast.DepthBias = R.rs.depthBias;
				rast.DepthBiasClamp = R.rs.depthBiasClamp;
				rast.SlopeScaledDepthBias = R.rs.slopeScaledDepthBias;
			} break;
			case PsoSubobj::Blend: {
				hasBlend = true;
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
				hasDepth = true;
				auto& D = *static_cast<const SubobjDepth*>(items[i].data);
				depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
				depth.DepthEnable = D.ds.depthEnable;
				depth.DepthWriteMask = D.ds.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
				depth.DepthFunc = ToDx(D.ds.depthFunc);
			} break;
			case PsoSubobj::RTVFormats: {
				hasRTV = true;
				auto& R = *static_cast<const SubobjRTVs*>(items[i].data);
				rtv.NumRenderTargets = R.rt.count;
				for (uint32_t k = 0; k < R.rt.count && k < 8; k++) rtv.RTFormats[k] = ToDxgi(R.rt.formats[k]);
			} break;
			case PsoSubobj::DSVFormat: {
				hasDSV = true;
				auto& Z = *static_cast<const SubobjDSV*>(items[i].data);
				dsv = ToDxgi(Z.dsv);
			} break;
			case PsoSubobj::Sample: {
				hasSample = true;
				auto& S = *static_cast<const SubobjSample*>(items[i].data);
				sample = { S.sd.count, S.sd.quality };
			} break;
			case PsoSubobj::InputLayout: {
				hasInputLayout = true;
				ToDx12InputLayout(static_cast<const SubobjInputLayout*>(items[i].data)->il, inputLayout);
				inputLayoutDesc = { inputLayout.data(), (UINT)inputLayout.size() };
			} break;
			default: break;
			}
		}

		// Validate & decide kind
		if (hasCS && hasGfx) {
			spdlog::error("DX12 pipeline creation: cannot mix compute and graphics shaders in one PSO");
			BreakIfDebugging();
			return {};          // invalid mix
		}
		if (!hasCS && !hasGfx) {
			spdlog::error("DX12 pipeline creation: no shaders specified");
			BreakIfDebugging();
			return {};        // no shaders
		}
		const bool isCompute = hasCS;
		
		PsoStreamBuilder sb;
		sb.push(SO_RootSignature{ .Value = root });

		if (hasCS)    sb.push(SO_CS{ .Value = cs });
		if (hasGfx) {
			if (as.pShaderBytecode) sb.push(SO_AS{ .Value = as });
			if (ms.pShaderBytecode) sb.push(SO_MS{ .Value = ms });
			if (vs.pShaderBytecode) sb.push(SO_VS{ .Value = vs });
			if (ps.pShaderBytecode) sb.push(SO_PS{ .Value = ps });

			if (hasRast)   sb.push(SO_Rasterizer{ .Value = rast });
			if (hasBlend)  sb.push(SO_Blend{ .Value = blend });
			if (hasDepth)  sb.push(SO_DepthStencil{ .Value = depth });
			if (hasRTV)    sb.push(SO_RtvFormats{ .Value = rtv });
			if (hasDSV)    sb.push(SO_DsvFormat{ .Value = dsv });
			if (hasSample) sb.push(SO_SampleDesc{ .Value = sample });
			if (hasInputLayout) sb.push(SO_InputLayout{ .Value = inputLayoutDesc });
			// sb.push(SO_PrimTopology{ .Value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE });
		}
		auto sd = sb.desc();

		ComPtr<ID3D12PipelineState> pso;
		if (FAILED(dimpl->dev->CreatePipelineState(&sd, IID_PPV_ARGS(&pso)))) {
			BreakIfDebugging();
			return {};
		}

		auto handle = dimpl->pipelines.alloc(Dx12Pipeline( pso, isCompute, dimpl->selfWeak.lock() ));
		Pipeline out(handle);
		out.vt = &g_psovt;
		out.impl = dimpl->pipelines.get(handle);;

		return MakePipelinePtr(d, out);
	}

	static void d_destroyBuffer(DeviceDeletionContext* d, ResourceHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->buffers.free(h); }
	static void d_destroyTexture(DeviceDeletionContext* d, ResourceHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->textures.free(h); }
	static void d_destroySampler(DeviceDeletionContext* d, SamplerHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->samplers.free(h); }
	static void d_destroyPipeline(DeviceDeletionContext* d, PipelineHandle h) noexcept { static_cast<Dx12Device*>(d->impl)->pipelines.free(h); }

	static void d_destroyCommandList(DeviceDeletionContext* d, CommandList* p) noexcept {
		if (!d || !p || !p->IsValid()) { 
			BreakIfDebugging();
			return; 
		}
		auto* impl = static_cast<Dx12Device*>(d->impl);
		impl->commandLists.free(p->GetHandle());
		p->Reset();
	}

	static Queue d_getQueue(Device* d, QueueKind qk) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Queue out{qk}; out.vt = &g_qvt;
		Dx12QueueState* s = (qk == QueueKind::Graphics ? &impl->gfx : qk == QueueKind::Compute ? &impl->comp : &impl->copy);
		s->dev = impl->selfWeak.lock();
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
		auto flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		if (allowTearing) {
			flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		}
		desc.Flags = flags;

		ComPtr<IDXGISwapChain1> sc1;
		IDXGIFactory7* facForCreate = impl->upgradeFn && impl->slFactory ? impl->slFactory.Get()
			: impl->factory.Get();
		auto hr = facForCreate->CreateSwapChainForHwnd(impl->gfx.q.Get(), (HWND)hwnd, &desc, nullptr, nullptr, &sc1);
		if (FAILED(hr)) {
			spdlog::error("DX12 CreateSwapChainForHwnd failed: {0}", std::to_string(hr));
			BreakIfDebugging();
			return {};
		}
		ComPtr<IDXGISwapChain3> sc; 
		sc1.As(&sc);

		std::vector<ComPtr<ID3D12Resource>> imgs(bufferCount);
		std::vector<ResourceHandle> imgHandles(bufferCount);

		for (UINT i = 0; i < bufferCount; i++) {
			sc->GetBuffer(i, IID_PPV_ARGS(&imgs[i]));
			// Register as a TextureHandle
			Dx12Texture t(imgs[i], desc.Format, w, h, 1, 1, D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1, impl->selfWeak.lock());
			imgHandles[i] = impl->textures.alloc(t);

		}

		auto* scWrap = new Dx12Swapchain(
			sc, desc.Format, w, h, bufferCount,
			imgs, imgHandles,
			impl->selfWeak.lock()
		);

		Swapchain out{};
		out.impl = scWrap;
		out.vt = &g_scvt;
		return MakeSwapchainPtr(d, out);
	}

	static void d_destroySwapchain(DeviceDeletionContext*, Swapchain* sc) noexcept {
		auto* s = static_cast<Dx12Swapchain*>(sc->impl);
		delete s; sc->impl = nullptr;
		sc->vt = nullptr;
	}

	static void d_destroyDevice(Device* d) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
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
			s.Filter = (ss.sampler.maxAnisotropy > 1) ? D3D12_FILTER_ANISOTROPIC
				: D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			s.MipLODBias = 0.0f;
			s.MaxAnisotropy = ss.sampler.maxAnisotropy;
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

		if (ld.flags & PipelineLayoutFlags::PF_AllowInputAssembler) {
			rs.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		}


		Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3DX12SerializeVersionedRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err))) {
			BreakIfDebugging();
			return {};
		}
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
		if (FAILED(impl->dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
			IID_PPV_ARGS(&root)))) {
			BreakIfDebugging();
			return {};
		}

		Dx12PipelineLayout L(ld, impl->selfWeak.lock());
		if (ld.pushConstants.size && ld.pushConstants.data) {
			L.pcs.assign(ld.pushConstants.data, ld.pushConstants.data + ld.pushConstants.size);
			// Build rcParams in the same order as params:
			L.rcParams.reserve(ld.pushConstants.size);
			for (uint32_t i = 0; i < ld.pushConstants.size; ++i) {
				const auto& pc = ld.pushConstants.data[i];
				L.rcParams.push_back(Dx12PipelineLayout::RootConstParam{
					pc.set, pc.binding, pc.num32BitValues, /*rootIndex=*/i
					});
			}
		}
		if (ld.staticSamplers.size && ld.staticSamplers.data)
			L.staticSamplers.assign(ld.staticSamplers.data, ld.staticSamplers.data + ld.staticSamplers.size);
		L.root = root;
		auto handle = impl->pipelineLayouts.alloc(std::move(L));
		PipelineLayout out(handle);
		out.vt = &g_plvt;
		out.impl = impl->pipelineLayouts.get(handle);
		return MakePipelineLayoutPtr(d, out);
	}


	static void d_destroyPipelineLayout(DeviceDeletionContext* d, PipelineLayoutHandle h) noexcept {
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
			if (!L || !L->root) {
				BreakIfDebugging();
				return {};
			}
			rs = L->root.Get();
		}

		D3D12_COMMAND_SIGNATURE_DESC desc{};
		desc.pArgumentDescs = dxArgs.data();
		desc.NumArgumentDescs = (UINT)dxArgs.size();
		desc.ByteStride = cd.byteStride;

		Microsoft::WRL::ComPtr<ID3D12CommandSignature> cs;
		if (FAILED(impl->dev->CreateCommandSignature(&desc, rs, IID_PPV_ARGS(&cs)))) { 
			BreakIfDebugging();
			return {}; 
		}
		Dx12CommandSignature S(cs, cd.byteStride, impl->selfWeak.lock());
		auto handle = impl->commandSignatures.alloc(std::move(S));
		CommandSignature out(handle);
		out.vt = &g_csvt;
		out.impl = impl->commandSignatures.get(handle);
		return MakeCommandSignaturePtr(d, out);
	}

	static void d_destroyCommandSignature(DeviceDeletionContext* d, CommandSignatureHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->commandSignatures.free(h);
	}

	static DescriptorHeapPtr d_createDescriptorHeap(Device* d, const DescriptorHeapDesc& hd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);

		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = ToDX(hd.type);
		desc.NumDescriptors = hd.capacity;
		desc.Flags = hd.shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		if (FAILED(impl->dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)))) { 
			BreakIfDebugging();
			return {}; 
		}

		auto descriptorSize = impl->dev->GetDescriptorHandleIncrementSize(desc.Type);
		Dx12DescriptorHeap H(heap, desc.Type, descriptorSize, (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0, impl->selfWeak.lock());

		auto handle = impl->descHeaps.alloc(std::move(H));
		DescriptorHeap out(handle);
		out.vt = &g_dhvt;
		out.impl = impl->descHeaps.get(handle);
		return MakeDescriptorHeapPtr(d, out);
	}
	static void d_destroyDescriptorHeap(DeviceDeletionContext* d, DescriptorHeapHandle h) noexcept {
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

	static Result d_createShaderResourceView(Device* d, DescriptorSlot s, const ResourceHandle& resource, const SrvDesc& dv) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) {
			BreakIfDebugging();
			return Result::InvalidArg;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Shader4ComponentMapping = (dv.componentMapping == 0)
			? D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
			: dv.componentMapping;

		switch (dv.dimension) {
		case SrvDim::Buffer: {
			auto* B = impl->buffers.get(resource);
			if (!B || !B->res) { 
				BreakIfDebugging();
				return Result::InvalidArg;
			}

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
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			desc.Texture1D.MostDetailedMip = dv.tex1D.mostDetailedMip;
			desc.Texture1D.MipLevels = dv.tex1D.mipLevels;
			desc.Texture1D.ResourceMinLODClamp = dv.tex1D.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture1DArray: {
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
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
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
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
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
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
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2DMSArray: {
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
			desc.Texture2DMSArray.FirstArraySlice = dv.tex2DMSArray.firstArraySlice;
			desc.Texture2DMSArray.ArraySize = dv.tex2DMSArray.arraySize;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture3D: {
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MostDetailedMip = dv.tex3D.mostDetailedMip;
			desc.Texture3D.MipLevels = dv.tex3D.mipLevels;
			desc.Texture3D.ResourceMinLODClamp = dv.tex3D.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::TextureCube: {
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			desc.TextureCube.MostDetailedMip = dv.cube.mostDetailedMip;
			desc.TextureCube.MipLevels = dv.cube.mipLevels;
			desc.TextureCube.ResourceMinLODClamp = dv.cube.minLodClamp;
			impl->dev->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::TextureCubeArray: {
			auto* T = impl->textures.get(resource); if (!T || !T->res) return Result::InvalidArg;
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
			auto* B = impl->buffers.get(resource); if (!B || !B->res) return Result::InvalidArg;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			desc.RaytracingAccelerationStructure.Location = B->res->GetGPUVirtualAddress();
			impl->dev->CreateShaderResourceView(B->res.Get(), &desc, dst);
			return Result::Ok;
		}

		default: break;
		}
		BreakIfDebugging();
		return Result::InvalidArg;
	}

	static Result d_createUnorderedAccessView(Device* d, DescriptorSlot s, const ResourceHandle& resource, const UavDesc& dv) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) { 
			BreakIfDebugging();
			return Result::Failed;
		};

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) {
			BreakIfDebugging();
			return Result::InvalidArg;
		}

		D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
		ID3D12Resource* pResource = nullptr;
		ID3D12Resource* pCounterResource = nullptr; // optional, for structured append/consume

		switch (dv.dimension)
		{
			// ========================= Buffer UAV =========================
		case UavDim::Buffer:
		{
			auto* B = impl->buffers.get(resource);
			if (!B || !B->res) { 
				BreakIfDebugging();
				return Result::InvalidArg; 
			}

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
				desc.Format = ToDxgi(dv.formatOverride);
				desc.Buffer.StructureByteStride = 0;
				desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
				break;
			}

			impl->dev->CreateUnorderedAccessView(pResource, pCounterResource, &desc, dst);
			return Result::Ok;
		}

		// ========================= Texture UAVs =========================
		case UavDim::Texture1D:
		{
			auto* T = impl->textures.get(resource);
			if (!T || !T->res) {
				BreakIfDebugging();
				return Result::InvalidArg;
			}
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			desc.Texture1D.MipSlice = dv.texture1D.mipSlice;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture1DArray:
		{
			auto* T = impl->textures.get(resource);
			if (!T || !T->res) { 
				BreakIfDebugging();
				return Result::InvalidArg;
			}
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			desc.Texture1DArray.MipSlice = dv.texture1DArray.mipSlice;
			desc.Texture1DArray.FirstArraySlice = dv.texture1DArray.firstArraySlice;
			desc.Texture1DArray.ArraySize = dv.texture1DArray.arraySize;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2D:
		{
			auto* T = impl->textures.get(resource);
			if (!T || !T->res) { 
				BreakIfDebugging();
				return Result::InvalidArg;
			}
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = dv.texture2D.mipSlice;
			desc.Texture2D.PlaneSlice = dv.texture2D.planeSlice;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2DArray:
		{
			auto* T = impl->textures.get(resource);
			if (!T || !T->res) { 
				BreakIfDebugging();
				return Result::InvalidArg;
			}
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = dv.texture2DArray.mipSlice;
			desc.Texture2DArray.FirstArraySlice = dv.texture2DArray.firstArraySlice;
			desc.Texture2DArray.ArraySize = dv.texture2DArray.arraySize;
			desc.Texture2DArray.PlaneSlice = dv.texture2DArray.planeSlice;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture3D:
		{
			auto* T = impl->textures.get(resource);
			if (!T || !T->res) { 
				BreakIfDebugging();
				return Result::InvalidArg;
			}
			pResource = T->res.Get();

			desc.Format = T->fmt;
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MipSlice = dv.texture3D.mipSlice;
			desc.Texture3D.FirstWSlice = dv.texture3D.firstWSlice;
			desc.Texture3D.WSize = (dv.texture3D.wSize == 0) ? UINT(-1) : dv.texture3D.wSize;

			impl->dev->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2DMS:
		case UavDim::Texture2DMSArray:
			// UAVs for MSAA textures are not supported by D3D12
			BreakIfDebugging();
			return Result::Unsupported;
		}
		BreakIfDebugging();
		return Result::InvalidArg;
	}

	static Result d_createConstantBufferView(Device* d, DescriptorSlot s, const ResourceHandle& bh, const CbvDesc& dv) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return Result::InvalidArg;
		auto* B = impl->buffers.get(bh); if (!B) { 
			BreakIfDebugging();
			return Result::InvalidArg;
		}

		D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
		desc.BufferLocation = B->res->GetGPUVirtualAddress() + dv.byteOffset;
		desc.SizeInBytes = (UINT)((dv.byteSize + 255) & ~255u);
		impl->dev->CreateConstantBufferView(&desc, dst);
		return Result::Ok;
	}

	static Result d_createSampler(Device* d, DescriptorSlot s, const SamplerDesc& sd) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)) { 
			BreakIfDebugging();
			return Result::InvalidArg;
		}

		D3D12_SAMPLER_DESC desc{};
		desc.Filter = BuildDxFilter(sd);
		desc.AddressU = ToDX(sd.addressU);
		desc.AddressV = ToDX(sd.addressV);
		desc.AddressW = ToDX(sd.addressW);

		// DX12 ignores unnormalizedCoordinates (always normalized)

		// Clamp anisotropy to device limit (DX12 spec says 1->16)
		desc.MaxAnisotropy = (sd.maxAnisotropy > 1) ? (std::min<uint32_t>(sd.maxAnisotropy, 16u)) : 1u;

		desc.MipLODBias = sd.mipLodBias;
		desc.MinLOD = sd.minLod;
		desc.MaxLOD = sd.maxLod;

		desc.ComparisonFunc = sd.compareEnable ? ToDX(sd.compareOp) : D3D12_COMPARISON_FUNC_NEVER;

		FillDxBorderColor(sd, desc.BorderColor);

		impl->dev->CreateSampler(&desc, dst);
		return Result::Ok;
	}

	static Result d_createRenderTargetView(Device* d, DescriptorSlot s, const ResourceHandle& texture, const RtvDesc& rd) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) { 
			BreakIfDebugging();
			return Result::Failed;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_RTV)) {
			BreakIfDebugging();
			return Result::InvalidArg;
		}

		// For texture RTVs we expect a texture resource
		auto* T = impl->textures.get(texture);
		if (!T && rd.dimension != RtvDim::Buffer) { 
			BreakIfDebugging();
			return Result::InvalidArg;
		}

		D3D12_RENDER_TARGET_VIEW_DESC r{};
		ID3D12Resource* pRes = nullptr;

		switch (rd.dimension)
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
			BreakIfDebugging();
			return Result::Unsupported;
		}

		default:
			BreakIfDebugging();
			return Result::Unsupported;
		}

		impl->dev->CreateRenderTargetView(pRes, &r, dst);
		return Result::Ok;
	}

	static Result d_createDepthStencilView(Device* d, DescriptorSlot s, const ResourceHandle& texture, const DsvDesc& dd) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) {
			BreakIfDebugging();
			return Result::Failed;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) {
			BreakIfDebugging();
			return Result::InvalidArg;
		}

		auto* T = impl->textures.get(texture);
		if (!T) { 
			BreakIfDebugging();
			return Result::InvalidArg; 
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC z{};
		z.Format = (dd.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dd.formatOverride);
		z.Flags = (dd.readOnlyDepth ? D3D12_DSV_FLAG_READ_ONLY_DEPTH : (D3D12_DSV_FLAGS)0) |
			(dd.readOnlyStencil ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : (D3D12_DSV_FLAGS)0);

		switch (dd.dimension)
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
			BreakIfDebugging();
			return Result::Unsupported;
		}

		impl->dev->CreateDepthStencilView(T->res.Get(), &z, dst);
		return Result::Ok;
	}

	static CommandAllocatorPtr d_createCommandAllocator(Device* d, QueueKind q) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> a;
		if (FAILED(impl->dev->CreateCommandAllocator(ToDX(q), IID_PPV_ARGS(&a)))) {
			__debugbreak();
			return {};
		}

		Dx12Allocator A(a, ToDX(q), impl->selfWeak.lock());
		auto h = impl->allocators.alloc(std::move(A));

		CommandAllocator out{h};
		out.impl = impl->allocators.get(h);
		out.vt = &g_calvt;
		return MakeCommandAllocatorPtr(d, out);
	}

	static void d_destroyCommandAllocator(DeviceDeletionContext* d, CommandAllocator* ca) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		impl->allocators.free(ca->GetHandle());
	}

	static CommandListPtr d_createCommandList(Device* d, QueueKind q, CommandAllocator ca) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* A = static_cast<Dx12Allocator*>(ca.impl);
		if (!A) { 
			BreakIfDebugging();
			return {};
		}

		ComPtr<ID3D12GraphicsCommandList7> cl;
		if (FAILED(impl->dev->CreateCommandList(0, A->type, A->alloc.Get(), nullptr, IID_PPV_ARGS(&cl)))) { 
			BreakIfDebugging();
			return {}; 
		}
		Dx12CommandList rec(cl, A->alloc, A->type, impl->selfWeak.lock());
		auto h = impl->commandLists.alloc(std::move(rec));

		CommandList out{h};
		Dx12CommandList* implCL = impl->commandLists.get(h);
		out.impl = implCL;
		out.vt = &g_clvt;
		return MakeCommandListPtr(d, out);
	}

	static ResourcePtr d_createCommittedBuffer(Device* d, const ResourceDesc& bd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->dev || bd.buffer.sizeBytes == 0) { 
			BreakIfDebugging();
			return {}; 
		}

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
		if (FAILED(hr)) {
			BreakIfDebugging();
			return {};
		}

		if (bd.debugName) res->SetName(std::wstring(bd.debugName, bd.debugName + ::strlen(bd.debugName)).c_str());

		Dx12Buffer B(std::move(res), impl->selfWeak.lock());
		auto handle = impl->buffers.alloc(std::move(B));

		Resource out{ handle, false };
		out.impl = impl->buffers.get(handle);
		out.vt = &g_buf_rvt;
		return MakeBufferPtr(d, out);
	}

	static ResourcePtr d_createCommittedTexture(Device* d, const ResourceDesc& td) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->dev || td.texture.width == 0 || td.texture.height == 0 || td.texture.format == Format::Unknown) {
			BreakIfDebugging();
			return {};
		}

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
		if (FAILED(hr)) {
			spdlog::error("Failed to create committed texture: {0}", hr);
			BreakIfDebugging();
			return {};
		};

		if (td.debugName) res->SetName(std::wstring(td.debugName, td.debugName + ::strlen(td.debugName)).c_str());

		auto arraySize = td.type == ResourceType::Texture3D ? 1 : td.texture.depthOrLayers;
		auto depth = td.type == ResourceType::Texture3D ? td.texture.depthOrLayers : 1;
		Dx12Texture T(std::move(res), desc.Format, td.texture.width, td.texture.height,
			td.texture.mipLevels, arraySize, (td.type == ResourceType::Texture3D)
			? D3D12_RESOURCE_DIMENSION_TEXTURE3D
			: (td.type == ResourceType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
				: D3D12_RESOURCE_DIMENSION_TEXTURE1D), depth, impl->selfWeak.lock());

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
		case ResourceType::Unknown: {
			BreakIfDebugging();
			return {};
		}
		}
		BreakIfDebugging();
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
		if (FAILED(impl->dev->CreateFence(initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f)))) {
			BreakIfDebugging();
			return {};
		}
		if (dbg) { std::wstring w(dbg, dbg + ::strlen(dbg)); f->SetName(w.c_str()); }
		Dx12Timeline T(f, impl->selfWeak.lock());
		auto h = impl->timelines.alloc(std::move(T));
		Timeline out{ h };
		out.impl = impl->timelines.get(h);
		out.vt = &g_tlvt;
		return MakeTimelinePtr(d, out);
	}


	static void d_destroyTimeline(DeviceDeletionContext* d, TimelineHandle t) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		impl->timelines.free(t);
	}

	static HeapPtr d_createHeap(Device* d, const HeapDesc& hd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->dev || hd.sizeBytes == 0) {
			BreakIfDebugging();
			return {};
		}
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
		if (FAILED(impl->dev->CreateHeap(&desc, IID_PPV_ARGS(&heap)))) {
			return {};
		}

#ifdef _WIN32
		if (hd.debugName) { std::wstring w(hd.debugName, hd.debugName + ::strlen(hd.debugName)); heap->SetName(w.c_str()); }
#endif

		Dx12Heap H(heap, hd.sizeBytes, impl->selfWeak.lock());
		auto h = impl->heaps.alloc(std::move(H));
		Heap out{ h };
		out.impl = impl->heaps.get(h);
		out.vt = &g_hevt;
		return MakeHeapPtr(d, out);
	}

	static void d_destroyHeap(DeviceDeletionContext* d, HeapHandle h) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) {
			BreakIfDebugging();
			return;
		}
		impl->heaps.free(h);
	}

	static void d_setNameBuffer(Device* d, ResourceHandle b, const char* n) noexcept {
		if (!n) { 
			BreakIfDebugging();
			return;
		}
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* B = impl->buffers.get(b)) {
			std::wstring w(n, n + ::strlen(n));
			B->res->SetName(w.c_str());
		}
	}
	
	static void d_setNameTexture(Device* d, ResourceHandle t, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
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
		if (!impl) {
			BreakIfDebugging();
			return {};
		}
		auto* H = impl->heaps.get(hh); if (!H || !H->heap) {
			BreakIfDebugging();
			return {};
		}
		if (td.texture.width == 0 || td.texture.height == 0 || td.texture.format == Format::Unknown) {
			BreakIfDebugging();
			return {};
		}
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
		if (FAILED(hr)) {
			BreakIfDebugging();
			return {};
		}
		if (td.debugName) res->SetName(std::wstring(td.debugName, td.debugName + ::strlen(td.debugName)).c_str());
		Dx12Texture T(std::move(res), desc.Format, td.texture.width, td.texture.height,
			td.texture.mipLevels, (td.type == ResourceType::Texture3D) ? 1 : td.texture.depthOrLayers,
			(td.type == ResourceType::Texture3D) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
			: (td.type == ResourceType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
				: D3D12_RESOURCE_DIMENSION_TEXTURE1D),
			(td.type == ResourceType::Texture3D) ? td.texture.depthOrLayers : 1,
			impl->selfWeak.lock());
		
		auto handle = impl->textures.alloc(std::move(T));
		Resource out(handle, true);
		out.impl = impl->textures.get(handle);
		out.vt = &g_tex_rvt;
		return MakeTexturePtr(d, out);
	}

	static ResourcePtr d_createPlacedBuffer(Device* d, HeapHandle hh, uint64_t offset, const ResourceDesc& bd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) {
			return {};
		}
		auto* H = impl->heaps.get(hh); if (!H || !H->heap) {
			return {};
		}
		if (bd.buffer.sizeBytes == 0) {
			return {};
		}
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
		if (FAILED(hr)) { 
			BreakIfDebugging();
			return {};
		}
		if (bd.debugName) res->SetName(std::wstring(bd.debugName, bd.debugName + ::strlen(bd.debugName)).c_str());
		Dx12Buffer B(std::move(res), impl->selfWeak.lock());
		auto handle = impl->buffers.alloc(std::move(B));
		Resource out{ handle, false };
		out.impl = impl->buffers.get(handle);
		out.vt = &g_buf_rvt;
		return MakeBufferPtr(d, out);
	}

	static ResourcePtr d_createPlacedResource(Device* d, HeapHandle hh, uint64_t offset, const ResourceDesc& rd) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) { 
			BreakIfDebugging();
			return {};
		}
		auto* H = impl->heaps.get(hh); if (!H || !H->heap) {
			BreakIfDebugging();
			return {};
		}
		switch (rd.type) {
		case ResourceType::Buffer:  return d_createPlacedBuffer(d, hh, offset, rd);
		case ResourceType::Texture3D:
		case ResourceType::Texture2D:
		case ResourceType::Texture1D:
			return d_createPlacedTexture(d, hh, offset, rd);
		case ResourceType::Unknown: {
			BreakIfDebugging();
			return {};
		}
		}
		BreakIfDebugging();
		return {};
	}

	static QueryPoolPtr d_createQueryPool(Device* d, const QueryPoolDesc& qd) noexcept {
		auto* dimpl = static_cast<Dx12Device*>(d->impl);
		if (!dimpl || qd.count == 0) {
			BreakIfDebugging();
			return {};
		}
		D3D12_QUERY_HEAP_DESC desc{};
		desc.Count = qd.count;

		D3D12_QUERY_HEAP_TYPE type;
		bool usePSO1 = false;

		switch (qd.type) {
		case QueryType::Timestamp:
			desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			type = desc.Type; break;

		case QueryType::Occlusion:
			desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
			type = desc.Type; break;

		case QueryType::PipelineStatistics: {
			// If mesh/task bits requested and supported -> use *_STATISTICS1
			bool needMesh = (qd.statsMask & (PS_TaskInvocations | PS_MeshInvocations | PS_MeshPrimitives)) != 0;

			D3D12_FEATURE_DATA_D3D12_OPTIONS9 opts9{};
			bool haveOpt9 = SUCCEEDED(dimpl->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &opts9, sizeof(opts9)));
			bool canMeshStats = haveOpt9 && !!opts9.MeshShaderPipelineStatsSupported;

			if (needMesh && !canMeshStats && qd.requireAllStats) {
				BreakIfDebugging();
				return {}; // Unsupported
			}

			desc.Type = needMesh && canMeshStats
				? D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1
				: D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
			type = desc.Type;
			usePSO1 = (desc.Type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1);
		} break;
		}

		Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
		if (FAILED(dimpl->dev->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap)))) {
			return {};
		}
		Dx12QueryPool qp(heap, type, qd.count, dimpl->selfWeak.lock());
		qp.usePSO1 = usePSO1;

		auto handle = dimpl->queryPools.alloc(std::move(qp));
		QueryPool out{ handle };
		out.impl = dimpl->queryPools.get(handle);
		out.vt = &g_qpvt;

		return MakeQueryPoolPtr(d, out);
	}

	static void d_destroyQueryPool(DeviceDeletionContext* d, QueryPoolHandle h) noexcept {
		static_cast<Dx12Device*>(d->impl)->queryPools.free(h);
	}

	static TimestampCalibration d_getTimestampCalibration(Device* d, QueueKind q) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* s = (q == QueueKind::Graphics) ? &impl->gfx : (q == QueueKind::Compute ? &impl->comp : &impl->copy);
		UINT64 freq = 0;
		if (s->q) s->q->GetTimestampFrequency(&freq);
		return { freq };
	}

	static CopyableFootprintsInfo d_getCopyableFootprints(
		Device* d,
		const FootprintRangeDesc& in,
		CopyableFootprint* out,
		uint32_t outCap) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !out || outCap == 0) {
			BreakIfDebugging();
			return {};
		}

		auto* T = impl->textures.get(in.texture);
		if (!T || !T->res) {
			BreakIfDebugging();
			return {};
		}
		const D3D12_RESOURCE_DESC desc = T->res->GetDesc();

		// Resource-wide properties
		const UINT mipLevels = desc.MipLevels;
		const UINT arrayLayers = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 1u : desc.DepthOrArraySize;

		// Plane count per DXGI format
		const UINT resPlaneCount = D3D12GetFormatPlaneCount(impl->dev.Get(), desc.Format);

		// Clamp input range to resource
		const UINT firstMip = std::min<UINT>(in.firstMip, mipLevels ? mipLevels - 1u : 0u);
		const UINT mipCount = std::min<UINT>(in.mipCount, mipLevels - firstMip);
		const UINT firstArray = std::min<UINT>(in.firstArraySlice, arrayLayers ? arrayLayers - 1u : 0u);
		const UINT arrayCount = std::min<UINT>(in.arraySize, arrayLayers - firstArray);
		const UINT firstPlane = std::min<UINT>(in.firstPlane, resPlaneCount ? resPlaneCount - 1u : 0u);
		const UINT planeCount = std::min<UINT>(in.planeCount, resPlaneCount - firstPlane);

		if (mipCount == 0 || arrayCount == 0 || planeCount == 0) {
			BreakIfDebugging();
			return {};
		}

		const UINT totalSubs = mipCount * arrayCount * planeCount;
		if (outCap < totalSubs) {
			BreakIfDebugging();
			return {}; // TODO: partial?
		}
		// D3D12 subresource layout: Mip + Array*NumMips + Plane*NumMips*ArraySize
		const UINT firstSubresource =
			firstMip + firstArray * mipLevels + firstPlane * mipLevels * arrayLayers;

		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> placed(totalSubs);
		std::vector<UINT>  numRows(totalSubs);
		std::vector<UINT64> rowSizes(totalSubs);
		UINT64 totalBytes = 0;

		impl->dev->GetCopyableFootprints(
			&desc,
			firstSubresource,
			totalSubs,
			in.baseOffset, // base offset you want footprints relative to
			placed.data(),
			numRows.data(),
			rowSizes.data(),
			&totalBytes);

		// Pack back into RHI-friendly structure
		for (UINT i = 0; i < totalSubs; ++i) {
			const auto& f = placed[i].Footprint;
			out[i].offset = placed[i].Offset;
			out[i].rowPitch = f.RowPitch;     // bytes
			out[i].height = f.Height;       // texel rows used for the copy
			out[i].width = f.Width;        // texels
			out[i].depth = f.Depth;        // slices for 3D (else 1)
		}

		return { totalSubs, totalBytes };
	}

	// ---------------- Queue vtable funcs ----------------
	static Result q_submit(Queue* q, Span<CommandList> lists, const SubmitDesc& s) noexcept {
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		auto* dev = qs->dev.get(); 
		if (!dev) {
			BreakIfDebugging();
			return Result::Failed;
		}

		// Pre-waits
		for (auto& w : s.waits) {
			auto* TL = dev->timelines.get(w.t); if (!TL) {
				BreakIfDebugging();
				return Result::InvalidArg;
			}
			if (FAILED(qs->q->Wait(TL->fence.Get(), w.value))) {
				BreakIfDebugging();
				return Result::Failed;
			}
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
			auto* TL = dev->timelines.get(sgn.t); if (!TL) {
				BreakIfDebugging();
				return Result::InvalidArg;
			}
			if (FAILED(qs->q->Signal(TL->fence.Get(), sgn.value))) {
				BreakIfDebugging();
				return Result::Failed;
			}
		}
		return Result::Ok;
	}

	static Result q_signal(Queue* q, const TimelinePoint& p) noexcept {
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		auto* dev = qs->dev.get(); 
		if (!dev) {
			BreakIfDebugging();
			return Result::Failed;
		}
		auto* TL = dev->timelines.get(p.t); if (!TL) {
			BreakIfDebugging();
			return Result::InvalidArg;
		}
		return SUCCEEDED(qs->q->Signal(TL->fence.Get(), p.value)) ? Result::Ok : Result::Failed;
	}

	static Result q_wait(Queue* q, const TimelinePoint& p) noexcept {
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		auto* dev = qs->dev.get(); if (!dev) {
			BreakIfDebugging();
			return Result::Failed;
		}
		auto* TL = dev->timelines.get(p.t); if (!TL) {
			BreakIfDebugging();
			return Result::InvalidArg;
		}
		return SUCCEEDED(qs->q->Wait(TL->fence.Get(), p.value)) ? Result::Ok : Result::Failed;
	}

	static void q_setName(Queue* q, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* qs = static_cast<Dx12QueueState*>(q->impl);
		if (!qs || !qs->q) {
			BreakIfDebugging();
			return;
		}
		std::wstring w(n, n + ::strlen(n));
		qs->q->SetName(w.c_str());
	}

	// ---------------- CommandList vtable funcs ----------------

	static inline UINT MipDim(UINT base, UINT mip) {
		return (std::max)(1u, base >> mip);
	}

	static inline UINT CalcSubresourceFor(const Dx12Texture& T, UINT mip, UINT arraySlice) {
		// PlaneSlice = 0 (non-planar). TODO: support planar formats
		return D3D12CalcSubresource(mip, arraySlice, 0, T.mips, T.arraySize);
	}

	static void cl_end(CommandList* cl) noexcept {
		auto* w = static_cast<Dx12CommandList*>(cl->impl);
		w->cl->Close();
	}
	static void cl_reset(CommandList* cl, const CommandAllocator& ca) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* a = static_cast<Dx12Allocator*>(ca.impl);
#if NDEBUG
		if (!l) { 
			BreakIfDebugging(); 
			spdlog::error("cl_reset: invalid command list"); 
		}
		if (!a) {
			BreakIfDebugging();
			spdlog::error("cl_reset: invalid command allocator");
		}
#endif
		l->cl->Reset(a->alloc.Get(), nullptr);
	}
	static void cl_beginPass(CommandList* cl, const PassBeginInfo& p) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		if (!l) {
			BreakIfDebugging();
			return;
		}

		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
		rtvs.reserve(p.colors.size);
		for (uint32_t i = 0; i < p.colors.size; ++i) {
			D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
			if (DxGetDstCpu(l->dev.get(), p.colors.data[i].rtv, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_RTV)) {
				rtvs.push_back(cpu);
				if (p.colors.data[i].loadOp == LoadOp::Clear) {
					l->cl->ClearRenderTargetView(cpu, p.colors.data[i].clear.rgba, 0, nullptr);
				}
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
		const D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
		if (p.depth) {
			if (DxGetDstCpu(l->dev.get(), p.depth->dsv, dsv, D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) {
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
		auto* impl = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = impl->dev.get();
		auto* L = dev->pipelineLayouts.get(layoutH);
		if (!L || !L->root) {
			BreakIfDebugging();
			return;
		}

		switch(impl->type){
			case D3D12_COMMAND_LIST_TYPE_DIRECT:
				impl->cl->SetGraphicsRootSignature(L->root.Get());
				impl->cl->SetComputeRootSignature(L->root.Get());
				break;
			case D3D12_COMMAND_LIST_TYPE_COMPUTE:
				impl->cl->SetComputeRootSignature(L->root.Get());
				break;
			case D3D12_COMMAND_LIST_TYPE_COPY:
				// no root signature for copy-only lists
				BreakIfDebugging();
				break;
		}

		impl->boundLayout = layoutH;
		impl->boundLayoutPtr = L;
	}
	static void cl_bindPipeline(CommandList* cl, PipelineHandle psoH) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev.get();
		if (auto* P = dev->pipelines.get(psoH)) {
			l->cl->SetPipelineState(P->pso.Get());
		}
	}
	static void cl_setVB(CommandList* cl, uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		std::vector<D3D12_VERTEX_BUFFER_VIEW> views; views.resize(numViews);
		auto* dev = l->dev.get();
		for (uint32_t i = 0; i < numViews; ++i) {
			if (auto* B = dev->buffers.get(pBufferViews[i].buffer)) {
				views[i].BufferLocation = B->res->GetGPUVirtualAddress() + pBufferViews[i].offset;
				views[i].SizeInBytes = pBufferViews[i].sizeBytes;
				views[i].StrideInBytes = pBufferViews[i].stride;
			}
		}
		l->cl->IASetVertexBuffers(startSlot, (UINT)numViews, views.data());
	}
	static void cl_setIB(CommandList* cl, const IndexBufferView& view) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev.get();
		if (auto* B = dev->buffers.get(view.buffer)) {
			D3D12_INDEX_BUFFER_VIEW ibv{};
			ibv.BufferLocation = B->res->GetGPUVirtualAddress() + view.offset;
			ibv.SizeInBytes = view.sizeBytes;
			ibv.Format = ToDxgi(view.format);
			l->cl->IASetIndexBuffer(&ibv);
		}
	}
	static void cl_draw(CommandList* cl, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		l->cl->DrawInstanced(vc, ic, fv, fi);
	}
	static void cl_drawIndexed(CommandList* cl, uint32_t ic, uint32_t inst, uint32_t firstIdx, int32_t vtxOff, uint32_t firstInst) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl); 
		l->cl->DrawIndexedInstanced(ic, inst, firstIdx, vtxOff, firstInst);
	}
	static void cl_dispatch(CommandList* cl, uint32_t x, uint32_t y, uint32_t z) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		l->cl->Dispatch(x, y, z);
	}

	static void cl_clearRTV_slot(CommandList* c, DescriptorSlot s, const rhi::ClearValue& cv) noexcept {
		auto* impl = static_cast<Dx12CommandList*>(c->impl);
		if (!impl) {
			BreakIfDebugging();
			return;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		if (!DxGetDstCpu(impl->dev.get(), s, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_RTV)) {
			BreakIfDebugging();
			return;
		}

		float rgba[4] = { cv.rgba[0], cv.rgba[1], cv.rgba[2], cv.rgba[3] };
		impl->cl->ClearRenderTargetView(cpu, rgba, 0, nullptr);
	}

	static void cl_clearDSV_slot(CommandList* c, DescriptorSlot s,
		bool clearDepth, bool clearStencil,
		float depth, uint8_t stencil) noexcept
	{
		auto* impl = static_cast<Dx12CommandList*>(c->impl);
		if (!impl) {
			BreakIfDebugging();
			return;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		if (!DxGetDstCpu(impl->dev.get(), s, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) {
			BreakIfDebugging();
			return;
		}

		D3D12_CLEAR_FLAGS flags = (D3D12_CLEAR_FLAGS)0;
		if (clearDepth)   flags |= D3D12_CLEAR_FLAG_DEPTH;
		if (clearStencil) flags |= D3D12_CLEAR_FLAG_STENCIL;

		impl->cl->ClearDepthStencilView(cpu, flags, depth, stencil, 0, nullptr);
	}
	static void cl_executeIndirect(
		CommandList* cl,
		CommandSignatureHandle sigH,
		ResourceHandle argBufH, uint64_t argOff,
		ResourceHandle cntBufH, uint64_t cntOff,
		uint32_t maxCount) noexcept
	{
		if (!cl || !cl->impl) {
			BreakIfDebugging();
			return;
		}

		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev.get();
		if (!dev) {
			BreakIfDebugging();
			return;
		}

		auto* S = dev->commandSignatures.get(sigH);
		if (!S || !S->sig) {
			BreakIfDebugging();
			return;
		}

		auto* argB = dev->buffers.get(argBufH);
		if (!argB || !argB->res) {
			BreakIfDebugging();
			return;
		}

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
	static void cl_setDescriptorHeaps(CommandList* cl, DescriptorHeapHandle csu, std::optional<DescriptorHeapHandle> samp) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev.get();

		ID3D12DescriptorHeap* heaps[2]{};
		UINT n = 0;
		if (auto* H = dev->descHeaps.get(csu)) {
			heaps[n++] = H->heap.Get();
		}
		if (samp.has_value()) {
			if (auto* H = dev->descHeaps.get(samp.value())) {
				heaps[n++] = H->heap.Get();
			}
		}
		if (n) {
			l->cl->SetDescriptorHeaps(n, heaps);
		}
	}

	static void cl_barrier(CommandList* cl, const BarrierBatch& b) noexcept {
		if (!cl || !cl->impl) {
			BreakIfDebugging();
			return;
		}
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = l->dev.get();
		if (!dev) {
			BreakIfDebugging();
			return;
		}

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

	static void cl_clearUavUint(CommandList* cl,
		const UavClearInfo& u,
		const UavClearUint& v) noexcept
	{
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		// Resolve the two matching descriptors
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		if (!DxGetDstCpu(impl, u.cpuVisible, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) {
			BreakIfDebugging();
			return;
		}
		if (!DxGetDstGpu(impl, u.shaderVisible, gpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) {
			BreakIfDebugging();
			return;
		}

		// Resource to clear
		
		ID3D12Resource* res = nullptr;
		if (u.resource.IsTexture()) {
			res = impl->textures.get(u.resource.GetHandle())->res.Get();
		} else {
			res = impl->buffers.get(u.resource.GetHandle())->res.Get();
		}
		if (!res) {
			BreakIfDebugging();
			return;
		}

		// NOTE: caller must have bound the shader-visible heap via SetDescriptorHeaps()
		// and transitioned 'res' to UAV/UNORDERED_ACCESS with your enhanced barriers.
		rec->cl->ClearUnorderedAccessViewUint(gpu, cpu, res, v.v, 0, nullptr);
	}

	static void cl_clearUavFloat(CommandList* cl,
		const UavClearInfo& u,
		const UavClearFloat& v) noexcept
	{
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		if (!DxGetDstCpu(impl, u.cpuVisible, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) {
			BreakIfDebugging();
			return;
		}
		if (!DxGetDstGpu(impl, u.shaderVisible, gpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) {
			BreakIfDebugging();
			return;
		}

		ID3D12Resource* res = nullptr;
		if (u.resource.IsTexture()) {
			res = impl->textures.get(u.resource.GetHandle())->res.Get();
		}
		else {
			res = impl->buffers.get(u.resource.GetHandle())->res.Get();
		}
		if (!res) {
			BreakIfDebugging();
			return;
		}

		rec->cl->ClearUnorderedAccessViewFloat(gpu, cpu, res, v.v, 0, nullptr);
	}

	static UINT Align256(UINT x) { return (x + 255u) & ~255u; }

	// texture -> buffer
	static void cl_copyTextureToBuffer(rhi::CommandList* cl, const rhi::BufferTextureCopyFootprint& r) noexcept {
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		auto* T = impl->textures.get(r.texture);
		auto* B = impl->buffers.get(r.buffer);
		if (!T || !B || !T->res || !B->res) {
			BreakIfDebugging();
			return;
		}

		D3D12_TEXTURE_COPY_LOCATION dst{};
		dst.pResource = B->res.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst.PlacedFootprint.Offset = r.footprint.offset;
		dst.PlacedFootprint.Footprint.Format = T->fmt;     // texture’s actual DXGI format
		dst.PlacedFootprint.Footprint.Width = r.footprint.width;
		dst.PlacedFootprint.Footprint.Height = r.footprint.height;
		dst.PlacedFootprint.Footprint.Depth = r.footprint.depth;
		dst.PlacedFootprint.Footprint.RowPitch = r.footprint.rowPitch;

		D3D12_TEXTURE_COPY_LOCATION src{};
		src.pResource = T->res.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		src.SubresourceIndex = CalcSubresourceFor(*T, r.mip,
			(T->dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 0u : r.arraySlice);

		// Full subresource copy (nullptr box) at (x,y,z) inside the texture
		rec->cl->CopyTextureRegion(&dst, r.x, r.y, r.z, &src, nullptr);
	}

	// buffer -> texture (symmetric)
	static void cl_copyBufferToTexture(rhi::CommandList* cl, const rhi::BufferTextureCopyFootprint& r) noexcept {
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		auto* T = impl->textures.get(r.texture);
		auto* B = impl->buffers.get(r.buffer);
		if (!T || !B || !T->res || !B->res) {
			BreakIfDebugging();
			return;
		}

		D3D12_TEXTURE_COPY_LOCATION src{};
		src.pResource = B->res.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset = r.footprint.offset;
		src.PlacedFootprint.Footprint.Format = T->fmt;
		src.PlacedFootprint.Footprint.Width = r.footprint.width;
		src.PlacedFootprint.Footprint.Height = r.footprint.height;
		src.PlacedFootprint.Footprint.Depth = r.footprint.depth;
		src.PlacedFootprint.Footprint.RowPitch = r.footprint.rowPitch;

		D3D12_TEXTURE_COPY_LOCATION dst{};
		dst.pResource = T->res.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = CalcSubresourceFor(*T, r.mip,
			(T->dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 0u : r.arraySlice);

		rec->cl->CopyTextureRegion(&dst, r.x, r.y, r.z, &src, nullptr);
	}

	static void cl_copyTextureRegion(
		CommandList* cl,
		const TextureCopyRegion& dst,
		const TextureCopyRegion& src) noexcept
	{
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		auto* DstT = impl->textures.get(dst.texture);
		auto* SrcT = impl->textures.get(src.texture);
		if (!DstT || !SrcT || !DstT->res || !SrcT->res) {
			BreakIfDebugging();
			return;
		}

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

		// Clamp box to the src subresource bounds just in case
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

	static void cl_copyBufferRegion( CommandList* cl,
		ResourceHandle dst, uint64_t dstOffset,
		ResourceHandle src, uint64_t srcOffset,
		uint64_t numBytes) noexcept
	{
		if (!cl || !cl->IsValid() || numBytes == 0) {
			BreakIfDebugging();
			return;
		}

		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* dev = rec->dev.get();
		if (!rec || !dev) {
			BreakIfDebugging();
			return;
		}

		// Look up buffer resources
		auto* D = dev->buffers.get(dst);
		auto* S = dev->buffers.get(src);
		if (!D || !S || !D->res || !S->res) {
			BreakIfDebugging();
			return;
		}

		// We don't validate bounds here (we don’t store sizes). DX12 will validate.
		// Required states (caller's responsibility via barriers):
		//   src:  COPY_SOURCE   (ResourceAccessType::CopySource / Layout::CopySource)
		//   dst:  COPY_DEST     (ResourceAccessType::CopyDest   / Layout::CopyDest)
		rec->cl->CopyBufferRegion(
			D->res.Get(), dstOffset,
			S->res.Get(), srcOffset,
			numBytes);
	}

	static void cl_setName(CommandList* cl, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		if (!l || !l->cl) {
			BreakIfDebugging();
			return;
		}
		l->cl->SetName(std::wstring(n, n + ::strlen(n)).c_str());
	}

	static void cl_writeTimestamp(CommandList* cl, QueryPoolHandle pool, uint32_t index, Stage /*ignored*/) noexcept {
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!impl) {
			BreakIfDebugging();
			return;
		}

		auto* P = impl->queryPools.get(pool);
		if (!P || P->type != D3D12_QUERY_HEAP_TYPE_TIMESTAMP) {
			BreakIfDebugging();
			return;
		}

		rec->cl->EndQuery(P->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
	}

	static void cl_beginQuery(CommandList* cl, QueryPoolHandle pool, uint32_t index) noexcept {
		auto* rec = static_cast<Dx12CommandList*>(cl->impl);
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!impl) {
			BreakIfDebugging();
			return;
		}

		auto* P = impl->queryPools.get(pool);
		if (!P) {
			BreakIfDebugging();
			return;
		}

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
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!impl) {
			BreakIfDebugging();
			return;
		}

		auto* P = impl->queryPools.get(pool);
		if (!P) {
			BreakIfDebugging();
			return;
		}

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
		auto* impl = rec ? rec->dev.get() : nullptr;
		if (!impl) {
			BreakIfDebugging();
			return;
		}

		auto* P = impl->queryPools.get(pool);
		if (!P) {
			BreakIfDebugging();
			return;
		}

		// Resolve to the given buffer (assumed COPY_DEST)
		auto* B = impl->buffers.get(dst);
		if (!B || !B->res) {
			BreakIfDebugging();
			return;
		}

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

	static void cl_pushConstants(CommandList* c, ShaderStage stages,
		uint32_t set, uint32_t binding,
		uint32_t dstOffset32, uint32_t num32,
		const void* data) noexcept
	{
		auto* impl = static_cast<Dx12CommandList*>(c->impl);
		if (!impl || !impl->boundLayoutPtr || !data || num32 == 0) {
			BreakIfDebugging();
			return;
		}

		// Find the matching push-constant root param
		const Dx12PipelineLayout* L = impl->boundLayoutPtr;
		const Dx12PipelineLayout::RootConstParam* rc = nullptr;
		for (const auto& r : L->rcParams) { // TODO: Better lookup than linear scan
			if (r.set == set && r.binding == binding) { rc = &r; break; }
		}
		if (!rc) {
			BreakIfDebugging();
			return; // not declared in layout
		}
		// Clamp for safety
		const uint32_t maxAvail = rc->num32;
		if (dstOffset32 >= maxAvail) return;
		if (dstOffset32 + num32 > maxAvail) num32 = maxAvail - dstOffset32;

		// Write to requested stages. On DX12, graphics/compute have distinct root constant slots.
		const auto* p32 = static_cast<const uint32_t*>(data);
		if ((uint32_t)stages & (uint32_t)ShaderStage::Compute) {
			impl->cl->SetComputeRoot32BitConstants(rc->rootIndex, num32, p32, dstOffset32);
		}
		else {
			impl->cl->SetGraphicsRoot32BitConstants(rc->rootIndex, num32, p32, dstOffset32);
		}
	}

	static void cl_setPrimitiveTopology(CommandList* cl, PrimitiveTopology pt) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		if (!l) {
			BreakIfDebugging();
			return;
		}
		l->cl->IASetPrimitiveTopology(ToDX(pt));
	}

	static void cl_dispatchMesh(CommandList* cl, uint32_t x, uint32_t y, uint32_t z) noexcept {
		auto* l = static_cast<Dx12CommandList*>(cl->impl);
		if (!l) {
			BreakIfDebugging();
			return;
		}
		l->cl->DispatchMesh(x, y, z);
	}

	// ---------------- Swapchain vtable funcs ----------------
	static uint32_t sc_count(Swapchain* sc) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->count; }
	static uint32_t sc_curr(Swapchain* sc) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->sc->GetCurrentBackBufferIndex(); }
	//static ViewHandle sc_rtv(Swapchain* sc, uint32_t i) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->rtvHandles[i]; }
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
		return Result::Ok;
	}
	static void sc_setName(Swapchain* sc, const char* n) noexcept {} // Cannot name IDXGISwapChain
	// ---------------- Resource vtable funcs ----------------

	static void buf_map(Resource* r, void** data, uint64_t offset, uint64_t size) noexcept {
		if (!r || !data) {
			BreakIfDebugging();
			return;
		}
		auto* B = static_cast<Dx12Buffer*>(r->impl);
		if (!B || !B->res) { 
			*data = nullptr; 
			BreakIfDebugging(); 
			return; }

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
		if (!B || !B->res) {
			BreakIfDebugging();
			return;
		}
		D3D12_RANGE range{};
		if (writeSize != ~0ull) {
			range.Begin = SIZE_T(writeOffset);
			range.End = SIZE_T(writeOffset + writeSize);
		}
		B->res->Unmap(0, (writeSize != ~0ull) ? &range : nullptr);
	}

	static void buf_setName(Resource* r, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* B = static_cast<Dx12Buffer*>(r->impl);
		if (!B || !B->res) {
			BreakIfDebugging();
			return;
		}
		B->res->SetName(s2ws(n).c_str());
	}

	static void tex_map(Resource* r, void** data, uint64_t /*offset*/, uint64_t /*size*/) noexcept {
		if (!r || !data) {
			BreakIfDebugging();
			return;
		}
		auto* T = static_cast<Dx12Texture*>(r->impl);
		if (!T || !T->res) { 
			*data = nullptr; 
			BreakIfDebugging();
			return;
		}

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
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* T = static_cast<Dx12Texture*>(r->impl);
		if (!T || !T->res) {
			BreakIfDebugging();
			return;
		}
		T->res->SetName(s2ws(n).c_str());
	}

	// ------------------ Allocator vtable funcs ----------------
	static void ca_reset(CommandAllocator* ca) noexcept {
		if (!ca || !ca->impl) {
			BreakIfDebugging();
			return;
		}
		auto* A = static_cast<Dx12Allocator*>(ca->impl);
		A->alloc->Reset(); // ID3D12CommandAllocator::Reset()
	}

	// ------------------ QueryPool vtable funcs ----------------
	static QueryResultInfo qp_getQueryResultInfo(QueryPool* p) noexcept {
		auto* P = static_cast<Dx12QueryPool*>(p->impl);
		QueryResultInfo out{};
		if (!P) {
			BreakIfDebugging();
			return out;
		}
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

	static PipelineStatsLayout qp_getPipelineStatsLayout(QueryPool* p,
		PipelineStatsFieldDesc* outBuf,
		uint32_t cap) noexcept
	{
		auto* P = static_cast<Dx12QueryPool*>(p->impl);
		PipelineStatsLayout L{};
		if (!P) {
			BreakIfDebugging();
			return L;
		}

		L.info = qp_getQueryResultInfo(p);

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

	static void qp_setName(QueryPool* qp, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* Q = static_cast<Dx12QueryPool*>(qp->impl);
		if (!Q || !Q->heap) {
			BreakIfDebugging();
			return;
		}
		Q->heap->SetName(s2ws(n).c_str());
	}

	// ------------------ Pipeline vtable funcs ----------------
	static void pso_setName(Pipeline* p, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* P = static_cast<Dx12Pipeline*>(p->impl);
		if (!P || !P->pso) {
			BreakIfDebugging();
			return;
		}
		P->pso->SetName(s2ws(n).c_str());
	}

	// ------------------ PipelineLayout vtable funcs ----------------
	static void pl_setName(PipelineLayout* pl, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* L = static_cast<Dx12PipelineLayout*>(pl->impl);
		if (!L || !L->root) {
			BreakIfDebugging();
			return;
		}
		L->root->SetName(s2ws(n).c_str());
	}

	// ------------------ CommandSignature vtable funcs ----------------
	static void cs_setName(CommandSignature* cs, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* S = static_cast<Dx12CommandSignature*>(cs->impl);
		if (!S || !S->sig) {
			BreakIfDebugging();
			return;
		}
		S->sig->SetName(s2ws(n).c_str());
	}

	// ------------------ DescriptorHeap vtable funcs ----------------
	static void dh_setName(DescriptorHeap* dh, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* H = static_cast<Dx12DescriptorHeap*>(dh->impl);
		if (!H || !H->heap) {
			BreakIfDebugging();
			return;
		}
		H->heap->SetName(s2ws(n).c_str());
	}

	// ------------------ Sampler vtable funcs ----------------
	static void s_setName(Sampler* s, const char* n) noexcept {
		return; // cannot name ID3D12SamplerState
	}

	// ------------------ Timeline vtable funcs ----------------
	static uint64_t tl_timelineCompletedValue(Timeline* t) noexcept {
		auto* impl = static_cast<Dx12Timeline*>(t->impl);
		return impl->fence ? impl->fence->GetCompletedValue() : 0;
	}

	static Result tl_timelineHostWait(Timeline* tl, const uint64_t p) noexcept {
		auto* TL = static_cast<Dx12Timeline*>(tl->impl);
		HANDLE e = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!e) {
			BreakIfDebugging();
			return Result::Failed;
		}
		HRESULT hr = TL->fence->SetEventOnCompletion(p, e);
		if (FAILED(hr)) { 
			CloseHandle(e); 
			BreakIfDebugging();
			return Result::Failed;
		}
		WaitForSingleObject(e, INFINITE);
		CloseHandle(e);
		return Result::Ok;
	}
	static void tl_setName(Timeline* tl, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* T = static_cast<Dx12Timeline*>(tl->impl);
		if (!T || !T->fence) {
			BreakIfDebugging();
			return;
		}
		T->fence->SetName(s2ws(n).c_str());
	}

	// ------------------ Heap vtable funcs ----------------
	static void h_setName(Heap* h, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* H = static_cast<Dx12Heap*>(h->impl);
		if (!H || !H->heap) {
			BreakIfDebugging();
			return;
		}
		H->heap->SetName(s2ws(n).c_str());
	}

} // namespace rhi

#endif // _WIN32