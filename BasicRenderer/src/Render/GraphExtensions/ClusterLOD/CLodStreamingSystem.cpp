#include "Render/GraphExtensions/ClusterLOD/CLodStreamingSystem.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingBeginFramePass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingReadbackCopyPass.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "RenderPasses/StreamingUploadPass.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "BuiltinResources.h"

CLodStreamingSystem::CLodStreamingSystem() {
    m_streamingNonResidentBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingActiveGroupsBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingPinnedGroupsBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingResidencyInitializedBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingRequestsInProgressBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_pendingLoadPriorityByGroup.assign(m_streamingStorageGroupCapacity, 0u);
    m_groupUsesPinnedStorage.assign(m_streamingStorageGroupCapacity, 0u);
    m_streamingNonResidentBitsUploadPending = true;

    try {
        // TODO: Don't send the manager through settings, it's dumb.
        // We should probably have a way to pass user data to extensions
        auto getter = SettingsManager::GetInstance().getSettingGetter<std::function<MeshManager*()>>(CLodStreamingMeshManagerGetterSettingName);
        m_getMeshManager = getter();
    }
    catch (...) {
    }

    try {
        auto getFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight");
        m_streamingReadbackRingSize = std::max<uint32_t>(getFramesInFlight(), 1u);
    }
    catch (...) {
        m_streamingReadbackRingSize = 3u;
    }

    try {
        auto getBudget = SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodStreamingCpuUploadBudgetSettingName);
        m_streamingCpuUploadBudgetRequests = std::max(getBudget(), 1u);
    }
    catch (...) {
        m_streamingCpuUploadBudgetRequests = 10000u;
    }

    m_streamingNonResidentBits = CreateAliasedUnmaterializedStructuredBuffer(
        CLodBitsetWordCount(m_streamingStorageGroupCapacity),
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_streamingNonResidentBits->SetName("CLod Streaming NonResident Bits");

    m_streamingActiveGroupsBits = CreateAliasedUnmaterializedStructuredBuffer(
        CLodBitsetWordCount(m_streamingStorageGroupCapacity),
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_streamingActiveGroupsBits->SetName("CLod Streaming Active Groups Bits");

    m_streamingLoadRequests = CreateAliasedUnmaterializedStructuredBuffer(
        CLodStreamingRequestCapacity,
        sizeof(CLodStreamingRequest),
        true,
        false,
        false,
        false);
    m_streamingLoadRequests->SetName("CLod Streaming Load Requests");

    m_streamingLoadCounter = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_streamingLoadCounter->SetName("CLod Streaming Load Counter");

    m_streamingRuntimeState = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodStreamingRuntimeState), true, false, false, false);
    m_streamingRuntimeState->SetName("CLod Streaming Runtime State");

    m_usedGroupsCounter = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_usedGroupsCounter->SetName("CLod Used Groups Counter");

    m_usedGroupsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodUsedGroupsCapacity,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_usedGroupsBuffer->SetName("CLod Used Groups Buffer");

    // Self-managed readback pipeline
    {
        auto device = DeviceManager::GetInstance().GetDevice();
        auto result = device.CreateTimeline(m_streamingReadbackFencePtr, 0, "CLodStreamingReadbackFence");
        if (result == rhi::Result::Ok && m_streamingReadbackFencePtr) {
            m_streamingReadbackFenceHandle = m_streamingReadbackFencePtr.Get();
        }
    }

    const uint64_t counterStagingBytes = sizeof(uint32_t);
    const uint64_t requestsStagingBytes = static_cast<uint64_t>(CLodStreamingRequestCapacity) * sizeof(CLodStreamingRequest);
    const uint64_t usedGroupsCounterStagingBytes = sizeof(uint32_t);
    const uint64_t usedGroupsBufferStagingBytes = static_cast<uint64_t>(CLodUsedGroupsCapacity) * sizeof(uint32_t);
    m_readbackStagingSlots.resize(m_streamingReadbackRingSize);
    for (uint32_t i = 0; i < m_streamingReadbackRingSize; ++i) {
        auto& slot = m_readbackStagingSlots[i];
        slot.counterStaging = Buffer::CreateShared(rhi::HeapType::Readback, counterStagingBytes);
        slot.counterStaging->SetName(("CLodReadbackCounter_" + std::to_string(i)).c_str());
        slot.requestsStaging = Buffer::CreateShared(rhi::HeapType::Readback, requestsStagingBytes);
        slot.requestsStaging->SetName(("CLodReadbackRequests_" + std::to_string(i)).c_str());
        slot.usedGroupsCounterStaging = Buffer::CreateShared(rhi::HeapType::Readback, usedGroupsCounterStagingBytes);
        slot.usedGroupsCounterStaging->SetName(("CLodReadbackUsedGroupsCounter_" + std::to_string(i)).c_str());
        slot.usedGroupsBufferStaging = Buffer::CreateShared(rhi::HeapType::Readback, usedGroupsBufferStagingBytes);
        slot.usedGroupsBufferStaging->SetName(("CLodReadbackUsedGroupsBuffer_" + std::to_string(i)).c_str());
    }

    // Start the background streaming worker thread.
    m_streamingWorkerThread = std::thread(&CLodStreamingSystem::StreamingWorkerMain, this);
}

CLodStreamingSystem::~CLodStreamingSystem() {
    m_streamingWorkerQuit.store(true, std::memory_order_release);
    m_streamingWorkerCV.notify_all();
    if (m_streamingWorkerThread.joinable()) {
        m_streamingWorkerThread.join();
    }
}

void CLodStreamingSystem::OnRegistryReset(ResourceRegistry* reg) {
    (void)reg;

    MeshManager* meshManager = nullptr;
    if (m_getMeshManager) {
        meshManager = m_getMeshManager();
    }

    std::vector<uint32_t> ownedPinnedGroups;
    ownedPinnedGroups.reserve(m_groupOwnedPages.size());
    for (const auto& [groupIndex, _] : m_groupOwnedPages) {
        if (groupIndex < m_groupUsesPinnedStorage.size() && m_groupUsesPinnedStorage[groupIndex] != 0u) {
            ownedPinnedGroups.push_back(groupIndex);
        }
    }
    for (uint32_t groupIndex : ownedPinnedGroups) {
        ReleaseOwnedPagesForGroup(groupIndex, meshManager);
    }

    if (meshManager != nullptr) {
        for (const auto& [_, pages] : m_preAllocatedPagesByGroup) {
            ReleasePreAllocatedPages(pages, meshManager);
        }
    }

    m_pendingStreamingRequests.clear();
    m_pageLru.Clear();
    m_pageOwnerGroup.clear();
    m_pageOwnerSegment.clear();
    m_groupOwnedPages.clear();
    m_preAllocatedPagesByGroup.clear();
    std::fill(m_groupUsesPinnedStorage.begin(), m_groupUsesPinnedStorage.end(), 0u);
    m_pageLruInitialized = false;
    m_streamingResidentGroupsCount = 0u;
    std::fill(m_streamingNonResidentBitsCpu.begin(), m_streamingNonResidentBitsCpu.end(), 0u);
    std::fill(m_streamingActiveGroupsBitsCpu.begin(), m_streamingActiveGroupsBitsCpu.end(), 0u);
    std::fill(m_streamingPinnedGroupsBitsCpu.begin(), m_streamingPinnedGroupsBitsCpu.end(), 0u);
    std::fill(m_streamingResidencyInitializedBitsCpu.begin(), m_streamingResidencyInitializedBitsCpu.end(), 0u);
    std::fill(m_streamingRequestsInProgressBitsCpu.begin(), m_streamingRequestsInProgressBitsCpu.end(), 0u);
    std::fill(m_pendingLoadPriorityByGroup.begin(), m_pendingLoadPriorityByGroup.end(), 0u);
    m_streamingActiveGroupScanCount = 0u;
    m_streamingNonResidentBitsUploadPending = true;
    m_streamingDomainDirty = true;

    // Discard any stale decoded readback data from the worker thread.
    {
        std::lock_guard lock(m_streamingWorkerMutex);
        m_decodedReadbackBatch.clear();
        m_decodedUsedGroupsBatch.clear();
    }
}

