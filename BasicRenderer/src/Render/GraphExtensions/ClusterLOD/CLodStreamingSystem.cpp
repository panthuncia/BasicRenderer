#include "Render/GraphExtensions/ClusterLOD/CLodStreamingSystem.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <unordered_set>

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingBeginFramePass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingFeedbackSortPass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingReadbackCopyPass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodAsyncUploadPass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodDirectStorageCompletionWaitPass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodDirectStorageLaunchPass.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Managers/UploadInstance.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "Render/Runtime/OpenRenderGraphSettings.h"
#include "RenderPasses/StreamingUploadPass.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "BuiltinResources.h"

struct CLodStreamingSystem::ParallelSortState {
    std::shared_ptr<Buffer> keyScratch;
    std::shared_ptr<Buffer> payloadScratch;
    std::shared_ptr<Buffer> sumTable;
    std::shared_ptr<Buffer> reduceTable;
    std::shared_ptr<Buffer> constants;
    std::shared_ptr<Buffer> countScatterArgs;
    std::shared_ptr<Buffer> reduceScanArgs;
};

CLodStreamingSystem::CLodStreamingSystem() {
    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };

    m_streamingNonResidentBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingActiveGroupsBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingPinnedGroupsBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingResidencyInitializedBitsCpu.assign(CLodBitsetWordCount(m_streamingStorageGroupCapacity), 0u);
    m_streamingRequestStateByGroup.assign(m_streamingStorageGroupCapacity, StreamingRequestState::None);
    m_pendingLoadPriorityByGroup.assign(m_streamingStorageGroupCapacity, 0u);
    m_pendingStreamingRequestHeapIndexByGroup.assign(m_streamingStorageGroupCapacity, UINT32_MAX);
    m_pendingStreamingRequestGenerationByGroup.assign(m_streamingStorageGroupCapacity, 0u);
    MarkStreamingNonResidentBitsDirtyAll();
    MarkStreamingActiveGroupsBitsDirty();

    try {
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
    tagBufferUsage(m_streamingNonResidentBits, "Cluster LOD streaming");

    m_streamingActiveGroupsBits = CreateAliasedUnmaterializedStructuredBuffer(
        CLodBitsetWordCount(m_streamingStorageGroupCapacity),
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_streamingActiveGroupsBits->SetName("CLod Streaming Active Groups Bits");
    tagBufferUsage(m_streamingActiveGroupsBits, "Cluster LOD streaming");

    m_streamingLoadRequests = CreateAliasedUnmaterializedStructuredBuffer(
        CLodStreamingRequestCapacity,
        sizeof(CLodStreamingRequest),
        true,
        false,
        false,
        false);
    m_streamingLoadRequests->SetName("CLod Streaming Load Requests");
    tagBufferUsage(m_streamingLoadRequests, "Cluster LOD streaming");

    m_streamingLoadRequestKeys = CreateAliasedUnmaterializedStructuredBuffer(
        CLodStreamingRequestCapacity,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_streamingLoadRequestKeys->SetName("CLod Streaming Load Request Keys");
    tagBufferUsage(m_streamingLoadRequestKeys, "Cluster LOD streaming");

    m_streamingLoadCounter = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_streamingLoadCounter->SetName("CLod Streaming Load Counter");
    tagBufferUsage(m_streamingLoadCounter, "Cluster LOD streaming");

    m_streamingRuntimeState = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodStreamingRuntimeState), true, false, false, false);
    m_streamingRuntimeState->SetName("CLod Streaming Runtime State");
    tagBufferUsage(m_streamingRuntimeState, "Cluster LOD streaming");

    m_usedGroupsCounter = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_usedGroupsCounter->SetName("CLod Used Groups Counter");
    tagBufferUsage(m_usedGroupsCounter, "Cluster LOD streaming");

    m_usedGroupsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        CLodUsedGroupsCapacity,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_usedGroupsBuffer->SetName("CLod Used Groups Buffer");
    tagBufferUsage(m_usedGroupsBuffer, "Cluster LOD streaming");

    // Self-managed readback pipeline
    {
        auto device = DeviceManager::GetInstance().GetDevice();
        auto result = device.CreateTimeline(m_streamingReadbackFencePtr, 0, "CLodStreamingReadbackFence");
        if (result == rhi::Result::Ok && m_streamingReadbackFencePtr) {
            m_streamingReadbackFenceHandle = m_streamingReadbackFencePtr.Get();
        }
        result = device.CreateTimeline(m_directStorageLaunchFencePtr, 0, "CLodDirectStorageLaunchFence");
        if (result == rhi::Result::Ok && m_directStorageLaunchFencePtr) {
            m_directStorageLaunchFenceHandle = m_directStorageLaunchFencePtr.Get();
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
        tagBufferUsage(slot.counterStaging, "Cluster LOD streaming readback");
        slot.requestsStaging = Buffer::CreateShared(rhi::HeapType::Readback, requestsStagingBytes);
        slot.requestsStaging->SetName(("CLodReadbackRequests_" + std::to_string(i)).c_str());
        tagBufferUsage(slot.requestsStaging, "Cluster LOD streaming readback");
        slot.usedGroupsCounterStaging = Buffer::CreateShared(rhi::HeapType::Readback, usedGroupsCounterStagingBytes);
        slot.usedGroupsCounterStaging->SetName(("CLodReadbackUsedGroupsCounter_" + std::to_string(i)).c_str());
        tagBufferUsage(slot.usedGroupsCounterStaging, "Cluster LOD streaming readback");
        slot.usedGroupsBufferStaging = Buffer::CreateShared(rhi::HeapType::Readback, usedGroupsBufferStagingBytes);
        slot.usedGroupsBufferStaging->SetName(("CLodReadbackUsedGroupsBuffer_" + std::to_string(i)).c_str());
        tagBufferUsage(slot.usedGroupsBufferStaging, "Cluster LOD streaming readback");
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
    DestroyParallelSortResources();
}

void CLodStreamingSystem::OnRegistryReset(ResourceRegistry* reg) {
    (void)reg;

    auto releaseBufferBacking = [](const std::shared_ptr<Buffer>& buffer) {
        if (buffer) {
            buffer->Dematerialize();
        }
    };

    releaseBufferBacking(m_streamingNonResidentBits);
    releaseBufferBacking(m_streamingActiveGroupsBits);
    releaseBufferBacking(m_streamingLoadRequestKeys);
    releaseBufferBacking(m_streamingLoadRequests);
    releaseBufferBacking(m_streamingLoadCounter);
    releaseBufferBacking(m_streamingRuntimeState);
    releaseBufferBacking(m_usedGroupsCounter);
    releaseBufferBacking(m_usedGroupsBuffer);
    if (m_parallelSortState) {
        releaseBufferBacking(m_parallelSortState->keyScratch);
        releaseBufferBacking(m_parallelSortState->payloadScratch);
        releaseBufferBacking(m_parallelSortState->sumTable);
        releaseBufferBacking(m_parallelSortState->reduceTable);
        releaseBufferBacking(m_parallelSortState->constants);
        releaseBufferBacking(m_parallelSortState->countScatterArgs);
        releaseBufferBacking(m_parallelSortState->reduceScanArgs);
    }
    if (m_uploadInstance) {
        m_uploadInstance->Cleanup();
    }

    MeshManager* meshManager = nullptr;
    if (m_getMeshManager) {
        meshManager = m_getMeshManager();
    }

    if (meshManager != nullptr) {
        meshManager->InvalidateCLodDiskStreamingPipeline();
    }

    // Evict ALL resident groups so MeshManager clears groupResidentFlags,
    // zeroes GroupChunks counts, and wipes GroupPageMap entries. Without this,
    // the GPU reads stale chunk data with freed-page references after rebuild.
    std::vector<uint32_t> ownedGroups;
    ownedGroups.reserve(m_groupOwnedPages.size());
    for (const auto& [groupIndex, _] : m_groupOwnedPages) {
        ownedGroups.push_back(groupIndex);
    }
    for (uint32_t groupIndex : ownedGroups) {
        if (meshManager != nullptr && IsGroupResident(groupIndex)) {
            meshManager->FreeCLodGroupEviction(groupIndex);
        }
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
    ClearPrefetchedChildLayouts();
    m_preAllocatedPagesByGroup.clear();
    m_pendingResidencyCommitGroups.clear();
    m_groupsUsingPinnedStorage.clear();
    m_readbackGapPinnedGroups.clear();
    m_readbackGeneration = 0;
    m_pageLruInitialized = false;
    m_streamingResidentGroupsCount = 0u;
    std::fill(m_streamingNonResidentBitsCpu.begin(), m_streamingNonResidentBitsCpu.end(), ~0u);
    std::fill(m_streamingActiveGroupsBitsCpu.begin(), m_streamingActiveGroupsBitsCpu.end(), 0u);
    std::fill(m_streamingPinnedGroupsBitsCpu.begin(), m_streamingPinnedGroupsBitsCpu.end(), 0u);
    std::fill(m_streamingResidencyInitializedBitsCpu.begin(), m_streamingResidencyInitializedBitsCpu.end(), 0u);
    std::fill(m_streamingRequestStateByGroup.begin(), m_streamingRequestStateByGroup.end(), StreamingRequestState::None);
    std::fill(m_pendingLoadPriorityByGroup.begin(), m_pendingLoadPriorityByGroup.end(), 0u);
    std::fill(m_pendingStreamingRequestHeapIndexByGroup.begin(), m_pendingStreamingRequestHeapIndexByGroup.end(), UINT32_MAX);
    std::fill(m_pendingStreamingRequestGenerationByGroup.begin(), m_pendingStreamingRequestGenerationByGroup.end(), 0u);
    m_streamingRequestsInProgressCount = 0u;
    m_pendingStreamingRequestCount = 0u;
    m_streamingDiagnosticTick = 0;
    m_lastInProgressSuppressionLogTick.clear();
    m_streamingActiveGroupScanCount = 0u;
    MarkStreamingNonResidentBitsDirtyAll();
    MarkStreamingActiveGroupsBitsDirty();
    m_streamingDomainDirty = true;

    // Discard any stale decoded readback data from the worker thread.
    {
        std::lock_guard lock(m_decodedReadbackMutex);
        m_decodedReadbackBatch.clear();
        m_decodedUsedGroupsBatch.clear();
    }

    // Clear in-flight flags so the worker thread doesn't process stale readback data.
    for (auto& slot : m_readbackStagingSlots) {
        slot.inFlight = false;
        slot.fenceValue = 0;
    }
    m_readbackStagingCursor = 0;
}

void CLodStreamingSystem::Initialize(RenderGraph& rg) {
    // Create a dedicated copy queue for async CLod streaming uploads.
    m_uploadQueueSlot = rg.CreateQueue(
        QueueKind::Copy,
        "CLodAsyncUpload",
        QueueAutoAssignmentPolicy::ManualOnly);

    // Create the upload instance for CLod-specific uploads, using the same
    // number of frames in flight as the global settings.
    const uint8_t numFramesInFlight = rg::runtime::GetOpenRenderGraphSettings().numFramesInFlight;
    m_uploadInstance = std::make_unique<UploadInstance>(numFramesInFlight);

    EnsureParallelSortResources();
}

void CLodStreamingSystem::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) {
    rg.RegisterResource(Builtin::CLod::StreamingNonResidentBits, m_streamingNonResidentBits);
    rg.RegisterResource(Builtin::CLod::StreamingActiveGroupsBits, m_streamingActiveGroupsBits);
    rg.RegisterResource(Builtin::CLod::StreamingLoadRequestKeys, m_streamingLoadRequestKeys);
    rg.RegisterResource(Builtin::CLod::StreamingLoadRequests, m_streamingLoadRequests);
    rg.RegisterResource(Builtin::CLod::StreamingLoadCounter, m_streamingLoadCounter);
    rg.RegisterResource(Builtin::CLod::StreamingRuntimeState, m_streamingRuntimeState);
    rg.RegisterResource(Builtin::CLod::StreamingTouchedGroupsCounter, m_usedGroupsCounter);
    rg.RegisterResource(Builtin::CLod::StreamingTouchedGroups, m_usedGroupsBuffer);

	auto streamingBeginPass = std::make_shared<CLodStreamingBeginFramePass>(
		[this]() -> UploadInstance* { return m_uploadInstance.get(); },
		m_streamingLoadCounter,
        m_streamingLoadRequestKeys,
		m_usedGroupsCounter,
		m_streamingNonResidentBits,
		m_streamingActiveGroupsBits,
		m_streamingRuntimeState,
		[this](std::vector<uint32_t>& outBits, uint32_t& outFirstWord) {
			if (!m_streamingNonResidentBitsUploadPending) {
				return false;
			}

			const uint32_t begin = std::min<uint32_t>(
                m_streamingNonResidentBitsDirtyBegin,
                static_cast<uint32_t>(m_streamingNonResidentBitsCpu.size()));
            const uint32_t end = std::min<uint32_t>(
                m_streamingNonResidentBitsDirtyEnd,
                static_cast<uint32_t>(m_streamingNonResidentBitsCpu.size()));
            if (begin >= end) {
                m_streamingNonResidentBitsUploadPending = false;
                outBits.clear();
                return false;
            }

            outFirstWord = begin;
			outBits.assign(m_streamingNonResidentBitsCpu.begin() + begin, m_streamingNonResidentBitsCpu.begin() + end);
			m_streamingNonResidentBitsUploadPending = false;
            m_streamingNonResidentBitsDirtyBegin = 0u;
            m_streamingNonResidentBitsDirtyEnd = 0u;
			return true;
		},
		[this](std::vector<uint32_t>& outBits, uint32_t& outActiveScanCount) {
			outActiveScanCount = m_streamingActiveGroupScanCount;
            if (!m_streamingActiveGroupsBitsUploadPending) {
                outBits.clear();
                return false;
            }

			outBits = m_streamingActiveGroupsBitsCpu;
            m_streamingActiveGroupsBitsUploadPending = false;
            return true;
		},
		[this]() {
			PollCompletedReadbackSlots();
		},
		[this]() {
			ProcessStreamingRequestsBudgeted();
		});

    auto streamingBeginPassDesc = RenderGraph::ExternalPassDesc::Compute(
        "CLod::StreamingBeginFramePass",
        streamingBeginPass);
    // Keep the CLod front-end behind the visibility/depth clear so the graph
    // cannot legally sink ClearVisibilityBufferPass after CLod rasterization.
    streamingBeginPassDesc.At(RenderGraph::ExternalInsertPoint::After("ClearVisibilityBufferPass"));
    outPasses.push_back(std::move(streamingBeginPassDesc));
}

void CLodStreamingSystem::GatherStructuralTailPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) {
    (void)rg;
    if (EnsureParallelSortResources()) {
        auto feedbackSortPass = std::make_shared<CLodStreamingFeedbackSortPass>(
            m_streamingLoadRequestKeys,
            m_streamingLoadRequests,
            m_streamingLoadCounter,
            m_parallelSortState->keyScratch,
            m_parallelSortState->payloadScratch,
            m_parallelSortState->sumTable,
            m_parallelSortState->reduceTable,
            m_parallelSortState->constants,
            m_parallelSortState->countScatterArgs,
            m_parallelSortState->reduceScanArgs);

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Compute(
                "CLod::StreamingFeedbackSort",
                feedbackSortPass)
                .At(RenderGraph::ExternalInsertPoint::After("PresentPass"))
                .PreferQueue(QueueKind::Graphics));
    }
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

    if (meshManager != nullptr && meshManager->HasPendingCLodDirectStorageUploads()) {
        std::vector<ExternalTimelinePoint> waits;
        meshManager->CollectCLodDirectStorageCompletionWaits(waits);
        if (!waits.empty()) {
            CLodDirectStorageCompletionWaitInputs waitInputs{};
            waitInputs.waits = std::move(waits);
            if (PagePool* pool = meshManager->GetCLodPagePool()) {
                auto slabGroup = pool->GetSlabResourceGroup();
                if (auto pt = pool->GetPageTableBuffer()) {
                    slabGroup->AddResource(pt);
                }
                waitInputs.targetSlabResolver = std::make_unique<ResourceGroupResolver>(slabGroup);
            }
            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Copy(
                    "CLod::DirectStorageCompletionWait",
                    std::make_shared<CLodDirectStorageCompletionWaitPass>(std::move(waitInputs)))
                    .At(RenderGraph::ExternalInsertPoint::Before("CLod::StreamingBeginFramePass"))
                    .PreferQueue(QueueKind::Graphics));
        }
    }

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

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Copy(
                "CLod::StreamingUpload",
                std::make_shared<StreamingUploadPass>(std::move(inputs)))
                .At(RenderGraph::ExternalInsertPoint::After("EvaluateMaterialGroupsPass"))
                .PreferQueue(QueueKind::Copy));
    }

    // Drain our dedicated UploadInstance on the async copy queue.
    if (m_uploadInstance && m_uploadInstance->HasPendingWork()) {
        CLodAsyncUploadInputs asyncInputs;
        asyncInputs.uploadInstance = m_uploadInstance.get();

        MeshManager* mm = m_getMeshManager ? m_getMeshManager() : nullptr;
        if (mm) {
            if (PagePool* pool = mm->GetCLodPagePool()) {
                auto slabGroup = pool->GetSlabResourceGroup();
                if (auto pt = pool->GetPageTableBuffer()) {
                    slabGroup->AddResource(pt);
                }
                asyncInputs.poolResolver = std::make_unique<ResourceGroupResolver>(slabGroup);
            }
        }

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Copy(
                "CLod::AsyncUpload",
                std::make_shared<CLodAsyncUploadPass>(std::move(asyncInputs)))
                .At(RenderGraph::ExternalInsertPoint::After("EvaluateMaterialGroupsPass"))
                .PreferQueue(QueueKind::Copy)
                .PinToQueue(m_uploadQueueSlot));
    }

    if (meshManager != nullptr
        && meshManager->HasPendingCLodDirectStorageLaunches()
        && m_directStorageLaunchFenceHandle.IsValid()) {
        CLodDirectStorageLaunchInputs launchInputs{};
        if (PagePool* pool = meshManager->GetCLodPagePool()) {
            auto slabGroup = pool->GetSlabResourceGroup();
            if (auto pt = pool->GetPageTableBuffer()) {
                slabGroup->AddResource(pt);
            }
            launchInputs.targetSlabResolver = std::make_unique<ResourceGroupResolver>(slabGroup);
        }
        launchInputs.launchCallback = [this, meshManager]() -> PassReturn {
            if (!m_directStorageLaunchFenceHandle.IsValid()) {
                return {};
            }

            const uint64_t fenceValue =
                m_directStorageLaunchFenceCounter.fetch_add(1, std::memory_order_relaxed) + 1;
            meshManager->LaunchPendingCLodDirectStorageUploads(
                m_directStorageLaunchFenceHandle,
                fenceValue);

            PassReturn ret{};
            ret.externalSignalsAfterCompletion.push_back({ m_directStorageLaunchFenceHandle, fenceValue });
            return ret;
        };

        outPasses.push_back(
            RenderGraph::ExternalPassDesc::Copy(
                "CLod::DirectStorageLaunch",
                std::make_shared<CLodDirectStorageLaunchPass>(std::move(launchInputs)))
                .At(RenderGraph::ExternalInsertPoint::After("PresentPass"))
                .PreferQueue(QueueKind::Graphics));
    }

    // Keep the readback at the frame tail. The copy reads CLod streaming
    // counters from earlier culling passes, but placing the immediate copy in
    // the middle of the visibility graph destabilizes replay segment boundaries.
    if (m_streamingReadbackFenceHandle.IsValid() && !m_readbackStagingSlots.empty()) {
        const bool hasStreamingFeedbackSort = m_parallelSortAvailable && m_parallelSortState != nullptr;
        uint32_t selectedSlot = UINT32_MAX;
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
                auto& slot = m_readbackStagingSlots[selectedSlot];
                slot.fenceValue = 0;
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

            outPasses.push_back(
                RenderGraph::ExternalPassDesc::Copy(
                    "CLod::StreamingReadbackCopy",
                    std::make_shared<CLodStreamingReadbackCopyPass>(
                        std::move(readbackInputs),
                        slot.counterStaging,
                        slot.requestsStaging,
                        slot.usedGroupsCounterStaging,
                        slot.usedGroupsBufferStaging,
                        [this, selectedSlot]() -> PassReturn {
                            if (!m_streamingReadbackFenceHandle.IsValid()) {
                                return {};
                            }

                            const uint64_t fenceValue = m_streamingReadbackFenceCounter.fetch_add(1, std::memory_order_relaxed) + 1;
                            {
                                std::lock_guard lock(m_streamingWorkerMutex);
                                if (selectedSlot >= m_readbackStagingSlots.size()) {
                                    return {};
                                }

                                auto& armedSlot = m_readbackStagingSlots[selectedSlot];
                                if (!armedSlot.inFlight) {
                                    return {};
                                }

                                armedSlot.fenceValue = fenceValue;
                            }

                            m_streamingWorkerCV.notify_one();
                            return { m_streamingReadbackFenceHandle, fenceValue };
                        }))
                    .At(RenderGraph::ExternalInsertPoint::After(
                        hasStreamingFeedbackSort ? "CLod::StreamingFeedbackSort" : "PresentPass"))
                    .PreferQueue(QueueKind::Copy));
        }
    }
}

