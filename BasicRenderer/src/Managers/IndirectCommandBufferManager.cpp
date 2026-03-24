#include "Managers/IndirectCommandBufferManager.h"

#include <algorithm>

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/ResourceGroup.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/DynamicResource.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/PSOManager.h"
#include "../../generated/BuiltinResources.h"
#include "Resources/Components.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "Resources/Buffers/Buffer.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Render/MemoryIntrospectionAPI.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    m_indirectCommandsResourceGroup = std::make_shared<ResourceGroup>("IndirectCommandBuffers");
}

IndirectCommandBufferManager::~IndirectCommandBufferManager() {
    auto& deletion = DeletionManager::GetInstance();
    for (auto& [viewID, perView] : m_viewIDToBuffers) {
    }
}

void IndirectCommandBufferManager::RegisterWorkload(const DrawWorkloadKey& workloadKey) {
    EnsureWorkloadRegistered(workloadKey);

    auto& list = m_phaseToFlags[workloadKey.renderPhase];
    if (std::find(list.begin(), list.end(), workloadKey) == list.end()) {
        list.push_back(workloadKey);
    }
}

std::string GetDebugNameForTechnique(TechniqueDescriptor technique) {
    std::string result;
    if (technique.compileFlags & MaterialCompileBlend) result += "Blend|";
    if (technique.compileFlags & MaterialCompileAlphaTest) result += "AlphaTest|";
    if (technique.compileFlags & MaterialCompileDoubleSided) result += "DoubleSided|";
    if (result.empty()) result = "None";
    else result.pop_back(); // remove trailing '|'
    return result;
}

Components::IndirectCommandBuffers
IndirectCommandBufferManager::CreateBuffersForView(uint64_t viewID) {
    PerViewBuffers perView;

    // Create one buffer per workload with current capacity (may be 0 if not yet sized)
    for (auto const& [workloadKey, cap] : m_workloadToCapacity) {
        unsigned int size = cap;
        if (size == 0) continue; // not yet sized, will be created on first UpdateBuffersForFlags

        auto res = CreateIndexedStructuredBuffer(size, sizeof(DispatchMeshIndirectCommand), true, true);
        res->SetName(
            "IndirectCommandBuffer(flags=" + std::string(GetDebugNameForTechnique(TechniqueDescriptor { {}, workloadKey.compileFlags }))
            + ", phase=" + std::to_string(workloadKey.renderPhase.hash)
            + ", clodOnly=" + std::to_string(workloadKey.clodOnly ? 1 : 0)
            + ", view=" + std::to_string(viewID) + ")");
        rg::memory::SetResourceUsageHint(*res, "Indirect command buffers");
        auto dyn = std::make_shared<DynamicGloballyIndexedResource>(res);
        auto entity = dyn->GetECSEntity();

        // Provide ECS with a way back to the shared_ptr
        entity.set<Components::Resource>({ dyn });

        // Tag participation and kind
        entity.add<Components::ParticipatesInPass>(RendererECSManager::GetInstance().GetRenderPhaseEntity(workloadKey.renderPhase));
        entity.add<Components::IsIndirectArguments>();
        if (workloadKey.clodOnly) {
            entity.add<Components::CLodOnlyDrawWorkload>();
        }
        else {
            entity.add<Components::GeneralDrawWorkload>();
        }

        m_indirectCommandsResourceGroup->AddResource(dyn);
        perView.buffersByWorkload[workloadKey] = { dyn, 0 };

        // Set the workload count to the last known value for this workload
        auto itCount = m_workloadToLastCount.find(workloadKey);
        if (itCount != m_workloadToLastCount.end()) {
            perView.buffersByWorkload[workloadKey].count = itCount->second;
        }
    }

    // Meshlet culling buffers sized by total commands across all flags
    auto makeMeshlet = [&](const char* label) {
        auto r = CreateIndexedStructuredBuffer((std::max)(m_totalIndirectCommands, 1u), sizeof(DispatchIndirectCommand), true, true);
        r->SetName(std::string(label) + " (view=" + std::to_string(viewID) + ")");
        rg::memory::SetResourceUsageHint(*r, "Indirect command buffers");
        auto dyn = std::make_shared<DynamicGloballyIndexedResource>(r);
        dyn->GetECSEntity().set<Components::Resource>({ dyn });
        return dyn;    };
    perView.meshletCullingIndirectCommandBuffer = makeMeshlet("MeshletCullingIndirectCommandBuffer");
    perView.meshletCullingResetIndirectCommandBuffer = makeMeshlet("MeshletCullingResetIndirectCommandBuffer");

    // Store
    m_viewIDToBuffers[viewID] = perView;

    Components::IndirectCommandBuffers out{};
    out.meshletCullingIndirectCommandBuffer = m_viewIDToBuffers[viewID].meshletCullingIndirectCommandBuffer;
    out.meshletCullingResetIndirectCommandBuffer = m_viewIDToBuffers[viewID].meshletCullingResetIndirectCommandBuffer;

    return out;
}