void CLodStreamingSystem::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) {
    rg.RegisterResource(Builtin::CLod::StreamingNonResidentBits, m_streamingNonResidentBits);
    rg.RegisterResource(Builtin::CLod::StreamingActiveGroupsBits, m_streamingActiveGroupsBits);
    rg.RegisterResource(Builtin::CLod::StreamingLoadRequests, m_streamingLoadRequests);
    rg.RegisterResource(Builtin::CLod::StreamingLoadCounter, m_streamingLoadCounter);
    rg.RegisterResource(Builtin::CLod::StreamingRuntimeState, m_streamingRuntimeState);
    rg.RegisterResource(Builtin::CLod::StreamingTouchedGroupsCounter, m_usedGroupsCounter);
    rg.RegisterResource(Builtin::CLod::StreamingTouchedGroups, m_usedGroupsBuffer);

    RenderGraph::ExternalPassDesc streamingBeginPassDesc;
    streamingBeginPassDesc.type = RenderGraph::PassType::Compute;
    streamingBeginPassDesc.name = "CLod::StreamingBeginFramePass";
    streamingBeginPassDesc.where = RenderGraph::ExternalInsertPoint::After("SkinningPass");

    streamingBeginPassDesc.pass = std::make_shared<CLodStreamingBeginFramePass>(
        m_streamingLoadCounter,
        m_usedGroupsCounter,
        m_streamingNonResidentBits,
        m_streamingActiveGroupsBits,
        m_streamingRuntimeState,
        [this](std::vector<uint32_t>& outBits) {
            if (!m_streamingNonResidentBitsUploadPending) {
                return false;
            }

            outBits = m_streamingNonResidentBitsCpu;
            m_streamingNonResidentBitsUploadPending = false;
            return true;
        },
        [this](std::vector<uint32_t>& outBits, uint32_t& outActiveScanCount) {
            outBits = m_streamingActiveGroupsBitsCpu;
            outActiveScanCount = m_streamingActiveGroupScanCount;
        },
        [this]() {
            PollCompletedReadbackSlots();
        },
        [this]() {
            ProcessStreamingRequestsBudgeted();
        });
    outPasses.push_back(std::move(streamingBeginPassDesc));
}

void CLodStreamingSystem::GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) {
    MeshManager* meshManager = nullptr;
    if (m_getMeshManager) {
        meshManager = m_getMeshManager();
    }

    if (meshManager != nullptr && meshManager->ConsumeCLodStreamingStructureDirty()) {
        m_streamingDomainDirty = true;
    }

    RefreshStreamingActiveGroupDomain();

    auto streamingUploads = rg::runtime::ConsumeStreamingUploadsDispatch();
    if (!streamingUploads.empty()) {
        StreamingUploadInputs inputs;
        inputs.uploads = std::move(streamingUploads);

        // Use the persistent slab ResourceGroup from PagePool so the
        // render graph can schedule transitions. The group auto-tracks
        // new slabs via version bumps.
        MeshManager* mm = m_getMeshManager ? m_getMeshManager() : nullptr;
        if (mm) {
            if (PagePool* pool = mm->GetCLodPagePool()) {
                auto slabGroup = pool->GetSlabResourceGroup();
                // Also include the page table buffer in the group if not present.
                if (auto pt = pool->GetPageTableBuffer()) {
                    slabGroup->AddResource(pt); // no-op if already present
                }
                inputs.poolResolver = std::make_unique<ResourceGroupResolver>(slabGroup);
            }
        }

        RenderGraph::ExternalPassDesc uploadDesc{};
        uploadDesc.type = RenderGraph::PassType::Copy;
        uploadDesc.name = "CLod::StreamingUpload";
        uploadDesc.where = RenderGraph::ExternalInsertPoint::After("EvaluateMaterialGroupsPass");
        uploadDesc.copyQueueSelection = CopyQueueSelection::Copy;
        uploadDesc.pass = std::make_shared<StreamingUploadPass>(std::move(inputs));
        outPasses.push_back(std::move(uploadDesc));
    }

    // Schedule a readback copy pass to capture the load counter + load requests
    // produced by culling pass 2 into a staging buffer ring slot.
    if (m_streamingReadbackFenceHandle.IsValid() && !m_readbackStagingSlots.empty()) {
        uint32_t selectedSlot = UINT32_MAX;
        uint64_t fv = 0;
        {
            std::lock_guard lock(m_streamingWorkerMutex);
            // Find a slot that the worker has already processed (inFlight == false).
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_readbackStagingSlots.size()); ++i) {
                const uint32_t idx = (m_readbackStagingCursor + i) % static_cast<uint32_t>(m_readbackStagingSlots.size());
                if (!m_readbackStagingSlots[idx].inFlight) {
                    selectedSlot = idx;
                    break;
                }
            }

            if (selectedSlot != UINT32_MAX) {
                m_readbackStagingCursor = (selectedSlot + 1u) % static_cast<uint32_t>(m_readbackStagingSlots.size());
                fv = m_streamingReadbackFenceCounter.fetch_add(1, std::memory_order_relaxed) + 1;
                auto& slot = m_readbackStagingSlots[selectedSlot];
                slot.fenceValue = fv;
                slot.inFlight = true;
            }
        }

        if (selectedSlot != UINT32_MAX) {
            auto& slot = m_readbackStagingSlots[selectedSlot];

            CLodStreamingReadbackCopyInputs readbackInputs{};
            readbackInputs.counterSource = m_streamingLoadCounter;
            readbackInputs.requestsSource = m_streamingLoadRequests;
            readbackInputs.usedGroupsCounterSource = m_usedGroupsCounter;
            readbackInputs.usedGroupsBufferSource = m_usedGroupsBuffer;

            RenderGraph::ExternalPassDesc readbackDesc{};
            readbackDesc.type = RenderGraph::PassType::Copy;
            readbackDesc.name = "CLod::StreamingReadbackCopy";
            readbackDesc.where = RenderGraph::ExternalInsertPoint::After("EvaluateMaterialGroupsPass");
            readbackDesc.copyQueueSelection = CopyQueueSelection::Copy;
            readbackDesc.pass = std::make_shared<CLodStreamingReadbackCopyPass>(
                std::move(readbackInputs),
                slot.counterStaging,
                slot.requestsStaging,
                slot.usedGroupsCounterStaging,
                slot.usedGroupsBufferStaging,
                m_streamingReadbackFenceHandle,
                fv);
            outPasses.push_back(std::move(readbackDesc));

            // Wake the background worker so it can HostWait for the new fence value.
            m_streamingWorkerCV.notify_one();
        }
    }
}

uint32_t CLodStreamingSystem::BitWordAddress(uint32_t key) {
    return key >> 5u;
}

uint32_t CLodStreamingSystem::BitMask(uint32_t key) {
    return 1u << (key & 31u);
}

uint32_t CLodStreamingSystem::UnpackStreamingRequestPriority(const CLodStreamingRequest& req) {
    return (req.viewId >> 16u) & 0xFFFFu;
}

