#include "Managers/IndirectCommandBufferManager.h"

#include <algorithm>

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/ResourceGroup.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/DynamicResource.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "../../generated/BuiltinResources.h"
#include "Resources/Components.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    m_indirectCommandsResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
    m_meshletCullingCommandResourceGroup = std::make_shared<ResourceGroup>(L"MeshletCullingCommandBuffers");

    //m_resources[Builtin::IndirectCommandBuffers::Opaque] = m_indirectCommandsResourceGroup;
    //m_resources[Builtin::IndirectCommandBuffers::AlphaTest] = m_indirectCommandsResourceGroup;
    //m_resources[Builtin::IndirectCommandBuffers::Blend] = m_indirectCommandsResourceGroup;
    m_resources[Builtin::IndirectCommandBuffers::MeshletCulling] = m_meshletCullingCommandResourceGroup;
}

IndirectCommandBufferManager::~IndirectCommandBufferManager() {
    auto& deletion = DeletionManager::GetInstance();
    for (auto& [viewID, perView] : m_viewIDToBuffers) {
        for (auto& [flags, buf] : perView.buffersByFlags) {
            deletion.MarkForDelete(buf.buffer->GetResource()); // delay delete
        }
        if (perView.meshletCullingIndirectCommandBuffer)
            deletion.MarkForDelete(perView.meshletCullingIndirectCommandBuffer->GetResource());
        if (perView.meshletCullingResetIndirectCommandBuffer)
            deletion.MarkForDelete(perView.meshletCullingResetIndirectCommandBuffer->GetResource());
    }
}

void IndirectCommandBufferManager::RegisterTechnique(const TechniqueDescriptor& tech) {
    // Record flags capacity entry (starts at 0 until UpdateBuffersForFlags is called)
    EnsureTechniqueRegistered(tech);

    // Build inverted index: phase -> flags
    for (auto const& phase : tech.passes) {
        auto& list = m_phaseToFlags[phase];
        // Insert unique flags (avoid duplicates)
        if (std::find(list.begin(), list.end(), tech.compileFlags) == list.end()) {
            list.push_back(tech.compileFlags);
        }
    }
}

Components::IndirectCommandBuffers
IndirectCommandBufferManager::CreateBuffersForView(uint64_t viewID) {
    PerViewBuffers perView;

    // Create one buffer per flags value with current capacity (may be 0 if not yet sized)
    for (auto const& [technique, cap] : m_flagsToCapacity) {
        unsigned int size = cap;
        if (size == 0) continue; // not yet sized, will be created on first UpdateBuffersForFlags

        auto res = ResourceManager::GetInstance()
            .CreateIndexedStructuredBuffer(size, sizeof(DispatchMeshIndirectCommand), true, true);
        res->SetName(L"IndirectCommandBuffer(flags=" + std::to_wstring(static_cast<uint64_t>(technique.compileFlags)) +
            L", view=" + std::to_wstring(viewID) + L")");
        auto dyn = std::make_shared<DynamicGloballyIndexedResource>(res);
        auto entity = dyn->GetECSEntity();

        // Provide ECS with a way back to the shared_ptr
        entity.set<Components::Resource>({ dyn });

        // Tag participation and kind
        for (auto& pass : technique.passes) {
            entity.add<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(pass));
        }
        entity.add<Components::IsIndirectArguments>();

        m_indirectCommandsResourceGroup->AddResource(dyn);
        perView.buffersByFlags[technique.compileFlags] = { dyn, 0 };

        // Set the workload count to the last known value for this flags combo
        auto itCount = m_flagsToLastCount.find(technique);
        if (itCount != m_flagsToLastCount.end()) {
            perView.buffersByFlags[technique.compileFlags].count = itCount->second;
        }
    }

    // Meshlet culling buffers sized by total commands across all flags
    auto makeMeshlet = [&](const wchar_t* label) {
        auto r = ResourceManager::GetInstance()
            .CreateIndexedStructuredBuffer((std::max)(m_totalIndirectCommands, 1u), sizeof(DispatchIndirectCommand), true, true);
        r->SetName(std::wstring(label) + L" (view=" + std::to_wstring(viewID) + L")");
        auto dyn = std::make_shared<DynamicGloballyIndexedResource>(r);
        dyn->GetECSEntity().set<Components::Resource>({ dyn });
        return dyn;    };
    perView.meshletCullingIndirectCommandBuffer = makeMeshlet(L"MeshletCullingIndirectCommandBuffer");
    perView.meshletCullingResetIndirectCommandBuffer = makeMeshlet(L"MeshletCullingResetIndirectCommandBuffer");
    m_meshletCullingCommandResourceGroup->AddResource(perView.meshletCullingIndirectCommandBuffer);
    m_meshletCullingCommandResourceGroup->AddResource(perView.meshletCullingResetIndirectCommandBuffer);

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

    for (auto& [flags, dyn] : perView.buffersByFlags) {
        deletion.MarkForDelete(dyn.buffer->GetResource());
        m_indirectCommandsResourceGroup->RemoveResource(dyn.buffer->GetResource().get());
    }
    if (perView.meshletCullingIndirectCommandBuffer) {
        deletion.MarkForDelete(perView.meshletCullingIndirectCommandBuffer->GetResource());
        m_meshletCullingCommandResourceGroup->RemoveResource(perView.meshletCullingIndirectCommandBuffer->GetResource().get());
    }
    if (perView.meshletCullingResetIndirectCommandBuffer) {
        deletion.MarkForDelete(perView.meshletCullingResetIndirectCommandBuffer->GetResource());
        m_meshletCullingCommandResourceGroup->RemoveResource(perView.meshletCullingResetIndirectCommandBuffer->GetResource().get());
    }

    m_viewIDToBuffers.erase(it);
}