void IndirectCommandBufferManager::UnregisterBuffers(uint64_t viewID) {
    auto it = m_viewIDToBuffers.find(viewID);
    if (it == m_viewIDToBuffers.end()) return;

    auto& perView = it->second;
    auto& deletion = DeletionManager::GetInstance();

    for (auto& [_, dyn] : perView.buffersByWorkload) {
        m_indirectCommandsResourceGroup->RemoveResource(dyn.buffer->GetResource().get());
    }

    m_viewIDToBuffers.erase(it);
}

void IndirectCommandBufferManager::UpdateBuffersForWorkload(const DrawWorkloadKey& workloadKey, unsigned int numDraws) {
    EnsureWorkloadRegistered(workloadKey);

    // Remember the last exact draw count for this workload
    m_workloadToLastCount[workloadKey] = numDraws;

    unsigned int newSize = RoundUp(numDraws);
    unsigned int& curr = m_workloadToCapacity[workloadKey];
    if (newSize <= curr) { // no grow, just update count
        for (auto& [viewID, perView] : m_viewIDToBuffers) {
            auto it = perView.buffersByWorkload.find(workloadKey);
            if (it != perView.buffersByWorkload.end()) {
                it->second.count = numDraws;
            }
            else {
                throw std::runtime_error("IndirectCommandBufferManager: missing buffer for workload on existing view");
            }
        }
        return;
    }

    curr = newSize;
    RecomputeTotal();

    auto& deletion = DeletionManager::GetInstance();

    // Grow this flags buffer for every view
    for (auto& [viewID, perView] : m_viewIDToBuffers) {
        auto it = perView.buffersByWorkload.find(workloadKey);
        if (it != perView.buffersByWorkload.end()) {
            // Replace existing
            auto res = CreateIndexedStructuredBuffer(curr, sizeof(DispatchMeshIndirectCommand), true, true);
            res->SetName(
                "IndirectCommandBuffer(flags=" + std::string(GetDebugNameForTechnique(TechniqueDescriptor { {}, workloadKey.compileFlags }))
                + ", phase=" + std::to_string(workloadKey.renderPhase.hash)
                + ", clodOnly=" + std::to_string(workloadKey.clodOnly ? 1 : 0)
                + ", view=" + std::to_string(viewID) + ")");
            rg::memory::SetResourceUsageHint(*res, "Indirect command buffers");
            it->second.buffer->SetResource(res);
            it->second.count = numDraws;
        }
        else {
            // Create new buffer for this view (this flags appeared after the view was created)
            auto res = CreateIndexedStructuredBuffer(curr, sizeof(DispatchMeshIndirectCommand), true, true);
            std::string techniqueName = GetDebugNameForTechnique(TechniqueDescriptor { {}, workloadKey.compileFlags });
            res->SetName("IndirectCommandBuffer(flags=" + techniqueName +
                ", phase=" + std::to_string(workloadKey.renderPhase.hash) +
                ", clodOnly=" + std::to_string(workloadKey.clodOnly ? 1 : 0) +
                ", view=" + std::to_string(viewID) + ")");
            rg::memory::SetResourceUsageHint(*res, "Indirect command buffers");
            auto dyn = std::make_shared<DynamicGloballyIndexedResource>(res);
            perView.buffersByWorkload.emplace(workloadKey, IndirectWorkload { dyn, 0 });
            m_indirectCommandsResourceGroup->AddResource(dyn);

            auto entity = dyn->GetECSEntity();
            entity.set<Components::Resource>({ dyn });
            entity.add<Components::ParticipatesInPass>(RendererECSManager::GetInstance().GetRenderPhaseEntity(workloadKey.renderPhase));
            entity.add<Components::IsIndirectArguments>();
            if (workloadKey.clodOnly) {
                entity.add<Components::CLodOnlyDrawWorkload>();
            }
            else {
                entity.add<Components::GeneralDrawWorkload>();
            }

            perView.buffersByWorkload[workloadKey].count = numDraws;
        }
    }

    // Meshlet buffers depend on total capacity; recreate both per view
    RecreateMeshletBuffersForAllViews();
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}