bool CLodStreamingSystem::IsGroupPinned(uint32_t groupIndex) const {
    const uint32_t wordAddress = BitWordAddress(groupIndex);
    if (wordAddress >= m_streamingPinnedGroupsBitsCpu.size()) {
        return false;
    }

    return (m_streamingPinnedGroupsBitsCpu[wordAddress] & BitMask(groupIndex)) != 0u;
}

bool CLodStreamingSystem::IsGroupActive(uint32_t groupIndex) const {
    const uint32_t wordAddress = BitWordAddress(groupIndex);
    if (wordAddress >= m_streamingActiveGroupsBitsCpu.size()) {
        return false;
    }

    return (m_streamingActiveGroupsBitsCpu[wordAddress] & BitMask(groupIndex)) != 0u;
}

bool CLodStreamingSystem::IsGroupResident(uint32_t groupIndex) const {
    const uint32_t wordAddress = BitWordAddress(groupIndex);
    if (wordAddress >= m_streamingNonResidentBitsCpu.size()) {
        return false;
    }

    return (m_streamingNonResidentBitsCpu[wordAddress] & BitMask(groupIndex)) == 0u;
}

bool CLodStreamingSystem::TryQueuePendingLoadRequest(const CLodStreamingRequest& req, uint32_t priority) {
    const uint32_t groupIndex = req.groupGlobalIndex;
    if (groupIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(groupIndex + 1u);
    }

    if (IsGroupResident(groupIndex)) {
        return false;
    }

    if (IsStreamingRequestInProgress(groupIndex)) {
        // Only update the priority tracker — do NOT push a duplicate entry.
        // The existing entry in m_pendingStreamingRequests (or in-flight I/O)
        // will pick up the elevated priority via m_pendingLoadPriorityByGroup.
        if (groupIndex < m_pendingLoadPriorityByGroup.size() && priority > m_pendingLoadPriorityByGroup[groupIndex]) {
            m_pendingLoadPriorityByGroup[groupIndex] = priority;
        }
        return false;
    }

    MarkStreamingRequestInProgress(groupIndex);
    m_pendingLoadPriorityByGroup[groupIndex] = priority;
    PendingStreamingRequest pending{};
    pending.request = req;
    pending.priority = priority;
    m_pendingStreamingRequests.push_back(pending);
    return true;
}

uint32_t CLodStreamingSystem::QueueLoadRequestWithParents(const CLodStreamingRequest& requestedLoad, uint32_t requestedPriority) {
    if (requestedLoad.groupGlobalIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(requestedLoad.groupGlobalIndex + 1u);
    }

    uint32_t queuedCount = 0u;
    std::vector<uint32_t> parentChain;
    uint32_t currentGroup = requestedLoad.groupGlobalIndex;
    const size_t maxHops = m_streamingParentGroupByGlobal.size();
    for (size_t hop = 0; hop < maxHops; ++hop) {
        if (currentGroup >= m_streamingParentGroupByGlobal.size()) {
            break;
        }

        const int32_t parent = m_streamingParentGroupByGlobal[currentGroup];
        if (parent < 0) {
            break;
        }

        const uint32_t parentGroup = static_cast<uint32_t>(parent);
        if (parentGroup == currentGroup) {
            break;
        }

        parentChain.push_back(parentGroup);
        currentGroup = parentGroup;
    }

    for (auto it = parentChain.rbegin(); it != parentChain.rend(); ++it) {
        const uint32_t parentGroup = *it;

        if (IsGroupResident(parentGroup)) {
            continue;
        }

        CLodStreamingRequest parentLoad = requestedLoad;
        parentLoad.groupGlobalIndex = parentGroup;
        const uint32_t parentPriority =
            (requestedPriority == std::numeric_limits<uint32_t>::max())
                ? requestedPriority
                : requestedPriority + 1u;
        if (TryQueuePendingLoadRequest(parentLoad, parentPriority)) {
            queuedCount++;
        }
    }

    if (TryQueuePendingLoadRequest(requestedLoad, requestedPriority)) {
        queuedCount++;
    }

    return queuedCount;
}

void CLodStreamingSystem::InitializePageLru(MeshManager* meshManager) {
    if (m_pageLruInitialized || !meshManager) return;

    auto* pool = meshManager->GetCLodPagePool();
    if (!pool) return;

    const uint32_t generalPages = pool->GetGeneralPageCount();
    const uint32_t totalPages = pool->GetTotalPageCount();
    if (generalPages == 0 || totalPages == 0) return;

    m_pageOwnerGroup.assign(totalPages, -1);
    m_pageOwnerSegment.resize(totalPages, 0u);

    for (uint32_t p = 0; p < generalPages; ++p) {
        m_pageLru.Insert(p);
    }

    m_pageLruInitialized = true;
    spdlog::info("CLodPageLRU initialized with {} general pages", generalPages);
}

void CLodStreamingSystem::EnsurePageTrackingCapacity(MeshManager* meshManager) {
    if (meshManager == nullptr) {
        return;
    }

    auto* pool = meshManager->GetCLodPagePool();
    if (pool == nullptr) {
        return;
    }

    const uint32_t totalPages = pool->GetTotalPageCount();
    if (m_pageOwnerGroup.size() < totalPages) {
        m_pageOwnerGroup.resize(totalPages, -1);
        m_pageOwnerSegment.resize(totalPages, 0u);
    }
}

void CLodStreamingSystem::ReleaseOwnedPagesForGroup(uint32_t groupIndex, MeshManager* meshManager) {
    auto it = m_groupOwnedPages.find(groupIndex);
    if (it == m_groupOwnedPages.end()) {
        if (groupIndex < m_groupUsesPinnedStorage.size()) {
            m_groupUsesPinnedStorage[groupIndex] = 0u;
        }
        return;
    }

    const bool usesPinnedStorage = groupIndex < m_groupUsesPinnedStorage.size() && m_groupUsesPinnedStorage[groupIndex] != 0u;
    std::vector<uint32_t> pinnedPagesToFree;
    if (usesPinnedStorage) {
        pinnedPagesToFree.reserve(it->second.size());
    }

    for (uint32_t page : it->second) {
        if (page == ~0u) {
            continue;
        }

        if (page < m_pageOwnerGroup.size()) {
            m_pageOwnerGroup[page] = -1;
            m_pageOwnerSegment[page] = 0u;
        }

        if (usesPinnedStorage) {
            pinnedPagesToFree.push_back(page);
        } else {
            m_pageLru.Insert(page);
        }
    }

    if (!pinnedPagesToFree.empty() && meshManager != nullptr) {
        if (PagePool* pool = meshManager->GetCLodPagePool()) {
            pool->FreePinnedPages(pinnedPagesToFree);
        }
    }

    m_groupOwnedPages.erase(it);
    if (groupIndex < m_groupUsesPinnedStorage.size()) {
        m_groupUsesPinnedStorage[groupIndex] = 0u;
    }
}

