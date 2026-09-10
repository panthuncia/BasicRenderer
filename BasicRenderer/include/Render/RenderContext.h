#pragma once

#include <rhi.h>
#include <OpenRenderGraph/OpenRenderGraph.h>
#include <memory>
#include <vector>

#include "Scene/Components.h"
#include "Render/SceneFrameSnapshot.h"
#include "Render/PublishedRendererState.h"
#include "Render/RasterBucketFlags.h"
#include "Render/ObjectBufferStateArtifacts.h"
#include "Render/ViewStateArtifacts.h"
#include "Render/WindPaletteService.h"
#include "ShaderBuffers.h"

class Scene;
class ObjectManager;
class MeshManager;
class IndirectCommandBufferManager;
class ViewManager;
class LightManager;
class EnvironmentManager;
class MaterialManager;
class SkeletonManager;
namespace org { class PixelBuffer; }
using org::PixelBuffer;
template<class T> class DynamicStructuredBuffer;
class SortedUnsignedIntBuffer;

namespace br::render {
class CLodRayTracingSystem;
}

using PreparedViewFrameData = br::render::PreparedViewFrameData;

using PreparedActiveDrawEntry = br::render::PublishedActiveSkinnedPlacement;

struct RenderContext {
	std::shared_ptr<const br::render::PublishedRendererState> publishedRendererState;
	std::shared_ptr<const br::render::PublishedManifestLease> publishedManifestLease;
	Components::DrawStats drawStats;
	ObjectManager* objectManager;
	MeshManager* meshManager;
	IndirectCommandBufferManager* indirectCommandBufferManager;
	EnvironmentManager* environmentManager;
	MaterialManager* materialManager;
	br::render::CLodRayTracingSystem* clodRayTracingSystem = nullptr;
	// Owner-thread snapshot used by delayed typed/transitioning packets. A
	// recording worker must not enumerate the live ViewManager container.
	std::vector<PreparedViewFrameData> preparedViews;
	uint32_t preparedViewCameraBufferSize = 0;
	uint64_t preparedViewResourceLayoutRevision = 0;
	uint32_t preparedLightPagePoolSize = 0;
	uint32_t preparedRasterBucketCount = 0;
	std::vector<MaterialRasterFlags> preparedRasterBucketFlags;

    Scene* currentScene;
	Components::Camera primaryCamera;
	Components::DepthMap primaryDepthMap;
	uint64_t primaryViewID = 0;
	bool hasPrimaryCamera = false;
    rhi::DescriptorHeap textureDescriptorHeap;
	rhi::DescriptorHeap samplerDescriptorHeap;
	rhi::DescriptorHeap rtvHeap;
    UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
    UINT frameIndex;
	uint64_t frameNumber = 0;
	UINT64 frameFenceValue;
    DirectX::XMUINT2 renderResolution;
	DirectX::XMUINT2 outputResolution;
    unsigned int globalPSOFlags;
	bool rayTracedReflectionsEnabled = false;
	bool clodRayTracingSupported = false;
	float deltaTime;
	br::render::SceneOverlapStatus sceneOverlapStatus;
};

struct UpdateContext {
	std::shared_ptr<const br::render::PublishedRendererState> publishedRendererState;
	std::shared_ptr<const br::render::PublishedManifestLease> publishedManifestLease;
	Components::DrawStats drawStats;
	ObjectManager* objectManager = nullptr;
	MeshManager* meshManager = nullptr;
	IndirectCommandBufferManager* indirectCommandBufferManager = nullptr;
	EnvironmentManager* environmentManager = nullptr;
	MaterialManager* materialManager = nullptr;
	// Ordered ray-tracing build/trace service. The typed reflections pass captures
	// all resource and program ownership before invoking it on a recording worker.
	std::shared_ptr<br::render::CLodRayTracingSystem> clodRayTracingSystem;
	// Immutable logical-frame material-bucket snapshot. Pass Update/Prepare
	// must use this rather than racing the live manager between phases.
	uint32_t preparedRasterBucketCount = 0;
	std::vector<MaterialRasterFlags> preparedRasterBucketFlags;
	std::vector<PreparedViewFrameData> preparedViews;
	uint32_t preparedViewCameraBufferSize = 0;
	uint64_t preparedViewResourceLayoutRevision = 0;
	uint32_t preparedLightPagePoolSize = 0;
	br::render::IWindPaletteService* windPaletteService = nullptr;
	rhi::DescriptorHeap textureDescriptorHeap;
	rhi::DescriptorHeap samplerDescriptorHeap;
	rhi::DescriptorHeapHandle rtvHeap{};

	Scene* currentScene = nullptr;
	Components::Camera primaryCamera;
	uint64_t primaryViewID = 0;
	bool hasPrimaryCamera = false;
	UINT frameIndex = 0;
	UINT64 frameFenceValue = 0;
	uint64_t frameNumber = 0;
	DirectX::XMUINT2 renderResolution{};
	DirectX::XMUINT2 outputResolution{};
	unsigned int globalPSOFlags = 0;
	float deltaTime = 0.0f;
};
