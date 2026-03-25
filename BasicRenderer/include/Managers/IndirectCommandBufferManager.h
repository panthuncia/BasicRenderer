#pragma once
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <span>
#include <spdlog/spdlog.h>
#include <OpenRenderGraph/OpenRenderGraph.h>

#include "Scene/Components.h"
#include "Materials/TechniqueDescriptor.h"

class DynamicGloballyIndexedResource;
class ResourceGroup;
class Resource;
class Buffer;

struct RenderPhase; // forward
struct ResourceIdentifier;

// Hash for MaterialCompileFlags
struct MaterialCompileFlagsHash {
    size_t operator()(MaterialCompileFlags f) const noexcept {
        return std::hash<uint64_t>()(static_cast<uint64_t>(f));
    }
};

struct IndirectWorkload {
    std::shared_ptr<DynamicGloballyIndexedResource> buffer;
    unsigned int count = 0;
};

struct IndirectBufferEntry {
    uint64_t viewID;
    DrawWorkloadKey key;
    IndirectWorkload workload;
};

class IndirectCommandBufferManager : public IResourceProvider {
public:
    ~IndirectCommandBufferManager();

    static std::unique_ptr<IndirectCommandBufferManager> CreateUnique() {
        return std::unique_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
    }
    static std::shared_ptr<IndirectCommandBufferManager> CreateShared() {
        return std::shared_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
    }

    // Tell the manager about a draw workload once. This builds the inverted index:
    // RenderPhase -> [workloadKey].
    void RegisterWorkload(const DrawWorkloadKey& workloadKey);

    // Ensure we have buffers for all known flags combinations for this view.
    // Returns a light-weight struct mapping flags->buffers plus the meshlet pair.
    Components::IndirectCommandBuffers CreateBuffersForView(uint64_t viewID);

    // Remove buffers associated with a view
    void UnregisterBuffers(uint64_t viewID);

    // Update the buffer associated with the workload to accommodate numDraws.
    // Rounds up to increment size. Triggers per-view reallocation for that workload,
    // and resizes meshlet buffers (sum of all flags sizes).
    void UpdateBuffersForWorkload(const DrawWorkloadKey& workloadKey, unsigned int numDraws);

    // Set growth granularity
    void SetIncrementSize(unsigned int incrementSize);

    // Query: which (per-view) indirect command buffers participate in a render pass?
    // Order is unspecified; returns empty if none registered.
    std::vector<std::pair<MaterialCompileFlags, IndirectWorkload>>
        GetBuffersForRenderPhase(uint64_t viewID, const RenderPhase& phase, bool clodOnly = false) const;

    // Get every per-view indirect buffer (all views, all flags)
    std::vector<IndirectBufferEntry> GetAllIndirectBuffers() const;

    // Filtered: all buffers that participate in a phase (across all views)
    std::vector<IndirectBufferEntry> GetIndirectBuffersForRenderPhase(const RenderPhase& phase, bool clodOnly = false) const;

    // per-view version of phase query, but returning viewID too
    std::vector<IndirectBufferEntry> GetViewIndirectBuffersForRenderPhase(uint64_t viewID, const RenderPhase& phase, bool clodOnly = false) const;

	// Iterate over all indirect buffers (all views, all flags):
    template<class F>
    void ForEachIndirectBuffer(F&& f) const {
        for (auto const& [viewID, perView] : m_viewIDToBuffers) {
            for (auto const& [key, wl] : perView.buffersByWorkload) {
                std::forward<F>(f)(viewID, key, wl);
            }
        }
    }

    // Filtered across all views:
    template<class F>
    void ForEachIndirectBufferInPhase(const RenderPhase& phase, bool clodOnly, F&& f) const {
        for (auto const& [viewID, perView] : m_viewIDToBuffers) {
            for (auto const& [key, wl] : perView.buffersByWorkload) {
                if (key.renderPhase == phase && key.clodOnly == clodOnly) {
                    std::forward<F>(f)(viewID, key, wl);
                }
            }
        }
    }


    // ---- IResourceProvider ---------------------------------------------------
    std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
    std::vector<ResourceIdentifier> GetSupportedKeys() override;
    std::vector<ResourceIdentifier> GetSupportedResolverKeys() override;
    std::shared_ptr<IResourceResolver> ProvideResolver(ResourceIdentifier const& key) override;

private:
    IndirectCommandBufferManager();

    // Per-view buffer set
    struct PerViewBuffers {
        // One buffer per unique draw workload
        std::unordered_map<DrawWorkloadKey,
            IndirectWorkload,
            DrawWorkloadKey::Hasher> buffersByWorkload;

        std::shared_ptr<DynamicGloballyIndexedResource> meshletCullingIndirectCommandBuffer;
        std::shared_ptr<DynamicGloballyIndexedResource> meshletCullingResetIndirectCommandBuffer;
    };

    // RenderPhase -> list of draw workloads that participate in that phase (inverted index)
    std::unordered_map<RenderPhase,
        std::vector<DrawWorkloadKey>,
        RenderPhase::Hasher> m_phaseToFlags;

    // Per-workload current capacity (rounded to increment)
    std::unordered_map<DrawWorkloadKey, unsigned int, DrawWorkloadKey::Hasher> m_workloadToCapacity;

    // Per-workload last known draw count (unrounded)
    std::unordered_map<DrawWorkloadKey, unsigned int, DrawWorkloadKey::Hasher> m_workloadToLastCount;

    // Single group that owns all indirect command buffers (regardless of flags)
    std::shared_ptr<ResourceGroup> m_indirectCommandsResourceGroup;

    // Provider plumbing
    std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;

    // ViewID -> buffers
    std::unordered_map<uint64_t, PerViewBuffers> m_viewIDToBuffers;

    // Sum of capacities for all flags (used as size for meshlet buffers)
    unsigned int m_totalIndirectCommands = 0;

    // Growth granularity
    unsigned int m_incrementSize = 1000;

    std::unordered_map<ResourceIdentifier, std::shared_ptr<IResourceResolver>, ResourceIdentifier::Hasher> m_resolvers;

    // Helpers
    unsigned int RoundUp(unsigned int x) const {
        return ((x + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
    }
    void RecomputeTotal();
    void RecreateMeshletBuffersForAllViews();
    void EnsurePerViewFlagsBuffers(uint64_t viewID);
    void EnsureWorkloadRegistered(const DrawWorkloadKey& workloadKey);
};