std::vector<uint32_t> CLodStreamingSystem::PopFreePages(uint32_t count, MeshManager* meshManager) {
    std::vector<uint32_t> pages;
    pages.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t page = m_pageLru.PopOldest();
        if (page == ~0u) break;

        int32_t ownerGroup = m_pageOwnerGroup[page];
        if (ownerGroup >= 0) {
            uint32_t g = static_cast<uint32_t>(ownerGroup);

            // Never evict pages belonging to groups the GPU reported as
            // visible last frame - doing so causes single-frame holes.
            const uint32_t wa = BitWordAddress(g);
            if (wa < m_usedGroupsBitsCpu.size() && (m_usedGroupsBitsCpu[wa] & BitMask(g)) != 0u) {
                // Push the page back to MRU and stop - we've hit the
                // on-screen frontier so no older pages are safe either.
                m_pageLru.Insert(page);
                break;
            }

            uint32_t seg = m_pageOwnerSegment[page];

            // Clear this segment's page in the group's ownership map.
            auto it = m_groupOwnedPages.find(g);
            if (it != m_groupOwnedPages.end() && seg < it->second.size()) {
                it->second[seg] = ~0u;
            }

            m_pageOwnerGroup[page] = -1;

            // If the group is currently resident, mark it non-resident.
            if (IsGroupResident(g)) {
                const uint32_t wordAddress = BitWordAddress(g);
                const uint32_t bitMask = BitMask(g);
                m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                m_streamingNonResidentBitsUploadPending = true;
                if (m_streamingResidentGroupsCount > 0u) {
                    m_streamingResidentGroupsCount--;
                }

                // Metadata-only eviction: clear group state in MeshManager
                // but don't free pages (they're managed by the page LRU now).
                if (meshManager) {
                    meshManager->FreeCLodGroupEviction(g);
                }
            }
        }

        pages.push_back(page);
    }

    return pages;
}

CLodStreamingSystem::PreAllocatedPages CLodStreamingSystem::PreAllocatePagesForGroup(
    uint32_t groupIndex, uint32_t segmentCount, MeshManager* meshManager) {
    PreAllocatedPages result;
    result.segmentCount = segmentCount;
    result.pagesBySegment.assign(segmentCount, ~0u);
    result.segmentNeedsFetch.assign(segmentCount, true);
    result.usesPinnedStorage = IsGroupPinned(groupIndex);

    EnsurePageTrackingCapacity(meshManager);

    uint32_t missingCount = 0;
    std::vector<uint32_t> reusedPages;

    // Check for still-valid pages from a previous partial eviction.
    auto ownedIt = m_groupOwnedPages.find(groupIndex);
    if (ownedIt != m_groupOwnedPages.end()) {
        const auto& owned = ownedIt->second;
        const bool ownedUsesPinnedStorage = groupIndex < m_groupUsesPinnedStorage.size() && m_groupUsesPinnedStorage[groupIndex] != 0u;
        for (uint32_t seg = 0; seg < segmentCount && seg < static_cast<uint32_t>(owned.size()); ++seg) {
            uint32_t existingPage = owned[seg];
            if (existingPage != ~0u && existingPage < m_pageOwnerGroup.size()
                && m_pageOwnerGroup[existingPage] == static_cast<int32_t>(groupIndex)
                && ownedUsesPinnedStorage == result.usesPinnedStorage) {
                // Page still has valid slab data — reuse it.
                if (!result.usesPinnedStorage) {
                    m_pageLru.Touch(existingPage);
                }
                result.pagesBySegment[seg] = existingPage;
                result.segmentNeedsFetch[seg] = false;
                reusedPages.push_back(existingPage);
            } else {
                missingCount++;
            }
        }
        // Handle case where segmentCount is larger than existing owned vector.
        if (segmentCount > static_cast<uint32_t>(owned.size())) {
            missingCount += segmentCount - static_cast<uint32_t>(owned.size());
        }
    } else {
        missingCount = segmentCount;
    }

    if (missingCount == 0) {
        return result;
    }

    std::vector<uint32_t> freshPages;
    if (result.usesPinnedStorage) {
        auto* pool = meshManager ? meshManager->GetCLodPagePool() : nullptr;
        if (pool == nullptr) {
            return PreAllocatedPages{};
        }

        freshPages = pool->AllocatePinnedPages(missingCount);
        EnsurePageTrackingCapacity(meshManager);
        if (freshPages.size() < missingCount) {
            pool->FreePinnedPages(freshPages);
            return PreAllocatedPages{};
        }
    } else {
        // Pop fresh pages for missing segments.
        freshPages = PopFreePages(missingCount, meshManager);
        if (freshPages.size() < missingCount) {
            // Not enough pages - release reused pages back to LRU and fail.
            for (uint32_t page : reusedPages) {
                m_pageLru.Insert(page);
            }
            for (uint32_t page : freshPages) {
                m_pageLru.Insert(page);
            }
            return PreAllocatedPages{}; // empty = failure
        }
    }

    // Assign fresh pages to missing segments.
    uint32_t freshIdx = 0;
    for (uint32_t seg = 0; seg < segmentCount; ++seg) {
        if (result.pagesBySegment[seg] == ~0u) {
            result.pagesBySegment[seg] = freshPages[freshIdx++];
            result.segmentNeedsFetch[seg] = true;
        }
    }

    return result;
}

void CLodStreamingSystem::AssignPagesToGroup(uint32_t groupIndex, const PreAllocatedPages& pages) {
    m_groupOwnedPages[groupIndex] = pages.pagesBySegment;
    if (groupIndex < m_groupUsesPinnedStorage.size()) {
        m_groupUsesPinnedStorage[groupIndex] = pages.usesPinnedStorage ? 1u : 0u;
    }

    for (uint32_t seg = 0; seg < pages.segmentCount; ++seg) {
        uint32_t page = pages.pagesBySegment[seg];
        if (page != ~0u && page < m_pageOwnerGroup.size()) {
            m_pageOwnerGroup[page] = static_cast<int32_t>(groupIndex);
            m_pageOwnerSegment[page] = seg;
            if (!pages.usesPinnedStorage) {
                m_pageLru.Touch(page);
            }
        }
    }
}

void CLodStreamingSystem::ReleasePreAllocatedPages(const PreAllocatedPages& pages, MeshManager* meshManager) {
    std::vector<uint32_t> pinnedPagesToFree;
    if (pages.usesPinnedStorage) {
        pinnedPagesToFree.reserve(pages.segmentCount);
    }

    for (uint32_t seg = 0; seg < pages.segmentCount; ++seg) {
        uint32_t page = pages.pagesBySegment[seg];
        if (page == ~0u) continue;

        if (pages.usesPinnedStorage) {
            if (page < m_pageOwnerGroup.size()) {
                m_pageOwnerGroup[page] = -1;
                m_pageOwnerSegment[page] = 0u;
            }
            pinnedPagesToFree.push_back(page);
            continue;
        }

        if (pages.segmentNeedsFetch[seg]) {
            // Fresh page that was never assigned — clear ownership and return to LRU.
            if (page < m_pageOwnerGroup.size()) {
                m_pageOwnerGroup[page] = -1;
                m_pageOwnerSegment[page] = 0u;
            }
        }
        // Both fresh and reused pages go back into the LRU.
        m_pageLru.Insert(page);
    }

    if (!pinnedPagesToFree.empty() && meshManager != nullptr) {
        if (PagePool* pool = meshManager->GetCLodPagePool()) {
            pool->FreePinnedPages(pinnedPagesToFree);
        }
    }
}

void CLodStreamingSystem::TouchGroupPages(uint32_t groupIndex) {
    auto it = m_groupOwnedPages.find(groupIndex);
    if (it != m_groupOwnedPages.end()) {
        for (uint32_t page : it->second) {
            if (page != ~0u) {
                m_pageLru.Touch(page);
            }
        }
    }

    // Walk parent chain.
    int32_t current = static_cast<int32_t>(groupIndex);
    for (size_t hop = 0; hop < m_streamingParentGroupByGlobal.size(); ++hop) {
        if (current < 0 || static_cast<uint32_t>(current) >= m_streamingParentGroupByGlobal.size()) break;
        int32_t parent = m_streamingParentGroupByGlobal[static_cast<uint32_t>(current)];
        if (parent < 0 || parent == current) break;

        auto pit = m_groupOwnedPages.find(static_cast<uint32_t>(parent));
        if (pit != m_groupOwnedPages.end()) {
            for (uint32_t page : pit->second) {
                if (page != ~0u) {
                    m_pageLru.Touch(page);
                }
            }
        }
        current = parent;
    }
}

