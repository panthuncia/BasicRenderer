#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <flecs.h>

#include "Interfaces/IResourceProvider.h"
#include "Resources/ResourceIdentifier.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Scene/Components.h"
#include "ShaderBuffers.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Interfaces/IResourceResolver.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"

class IndirectCommandBufferManager;
class ResourceGroup;
class PixelBuffer;
class DynamicGloballyIndexedResource;

// Flags describing purpose/type of a view
struct ViewFlags {
    bool primaryCamera : 1;
    bool shadow : 1;
    bool reflection : 1;
    bool probe : 1;
    bool cascaded : 1;

    static ViewFlags PrimaryCamera() { return { true, false, false, false, false }; }
    static ViewFlags ShadowCascade(bool cascadedFlag = true) { return { false, true, false, false, cascadedFlag }; }
    static ViewFlags ShadowFace() { return { false, true, false, false, false }; }
    static ViewFlags Generic() { return { false, false, false, false, false }; }
};

// Optional creation customization
struct ViewCreationParams {
    // Provide existing depth resources if already created externally
    std::shared_ptr<PixelBuffer> depthMap;
    std::shared_ptr<PixelBuffer> linearDepthMap;

    // Link to ECS entity that owns this view (camera or light)
    uint64_t parentEntityID = 0;

    // Light meta
    Components::LightType lightType = Components::LightType::Directional;
    int cascadeIndex = -1;
};

struct ViewResources {
    std::shared_ptr<BufferView> cameraBufferView;
	std::shared_ptr<BufferView> cullingCameraBufferView;
    uint32_t cameraBufferIndex = 0;

    Components::IndirectCommandBuffers indirectCommandBuffers;

    std::shared_ptr<DynamicGloballyIndexedResource> meshletBitfieldBuffer;
    std::shared_ptr<DynamicGloballyIndexedResource> meshInstanceMeshletCullingBitfieldBuffer;
    std::shared_ptr<DynamicGloballyIndexedResource> meshInstanceOcclusionCullingBitfieldBuffer;

    std::shared_ptr<PixelBuffer> depthMap;
    std::shared_ptr<PixelBuffer> linearDepthMap;

    // Cached descriptor indices (filled after descriptor registration, optional)
    uint32_t meshletBitfieldSRVIndex = 0;
    uint32_t meshInstanceMeshletCullingBitfieldSRVIndex = 0;
    uint32_t meshInstanceOcclusionCullingBitfieldSRVIndex = 0;
};

struct View {
    uint64_t id = 0;
    CameraInfo cameraInfo{};
    ViewFlags flags = ViewFlags::Generic();

    Components::LightType lightType = Components::LightType::Directional;
    int cascadeIndex = -1;
    uint64_t parentEntityID = 0;

    ViewResources gpu;
};

// Filtering helper for iteration
struct ViewFilter {
    bool requirePrimary = false;
    bool requireShadow = false;
    bool requireCascade = false;
    bool requireLightType = false;
    Components::LightType lightType;

    bool Match(const View& v) const {
        if (requirePrimary && !v.flags.primaryCamera) return false;
        if (requireShadow && !v.flags.shadow) return false;
        if (requireCascade && !v.flags.cascaded) return false;
        if (requireLightType && v.lightType != lightType) return false;
        return true;
    }
};

// Optional callbacks
struct ViewEvents {
    std::function<void(const View&)> onCreated;
    std::function<void(uint64_t)>    onDestroyed;
    std::function<void(const View&)> onCameraUpdated;
    std::function<void(const View&)> onDepthAttached;
};

class ViewManager : public IResourceProvider {
public:
    static std::unique_ptr<ViewManager> CreateUnique() {
        return std::unique_ptr<ViewManager>(new ViewManager());
    }
    static std::shared_ptr<ViewManager> CreateShared() {
        return std::shared_ptr<ViewManager>(new ViewManager());
    }

    // Inject IndirectCommandBufferManager
    void SetIndirectCommandBufferManager(IndirectCommandBufferManager* manager);

    // Create a new view (camera or light), returns viewID
    uint64_t CreateView(const CameraInfo& cameraInfo,
        const ViewFlags& flags,
        const ViewCreationParams& params = {});

    // Destroy view and unregister indirect buffers
    void DestroyView(uint64_t viewID);

    // Attach (or replace) depth resources post creation
    void AttachDepth(uint64_t viewID,
        std::shared_ptr<PixelBuffer> depth,
        std::shared_ptr<PixelBuffer> linearDepth);

    // Update camera matrices/params
    void UpdateCamera(uint64_t viewID, const CameraInfo& cameraInfo);

    // Resize resources when global counts change
    void ResizeMeshletBitfields(uint64_t numMeshlets);
    void ResizeInstanceBitfields(uint32_t numInstances);

    // Access
    View* Get(uint64_t viewID);
    const View* Get(uint64_t viewID) const;

    // Iteration
    template<class F>
    void ForEachView(F&& f) {
        for (auto& [_, v] : m_views)
            std::forward<F>(f)(v);
    }

    template<class F>
    void ForEachFiltered(const ViewFilter& filter, F&& f) {
        for (auto& [_, v] : m_views)
            if (filter.Match(v))
                std::forward<F>(f)(v);
    }

    // Indirect workloads + full view data
    template<class F>
    void ForEachIndirectWorkload(F&& f) {
        if (!m_indirectManager) return;
        m_indirectManager->ForEachIndirectBuffer(
            [&](uint64_t viewID, MaterialCompileFlags flags, const IndirectWorkload& wl) {
                auto it = m_views.find(viewID);
                if (it == m_views.end()) return;
                std::forward<F>(f)(it->second, flags, wl);
            });
    }

    // Descriptor index baking (optional, for convenience)
    void BakeDescriptorIndices();

    // Events
    void SetEvents(ViewEvents events) { m_events = events; }

    // IResourceProvider
    std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
    std::vector<ResourceIdentifier> GetSupportedKeys() override;
    std::vector<ResourceIdentifier> GetSupportedResolverKeys() override;
    std::shared_ptr<IResourceResolver> ProvideResolver(ResourceIdentifier const& key) override;
private:
    ViewManager();

    // Helpers
    void AllocateBitfields(View& v);
    void ReplaceBitfield(std::shared_ptr<DynamicGloballyIndexedResource>& holder,
        uint64_t newElementCountBits,
        const wchar_t* labelFormat,
        uint64_t viewID);

    std::unordered_map<uint64_t, View> m_views;
    std::atomic<uint64_t> m_nextViewID{ 1 };


    // Core buffers/groups
    std::shared_ptr<LazyDynamicStructuredBuffer<CameraInfo>> m_cameraBuffer;
	std::shared_ptr<LazyDynamicStructuredBuffer<CullingCameraInfo>> m_cullingCameraBuffer;
    std::shared_ptr<ResourceGroup> m_meshletBitfieldGroup;
    std::shared_ptr<ResourceGroup> m_meshInstanceMeshletCullingBitfieldGroup;
    std::shared_ptr<ResourceGroup> m_meshInstanceOcclusionCullingBitfieldGroup;

    std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
    std::unordered_map<ResourceIdentifier, std::shared_ptr<IResourceResolver>, ResourceIdentifier::Hasher> m_resolvers;

    // Global sizing
    uint64_t m_currentMeshletBitfieldSizeBits = 1;
    uint32_t m_currentMeshInstanceBitfieldSizeBits = 1;

    IndirectCommandBufferManager* m_indirectManager = nullptr;

    std::mutex m_cameraUpdateMutex;
    ViewEvents m_events;
};