void IndirectCommandBufferManager::UpdateBuffersForTechnique(TechniqueDescriptor technique, unsigned int numDraws) {
    EnsureTechniqueRegistered(technique);

    // Remember the last exact draw count for this flags combo
    m_flagsToLastCount[technique] = numDraws;

    unsigned int newSize = RoundUp(numDraws);
    unsigned int& curr = m_flagsToCapacity[technique];
    if (newSize <= curr) { // no grow, just update count
        for (auto& [viewID, perView] : m_viewIDToBuffers) {
            auto it = perView.buffersByFlags.find(technique.compileFlags);
            if (it != perView.buffersByFlags.end()) {
                it->second.count = numDraws;
            }
            else {
                throw std::runtime_error("IndirectCommandBufferManager: missing buffer for flags on existing view");
            }
        }
        return;
    }

    curr = newSize;
    RecomputeTotal();

    auto& deletion = DeletionManager::GetInstance();

    // Grow this flags buffer for every view
    for (auto& [viewID, perView] : m_viewIDToBuffers) {
        auto it = perView.buffersByFlags.find(technique.compileFlags);
        if (it != perView.buffersByFlags.end()) {
            // Replace existing
            deletion.MarkForDelete(it->second.buffer->GetResource());
            auto res = ResourceManager::GetInstance()
                .CreateIndexedStructuredBuffer(curr, sizeof(DispatchMeshIndirectCommand), true, true);
            res->SetName(L"IndirectCommandBuffer(flags=" + std::to_wstring(static_cast<uint64_t>(technique.compileFlags)) +
                L", view=" + std::to_wstring(viewID) + L")");
            it->second.buffer->SetResource(res);
            it->second.count = numDraws;
        }
        else {
            // Create new buffer for this view (this flags appeared after the view was created)
            auto res = ResourceManager::GetInstance()
                .CreateIndexedStructuredBuffer(curr, sizeof(DispatchMeshIndirectCommand), true, true);
            res->SetName(L"IndirectCommandBuffer(flags=" + std::to_wstring(static_cast<uint64_t>(technique.compileFlags)) +
                L", view=" + std::to_wstring(viewID) + L")");
            auto dyn = std::make_shared<DynamicGloballyIndexedResource>(res);
            perView.buffersByFlags.emplace(technique.compileFlags, dyn);
            m_indirectCommandsResourceGroup->AddResource(dyn);

            auto entity = dyn->GetECSEntity();
            entity.set<Components::Resource>({ dyn }); // <-- important
            for (auto& pass : technique.passes) {
                entity.add<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(pass));
            }
            entity.add<Components::IsIndirectArguments>();

            perView.buffersByFlags[technique.compileFlags].count = numDraws;
        }
    }

    // Meshlet buffers depend on total capacity; recreate both per view
    RecreateMeshletBuffersForAllViews();
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}

std::vector<std::pair<MaterialCompileFlags, IndirectWorkload>>
IndirectCommandBufferManager::GetBuffersForRenderPhase(uint64_t viewID, const RenderPhase& phase) const {
    std::vector<std::pair<MaterialCompileFlags, IndirectWorkload>> out;

    auto vIt = m_viewIDToBuffers.find(viewID);
    if (vIt == m_viewIDToBuffers.end()) return out;
    auto const& perView = vIt->second;

    auto pIt = m_phaseToFlags.find(phase);
    if (pIt == m_phaseToFlags.end()) return out;

    out.reserve(pIt->second.size());
    for (auto flags : pIt->second) {
        auto fIt = perView.buffersByFlags.find(flags);
        if (fIt != perView.buffersByFlags.end()) {
            out.push_back(*fIt);
        }
    }
    return out;
}

std::vector<IndirectBufferEntry>
IndirectCommandBufferManager::GetAllIndirectBuffers() const {
    std::vector<IndirectBufferEntry> out;
    size_t total = 0;
    for (auto const& [_, perView] : m_viewIDToBuffers) total += perView.buffersByFlags.size();
    out.reserve(total);

    for (auto const& [viewID, perView] : m_viewIDToBuffers) {
        for (auto const& [flags, wl] : perView.buffersByFlags) {
            out.push_back(IndirectBufferEntry{ viewID, flags, wl });
        }
    }
    return out;
}