void CLodStreamingSystem::EnsureStreamingStorageCapacity(uint32_t requiredGroupCount) {
    if (requiredGroupCount <= m_streamingStorageGroupCapacity) {
        return;
    }

    const uint32_t newCapacity = CLodRoundUpCapacity(requiredGroupCount);
    const uint32_t newWordCount = CLodBitsetWordCount(newCapacity);

    m_streamingNonResidentBits->ResizeStructured(newWordCount);
    m_streamingActiveGroupsBits->ResizeStructured(newWordCount);

    m_streamingNonResidentBitsCpu.resize(newWordCount, 0u);
    m_streamingActiveGroupsBitsCpu.resize(newWordCount, 0u);
    m_streamingPinnedGroupsBitsCpu.resize(newWordCount, 0u);
    m_streamingResidencyInitializedBitsCpu.resize(newWordCount, 0u);
    m_streamingRequestsInProgressBitsCpu.resize(newWordCount, 0u);
    m_usedGroupsBitsCpu.resize(newWordCount, 0u);
    m_pendingLoadPriorityByGroup.resize(newCapacity, 0u);
    m_streamingParentGroupByGlobal.resize(newCapacity, -1);
    m_groupUsesPinnedStorage.resize(newCapacity, 0u);
    m_streamingStorageGroupCapacity = newCapacity;

    m_streamingNonResidentBitsUploadPending = true;
}

void CLodStreamingSystem::RefreshStreamingActiveGroupDomain() {
    MeshManager* meshManager = nullptr;
    if (m_getMeshManager) {
        meshManager = m_getMeshManager();
    }

    if (meshManager == nullptr) {
        return;
    }

    InitializePageLru(meshManager);

    meshManager->ProcessCLodDiskStreamingIO(64u);
    ApplyDiskStreamingCompletions(meshManager);

    if (m_streamingDomainDirty) {
        m_streamingDomainDirty = false;

        std::fill(m_streamingActiveGroupsBitsCpu.begin(), m_streamingActiveGroupsBitsCpu.end(), 0u);
        std::fill(m_streamingPinnedGroupsBitsCpu.begin(), m_streamingPinnedGroupsBitsCpu.end(), 0u);
        m_streamingActiveGroupScanCount = 0u;

        meshManager->GetCLodStreamingDomainSnapshot(m_cachedDomainSnapshot);

        const uint32_t maxGroupIndex = m_cachedDomainSnapshot.maxGroupIndex;
        EnsureStreamingStorageCapacity(maxGroupIndex);
        std::fill(m_streamingParentGroupByGlobal.begin(), m_streamingParentGroupByGlobal.end(), -1);

        if (!m_cachedDomainSnapshot.parentGroupByGlobal.empty()) {
            const size_t copyCount = std::min(m_cachedDomainSnapshot.parentGroupByGlobal.size(), m_streamingParentGroupByGlobal.size());
            std::copy_n(m_cachedDomainSnapshot.parentGroupByGlobal.begin(), copyCount, m_streamingParentGroupByGlobal.begin());
        }

        m_streamingActiveGroupScanCount = maxGroupIndex;

        for (const auto& range : m_cachedDomainSnapshot.activeRanges) {
            const uint32_t rangeBegin = std::min(range.groupsBase, m_streamingStorageGroupCapacity);
            const uint32_t rangeEnd = std::min(range.groupsBase + range.groupCount, m_streamingStorageGroupCapacity);
            for (uint32_t groupIndex = rangeBegin; groupIndex < rangeEnd; ++groupIndex) {
                m_streamingActiveGroupsBitsCpu[BitWordAddress(groupIndex)] |= BitMask(groupIndex);
            }
        }

        for (const auto& range : m_cachedDomainSnapshot.coarsestRanges) {
            const uint32_t rangeBegin = std::min(range.groupsBase, m_streamingStorageGroupCapacity);
            const uint32_t rangeEnd = std::min(range.groupsBase + range.groupCount, m_streamingStorageGroupCapacity);
            for (uint32_t groupIndex = rangeBegin; groupIndex < rangeEnd; ++groupIndex) {
                const uint32_t wa = BitWordAddress(groupIndex);
                const uint32_t bm = BitMask(groupIndex);
                m_streamingPinnedGroupsBitsCpu[wa] |= bm;
            }
        }

        std::vector<uint32_t> groupsToRelease;
        groupsToRelease.reserve(m_groupOwnedPages.size());
        for (const auto& [groupIndex, _] : m_groupOwnedPages) {
            if (groupIndex >= m_groupUsesPinnedStorage.size() || m_groupUsesPinnedStorage[groupIndex] == 0u) {
                continue;
            }

            const uint32_t wa = BitWordAddress(groupIndex);
            const uint32_t mask = BitMask(groupIndex);
            const bool stillActive = wa < m_streamingActiveGroupsBitsCpu.size() && (m_streamingActiveGroupsBitsCpu[wa] & mask) != 0u;
            const bool stillPinned = wa < m_streamingPinnedGroupsBitsCpu.size() && (m_streamingPinnedGroupsBitsCpu[wa] & mask) != 0u;
            if (!stillActive || !stillPinned) {
                groupsToRelease.push_back(groupIndex);
            }
        }

        for (uint32_t groupIndex : groupsToRelease) {
            const uint32_t wa = BitWordAddress(groupIndex);
            const uint32_t mask = BitMask(groupIndex);
            if (IsGroupResident(groupIndex)) {
                meshManager->FreeCLodGroupEviction(groupIndex);
            }

            ReleaseOwnedPagesForGroup(groupIndex, meshManager);
            if (wa < m_streamingNonResidentBitsCpu.size()) {
                m_streamingNonResidentBitsCpu[wa] |= mask;
                m_streamingResidencyInitializedBitsCpu[wa] &= ~mask;
            }
            ClearStreamingRequestInProgress(groupIndex);
            if (groupIndex < m_pendingLoadPriorityByGroup.size()) {
                m_pendingLoadPriorityByGroup[groupIndex] = 0u;
            }
            m_streamingNonResidentBitsUploadPending = true;
        }

        std::vector<uint32_t> preAllocatedGroupsToRelease;
        preAllocatedGroupsToRelease.reserve(m_preAllocatedPagesByGroup.size());
        for (const auto& [groupIndex, pages] : m_preAllocatedPagesByGroup) {
            if (!pages.usesPinnedStorage) {
                continue;
            }

            const uint32_t wa = BitWordAddress(groupIndex);
            const uint32_t mask = BitMask(groupIndex);
            const bool stillActive = wa < m_streamingActiveGroupsBitsCpu.size() && (m_streamingActiveGroupsBitsCpu[wa] & mask) != 0u;
            const bool stillPinned = wa < m_streamingPinnedGroupsBitsCpu.size() && (m_streamingPinnedGroupsBitsCpu[wa] & mask) != 0u;
            if (!stillActive || !stillPinned) {
                preAllocatedGroupsToRelease.push_back(groupIndex);
            }
        }

        for (uint32_t groupIndex : preAllocatedGroupsToRelease) {
            auto it = m_preAllocatedPagesByGroup.find(groupIndex);
            if (it == m_preAllocatedPagesByGroup.end()) {
                continue;
            }

            ReleasePreAllocatedPages(it->second, meshManager);
            m_preAllocatedPagesByGroup.erase(it);

            const uint32_t wa = BitWordAddress(groupIndex);
            const uint32_t mask = BitMask(groupIndex);
            if (wa < m_streamingNonResidentBitsCpu.size()) {
                m_streamingNonResidentBitsCpu[wa] |= mask;
                m_streamingResidencyInitializedBitsCpu[wa] &= ~mask;
            }
            ClearStreamingRequestInProgress(groupIndex);
            if (groupIndex < m_pendingLoadPriorityByGroup.size()) {
                m_pendingLoadPriorityByGroup[groupIndex] = 0u;
            }
            m_streamingNonResidentBitsUploadPending = true;
        }
    }

    const uint32_t scanWordCount = CLodBitsetWordCount(m_streamingActiveGroupScanCount);
    for (uint32_t w = 0u; w < scanWordCount; ++w) {
        uint32_t pending = m_streamingActiveGroupsBitsCpu[w] & ~m_streamingResidencyInitializedBitsCpu[w];
        if (pending == 0u) {
            continue;
        }
        m_streamingResidencyInitializedBitsCpu[w] |= pending;
        const uint32_t pinnedWord = m_streamingPinnedGroupsBitsCpu[w];

        while (pending != 0u) {
            const int bit = std::countr_zero(pending);
            pending &= pending - 1u;
            const uint32_t groupIndex = (w << 5u) | static_cast<uint32_t>(bit);
            if (groupIndex >= m_streamingActiveGroupScanCount) {
                break;
            }
            const uint32_t bitMask = 1u << bit;
            const uint32_t wordAddress = w;
            const bool pinned = (pinnedWord & bitMask) != 0u;

            if (!pinned) {
                // Non-pinned groups: just mark as non-resident.
                m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                m_streamingNonResidentBitsUploadPending = true;
                continue;
            }

            // Pinned groups: allocate from dedicated non-evictable slabs.
            auto* pool = meshManager->GetCLodPagePool();
            const uint32_t pageSize = pool ? static_cast<uint32_t>(pool->GetPageSize()) : 0u;
            const auto info = meshManager->GetCLodGroupStreamingInfo(groupIndex);
            const uint32_t pagesNeeded = (info.valid && pageSize > 0)
                ? CLodEstimatePagesNeeded(info.hint, info.vertexByteSize, pageSize)
                : 1u;

            auto preAlloc = PreAllocatePagesForGroup(groupIndex, pagesNeeded, meshManager);
            if (preAlloc.segmentCount == 0) {
                // Dedicated pinned slab pool exhausted — can't load this pinned group yet.
                m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                m_streamingResidencyInitializedBitsCpu[wordAddress] &= ~bitMask;
                m_streamingNonResidentBitsUploadPending = true;
                continue;
            }

            m_preAllocatedPagesByGroup[groupIndex] = std::move(preAlloc);

            // Build fetch mask (all true for initial load — every segment needs data).
            auto paIt = m_preAllocatedPagesByGroup.find(groupIndex);
            std::vector<bool> segmentNeedsFetch;
            std::vector<uint32_t> preAllocPageIDs;
            if (paIt != m_preAllocatedPagesByGroup.end()) {
                const auto& pa = paIt->second;
                segmentNeedsFetch = pa.segmentNeedsFetch;
                preAllocPageIDs = pa.pagesBySegment;
            }

            const bool queued = meshManager->QueueCLodGroupDiskIO(groupIndex, segmentNeedsFetch, preAllocPageIDs);
            if (queued) {
                MarkStreamingRequestInProgress(groupIndex);
                m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
            } else {
                // Failed to queue - release pages.
                if (paIt != m_preAllocatedPagesByGroup.end()) {
                    ReleasePreAllocatedPages(paIt->second, meshManager);
                    m_preAllocatedPagesByGroup.erase(paIt);
                }
                m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                m_streamingResidencyInitializedBitsCpu[wordAddress] &= ~bitMask;
            }
            m_streamingNonResidentBitsUploadPending = true;
        }
    }

    uint32_t residentCount = 0u;
    for (uint32_t w = 0u; w < scanWordCount; ++w) {
        const uint32_t activeWord = m_streamingActiveGroupsBitsCpu[w];
        if (activeWord == 0u) {
            continue;
        }
        const uint32_t residentWord = activeWord & ~m_streamingNonResidentBitsCpu[w];
        residentCount += static_cast<uint32_t>(std::popcount(residentWord));
    }

    m_streamingResidentGroupsCount = residentCount;
}

