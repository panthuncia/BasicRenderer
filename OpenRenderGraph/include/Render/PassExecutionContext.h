#pragma once

#include <cstdint>
#include <rhi.h>
#include <DirectXMath.h>

struct HostFrameData {
	rhi::DescriptorHeap textureDescriptorHeap;
	rhi::DescriptorHeap samplerDescriptorHeap;
	DirectX::XMUINT2 renderResolution = {};
	DirectX::XMUINT2 outputResolution = {};
	void* userFrameData = nullptr;

	template<typename T>
	T* GetUserFrameDataAs() const noexcept {
		return static_cast<T*>(userFrameData);
	}
};

struct PassExecutionContext {
	rhi::Device device;
	rhi::CommandList commandList;
	rhi::Queue commandQueue;
	UINT frameIndex = 0;
	UINT64 frameFenceValue = 0;
	float deltaTime = 0.0f;
	HostFrameData* hostFrameData = nullptr;
};
