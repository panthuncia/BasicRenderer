#include "rhi_dx12.h"
#include <atlbase.h>

#define VERIFY(expr) if (FAILED(expr)) { spdlog::error("Validation error!"); }

namespace rhi {
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

	DevicePtr CreateD3D12Device(const DeviceCreateInfo& ci) noexcept {
		UINT flags = 0;
		if (ci.enableDebug) { ComPtr<ID3D12Debug> dbg; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer(), flags |= DXGI_CREATE_FACTORY_DEBUG; }
		auto impl = std::make_shared<Dx12Device>();
		CreateDXGIFactory2(flags, IID_PPV_ARGS(&impl->factory));

		// Select default adapter. TODO: expose adapter selection
		ComPtr<IDXGIAdapter1> adapter;
		impl->factory->EnumAdapters1(0, &adapter);

		adapter.As(&impl->adapter);

		D3D12CreateDevice(impl->adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&impl->dev));
		if (ci.enableDebug) {
			EnableDebug(impl->dev.Get());
		}

#if BUILD_TYPE == BUILD_TYPE_DEBUG
		ComPtr<ID3D12InfoQueue1> infoQueue;
		if (SUCCEEDED(impl->dev->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

			DWORD callbackCookie = 0;
			//infoQueue->RegisterMessageCallback([](D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context) {
			//	// Log or print the debug messages,
			//	spdlog::error("D3D12 Debug Message: {}", description);
			//	}, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &callbackCookie);
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

		impl->selfWeak = impl;

		Device d { impl.get(), &g_devvt};
		return MakeDevicePtr(&d, impl);
	}

}