bool CLodStreamingSystem::IsStreamingRequestInProgress(uint32_t groupIndex) const {
    const uint32_t wordAddress = BitWordAddress(groupIndex);
    if (wordAddress >= m_streamingRequestsInProgressBitsCpu.size()) {
        return false;
    }

    const uint32_t bitMask = BitMask(groupIndex);
    return (m_streamingRequestsInProgressBitsCpu[wordAddress] & bitMask) != 0u;
}

void CLodStreamingSystem::MarkStreamingRequestInProgress(uint32_t groupIndex) {
    const uint32_t wordAddress = BitWordAddress(groupIndex);
    const uint32_t bitMask = BitMask(groupIndex);
    m_streamingRequestsInProgressBitsCpu[wordAddress] |= bitMask;
}

void CLodStreamingSystem::ClearStreamingRequestInProgress(uint32_t groupIndex) {
    const uint32_t wordAddress = BitWordAddress(groupIndex);
    if (wordAddress >= m_streamingRequestsInProgressBitsCpu.size()) {
        return;
    }

    const uint32_t bitMask = BitMask(groupIndex);
    m_streamingRequestsInProgressBitsCpu[wordAddress] &= ~bitMask;
}

void CLodStreamingSystem::ApplyDiskStreamingCompletions(MeshManager* meshManager) {
    if (meshManager == nullptr) {
        return;
    }

    std::vector<MeshManager::CLodDiskStreamingCompletion> completions;
    meshManager->DrainCompletedCLodDiskStreamingGroups(completions);

    for (const auto& completion : completions) {
        const uint32_t groupIndex = completion.groupGlobalIndex;
        if (groupIndex >= m_streamingStorageGroupCapacity) {
            continue;
        }

        ClearStreamingRequestInProgress(groupIndex);
        if (groupIndex < m_pendingLoadPriorityByGroup.size()) {
            m_pendingLoadPriorityByGroup[groupIndex] = 0u;
        }

        auto preAllocIt = m_preAllocatedPagesByGroup.find(groupIndex);

        if (completion.success) {
            const uint32_t wordAddress = BitWordAddress(groupIndex);
            const uint32_t bitMask = BitMask(groupIndex);
            const bool wasNonResident = (m_streamingNonResidentBitsCpu[wordAddress] & bitMask) != 0u;
            m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
            if (wasNonResident) {
                m_streamingResidentGroupsCount++;
                m_streamingNonResidentBitsUploadPending = true;
            }

            if (preAllocIt != m_preAllocatedPagesByGroup.end()) {
                AssignPagesToGroup(groupIndex, preAllocIt->second);
                m_preAllocatedPagesByGroup.erase(preAllocIt);
            }
            TouchGroupPages(groupIndex);
        }
        else {
            if (preAllocIt != m_preAllocatedPagesByGroup.end()) {
                ReleasePreAllocatedPages(preAllocIt->second, meshManager);
                m_preAllocatedPagesByGroup.erase(preAllocIt);
            }

            const uint32_t wordAddress = BitWordAddress(groupIndex);
            const uint32_t bitMask = BitMask(groupIndex);
            if ((m_streamingPinnedGroupsBitsCpu[wordAddress] & bitMask) != 0u) {
                m_streamingResidencyInitializedBitsCpu[wordAddress] &= ~bitMask;
            }
        }
    }
}

