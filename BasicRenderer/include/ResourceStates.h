#pragma once

#include <d3d12.h>
#include <stdexcept>

class Resource;

enum class ResourceState {
	UNKNOWN,
	INDEX,
	VERTEX,
	CONSTANT,
	PIXEL_SRV,
	NON_PIXEL_SRV,
	ALL_SRV,
	RENDER_TARGET,
	DEPTH_WRITE,
	DEPTH_READ,
	UPLOAD,
	COPY_SOURCE,
	COPY_DEST,
};

inline D3D12_RESOURCE_STATES ResourceStateToD3D12(ResourceState state) {
	switch (state) {
	case ResourceState::UNKNOWN:
		return D3D12_RESOURCE_STATE_COMMON;
	case ResourceState::UPLOAD:
		return D3D12_RESOURCE_STATE_GENERIC_READ;
	case ResourceState::RENDER_TARGET:
		return D3D12_RESOURCE_STATE_RENDER_TARGET;
	case ResourceState::DEPTH_WRITE:
		return D3D12_RESOURCE_STATE_DEPTH_WRITE;
	case ResourceState::DEPTH_READ:
		return D3D12_RESOURCE_STATE_DEPTH_READ;
	case ResourceState::PIXEL_SRV:
		return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	case ResourceState::NON_PIXEL_SRV:
		return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	case ResourceState::ALL_SRV:
		return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	case ResourceState::INDEX:
		return D3D12_RESOURCE_STATE_INDEX_BUFFER;
	case ResourceState::VERTEX:
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	case ResourceState::CONSTANT:
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	case ResourceState::COPY_SOURCE:
		return D3D12_RESOURCE_STATE_COPY_SOURCE;
	case ResourceState::COPY_DEST:
		return D3D12_RESOURCE_STATE_COPY_DEST;
	}
	throw std::runtime_error("Invalid Resource State");
}

struct ResourceTransition {
	Resource* resource;
	ID3D12Resource* APIResource;
	ResourceState beforeState;
	ResourceState afterState;
};