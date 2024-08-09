#pragma once
#include <d3d12.h>
#include <vector>
#include <wrl.h>

#include "Buffers.h"
#include "CBuffer.h"

class FrameResource {
public:
	FrameResource() = default;
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	void Initialize();
	~FrameResource();
	// We cannot reset the allocator until the GPU is done processing the
	// commands. So each frame needs their own allocator.
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;
	// We cannot update a cbuffer until the GPU is done processing the
	// commands that reference it. So each frame needs their own cbuffers.
	std::vector<CBuffer<PerMeshCB>> objectConstantBuffers;
	std::vector<CBuffer<PerMaterialCB>> materialConstantBuffers;
	CBuffer<PerFrameCB> frameConstantBuffer;
	std::vector<LightInfo> lightsData;
	ComPtr<ID3D12Resource> lightBuffer;
	// Fence value to mark commands up to this fence point. This lets us
	// check if these frame resources are still in use by the GPU.
	UINT64 Fence = 0;

	ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	UINT descriptorSize = 0;
};