void CLodStreamingSystem::PollCompletedReadbackSlots() {
    // Drain decoded (groupIndex, priority) pairs produced by the background worker thread.
    std::vector<std::pair<uint32_t, uint32_t>> batch;
    std::vector<uint32_t> usedGroupsBatch;
    {
        std::lock_guard lock(m_streamingWorkerMutex);
        batch.swap(m_decodedReadbackBatch);
        usedGroupsBatch.swap(m_decodedUsedGroupsBatch);
    }

    // Rebuild the used-groups bitset so PopFreePages can protect visible groups.
    // Only clear and rebuild when fresh data arrived — if the worker hasn't
    // produced new readback results this frame, keep the previous bitset so
    // visible groups remain protected across the readback latency gap.
    if (!usedGroupsBatch.empty()) {
        std::fill(m_usedGroupsBitsCpu.begin(), m_usedGroupsBitsCpu.end(), 0u);
        for (const uint32_t groupIndex : usedGroupsBatch) {
            const uint32_t wa = BitWordAddress(groupIndex);
            if (wa < m_usedGroupsBitsCpu.size()) {
                m_usedGroupsBitsCpu[wa] |= BitMask(groupIndex);
            }
        }
    }

    // Touch the page LRU for all GPU-reported visible groups and their parent chains.
    for (const uint32_t groupIndex : usedGroupsBatch) {
        TouchGroupPages(groupIndex);
    }

    if (batch.empty()) {
        return;
    }

    uint32_t queuedCount = 0;
    for (const auto& [groupIndex, priority] : batch) {
        CLodStreamingRequest req{};
        req.groupGlobalIndex = groupIndex;
        queuedCount += QueueLoadRequestWithParents(req, priority);
    }

    spdlog::info(
        "CLod streaming: drained {} decoded groups from worker, {} queued, {} LRU touches",
        static_cast<uint32_t>(batch.size()),
        queuedCount,
        static_cast<uint32_t>(usedGroupsBatch.size()));
}

void CLodStreamingSystem::StreamingWorkerMain() {
    uint64_t lastProcessed = 0;

    while (!m_streamingWorkerQuit.load(std::memory_order_relaxed)) {
        // Wait until there is a new fence value to process.
        {
            std::unique_lock lock(m_streamingWorkerMutex);
            m_streamingWorkerCV.wait(lock, [&] {
                return m_streamingWorkerQuit.load(std::memory_order_relaxed)
                    || m_streamingReadbackFenceCounter.load(std::memory_order_relaxed) > lastProcessed;
            });
        }
        if (m_streamingWorkerQuit.load(std::memory_order_relaxed)) break;

        const uint64_t target = m_streamingReadbackFenceCounter.load(std::memory_order_acquire);
        if (target <= lastProcessed) continue;

        // HostWait with a timeout so we can check for shutdown periodically.
        while (!m_streamingWorkerQuit.load(std::memory_order_relaxed)) {
            auto result = m_streamingReadbackFenceHandle.HostWait(target, 100);
            if (result != rhi::Result::WaitTimeout) break;
        }
        if (m_streamingWorkerQuit.load(std::memory_order_relaxed)) break;

        // Process all completed staging slots under the mutex.
        {
            std::lock_guard lock(m_streamingWorkerMutex);

            // Cross-slot deduplication: a single map/set for all slots in
            // this batch so the same group doesn't appear multiple times in
            // the output when it was requested across consecutive frames.
            std::unordered_map<uint32_t, uint32_t> batchMaxPriorityByGroup;
            std::unordered_set<uint32_t> batchUsedGroups;
            uint32_t totalRequestCount = 0;
            uint32_t totalUsedGroupsCount = 0;

            for (auto& slot : m_readbackStagingSlots) {
                if (!slot.inFlight || slot.fenceValue > target) {
                    continue;
                }

                // Map and read the load counter
                uint32_t requestCount = 0;
                {
                    auto apiResource = slot.counterStaging->GetAPIResource();
                    void* mapped = nullptr;
                    apiResource.Map(&mapped);
                    if (mapped) {
                        std::memcpy(&requestCount, mapped, sizeof(uint32_t));
                        apiResource.Unmap(0, 0);
                    }
                }
                requestCount = std::min<uint32_t>(requestCount, CLodStreamingRequestCapacity);

                if (requestCount > 0) {
                    // Map and read the load request array
                    std::vector<CLodStreamingRequest> requests(requestCount);
                    {
                        auto apiResource = slot.requestsStaging->GetAPIResource();
                        void* mapped = nullptr;
                        apiResource.Map(&mapped);
                        if (mapped) {
                            std::memcpy(requests.data(), mapped, requestCount * sizeof(CLodStreamingRequest));
                            apiResource.Unmap(0, 0);
                        }
                    }

                    totalRequestCount += requestCount;
                    for (uint32_t i = 0; i < requestCount; ++i) {
                        const uint32_t groupIndex = requests[i].groupGlobalIndex;
                        const uint32_t priority = UnpackStreamingRequestPriority(requests[i]);
                        auto it = batchMaxPriorityByGroup.find(groupIndex);
                        if (it == batchMaxPriorityByGroup.end()) {
                            batchMaxPriorityByGroup.emplace(groupIndex, priority);
                        } else {
                            it->second = std::max(it->second, priority);
                        }
                    }
                }

                // Read the used-groups append buffer (GPU-reported visible groups for LRU touch).
                uint32_t usedGroupsCount = 0;
                if (slot.usedGroupsCounterStaging) {
                    auto apiResource = slot.usedGroupsCounterStaging->GetAPIResource();
                    void* mapped = nullptr;
                    apiResource.Map(&mapped);
                    if (mapped) {
                        std::memcpy(&usedGroupsCount, mapped, sizeof(uint32_t));
                        apiResource.Unmap(0, 0);
                    }
                }
                usedGroupsCount = std::min<uint32_t>(usedGroupsCount, CLodUsedGroupsCapacity);

                if (usedGroupsCount > 0 && slot.usedGroupsBufferStaging) {
                    std::vector<uint32_t> usedGroups(usedGroupsCount);
                    {
                        auto apiResource = slot.usedGroupsBufferStaging->GetAPIResource();
                        void* mapped = nullptr;
                        apiResource.Map(&mapped);
                        if (mapped) {
                            std::memcpy(usedGroups.data(), mapped, usedGroupsCount * sizeof(uint32_t));
                            apiResource.Unmap(0, 0);
                        }
                    }

                    totalUsedGroupsCount += usedGroupsCount;
                    batchUsedGroups.insert(usedGroups.begin(), usedGroups.end());
                }

                slot.inFlight = false;
            }

            // Push cross-slot deduplicated results for the main thread.
            for (const auto& [groupIndex, priority] : batchMaxPriorityByGroup) {
                m_decodedReadbackBatch.emplace_back(groupIndex, priority);
            }
            for (const uint32_t g : batchUsedGroups) {
                m_decodedUsedGroupsBatch.push_back(g);
            }

            if (totalRequestCount > 0) {
                spdlog::info(
                    "CLod streaming worker: {} requests ({} unique groups) decoded",
                    totalRequestCount,
                    static_cast<uint32_t>(batchMaxPriorityByGroup.size()));
            }
            if (totalUsedGroupsCount > 0) {
                spdlog::info(
                    "CLod streaming worker: {} used groups ({} unique) decoded",
                    totalUsedGroupsCount,
                    static_cast<uint32_t>(batchUsedGroups.size()));
            }
        }

        lastProcessed = target;
    }
}