std::vector<IndirectBufferEntry>
IndirectCommandBufferManager::GetIndirectBuffersForRenderPhase(const RenderPhase& phase) const {
    std::vector<IndirectBufferEntry> out;

    auto pit = m_phaseToFlags.find(phase);
    if (pit == m_phaseToFlags.end()) return out;

    // Build a set for lookup of relevant flags
    std::unordered_set<MaterialCompileFlags, MaterialCompileFlagsHash> include(
        pit->second.begin(), pit->second.end());

    out.reserve(m_viewIDToBuffers.size() * include.size());

    for (auto const& [viewID, perView] : m_viewIDToBuffers) {
        for (auto const& [flags, wl] : perView.buffersByFlags) {
            if (include.count(flags)) {
                out.push_back(IndirectBufferEntry{ viewID, flags, wl });
            }
        }
    }
    return out;
}

std::vector<IndirectBufferEntry>
IndirectCommandBufferManager::GetViewIndirectBuffersForRenderPhase(uint64_t viewID, const RenderPhase& phase) const {
    std::vector<IndirectBufferEntry> out;

    auto vit = m_viewIDToBuffers.find(viewID);
    if (vit == m_viewIDToBuffers.end()) return out;

    auto pit = m_phaseToFlags.find(phase);
    if (pit == m_phaseToFlags.end()) return out;

    std::unordered_set<MaterialCompileFlags, MaterialCompileFlagsHash> include(
        pit->second.begin(), pit->second.end());

    auto const& perView = vit->second;
    for (auto const& [flags, wl] : perView.buffersByFlags) {
        if (include.count(flags)) {
            out.push_back(IndirectBufferEntry{ viewID, flags, wl });
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

// -------------------- helpers --------------------

void IndirectCommandBufferManager::RecomputeTotal() {
    unsigned int sum = 0;
    for (auto const& [flags, cap] : m_flagsToCapacity) sum += cap;
    m_totalIndirectCommands = sum;
}

void IndirectCommandBufferManager::RecreateMeshletBuffersForAllViews() {
    auto& deletion = DeletionManager::GetInstance();
    for (auto& [viewID, perView] : m_viewIDToBuffers) {
        if (perView.meshletCullingIndirectCommandBuffer) {
            deletion.MarkForDelete(perView.meshletCullingIndirectCommandBuffer->GetResource());
        }
        if (perView.meshletCullingResetIndirectCommandBuffer) {
            deletion.MarkForDelete(perView.meshletCullingResetIndirectCommandBuffer->GetResource());
        }

        if (m_totalIndirectCommands == 0) {
            perView.meshletCullingIndirectCommandBuffer.reset();
            perView.meshletCullingResetIndirectCommandBuffer.reset();
            continue;
        }

        auto makeMeshlet = [&](const wchar_t* label) {
            auto r = ResourceManager::GetInstance()
                .CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), true, true);
            r->SetName(std::wstring(label) + L" (view=" + std::to_wstring(viewID) + L")");
            auto dyn = std::make_shared<DynamicGloballyIndexedResource>(r);
            m_meshletCullingCommandResourceGroup->AddResource(dyn);
            return dyn;
            };

        perView.meshletCullingIndirectCommandBuffer = makeMeshlet(L"MeshletCullingIndirectCommandBuffer");
        perView.meshletCullingResetIndirectCommandBuffer = makeMeshlet(L"MeshletCullingResetIndirectCommandBuffer");
    }
}

void IndirectCommandBufferManager::EnsurePerViewFlagsBuffers(uint64_t viewID) {
    auto it = m_viewIDToBuffers.find(viewID);
    if (it == m_viewIDToBuffers.end()) return;
    auto& perView = it->second;

    for (auto const& [technique, cap] : m_flagsToCapacity) {
        if (cap == 0) continue;
        if (perView.buffersByFlags.count(technique.compileFlags)) continue;

        auto res = ResourceManager::GetInstance()
            .CreateIndexedStructuredBuffer(cap, sizeof(DispatchMeshIndirectCommand), true, true);
        res->SetName(L"IndirectCommandBuffer(flags=" + std::to_wstring(static_cast<uint64_t>(technique.compileFlags)) +
            L", view=" + std::to_wstring(viewID) + L")");

        auto dyn = std::make_shared<DynamicGloballyIndexedResource>(res);
        perView.buffersByFlags.emplace(technique.compileFlags, dyn);
        m_indirectCommandsResourceGroup->AddResource(dyn);

        auto entity = dyn->GetECSEntity();
        entity.set<Components::Resource>({ dyn });
        for (auto& pass : technique.passes) {
            entity.add<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(pass));
        }
        entity.add<Components::IsIndirectArguments>();

        // Initialize count from last known value
        auto itCount = m_flagsToLastCount.find(technique);
        if (itCount != m_flagsToLastCount.end()) {
            perView.buffersByFlags[technique.compileFlags].count = itCount->second;
        }
    }
}

void IndirectCommandBufferManager::EnsureTechniqueRegistered(TechniqueDescriptor technique) {
    if (!m_flagsToCapacity.count(technique)) {
        m_flagsToCapacity[technique] = 0;
    }
    if (!m_flagsToLastCount.count(technique)) {
        m_flagsToLastCount[technique] = 0;
    }
}
