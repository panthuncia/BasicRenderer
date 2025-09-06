// rhi_interop_dx12.h  (include only where needed)
#pragma once
#include "rhi.h"
#include "rhi_interop.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include "sl_result.h"

namespace rhi::dx12 {

    inline ID3D12Device* get_device(rhi::Device d) {
        D3D12DeviceInfo info{};
        if (!QueryNativeDevice(d, RHI_IID_D3D12_DEVICE, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12Device*>(info.device);
    }
    inline IDXGIFactory7* get_factory(rhi::Device d) {
        D3D12DeviceInfo info{};
        if (!QueryNativeDevice(d, RHI_IID_D3D12_DEVICE, &info, sizeof(info))) return nullptr;
        return static_cast<IDXGIFactory7*>(info.factory);
    }
    inline IDXGIAdapter4* get_adapter(rhi::Device d) {
        D3D12DeviceInfo info{};
        if (!QueryNativeDevice(d, RHI_IID_D3D12_DEVICE, &info, sizeof(info))) return nullptr;
        return static_cast<IDXGIAdapter4*>(info.adapter);
    }
    inline ID3D12CommandQueue* get_queue(rhi::Queue q) {
        D3D12QueueInfo info{};
        if (!QueryNativeQueue(q, RHI_IID_D3D12_QUEUE, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12CommandQueue*>(info.queue);
    }
    inline ID3D12GraphicsCommandList* get_cmd_list(rhi::CommandList cl) {
        D3D12CmdListInfo info{};
        if (!QueryNativeCmdList(cl, RHI_IID_D3D12_CMD_LIST, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12GraphicsCommandList*>(info.cmdList);
    }
    inline ID3D12CommandAllocator* get_allocator(rhi::CommandList cl) {
        D3D12CmdListInfo info{};
        if (!QueryNativeCmdList(cl, RHI_IID_D3D12_CMD_LIST, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12CommandAllocator*>(info.allocator);
    }
    inline IDXGISwapChain3* get_swapchain(rhi::Swapchain sc) {
        D3D12SwapchainInfo info{};
        if (!QueryNativeSwapchain(sc, RHI_IID_D3D12_SWAPCHAIN, &info, sizeof(info))) return nullptr;
        return static_cast<IDXGISwapChain3*>(info.swapchain);
    }
    inline ID3D12Resource* get_resource(rhi::Resource h) {
		D3D12ResourceInfo info{};
		if (!QueryNativeResource(h, RHI_IID_D3D12_RESOURCE, &info, sizeof(info))) return nullptr;
		return static_cast<ID3D12Resource*>(info.resource);
	}
    inline ID3D12Heap* get_heap(rhi::Heap h) {
        D3D12HeapInfo info{};
		if (!QueryNativeHeap(h, RHI_IID_D3D12_HEAP, &info, sizeof(info))) return nullptr;
		return static_cast<ID3D12Heap*>(info.heap);
	}
    inline ID3D12QueryHeap* get_querypool(rhi::QueryPool qp) {
		D3D12QueryPoolInfo info{};
		if (!QueryNativeQueryPool(qp, RHI_IID_D3D12_HEAP, &info, sizeof(info))) return nullptr;
		return static_cast<ID3D12QueryHeap*>(info.queryPool);
	}
    inline ID3D12PipelineState* get_pipeline(rhi::Pipeline p) {
        D3D12PipelineInfo info{};
        if (!QueryNativePipeline(p, RHI_IID_D3D12_PIPELINE, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12PipelineState*>(info.pipeline);
	}
    inline ID3D12RootSignature* get_pipeline_layout(rhi::PipelineLayout pl) {
        D3D12PipelineLayoutInfo info{};
        if (!QueryNativePipelineLayout(pl, RHI_IID_D3D12_PIPELINE_LAYOUT, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12RootSignature*>(info.layout);
	}
    inline ID3D12CommandSignature* get_command_signature(rhi::CommandSignature cs) {
        D3D12CommandSignatureInfo info{};
        if (!QueryNativeCommandSignature(cs, RHI_IID_D3D12_COMMAND_SIGNATURE, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12CommandSignature*>(info.cmdSig);
	}
    inline ID3D12DescriptorHeap* get_descriptor_heap(rhi::DescriptorHeap dh) {
        D3D12DescriptorHeapInfo info{};
        if (!QueryNativeDescriptorHeap(dh, RHI_IID_D3D12_DESCRIPTOR_HEAP, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12DescriptorHeap*>(info.descHeap);
	}
    inline ID3D12Fence* get_timeline(rhi::Timeline t) {
        D3D12TimelineInfo info{};
        if (!QueryNativeTimeline(t, RHI_IID_D3D12_TIMELINE, &info, sizeof(info))) return nullptr;
        return static_cast<ID3D12Fence*>(info.timeline);
	}

    using PFN_UpgradeInterface = sl::Result(*)(void**); // Streamline's slUpgradeInterface-compatible
    bool enable_streamline_interposer(Device, PFN_UpgradeInterface upgrade);
    void disable_streamline_interposer(Device);
} // namespace rhi::dx12
