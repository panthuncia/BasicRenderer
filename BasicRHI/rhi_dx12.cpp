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
		&cl_clearView,
		&cl_executeIndirect,
		&cl_setDescriptorHeaps,
		&cl_clearUavUint,
		&cl_clearUavFloat,
		&cl_copyBufferToTexture,
		&cl_copyTextureRegion,
		&cl_copyBufferRegion,
		&cl_writeTimestamp,
		&cl_beginQuery,
		&cl_endQuery,
		&cl_resolveQueryData,
		&cl_resetQueries,
		&cl_setName,
		1u
    };
    const SwapchainVTable g_scvt = { 
		&sc_count,
		&sc_curr,
		&sc_rtv,
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
}