std::vector<std::pair<MaterialCompileFlags, IndirectWorkload>>
IndirectCommandBufferManager::GetBuffersForRenderPhase(uint64_t viewID, const RenderPhase& phase, bool clodOnly) const {
    std::vector<std::pair<MaterialCompileFlags, IndirectWorkload>> out;

    auto vIt = m_viewIDToBuffers.find(viewID);
    if (vIt == m_viewIDToBuffers.end()) return out;
    auto const& perView = vIt->second;

    for (auto const& [key, wl] : perView.buffersByWorkload) {
        if (key.renderPhase == phase && key.clodOnly == clodOnly) {
            out.emplace_back(key.compileFlags, wl);
        }
    }
    return out;
}

std::vector<IndirectBufferEntry>
IndirectCommandBufferManager::GetAllIndirectBuffers() const {
    std::vector<IndirectBufferEntry> out;
    size_t total = 0;
    for (auto const& [_, perView] : m_viewIDToBuffers) total += perView.buffersByWorkload.size();
    out.reserve(total);

    for (auto const& [viewID, perView] : m_viewIDToBuffers) {
        for (auto const& [key, wl] : perView.buffersByWorkload) {
            out.push_back(IndirectBufferEntry{ viewID, key, wl });
        }
    }
    return out;
}

std::vector<IndirectBufferEntry>
IndirectCommandBufferManager::GetIndirectBuffersForRenderPhase(const RenderPhase& phase, bool clodOnly) const {
    std::vector<IndirectBufferEntry> out;

    for (auto const& [viewID, perView] : m_viewIDToBuffers) {
        for (auto const& [key, wl] : perView.buffersByWorkload) {
            if (key.renderPhase == phase && key.clodOnly == clodOnly) {
                out.push_back(IndirectBufferEntry{ viewID, key, wl });
            }
        }
    }
    return out;
}

std::vector<IndirectBufferEntry>
IndirectCommandBufferManager::GetViewIndirectBuffersForRenderPhase(uint64_t viewID, const RenderPhase& phase, bool clodOnly) const {
    std::vector<IndirectBufferEntry> out;

    auto vit = m_viewIDToBuffers.find(viewID);
    if (vit == m_viewIDToBuffers.end()) return out;

    auto const& perView = vit->second;
    for (auto const& [key, wl] : perView.buffersByWorkload) {
        if (key.renderPhase == phase && key.clodOnly == clodOnly) {
            out.push_back(IndirectBufferEntry{ viewID, key, wl });
        }
    }
    return out;
}

// -------------------- IResourceProvider --------------------

std::shared_ptr<Resource> IndirectCommandBufferManager::ProvideResource(ResourceIdentifier const& key) {
    auto it = m_resources.find(key);
    if (it != m_resources.end()) return it->second;
    return nullptr;
}

std::vector<ResourceIdentifier> IndirectCommandBufferManager::GetSupportedKeys() {
    std::vector<ResourceIdentifier> keys;
    keys.reserve(m_resources.size());
    for (auto const& [key, _] : m_resources)
        keys.push_back(key);
    return keys;
}
std::vector<ResourceIdentifier> IndirectCommandBufferManager::GetSupportedResolverKeys() {
    std::vector<ResourceIdentifier> keys;
    keys.reserve(m_resolvers.size());
    for (auto const& [k, _] : m_resolvers)
        keys.push_back(k);
    return keys;
}
std::shared_ptr<IResourceResolver> IndirectCommandBufferManager::ProvideResolver(ResourceIdentifier const& key) {
    auto it = m_resolvers.find(key);
    if (it == m_resolvers.end()) return nullptr;
    return it->second;
}

