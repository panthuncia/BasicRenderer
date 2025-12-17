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
#if BUILD_TYPE == BUILD_DEBUG
	if (IsDebuggerPresent()) {
		__debugbreak();
	}
#endif
}

inline std::wstring s2ws(const std::string& s) {
	int buffSize = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring ws(buffSize, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), buffSize);
	return ws;
}

namespace rhi {
	using Microsoft::WRL::ComPtr;
	struct Dx12Device;

	enum class Dx12ResourceKind : uint8_t { Buffer, Texture };

	struct Dx12Resource {
		// Texture constructor
		explicit Dx12Resource(ComPtr<ID3D12Resource> r, DXGI_FORMAT f, uint32_t width, uint32_t height, uint16_t mips, uint16_t arraySize, D3D12_RESOURCE_DIMENSION dim, uint16_t depth, std::shared_ptr<Dx12Device> d)
			: res(r), fmt(f), tex({ width, height, mips, arraySize, depth }), dev(d) {
		}
		// Buffer constructor
		explicit Dx12Resource(ComPtr<ID3D12Resource> r, uint64_t size, std::shared_ptr<Dx12Device> d)
			: res(r), kind(Dx12ResourceKind::Buffer), buf({ size }), dev(d) {
		}
		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		Dx12ResourceKind kind;

		DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
		//uint32_t w = 0, h = 0;
		//uint16_t mips = 1, arraySize = 1, depth = 1;
		struct Buffer { // for buffers
			uint64_t size = 0;
		};
		struct Texture { // for textures
			uint32_t w = 0, h = 0;
			uint16_t mips = 1;
			uint16_t arraySize = 1; // for 1D/2D/cube (cube arrays should already multiply by 6)
			uint16_t depth = 1;
		};
		union
		{
			Buffer buf;
			Texture tex;
		};

		D3D12_RESOURCE_DIMENSION dim = D3D12_RESOURCE_DIMENSION_UNKNOWN;
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
		d.Flags = ToDX(td.resourceFlags);
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
#if BUILD_TYPE == BUILD_DEBUG
		std::unordered_map<TimelineHandle, uint64_t, HandleHash<TimelineHandle>, HandleEqual<TimelineHandle>> lastSignaledValue; // track last signaled value per timeline
#endif
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

	template<> struct HandleFor<Dx12Resource> { using type = ResourceHandle; };
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

		Registry<Dx12Resource> resources;
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

} // namespace rhi

#endif // _WIN32