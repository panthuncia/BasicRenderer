// rhi_interop_dx12.h  (include only where needed)
#pragma once
#include "rhi.h"
#include <d3d12.h>
#include <dxgi1_6.h>

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
} // namespace rhi::dx12
