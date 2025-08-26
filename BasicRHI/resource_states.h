#pragma once

#include <cstdint>

namespace rhi {

	enum ResourceAccessType {
		None = 0,
		Common = 1,
		VertexBuffer = 1 << 1,
		ConstantBuffer = 1 << 2,
		IndexBuffer = 1 << 3,
		RenderTarget = 1 << 4,
		UnorderedAccess = 1 << 5,
		DepthReadWrite = 1 << 6,
		DepthRead = 1 << 7,
		ShaderResource = 1 << 8,
		IndirectArgument = 1 << 9,
		CopyDest = 1 << 10,
		CopySource = 1 << 11,
		RaytracingAccelerationStructureRead = 1 << 12,
		RaytracingAccelerationStructureWrite = 1 << 13,
	};

	inline ResourceAccessType operator|(ResourceAccessType a, ResourceAccessType b)
	{
		return static_cast<ResourceAccessType>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
	}

	enum class ResourceLayout {
		Undefined,
		Common,
		Present,
		GenericRead,
		RenderTarget,
		UnorderedAccess,
		DepthReadWrite,
		DepthRead,
		ShaderResource,
		CopySource,
		CopyDest,

		ResolveSource,
		ResolveDest,
		ShadingRateSource,

		DirectCommon,
		DirectGenericRead,
		DirectUnorderedAccess,
		DirectShaderResource,
		DirectCopySource,
		DirectCopyDest,

		ComputeCommon,
		ComputeGenericRead,
		ComputeUnorderedAccess,
		ComputeShaderResource,
		ComputeCopySource,
		ComputeCopyDest
	};

	inline bool ResourceLayoutIsUnorderedAccess(ResourceLayout layout) {
		return layout == ResourceLayout::UnorderedAccess ||
			layout == ResourceLayout::DirectUnorderedAccess ||
			layout == ResourceLayout::ComputeUnorderedAccess;
	}

	enum class ResourceSyncState {
		None,
		All,
		Draw,
		IndexInput,
		VertexShading,
		PixelShading,
		DepthStencil,
		RenderTarget,
		ComputeShading,
		Raytracing,
		Copy,
		Resolve,
		ExecuteIndirect,
		Predication,
		AllShading,
		NonPixelShading,
		EmitRaytracingAccelerationStructurePostbuildInfo,
		ClearUnorderedAccessView,
		VideoDecode,
		VideoProcess,
		VideoEncode,
		BuildRaytracingAccelerationStructure,
		CopyRatracingAccelerationStructure,
		SyncSplit,
	};

	inline unsigned int ResourceAccessGetNumReadStates(ResourceAccessType access) {
		int num = 0;
		if (access & ResourceAccessType::ShaderResource) num++;
		if (access & ResourceAccessType::DepthRead) num++;
		if (access & ResourceAccessType::RenderTarget) num++;
		if (access & ResourceAccessType::CopySource) num++;
		if (access & ResourceAccessType::IndexBuffer) num++;
		if (access & ResourceAccessType::VertexBuffer) num++;
		if (access & ResourceAccessType::ConstantBuffer) num++;

		return num;
	}

	inline ResourceLayout AccessToLayout(ResourceAccessType access, bool directQueue) {
		// most-specific first:
		if (access & ResourceAccessType::Common)
			return ResourceLayout::Common;
		if (access & ResourceAccessType::UnorderedAccess)
			return ResourceLayout::UnorderedAccess;
		if (access & ResourceAccessType::RenderTarget)
			return ResourceLayout::RenderTarget;
		if (access & ResourceAccessType::DepthReadWrite)
			return ResourceLayout::DepthReadWrite;
		if (access & ResourceAccessType::CopySource)
			return ResourceLayout::CopySource;
		if (access & ResourceAccessType::CopyDest)
			return ResourceLayout::CopyDest;

		auto num = ResourceAccessGetNumReadStates(access);
		if (num > 1) {
			if (directQueue) {
				return ResourceLayout::DirectGenericRead;
			}
			else {
				return ResourceLayout::ComputeGenericRead;
			}
		}
		else {
			if (access & ResourceAccessType::ShaderResource) {
				return ResourceLayout::ShaderResource;
			}
			if (access & ResourceAccessType::DepthRead) {
				return ResourceLayout::DepthRead;
			}
			if (access & ResourceAccessType::IndexBuffer) {
				return ResourceLayout::GenericRead;
			}
			if (access & ResourceAccessType::VertexBuffer) {
				return ResourceLayout::GenericRead;
			}
			if (access & ResourceAccessType::ConstantBuffer) {
				return ResourceLayout::GenericRead;
			}
		}

		return ResourceLayout::Common;
	}


	inline ResourceSyncState ComputeSyncFromAccess(ResourceAccessType access) {
		bool needsIndirect = (access & ResourceAccessType::IndirectArgument) != 0;

		if (needsIndirect) {
			return ResourceSyncState::ExecuteIndirect;
		}

		return ResourceSyncState::ComputeShading;
	}

