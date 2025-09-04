#pragma once

#include <cstdint>

#include "rhi.h"

namespace rhi {
	enum class NativeBackend : uint32_t { Unknown, D3D12, Vulkan };

	// --- Generic native handle (opaque)
	struct NativeHandle { void* ptr{}; uint32_t tag{}; uint32_t version{}; };

	// --- Stable, versioned IDs for interop structs
	constexpr uint32_t RHI_IID_D3D12_DEVICE = 0x1001;
	constexpr uint32_t RHI_IID_D3D12_QUEUE = 0x1002;
	constexpr uint32_t RHI_IID_D3D12_CMD_LIST = 0x1003;
	constexpr uint32_t RHI_IID_D3D12_SWAPCHAIN = 0x1004;
	constexpr uint32_t RHI_IID_D3D12_RESOURCE = 0x1005;
	constexpr uint32_t RHI_IID_D3D12_HEAP = 0x1006;

	constexpr uint32_t RHI_IID_VK_DEVICE = 0x2001;
	constexpr uint32_t RHI_IID_VK_QUEUE = 0x2002;
	constexpr uint32_t RHI_IID_VK_COMMAND_BUFFER = 0x2003;
	constexpr uint32_t RHI_IID_VK_SWAPCHAIN = 0x2004;
	constexpr uint32_t RHI_IID_VK_RESOURCE = 0x2005;
	constexpr uint32_t RHI_IID_VK_HEAP = 0x2006;

	// --- Narrow, typed query surface (no native types)
	struct D3D12DeviceInfo { void* device; void* factory; void* adapter; uint32_t version; };
	struct D3D12QueueInfo { void* queue;  uint32_t version; };
	struct D3D12CmdListInfo { void* cmdList; void* allocator; uint32_t version; };
	struct D3D12SwapchainInfo { void* swapchain; uint32_t version; };
	struct D3D12ResourceInfo { void* resource; uint32_t version; };
	struct D3D12HeapInfo { void* heap; uint32_t version; };

	struct VulkanDeviceInfo { void* instance; void* physicalDevice; void* device; uint32_t version; };
	struct VulkanQueueInfo { void* queue; uint32_t familyIndex; uint32_t version; };
	struct VulkanCmdBufInfo { void* commandBuffer; uint32_t version; };
	struct VulkanSwapchainInfo { void* swapchain; uint32_t version; };
	struct VulkanResourceInfo { void* resource; uint32_t version; };
	struct VulkanHeapInfo { void* heap; uint32_t version; };

	// --- Query interface entry points (implemented by each backend)
	bool QueryNativeDevice(Device, uint32_t iid /*RHI_IID_...*/, void* outStruct /*typed*/, uint32_t outSize) noexcept;
	bool QueryNativeQueue(Queue, uint32_t iid, void* outStruct, uint32_t outSize) noexcept;
	bool QueryNativeCmdList(CommandList, uint32_t iid, void* outStruct, uint32_t outSize) noexcept;
	bool QueryNativeSwapchain(Swapchain, uint32_t iid, void* outStruct, uint32_t outSize) noexcept;
	bool QueryNativeResource(Resource, uint32_t iid, void* outStruct, uint32_t outSize) noexcept;
	bool QueryNativeHeap(Heap, uint32_t iid, void* outStruct, uint32_t outSize) noexcept;
} // namespace rhi