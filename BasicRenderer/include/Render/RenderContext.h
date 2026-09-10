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

struct PreparedViewFrameData {
	uint64_t id = 0;
	uint32_t cameraBufferIndex = 0;
	bool primary = false;
	bool shadow = false;
	bool cascade = false;
	Components::LightType lightType = Components::LightType::Directional;
	std::shared_ptr<PixelBuffer> visibilityBuffer;
	std::shared_ptr<PixelBuffer> deepVisibilityHeadPointers;
	uint32_t visibilitySRVIndex = 0xFFFFFFFFu;
	uint32_t visibilityUAVIndex = 0xFFFFFFFFu;
	uint32_t deepVisibilityHeadPointersUAVIndex = 0xFFFFFFFFu;
};

using PreparedActiveDrawEntry = br::render::PublishedActiveSkinnedPlacement;

// Transitional object publication selected before graph update. CPU records
// are copied; resource identities are retained so declarations can freeze their
// exact backing versions for the accepted frame.
struct PreparedObjectFrameData {
	uint32_t residentTransformCount = 0;
	std::shared_ptr<DynamicStructuredBuffer<SkinnedAssemblyPlacementGPU>> skinnedPlacements;
	std::shared_ptr<SortedUnsignedIntBuffer> activeSkinnedPlacements;
	uint32_t activeSkinnedPlacementResidentSize = 0;
	std::shared_ptr<const std::vector<SkinnedAssemblyPlacementGPU>> placementRecords;
	std::shared_ptr<const std::vector<PreparedActiveDrawEntry>> activePlacementEntries;
};

struct RenderContext {
	std::shared_ptr<const br::render::PublishedRendererState> publishedRendererState;
	std::shared_ptr<const br::render::PublishedManifestLease> publishedManifestLease;
	Components::DrawStats drawStats;
	ObjectManager* objectManager;
	MeshManager* meshManager;
	IndirectCommandBufferManager* indirectCommandBufferManager;
	ViewManager* viewManager;
    LightManager* lightManager;
	EnvironmentManager* environmentManager;
	MaterialManager* materialManager;
	br::render::CLodRayTracingSystem* clodRayTracingSystem = nullptr;
	// Owner-thread snapshot used by delayed typed/transitioning packets. A
	// recording worker must not enumerate the live ViewManager container.
	std::vector<PreparedViewFrameData> preparedViews;
	uint32_t preparedViewCameraBufferSize = 0;
	uint64_t preparedViewResourceLayoutRevision = 0;
	PreparedObjectFrameData preparedObjects;
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
	ViewManager* viewManager = nullptr;
	LightManager* lightManager = nullptr;
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
	PreparedObjectFrameData preparedObjects;
	SkeletonManager* skeletonManager = nullptr;
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
