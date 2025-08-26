#include "rhi_dx12.h"

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
		&d_createBuffer,
		&d_createTexture,
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
		
		&d_getQueue,
		&d_waitIdle,
		&d_flushDeletionQueue,
		&d_destroyDevice,
        2u
    };

    const QueueVTable g_qvt = { 
		&q_submit,
		&q_signal,
		&q_wait, 
		1u };

	const CommandAllocatorVTable g_calvt = {
		&ca_reset,
		1u
	};

    const CommandListVTable g_clvt = {
        &cl_begin,
		&cl_end,
		&cl_reset,
		&cl_beginPass,
		&cl_endPass,
		&cl_barriers,
		&cl_bindLayout,
		&cl_bindPipeline,
		&cl_setVB,
		&cl_setIB,
		&cl_draw,
		&cl_drawIndexed,
		&cl_dispatch,
		&cl_clearView,
		&cl_executeIndirect,
		&cl_setDescriptorHeaps,
		1u
    };
    const SwapchainVTable g_scvt = { 
		&sc_count,
		&sc_curr,
		&sc_rtv,
		&sc_img,
		&sc_resize,
		&sc_present, 
		1u };
}