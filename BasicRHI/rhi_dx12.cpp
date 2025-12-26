#include "rhi_dx12.h"
#include <atlbase.h>

#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_security.h>
#include <string>

#include "rhi_dx12_casting.h"


#define VERIFY(expr) if (FAILED(expr)) { spdlog::error("Validation error!"); }

namespace rhi {

	static Result d_createPipelineFromStream(Device* d,
		const PipelineStreamItem* items,
		uint32_t count,
		PipelinePtr& out) noexcept
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
					RHI_FAIL(Result::InvalidArgument);
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
				inputLayoutDesc = { inputLayout.data(), static_cast<uint32_t>(inputLayout.size()) };
			} break;
			default: break;
			}
		}

		// Validate & decide kind
		if (hasCS && hasGfx) {
			spdlog::error("DX12 pipeline creation: cannot mix compute and graphics shaders in one PSO");
			RHI_FAIL(Result::InvalidArgument);
		}
		if (!hasCS && !hasGfx) {
			spdlog::error("DX12 pipeline creation: no shaders specified");
			RHI_FAIL(Result::InvalidArgument);
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
		auto hr = dimpl->pNativeDevice->CreatePipelineState(&sd, IID_PPV_ARGS(&pso));
		if (FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}

		auto handle = dimpl->pipelines.alloc(Dx12Pipeline(pso, isCompute));
		Pipeline ret(handle);
		ret.vt = &g_psovt;
		ret.impl = dimpl;

		out = MakePipelinePtr(d, ret);
		return Result::Ok;
	}

	static void d_destroyBuffer(DeviceDeletionContext* d, ResourceHandle h) noexcept
	{
		dx12_detail::Dev(d)->resources.free(h);
	}
	static void d_destroyTexture(DeviceDeletionContext* d, ResourceHandle h) noexcept
	{
		dx12_detail::Dev(d)->resources.free(h);
	}
	static void d_destroySampler(DeviceDeletionContext* d, SamplerHandle h) noexcept
	{
		dx12_detail::Dev(d)->samplers.free(h);
	}
	static void d_destroyPipeline(DeviceDeletionContext* d, PipelineHandle h) noexcept
	{
		dx12_detail::Dev(d)->pipelines.free(h);
	}

	static void d_destroyCommandList(DeviceDeletionContext* d, CommandList* p) noexcept {
		if (!d || !p || !p->IsValid()) {
			BreakIfDebugging();
			return;
		}
		auto* impl = dx12_detail::Dev(d);
		impl->commandLists.free(p->GetHandle());
		p->Reset();
	}

	static Queue d_getQueue(Device* d, QueueKind qk) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Queue out{ qk }; out.vt = &g_qvt;
		Dx12QueueState* s = (qk == QueueKind::Graphics ? &impl->gfx : qk == QueueKind::Compute ? &impl->comp : &impl->copy);
		out.impl = impl;
		return out;
	}

	static Result d_waitIdle(Device* d) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto waitQ = [](Dx12QueueState& q) {
			if (!q.pNativeQueue || !q.fence) return;
			q.pNativeQueue->Signal(q.fence.Get(), ++q.value);
			if (q.fence->GetCompletedValue() < q.value) {
				HANDLE e = CreateEvent(nullptr, FALSE, FALSE, nullptr);
				q.fence->SetEventOnCompletion(q.value, e);
				WaitForSingleObject(e, INFINITE);
				CloseHandle(e);
			}
			};
		waitQ(impl->gfx);
		waitQ(impl->comp);
		waitQ(impl->copy);
		return Result::Ok;
	}
	static void   d_flushDeletionQueue(Device*) noexcept {}

	// Swapchain create/destroy
	static Result d_createSwapchain(Device* d, void* hwnd, const uint32_t w, const uint32_t h, const Format fmt, const uint32_t bufferCount, const bool allowTearing, SwapchainPtr& out) noexcept {
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
		if (const auto hr = impl->pSLProxyFactory->CreateSwapChainForHwnd(impl->gfx.pSLProxyQueue.Get(), static_cast<HWND>(hwnd), &desc, nullptr, nullptr, &sc1); FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}
		ComPtr<IDXGISwapChain3> proxySc3;
		if (const auto hr = sc1.As(&proxySc3); FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}

		IDXGISwapChain3* nativeSc3Raw = nullptr;
		slGetNativeInterface(proxySc3.Get(), (void**)&nativeSc3Raw);
		ComPtr<IDXGISwapChain3> nativeSc3;
		nativeSc3.Attach(nativeSc3Raw);

		std::vector<ComPtr<ID3D12Resource>> imgs(bufferCount);
		std::vector<ResourceHandle> imgHandles(bufferCount);

		for (UINT i = 0; i < bufferCount; i++) {
			proxySc3->GetBuffer(i, IID_PPV_ARGS(&imgs[i]));
			// Register as a TextureHandle
			Dx12Resource t(imgs[i], desc.Format, w, h, 1, 1, D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1);
			imgHandles[i] = impl->resources.alloc(t);

		}

		auto scWrap = Dx12Swapchain(
			nativeSc3, proxySc3, desc.Format, w, h, bufferCount,
			imgs, imgHandles
		);

		auto scHandle = impl->swapchains.alloc(scWrap);

		Swapchain ret{ scHandle };
		ret.impl = impl;
		ret.vt = &g_scvt;
		out = MakeSwapchainPtr(d, ret);
		return Result::Ok;
	}

	static void d_destroySwapchain(DeviceDeletionContext*, Swapchain* sc) noexcept {
		auto* s = dx12_detail::SC(sc);
		if (!s) { sc->impl = nullptr; sc->vt = nullptr; return; }

		// Ensure GPU is idle before ripping out backbuffers.
		auto dev = dx12_detail::Dev(sc);
		if (dev && dev->self.vt && dev->self.vt->deviceWaitIdle) {
			(void)dev->self.vt->deviceWaitIdle(&dev->self);
		}

		if (dev)
		{
			// Release references held by the resource registry and free the handles.
			for (auto h : s->imageHandles)
			{
				if (h.generation != 0) // invalid guard
				{
					if (auto* r = dev->resources.get(h))
						r->res.Reset(); // drop ID3D12Resource ref so DXGI can actually destroy
					dev->resources.free(h);
				}
			}
		}
		
		dev->swapchains.free(sc->GetHandle());
		sc->impl = nullptr;
		sc->vt = nullptr;
	}

	static void d_destroyDevice(Device* d) noexcept {
		// Wait for idle
		auto* impl = static_cast<Dx12Device*>(d->impl);

		//impl->self.vt->deviceWaitIdle(&impl->self); // Fails
		//d->WaitIdle(); // Also fails

		// Clear all registries (drops ComPtrs inside the stored objects)
		impl->swapchains.clear();
		impl->queryPools.clear();
		impl->timelines.clear();
		impl->commandLists.clear();
		impl->allocators.clear();
		impl->descHeaps.clear();
		impl->commandSignatures.clear();
		impl->pipelines.clear();
		impl->pipelineLayouts.clear();
		impl->samplers.clear();
		impl->resources.clear();
		impl->heaps.clear();

		// release queues/fences
		impl->gfx = {};
		impl->comp = {};
		impl->copy = {};

		// release device/factory
		impl->pSLProxyDevice.Reset();
		impl->pNativeDevice.Reset();
		impl->pSLProxyFactory.Reset();
		impl->pNativeFactory.Reset();
		impl->adapter.Reset();

		// invalidate the RHI handle
		d->impl = nullptr;
		d->vt = nullptr;

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

	static Result d_createPipelineLayout(Device* d, const PipelineLayoutDesc& ld, PipelineLayoutPtr& out) noexcept {
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
		HRESULT hr = D3DX12SerializeVersionedRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err);
		if (FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
		hr = impl->pNativeDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
			IID_PPV_ARGS(&root));
		if (FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}

		Dx12PipelineLayout L(ld);
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
		if (ld.staticSamplers.size && ld.staticSamplers.data) {
			L.staticSamplers.assign(ld.staticSamplers.data, ld.staticSamplers.data + ld.staticSamplers.size);
		}
		L.root = root;
		auto handle = impl->pipelineLayouts.alloc(L);
		PipelineLayout ret(handle);
		ret.vt = &g_plvt;
		ret.impl = impl;
		out = MakePipelineLayoutPtr(d, ret);
		return Result::Ok;
	}

	static void CopyUtf8FromWide(const wchar_t* src, char* dst, size_t dstCap) {
		if (!dst || dstCap == 0) return;
		dst[0] = '\0';
		if (!src) return;
		int written = WideCharToMultiByte(
			CP_UTF8,
			0, src,
			-1, dst,
			static_cast<int>(dstCap),
			nullptr,
			nullptr);
		if (written <= 0) dst[0] = '\0';
		dst[dstCap - 1] = '\0';
	}

	inline std::vector<D3D_SHADER_MODEL> shaderModels = {
		D3D_SHADER_MODEL_6_8,
		D3D_SHADER_MODEL_6_7,
		D3D_SHADER_MODEL_6_6,
		D3D_SHADER_MODEL_6_5,
		D3D_SHADER_MODEL_6_4,
		D3D_SHADER_MODEL_6_3,
		D3D_SHADER_MODEL_6_2,
		D3D_SHADER_MODEL_6_1,
		D3D_SHADER_MODEL_6_0,
	};

	inline static D3D_SHADER_MODEL getHighestShaderModel(ID3D12Device* dev)
	{
		if (!dev) return D3D_SHADER_MODEL_6_0;

		for (const auto& model : shaderModels) {
			D3D12_FEATURE_DATA_SHADER_MODEL sm{};
			sm.HighestShaderModel = model;
			if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) {
				return model;
			}
		}
		BreakIfDebugging(); // Nothing under 6_0 is supported
		return D3D_SHADER_MODEL_6_0;
	}

	template<class T>
	concept HasPerPrimitiveVrs = requires(T t) { t.PerPrimitiveShadingRateSupportedWithViewportIndexing; };

	template<class T>
	concept HasAdditionalVrsRates = requires(T t) { t.AdditionalShadingRatesSupported; };

	static Result d_queryFeatureInfo(const Device* d, FeatureInfoHeader* chain) noexcept
	{
		if (!d || !chain) return Result::InvalidArgument;

		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->pNativeDevice) return Result::Failed;

		ID3D12Device* dev = impl->pNativeDevice.Get();

		D3D12_FEATURE_DATA_D3D12_OPTIONS   opt0{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS1  opt1{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS3  opt3{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS5  opt5{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS6  opt6{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS7  opt7{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS9  opt9{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS11 opt11{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS12 opt12{};
		D3D12_FEATURE_DATA_D3D12_OPTIONS16 opt16{};
		D3D12_FEATURE_DATA_TIGHT_ALIGNMENT ta{};

		// Note: if a CheckFeatureSupport fails, the struct stays zeroed => "unsupported".
		(void)dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opt0, sizeof(opt0));
		(void)dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &opt1, sizeof(opt1));
		(void)dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &opt3, sizeof(opt3));
		(void)dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5));
		auto hasOptions6 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &opt6, sizeof(opt6)));
		auto hasOptions7 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7)));
		auto hasOptions9 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &opt9, sizeof(opt9)));
		auto hasOptions11 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS11, &opt11, sizeof(opt11)));
		auto hasOptions12 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &opt12, sizeof(opt12)));
		auto hasOptions16 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &opt16, sizeof(opt16)));
		auto hasTightAlignment = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_TIGHT_ALIGNMENT, &ta, sizeof(ta)));

		D3D12_FEATURE_DATA_SHADER_MODEL sm{};
		sm.HighestShaderModel = getHighestShaderModel(dev);

		// Architecture (UMA, etc.)
		D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
		arch.NodeIndex = 0;
		(void)dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch));

		bool gpuUploadHeapSupported = false;
		if (hasOptions16) {
			gpuUploadHeapSupported = opt16.GPUUploadHeapSupported;
		}

		bool tightAlignmentSupported = false;
		if (hasTightAlignment) {
			tightAlignmentSupported = (ta.SupportTier >= D3D12_TIGHT_ALIGNMENT_TIER_1);
		}

		const bool createNotZeroedSupported = hasOptions7;

		const bool hasMeshShaders =
			(opt7.MeshShaderTier == D3D12_MESH_SHADER_TIER_1);

		const bool hasRayTracingPipeline =
			(opt5.RaytracingTier == D3D12_RAYTRACING_TIER_1_0) ||
			(opt5.RaytracingTier == D3D12_RAYTRACING_TIER_1_1);

		const bool hasRayTracing11 =
			(opt5.RaytracingTier == D3D12_RAYTRACING_TIER_1_1);

		const bool hasVrsPerDraw =
			(opt6.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_1) ||
			(opt6.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_2);

		const bool hasVrsAttachment =
			(opt6.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_2);

		const bool unifiedResourceHeaps =
			(opt0.ResourceHeapTier == D3D12_RESOURCE_HEAP_TIER_2);

		const bool unboundedDescriptorTables =
			(opt0.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3);

		// Optional DX12 Options11-derived semantics
		bool derivativesInMeshAndTask = false;
		bool atomicInt64GroupShared = false;
		bool atomicInt64Typed = false;
		bool atomicInt64OnDescriptorHeapResources = false;
		if (hasOptions9) {
			derivativesInMeshAndTask = opt9.DerivativesInMeshAndAmplificationShadersSupported;
			atomicInt64GroupShared = opt9.AtomicInt64OnGroupSharedSupported;
			atomicInt64Typed = opt9.AtomicInt64OnTypedResourceSupported;
		}
		if (hasOptions11) {
			atomicInt64OnDescriptorHeapResources = opt11.AtomicInt64OnDescriptorHeapResourceSupported;
		}

		//Walk requested chain and fill
		for (FeatureInfoHeader* h = chain; h; h = h->pNext) {
			if (h->structSize < sizeof(FeatureInfoHeader))
				return Result::InvalidArgument;

			switch (h->sType) {
			case FeatureInfoStructType::AdapterInfo: {
				if (h->structSize < sizeof(AdapterFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<AdapterFeatureInfo*>(h);

				DXGI_ADAPTER_DESC3 desc{};
				if (impl->adapter && SUCCEEDED(impl->adapter->GetDesc3(&desc))) {
					CopyUtf8FromWide(desc.Description, out->name, sizeof(out->name));
					out->vendorId = desc.VendorId;
					out->deviceId = desc.DeviceId;
					out->dedicatedVideoMemory = desc.DedicatedVideoMemory;
					out->dedicatedSystemMemory = desc.DedicatedSystemMemory;
					out->sharedSystemMemory = desc.SharedSystemMemory;
				}
				else {
					// Leave defaults if adapter unavailable.
					out->name[0] = '\0';
					out->vendorId = out->deviceId = 0;
					out->dedicatedVideoMemory = out->dedicatedSystemMemory = out->sharedSystemMemory = 0;
				}
			} break;

			case FeatureInfoStructType::Architecture: {
				if (h->structSize < sizeof(ArchitectureFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<ArchitectureFeatureInfo*>(h);
				out->uma = arch.UMA;
				out->cacheCoherentUMA = arch.CacheCoherentUMA;
				out->isolatedMMU = arch.IsolatedMMU;
				out->tileBasedRenderer = arch.TileBasedRenderer;
			} break;

			case FeatureInfoStructType::Features: {
				if (h->structSize < sizeof(ShaderFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<ShaderFeatureInfo*>(h);

				out->maxShaderModel = ToRHI(sm.HighestShaderModel);

				// Tier-derived semantics:
				out->unifiedResourceHeaps = unifiedResourceHeaps;
				out->unboundedDescriptorTables = unboundedDescriptorTables;

				// "Actual shader capability" bits:
				out->waveOps = opt1.WaveOps;
				out->int64ShaderOps = opt1.Int64ShaderOps;
				out->barycentrics = opt3.BarycentricsSupported;

				// Options11-derived bits (or false if not available):
				out->derivativesInMeshAndTaskShaders = derivativesInMeshAndTask;
				out->atomicInt64OnGroupShared = atomicInt64GroupShared;
				out->atomicInt64OnTypedResource = atomicInt64Typed;
				out->atomicInt64OnDescriptorHeapResources = atomicInt64OnDescriptorHeapResources;
			} break;

			case FeatureInfoStructType::MeshShaders: {
				if (h->structSize < sizeof(MeshShaderFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<MeshShaderFeatureInfo*>(h);

				// DX12 exposes this as a single tier: if present, both mesh+amplification exist.
				out->meshShader = hasMeshShaders;
				out->taskShader = hasMeshShaders;

				// Derivatives support is independent (Options11). Only meaningful if mesh shaders exist.
				out->derivatives = hasMeshShaders && derivativesInMeshAndTask;
			} break;

			case FeatureInfoStructType::RayTracing: {
				if (h->structSize < sizeof(RayTracingFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<RayTracingFeatureInfo*>(h);

				// DXR tier -> semantic bits.
				out->pipeline = hasRayTracingPipeline;

				// DXR 1.1 implies inline raytracing
				out->rayQuery = hasRayTracing11;

				// Also a heuristic: "indirect trace" is not a single clean DX12 bit in core options;
				out->indirect = hasRayTracing11;
			} break;

			case FeatureInfoStructType::ShadingRate: {
				if (h->structSize < sizeof(ShadingRateFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<ShadingRateFeatureInfo*>(h);

				out->perDrawRate = hasVrsPerDraw;
				out->attachmentRate = hasVrsAttachment;

				// Only meaningful if attachmentRate.
				out->tileSize = hasVrsAttachment ? opt6.ShadingRateImageTileSize : 0;

				if constexpr (HasAdditionalVrsRates<decltype(opt6)>) {
					out->additionalRates = (opt6.AdditionalShadingRatesSupported != 0);
				}
				else {
					out->additionalRates = false;
				}

				if constexpr (HasPerPrimitiveVrs<decltype(opt6)>) {
					out->perPrimitiveRate = (opt6.PerPrimitiveShadingRateSupportedWithViewportIndexing != 0);
				}
				else {
					out->perPrimitiveRate = false;
				}
			} break;

			case FeatureInfoStructType::EnhancedBarriers: {
				if (h->structSize < sizeof(EnhancedBarriersFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<EnhancedBarriersFeatureInfo*>(h);

				if (hasOptions12) {
					out->enhancedBarriers = (opt12.EnhancedBarriersSupported != 0);
					out->relaxedFormatCasting = (opt12.RelaxedFormatCastingSupported != 0);
				}
				else {
					out->enhancedBarriers = false;
					out->relaxedFormatCasting = false;
				}
			} break;
			case FeatureInfoStructType::ResourceAllocation: {
				if (h->structSize < sizeof(ResourceAllocationFeatureInfo)) return Result::InvalidArgument;
				auto* out = reinterpret_cast<ResourceAllocationFeatureInfo*>(h);

				out->gpuUploadHeapSupported = gpuUploadHeapSupported;
				out->tightAlignmentSupported = tightAlignmentSupported;
				out->createNotZeroedHeapSupported = createNotZeroedSupported;
			} break;
			default:
				// Unknown sType: ignore
				break;
			}
		}

		return Result::Ok;
	}

	static Result d_queryVideoMemoryInfo(
		const Device* d,
		uint32_t nodeIndex,
		MemorySegmentGroup segmentGroup,
		VideoMemoryInfo& out) noexcept
	{
		if (!d) return Result::InvalidArgument;

		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->adapter) return Result::Failed;

		ComPtr<IDXGIAdapter3> a3;
		HRESULT hr = impl->adapter.As(&a3);
		if (FAILED(hr) || !a3) return Result::Unsupported;

		DXGI_MEMORY_SEGMENT_GROUP dxGroup = DXGI_MEMORY_SEGMENT_GROUP_LOCAL;
		switch (segmentGroup) {
		case MemorySegmentGroup::Local:    dxGroup = DXGI_MEMORY_SEGMENT_GROUP_LOCAL; break;
		case MemorySegmentGroup::NonLocal: dxGroup = DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL; break;
		default: return Result::InvalidArgument;
		}

		DXGI_QUERY_VIDEO_MEMORY_INFO info{};
		hr = a3->QueryVideoMemoryInfo(nodeIndex, dxGroup, &info);

		if (FAILED(hr)) {
			return ToRHI(hr);
		}

		out.budgetBytes = info.Budget;
		out.currentUsageBytes = info.CurrentUsage;
		out.availableForReservationBytes = info.AvailableForReservation;
		out.currentReservationBytes = info.CurrentReservation;
		return Result::Ok;
	}

	static void d_destroyPipelineLayout(DeviceDeletionContext* d, PipelineLayoutHandle h) noexcept {
		dx12_detail::Dev(d)->pipelineLayouts.free(h);
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

	static Result d_createCommandSignature(Device* d,
		const CommandSignatureDesc& cd,
		const PipelineLayoutHandle layout, CommandSignaturePtr& out) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);

		std::vector<D3D12_INDIRECT_ARGUMENT_DESC> dxArgs(cd.args.size);
		bool hasRoot = false;
		for (uint32_t i = 0; i < cd.args.size; ++i) {
			if (!FillDx12Arg(cd.args.data[i], dxArgs[i])) {
				RHI_FAIL(Result::InvalidArgument);
			}
			hasRoot |= (cd.args.data[i].kind == IndirectArgKind::Constant);
		}

		ID3D12RootSignature* rs = nullptr;
		if (hasRoot) {
			auto* L = impl->pipelineLayouts.get(layout);
			if (!L || !L->root) {
				RHI_FAIL(Result::InvalidArgument);
			}
			rs = L->root.Get();
		}

		D3D12_COMMAND_SIGNATURE_DESC desc{};
		desc.pArgumentDescs = dxArgs.data();
		desc.NumArgumentDescs = static_cast<UINT>(dxArgs.size());
		desc.ByteStride = cd.byteStride;

		Microsoft::WRL::ComPtr<ID3D12CommandSignature> cs;
		if (const auto hr = impl->pNativeDevice->CreateCommandSignature(&desc, rs, IID_PPV_ARGS(&cs)); FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}
		const Dx12CommandSignature s(cs, cd.byteStride);
		const auto handle = impl->commandSignatures.alloc(s);
		CommandSignature ret(handle);
		ret.vt = &g_csvt;
		ret.impl = impl;
		out = MakeCommandSignaturePtr(d, ret);
		return Result::Ok;
	}

	static void d_destroyCommandSignature(DeviceDeletionContext* d, CommandSignatureHandle h) noexcept {
		dx12_detail::Dev(d)->commandSignatures.free(h);
	}

	static Result d_createDescriptorHeap(Device* d, const DescriptorHeapDesc& hd, DescriptorHeapPtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);

		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = ToDX(hd.type);
		desc.NumDescriptors = hd.capacity;
		desc.Flags = hd.shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		if (const auto hr = impl->pNativeDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)); FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}

		const auto descriptorSize = impl->pNativeDevice->GetDescriptorHandleIncrementSize(desc.Type);
		const Dx12DescriptorHeap H(heap, desc.Type, descriptorSize, (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0);

		const auto handle = impl->descHeaps.alloc(H);
		DescriptorHeap ret(handle);
		ret.vt = &g_dhvt;
		ret.impl = impl;
		out = MakeDescriptorHeapPtr(d, ret);
		return Result::Ok;
	}
	static void d_destroyDescriptorHeap(DeviceDeletionContext* d, DescriptorHeapHandle h) noexcept {
		dx12_detail::Dev(d)->descHeaps.free(h);
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
			return Result::InvalidArgument;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Shader4ComponentMapping = (dv.componentMapping == 0)
			? D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
			: dv.componentMapping;

		switch (dv.dimension) {
		case SrvDim::Buffer: {
			auto* B = impl->resources.get(resource);
			if (!B || !B->res) {
				BreakIfDebugging();
				return Result::InvalidArgument;
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
			impl->pNativeDevice->CreateShaderResourceView(B->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture1D: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			desc.Texture1D.MostDetailedMip = dv.tex1D.mostDetailedMip;
			desc.Texture1D.MipLevels = dv.tex1D.mipLevels;
			desc.Texture1D.ResourceMinLODClamp = dv.tex1D.minLodClamp;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture1DArray: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			desc.Texture1DArray.MostDetailedMip = dv.tex1DArray.mostDetailedMip;
			desc.Texture1DArray.MipLevels = dv.tex1DArray.mipLevels;
			desc.Texture1DArray.FirstArraySlice = dv.tex1DArray.firstArraySlice;
			desc.Texture1DArray.ArraySize = dv.tex1DArray.arraySize;
			desc.Texture1DArray.ResourceMinLODClamp = dv.tex1DArray.minLodClamp;
			auto* R = T->res.Get();
			impl->pNativeDevice->CreateShaderResourceView(R, &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2D: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MostDetailedMip = dv.tex2D.mostDetailedMip;
			desc.Texture2D.MipLevels = dv.tex2D.mipLevels;
			desc.Texture2D.PlaneSlice = dv.tex2D.planeSlice;
			desc.Texture2D.ResourceMinLODClamp = dv.tex2D.minLodClamp;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2DArray: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MostDetailedMip = dv.tex2DArray.mostDetailedMip;
			desc.Texture2DArray.MipLevels = dv.tex2DArray.mipLevels;
			desc.Texture2DArray.FirstArraySlice = dv.tex2DArray.firstArraySlice;
			desc.Texture2DArray.ArraySize = dv.tex2DArray.arraySize;
			desc.Texture2DArray.PlaneSlice = dv.tex2DArray.planeSlice;
			desc.Texture2DArray.ResourceMinLODClamp = dv.tex2DArray.minLodClamp;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2DMS: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture2DMSArray: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
			desc.Texture2DMSArray.FirstArraySlice = dv.tex2DMSArray.firstArraySlice;
			desc.Texture2DMSArray.ArraySize = dv.tex2DMSArray.arraySize;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::Texture3D: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MostDetailedMip = dv.tex3D.mostDetailedMip;
			desc.Texture3D.MipLevels = dv.tex3D.mipLevels;
			desc.Texture3D.ResourceMinLODClamp = dv.tex3D.minLodClamp;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::TextureCube: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			desc.TextureCube.MostDetailedMip = dv.cube.mostDetailedMip;
			desc.TextureCube.MipLevels = dv.cube.mipLevels;
			desc.TextureCube.ResourceMinLODClamp = dv.cube.minLodClamp;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::TextureCubeArray: {
			auto* T = impl->resources.get(resource); if (!T || !T->res) return Result::InvalidArgument;
			desc.Format = (dv.formatOverride == Format::Unknown) ? T->fmt : ToDxgi(dv.formatOverride);
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			desc.TextureCubeArray.MostDetailedMip = dv.cubeArray.mostDetailedMip;
			desc.TextureCubeArray.MipLevels = dv.cubeArray.mipLevels;
			desc.TextureCubeArray.First2DArrayFace = dv.cubeArray.first2DArrayFace;
			desc.TextureCubeArray.NumCubes = dv.cubeArray.numCubes;
			desc.TextureCubeArray.ResourceMinLODClamp = dv.cubeArray.minLodClamp;
			impl->pNativeDevice->CreateShaderResourceView(T->res.Get(), &desc, dst);
			return Result::Ok;
		}

		case SrvDim::AccelerationStruct: {
			// AS is stored in a buffer with ResourceFlags::RaytracingAccelerationStructure
			auto* B = impl->resources.get(resource); if (!B || !B->res) return Result::InvalidArgument;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			desc.RaytracingAccelerationStructure.Location = B->res->GetGPUVirtualAddress();
			impl->pNativeDevice->CreateShaderResourceView(B->res.Get(), &desc, dst);
			return Result::Ok;
		}

		default: break;
		}
		BreakIfDebugging();
		return Result::InvalidArgument;
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
			return Result::InvalidArgument;
		}

		D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
		ID3D12Resource* pResource = nullptr;
		ID3D12Resource* pCounterResource = nullptr; // optional, for structured append/consume

		switch (dv.dimension)
		{
			// ========================= Buffer UAV =========================
		case UavDim::Buffer:
		{
			auto* B = impl->resources.get(resource);
			if (!B || !B->res) {
				BreakIfDebugging();
				return Result::InvalidArgument;
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

			impl->pNativeDevice->CreateUnorderedAccessView(pResource, pCounterResource, &desc, dst);
			return Result::Ok;
		}

		// ========================= Texture UAVs =========================
		case UavDim::Texture1D:
		{
			auto* T = impl->resources.get(resource);
			if (!T || !T->res) {
				BreakIfDebugging();
				return Result::InvalidArgument;
			}
			pResource = T->res.Get();
			if (dv.formatOverride != Format::Unknown) {
				desc.Format = ToDxgi(dv.formatOverride);
			}
			else {
				desc.Format = T->fmt;
			}
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			desc.Texture1D.MipSlice = dv.texture1D.mipSlice;

			impl->pNativeDevice->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture1DArray:
		{
			auto* T = impl->resources.get(resource);
			if (!T || !T->res) {
				BreakIfDebugging();
				return Result::InvalidArgument;
			}
			pResource = T->res.Get();

			if (dv.formatOverride != Format::Unknown) {
				desc.Format = ToDxgi(dv.formatOverride);
			}
			else {
				desc.Format = T->fmt;
			}
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			desc.Texture1DArray.MipSlice = dv.texture1DArray.mipSlice;
			desc.Texture1DArray.FirstArraySlice = dv.texture1DArray.firstArraySlice;
			desc.Texture1DArray.ArraySize = dv.texture1DArray.arraySize;

			impl->pNativeDevice->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2D:
		{
			auto* T = impl->resources.get(resource);
			if (!T || !T->res) {
				BreakIfDebugging();
				return Result::InvalidArgument;
			}
			pResource = T->res.Get();

			if (dv.formatOverride != Format::Unknown) {
				desc.Format = ToDxgi(dv.formatOverride);
			}
			else {
				desc.Format = T->fmt;
			}			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = dv.texture2D.mipSlice;
			desc.Texture2D.PlaneSlice = dv.texture2D.planeSlice;

			impl->pNativeDevice->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2DArray:
		{
			auto* T = impl->resources.get(resource);
			if (!T || !T->res) {
				BreakIfDebugging();
				return Result::InvalidArgument;
			}
			pResource = T->res.Get();

			if (dv.formatOverride != Format::Unknown) {
				desc.Format = ToDxgi(dv.formatOverride);
			}
			else {
				desc.Format = T->fmt;
			}			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = dv.texture2DArray.mipSlice;
			desc.Texture2DArray.FirstArraySlice = dv.texture2DArray.firstArraySlice;
			desc.Texture2DArray.ArraySize = dv.texture2DArray.arraySize;
			desc.Texture2DArray.PlaneSlice = dv.texture2DArray.planeSlice;

			impl->pNativeDevice->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture3D:
		{
			auto* T = impl->resources.get(resource);
			if (!T || !T->res) {
				BreakIfDebugging();
				return Result::InvalidArgument;
			}
			pResource = T->res.Get();

			if (dv.formatOverride != Format::Unknown) {
				desc.Format = ToDxgi(dv.formatOverride);
			}
			else {
				desc.Format = T->fmt;
			}			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MipSlice = dv.texture3D.mipSlice;
			desc.Texture3D.FirstWSlice = dv.texture3D.firstWSlice;
			desc.Texture3D.WSize = (dv.texture3D.wSize == 0) ? UINT(-1) : dv.texture3D.wSize;

			impl->pNativeDevice->CreateUnorderedAccessView(pResource, nullptr, &desc, dst);
			return Result::Ok;
		}

		case UavDim::Texture2DMS:
		case UavDim::Texture2DMSArray:
			// UAVs for MSAA textures are not supported by D3D12
			BreakIfDebugging();
			return Result::Unsupported;
		}
		BreakIfDebugging();
		return Result::InvalidArgument;
	}

	static Result d_createConstantBufferView(Device* d, DescriptorSlot s, const ResourceHandle& bh, const CbvDesc& dv) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) return Result::InvalidArgument;
		auto* B = impl->resources.get(bh); if (!B) {
			BreakIfDebugging();
			return Result::InvalidArgument;
		}

		D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
		desc.BufferLocation = B->res->GetGPUVirtualAddress() + dv.byteOffset;
		desc.SizeInBytes = (UINT)((dv.byteSize + 255) & ~255u);
		impl->pNativeDevice->CreateConstantBufferView(&desc, dst);
		return Result::Ok;
	}

	static Result d_createSampler(Device* d, DescriptorSlot s, const SamplerDesc& sd) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		D3D12_CPU_DESCRIPTOR_HANDLE dst{};
		if (!DxGetDstCpu(impl, s, dst, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)) {
			BreakIfDebugging();
			return Result::InvalidArgument;
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

		impl->pNativeDevice->CreateSampler(&desc, dst);
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
			return Result::InvalidArgument;
		}

		// For texture RTVs we expect a texture resource
		auto* T = impl->resources.get(texture);
		if (!T && rd.dimension != RtvDim::Buffer) {
			BreakIfDebugging();
			return Result::InvalidArgument;
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

		impl->pNativeDevice->CreateRenderTargetView(pRes, &r, dst);
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
			return Result::InvalidArgument;
		}

		auto* T = impl->resources.get(texture);
		if (!T) {
			BreakIfDebugging();
			return Result::InvalidArgument;
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

		impl->pNativeDevice->CreateDepthStencilView(T->res.Get(), &z, dst);
		return Result::Ok;
	}

	static Result d_createCommandAllocator(Device* d, QueueKind q, CommandAllocatorPtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> a;
		if (const auto hr = impl->pNativeDevice->CreateCommandAllocator(ToDX(q), IID_PPV_ARGS(&a)); FAILED(hr)) {
			__debugbreak();
			return ToRHI(hr);
		}

		Dx12Allocator A(a, ToDX(q));
		const auto h = impl->allocators.alloc(A);

		CommandAllocator ret{ h };
		ret.impl = impl;
		ret.vt = &g_calvt;
		out = MakeCommandAllocatorPtr(d, ret);
		return Result::Ok;
	}

	static void d_destroyCommandAllocator(DeviceDeletionContext* d, CommandAllocator* ca) noexcept {
		dx12_detail::Dev(d)->allocators.free(ca->GetHandle());
	}

	static Result d_createCommandList(Device* d, QueueKind q, CommandAllocator ca, CommandListPtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		const auto* A = dx12_detail::Alloc(&ca);
		if (!A) {
			RHI_FAIL(Result::InvalidArgument);
		}

		ComPtr<ID3D12GraphicsCommandList7> cl;
		if (const auto hr = impl->pNativeDevice->CreateCommandList(0, A->type, A->alloc.Get(), nullptr, IID_PPV_ARGS(&cl)); FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}
		const Dx12CommandList rec(cl, A->alloc, A->type);
		const auto h = impl->commandLists.alloc(rec);

		CommandList ret{ h };
		ret.impl = impl;
		ret.vt = &g_clvt;
		out = MakeCommandListPtr(d, ret);
		return Result::Ok;
	}

	static Result d_createCommittedBuffer(Device* d, const ResourceDesc& bd, ResourcePtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->pNativeDevice || bd.buffer.sizeBytes == 0) {
			RHI_FAIL(Result::InvalidArgument);
		}

		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = ToDx(bd.heapType);
		hp.CreationNodeMask = 1;
		hp.VisibleNodeMask = 1;

		const auto hf = ToDX(bd.heapFlags);

		const D3D12_RESOURCE_FLAGS flags = ToDX(bd.resourceFlags);
		const D3D12_RESOURCE_DESC1  desc = MakeBufferDesc1(bd.buffer.sizeBytes, flags);

		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		// Buffers must use UNDEFINED layout per spec
		constexpr D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;

		std::vector<DXGI_FORMAT> castableFormats;
		for (const auto& fmt : bd.castableFormats) {
			castableFormats.push_back(ToDxgi(fmt));
		}

		const HRESULT hr = impl->pNativeDevice->CreateCommittedResource3(
			&hp,
			hf,
			&desc,
			initialLayout,
			/*pOptimizedClearValue*/ nullptr,        // buffers: must be null
			/*pProtectedSession*/   nullptr,
			/*NumCastableFormats*/   static_cast<uint32_t>(castableFormats.size()),
			/*pCastableFormats*/     castableFormats.data(),
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}

		if (bd.debugName) {
			res->SetName(std::wstring(bd.debugName, bd.debugName + ::strlen(bd.debugName)).c_str());
		}

		Dx12Resource B(std::move(res), bd.buffer.sizeBytes);
		const auto handle = impl->resources.alloc(B);

		Resource ret{ handle, false };
		ret.impl = impl;
		ret.vt = &g_buf_rvt;
		out = MakeBufferPtr(d, ret);
		return Result::Ok;
	}

	static Result d_createCommittedTexture(Device* d, const ResourceDesc& td, ResourcePtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->pNativeDevice || td.texture.width == 0 || td.texture.height == 0 || td.texture.format == Format::Unknown) {
			RHI_FAIL(Result::InvalidArgument);
		}

		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = ToDx(td.heapType);
		hp.CreationNodeMask = 1;
		hp.VisibleNodeMask = 1;

		const auto hf = ToDX(td.heapFlags);

		const D3D12_RESOURCE_DESC1 desc = MakeTexDesc1(td);

		D3D12_CLEAR_VALUE* pClear = nullptr;
		D3D12_CLEAR_VALUE clear;
		if (td.texture.optimizedClear) {
			clear = ToDX(*td.texture.optimizedClear);
			pClear = &clear;
		}
		// Textures can specify InitialLayout (enhanced barriers)
		const D3D12_BARRIER_LAYOUT initialLayout = ToDX(td.texture.initialLayout);

		std::vector<DXGI_FORMAT> castableFormats;
		for (const auto& fmt : td.castableFormats) {
			castableFormats.push_back(ToDxgi(fmt));
		}

		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		HRESULT hr = impl->pNativeDevice->CreateCommittedResource3(
			&hp,
			hf,
			&desc,
			initialLayout,
			pClear,
			/*pProtectedSession*/ nullptr,
			/*NumCastableFormats*/ static_cast<uint32_t>(castableFormats.size()),
			/*pCastableFormats*/   castableFormats.data(),
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) {
			spdlog::error("Failed to create committed texture: {0}", hr);
			RHI_FAIL(ToRHI(hr));
		};

		if (td.debugName) res->SetName(std::wstring(td.debugName, td.debugName + ::strlen(td.debugName)).c_str());

		const auto arraySize = td.type == ResourceType::Texture3D ? 1 : td.texture.depthOrLayers;
		const auto depth = td.type == ResourceType::Texture3D ? td.texture.depthOrLayers : 1;
		Dx12Resource T(std::move(res), desc.Format, td.texture.width, td.texture.height,
			td.texture.mipLevels, arraySize, (td.type == ResourceType::Texture3D)
			? D3D12_RESOURCE_DIMENSION_TEXTURE3D
			: (td.type == ResourceType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
				: D3D12_RESOURCE_DIMENSION_TEXTURE1D), depth);

		auto handle = impl->resources.alloc(T);

		Resource ret{ handle, true };
		ret.impl = impl;
		ret.vt = &g_tex_rvt;

		out = MakeTexturePtr(d, ret);
		return Result::Ok;
	}

	static Result d_createCommittedResource(Device* d, const ResourceDesc& td, ResourcePtr& out) noexcept {
		switch (td.type) {
		case ResourceType::Buffer:  return d_createCommittedBuffer(d, td, out);
		case ResourceType::Texture3D:
		case ResourceType::Texture2D:
		case ResourceType::Texture1D:
			return d_createCommittedTexture(d, td, out);
		case ResourceType::Unknown: {
			BreakIfDebugging();
			return Result::InvalidArgument;
		}
		}
		BreakIfDebugging();
		return Result::Unexpected;
	}

	static uint32_t d_getDescriptorHandleIncrementSize(Device* d, DescriptorHeapType type) noexcept {
		const auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->pNativeDevice) return 0;
		const D3D12_DESCRIPTOR_HEAP_TYPE t = ToDX(type);
		return (uint32_t)impl->pNativeDevice->GetDescriptorHandleIncrementSize(t);
	}

	static Result d_createTimeline(Device* d, uint64_t initial, const char* dbg, TimelinePtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		Microsoft::WRL::ComPtr<ID3D12Fence> f;
		if (const auto hr = impl->pNativeDevice->CreateFence(initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f)); FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}
		if (dbg) { std::wstring w(dbg, dbg + ::strlen(dbg)); f->SetName(w.c_str()); }
		const Dx12Timeline T(f);
		const auto h = impl->timelines.alloc(T);
		Timeline ret{ h };
		ret.impl = impl;
		ret.vt = &g_tlvt;
		out = MakeTimelinePtr(d, ret);
		return Result::Ok;
	}


	static void d_destroyTimeline(DeviceDeletionContext* d, TimelineHandle t) noexcept {
		dx12_detail::Dev(d)->timelines.free(t);
	}

	static Result d_createHeap(const Device* d, const HeapDesc& hd, HeapPtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->pNativeDevice || hd.sizeBytes == 0) {
			RHI_FAIL(Result::InvalidArgument);
		}
		D3D12_HEAP_PROPERTIES props{};
		props.Type = ToDx(hd.memory);
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
		if (const auto hr = impl->pNativeDevice->CreateHeap(&desc, IID_PPV_ARGS(&heap)); FAILED(hr)) {
			return ToRHI(hr);
		}

#ifdef _WIN32
		if (hd.debugName) { const std::wstring w(hd.debugName, hd.debugName + ::strlen(hd.debugName)); heap->SetName(w.c_str()); }
#endif

		const Dx12Heap H(heap, hd.sizeBytes);
		const auto h = impl->heaps.alloc(H);
		Heap ret{ h };
		ret.impl = impl;
		ret.vt = &g_hevt;
		out = MakeHeapPtr(d, ret);
		return Result::Ok;
	}

	static void d_destroyHeap(DeviceDeletionContext* d, HeapHandle h) noexcept {
		dx12_detail::Dev(d)->heaps.free(h);
	}

	static void d_setNameBuffer(Device* d, ResourceHandle b, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (auto* B = impl->resources.get(b)) {
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
		if (auto* T = impl->resources.get(t)) {
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

	static Result d_createPlacedTexture(Device* d, HeapHandle hh, uint64_t offset, const ResourceDesc& td, ResourcePtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) {
			RHI_FAIL(Result::InvalidArgument);
		}
		const auto* H = impl->heaps.get(hh); if (!H || !H->heap) {
			RHI_FAIL(Result::InvalidArgument);
		}
		if (td.texture.width == 0 || td.texture.height == 0 || td.texture.format == Format::Unknown) {
			RHI_FAIL(Result::InvalidArgument);
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

		std::vector<DXGI_FORMAT> castableFormats;
		for (const auto& fmt : td.castableFormats) {
			castableFormats.push_back(ToDxgi(fmt));
		}

		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		const HRESULT hr = impl->pNativeDevice->CreatePlacedResource2(
			H->heap.Get(),
			offset,
			&desc,
			initialLayout,
			pClear,
			/*numCastableFormats*/ static_cast<uint32_t>(castableFormats.size()),
			/*pProtectedSession*/ castableFormats.data(),
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}
		if (td.debugName) res->SetName(std::wstring(td.debugName, td.debugName + ::strlen(td.debugName)).c_str());
		Dx12Resource T(std::move(res), desc.Format, td.texture.width, td.texture.height,
			td.texture.mipLevels, (td.type == ResourceType::Texture3D) ? 1 : td.texture.depthOrLayers,
			(td.type == ResourceType::Texture3D) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
			: (td.type == ResourceType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
				: D3D12_RESOURCE_DIMENSION_TEXTURE1D),
			(td.type == ResourceType::Texture3D) ? td.texture.depthOrLayers : 1);

		const auto handle = impl->resources.alloc(T);
		Resource ret(handle, true);
		ret.impl = impl;
		ret.vt = &g_tex_rvt;
		out = MakeTexturePtr(d, ret);
		return Result::Ok;
	}

	static Result d_createPlacedBuffer(Device* d, const HeapHandle hh, const uint64_t offset, const ResourceDesc& bd, ResourcePtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) {
			RHI_FAIL(Result::InvalidArgument);
		}
		const auto* H = impl->heaps.get(hh); if (!H || !H->heap) {
			RHI_FAIL(Result::InvalidArgument);
		}
		if (bd.buffer.sizeBytes == 0) {
			return Result::InvalidArgument;
		}
		const D3D12_RESOURCE_FLAGS flags = ToDX(bd.resourceFlags);
		const D3D12_RESOURCE_DESC1  desc = MakeBufferDesc1(bd.buffer.sizeBytes, flags);
		// Buffers must use UNDEFINED layout per spec
		const D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;

		std::vector<DXGI_FORMAT> castableFormats;
		for (const auto& fmt : bd.castableFormats) {
			castableFormats.push_back(ToDxgi(fmt));
		}

		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		HRESULT hr = impl->pNativeDevice->CreatePlacedResource2(
			H->heap.Get(),
			offset,
			&desc,
			initialLayout,
			/*pOptimizedClearValue*/ nullptr,        // buffers: must be null
			/*numCastableFormats*/   static_cast<uint32_t>(castableFormats.size()),
			/*pProtectedSession*/   castableFormats.data(),
			IID_PPV_ARGS(&res));
		if (FAILED(hr)) {
			spdlog::info("?");
			//RHI_FAIL(ToRHI(hr));
		}
		if (bd.debugName) res->SetName(std::wstring(bd.debugName, bd.debugName + ::strlen(bd.debugName)).c_str());
		const Dx12Resource B(std::move(res), bd.buffer.sizeBytes);
		const auto handle = impl->resources.alloc(B);
		Resource ret{ handle, false };
		ret.impl = impl;
		ret.vt = &g_buf_rvt;
		out = MakeBufferPtr(d, ret);
		return Result::Ok;
	}

	static Result d_createPlacedResource(Device* d, const HeapHandle hh, const uint64_t offset, const ResourceDesc& rd, ResourcePtr& out) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl) {
			RHI_FAIL(Result::InvalidArgument);
		}
		auto* H = impl->heaps.get(hh); if (!H || !H->heap) {
			RHI_FAIL(Result::InvalidArgument);
		}
		switch (rd.type) {
		case ResourceType::Buffer:  return d_createPlacedBuffer(d, hh, offset, rd, out);
		case ResourceType::Texture3D:
		case ResourceType::Texture2D:
		case ResourceType::Texture1D:
			return d_createPlacedTexture(d, hh, offset, rd, out);
		case ResourceType::Unknown: {
			BreakIfDebugging();
			return Result::InvalidArgument;
		}
		}
		BreakIfDebugging();
		return Result::Unexpected;
	}

	static Result d_createQueryPool(Device* d, const QueryPoolDesc& qd, QueryPoolPtr& out) noexcept {
		auto* dimpl = static_cast<Dx12Device*>(d->impl);
		if (!dimpl || qd.count == 0) {
			RHI_FAIL(Result::InvalidArgument);
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
			bool haveOpt9 = SUCCEEDED(dimpl->pNativeDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &opts9, sizeof(opts9)));
			bool canMeshStats = haveOpt9 && !!opts9.MeshShaderPipelineStatsSupported;

			if (needMesh && !canMeshStats && qd.requireAllStats) {
				RHI_FAIL(Result::InvalidArgument);
			}

			desc.Type = needMesh && canMeshStats
				? D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1
				: D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
			type = desc.Type;
			usePSO1 = (desc.Type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1);
		} break;
		}

		Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
		if (const auto hr = dimpl->pNativeDevice->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap)); FAILED(hr)) {
			RHI_FAIL(ToRHI(hr));
		}
		Dx12QueryPool qp(heap, type, qd.count);
		qp.usePSO1 = usePSO1;

		const auto handle = dimpl->queryPools.alloc(qp);
		QueryPool ret{ handle };
		ret.impl = dimpl;
		ret.vt = &g_qpvt;

		out = MakeQueryPoolPtr(d, ret);
		return Result::Ok;
	}

	static void d_destroyQueryPool(DeviceDeletionContext* d, QueryPoolHandle h) noexcept {
		dx12_detail::Dev(d)->queryPools.free(h);
	}

	static TimestampCalibration d_getTimestampCalibration(Device* d, QueueKind q) noexcept {
		auto* impl = static_cast<Dx12Device*>(d->impl);
		auto* s = (q == QueueKind::Graphics) ? &impl->gfx : (q == QueueKind::Compute ? &impl->comp : &impl->copy);
		UINT64 freq = 0;
		if (s->pNativeQueue) s->pNativeQueue->GetTimestampFrequency(&freq);
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

		auto* T = impl->resources.get(in.texture);
		if (!T || !T->res) {
			BreakIfDebugging();
			return {};
		}
		const D3D12_RESOURCE_DESC desc = T->res->GetDesc();

		// Resource-wide properties
		const UINT mipLevels = desc.MipLevels;
		const UINT arrayLayers = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 1u : desc.DepthOrArraySize;

		// Plane count per DXGI format
		const UINT resPlaneCount = D3D12GetFormatPlaneCount(impl->pNativeDevice.Get(), desc.Format);

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

		impl->pNativeDevice->GetCopyableFootprints(
			&desc,
			firstSubresource,
			totalSubs,
			in.baseOffset,
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

	static Result d_getResourceAllocationInfo(
		const Device* d,
		const ResourceDesc* resources,
		uint32_t resourceCount,
		ResourceAllocationInfo* outInfos) noexcept
	{
		// Validate inputs
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !resources || resourceCount == 0 || !outInfos) {
			RHI_FAIL(Result::InvalidArgument);
		}
		// TODO: Should we store this elsewhere to avoid reallocating every call?
		std::vector<D3D12_RESOURCE_DESC1> descs;
		descs.resize(resourceCount);
		for (size_t i = 0; i < resourceCount; ++i) {
			switch (resources[i].type) {
			case ResourceType::Buffer:
				descs[i] = MakeBufferDesc1(resources[i].buffer.sizeBytes, ToDX(resources[i].resourceFlags));
				break;
			case ResourceType::Texture1D:
			case ResourceType::Texture2D:
			case ResourceType::Texture3D:
				descs[i] = MakeTexDesc1(resources[i]);
				break;
			default:
				RHI_FAIL(Result::InvalidArgument);
			}
		}
		// Out array
		std::vector<D3D12_RESOURCE_ALLOCATION_INFO1> dxInfos;
		dxInfos.resize(resourceCount);
		impl->pNativeDevice->GetResourceAllocationInfo2(0, resourceCount, descs.data(), dxInfos.data());
		// Pack back
		for (size_t i = 0; i < resourceCount; ++i) {
			outInfos[i].offset = dxInfos[i].Offset;
			outInfos[i].alignment = dxInfos[i].Alignment;
			outInfos[i].sizeInBytes = dxInfos[i].SizeInBytes;
		}
		return Result::Ok;
	}

	static Result d_setResidencyPriority(
		const Device* d,
		const Span<PageableRef> objects,
		ResidencyPriority priority) noexcept
	{
		auto* impl = static_cast<Dx12Device*>(d->impl);
		if (!impl || !impl->pNativeDevice) {
			RHI_FAIL(Result::InvalidArgument);
		}

		if (objects.size == 0) return Result::Ok;

		std::vector<ID3D12Pageable*> pageables;
		pageables.reserve(objects.size);

		std::vector<D3D12_RESIDENCY_PRIORITY> priorities;
		priorities.resize(objects.size, ToDX(priority));

		for (uint32_t i = 0; i < objects.size; ++i)
		{
			const PageableRef& o = objects.data[i];
			ID3D12Pageable* native = nullptr;

			switch (o.kind)
			{
			case PageableKind::Resource:
			{
				auto* R = impl->resources.get(o.resource);
				if (!R || !R->res) {
					RHI_FAIL(Result::InvalidArgument);
				}
				native = R->res.Get(); // ID3D12Resource* -> ID3D12Pageable*
			} break;

			case PageableKind::Heap:
			{
				auto* H = impl->heaps.get(o.heap);
				if (!H || !H->heap) {
					RHI_FAIL(Result::InvalidArgument);
				}
				native = H->heap.Get(); // ID3D12Heap* -> ID3D12Pageable*
			} break;

			case PageableKind::DescriptorHeap:
			{
				auto* DH = impl->descHeaps.get(o.descHeap);
				if (!DH || !DH->heap) {
					RHI_FAIL(Result::InvalidArgument);
				}
				native = DH->heap.Get(); // ID3D12DescriptorHeap* -> ID3D12Pageable*
			} break;

			case PageableKind::QueryPool:
			{
				auto* QH = impl->queryPools.get(o.queryPool);
				if (!QH || !QH->heap) {
					RHI_FAIL(Result::InvalidArgument);
				}
				native = QH->heap.Get(); // ID3D12QueryHeap* -> ID3D12Pageable*
			} break;

			case PageableKind::Pipeline:
			{
				auto* P = impl->pipelines.get(o.pipeline);
				if (!P || !P->pso) {
					RHI_FAIL(Result::InvalidArgument);
				}
				native = P->pso.Get(); // ID3D12PipelineState* -> ID3D12Pageable*
			} break;

			default:
				RHI_FAIL(Result::InvalidArgument);
			}

			// Defensive (should never happen if the above is correct)
			if (!native) {
				RHI_FAIL(Result::InvalidArgument);
			}

			pageables.push_back(native);
		}

		const HRESULT hr = impl->pNativeDevice->SetResidencyPriority(
			static_cast<UINT>(pageables.size()),
			pageables.data(),
			priorities.data());

		if (FAILED(hr)) return ToRHI(hr);
		return Result::Ok;
	}

	// ---------------- Queue vtable funcs ----------------
	static Result q_submit(Queue* q, Span<CommandList> lists, const SubmitDesc& s) noexcept {
		auto* qs = dx12_detail::QState(q);
		auto* dev = dx12_detail::Dev(q);
		if (!dev) {
			RHI_FAIL(Result::InvalidArgument);
		}

		// Pre-waits
		for (auto& w : s.waits) {
			auto* TL = dev->timelines.get(w.t); if (!TL) {
				RHI_FAIL(Result::InvalidArgument);
			}
			if (FAILED(qs->pNativeQueue->Wait(TL->fence.Get(), w.value))) {
				RHI_FAIL(Result::InvalidArgument);
			}
		}

		// Execute command lists
		std::vector<ID3D12CommandList*> native; native.reserve(lists.size);
		for (auto& L : lists) {
			auto* w = dx12_detail::CL(&L);
			native.push_back(w->cl.Get());
		}
		if (!native.empty()) qs->pNativeQueue->ExecuteCommandLists(static_cast<uint32_t>(native.size()), native.data());

		// Post-signals
		for (auto& sgn : s.signals) {
			auto* TL = dev->timelines.get(sgn.t); if (!TL) {
				RHI_FAIL(Result::InvalidArgument);
			}
			if (const auto hr = qs->pNativeQueue->Signal(TL->fence.Get(), sgn.value); FAILED(hr)) {
				RHI_FAIL(ToRHI(hr));
			}
		}
		return Result::Ok;
	}

	static Result q_signal(Queue* q, const TimelinePoint& p) noexcept {
		auto* qs = dx12_detail::QState(q);
		auto* dev = dx12_detail::Dev(q);
		if (!dev) {
			RHI_FAIL(Result::InvalidArgument);
		}
		auto* TL = dev->timelines.get(p.t); if (!TL) {
			BreakIfDebugging();
			return Result::InvalidArgument;
		}
#if BUILD_TYPE == BUILD_DEBUG
		auto last = qs->lastSignaledValue.find(p.t);
		if (last != qs->lastSignaledValue.end() && p.value <= last->second) {
			BreakIfDebugging();
			return Result::InvalidArgument; // must be strictly greater
		}
		qs->lastSignaledValue[p.t] = p.value;
#endif
		return SUCCEEDED(qs->pNativeQueue->Signal(TL->fence.Get(), p.value)) ? Result::Ok : Result::Failed;
	}

	static Result q_wait(Queue* q, const TimelinePoint& p) noexcept {
		auto* qs = dx12_detail::QState(q);
		auto* dev = dx12_detail::Dev(q);
		if (!dev) {
			RHI_FAIL(Result::InvalidArgument);
		}
		auto* TL = dev->timelines.get(p.t); if (!TL) {
			RHI_FAIL(Result::InvalidArgument);
		}
		return SUCCEEDED(qs->pNativeQueue->Wait(TL->fence.Get(), p.value)) ? Result::Ok : Result::Failed;
	}

	static void q_setName(Queue* q, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* qs = dx12_detail::QState(q);
		if (!qs || !qs->pNativeQueue) {
			BreakIfDebugging();
			return;
		}
		std::wstring w(n, n + ::strlen(n));
		qs->pNativeQueue->SetName(w.c_str());
	}

	// ---------------- CommandList vtable funcs ----------------

	static inline UINT MipDim(UINT base, UINT mip) {
		return (std::max)(1u, base >> mip);
	}

	static inline UINT CalcSubresourceFor(const Dx12Resource& T, UINT mip, UINT arraySlice) {
		// PlaneSlice = 0 (non-planar). TODO: support planar formats
		return D3D12CalcSubresource(mip, arraySlice, 0, T.tex.mips, T.tex.arraySize);
	}

	static void cl_end(CommandList* cl) noexcept {
		auto* w = dx12_detail::CL(cl);
		w->cl->Close();
	}
	static void cl_reset(CommandList* cl, const CommandAllocator& ca) noexcept {
		auto* l = dx12_detail::CL(cl);
		auto* a = dx12_detail::Alloc(&ca);
#if BUILD_TYPE == BUILD_DEBUG
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
		auto* l = dx12_detail::CL(cl);
		if (!l) {
			BreakIfDebugging();
			return;
		}

		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
		rtvs.reserve(p.colors.size);
		auto dev = dx12_detail::Dev(cl);
		for (uint32_t i = 0; i < p.colors.size; ++i) {
			D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
			if (DxGetDstCpu(dev, p.colors.data[i].rtv, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_RTV)) {
				rtvs.push_back(cpu);
				if (p.colors.data[i].loadOp == LoadOp::Clear) {
					l->cl->ClearRenderTargetView(cpu, p.colors.data[i].clear.rgba, 0, nullptr);
				}
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
		const D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
		if (p.depth) {
			if (DxGetDstCpu(dev, p.depth->dsv, dsv, D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) {
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
		auto* impl = dx12_detail::CL(cl);
		auto* dev = dx12_detail::Dev(cl);
		auto* L = dev->pipelineLayouts.get(layoutH);
		if (!L || !L->root) {
			BreakIfDebugging();
			return;
		}

		switch (impl->type) {
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
		auto* l = dx12_detail::CL(cl);
		auto* dev = dx12_detail::Dev(cl);
		if (auto* P = dev->pipelines.get(psoH)) {
			l->cl->SetPipelineState(P->pso.Get());
		}
	}
	static void cl_setVB(CommandList* cl, uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept {
		auto* l = dx12_detail::CL(cl);
		std::vector<D3D12_VERTEX_BUFFER_VIEW> views; views.resize(numViews);
		auto* dev = dx12_detail::Dev(cl);
		for (uint32_t i = 0; i < numViews; ++i) {
			if (auto* B = dev->resources.get(pBufferViews[i].buffer)) {
				views[i].BufferLocation = B->res->GetGPUVirtualAddress() + pBufferViews[i].offset;
				views[i].SizeInBytes = pBufferViews[i].sizeBytes;
				views[i].StrideInBytes = pBufferViews[i].stride;
			}
		}
		l->cl->IASetVertexBuffers(startSlot, (UINT)numViews, views.data());
	}
	static void cl_setIB(CommandList* cl, const IndexBufferView& view) noexcept {
		auto* l = dx12_detail::CL(cl);
		auto* dev = dx12_detail::Dev(cl);
		if (auto* B = dev->resources.get(view.buffer)) {
			D3D12_INDEX_BUFFER_VIEW ibv{};
			ibv.BufferLocation = B->res->GetGPUVirtualAddress() + view.offset;
			ibv.SizeInBytes = view.sizeBytes;
			ibv.Format = ToDxgi(view.format);
			l->cl->IASetIndexBuffer(&ibv);
		}
	}
	static void cl_draw(CommandList* cl, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) noexcept {
		auto* l = dx12_detail::CL(cl);
		l->cl->DrawInstanced(vc, ic, fv, fi);
	}
	static void cl_drawIndexed(CommandList* cl, uint32_t ic, uint32_t inst, uint32_t firstIdx, int32_t vtxOff, uint32_t firstInst) noexcept {
		auto* l = dx12_detail::CL(cl);
		l->cl->DrawIndexedInstanced(ic, inst, firstIdx, vtxOff, firstInst);
	}
	static void cl_dispatch(CommandList* cl, uint32_t x, uint32_t y, uint32_t z) noexcept {
		auto* l = dx12_detail::CL(cl);
		l->cl->Dispatch(x, y, z);
	}

	static void cl_clearRTV_slot(CommandList* c, DescriptorSlot s, const rhi::ClearValue& cv) noexcept {
		auto* impl = dx12_detail::CL(c);
		if (!impl) {
			BreakIfDebugging();
			return;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		if (!DxGetDstCpu(dx12_detail::Dev(c), s, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_RTV)) {
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
		auto* impl = dx12_detail::CL(c);
		if (!impl) {
			BreakIfDebugging();
			return;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		if (!DxGetDstCpu(dx12_detail::Dev(c), s, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) {
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

		auto* l = dx12_detail::CL(cl);
		auto* dev = dx12_detail::Dev(cl);
		if (!dev) {
			BreakIfDebugging();
			return;
		}

		auto* S = dev->commandSignatures.get(sigH);
		if (!S || !S->sig) {
			BreakIfDebugging();
			return;
		}

		auto* argB = dev->resources.get(argBufH);
		if (!argB || !argB->res) {
			BreakIfDebugging();
			return;
		}

		ID3D12Resource* cntRes = nullptr;
		if (cntBufH.valid()) {
			auto* c = dev->resources.get(cntBufH);
			if (c && c->res) cntRes = c->res.Get();
		}

		l->cl->ExecuteIndirect(
			S->sig.Get(),
			maxCount,
			argB->res.Get(), argOff,
			cntRes, cntOff);
	}
	static void cl_setDescriptorHeaps(CommandList* cl, DescriptorHeapHandle csu, std::optional<DescriptorHeapHandle> samp) noexcept {
		auto* l = dx12_detail::CL(cl);
		auto* dev = dx12_detail::Dev(cl);

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
		auto* l = dx12_detail::CL(cl);
		auto* dev = dx12_detail::Dev(cl);
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
			auto* T = dev->resources.get(t.texture);
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
			auto* B = dev->resources.get(br.buffer);
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
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
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
			res = impl->resources.get(u.resource.GetHandle())->res.Get();
		}
		else {
			res = impl->resources.get(u.resource.GetHandle())->res.Get();
		}
		if (!res) {
			BreakIfDebugging();
			return;
		}

		// NOTE: caller must have bound the shader-visible heap via SetDescriptorHeaps()
		// and transitioned 'res' to UAV/UNORDERED_ACCESS
		rec->cl->ClearUnorderedAccessViewUint(gpu, cpu, res, v.v, 0, nullptr);
	}

	static void cl_clearUavFloat(CommandList* cl,
		const UavClearInfo& u,
		const UavClearFloat& v) noexcept
	{
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
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
			res = impl->resources.get(u.resource.GetHandle())->res.Get();
		}
		else {
			res = impl->resources.get(u.resource.GetHandle())->res.Get();
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
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		auto* T = impl->resources.get(r.texture);
		auto* B = impl->resources.get(r.buffer);
		if (!T || !B || !T->res || !B->res) {
			BreakIfDebugging();
			return;
		}

		D3D12_TEXTURE_COPY_LOCATION dst{};
		dst.pResource = B->res.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst.PlacedFootprint.Offset = r.footprint.offset;
		dst.PlacedFootprint.Footprint.Format = T->fmt;     // texture s actual DXGI format
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
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		auto* T = impl->resources.get(r.texture);
		auto* B = impl->resources.get(r.buffer);
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
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
		if (!rec || !impl) {
			BreakIfDebugging();
			return;
		}

		auto* DstT = impl->resources.get(dst.texture);
		auto* SrcT = impl->resources.get(src.texture);
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
		UINT srcW = src.width ? src.width : MipDim(SrcT->tex.w, src.mip);
		UINT srcH = src.height ? src.height : MipDim(SrcT->tex.h, src.mip);
		UINT srcD = src.depth ? src.depth : ((SrcT->dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			? MipDim(SrcT->tex.depth, src.mip) : 1u);

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

	static void cl_copyBufferRegion(CommandList* cl,
		ResourceHandle dst, uint64_t dstOffset,
		ResourceHandle src, uint64_t srcOffset,
		uint64_t numBytes) noexcept
	{
		if (!cl || !cl->IsValid() || numBytes == 0) {
			BreakIfDebugging();
			return;
		}

		auto* rec = dx12_detail::CL(cl);
		auto* dev = dx12_detail::Dev(cl);
		if (!rec || !dev) {
			BreakIfDebugging();
			return;
		}

		// Look up buffer resources
		auto* D = dev->resources.get(dst);
		auto* S = dev->resources.get(src);
		if (!D || !S || !D->res || !S->res) {
			BreakIfDebugging();
			return;
		}

		// We don't validate bounds here (we don't store sizes). DX12 will validate.
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
		auto* l = dx12_detail::CL(cl);
		if (!l || !l->cl) {
			BreakIfDebugging();
			return;
		}
		l->cl->SetName(std::wstring(n, n + ::strlen(n)).c_str());
	}

	static void cl_writeTimestamp(CommandList* cl, QueryPoolHandle pool, uint32_t index, Stage /*ignored*/) noexcept {
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
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
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
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
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
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
		auto* rec = dx12_detail::CL(cl);
		auto* impl = rec ? dx12_detail::Dev(cl) : nullptr;
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
		auto* B = impl->resources.get(dst);
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
		auto* impl = dx12_detail::CL(c);
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
		if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Compute)) {
			impl->cl->SetComputeRoot32BitConstants(rc->rootIndex, num32, p32, dstOffset32);
		}
		// If any graphics states are set, write there too
		if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::AllGraphics)) {
			impl->cl->SetGraphicsRoot32BitConstants(rc->rootIndex, num32, p32, dstOffset32);
		}
	}

	static void cl_setPrimitiveTopology(CommandList* cl, PrimitiveTopology pt) noexcept {
		auto* l = dx12_detail::CL(cl);
		if (!l) {
			BreakIfDebugging();
			return;
		}
		l->cl->IASetPrimitiveTopology(ToDX(pt));
	}

	static void cl_dispatchMesh(CommandList* cl, uint32_t x, uint32_t y, uint32_t z) noexcept {
		auto* l = dx12_detail::CL(cl);
		if (!l) {
			BreakIfDebugging();
			return;
		}
		l->cl->DispatchMesh(x, y, z);
	}

	// ---------------- Swapchain vtable funcs ----------------
	static uint32_t sc_count(Swapchain* sc) noexcept { return dx12_detail::SC(sc)->count; }
	static uint32_t sc_curr(Swapchain* sc) noexcept { return dx12_detail::SC(sc)->pSlProxySC->GetCurrentBackBufferIndex(); }
	//static ViewHandle sc_rtv(Swapchain* sc, uint32_t i) noexcept { return static_cast<Dx12Swapchain*>(sc->impl)->rtvHandles[i]; }
	static ResourceHandle sc_img(Swapchain* sc, uint32_t i) noexcept { return dx12_detail::SC(sc)->imageHandles[i]; }
	static Result sc_present(Swapchain* sc, bool vsync) noexcept {
		auto* s = dx12_detail::SC(sc);
		UINT sync = vsync ? 1 : 0; UINT flags = 0;
		return s->pSlProxySC->Present(sync, flags) == S_OK ? Result::Ok : Result::Failed;
	}

	static Result sc_resizeBuffers(
		Swapchain* sc,
		uint32_t numBuffers,
		uint32_t w, uint32_t h,
		Format newFormat,
		uint32_t flags) noexcept
	{
		auto* s = static_cast<Dx12Swapchain*>(sc->impl);
		if (!s || !s->pSlProxySC) return Result::InvalidNativePointer;

		auto* dev = dx12_detail::Dev(sc);

		const uint32_t oldCount = s->count;
		const uint32_t newCount = (numBuffers != 0) ? numBuffers : oldCount;
		const DXGI_FORMAT newFmt = (newFormat != Format::Unknown) ? ToDxgi(newFormat) : s->fmt;

		// Ensure nothing is using the old backbuffers.
		if (dev->self.vt && dev->self.vt->deviceWaitIdle)
			(void)dev->self.vt->deviceWaitIdle(&dev->self);

		// Release all refs to old backbuffers.
		for (uint32_t i = 0; i < oldCount; ++i)
		{
			if (i < s->images.size())
				s->images[i].Reset();

			if (i < s->imageHandles.size())
			{
				const auto hnd = s->imageHandles[i];
				if (hnd.generation != 0)
					if (auto* r = dev->resources.get(hnd))
						r->res.Reset();
			}
		}

		// Resize swapchain buffers.
		HRESULT hr = s->pSlProxySC->ResizeBuffers((UINT)newCount, (UINT)w, (UINT)h, newFmt, (UINT)flags);
		if (FAILED(hr)) return ToRHI(hr);

		// If w/h were 0, query actual post-resize dims.
		if (w == 0 || h == 0)
		{
			DXGI_SWAP_CHAIN_DESC1 d{};
			if (SUCCEEDED(s->pSlProxySC->GetDesc1(&d))) { s->w = d.Width; s->h = d.Height; }
		}
		else { s->w = (UINT)w; s->h = (UINT)h; }

		s->fmt = newFmt;
		s->count = (UINT)newCount;

		// Adjust handle arrays (free excess if shrinking, alloc new if growing).
		if (newCount < s->imageHandles.size())
		{
			for (uint32_t i = newCount; i < (uint32_t)s->imageHandles.size(); ++i)
			{
				const auto hnd = s->imageHandles[i];
				if (hnd.generation != 0)
				{
					if (auto* r = dev->resources.get(hnd))
						r->res.Reset();
					dev->resources.free(hnd);
				}
			}
			s->imageHandles.resize(newCount);
		}
		else
		{
			s->imageHandles.resize(newCount); // default init new entries
		}

		s->images.resize(newCount);

		// Reacquire and re-register/update resources.
		for (uint32_t i = 0; i < newCount; ++i)
		{
			ComPtr<ID3D12Resource> img;
			hr = s->pSlProxySC->GetBuffer((UINT)i, IID_PPV_ARGS(&img));
			if (FAILED(hr)) return ToRHI(hr);

			s->images[i] = img;

			Dx12Resource t(img, newFmt, s->w, s->h, 1, 1, D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1);

			// Preserve existing handle if possible, in case they're being stored externally
			// otherwise allocate a new one.
			if (s->imageHandles[i].generation != 0)
			{
				if (auto* r = dev->resources.get(s->imageHandles[i]))
					*r = t;
				else
					s->imageHandles[i] = dev->resources.alloc(t);
			}
			else
			{
				s->imageHandles[i] = dev->resources.alloc(t);
			}
		}

		s->current = s->pSlProxySC->GetCurrentBackBufferIndex();
		return Result::Ok;
	}
	static void sc_setName(Swapchain* sc, const char* n) noexcept {} // Cannot name IDXGISwapChain
	// ---------------- Resource vtable funcs ----------------

	static void buf_map(Resource* r, void** data, uint64_t offset, uint64_t size) noexcept {
		if (!r || !data) {
			BreakIfDebugging();
			return;
		}
		auto* B = dx12_detail::Res(r);
		if (!B || !B->res) {
			*data = nullptr;
			BreakIfDebugging();
			return;
		}

		D3D12_RANGE readRange{};
		D3D12_RANGE* pRange = nullptr;
		if (size != ~0ull) {
			readRange.Begin = SIZE_T(offset);
			readRange.End = SIZE_T(offset + size);
			pRange = &readRange;
		}

		void* ptr = nullptr;
		HRESULT hr = B->res->Map(0, pRange, &ptr);
		if (SUCCEEDED(hr)) {
			*data = static_cast<uint8_t*>(ptr) + size_t(offset);
		}
		else {
			*data = nullptr;
		}
	}

	static void buf_unmap(Resource* r, uint64_t writeOffset, uint64_t writeSize) noexcept {
		auto* B = dx12_detail::Res(r);
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
		auto* B = dx12_detail::Res(r);
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
		auto* T = dx12_detail::Res(r);
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
		auto* T = dx12_detail::Res(r);
		if (T && T->res) T->res->Unmap(0, nullptr);
	}

	static void tex_setName(Resource* r, const char* n) noexcept {
		if (!n) {
			BreakIfDebugging();
			return;
		}
		auto* T = dx12_detail::Res(r);
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
		auto* A = dx12_detail::Alloc(ca);
		A->alloc->Reset(); // ID3D12CommandAllocator::Reset()
	}

	// ------------------ QueryPool vtable funcs ----------------
	static QueryResultInfo qp_getQueryResultInfo(QueryPool* p) noexcept {
		auto* P = dx12_detail::QP(p);
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
		auto* P = dx12_detail::QP(p);
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

			for (unsigned int i = 0; i < cap; i++) {
				auto type = outBuf[i].field;
				switch (type) {
				case PipelineStatTypes::IAVertices:            push(type, offsetof(S, IAVertices)); break;
				case PipelineStatTypes::IAPrimitives:          push(type, offsetof(S, IAPrimitives)); break;
				case PipelineStatTypes::VSInvocations:         push(type, offsetof(S, VSInvocations)); break;
				case PipelineStatTypes::GSInvocations:         push(type, offsetof(S, GSInvocations)); break;
				case PipelineStatTypes::GSPrimitives:          push(type, offsetof(S, GSPrimitives)); break;
				case PipelineStatTypes::TSControlInvocations:  push(type, offsetof(S, HSInvocations)); break;
				case PipelineStatTypes::TSEvaluationInvocations: push(type, offsetof(S, DSInvocations)); break;
				case PipelineStatTypes::PSInvocations:         push(type, offsetof(S, PSInvocations)); break;
				case PipelineStatTypes::CSInvocations:         push(type, offsetof(S, CSInvocations)); break;
				case PipelineStatTypes::TaskInvocations:       tmp.push_back({ PipelineStatTypes::TaskInvocations, 0, 0, false }); break;
				case PipelineStatTypes::MeshInvocations:       tmp.push_back({ PipelineStatTypes::MeshInvocations, 0, 0, false }); break;
				case PipelineStatTypes::MeshPrimitives:        tmp.push_back({ PipelineStatTypes::MeshPrimitives,  0, 0, false }); break;
				default:
					tmp.push_back({ type, 0, 0, false });
					break;
				}
			}
		}
		else {
			using S = D3D12_QUERY_DATA_PIPELINE_STATISTICS1;
			auto push = [&](PipelineStatTypes f, size_t off) {
				tmp.push_back({ f, uint32_t(off), uint32_t(sizeof(uint64_t)), true });
				};

			for (unsigned int i = 0; i < cap; i++) {
				auto type = outBuf[i].field;
				switch (type) {
				case PipelineStatTypes::IAVertices:            push(type, offsetof(S, IAVertices)); break;
				case PipelineStatTypes::IAPrimitives:          push(type, offsetof(S, IAPrimitives)); break;
				case PipelineStatTypes::VSInvocations:         push(type, offsetof(S, VSInvocations)); break;
				case PipelineStatTypes::GSInvocations:         push(type, offsetof(S, GSInvocations)); break;
				case PipelineStatTypes::GSPrimitives:          push(type, offsetof(S, GSPrimitives)); break;
				case PipelineStatTypes::TSControlInvocations:  push(type, offsetof(S, HSInvocations)); break;
				case PipelineStatTypes::TSEvaluationInvocations: push(type, offsetof(S, DSInvocations)); break;
				case PipelineStatTypes::PSInvocations:         push(type, offsetof(S, PSInvocations)); break;
				case PipelineStatTypes::CSInvocations:         push(type, offsetof(S, CSInvocations)); break;
				case PipelineStatTypes::TaskInvocations:       push(type, offsetof(S, ASInvocations)); break;
				case PipelineStatTypes::MeshInvocations:       push(type, offsetof(S, MSInvocations)); break;
				case PipelineStatTypes::MeshPrimitives:        push(type, offsetof(S, MSPrimitives)); break;
				default:
					tmp.push_back({ type, 0, 0, false });
					break;
				}
			}
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
		auto* Q = dx12_detail::QP(qp);
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
		auto* P = dx12_detail::Pso(p);
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
		auto* L = dx12_detail::PL(pl);
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
		auto* S = dx12_detail::CSig(cs);
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
		auto* H = dx12_detail::DH(dh);
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
		auto* impl = dx12_detail::TL(t);
		return impl->fence ? impl->fence->GetCompletedValue() : 0;
	}

	static Result tl_timelineHostWait(Timeline* tl, const uint64_t p) noexcept {
		auto* TL = dx12_detail::TL(tl);
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
		auto* T = dx12_detail::TL(tl);
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
		auto* H = dx12_detail::Hp(h);
		if (!H || !H->heap) {
			BreakIfDebugging();
			return;
		}
		H->heap->SetName(s2ws(n).c_str());
	}

	const DeviceVTable g_devvt = {
		&d_createPipelineFromStream,
		&d_createPipelineLayout,
		&d_createCommandSignature,
		&d_createCommandAllocator,
		&d_createCommandList,
		&d_createSwapchain,
		&d_createDescriptorHeap,
		&d_createConstantBufferView,
		&d_createShaderResourceView,
		&d_createUnorderedAccessView,
		&d_createRenderTargetView,
		&d_createDepthStencilView,
		&d_createSampler,
		&d_createCommittedResource,
		&d_createTimeline,
		&d_createHeap,
		&d_createPlacedResource,
		&d_createQueryPool,

		&d_destroySampler,
		&d_destroyPipelineLayout,
		&d_destroyPipeline,
		&d_destroyCommandSignature,
		&d_destroyCommandAllocator,
		&d_destroyCommandList,
		&d_destroySwapchain,
		&d_destroyDescriptorHeap,
		&d_destroyBuffer,
		&d_destroyTexture,
		&d_destroyTimeline,
		&d_destroyHeap,
		&d_destroyQueryPool,

		&d_getQueue,
		&d_waitIdle,
		&d_flushDeletionQueue,
		&d_getDescriptorHandleIncrementSize,
		&d_getTimestampCalibration,
		&d_getCopyableFootprints,
		&d_getResourceAllocationInfo,
		&d_queryFeatureInfo,
		&d_setResidencyPriority,
		&d_queryVideoMemoryInfo,

		&d_setNameBuffer,
		&d_setNameTexture,
		&d_setNameSampler,
		&d_setNamePipelineLayout,
		&d_setNamePipeline,
		&d_setNameCommandSignature,
		&d_setNameDescriptorHeap,
		&d_setNameTimeline,
		&d_setNameHeap,
		&d_destroyDevice,
        2u
    };

    const QueueVTable g_qvt = { 
		&q_submit,
		&q_signal,
		&q_wait,
		&q_setName,
		1u };

	const CommandAllocatorVTable g_calvt = {
		&ca_reset,
		1u
	};

	const CommandListVTable g_clvt = {
		&cl_end,
		&cl_reset,
		&cl_beginPass,
		&cl_endPass,
		&cl_barrier,
		&cl_bindLayout,
		&cl_bindPipeline,
		&cl_setVB,
		&cl_setIB,
		&cl_draw,
		&cl_drawIndexed,
		&cl_dispatch,
		cl_clearRTV_slot,
		cl_clearDSV_slot,
		&cl_executeIndirect,
		&cl_setDescriptorHeaps,
		&cl_clearUavUint,
		&cl_clearUavFloat,
		&cl_copyTextureToBuffer,
		&cl_copyBufferToTexture,
		&cl_copyTextureRegion,
		&cl_copyBufferRegion,
		&cl_writeTimestamp,
		&cl_beginQuery,
		&cl_endQuery,
		&cl_resolveQueryData,
		&cl_resetQueries,
		&cl_pushConstants,
		&cl_setPrimitiveTopology,
		&cl_dispatchMesh,
		&cl_setName,
		1u
    };
    const SwapchainVTable g_scvt = { 
		&sc_count,
		&sc_curr,
		//&sc_rtv,
		&sc_img,
		&sc_present,
		&sc_resizeBuffers,
		&sc_setName,
		1u };
	const ResourceVTable g_buf_rvt = {
		&buf_map,
		&buf_unmap,
		&buf_setName,
		1
	};
	const ResourceVTable g_tex_rvt = {
		&tex_map,
		&tex_unmap,
		&tex_setName,
		1
	};
	const QueryPoolVTable g_qpvt = {
		&qp_getQueryResultInfo,
		&qp_getPipelineStatsLayout,
		&qp_setName,
		1u
	};
	const PipelineVTable g_psovt = {
		&pso_setName,
		1u
	};
	const PipelineLayoutVTable g_plvt = {
		&pl_setName,
		1u
	};
	const CommandSignatureVTable g_csvt = {
		&cs_setName,
		1u
	};
	const DescriptorHeapVTable g_dhvt = {
		&dh_setName,
		1u
	};
	const SamplerVTable g_svt = {
		&s_setName,
		1u
	};
	const TimelineVTable g_tlvt = {
		&tl_timelineCompletedValue,
		&tl_timelineHostWait,
		&tl_setName,
		1u
	};
	const HeapVTable g_hevt = {
		&h_setName,
		1u
	};

	// ---------------- Helpers ----------------
	
	void EnableShaderBasedValidation() {
		CComPtr<ID3D12Debug> spDebugController0;
		CComPtr<ID3D12Debug1> spDebugController1;
		VERIFY(D3D12GetDebugInterface(IID_PPV_ARGS(&spDebugController0)));
		VERIFY(spDebugController0->QueryInterface(IID_PPV_ARGS(&spDebugController1)));
		spDebugController1->SetEnableGPUBasedValidation(true);
	}

	static void EnableDebug(ID3D12Device* device) {
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
		}
		//EnableShaderBasedValidation();
		ComPtr<ID3D12InfoQueue> iq; if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&iq)))) {
			D3D12_MESSAGE_ID blocked[] = { (D3D12_MESSAGE_ID)1356, (D3D12_MESSAGE_ID)1328, (D3D12_MESSAGE_ID)1008 };
			D3D12_INFO_QUEUE_FILTER f{}; f.DenyList.NumIDs = (UINT)_countof(blocked); f.DenyList.pIDList = blocked; iq->AddStorageFilterEntries(&f);
		}
	}

	void SlLogMessageCallback(sl::LogType level, const char* message) {
		//spdlog::info("Streamline Log: {}", message);
	}
	std::wstring GetExePath() {
		WCHAR buffer[MAX_PATH] = { 0 };
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
		return std::wstring(buffer).substr(0, pos);
	}

	bool InitSL() noexcept
	{
		// IMPORTANT: Always securely load SL library, see source/core/sl.security/secureLoadLibrary for more details
// Always secure load SL modules
		if (!sl::security::verifyEmbeddedSignature(L"sl.interposer.dll"))
		{
			// SL module not signed, disable SL
		}
		else
		{
			auto mod = LoadLibraryW(L"sl.interposer.dll");

			if (!mod) {
				spdlog::error("Failed to load sl.interposer.dll, ensure it is in the correct directory.");
				return false;
			}

		}

		sl::Preferences pref{};
		pref.showConsole = false; // for debugging, set to false in production
		pref.logLevel = sl::LogLevel::eDefault;
		auto path = GetExePath() + L"\\NVSL";
		const wchar_t* path_wchar = path.c_str();
		pref.pathsToPlugins = { &path_wchar }; // change this if Streamline plugins are not located next to the executable
		pref.numPathsToPlugins = 1; // change this if Streamline plugins are not located next to the executable
		pref.pathToLogsAndData = {}; // change this to enable logging to a file
		pref.logMessageCallback = SlLogMessageCallback; // highly recommended to track warning/error messages in your callback
		pref.engine = sl::EngineType::eCustom; // If using UE or Unity
		pref.engineVersion = "0.0.1"; // Optional version
		pref.projectId = "72a89ee2-1139-4cc5-8daa-d27189bed781"; // Optional project id
		sl::Feature myFeatures[] = { sl::kFeatureDLSS };
		pref.featuresToLoad = myFeatures;
		pref.numFeaturesToLoad = _countof(myFeatures);
		pref.renderAPI = sl::RenderAPI::eD3D12;
		pref.flags |= sl::PreferenceFlags::eUseManualHooking;
		if (SL_FAILED(res, slInit(pref)))
		{
			// Handle error, check the logs
			if (res == sl::Result::eErrorDriverOutOfDate) { /* inform user */ }
			// and so on ...
			return false;
		}
		return true;
	}

	rhi::Result CreateD3D12Device(const DeviceCreateInfo& ci, DevicePtr& outPtr, bool enableStreamlineInterposer) noexcept
	{
		bool l_enableStreamline = enableStreamlineInterposer;
		if (l_enableStreamline)
		{
			if (!InitSL())
			{
				spdlog::error("Failed to initialize NVIDIA Streamline. DLSS will not be available.");
				l_enableStreamline = false;
			}
		}

		UINT flags = 0;
		if (ci.enableDebug)
		{
			ComPtr<ID3D12Debug> dbg;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
				dbg->EnableDebugLayer(), flags |= DXGI_CREATE_FACTORY_DEBUG;
		}

		auto impl = std::make_shared<Dx12Device>();
		//impl->selfWeak = impl;

		// Native factory/device
		CreateDXGIFactory2(flags, IID_PPV_ARGS(&impl->pNativeFactory));

		// Streamline manual hooking setup
		if (l_enableStreamline)
		{
			// IMPORTANT: slInit(pref.flags |= eUseManualHooking) must have been called
			// before this.

			impl->pSLProxyFactory = impl->pNativeFactory;
			{
				IDXGIFactory* fac = impl->pSLProxyFactory.Get();
				if (SL_FAILED(res, slUpgradeInterface(reinterpret_cast<void**>(&fac)))) {
					RHI_FAIL(Result::Failed);
				}
				impl->pSLProxyFactory.Attach(static_cast<IDXGIFactory7*>(fac));
			}
		}
		else
		{
			impl->pSLProxyFactory = impl->pNativeFactory;
		}

		ComPtr<IDXGIAdapter1> adapter;
		impl->pNativeFactory->EnumAdapters1(0, &adapter);
		adapter.As(&impl->adapter);

		D3D12CreateDevice(impl->adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&impl->pNativeDevice));

		// Streamline manual hooking setup
		if (l_enableStreamline)
		{
			// Tell SL which native device to use
			if (SL_FAILED(res, slSetD3DDevice(impl->pNativeDevice.Get()))) {
				RHI_FAIL(Result::Failed);
			}

			// Make proxy device/factory via slUpgradeInterface
			impl->pSLProxyDevice = impl->pNativeDevice;
			{
				ID3D12Device* dev = impl->pSLProxyDevice.Get();
				if (SL_FAILED(res, slUpgradeInterface(reinterpret_cast<void**>(&dev)))) {
					RHI_FAIL(Result::Failed);
				}
				impl->pSLProxyDevice.Attach(dev);
			}
		}
		else
		{
			impl->pSLProxyDevice = impl->pNativeDevice;
		}

		if (ci.enableDebug) EnableDebug(impl->pNativeDevice.Get());

		// Queue creation: MUST go through proxy device, store both proxy+native
		auto makeQ = [&](D3D12_COMMAND_LIST_TYPE t, Dx12QueueState& out)
			{
				D3D12_COMMAND_QUEUE_DESC qd{};
				qd.Type = t;

				// Hooked API: CreateCommandQueue must be invoked on proxy when SL enabled
				ComPtr<ID3D12CommandQueue> qProxy;
				HRESULT hr = impl->pSLProxyDevice->CreateCommandQueue(&qd, IID_PPV_ARGS(&qProxy));
				if (FAILED(hr)) { /* handle */ }

				out.pSLProxyQueue = qProxy;

				// Extract native queue for normal engine use to avoid proxies internally
				ID3D12CommandQueue* qNative = nullptr;
				if (SL_FAILED(res, slGetNativeInterface(out.pSLProxyQueue.Get(), (void**)&qNative)))
				{
					// If this fails, fall back to using qProxy as both.
					out.pNativeQueue = out.pSLProxyQueue;
				}
				else
				{
					out.pNativeQueue.Attach(qNative); // qNative is returned with refcount
				}
				//out.dev = impl;
				impl->pNativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&out.fence));
				out.value = 0;
			};

		makeQ(D3D12_COMMAND_LIST_TYPE_DIRECT, impl->gfx);
		makeQ(D3D12_COMMAND_LIST_TYPE_COMPUTE, impl->comp);
		makeQ(D3D12_COMMAND_LIST_TYPE_COPY, impl->copy);

		Device d{ impl.get(), &g_devvt };
		outPtr = MakeDevicePtr(&d, impl);
		return Result::Ok;
	}


}