	inline ResourceSyncState RenderSyncFromAccess(ResourceAccessType access)
	{
		// pick out each distinct sync category
		bool needsCommon = (access & ResourceAccessType::Common) != 0;
		bool needsShading = (access & (ResourceAccessType::VertexBuffer
			| ResourceAccessType::ConstantBuffer
			| ResourceAccessType::ShaderResource
			| ResourceAccessType::UnorderedAccess)) != 0;
		bool needsIndexInput = (access & ResourceAccessType::IndexBuffer) != 0;
		bool needsRenderTarget = (access & ResourceAccessType::RenderTarget) != 0;
		bool needsDepthStencil = (access & (ResourceAccessType::DepthRead
			| ResourceAccessType::DepthReadWrite)) != 0;
		bool needsCopy = (access & (ResourceAccessType::CopySource
			| ResourceAccessType::CopyDest)) != 0;
		bool needsIndirect = (access & ResourceAccessType::IndirectArgument) != 0;
		bool needsRayTracing = (access & ResourceAccessType::RaytracingAccelerationStructureRead) != 0;
		bool needsBuildAS = (access & ResourceAccessType::RaytracingAccelerationStructureWrite) != 0;

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
			return ResourceSyncState::None;
		// more than one category = full pipeline sync
		if (categoryCount > 1)
			return ResourceSyncState::All;

		// exactly one category = pick it
		if (needsCommon)        return ResourceSyncState::All;
		if (needsShading)       return ResourceSyncState::AllShading;
		if (needsIndexInput)    return ResourceSyncState::IndexInput;
		if (needsRenderTarget)  return ResourceSyncState::RenderTarget;
		if (needsDepthStencil)  return ResourceSyncState::DepthStencil;
		if (needsCopy)          return ResourceSyncState::Copy;
		if (needsIndirect)      return ResourceSyncState::ExecuteIndirect;
		if (needsBuildAS)       return ResourceSyncState::BuildRaytracingAccelerationStructure;
		if (needsRayTracing)    return ResourceSyncState::Raytracing;

		// (should never get here)
		return ResourceSyncState::All;
	}

	inline bool AccessTypeIsWriteType(ResourceAccessType access) {
		if (access & ResourceAccessType::RenderTarget) return true;
		if (access & ResourceAccessType::DepthReadWrite) return true;
		if (access & ResourceAccessType::CopyDest) return true;
		if (access & ResourceAccessType::UnorderedAccess) return true;
		if (access & ResourceAccessType::RaytracingAccelerationStructureWrite) return true;
		return false;
	}

	inline bool ValidateResourceLayoutAndAccessType(ResourceLayout layout, ResourceAccessType access) {
		if (access & ResourceAccessType::DepthRead && access & ResourceAccessType::DepthReadWrite) {
			return false;
		}
		switch (layout) {
		case ResourceLayout::Common:
			if ((access & ~(ResourceAccessType::ShaderResource | ResourceAccessType::CopyDest | ResourceAccessType::CopySource)) != 0)
				return false;
			break;
		case ResourceLayout::DirectCommon:
			if ((access & ~(ResourceAccessType::ShaderResource | ResourceAccessType::CopyDest | ResourceAccessType::CopySource | ResourceAccessType::UnorderedAccess)) != 0)
				return false;
			break;
		case ResourceLayout::ComputeCommon:
			if ((access & ~(ResourceAccessType::ShaderResource | ResourceAccessType::CopyDest | ResourceAccessType::CopySource | ResourceAccessType::UnorderedAccess)) != 0)
				return false;
			break;
		case ResourceLayout::GenericRead:
			if ((access & ~(ResourceAccessType::ShaderResource | ResourceAccessType::CopySource)) != 0)
				return false;
			break;
		case ResourceLayout::DirectGenericRead:
			if ((access & ~(ResourceAccessType::ShaderResource | ResourceAccessType::CopySource | ResourceAccessType::DepthRead /*| ResourceAccessType::SHADING_RATE_SOURCE | ResourceAccessType::RESOLVE_SOURCE*/)) != 0)
				return false;
			break;
		case ResourceLayout::ComputeGenericRead:
			if ((access & ~(ResourceAccessType::ShaderResource | ResourceAccessType::CopySource)) != 0)
				return false;
			break;
		case ResourceLayout::RenderTarget:
			if ((access & ~(ResourceAccessType::RenderTarget)) != 0)
				return false;
			break;
		case ResourceLayout::UnorderedAccess:
		case ResourceLayout::DirectUnorderedAccess:
		case ResourceLayout::ComputeUnorderedAccess:
			if ((access & ~(ResourceAccessType::UnorderedAccess)) != 0)
				return false;
			break;
		case ResourceLayout::DepthReadWrite:
			if ((access & ~(ResourceAccessType::DepthReadWrite | ResourceAccessType::DepthRead)) != 0)
				return false;
			break;
		case ResourceLayout::DepthRead:
			if ((access & ~(ResourceAccessType::DepthRead)) != 0)
				return false;
			break;
		case ResourceLayout::ShaderResource:
		case ResourceLayout::DirectShaderResource:
		case ResourceLayout::ComputeShaderResource:
			if ((access & ~(ResourceAccessType::ShaderResource)) != 0)
				return false;
			break;
		case ResourceLayout::CopySource:
		case ResourceLayout::DirectCopySource:
		case ResourceLayout::ComputeCopySource:
			if ((access & ~(ResourceAccessType::CopySource)) != 0)
				return false;
			break;
		case ResourceLayout::CopyDest:
			if ((access & ~(ResourceAccessType::CopyDest)) != 0)
				return false;
			break;
		}
		//TODO: other types
		return true;
	}

	inline bool ResourceSyncStateIsNotComputeSyncState(ResourceSyncState state) {
		switch (state) { // TODO: What states can the compute queue actually work with?
		case ResourceSyncState::None:
		case ResourceSyncState::All:
		case ResourceSyncState::ComputeShading:
			return false;
		}
		return true;
	}
}