// -------------------- helpers --------------------

void IndirectCommandBufferManager::RecomputeTotal() {
    unsigned int sum = 0;
    for (auto const& [_, cap] : m_workloadToCapacity) sum += cap;
    m_totalIndirectCommands = sum;
}

void IndirectCommandBufferManager::RecreateMeshletBuffersForAllViews() {
    auto& deletion = DeletionManager::GetInstance();
    for (auto& [viewID, perView] : m_viewIDToBuffers) {
        if (perView.meshletCullingIndirectCommandBuffer) {
        }
        if (perView.meshletCullingResetIndirectCommandBuffer) {
        }

        if (m_totalIndirectCommands == 0) {
            perView.meshletCullingIndirectCommandBuffer.reset();
            perView.meshletCullingResetIndirectCommandBuffer.reset();
            continue;
        }

        auto makeMeshlet = [&](const char* label) {
            auto r = CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), true, true);
            r->SetName(std::string(label) + " (view=" + std::to_string(viewID) + ")");
            rg::memory::SetResourceUsageHint(*r, "Indirect command buffers");
            auto dyn = std::make_shared<DynamicGloballyIndexedResource>(r);
            return dyn;
            };

        perView.meshletCullingIndirectCommandBuffer = makeMeshlet("MeshletCullingIndirectCommandBuffer");
        perView.meshletCullingResetIndirectCommandBuffer = makeMeshlet("MeshletCullingResetIndirectCommandBuffer");
    }
}

void IndirectCommandBufferManager::EnsurePerViewFlagsBuffers(uint64_t viewID) {
    auto it = m_viewIDToBuffers.find(viewID);
    if (it == m_viewIDToBuffers.end()) return;
    auto& perView = it->second;

    for (auto const& [workloadKey, cap] : m_workloadToCapacity) {
        if (cap == 0) continue;
        if (perView.buffersByWorkload.count(workloadKey)) continue;

        auto res = CreateIndexedStructuredBuffer(cap, sizeof(DispatchMeshIndirectCommand), true, true);
        res->SetName("IndirectCommandBuffer(flags=" + std::to_string(static_cast<uint64_t>(workloadKey.compileFlags)) +
            ", phase=" + std::to_string(workloadKey.renderPhase.hash) +
            ", clodOnly=" + std::to_string(workloadKey.clodOnly ? 1 : 0) +
            ", view=" + std::to_string(viewID) + ")");
        rg::memory::SetResourceUsageHint(*res, "Indirect command buffers");

        auto dyn = std::make_shared<DynamicGloballyIndexedResource>(res);
        perView.buffersByWorkload.emplace(workloadKey, IndirectWorkload { dyn, 0 });
        m_indirectCommandsResourceGroup->AddResource(dyn);

        auto entity = dyn->GetECSEntity();
        entity.set<Components::Resource>({ dyn });
        entity.add<Components::ParticipatesInPass>(RendererECSManager::GetInstance().GetRenderPhaseEntity(workloadKey.renderPhase));
        entity.add<Components::IsIndirectArguments>();
        if (workloadKey.clodOnly) {
            entity.add<Components::CLodOnlyDrawWorkload>();
        }
        else {
            entity.add<Components::GeneralDrawWorkload>();
        }

        // Initialize count from last known value
        auto itCount = m_workloadToLastCount.find(workloadKey);
        if (itCount != m_workloadToLastCount.end()) {
            perView.buffersByWorkload[workloadKey].count = itCount->second;
        }
    }
}

void IndirectCommandBufferManager::EnsureWorkloadRegistered(const DrawWorkloadKey& workloadKey) {
    if (!m_workloadToCapacity.count(workloadKey)) {
        m_workloadToCapacity[workloadKey] = 0;
    }
    if (!m_workloadToLastCount.count(workloadKey)) {
        m_workloadToLastCount[workloadKey] = 0;
    }
}