void CLodStreamingSystem::ProcessStreamingRequestsBudgeted() {
    const uint32_t budget = std::max(m_streamingCpuUploadBudgetRequests, 1u);
    CLodStreamingOperationStats frameStats{};

    MeshManager* meshManager = nullptr;
    if (m_getMeshManager) {
        meshManager = m_getMeshManager();
    }

    if (meshManager != nullptr) {
        InitializePageLru(meshManager);
        meshManager->ProcessCLodDiskStreamingIO(budget);
        ApplyDiskStreamingCompletions(meshManager);
    }

    const size_t n = m_pendingStreamingRequests.size();
    if (n > budget) {
        auto it = m_pendingStreamingRequests.begin() + static_cast<ptrdiff_t>(n - budget);
        std::nth_element(m_pendingStreamingRequests.begin(), it, m_pendingStreamingRequests.end(),
            [](const PendingStreamingRequest& a, const PendingStreamingRequest& b) {
                return a.priority < b.priority;
            });
        std::sort(it, m_pendingStreamingRequests.end(),
            [](const PendingStreamingRequest& a, const PendingStreamingRequest& b) {
                return a.priority < b.priority;
            });
    }
    else {
        std::sort(m_pendingStreamingRequests.begin(), m_pendingStreamingRequests.end(),
            [](const PendingStreamingRequest& a, const PendingStreamingRequest& b) {
                return a.priority < b.priority;
            });
    }

    auto* pool = meshManager ? meshManager->GetCLodPagePool() : nullptr;
    const uint64_t pageSize = pool ? pool->GetPageSize() : 0u;

    uint32_t processed = 0;
    while (processed < budget && !m_pendingStreamingRequests.empty()) {
        const PendingStreamingRequest pending = m_pendingStreamingRequests.back();
        m_pendingStreamingRequests.pop_back();

        const uint32_t groupIndex = pending.request.groupGlobalIndex;
        if (groupIndex >= m_streamingStorageGroupCapacity) {
            EnsureStreamingStorageCapacity(groupIndex + 1u);
        }

        if (groupIndex >= m_pendingLoadPriorityByGroup.size()) {
            processed++;
            continue;
        }

        // Load path
        frameStats.loadRequested++;
        frameStats.loadUnique++;

        // Skip groups that are no longer in the active domain (e.g. after a
        // streaming domain refresh removed them). Processing these would
        // wastefully evict pages for geometry that will never be rendered.
        if (!IsGroupActive(groupIndex)) {
            ClearStreamingRequestInProgress(groupIndex);
            m_pendingLoadPriorityByGroup[groupIndex] = 0u;
            processed++;
            continue;
        }

        if (IsGroupResident(groupIndex)) {
            TouchGroupPages(groupIndex);
            ClearStreamingRequestInProgress(groupIndex);
            m_pendingLoadPriorityByGroup[groupIndex] = 0u;
            processed++;
            continue;
        }

        if (meshManager == nullptr) {
            // No MeshManager — just mark resident in CPU bits.
            const uint32_t wordAddress = BitWordAddress(groupIndex);
            const uint32_t bitMask = BitMask(groupIndex);
            const uint32_t oldWord = m_streamingNonResidentBitsCpu[wordAddress];
            m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
            if (m_streamingNonResidentBitsCpu[wordAddress] != oldWord) {
                frameStats.loadApplied++;
                m_streamingResidentGroupsCount++;
                m_streamingNonResidentBitsUploadPending = true;
            }
            ClearStreamingRequestInProgress(groupIndex);
            m_pendingLoadPriorityByGroup[groupIndex] = 0u;
            processed++;
            continue;
        }

        // If disk I/O is already queued/in-flight for this group, skip.
        if (meshManager->IsCLodGroupDiskIOQueued(groupIndex)) {
            processed++;
            continue;
        }

        // If pages are already pre-allocated (from pinned-group init or a
        // previous frame), skip — overwriting the map entry would leak the
        // existing pages out of the LRU permanently.
        if (m_preAllocatedPagesByGroup.count(groupIndex)) {
            processed++;
            continue;
        }

        // Pre-allocate pages (popping from LRU, evicting as needed).
        if (pool && pageSize > 0) {
            const auto info = meshManager->GetCLodGroupStreamingInfo(groupIndex);
            const uint32_t pagesNeeded = info.valid
                ? CLodEstimatePagesNeeded(info.hint, info.vertexByteSize, pageSize)
                : 1u;

            auto preAlloc = PreAllocatePagesForGroup(groupIndex, pagesNeeded, meshManager);
            if (preAlloc.segmentCount == 0) {
                // LRU exhausted — can't allocate pages.
                frameStats.loadFailed++;
                ClearStreamingRequestInProgress(groupIndex);
                m_pendingLoadPriorityByGroup[groupIndex] = 0u;
                processed++;
                continue;
            }

            // Verify we got all the pages we asked for.  A short return
            // means PopFreePages hit used (on-screen) groups and stopped
            // early - we're exceeding the on-screen geometry budget.
            {
                uint32_t allocated = 0;
                for (uint32_t p : preAlloc.pagesBySegment) {
                    if (p != ~0u) allocated++;
                }
                if (allocated < pagesNeeded) {
                    spdlog::error(
                        "CLod streaming: on-screen geometry budget exceeded - "
                        "group {} needs {} pages but only {} available without "
                        "evicting visible groups. Dropping load request.",
                        groupIndex, pagesNeeded, allocated);
                    ReleasePreAllocatedPages(preAlloc, meshManager);
                    ClearStreamingRequestInProgress(groupIndex);
                    m_pendingLoadPriorityByGroup[groupIndex] = 0u;
                    frameStats.loadFailed++;
                    processed++;
                    continue;
                }
            }

            m_preAllocatedPagesByGroup[groupIndex] = std::move(preAlloc);
        }

        // Build segment fetch mask and page ID list from pre-allocated pages.
        std::vector<bool> segmentNeedsFetch;
        std::vector<uint32_t> preAllocPageIDs;
        auto paIt = m_preAllocatedPagesByGroup.find(groupIndex);
        if (paIt != m_preAllocatedPagesByGroup.end()) {
            const auto& pa = paIt->second;
            segmentNeedsFetch = pa.segmentNeedsFetch;
            preAllocPageIDs = pa.pagesBySegment;
        }

        // Queue disk I/O with the segment fetch mask and pre-allocated pages.
        const bool queued = meshManager->QueueCLodGroupDiskIO(groupIndex, segmentNeedsFetch, preAllocPageIDs);
        if (queued) {
            MarkStreamingRequestInProgress(groupIndex);
        }
        else {
            frameStats.loadFailed++;
            // Release pre-allocated pages on failure.
            if (paIt != m_preAllocatedPagesByGroup.end()) {
                ReleasePreAllocatedPages(paIt->second, meshManager);
                m_preAllocatedPagesByGroup.erase(paIt);
            }
            ClearStreamingRequestInProgress(groupIndex);
            m_pendingLoadPriorityByGroup[groupIndex] = 0u;
        }

        processed++;
    }

    if (meshManager != nullptr) {
        const auto debugStats = meshManager->GetCLodStreamingDebugStats();
        frameStats.residentGroups = debugStats.residentGroups;
        frameStats.residentAllocations = debugStats.residentAllocations;
        frameStats.queuedRequests = debugStats.queuedRequests;
        frameStats.completedResults = debugStats.completedResults;
        frameStats.residentAllocationBytes = debugStats.residentAllocationBytes;
        frameStats.completedResultBytes = debugStats.completedResultBytes;
        frameStats.streamedBytesThisFrame = debugStats.totalStreamedBytes - m_prevTotalStreamedBytes;
        m_prevTotalStreamedBytes = debugStats.totalStreamedBytes;
    }

    PublishCLodStreamingOperationStats(frameStats);
}