uint32_t CLodStreamingSystem::BitWordAddress(uint32_t key) {
    return key >> 5u;
}

uint32_t CLodStreamingSystem::BitMask(uint32_t key) {
    return 1u << (key & 31u);
}

bool CLodStreamingSystem::EnsureParallelSortResources() {
    if (m_parallelSortAttempted) {
        return m_parallelSortAvailable;
    }

    m_parallelSortAttempted = true;
    m_parallelSortState = std::make_unique<ParallelSortState>();

    auto tagBufferUsage = [](const std::shared_ptr<Buffer>& buffer, std::string_view usage) {
        if (buffer) {
            rg::memory::SetResourceUsageHint(*buffer, std::string(usage));
        }
    };

    constexpr uint32_t blockSize = 4u * 128u;
    constexpr uint32_t sortBinCount = 16u;
    constexpr uint32_t numBlocks = (CLodStreamingRequestCapacity + blockSize - 1u) / blockSize;
    constexpr uint32_t numReducedBlocks = (numBlocks + blockSize - 1u) / blockSize;
    constexpr uint32_t sumTableElements = sortBinCount * numBlocks;
    constexpr uint32_t reduceTableElements = sortBinCount * numReducedBlocks;
    constexpr uint32_t radixIterationCount = 8u;

    m_parallelSortState->keyScratch = CreateAliasedUnmaterializedStructuredBuffer(
        CLodStreamingRequestCapacity,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_parallelSortState->keyScratch->SetName("CLod Streaming Sort Key Scratch");

    m_parallelSortState->payloadScratch = CreateAliasedUnmaterializedStructuredBuffer(
        CLodStreamingRequestCapacity,
        sizeof(CLodStreamingRequest),
        true,
        false,
        false,
        false);
    m_parallelSortState->payloadScratch->SetName("CLod Streaming Sort Payload Scratch");

    m_parallelSortState->sumTable = CreateAliasedUnmaterializedStructuredBuffer(
        sumTableElements,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_parallelSortState->sumTable->SetName("CLod Streaming Sort Sum Table");

    m_parallelSortState->reduceTable = CreateAliasedUnmaterializedStructuredBuffer(
        reduceTableElements,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_parallelSortState->reduceTable->SetName("CLod Streaming Sort Reduce Table");

    struct ParallelSortConstantsCpu {
        uint32_t numKeys;
        int32_t numBlocksPerThreadGroup;
        uint32_t numThreadGroups;
        uint32_t numThreadGroupsWithAdditionalBlocks;
        uint32_t numReduceThreadgroupPerBin;
        uint32_t numScanValues;
        uint32_t shift;
        uint32_t padding;
    };
    static_assert(sizeof(ParallelSortConstantsCpu) == 32u);

    m_parallelSortState->constants = CreateAliasedUnmaterializedStructuredBuffer(
        radixIterationCount,
        sizeof(ParallelSortConstantsCpu),
        true,
        false,
        false,
        false);
    m_parallelSortState->constants->SetName("CLod Streaming Sort Constants");

    m_parallelSortState->countScatterArgs = CreateAliasedUnmaterializedStructuredBuffer(
        3u,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_parallelSortState->countScatterArgs->SetName("CLod Streaming Sort Count Scatter Args");

    m_parallelSortState->reduceScanArgs = CreateAliasedUnmaterializedStructuredBuffer(
        3u,
        sizeof(uint32_t),
        true,
        false,
        false,
        false);
    m_parallelSortState->reduceScanArgs->SetName("CLod Streaming Sort Reduce Scan Args");

    tagBufferUsage(m_parallelSortState->keyScratch, "Cluster LOD streaming sort");
    tagBufferUsage(m_parallelSortState->payloadScratch, "Cluster LOD streaming sort");
    tagBufferUsage(m_parallelSortState->sumTable, "Cluster LOD streaming sort");
    tagBufferUsage(m_parallelSortState->reduceTable, "Cluster LOD streaming sort");
    tagBufferUsage(m_parallelSortState->constants, "Cluster LOD streaming sort");
    tagBufferUsage(m_parallelSortState->countScatterArgs, "Cluster LOD streaming sort");
    tagBufferUsage(m_parallelSortState->reduceScanArgs, "Cluster LOD streaming sort");

    m_parallelSortAvailable = true;
    return true;
}

void CLodStreamingSystem::DestroyParallelSortResources() {
    if (!m_parallelSortState) {
        m_parallelSortAvailable = false;
        return;
    }

    m_parallelSortState.reset();
    m_parallelSortAvailable = false;
}

void CLodStreamingSystem::MarkStreamingNonResidentBitsDirtyWord(uint32_t wordAddress) {
    if (wordAddress >= m_streamingNonResidentBitsCpu.size()) {
        return;
    }

    if (!m_streamingNonResidentBitsUploadPending) {
        m_streamingNonResidentBitsDirtyBegin = wordAddress;
        m_streamingNonResidentBitsDirtyEnd = wordAddress + 1u;
        m_streamingNonResidentBitsUploadPending = true;
        return;
    }

    m_streamingNonResidentBitsDirtyBegin = std::min(m_streamingNonResidentBitsDirtyBegin, wordAddress);
    m_streamingNonResidentBitsDirtyEnd = std::max(m_streamingNonResidentBitsDirtyEnd, wordAddress + 1u);
}

void CLodStreamingSystem::MarkStreamingNonResidentBitsDirtyAll() {
    const auto wordCount = static_cast<uint32_t>(m_streamingNonResidentBitsCpu.size());
    if (wordCount == 0u) {
        m_streamingNonResidentBitsUploadPending = false;
        m_streamingNonResidentBitsDirtyBegin = 0u;
        m_streamingNonResidentBitsDirtyEnd = 0u;
        return;
    }

    m_streamingNonResidentBitsUploadPending = true;
    m_streamingNonResidentBitsDirtyBegin = 0u;
    m_streamingNonResidentBitsDirtyEnd = wordCount;
}

void CLodStreamingSystem::MarkStreamingActiveGroupsBitsDirty() {
    m_streamingActiveGroupsBitsUploadPending = true;
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

bool CLodStreamingSystem::UsesPinnedStorage(uint32_t groupIndex) const {
    return m_groupsUsingPinnedStorage.count(groupIndex) != 0u;
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
        // Update priority without enqueueing a duplicate request.
        const uint32_t oldPriority = GetPendingLoadPriority(groupIndex);
        uint32_t newPriority = oldPriority;
        if (m_priorityMode == CLodPriorityMode::Sum) {
            newPriority += priority;
        } else {
            newPriority = std::max(newPriority, priority);
        }
        if (newPriority != oldPriority) {
            if (groupIndex < m_streamingRequestStateByGroup.size()
                && m_streamingRequestStateByGroup[groupIndex] == StreamingRequestState::PendingCpu) {
                CLodStreamingRequest pendingReq = req;
                pendingReq.groupGlobalIndex = groupIndex;
                PushOrUpdatePendingStreamingRequest(pendingReq, newPriority);
            } else {
                SetPendingLoadPriority(groupIndex, newPriority);
            }
        }

        constexpr uint64_t kDiagnosticLogCooldownTicks = 120u;
        const uint64_t currentTick = m_streamingDiagnosticTick;
        bool shouldLog = true;
        if (const auto it = m_lastInProgressSuppressionLogTick.find(groupIndex);
            it != m_lastInProgressSuppressionLogTick.end()) {
            shouldLog = currentTick >= it->second + kDiagnosticLogCooldownTicks;
        }

        if (shouldLog) {
            m_lastInProgressSuppressionLogTick[groupIndex] = currentTick;

            MeshManager::CLodStreamingDebugStats debugStats{};
            bool meshQueued = false;
            if (MeshManager* meshManager = m_getMeshManager ? m_getMeshManager() : nullptr) {
                meshQueued = meshManager->IsCLodGroupDiskIOQueued(groupIndex);
                debugStats = meshManager->GetCLodStreamingDebugStats();
            }

            spdlog::info(
                "CLod streaming diag[tick={}]: suppressing duplicate load for group {} because it is already in progress; newPriority={} accumulatedPriority={} meshQueued={} cpuPending={} cpuInProgress={} meshPending={} meshQueuedOrInFlight={} meshCompleted={}",
                currentTick,
                groupIndex,
                priority,
                GetPendingLoadPriority(groupIndex),
                meshQueued ? 1u : 0u,
                m_pendingStreamingRequestCount,
                m_streamingRequestsInProgressCount,
                debugStats.queuedRequests,
                debugStats.queuedOrInFlightGroups,
                debugStats.completedResults);
        }

        return false;
    }

    PushOrUpdatePendingStreamingRequest(req, priority);
    return true;
}

uint32_t CLodStreamingSystem::QueueLoadRequestWithParents(const CLodStreamingRequest& requestedLoad, uint32_t requestedPriority) {
    ZoneScopedN("CLodStreamingSystem::QueueLoadRequestWithParents");

    if (requestedLoad.groupGlobalIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(requestedLoad.groupGlobalIndex + 1u);
    }

    uint32_t queuedCount = 0u;
    m_parentChainScratch.clear();
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

        m_parentChainScratch.push_back(parentGroup);
        currentGroup = parentGroup;
    }

    for (auto it = m_parentChainScratch.rbegin(); it != m_parentChainScratch.rend(); ++it) {
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
    ZoneScopedN("CLodStreamingSystem::InitializePageLru");

    if (m_pageLruInitialized || !meshManager) return;

    auto* pool = meshManager->GetCLodPagePool();
    if (!pool) return;

    const uint32_t generalPages = pool->GetGeneralPageCount();
    const uint32_t totalPages = pool->GetTotalPageCount();
    if (generalPages == 0 || totalPages == 0) return;

    m_pageOwnerGroup.assign(totalPages, -1);
    m_pageOwnerSegment.resize(totalPages, 0u);
    m_pageState.assign(totalPages, CLodPhysicalPageState::Free);
    m_pendingPageOwnerGroup.assign(totalPages, ~0u);
    m_pendingPageOwnerSegment.assign(totalPages, 0u);

    {
        ZoneScopedN("CLodStreamingSystem::InitializePageLru::PopulateFreePages");
        for (uint32_t p = 0; p < generalPages; ++p) {
            m_pageLru.Insert(p);
        }
    }

    m_pageLruInitialized = true;

    // Route PagePool uploads through our dedicated UploadInstance.
    if (m_uploadInstance) {
        pool->SetUploadFunction([inst = m_uploadInstance.get()](
            const void* data, size_t size,
            rg::runtime::UploadTarget target, size_t offset) {
            inst->UploadData(data, size, target, offset);
        });
    }

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
        m_pageState.resize(totalPages, CLodPhysicalPageState::Free);
        m_pendingPageOwnerGroup.resize(totalPages, ~0u);
        m_pendingPageOwnerSegment.resize(totalPages, 0u);
    }
}

void CLodStreamingSystem::ReleaseOwnedPagesForGroup(uint32_t groupIndex, MeshManager* meshManager) {
    EvictPrefetchedChildLayoutsForOwner(groupIndex);
    m_pendingResidencyCommitGroups.erase(groupIndex);

    auto it = m_groupOwnedPages.find(groupIndex);
    if (it == m_groupOwnedPages.end()) {
        SetGroupUsesPinnedStorage(groupIndex, false);
        return;
    }

    // If pages were pinned during the readback-gap window, unpin them
    // before returning them to the LRU / freeing pinned storage.
    const bool hadReadbackGapPin = m_readbackGapPinnedGroups.erase(groupIndex) != 0;

    const bool usesPinnedStorage = UsesPinnedStorage(groupIndex);
    std::vector<uint32_t> pinnedPagesToFree;
    if (usesPinnedStorage) {
        pinnedPagesToFree.reserve(it->second.size());
    }

    for (uint32_t page : it->second) {
        if (page == ~0u) {
            continue;
        }

        if (hadReadbackGapPin && !usesPinnedStorage) {
            m_pageLru.Unpin(page);
        }

        if (page < m_pageOwnerGroup.size()) {
            m_pageOwnerGroup[page] = -1;
            m_pageOwnerSegment[page] = 0u;
            m_pageState[page] = CLodPhysicalPageState::Free;
            m_pendingPageOwnerGroup[page] = ~0u;
            m_pendingPageOwnerSegment[page] = 0u;
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
    SetGroupUsesPinnedStorage(groupIndex, false);
}

void CLodStreamingSystem::PrefetchChildGroupLayouts(uint32_t parentGroupIndex, MeshManager* meshManager) {
    if (meshManager == nullptr) {
        return;
    }

    const auto childIt = m_childGroupsByGlobal.find(parentGroupIndex);
    if (childIt == m_childGroupsByGlobal.end() || childIt->second.empty()) {
        return;
    }

    EvictPrefetchedChildLayoutsForOwner(parentGroupIndex);

    std::vector<uint32_t> insertedChildren;
    insertedChildren.reserve(childIt->second.size());

    for (uint32_t childGroupIndex : childIt->second) {
        CLodCache::GroupPayloadLayoutMetadata layout;
        std::string message;
        if (!meshManager->TryGetCLodGroupPayloadLayout(childGroupIndex, layout, &message) || !layout.IsValid()) {
            spdlog::debug(
                "CLod streaming: child header prefetch miss for parent {} child {}: {}",
                parentGroupIndex,
                childGroupIndex,
                message.empty() ? "layout unavailable" : message);
            continue;
        }

        CachedChildGroupLayout cachedLayout{};
        cachedLayout.ownerGroupIndex = parentGroupIndex;
        cachedLayout.layout = std::move(layout);
        m_prefetchedChildLayoutsByGroup[childGroupIndex] = std::move(cachedLayout);
        insertedChildren.push_back(childGroupIndex);
    }

    if (!insertedChildren.empty()) {
        m_prefetchedChildLayoutKeysByOwner[parentGroupIndex] = std::move(insertedChildren);
    }
}

void CLodStreamingSystem::InstallPrefetchedChildGroupLayouts(
    uint32_t parentGroupIndex,
    std::vector<MeshManager::CLodPrefetchedChildLayout>&& prefetchedLayouts) {
    if (prefetchedLayouts.empty()) {
        return;
    }

    EvictPrefetchedChildLayoutsForOwner(parentGroupIndex);

    std::vector<uint32_t> insertedChildren;
    insertedChildren.reserve(prefetchedLayouts.size());

    for (auto& prefetchedLayout : prefetchedLayouts) {
        if (!prefetchedLayout.layout.IsValid()) {
            continue;
        }

        const uint32_t childGroupIndex = prefetchedLayout.groupGlobalIndex;
        CachedChildGroupLayout cachedLayout{};
        cachedLayout.ownerGroupIndex = parentGroupIndex;
        cachedLayout.layout = std::move(prefetchedLayout.layout);
        m_prefetchedChildLayoutsByGroup[childGroupIndex] = std::move(cachedLayout);
        insertedChildren.push_back(childGroupIndex);
    }

    if (!insertedChildren.empty()) {
        m_prefetchedChildLayoutKeysByOwner[parentGroupIndex] = std::move(insertedChildren);
    }
}

void CLodStreamingSystem::EvictPrefetchedChildLayoutsForOwner(uint32_t ownerGroupIndex) {
    auto ownerIt = m_prefetchedChildLayoutKeysByOwner.find(ownerGroupIndex);
    if (ownerIt == m_prefetchedChildLayoutKeysByOwner.end()) {
        return;
    }

    for (uint32_t childGroupIndex : ownerIt->second) {
        auto layoutIt = m_prefetchedChildLayoutsByGroup.find(childGroupIndex);
        if (layoutIt != m_prefetchedChildLayoutsByGroup.end() && layoutIt->second.ownerGroupIndex == ownerGroupIndex) {
            m_prefetchedChildLayoutsByGroup.erase(layoutIt);
        }
    }

    m_prefetchedChildLayoutKeysByOwner.erase(ownerIt);
}

void CLodStreamingSystem::ClearPrefetchedChildLayouts() {
    m_prefetchedChildLayoutsByGroup.clear();
    m_prefetchedChildLayoutKeysByOwner.clear();
}

std::vector<uint32_t> CLodStreamingSystem::PopFreePages(uint32_t count, MeshManager* meshManager) {
    std::vector<uint32_t> pages;
    pages.reserve(count);

    uint32_t attemptsRemaining = m_pageLru.Size();
    while (pages.size() < count && attemptsRemaining > 0u) {
        --attemptsRemaining;
        uint32_t page = m_pageLru.PopOldest();
        if (page == ~0u) break;

        int32_t ownerGroup = m_pageOwnerGroup[page];
        if (ownerGroup >= 0) {
            uint32_t g = static_cast<uint32_t>(ownerGroup);

            if (IsStreamingRequestInProgress(g) ||
                m_preAllocatedPagesByGroup.count(g) != 0u ||
                m_pendingResidencyCommitGroups.count(g) != 0u) {
                continue;
            }

            // Never evict pages belonging to groups the GPU reported as
            // visible last frame - doing so causes single-frame holes.
            const uint32_t wa = BitWordAddress(g);
            if (wa < m_usedGroupsBitsCpu.size() && (m_usedGroupsBitsCpu[wa] & BitMask(g)) != 0u) {
                // Push the page back to MRU and stop - we've hit the
                // on-screen frontier so no older pages are safe either.
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
                MarkStreamingNonResidentBitsDirtyWord(wordAddress);
                if (m_streamingResidentGroupsCount > 0u) {
                    m_streamingResidentGroupsCount--;
                }

                // Metadata-only eviction: clear group state in MeshManager
                // but don't free pages (they're managed by the page LRU now).
                if (meshManager) {
                    meshManager->FreeCLodGroupEviction(g);
                }
                m_pendingResidencyCommitGroups.erase(g);
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
        const bool ownedUsesPinnedStorage = UsesPinnedStorage(groupIndex);
        for (uint32_t seg = 0; seg < segmentCount && seg < static_cast<uint32_t>(owned.size()); ++seg) {
            uint32_t existingPage = owned[seg];
            if (existingPage != ~0u && existingPage < m_pageOwnerGroup.size()
                && m_pageOwnerGroup[existingPage] == static_cast<int32_t>(groupIndex)
                && ownedUsesPinnedStorage == result.usesPinnedStorage) {
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

    for (uint32_t seg = 0; seg < segmentCount; ++seg) {
        const uint32_t page = result.pagesBySegment[seg];
        if (page == ~0u || page >= m_pageState.size()) {
            continue;
        }
        if (result.segmentNeedsFetch[seg]) {
            // PopOldest() keeps pages represented in the LRU. Preallocated pages
            // must therefore keep a provisional owner so later allocations can
            // reject them via m_preAllocatedPagesByGroup / in-progress checks.
            m_pageOwnerGroup[page] = static_cast<int32_t>(groupIndex);
            m_pageOwnerSegment[page] = seg;
            m_pageState[page] = CLodPhysicalPageState::PreAllocatedCpuUpload;
            m_pendingPageOwnerGroup[page] = groupIndex;
            m_pendingPageOwnerSegment[page] = seg;
        }
    }

    return result;
}

void CLodStreamingSystem::AssignPagesToGroup(uint32_t groupIndex, const PreAllocatedPages& pages) {
    m_groupOwnedPages[groupIndex] = pages.pagesBySegment;
    SetGroupUsesPinnedStorage(groupIndex, pages.usesPinnedStorage);

    for (uint32_t seg = 0; seg < pages.segmentCount; ++seg) {
        uint32_t page = pages.pagesBySegment[seg];
        if (page != ~0u && page < m_pageOwnerGroup.size()) {
            m_pageOwnerGroup[page] = static_cast<int32_t>(groupIndex);
            m_pageOwnerSegment[page] = seg;
            m_pageState[page] = CLodPhysicalPageState::Resident;
            m_pendingPageOwnerGroup[page] = ~0u;
            m_pendingPageOwnerSegment[page] = 0u;
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
                m_pageState[page] = CLodPhysicalPageState::Free;
                m_pendingPageOwnerGroup[page] = ~0u;
                m_pendingPageOwnerSegment[page] = 0u;
            }
            pinnedPagesToFree.push_back(page);
            continue;
        }

        if (pages.segmentNeedsFetch[seg]) {
            // Fresh page that was never assigned - clear ownership and return to LRU.
            if (page < m_pageOwnerGroup.size()) {
                m_pageOwnerGroup[page] = -1;
                m_pageOwnerSegment[page] = 0u;
                m_pageState[page] = CLodPhysicalPageState::Free;
                m_pendingPageOwnerGroup[page] = ~0u;
                m_pendingPageOwnerSegment[page] = 0u;
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

void CLodStreamingSystem::CommitPendingResidencyPromotions() {
    ZoneScopedN("CLodStreamingSystem::CommitPendingResidencyPromotions");

    if (m_pendingResidencyCommitGroups.empty()) {
        return;
    }

    std::vector<uint32_t> groups;
    {
        ZoneScopedN("CLodStreamingSystem::CommitPendingResidencyPromotions::CollectGroups");
        groups.reserve(m_pendingResidencyCommitGroups.size());
        for (uint32_t groupIndex : m_pendingResidencyCommitGroups) {
            groups.push_back(groupIndex);
        }
        m_pendingResidencyCommitGroups.clear();
    }

    {
        ZoneScopedN("CLodStreamingSystem::CommitPendingResidencyPromotions::ApplyPromotions");
        for (uint32_t groupIndex : groups) {
            if (groupIndex >= m_streamingStorageGroupCapacity || !IsGroupActive(groupIndex)) {
                continue;
            }
            if (m_groupOwnedPages.find(groupIndex) == m_groupOwnedPages.end()) {
                continue;
            }

            const uint32_t wordAddress = BitWordAddress(groupIndex);
            if (wordAddress >= m_streamingNonResidentBitsCpu.size()) {
                continue;
            }

            const uint32_t bitMask = BitMask(groupIndex);
            const bool wasNonResident = (m_streamingNonResidentBitsCpu[wordAddress] & bitMask) != 0u;
            m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
            if (wasNonResident) {
                m_streamingResidentGroupsCount++;
                MarkStreamingNonResidentBitsDirtyWord(wordAddress);
            }
        }
    }
}

void CLodStreamingSystem::TouchGroupPages(uint32_t groupIndex) {
    ZoneScopedN("CLodStreamingSystem::TouchGroupPages");

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
    m_usedGroupsBitsCpu.resize(newWordCount, 0u);
    m_streamingParentGroupByGlobal.resize(newCapacity, -1);
    m_streamingRequestStateByGroup.resize(newCapacity, StreamingRequestState::None);
    m_pendingLoadPriorityByGroup.resize(newCapacity, 0u);
    m_pendingStreamingRequestHeapIndexByGroup.resize(newCapacity, UINT32_MAX);
    m_pendingStreamingRequestGenerationByGroup.resize(newCapacity, 0u);
    m_streamingStorageGroupCapacity = newCapacity;

    MarkStreamingNonResidentBitsDirtyAll();
    MarkStreamingActiveGroupsBitsDirty();
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

    bool rebuiltDomain = false;
    if (m_streamingDomainDirty) {
        m_streamingDomainDirty = false;
        rebuiltDomain = true;

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

        ClearPrefetchedChildLayouts();
        m_childGroupsByGlobal.clear();
        for (uint32_t childGlobal = 0; childGlobal < static_cast<uint32_t>(m_streamingParentGroupByGlobal.size()); ++childGlobal) {
            const int32_t parent = m_streamingParentGroupByGlobal[childGlobal];
            if (parent >= 0) {
                m_childGroupsByGlobal[static_cast<uint32_t>(parent)].push_back(childGlobal);
            }
        }

        // Copy original error values (retained for potential future use)
        m_groupOriginalErrorByGlobal.assign(m_streamingStorageGroupCapacity, 0.0f);
        if (!m_cachedDomainSnapshot.groupOriginalErrorByGlobal.empty()) {
            const size_t errorCopyCount = std::min(
                m_cachedDomainSnapshot.groupOriginalErrorByGlobal.size(),
                m_groupOriginalErrorByGlobal.size());
            std::copy_n(m_cachedDomainSnapshot.groupOriginalErrorByGlobal.begin(), errorCopyCount, m_groupOriginalErrorByGlobal.begin());
        }
        m_errorOverriddenGroups.clear();

        m_streamingActiveGroupScanCount = maxGroupIndex;

        for (const auto& range : m_cachedDomainSnapshot.activeRanges) {
            const uint32_t rangeBegin = std::min(range.groupsBase, m_streamingStorageGroupCapacity);
            const uint32_t rangeEnd = std::min(range.groupsBase + range.groupCount, m_streamingStorageGroupCapacity);
            for (uint32_t groupIndex = rangeBegin; groupIndex < rangeEnd; ++groupIndex) {
                m_streamingActiveGroupsBitsCpu[BitWordAddress(groupIndex)] |= BitMask(groupIndex);
            }
        }
        MarkStreamingActiveGroupsBitsDirty();

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
            if (!UsesPinnedStorage(groupIndex)) {
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
                MarkStreamingNonResidentBitsDirtyWord(wa);
            }
            ClearStreamingRequestInProgress(groupIndex);
            ClearPendingLoadPriority(groupIndex);
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
                MarkStreamingNonResidentBitsDirtyWord(wa);
            }
            ClearStreamingRequestInProgress(groupIndex);
            ClearPendingLoadPriority(groupIndex);
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
                MarkStreamingNonResidentBitsDirtyWord(wordAddress);
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
                // Dedicated pinned slab pool exhausted - can't load this pinned group yet.
                m_streamingNonResidentBitsCpu[wordAddress] |= bitMask;
                m_streamingResidencyInitializedBitsCpu[wordAddress] &= ~bitMask;
                MarkStreamingNonResidentBitsDirtyWord(wordAddress);
                continue;
            }

            m_preAllocatedPagesByGroup[groupIndex] = std::move(preAlloc);

            auto paIt = m_preAllocatedPagesByGroup.find(groupIndex);
            const std::vector<bool> emptySegmentNeedsFetch;
            const std::vector<uint32_t> emptyPreAllocPageIDs;
            const std::vector<bool>* segmentNeedsFetch = &emptySegmentNeedsFetch;
            const std::vector<uint32_t>* preAllocPageIDs = &emptyPreAllocPageIDs;
            if (paIt != m_preAllocatedPagesByGroup.end()) {
                const auto& pa = paIt->second;
                segmentNeedsFetch = &pa.segmentNeedsFetch;
                preAllocPageIDs = &pa.pagesBySegment;
            }

            const bool queued = meshManager->QueueCLodGroupDiskIO(groupIndex, *segmentNeedsFetch, *preAllocPageIDs);
            if (queued) {
                MarkStreamingRequestDiskIo(groupIndex);
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
            MarkStreamingNonResidentBitsDirtyWord(wordAddress);
        }
    }

    if (rebuiltDomain) {
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
}

bool CLodStreamingSystem::IsStreamingRequestInProgress(uint32_t groupIndex) const {
    return groupIndex < m_streamingRequestStateByGroup.size()
        && m_streamingRequestStateByGroup[groupIndex] != StreamingRequestState::None;
}

void CLodStreamingSystem::MarkStreamingRequestPending(uint32_t groupIndex) {
    if (groupIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(groupIndex + 1u);
    }

    auto& state = m_streamingRequestStateByGroup[groupIndex];
    if (state == StreamingRequestState::None) {
        ++m_streamingRequestsInProgressCount;
    }
    if (state != StreamingRequestState::PendingCpu) {
        ++m_pendingStreamingRequestCount;
    }
    state = StreamingRequestState::PendingCpu;
}

void CLodStreamingSystem::MarkStreamingRequestDiskIo(uint32_t groupIndex) {
    if (groupIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(groupIndex + 1u);
    }

    auto& state = m_streamingRequestStateByGroup[groupIndex];
    if (state == StreamingRequestState::None) {
        ++m_streamingRequestsInProgressCount;
    }
    if (state == StreamingRequestState::PendingCpu && m_pendingStreamingRequestCount > 0u) {
        --m_pendingStreamingRequestCount;
    }
    state = StreamingRequestState::DiskIo;
}

void CLodStreamingSystem::ClearStreamingRequestInProgress(uint32_t groupIndex) {
    if (groupIndex >= m_streamingRequestStateByGroup.size()) {
        return;
    }

    auto& state = m_streamingRequestStateByGroup[groupIndex];
    if (state == StreamingRequestState::None) {
        return;
    }

    if (state == StreamingRequestState::PendingCpu
        && groupIndex < m_pendingStreamingRequestHeapIndexByGroup.size()) {
        const uint32_t heapIndex = m_pendingStreamingRequestHeapIndexByGroup[groupIndex];
        if (heapIndex != UINT32_MAX
            && heapIndex < m_pendingStreamingRequests.size()
            && m_pendingStreamingRequests[heapIndex].request.groupGlobalIndex == groupIndex) {
            auto higherPriority = [this](uint32_t lhsIndex, uint32_t rhsIndex) {
                const auto& lhs = m_pendingStreamingRequests[lhsIndex];
                const auto& rhs = m_pendingStreamingRequests[rhsIndex];
                if (lhs.priority != rhs.priority) {
                    return lhs.priority > rhs.priority;
                }
                return lhs.request.groupGlobalIndex < rhs.request.groupGlobalIndex;
            };

            auto swapEntries = [this](uint32_t a, uint32_t b) {
                std::swap(m_pendingStreamingRequests[a], m_pendingStreamingRequests[b]);
                m_pendingStreamingRequestHeapIndexByGroup[m_pendingStreamingRequests[a].request.groupGlobalIndex] = a;
                m_pendingStreamingRequestHeapIndexByGroup[m_pendingStreamingRequests[b].request.groupGlobalIndex] = b;
            };

            auto siftUp = [&](uint32_t index) {
                while (index > 0u) {
                    const uint32_t parent = (index - 1u) >> 1u;
                    if (!higherPriority(index, parent)) {
                        break;
                    }
                    swapEntries(index, parent);
                    index = parent;
                }
                return index;
            };

            auto siftDown = [&](uint32_t index) {
                for (;;) {
                    const uint32_t left = index * 2u + 1u;
                    const uint32_t right = left + 1u;
                    uint32_t best = index;
                    if (left < m_pendingStreamingRequests.size() && higherPriority(left, best)) {
                        best = left;
                    }
                    if (right < m_pendingStreamingRequests.size() && higherPriority(right, best)) {
                        best = right;
                    }
                    if (best == index) {
                        break;
                    }
                    swapEntries(index, best);
                    index = best;
                }
            };

            m_pendingStreamingRequestHeapIndexByGroup[groupIndex] = UINT32_MAX;
            if (heapIndex + 1u == m_pendingStreamingRequests.size()) {
                m_pendingStreamingRequests.pop_back();
            } else {
                m_pendingStreamingRequests[heapIndex] = m_pendingStreamingRequests.back();
                m_pendingStreamingRequests.pop_back();
                const uint32_t movedGroup = m_pendingStreamingRequests[heapIndex].request.groupGlobalIndex;
                m_pendingStreamingRequestHeapIndexByGroup[movedGroup] = heapIndex;
                const uint32_t adjustedIndex = siftUp(heapIndex);
                siftDown(adjustedIndex);
            }
        }
    }

    if (state == StreamingRequestState::PendingCpu && m_pendingStreamingRequestCount > 0u) {
        --m_pendingStreamingRequestCount;
    }
    if (m_streamingRequestsInProgressCount > 0u) {
        --m_streamingRequestsInProgressCount;
    }
    state = StreamingRequestState::None;
    if (groupIndex < m_pendingStreamingRequestHeapIndexByGroup.size()) {
        m_pendingStreamingRequestHeapIndexByGroup[groupIndex] = UINT32_MAX;
    }
    if (groupIndex < m_pendingStreamingRequestGenerationByGroup.size()) {
        ++m_pendingStreamingRequestGenerationByGroup[groupIndex];
    }
}

uint32_t CLodStreamingSystem::GetPendingLoadPriority(uint32_t groupIndex) const {
    return groupIndex < m_pendingLoadPriorityByGroup.size() ? m_pendingLoadPriorityByGroup[groupIndex] : 0u;
}

void CLodStreamingSystem::SetPendingLoadPriority(uint32_t groupIndex, uint32_t priority) {
    if (groupIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(groupIndex + 1u);
    }
    m_pendingLoadPriorityByGroup[groupIndex] = priority;
}

void CLodStreamingSystem::ClearPendingLoadPriority(uint32_t groupIndex) {
    if (groupIndex < m_pendingLoadPriorityByGroup.size()) {
        m_pendingLoadPriorityByGroup[groupIndex] = 0u;
    }
}

void CLodStreamingSystem::PushOrUpdatePendingStreamingRequest(const CLodStreamingRequest& req, uint32_t priority) {
    const uint32_t groupIndex = req.groupGlobalIndex;
    if (groupIndex >= m_streamingStorageGroupCapacity) {
        EnsureStreamingStorageCapacity(groupIndex + 1u);
    }

    MarkStreamingRequestPending(groupIndex);
    SetPendingLoadPriority(groupIndex, priority);
    const uint32_t generation = ++m_pendingStreamingRequestGenerationByGroup[groupIndex];

    auto higherPriority = [this](uint32_t lhsIndex, uint32_t rhsIndex) {
        const auto& lhs = m_pendingStreamingRequests[lhsIndex];
        const auto& rhs = m_pendingStreamingRequests[rhsIndex];
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return lhs.request.groupGlobalIndex < rhs.request.groupGlobalIndex;
    };

    auto swapEntries = [this](uint32_t a, uint32_t b) {
        std::swap(m_pendingStreamingRequests[a], m_pendingStreamingRequests[b]);
        m_pendingStreamingRequestHeapIndexByGroup[m_pendingStreamingRequests[a].request.groupGlobalIndex] = a;
        m_pendingStreamingRequestHeapIndexByGroup[m_pendingStreamingRequests[b].request.groupGlobalIndex] = b;
    };

    auto siftUp = [&](uint32_t index) {
        while (index > 0u) {
            const uint32_t parent = (index - 1u) >> 1u;
            if (!higherPriority(index, parent)) {
                break;
            }
            swapEntries(index, parent);
            index = parent;
        }
    };

    auto siftDown = [&](uint32_t index) {
        for (;;) {
            const uint32_t left = index * 2u + 1u;
            const uint32_t right = left + 1u;
            uint32_t best = index;
            if (left < m_pendingStreamingRequests.size() && higherPriority(left, best)) {
                best = left;
            }
            if (right < m_pendingStreamingRequests.size() && higherPriority(right, best)) {
                best = right;
            }
            if (best == index) {
                break;
            }
            swapEntries(index, best);
            index = best;
        }
    };

    uint32_t heapIndex = m_pendingStreamingRequestHeapIndexByGroup[groupIndex];
    if (heapIndex == UINT32_MAX || heapIndex >= m_pendingStreamingRequests.size()
        || m_pendingStreamingRequests[heapIndex].request.groupGlobalIndex != groupIndex) {
        PendingStreamingRequest pending{};
        pending.request = req;
        pending.priority = priority;
        pending.generation = generation;
        m_pendingStreamingRequests.push_back(pending);
        heapIndex = static_cast<uint32_t>(m_pendingStreamingRequests.size() - 1u);
        m_pendingStreamingRequestHeapIndexByGroup[groupIndex] = heapIndex;
        siftUp(heapIndex);
        return;
    }

    const uint32_t oldPriority = m_pendingStreamingRequests[heapIndex].priority;
    m_pendingStreamingRequests[heapIndex].request = req;
    m_pendingStreamingRequests[heapIndex].priority = priority;
    m_pendingStreamingRequests[heapIndex].generation = generation;
    if (priority > oldPriority) {
        siftUp(heapIndex);
    } else if (priority < oldPriority) {
        siftDown(heapIndex);
    }
}

bool CLodStreamingSystem::PopHighestPriorityPendingStreamingRequest(PendingStreamingRequest& outRequest) {
    if (m_pendingStreamingRequests.empty()) {
        return false;
    }

    auto higherPriority = [this](uint32_t lhsIndex, uint32_t rhsIndex) {
        const auto& lhs = m_pendingStreamingRequests[lhsIndex];
        const auto& rhs = m_pendingStreamingRequests[rhsIndex];
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return lhs.request.groupGlobalIndex < rhs.request.groupGlobalIndex;
    };

    auto swapEntries = [this](uint32_t a, uint32_t b) {
        std::swap(m_pendingStreamingRequests[a], m_pendingStreamingRequests[b]);
        m_pendingStreamingRequestHeapIndexByGroup[m_pendingStreamingRequests[a].request.groupGlobalIndex] = a;
        m_pendingStreamingRequestHeapIndexByGroup[m_pendingStreamingRequests[b].request.groupGlobalIndex] = b;
    };

    outRequest = m_pendingStreamingRequests.front();
    const uint32_t groupIndex = outRequest.request.groupGlobalIndex;
    if (groupIndex < m_pendingStreamingRequestHeapIndexByGroup.size()
        && m_pendingStreamingRequestHeapIndexByGroup[groupIndex] == 0u) {
        m_pendingStreamingRequestHeapIndexByGroup[groupIndex] = UINT32_MAX;
    }

    if (m_pendingStreamingRequests.size() == 1u) {
        m_pendingStreamingRequests.pop_back();
        return true;
    }

    m_pendingStreamingRequests.front() = m_pendingStreamingRequests.back();
    m_pendingStreamingRequests.pop_back();
    m_pendingStreamingRequestHeapIndexByGroup[m_pendingStreamingRequests.front().request.groupGlobalIndex] = 0u;

    uint32_t index = 0u;
    for (;;) {
        const uint32_t left = index * 2u + 1u;
        const uint32_t right = left + 1u;
        uint32_t best = index;
        if (left < m_pendingStreamingRequests.size() && higherPriority(left, best)) {
            best = left;
        }
        if (right < m_pendingStreamingRequests.size() && higherPriority(right, best)) {
            best = right;
        }
        if (best == index) {
            break;
        }
        swapEntries(index, best);
        index = best;
    }

    return true;
}

void CLodStreamingSystem::SetGroupUsesPinnedStorage(uint32_t groupIndex, bool usesPinnedStorage) {
    if (usesPinnedStorage) {
        m_groupsUsingPinnedStorage.insert(groupIndex);
        return;
    }

    m_groupsUsingPinnedStorage.erase(groupIndex);
}

void CLodStreamingSystem::ApplyDiskStreamingCompletions(MeshManager* meshManager) {
    ZoneScopedN("CLodStreamingSystem::ApplyDiskStreamingCompletions");

    if (meshManager == nullptr) {
        return;
    }

    std::vector<MeshManager::CLodDiskStreamingCompletion> completions;
    {
        ZoneScopedN("CLodStreamingSystem::ApplyDiskStreamingCompletions::DrainCompletions");
        meshManager->DrainCompletedCLodDiskStreamingGroups(completions);
    }

    {
        ZoneScopedN("CLodStreamingSystem::ApplyDiskStreamingCompletions::ApplyCompletions");
        for (auto& completion : completions) {
            const uint32_t groupIndex = completion.groupGlobalIndex;
            if (groupIndex >= m_streamingStorageGroupCapacity) {
                continue;
            }

            ClearStreamingRequestInProgress(groupIndex);
            ClearPendingLoadPriority(groupIndex);

            auto preAllocIt = m_preAllocatedPagesByGroup.find(groupIndex);

            if (completion.success) {
                if (preAllocIt != m_preAllocatedPagesByGroup.end()) {
                    const bool usesPinnedStorage = preAllocIt->second.usesPinnedStorage;
                    {
                        ZoneScopedN("CLodStreamingSystem::ApplyDiskStreamingCompletions::AssignPagesToGroup");
                        AssignPagesToGroup(groupIndex, preAllocIt->second);
                    }
                    m_preAllocatedPagesByGroup.erase(preAllocIt);

                    // Pin newly-assigned pages until GPU readback confirms use.
                    if (!usesPinnedStorage) {
                        ZoneScopedN("CLodStreamingSystem::ApplyDiskStreamingCompletions::PinReadbackGapPages");
                        auto ownedIt = m_groupOwnedPages.find(groupIndex);
                        if (ownedIt != m_groupOwnedPages.end()) {
                            for (uint32_t page : ownedIt->second) {
                                if (page != ~0u) {
                                    m_pageLru.Pin(page);
                                }
                            }
                        }
                        m_readbackGapPinnedGroups[groupIndex] = m_readbackGeneration;
                    }
                }

                TouchGroupPages(groupIndex);
                InstallPrefetchedChildGroupLayouts(groupIndex, std::move(completion.prefetchedChildLayouts));
                m_pendingResidencyCommitGroups.insert(groupIndex);
            }
            else {
                m_pendingResidencyCommitGroups.erase(groupIndex);
                if (preAllocIt != m_preAllocatedPagesByGroup.end()) {
                    ZoneScopedN("CLodStreamingSystem::ApplyDiskStreamingCompletions::ReleaseFailedPreallocation");
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
}

void CLodStreamingSystem::PollCompletedReadbackSlots() {
    ZoneScopedN("CLodStreamingSystem::PollCompletedReadbackSlots");

    ++m_streamingDiagnosticTick;

    // Drain decoded (groupIndex, priority) pairs produced by the background worker thread.
    m_readbackBatchScratch.clear();
    m_usedGroupsBatchScratch.clear();
    {
        ZoneScopedN("CLodStreamingSystem::PollCompletedReadbackSlots::SwapWorkerBatches");
        std::lock_guard lock(m_decodedReadbackMutex);
        m_readbackBatchScratch.swap(m_decodedReadbackBatch);
        m_usedGroupsBatchScratch.swap(m_decodedUsedGroupsBatch);
    }

    // Rebuild the used-groups protection bitset from the deduplicated append buffer
    if (!m_usedGroupsBatchScratch.empty()) {
        ZoneScopedN("CLodStreamingSystem::PollCompletedReadbackSlots::RebuildUsedGroupsBitset");
        std::fill(m_usedGroupsBitsCpu.begin(), m_usedGroupsBitsCpu.end(), 0u);
        for (const uint32_t groupIndex : m_usedGroupsBatchScratch) {
            const uint32_t wa = BitWordAddress(groupIndex);
            if (wa < m_usedGroupsBitsCpu.size()) {
                m_usedGroupsBitsCpu[wa] |= BitMask(groupIndex);
            }
        }
    }

    // Touch the page LRU for all GPU-reported visible groups and their parent chains.
    {
        ZoneScopedN("CLodStreamingSystem::PollCompletedReadbackSlots::TouchVisibleGroupsLru");
        for (const uint32_t word : m_lruTouchedGroupWordsScratch) {
            if (word < m_lruTouchedGroupsBitsScratch.size()) {
                m_lruTouchedGroupsBitsScratch[word] = 0u;
            }
        }
        m_lruTouchedGroupWordsScratch.clear();
        if (m_lruTouchedGroupsBitsScratch.size() < m_streamingActiveGroupsBitsCpu.size()) {
            m_lruTouchedGroupsBitsScratch.resize(m_streamingActiveGroupsBitsCpu.size(), 0u);
        }

        auto touchGroupPagesOnce = [this](uint32_t touchedGroup) {
            const uint32_t wordAddress = BitWordAddress(touchedGroup);
            if (wordAddress >= m_lruTouchedGroupsBitsScratch.size()) {
                return;
            }

            const uint32_t bitMask = BitMask(touchedGroup);
            uint32_t& touchedWord = m_lruTouchedGroupsBitsScratch[wordAddress];
            if ((touchedWord & bitMask) != 0u) {
                return;
            }
            if (touchedWord == 0u) {
                m_lruTouchedGroupWordsScratch.push_back(wordAddress);
            }
            touchedWord |= bitMask;

            auto pagesIt = m_groupOwnedPages.find(touchedGroup);
            if (pagesIt == m_groupOwnedPages.end()) {
                return;
            }

            for (uint32_t page : pagesIt->second) {
                if (page != ~0u) {
                    m_pageLru.Touch(page);
                }
            }
        };

        for (const uint32_t groupIndex : m_usedGroupsBatchScratch) {
            touchGroupPagesOnce(groupIndex);

            int32_t current = static_cast<int32_t>(groupIndex);
            for (size_t hop = 0; hop < m_streamingParentGroupByGlobal.size(); ++hop) {
                if (current < 0 || static_cast<uint32_t>(current) >= m_streamingParentGroupByGlobal.size()) {
                    break;
                }

                const int32_t parent = m_streamingParentGroupByGlobal[static_cast<uint32_t>(current)];
                if (parent < 0 || parent == current) {
                    break;
                }

                touchGroupPagesOnce(static_cast<uint32_t>(parent));
                current = parent;
            }
        }
    }

    {
        ZoneScopedN("CLodStreamingSystem::PollCompletedReadbackSlots::UnpinReadbackGapGroups");
        for (const uint32_t groupIndex : m_usedGroupsBatchScratch) {
        auto gapIt = m_readbackGapPinnedGroups.find(groupIndex);
        if (gapIt != m_readbackGapPinnedGroups.end()) {
            auto ownedIt = m_groupOwnedPages.find(groupIndex);
            if (ownedIt != m_groupOwnedPages.end()) {
                for (uint32_t page : ownedIt->second) {
                    if (page != ~0u) {
                        m_pageLru.Unpin(page);
                    }
                }
            }
            m_readbackGapPinnedGroups.erase(gapIt);
        }
        }
    }

    // Safety timeout: unpin groups that were loaded but never rendered
    // within a generous window (2x readback ring depth).
    if (!m_usedGroupsBatchScratch.empty()) {
        ZoneScopedN("CLodStreamingSystem::PollCompletedReadbackSlots::ExpireReadbackGapPins");
        ++m_readbackGeneration;
        const uint64_t maxAge = static_cast<uint64_t>(m_streamingReadbackRingSize) * 2u;
        m_expiredReadbackGapGroupsScratch.clear();
        for (const auto& [g, gen] : m_readbackGapPinnedGroups) {
            if (m_readbackGeneration - gen > maxAge) {
                m_expiredReadbackGapGroupsScratch.push_back(g);
            }
        }
        for (uint32_t g : m_expiredReadbackGapGroupsScratch) {
            auto ownedIt = m_groupOwnedPages.find(g);
            if (ownedIt != m_groupOwnedPages.end()) {
                for (uint32_t page : ownedIt->second) {
                    if (page != ~0u) {
                        m_pageLru.Unpin(page);
                    }
                }
            }
            m_readbackGapPinnedGroups.erase(g);
        }
    }

    if (m_readbackBatchScratch.empty()) {
        return;
    }

    uint32_t queuedCount = 0;
    {
        ZoneScopedN("CLodStreamingSystem::PollCompletedReadbackSlots::QueueLoadRequests");
        for (const auto& [groupIndex, priority] : m_readbackBatchScratch) {
            CLodStreamingRequest req{};
            req.groupGlobalIndex = groupIndex;
            queuedCount += QueueLoadRequestWithParents(req, priority);
        }
    }

    spdlog::info(
        "CLod streaming: drained {} decoded groups from worker, {} queued, {} LRU touches",
        static_cast<uint32_t>(m_readbackBatchScratch.size()),
        queuedCount,
        static_cast<uint32_t>(m_usedGroupsBatchScratch.size()));
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

            std::vector<std::pair<uint32_t, uint32_t>> decodedRequests;
            std::vector<uint32_t> sumSeenGroups;
            std::vector<uint32_t> decodedUsedGroups;
            uint32_t totalRequestCount = 0;
            uint32_t totalUsedGroupsCount = 0;
            const bool sortedFeedback = m_parallelSortAvailable;

            auto beginDecodeGeneration = [](uint32_t& generation, std::vector<uint32_t>& seen) {
                ++generation;
                if (generation == 0u) {
                    generation = 1u;
                    std::fill(seen.begin(), seen.end(), 0u);
                }
            };

            beginDecodeGeneration(m_decodeSeenGeneration, m_decodeSeenGenerationByGroup);
            beginDecodeGeneration(m_decodeUsedSeenGeneration, m_decodeUsedSeenGenerationByGroup);

            for (auto& slot : m_readbackStagingSlots) {
                if (!slot.inFlight || slot.fenceValue == 0 || slot.fenceValue > target) {
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

                if (requestCount > 0 && slot.requestsStaging) {
                    totalRequestCount += requestCount;

                    auto apiResource = slot.requestsStaging->GetAPIResource();
                    void* mapped = nullptr;
                    apiResource.Map(&mapped);
                    if (mapped) {
                        const auto* requests = static_cast<const CLodStreamingRequest*>(mapped);
                        for (uint32_t i = 0; i < requestCount; ++i) {
                            const uint32_t groupIndex = requests[i].groupGlobalIndex;
                            const uint32_t priority = UnpackStreamingRequestPriority(requests[i]);
                            if (groupIndex >= m_decodeSeenGenerationByGroup.size()) {
                                const size_t newSize = static_cast<size_t>(groupIndex) + 1u;
                                m_decodeSeenGenerationByGroup.resize(newSize, 0u);
                                m_decodePriorityAccumByGroup.resize(newSize, 0u);
                            }

                            if (m_priorityMode == CLodPriorityMode::Sum || !sortedFeedback) {
                                if (m_decodeSeenGenerationByGroup[groupIndex] != m_decodeSeenGeneration) {
                                    m_decodeSeenGenerationByGroup[groupIndex] = m_decodeSeenGeneration;
                                    m_decodePriorityAccumByGroup[groupIndex] = priority;
                                    sumSeenGroups.push_back(groupIndex);
                                }
                                else if (m_priorityMode == CLodPriorityMode::Sum) {
                                    m_decodePriorityAccumByGroup[groupIndex] += priority;
                                }
                                else {
                                    m_decodePriorityAccumByGroup[groupIndex] = std::max(m_decodePriorityAccumByGroup[groupIndex], priority);
                                }
                            }
                            else if (m_decodeSeenGenerationByGroup[groupIndex] != m_decodeSeenGeneration) {
                                // Sorted feedback is highest-priority first, so the first occurrence wins.
                                m_decodeSeenGenerationByGroup[groupIndex] = m_decodeSeenGeneration;
                                decodedRequests.emplace_back(groupIndex, priority);
                            }
                        }
                        apiResource.Unmap(0, 0);
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
                    totalUsedGroupsCount += usedGroupsCount;
                    auto apiResource = slot.usedGroupsBufferStaging->GetAPIResource();
                    void* mapped = nullptr;
                    apiResource.Map(&mapped);
                    if (mapped) {
                        const auto* usedGroups = static_cast<const uint32_t*>(mapped);
                        for (uint32_t i = 0; i < usedGroupsCount; ++i) {
                            const uint32_t groupIndex = usedGroups[i];
                            if (groupIndex >= m_decodeUsedSeenGenerationByGroup.size()) {
                                m_decodeUsedSeenGenerationByGroup.resize(static_cast<size_t>(groupIndex) + 1u, 0u);
                            }
                            if (m_decodeUsedSeenGenerationByGroup[groupIndex] != m_decodeUsedSeenGeneration) {
                                m_decodeUsedSeenGenerationByGroup[groupIndex] = m_decodeUsedSeenGeneration;
                                decodedUsedGroups.push_back(groupIndex);
                            }
                        }
                        apiResource.Unmap(0, 0);
                    }
                }

                slot.inFlight = false;
            }

            if (!sumSeenGroups.empty()) {
                decodedRequests.reserve(decodedRequests.size() + sumSeenGroups.size());
                for (uint32_t groupIndex : sumSeenGroups) {
                    decodedRequests.emplace_back(groupIndex, m_decodePriorityAccumByGroup[groupIndex]);
                }
                std::sort(
                    decodedRequests.begin(),
                    decodedRequests.end(),
                    [](const auto& a, const auto& b) {
                        return a.second > b.second;
                    });
            }

            {
                std::lock_guard decodedLock(m_decodedReadbackMutex);
                m_decodedReadbackBatch.reserve(m_decodedReadbackBatch.size() + decodedRequests.size());
                m_decodedUsedGroupsBatch.reserve(m_decodedUsedGroupsBatch.size() + decodedUsedGroups.size());

                // Push cross-slot deduplicated results for the main thread.
                for (const auto& [groupIndex, priority] : decodedRequests) {
                    m_decodedReadbackBatch.emplace_back(groupIndex, priority);
                }
                for (const uint32_t g : decodedUsedGroups) {
                    m_decodedUsedGroupsBatch.push_back(g);
                }
            }
        }

        lastProcessed = target;
    }
}

void CLodStreamingSystem::ProcessStreamingRequestsBudgeted() {
    ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted");

    const uint32_t budget = std::max(m_streamingCpuUploadBudgetRequests, 1u);
    CLodStreamingOperationStats frameStats{};

    MeshManager* meshManager = nullptr;
    if (m_getMeshManager) {
        meshManager = m_getMeshManager();
    }

    if (meshManager != nullptr) {
        ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::StreamingMaintenance");
        {
            ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::StreamingMaintenance::InitializePageLru");
            InitializePageLru(meshManager);
        }
        {
            ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::StreamingMaintenance::CommitPendingResidencyPromotions");
            CommitPendingResidencyPromotions();
        }
        {
            ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::StreamingMaintenance::ProcessDiskStreamingIO");
            meshManager->ProcessCLodDiskStreamingIO(budget);
        }
        {
            ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::StreamingMaintenance::ApplyDiskStreamingCompletions");
            ApplyDiskStreamingCompletions(meshManager);
        }
    }

    auto* pool = meshManager ? meshManager->GetCLodPagePool() : nullptr;
    const uint64_t pageSize = pool ? pool->GetPageSize() : 0u;

    struct QueuedStreamingCandidate {
        uint32_t groupIndex = 0u;
        MeshManager::CLodGroupDiskIOBatchRequest request;
    };
    std::vector<QueuedStreamingCandidate> diskIoBatch;
    diskIoBatch.reserve(budget);

    uint32_t processed = 0;
    {
        ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::SelectAndPrepareRequests");
        while (processed < budget && !m_pendingStreamingRequests.empty()) {
            PendingStreamingRequest pending{};
            {
                ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::PopPendingRequest");
                if (!PopHighestPriorityPendingStreamingRequest(pending)) {
                    break;
                }
            }

            const uint32_t groupIndex = pending.request.groupGlobalIndex;
            const uint32_t priority = pending.priority;
            {
                ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::ValidatePendingRequest");
                if (groupIndex >= m_streamingStorageGroupCapacity) {
                    EnsureStreamingStorageCapacity(groupIndex + 1u);
                }

                if (groupIndex >= m_streamingRequestStateByGroup.size()
                    || m_streamingRequestStateByGroup[groupIndex] != StreamingRequestState::PendingCpu
                    || priority != GetPendingLoadPriority(groupIndex)
                    || groupIndex >= m_pendingStreamingRequestGenerationByGroup.size()
                    || pending.generation != m_pendingStreamingRequestGenerationByGroup[groupIndex]) {
                    continue;
                }
            }

            // Load path
            frameStats.loadRequested++;
            frameStats.loadUnique++;

            {
                ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::ActiveResidentChecks");
                // Skip groups that are no longer in the active domain.
                if (!IsGroupActive(groupIndex)) {
                    ClearStreamingRequestInProgress(groupIndex);
                    ClearPendingLoadPriority(groupIndex);
                    processed++;
                    continue;
                }

                if (IsGroupResident(groupIndex)) {
                    TouchGroupPages(groupIndex);
                    ClearStreamingRequestInProgress(groupIndex);
                    ClearPendingLoadPriority(groupIndex);
                    processed++;
                    continue;
                }
            }

            if (meshManager == nullptr) {
                ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::CpuFallbackCommit");
                const uint32_t wordAddress = BitWordAddress(groupIndex);
                const uint32_t bitMask = BitMask(groupIndex);
                const uint32_t oldWord = m_streamingNonResidentBitsCpu[wordAddress];
                m_streamingNonResidentBitsCpu[wordAddress] &= ~bitMask;
                if (m_streamingNonResidentBitsCpu[wordAddress] != oldWord) {
                    frameStats.loadApplied++;
                    m_streamingResidentGroupsCount++;
                    MarkStreamingNonResidentBitsDirtyWord(wordAddress);
                }
                ClearStreamingRequestInProgress(groupIndex);
                ClearPendingLoadPriority(groupIndex);
                processed++;
                continue;
            }

            // Pre-allocate pages (popping from LRU, evicting as needed).
            if (m_preAllocatedPagesByGroup.count(groupIndex) == 0u && pool && pageSize > 0) {
                ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::SerialPagePreallocate");
                const auto info = meshManager->GetCLodGroupStreamingInfo(groupIndex);
                const uint32_t pagesNeeded = info.valid
                    ? CLodEstimatePagesNeeded(info.hint, info.vertexByteSize, pageSize)
                    : 1u;

                auto preAlloc = PreAllocatePagesForGroup(groupIndex, pagesNeeded, meshManager);
                if (preAlloc.segmentCount == 0) {
                    frameStats.loadFailed++;
                    ClearStreamingRequestInProgress(groupIndex);
                    ClearPendingLoadPriority(groupIndex);
                    processed++;
                    continue;
                }

                // Verify we got all the pages we asked for.  A short return
                // means PopFreePages hit used (on-screen) groups and stopped
                // early - we're exceeding the on-screen geometry budget.
                {
                    ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::VerifyPreallocatedPages");
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
                        ClearPendingLoadPriority(groupIndex);
                        frameStats.loadFailed++;
                        processed++;
                        continue;
                    }
                }

                m_preAllocatedPagesByGroup[groupIndex] = std::move(preAlloc);
            }

            {
                ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::BuildDiskIoCandidate");
                // Build segment fetch mask and page ID list from pre-allocated pages.
                const std::vector<bool> emptySegmentNeedsFetch;
                const std::vector<uint32_t> emptyPreAllocPageIDs;
                const std::vector<bool>* segmentNeedsFetch = &emptySegmentNeedsFetch;
                const std::vector<uint32_t>* preAllocPageIDs = &emptyPreAllocPageIDs;
                const CLodCache::GroupPayloadLayoutMetadata* prefetchedLayout = nullptr;
                auto paIt = m_preAllocatedPagesByGroup.find(groupIndex);
                if (paIt != m_preAllocatedPagesByGroup.end()) {
                    const auto& pa = paIt->second;
                    segmentNeedsFetch = &pa.segmentNeedsFetch;
                    preAllocPageIDs = &pa.pagesBySegment;
                }

                auto prefetchedIt = m_prefetchedChildLayoutsByGroup.find(groupIndex);
                if (prefetchedIt != m_prefetchedChildLayoutsByGroup.end() && prefetchedIt->second.layout.IsValid()) {
                    prefetchedLayout = &prefetchedIt->second.layout;
                    spdlog::debug(
                        "CLod streaming: queueing group {} with prefetched child header metadata from owner {}",
                        groupIndex,
                        prefetchedIt->second.ownerGroupIndex);
                }

                QueuedStreamingCandidate candidate{};
                candidate.groupIndex = groupIndex;
                candidate.request.groupGlobalIndex = groupIndex;
                candidate.request.segmentNeedsFetch = *segmentNeedsFetch;
                candidate.request.preAllocatedPages = *preAllocPageIDs;
                candidate.request.priority = priority;
                if (prefetchedLayout != nullptr && prefetchedLayout->IsValid()) {
                    candidate.request.prefetchedLayout = *prefetchedLayout;
                }
                if (meshManager->IsCLodStreamingDirectStorageEnabled()) {
                    auto childIt = m_childGroupsByGlobal.find(groupIndex);
                    if (childIt != m_childGroupsByGlobal.end() && !childIt->second.empty()) {
                        candidate.request.childLayoutPrefetchGroups = childIt->second;
                    }
                }
                diskIoBatch.push_back(std::move(candidate));
            }

            processed++;
        }
    }

    if (meshManager != nullptr && !diskIoBatch.empty()) {
        ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::QueueDiskIoBatch");

        std::vector<MeshManager::CLodGroupDiskIOBatchRequest> batchRequests;
        {
            ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::QueueDiskIoBatch::BuildRequests");
            batchRequests.reserve(diskIoBatch.size());
            for (const auto& candidate : diskIoBatch) {
                batchRequests.push_back(candidate.request);
            }
        }

        std::vector<bool> queuedByRequest;
        {
            ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::QueueDiskIoBatch::Submit");
            meshManager->QueueCLodGroupDiskIOBatch(batchRequests, &queuedByRequest);
        }

        {
            ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::QueueDiskIoBatch::ApplyResults");
            for (uint32_t i = 0; i < static_cast<uint32_t>(diskIoBatch.size()); ++i) {
                const uint32_t groupIndex = diskIoBatch[i].groupIndex;
                const bool queued = i < queuedByRequest.size() && queuedByRequest[i];
                if (queued) {
                    MarkStreamingRequestDiskIo(groupIndex);
                    continue;
                }

                frameStats.loadFailed++;
                auto paIt = m_preAllocatedPagesByGroup.find(groupIndex);
                if (paIt != m_preAllocatedPagesByGroup.end()) {
                    ReleasePreAllocatedPages(paIt->second, meshManager);
                    m_preAllocatedPagesByGroup.erase(paIt);
                }
                ClearStreamingRequestInProgress(groupIndex);
                ClearPendingLoadPriority(groupIndex);
            }
        }
    }

    if (meshManager != nullptr) {
        ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::CollectDebugStats");
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

    {
        ZoneScopedN("CLodStreamingSystem::ProcessStreamingRequestsBudgeted::PublishStats");
        PublishCLodStreamingOperationStats(frameStats);
    }
}
