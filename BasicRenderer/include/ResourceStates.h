#pragma once

#include <d3d12.h>

enum class ResourceState {
    Undefined,
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderResource,
    CopySource,
    CopyDest,
};

inline D3D12_RESOURCE_STATES ResourceStateToD3D12(ResourceState state) {
	switch (state) {
	case ResourceState::Common:
		return D3D12_RESOURCE_STATE_COMMON;
	case ResourceState::RenderTarget:
		return D3D12_RESOURCE_STATE_RENDER_TARGET;
	case ResourceState::DepthWrite:
		return D3D12_RESOURCE_STATE_DEPTH_WRITE;
	case ResourceState::DepthRead:
		return D3D12_RESOURCE_STATE_DEPTH_READ;
	case ResourceState::ShaderResource:
		return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	case ResourceState::CopySource:
		return D3D12_RESOURCE_STATE_COPY_SOURCE;
	case ResourceState::CopyDest:
		return D3D12_RESOURCE_STATE_COPY_DEST;
	default:
		return D3D12_RESOURCE_STATE_COMMON;
	}
}