#pragma once

#include <directx/d3d12.h>
#include <stdexcept>
#include <spdlog/spdlog.h>
#if defined(_DEBUG)
#include <string>
#endif // _DEBUG
#include <set>


class Resource;

enum ResourceAccessType {
	NONE = 0,
	COMMON = 1,
	VERTEX_BUFFER = 1<<1,
	CONSTANT_BUFFER = 1<<2,
	INDEX_BUFFER = 1<<3,
	RENDER_TARGET = 1<<4,
	UNORDERED_ACCESS = 1<<5,
	DEPTH_READ_WRITE = 1<<6,
	DEPTH_READ = 1<<7,
	SHADER_RESOURCE = 1<<8,
	INDIRECT_ARGUMENT = 1<<9,
	COPY_DEST = 1<<10,
	COPY_SOURCE = 1<<11,
	RAYTRACING_ACCELERATION_STRUCTURE_READ = 1<<12,
	RAYTRACING_ACCELERATION_STRUCTURE_WRITE = 1<<13,
};

inline ResourceAccessType operator|(ResourceAccessType a, ResourceAccessType b)
{
	return static_cast<ResourceAccessType>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

enum class ResourceLayout {
	LAYOUT_UNDEFINED = D3D12_BARRIER_LAYOUT_UNDEFINED,
	LAYOUT_COMMON = D3D12_BARRIER_LAYOUT_COMMON,
	LAYOUT_PRESENT = D3D12_BARRIER_LAYOUT_PRESENT,
	LAYOUT_GENERIC_READ = D3D12_BARRIER_LAYOUT_GENERIC_READ,
	LAYOUT_RENDER_TARGET = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
	LAYOUT_UNORDERED_ACCESS = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
	LAYOUT_DEPTH_READ_WRITE = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
	LAYOUT_DEPTH_READ = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
	LAYOUT_SHADER_RESOURCE = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
	LAYOUT_COPY_SOURCE = D3D12_BARRIER_LAYOUT_COPY_SOURCE,
	LAYOUT_COPY_DEST = D3D12_BARRIER_LAYOUT_COPY_DEST,

	LAYOUT_RESOLVE_SOURCE = D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE, // ?
	LAYOUT_RESOLVE_DEST = D3D12_BARRIER_LAYOUT_RESOLVE_DEST,
	LAYOUT_SHADING_RATE_SOURCE = D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE,

	LAYOUT_DIRECT_COMMON = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON,
	LAYOUT_DIRECT_GENERIC_READ = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ,
	LAYOUT_DIRECT_UNORDERED_ACCESS = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS,
	LAYOUT_DIRECT_SHADER_RESOURCE = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE,
	LAYOUT_DIRECT_COPY_SOURCE = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE,
	LAYOUT_DIRECT_COPY_DEST = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST,

	LAYOUT_COMPUTE_COMMON = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON,
	LAYOUT_COMPUTE_GENERIC_READ = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ,
	LAYOUT_COMPUTE_UNORDERED_ACCESS = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS,
	LAYOUT_COMPUTE_SHADER_RESOURCE = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE,
	LAYOUT_COMPUTE_COPY_SOURCE = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE,
	LAYOUT_COMPUTE_COPY_DEST = D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST,
};

inline bool ResourceLayoutIsUnorderedAccess(ResourceLayout layout) {
	return layout == ResourceLayout::LAYOUT_UNORDERED_ACCESS ||
		layout == ResourceLayout::LAYOUT_DIRECT_UNORDERED_ACCESS ||
		layout == ResourceLayout::LAYOUT_COMPUTE_UNORDERED_ACCESS;
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

inline unsigned int ResourceAccessGetNumReadStates(ResourceAccessType access) {
	if (access & ResourceAccessType::SHADER_RESOURCE && access & ResourceAccessType::DEPTH_READ) {
		spdlog::warn("ResourceAccessGetNumReadStates: SHADER_RESOURCE and DEPTH_READ are both set. This is not supported.");
	}
	int num = 0;
	if (access & ResourceAccessType::SHADER_RESOURCE) num++;
	if (access & ResourceAccessType::DEPTH_READ) num++;
	if (access & ResourceAccessType::RENDER_TARGET) num++;
	if (access & ResourceAccessType::COPY_SOURCE) num++;
	if (access & ResourceAccessType::INDEX_BUFFER) num++;
	if (access & ResourceAccessType::VERTEX_BUFFER) num++;
	if (access & ResourceAccessType::CONSTANT_BUFFER) num++;

	return num;
}

inline ResourceLayout AccessToLayout(ResourceAccessType access, bool directQueue) {
	// most-specific first:
	if (access & ResourceAccessType::COMMON)
		return ResourceLayout::LAYOUT_COMMON;
	if (access & ResourceAccessType::UNORDERED_ACCESS) 
		return ResourceLayout::LAYOUT_UNORDERED_ACCESS;
	if (access & ResourceAccessType::RENDER_TARGET)      
		return ResourceLayout::LAYOUT_RENDER_TARGET;
	if (access & ResourceAccessType::DEPTH_READ_WRITE)        
		return ResourceLayout::LAYOUT_DEPTH_READ_WRITE;
	if (access & ResourceAccessType::COPY_SOURCE)        
		return ResourceLayout::LAYOUT_COPY_SOURCE;
	if (access & ResourceAccessType::COPY_DEST)          
		return ResourceLayout::LAYOUT_COPY_DEST;

	auto num = ResourceAccessGetNumReadStates(access);
	if (num > 1) {
		if (directQueue) {
			return ResourceLayout::LAYOUT_DIRECT_GENERIC_READ;
		}
		else {
			return ResourceLayout::LAYOUT_COMPUTE_GENERIC_READ;
		}
	}
	else {
		if (access & ResourceAccessType::SHADER_RESOURCE) {
			return ResourceLayout::LAYOUT_SHADER_RESOURCE;
		}
		if (access & ResourceAccessType::DEPTH_READ) {
			return ResourceLayout::LAYOUT_DEPTH_READ;
		}
		if (access & ResourceAccessType::INDEX_BUFFER) {
			return ResourceLayout::LAYOUT_GENERIC_READ;
		}
		if (access & ResourceAccessType::VERTEX_BUFFER) {
			return ResourceLayout::LAYOUT_GENERIC_READ;
		}
		if (access & ResourceAccessType::CONSTANT_BUFFER) {
			return ResourceLayout::LAYOUT_GENERIC_READ;
		}
	}
	
	return ResourceLayout::LAYOUT_COMMON;
}


inline ResourceSyncState ComputeSyncFromAccess(ResourceAccessType access) {
	bool needsIndirect = (access & ResourceAccessType::INDIRECT_ARGUMENT) != 0;

	if (needsIndirect) {
		return ResourceSyncState::EXECUTE_INDIRECT;
	}

	return ResourceSyncState::COMPUTE_SHADING;
}

inline ResourceSyncState RenderSyncFromAccess(ResourceAccessType access)
{
	// pick out each distinct sync category
	bool needsCommon      = (access & ResourceAccessType::COMMON) != 0;
	bool needsShading     = (access & (ResourceAccessType::VERTEX_BUFFER
		| ResourceAccessType::CONSTANT_BUFFER
		| ResourceAccessType::SHADER_RESOURCE
		| ResourceAccessType::UNORDERED_ACCESS)) != 0;
	bool needsIndexInput  = (access & ResourceAccessType::INDEX_BUFFER) != 0;
	bool needsRenderTarget= (access & ResourceAccessType::RENDER_TARGET) != 0;
	bool needsDepthStencil= (access & (ResourceAccessType::DEPTH_READ
		| ResourceAccessType::DEPTH_READ_WRITE)) != 0;
	bool needsCopy        = (access & (ResourceAccessType::COPY_SOURCE
		| ResourceAccessType::COPY_DEST)) != 0;
	bool needsIndirect    = (access & ResourceAccessType::INDIRECT_ARGUMENT) != 0;
	bool needsRayTracing  = (access & ResourceAccessType::RAYTRACING_ACCELERATION_STRUCTURE_READ) != 0;
	bool needsBuildAS     = (access & ResourceAccessType::RAYTRACING_ACCELERATION_STRUCTURE_WRITE)!= 0;

	// count how many distinct categories are requested
	int categoryCount =
		(int)needsCommon
		+ (int)needsShading
		+ (int)needsIndexInput
		+ (int)needsRenderTarget
		+ (int)needsDepthStencil
		+ (int)needsCopy
		+ (int)needsIndirect
		+ (int)needsRayTracing
		+ (int)needsBuildAS;

	// zero categories = no sync
	if (categoryCount == 0)
		return ResourceSyncState::NONE;
	// more than one category = full pipeline sync
	if (categoryCount > 1)
		return ResourceSyncState::ALL;

	if (needsRenderTarget && needsShading) {
		spdlog::warn("RenderSyncFromAccess: RenderTarget and Shading access types are both set. This is not supported.");
	}

	// exactly one category = pick it
	if (needsCommon)        return ResourceSyncState::ALL;
	if (needsShading)       return ResourceSyncState::ALL_SHADING;
	if (needsIndexInput)    return ResourceSyncState::INDEX_INPUT;
	if (needsRenderTarget)  return ResourceSyncState::RENDER_TARGET;
	if (needsDepthStencil)  return ResourceSyncState::DEPTH_STENCIL;
	if (needsCopy)          return ResourceSyncState::COPY;
	if (needsIndirect)      return ResourceSyncState::EXECUTE_INDIRECT;
	if (needsBuildAS)       return ResourceSyncState::BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
	if (needsRayTracing)    return ResourceSyncState::RAYTRACING;


	throw std::runtime_error("ResourceSyncState: Unknown access type");
	// (should never get here)
	return ResourceSyncState::ALL;
}

inline bool AccessTypeIsWriteType(ResourceAccessType access) {
	if (access & ResourceAccessType::RENDER_TARGET) return true;
	if (access & ResourceAccessType::DEPTH_READ_WRITE) return true;
	if (access & ResourceAccessType::COPY_DEST) return true;
	if (access & ResourceAccessType::UNORDERED_ACCESS) return true;
	if (access & ResourceAccessType::RAYTRACING_ACCELERATION_STRUCTURE_WRITE) return true;
	return false;
}

inline D3D12_BARRIER_ACCESS ResourceAccessTypeToD3D12(ResourceAccessType state) {
	if (state == ResourceAccessType::NONE) {
		return D3D12_BARRIER_ACCESS_NO_ACCESS;
	}
	D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;
	if (state & ResourceAccessType::INDEX_BUFFER) {
		access |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
	}
	if (state & ResourceAccessType::VERTEX_BUFFER) {
		access |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
	}
	if (state & ResourceAccessType::CONSTANT_BUFFER) {
		access |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
	}
	if (state & ResourceAccessType::SHADER_RESOURCE) {
		access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	}
	if (state & ResourceAccessType::RENDER_TARGET) {
		access |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
	}
	if (state & ResourceAccessType::DEPTH_READ_WRITE) {
		access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
	}
	if (state & ResourceAccessType::DEPTH_READ) {
		access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
	}
	if (state & ResourceAccessType::COPY_SOURCE) {
		access |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
	}
	if (state & ResourceAccessType::COPY_DEST) {
		access |= D3D12_BARRIER_ACCESS_COPY_DEST;
	}
	if (state & ResourceAccessType::UNORDERED_ACCESS) {
		access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
	}
	if (state & ResourceAccessType::INDIRECT_ARGUMENT) {
		access |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
	}
	if (state & ResourceAccessType::RAYTRACING_ACCELERATION_STRUCTURE_READ) {
		access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
	}
	if (state & ResourceAccessType::RAYTRACING_ACCELERATION_STRUCTURE_WRITE) {
		access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
	}
	return access;
}

inline bool ResourceSyncStateIsNotComputeSyncState(ResourceSyncState state) {
	switch (state) { // TODO: What states can the compute queue actually work with?
	case ResourceSyncState::NONE:
	case ResourceSyncState::ALL:
	case ResourceSyncState::COMPUTE_SHADING:
		return false;
	}
	return true;
}

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

inline bool ValidateResourceLayoutAndAccessType(ResourceLayout layout, ResourceAccessType access) {
	if (access & ResourceAccessType::DEPTH_READ && access & ResourceAccessType::DEPTH_READ_WRITE) {
		return false;
	}
	switch (layout) {
	case ResourceLayout::LAYOUT_COMMON:
		if ((access & ~(ResourceAccessType::SHADER_RESOURCE | ResourceAccessType::COPY_DEST | ResourceAccessType::COPY_SOURCE)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_DIRECT_COMMON:
		if ((access & ~(ResourceAccessType::SHADER_RESOURCE | ResourceAccessType::COPY_DEST | ResourceAccessType::COPY_SOURCE | ResourceAccessType::UNORDERED_ACCESS)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_COMPUTE_COMMON:
		if ((access & ~(ResourceAccessType::SHADER_RESOURCE | ResourceAccessType::COPY_DEST | ResourceAccessType::COPY_SOURCE | ResourceAccessType::UNORDERED_ACCESS)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_GENERIC_READ:
		if ((access & ~(ResourceAccessType::SHADER_RESOURCE | ResourceAccessType::COPY_SOURCE)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_DIRECT_GENERIC_READ:
		if ((access & ~(ResourceAccessType::SHADER_RESOURCE | ResourceAccessType::COPY_SOURCE | ResourceAccessType::DEPTH_READ /*| ResourceAccessType::SHADING_RATE_SOURCE | ResourceAccessType::RESOLVE_SOURCE*/)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_COMPUTE_GENERIC_READ:
		if ((access & ~(ResourceAccessType::SHADER_RESOURCE | ResourceAccessType::COPY_SOURCE)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_RENDER_TARGET:
		if ((access & ~(ResourceAccessType::RENDER_TARGET)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_UNORDERED_ACCESS:
	case ResourceLayout::LAYOUT_DIRECT_UNORDERED_ACCESS:
	case ResourceLayout::LAYOUT_COMPUTE_UNORDERED_ACCESS:
		if ((access & ~(ResourceAccessType::UNORDERED_ACCESS)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_DEPTH_READ_WRITE:
		if ((access & ~(ResourceAccessType::DEPTH_READ_WRITE | ResourceAccessType::DEPTH_READ)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_DEPTH_READ:
		if ((access & ~(ResourceAccessType::DEPTH_READ)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_SHADER_RESOURCE:
	case ResourceLayout::LAYOUT_DIRECT_SHADER_RESOURCE:
	case ResourceLayout::LAYOUT_COMPUTE_SHADER_RESOURCE:
		if ((access & ~(ResourceAccessType::SHADER_RESOURCE)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_COPY_SOURCE:
	case ResourceLayout::LAYOUT_DIRECT_COPY_SOURCE:
	case ResourceLayout::LAYOUT_COMPUTE_COPY_SOURCE:
		if ((access & ~(ResourceAccessType::COPY_SOURCE)) != 0)
			return false;
		break;
	case ResourceLayout::LAYOUT_COPY_DEST:
		if ((access & ~(ResourceAccessType::COPY_DEST)) != 0)
			return false;
		break;
	}
	//TODO: other types
	return true;
}