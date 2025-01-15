#pragma once

#include <directx/d3d12.h>
#include <stdexcept>
#if defined(_DEBUG)
#include <string>
#endif // _DEBUG


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
	UNORDERED_ACCESS,
	INDIRECT_ARGUMENT,
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
	case ResourceState::UNORDERED_ACCESS:
		return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	case ResourceState::INDIRECT_ARGUMENT:
		return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
	}
	throw std::runtime_error("Invalid Resource State");
}

inline D3D12_BARRIER_ACCESS ResourceStateToD3D12AccessType(ResourceState state) {
	switch (state) {
	case ResourceState::UNKNOWN:
		return D3D12_BARRIER_ACCESS_COMMON;
	case ResourceState::INDEX:
		return D3D12_BARRIER_ACCESS_INDEX_BUFFER;
	case ResourceState::VERTEX:
		return D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
	case ResourceState::CONSTANT:
		return D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
	case ResourceState::PIXEL_SRV:
		return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	case ResourceState::NON_PIXEL_SRV:
		return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	case ResourceState::ALL_SRV:
		return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	case ResourceState::RENDER_TARGET:
		return D3D12_BARRIER_ACCESS_RENDER_TARGET;
	case ResourceState::DEPTH_WRITE:
		return D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
	case ResourceState::DEPTH_READ:
		return D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
	case ResourceState::UPLOAD:
		return D3D12_BARRIER_ACCESS_COPY_SOURCE;
	case ResourceState::COPY_SOURCE:
		return D3D12_BARRIER_ACCESS_COPY_SOURCE;
	case ResourceState::COPY_DEST:
		return D3D12_BARRIER_ACCESS_COPY_DEST;
	case ResourceState::UNORDERED_ACCESS:
		return D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
	case ResourceState::INDIRECT_ARGUMENT:
		return D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
	}
	throw std::runtime_error("Invalid Resource State");
}

// TODO: Use COMPUTE_QUEUE and DIRECT_QUEUE barrier types- requires reworking render graph compiler
inline D3D12_BARRIER_LAYOUT ResourceStateToD3D12GraphicsBarrierLayout(ResourceState state) {
	switch (state) {
	case ResourceState::UNKNOWN:
		return D3D12_BARRIER_LAYOUT_COMMON;
	case ResourceState::INDEX:
		throw std::runtime_error("Index Buffer is not a texture and has no layout");
	case ResourceState::VERTEX:
		throw std::runtime_error("Vertex Buffer is not a texture and has no layout");
	case ResourceState::CONSTANT:
		throw std::runtime_error("Constant Buffer is not a texture and has no layout");
	case ResourceState::PIXEL_SRV:
		return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
	case ResourceState::NON_PIXEL_SRV:
		return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
	case ResourceState::ALL_SRV:
		return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
	case ResourceState::RENDER_TARGET:
		return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	case ResourceState::DEPTH_WRITE:
		return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
	case ResourceState::DEPTH_READ:
		return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
	case ResourceState::UPLOAD:
		return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
	case ResourceState::COPY_SOURCE:
		return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
	case ResourceState::COPY_DEST:
		return D3D12_BARRIER_LAYOUT_COPY_DEST;
	case ResourceState::UNORDERED_ACCESS:
		return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
	case ResourceState::INDIRECT_ARGUMENT:
		throw std::runtime_error("Indirect Argument Buffer is not a texture and has no layout");
	}
	throw std::runtime_error("Invalid Resource State");
}

inline D3D12_BARRIER_LAYOUT ResourceStateToD3D12ComputeBarrierLayout(ResourceState state) {
	switch (state) {
	case ResourceState::UNKNOWN:
		return D3D12_BARRIER_LAYOUT_COMMON;
	case ResourceState::INDEX:
		throw std::runtime_error("Index Buffer is not a texture and has no layout");
	case ResourceState::VERTEX:
		throw std::runtime_error("Vertex Buffer is not a texture and has no layout");
	case ResourceState::CONSTANT:
		throw std::runtime_error("Constant Buffer is not a texture and has no layout");
	case ResourceState::PIXEL_SRV:
		throw std::runtime_error("Pixel Shader Resource is not a compute resource bitmask");
	case ResourceState::NON_PIXEL_SRV:
		return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
	case ResourceState::ALL_SRV:
		throw std::runtime_error("All Shader Resource is not a valid compute bitmask");
	case ResourceState::RENDER_TARGET:
		throw std::runtime_error("Render Target is not a compute resource bitmask");
	case ResourceState::DEPTH_WRITE:
		throw std::runtime_error("Depth Write is not a compute resource bitmask");
	case ResourceState::DEPTH_READ:
		throw std::runtime_error("Depth Read is not a compute resource bitmask");
	case ResourceState::UPLOAD:
		return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
	case ResourceState::COPY_SOURCE:
		return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
	case ResourceState::COPY_DEST:
		return D3D12_BARRIER_LAYOUT_COPY_DEST;
	case ResourceState::UNORDERED_ACCESS:
		return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
	case ResourceState::INDIRECT_ARGUMENT:
		throw std::runtime_error("Indirect Argument Buffer is not a texture and has no layout");
	}
	throw std::runtime_error("Invalid Resource State");
}

