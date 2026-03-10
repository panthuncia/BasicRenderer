#include "Render/GraphExtensions/ClusterLOD/CLodStreamingSystem.h"

#include <algorithm>
#include <bit>
#include <cstring>

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
    m_streamingEvictionExemptBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingResidencyInitializedBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingLruCurrentStampByGroup.assign(m_streamingStorageGroupCapacity, 0ull);
    m_streamingRequestsInProgressBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_pendingLoadPriorityByGroup.assign(m_streamingStorageGroupCapacity, 0u);
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

    try {
        auto getResidentBudget = SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodStreamingResidentBudgetSettingName);
        m_streamingResidentBudgetGroups = std::max(getResidentBudget(), 1u);
    }
    catch (...) {
        m_streamingResidentBudgetGroups = std::numeric_limits<uint32_t>::max();
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

    m_streamingLoadRequestBits = CreateAliasedUnmaterializedStructuredBuffer(
        CLodBitsetWordCount(m_streamingStorageGroupCapacity),
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_streamingLoadRequestBits->SetName("CLod Streaming Load Request Bits");

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

    // ── Self-managed readback pipeline ─────────────────────────────────
    {
        auto device = DeviceManager::GetInstance().GetDevice();
        auto result = device.CreateTimeline(m_streamingReadbackFencePtr, 0, "CLodStreamingReadbackFence");
        if (result == rhi::Result::Ok && m_streamingReadbackFencePtr) {
            m_streamingReadbackFenceHandle = m_streamingReadbackFencePtr.Get();
        }
    }

    const uint64_t counterStagingBytes = sizeof(uint32_t);
    const uint64_t requestsStagingBytes = static_cast<uint64_t>(CLodStreamingRequestCapacity) * sizeof(CLodStreamingRequest);
    m_readbackStagingSlots.resize(m_streamingReadbackRingSize);
    for (uint32_t i = 0; i < m_streamingReadbackRingSize; ++i) {
        auto& slot = m_readbackStagingSlots[i];
        slot.counterStaging = Buffer::CreateShared(rhi::HeapType::Readback, counterStagingBytes);
        slot.counterStaging->SetName(("CLodReadbackCounter_" + std::to_string(i)).c_str());
        slot.requestsStaging = Buffer::CreateShared(rhi::HeapType::Readback, requestsStagingBytes);
        slot.requestsStaging->SetName(("CLodReadbackRequests_" + std::to_string(i)).c_str());
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
    m_pendingStreamingRequests.clear();
    m_streamingLruEntries.clear();
    m_streamingLruSerial = 0u;
    m_streamingResidentGroupsCount = 0u;
    std::fill(m_streamingNonResidentBitsCpu.begin(), m_streamingNonResidentBitsCpu.end(), 0u);
    std::fill(m_streamingActiveGroupsBitsCpu.begin(), m_streamingActiveGroupsBitsCpu.end(), 0u);
    std::fill(m_streamingPinnedGroupsBitsCpu.begin(), m_streamingPinnedGroupsBitsCpu.end(), 0u);
    std::fill(m_streamingEvictionExemptBitsCpu.begin(), m_streamingEvictionExemptBitsCpu.end(), 0u);
    std::fill(m_streamingResidencyInitializedBitsCpu.begin(), m_streamingResidencyInitializedBitsCpu.end(), 0u);
    std::fill(m_streamingLruCurrentStampByGroup.begin(), m_streamingLruCurrentStampByGroup.end(), 0ull);
    std::fill(m_streamingRequestsInProgressBitsCpu.begin(), m_streamingRequestsInProgressBitsCpu.end(), 0u);
    std::fill(m_pendingLoadPriorityByGroup.begin(), m_pendingLoadPriorityByGroup.end(), 0u);
    m_streamingActiveGroupScanCount = 0u;
    m_streamingNonResidentBitsUploadPending = true;
    m_streamingDomainDirty = true;

    // Discard any stale decoded readback data from the worker thread.
    {
        std::lock_guard lock(m_streamingWorkerMutex);
        m_decodedReadbackBatch.clear();
    }
}

void CLodStreamingSystem::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) {
    rg.RegisterResource(Builtin::CLod::StreamingNonResidentBits, m_streamingNonResidentBits);
    rg.RegisterResource(Builtin::CLod::StreamingActiveGroupsBits, m_streamingActiveGroupsBits);
    rg.RegisterResource(Builtin::CLod::StreamingLoadRequestBits, m_streamingLoadRequestBits);
    rg.RegisterResource(Builtin::CLod::StreamingLoadRequests, m_streamingLoadRequests);
    rg.RegisterResource(Builtin::CLod::StreamingLoadCounter, m_streamingLoadCounter);
    rg.RegisterResource(Builtin::CLod::StreamingRuntimeState, m_streamingRuntimeState);

    RenderGraph::ExternalPassDesc streamingBeginPassDesc;
    streamingBeginPassDesc.type = RenderGraph::PassType::Compute;
    streamingBeginPassDesc.name = "CLod::StreamingBeginFramePass";
    streamingBeginPassDesc.where = RenderGraph::ExternalInsertPoint::After("SkinningPass");

    streamingBeginPassDesc.pass = std::make_shared<CLodStreamingBeginFramePass>(
        m_streamingLoadCounter,
        m_streamingLoadRequestBits,
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
            return static_cast<uint32_t>(m_streamingNonResidentBitsCpu.size());
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

            RenderGraph::ExternalPassDesc readbackDesc{};
            readbackDesc.type = RenderGraph::PassType::Copy;
            readbackDesc.name = "CLod::StreamingReadbackCopy";
            readbackDesc.where = RenderGraph::ExternalInsertPoint::After("EvaluateMaterialGroupsPass");
            readbackDesc.copyQueueSelection = CopyQueueSelection::Copy;
            readbackDesc.pass = std::make_shared<CLodStreamingReadbackCopyPass>(
                std::move(readbackInputs),
                slot.counterStaging,
                slot.requestsStaging,
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

void CLodStreamingSystem::TouchStreamingLru(uint32_t groupIndex) {
    if (groupIndex >= m_streamingLruCurrentStampByGroup.size()) {
        return;
    }

    const uint64_t stamp = ++m_streamingLruSerial;
    m_streamingLruCurrentStampByGroup[groupIndex] = stamp;
    m_streamingLruEntries.push_back({ groupIndex, stamp });
}

bool CLodStreamingSystem::TryQueuePendingLoadRequest(const CLodStreamingRequest& req, uint32_t priority) {
    const uint32_t groupIndex = req.groupGlobalIndex;
    if (groupIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(groupIndex + 1u);
    }

    if (!IsGroupActive(groupIndex)) {
        return false;
    }

    if (IsGroupResident(groupIndex)) {
        return false;
    }

    if (IsStreamingRequestInProgress(groupIndex)) {
        if (groupIndex < m_pendingLoadPriorityByGroup.size() && priority > m_pendingLoadPriorityByGroup[groupIndex]) {
            m_pendingLoadPriorityByGroup[groupIndex] = priority;
            PendingStreamingRequest pending{};
            pending.isLoad = true;
            pending.request = req;
            pending.priority = priority;
            m_pendingStreamingRequests.push_back(pending);
        }
        return false;
    }

    MarkStreamingRequestInProgress(groupIndex);
    m_pendingLoadPriorityByGroup[groupIndex] = priority;
    PendingStreamingRequest pending{};
    pending.isLoad = true;
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
        if (!IsGroupActive(parentGroup)) {
            continue;
        }
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

bool CLodStreamingSystem::TryEvictLruVictim(uint32_t avoidGroup, CLodStreamingOperationStats& frameStats) {
    while (!m_streamingLruEntries.empty()) {
        const StreamingLruEntry entry = m_streamingLruEntries.front();
        m_streamingLruEntries.pop_front();

        if (entry.groupIndex >= m_streamingLruCurrentStampByGroup.size()) {
            continue;
        }
        if (m_streamingLruCurrentStampByGroup[entry.groupIndex] != entry.stamp) {
            continue;
        }
        if (entry.groupIndex == avoidGroup) {
            continue;
        }
        if (!IsGroupActive(entry.groupIndex)) {
            continue;
        }
        if (IsGroupPinned(entry.groupIndex)) {
            continue;
        }
        if (!IsGroupResident(entry.groupIndex)) {
            continue;
        }

        CLodStreamingRequest evictReq{};
        evictReq.groupGlobalIndex = entry.groupIndex;

        bool residencyBitChanged = false;
        const bool serviced = ApplyEvictionRequest(evictReq, residencyBitChanged);

        frameStats.unloadRequested++;
        frameStats.unloadUnique++;
        if (!serviced) {
            frameStats.unloadFailed++;
            continue;
        }

        if (residencyBitChanged) {
            frameStats.unloadApplied++;
            if (m_streamingResidentGroupsCount > 0u) {
                m_streamingResidentGroupsCount--;
            }
            return true;
        }
    }

    return false;
}

void CLodStreamingSystem::EnsureStreamingStorageCapacity(uint32_t requiredGroupCount) {
    if (requiredGroupCount <= m_streamingStorageGroupCapacity) {
        return;
    }

    const uint32_t newCapacity = CLodRoundUpCapacity(requiredGroupCount);
    const uint32_t newWordCount = CLodBitsetWordCount(newCapacity);

    m_streamingNonResidentBits->ResizeStructured(newWordCount);
    m_streamingActiveGroupsBits->ResizeStructured(newWordCount);
    m_streamingLoadRequestBits->ResizeStructured(newWordCount);

    m_streamingNonResidentBitsCpu.resize(newWordCount, 0u);
    m_streamingActiveGroupsBitsCpu.resize(newWordCount, 0u);
    m_streamingPinnedGroupsBitsCpu.resize(newWordCount, 0u);
    m_streamingEvictionExemptBitsCpu.resize(newWordCount, 0u);
    m_streamingResidencyInitializedBitsCpu.resize(newWordCount, 0u);
    m_streamingRequestsInProgressBitsCpu.resize(newWordCount, 0u);
    m_pendingLoadPriorityByGroup.resize(newCapacity, 0u);
    m_streamingLruCurrentStampByGroup.resize(newCapacity, 0ull);
    m_streamingParentGroupByGlobal.resize(newCapacity, -1);
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

    meshManager->ProcessCLodDiskStreamingIO();
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
                m_streamingEvictionExemptBitsCpu[wa] |= bm;
            }
        }
    }

    m_initRequests.clear();
    m_initRequestWordAddresses.clear();
    m_initRequestBitMasks.clear();
    m_initRequestPinnedFlags.clear();

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
            const bool pinned = (pinnedWord & bitMask) != 0u;

            MeshManager::CLodGlobalResidencyRequest initRequest{};
            initRequest.groupGlobalIndex = groupIndex;
            initRequest.resident = pinned;
            m_initRequests.push_back(initRequest);
            m_initRequestWordAddresses.push_back(w);
            m_initRequestBitMasks.push_back(bitMask);
            m_initRequestPinnedFlags.push_back(pinned ? 1u : 0u);
        }
    }

    if (!m_initRequests.empty()) {
        std::vector<MeshManager::CLodGlobalResidencyResult> results;
        meshManager->SetCLodGroupResidencyForGlobalBatchEx(m_initRequests, results);

        const size_t resultCount = std::min(results.size(), m_initRequests.size());
        for (size_t i = 0; i < resultCount; ++i) {
            const uint32_t wordAddress = m_initRequestWordAddresses[i];
            const uint32_t bitMask = m_initRequestBitMasks[i];
            const bool pinned = m_initRequestPinnedFlags[i] != 0u;
            const auto& result = results[i];

            if (pinned) {
                if (!result.applied) {
                    m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                    if (!result.queued) {
                        m_streamingResidencyInitializedBitsCpu[wordAddress] &= ~bitMask;
                    }
                }
                else {
                    m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
                }
            }
            else {
                m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
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

bool CLodStreamingSystem::ApplyEvictionRequest(const CLodStreamingRequest& req, bool& outResidencyBitChanged) {
    outResidencyBitChanged = false;

    const uint32_t groupIndex = req.groupGlobalIndex;
    if (groupIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(groupIndex + 1u);
    }

    const uint32_t wordAddress = BitWordAddress(groupIndex);
    const uint32_t bitMask = BitMask(groupIndex);
    const uint32_t oldWord = m_streamingNonResidentBitsCpu[wordAddress];

    MeshManager* meshManager = nullptr;
    if (m_getMeshManager) {
        meshManager = m_getMeshManager();
    }

    bool serviced = false;
    if (meshManager == nullptr) {
        serviced = true;
    }
    else {
        const auto result = meshManager->SetCLodGroupResidencyForGlobalEx(req.groupGlobalIndex, false);
        serviced = result.serviced;
    }

    if (!serviced) {
        return false;
    }

    m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;

    outResidencyBitChanged = (m_streamingNonResidentBitsCpu[wordAddress] != oldWord);
    if (outResidencyBitChanged) {
        m_streamingNonResidentBitsUploadPending = true;
    }

    return true;
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

        if (completion.success) {
            const uint32_t wordAddress = BitWordAddress(groupIndex);
            const uint32_t bitMask = BitMask(groupIndex);
            const bool wasNonResident = (m_streamingNonResidentBitsCpu[wordAddress] & bitMask) != 0u;
            m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
            if (wasNonResident) {
                m_streamingResidentGroupsCount++;
                m_streamingNonResidentBitsUploadPending = true;
            }
            TouchStreamingLru(groupIndex);
        }
        else {
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
    {
        std::lock_guard lock(m_streamingWorkerMutex);
        batch.swap(m_decodedReadbackBatch);
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
        "CLod streaming: drained {} decoded groups from worker, {} queued",
        static_cast<uint32_t>(batch.size()),
        queuedCount);
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

            for (auto& slot : m_readbackStagingSlots) {
                if (!slot.inFlight || slot.fenceValue > target) {
                    continue;
                }

                // ── Map and read the load counter ──────────────────────
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
                    // ── Map and read the load request array ───────────
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

                    // ── Deduplicate by group, keeping max priority ────
                    std::unordered_map<uint32_t, uint32_t> maxPriorityByGroup;
                    maxPriorityByGroup.reserve(requestCount);
                    for (uint32_t i = 0; i < requestCount; ++i) {
                        const uint32_t groupIndex = requests[i].groupGlobalIndex;
                        const uint32_t priority = UnpackStreamingRequestPriority(requests[i]);
                        auto it = maxPriorityByGroup.find(groupIndex);
                        if (it == maxPriorityByGroup.end()) {
                            maxPriorityByGroup.emplace(groupIndex, priority);
                        } else {
                            it->second = std::max(it->second, priority);
                        }
                    }

                    // ── Push decoded pairs for the main thread ────────
                    for (const auto& [groupIndex, priority] : maxPriorityByGroup) {
                        m_decodedReadbackBatch.emplace_back(groupIndex, priority);
                    }

                    spdlog::info(
                        "CLod streaming worker: {} requests ({} unique groups) decoded",
                        requestCount,
                        static_cast<uint32_t>(maxPriorityByGroup.size()));
                }

                slot.inFlight = false;
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
        meshManager->ProcessCLodDiskStreamingIO(budget);
        ApplyDiskStreamingCompletions(meshManager);

        // If disk streaming results failed because the page pool ran out of
        // pages, evict LRU victims to free pages and then retry.
        while (meshManager->GetPageExhaustedResultCount() > 0u) {
            const bool evicted = TryEvictLruVictim(/*avoidGroup=*/UINT32_MAX, frameStats);
            if (!evicted) {
                spdlog::warn("CLod streaming: {} results pending due to page exhaustion but no LRU victims to evict",
                             meshManager->GetPageExhaustedResultCount());
                break;
            }

            const uint32_t retried = meshManager->RetryPageExhaustedResults(1u);
            // Drain completions produced by the retry so residency bits are updated.
            ApplyDiskStreamingCompletions(meshManager);
            if (retried == 0u) {
                // Eviction freed some pages but still not enough for the next
                // result — continue evicting.
                continue;
            }
        }
    }

    m_loadBatchRequests.clear();
    m_loadBatchRequests.reserve(budget);
    m_loadBatchEntries.clear();
    m_loadBatchEntries.reserve(budget);

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

    uint32_t processed = 0;
    while (processed < budget && !m_pendingStreamingRequests.empty()) {
        const PendingStreamingRequest pending = m_pendingStreamingRequests.back();
        m_pendingStreamingRequests.pop_back();

        const uint32_t groupIndex = pending.request.groupGlobalIndex;
        if (groupIndex >= m_streamingStorageGroupCapacity) {
            EnsureStreamingStorageCapacity(groupIndex + 1u);
        }

        if (pending.isLoad) {
            if (groupIndex >= m_pendingLoadPriorityByGroup.size()) {
                processed++;
                continue;
            }

            if (pending.priority < m_pendingLoadPriorityByGroup[groupIndex]) {
                continue;
            }
        }

        if (pending.request.groupGlobalIndex >= m_streamingStorageGroupCapacity) {
            EnsureStreamingStorageCapacity(pending.request.groupGlobalIndex + 1u);
        }

        if (!IsGroupResident(pending.request.groupGlobalIndex)) {
            while (m_streamingResidentGroupsCount >= m_streamingResidentBudgetGroups) {
                const bool evicted = TryEvictLruVictim(pending.request.groupGlobalIndex, frameStats);
                if (!evicted) {
                    break;
                }
            }
        }

        if (!pending.isLoad) {
            bool residencyBitChanged = false;
            const bool serviced = ApplyEvictionRequest(pending.request, residencyBitChanged);
            frameStats.unloadRequested++;
            frameStats.unloadUnique++;
            if (!serviced) {
                frameStats.unloadFailed++;
            }
            else if (residencyBitChanged) {
                frameStats.unloadApplied++;
                if (m_streamingResidentGroupsCount > 0u) {
                    m_streamingResidentGroupsCount--;
                }
            }

            TouchStreamingLru(pending.request.groupGlobalIndex);
            ClearStreamingRequestInProgress(pending.request.groupGlobalIndex);
            m_pendingLoadPriorityByGroup[pending.request.groupGlobalIndex] = 0u;
            processed++;
            continue;
        }

        frameStats.loadRequested++;
        frameStats.loadUnique++;

        const uint32_t wordAddress = BitWordAddress(groupIndex);
        const uint32_t bitMask = BitMask(groupIndex);
        const uint32_t oldWord = m_streamingNonResidentBitsCpu[wordAddress];

        if (meshManager == nullptr) {
            m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
            const bool residencyBitChanged = (m_streamingNonResidentBitsCpu[wordAddress] != oldWord);
            if (residencyBitChanged) {
                frameStats.loadApplied++;
                m_streamingResidentGroupsCount++;
                m_streamingNonResidentBitsUploadPending = true;
            }

            TouchStreamingLru(groupIndex);
            ClearStreamingRequestInProgress(groupIndex);
            m_pendingLoadPriorityByGroup[groupIndex] = 0u;
            processed++;
            continue;
        }

        MeshManager::CLodGlobalResidencyRequest req{};
        req.groupGlobalIndex = groupIndex;
        req.resident = true;
        m_loadBatchRequests.push_back(req);

        PendingLoadBatchEntry entry{};
        entry.groupIndex = groupIndex;
        entry.wordAddress = wordAddress;
        entry.bitMask = bitMask;
        entry.oldWord = oldWord;
        m_loadBatchEntries.push_back(entry);

        processed++;
    }

    if (meshManager != nullptr && !m_loadBatchRequests.empty()) {
        std::vector<MeshManager::CLodGlobalResidencyResult> results;
        meshManager->SetCLodGroupResidencyForGlobalBatchEx(m_loadBatchRequests, results);

        const size_t count = std::min(m_loadBatchEntries.size(), results.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& entry = m_loadBatchEntries[i];
            const auto& result = results[i];
            const bool serviced = result.serviced;
            if (!serviced) {
                frameStats.loadFailed++;
            }
            else if (result.applied) {
                m_streamingNonResidentBitsCpu[entry.wordAddress] &= ~entry.bitMask;
                const bool residencyBitChanged = (m_streamingNonResidentBitsCpu[entry.wordAddress] != entry.oldWord);
                if (residencyBitChanged) {
                    frameStats.loadApplied++;
                    m_streamingResidentGroupsCount++;
                    m_streamingNonResidentBitsUploadPending = true;
                }
            }

            if (result.queued && !result.applied) {
                continue;
            }

            TouchStreamingLru(entry.groupIndex);
            ClearStreamingRequestInProgress(entry.groupIndex);
            m_pendingLoadPriorityByGroup[entry.groupIndex] = 0u;
        }

        for (size_t i = count; i < m_loadBatchEntries.size(); ++i) {
            const auto& entry = m_loadBatchEntries[i];
            frameStats.loadFailed++;
            TouchStreamingLru(entry.groupIndex);
            ClearStreamingRequestInProgress(entry.groupIndex);
            m_pendingLoadPriorityByGroup[entry.groupIndex] = 0u;
        }
    }

    if (meshManager != nullptr) {
        const auto debugStats = meshManager->GetCLodStreamingDebugStats();
        frameStats.residentGroups = debugStats.residentGroups;
        frameStats.residentAllocations = debugStats.residentAllocations;
        frameStats.queuedRequests = debugStats.queuedRequests;
        frameStats.completedResults = debugStats.completedResults;
        frameStats.residentAllocationBytes = debugStats.residentAllocationBytes;
        frameStats.completedResultBytes = debugStats.completedResultBytes;
    }

    PublishCLodStreamingOperationStats(frameStats);
}