enum class ResourceSyncState {
	NONE,
	ALL,
	DRAW,
	INDEX_INPUT,
	VERTEX_SHADING,
	PIXEL_SHADING,
	DEPTH_STENCIL,
	RENDER_TARGET,
	COMPUTE_SHADING,
	RAYTRACING,
	COPY,
	RESOLVE,
	EXECUTE_INDIRECT,
	PREDICATION,
	ALL_SHADING,
	NON_PIXEL_SHADING,
	EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO,
	CLEAR_UNORDERED_ACCESS_VIEW,
	VIDEO_DECODE,
	VIDEO_PROCESS,
	VIDEO_ENCODE,
	BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
	COPY_RAYTRACING_ACCELERATION_STRUCTURE,
	SYNC_SPLIT,
};

inline D3D12_BARRIER_SYNC ResourceSyncStateToD3D12(ResourceSyncState state) {
	switch (state) {
	case ResourceSyncState::NONE:
		return D3D12_BARRIER_SYNC_NONE;
	case ResourceSyncState::ALL:
		return D3D12_BARRIER_SYNC_ALL;
	case ResourceSyncState::DRAW:
		return D3D12_BARRIER_SYNC_DRAW;
	case ResourceSyncState::INDEX_INPUT:
		return D3D12_BARRIER_SYNC_INDEX_INPUT;
	case ResourceSyncState::VERTEX_SHADING:
		return D3D12_BARRIER_SYNC_VERTEX_SHADING;
	case ResourceSyncState::PIXEL_SHADING:
		return D3D12_BARRIER_SYNC_PIXEL_SHADING;
	case ResourceSyncState::DEPTH_STENCIL:
		return D3D12_BARRIER_SYNC_DEPTH_STENCIL;
	case ResourceSyncState::RENDER_TARGET:
		return D3D12_BARRIER_SYNC_RENDER_TARGET;
	case ResourceSyncState::COMPUTE_SHADING:
		return D3D12_BARRIER_SYNC_COMPUTE_SHADING;
	case ResourceSyncState::RAYTRACING:
		return D3D12_BARRIER_SYNC_RAYTRACING;
	case ResourceSyncState::COPY:
		return D3D12_BARRIER_SYNC_COPY;
	case ResourceSyncState::RESOLVE:
		return D3D12_BARRIER_SYNC_RESOLVE;
	case ResourceSyncState::EXECUTE_INDIRECT:
		return D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
	case ResourceSyncState::PREDICATION:
		return D3D12_BARRIER_SYNC_PREDICATION;
	case ResourceSyncState::ALL_SHADING:
		return D3D12_BARRIER_SYNC_ALL_SHADING;
	case ResourceSyncState::NON_PIXEL_SHADING:
		return D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
	case ResourceSyncState::EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO:
		return D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO;
	case ResourceSyncState::CLEAR_UNORDERED_ACCESS_VIEW:
		return D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
	case ResourceSyncState::VIDEO_DECODE:
		return D3D12_BARRIER_SYNC_VIDEO_DECODE;
	case ResourceSyncState::VIDEO_PROCESS:
		return D3D12_BARRIER_SYNC_VIDEO_PROCESS;
	case ResourceSyncState::VIDEO_ENCODE:
		return D3D12_BARRIER_SYNC_VIDEO_ENCODE;
	case ResourceSyncState::BUILD_RAYTRACING_ACCELERATION_STRUCTURE:
		return D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
	case ResourceSyncState::COPY_RAYTRACING_ACCELERATION_STRUCTURE:
		return D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
	case ResourceSyncState::SYNC_SPLIT:
		return D3D12_BARRIER_SYNC_SPLIT;
	}
	throw std::runtime_error("Invalid Sync State");
}

struct ResourceTransition {
	Resource* resource = nullptr;
	ResourceState beforeState;
	ResourceState afterState;
#if defined(_DEBUG)
	std::wstring name;
#endif // DEBUG
};