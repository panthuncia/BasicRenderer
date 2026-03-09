#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Managers/MeshManager.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Resources/Buffers/Buffer.h"

class CLodStreamingSystem {
public:
    CLodStreamingSystem();

    void OnRegistryReset(ResourceRegistry* reg);
    void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses);
    void GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses);

private:
    struct StreamingRequestReadbackState {
        bool pending = false;
        uint64_t captureId = 0;
        uint64_t sampleId = 0;
        bool hasCounter = false;
        bool hasRequests = false;
        uint32_t requestCount = 0;
        std::vector<CLodStreamingRequest> requests;
    };

    struct StreamingServiceSummary {
        uint32_t requested = 0;
        uint32_t unique = 0;
        uint32_t applied = 0;
        uint32_t failed = 0;
    };

    struct PendingStreamingRequest {
        bool isLoad = true;
        CLodStreamingRequest request{};
        uint32_t priority = 0u;
    };

    struct StreamingLruEntry {
        uint32_t groupIndex = 0u;
        uint64_t stamp = 0u;
    };

    struct PendingLoadBatchEntry {
        uint32_t groupIndex = 0u;
        uint32_t wordAddress = 0u;
        uint32_t bitMask = 0u;
        uint32_t oldWord = 0u;
    };

    static uint32_t BitWordAddress(uint32_t key);
    static uint32_t BitMask(uint32_t key);
    static uint32_t UnpackStreamingRequestPriority(const CLodStreamingRequest& req);

    bool IsGroupPinned(uint32_t groupIndex) const;
    bool IsGroupActive(uint32_t groupIndex) const;
    bool IsGroupResident(uint32_t groupIndex) const;
    void TouchStreamingLru(uint32_t groupIndex);
    bool TryQueuePendingLoadRequest(const CLodStreamingRequest& req, uint32_t priority);
    uint32_t QueueLoadRequestWithParents(const CLodStreamingRequest& requestedLoad, uint32_t requestedPriority);
    bool TryEvictLruVictim(uint32_t avoidGroup, CLodStreamingOperationStats& frameStats);
    void EnsureStreamingStorageCapacity(uint32_t requiredGroupCount);
    void RefreshStreamingActiveGroupDomain();
    bool IsStreamingRequestInProgress(uint32_t groupIndex) const;
    void MarkStreamingRequestInProgress(uint32_t groupIndex);
    void ClearStreamingRequestInProgress(uint32_t groupIndex);
    bool ApplyEvictionRequest(const CLodStreamingRequest& req, bool& outResidencyBitChanged);
    void ApplyDiskStreamingCompletions(MeshManager* meshManager);
    StreamingRequestReadbackState* FindStreamingLoadReadbackStateByCaptureId(uint64_t captureId);
    void TryFinalizeStreamingLoadReadback(StreamingRequestReadbackState& state);
    void ScheduleStreamingReadbacks();
    void ProcessStreamingRequestsBudgeted();

    std::shared_ptr<Buffer> m_streamingNonResidentBits;
    std::shared_ptr<Buffer> m_streamingActiveGroupsBits;
    std::shared_ptr<Buffer> m_streamingLoadRequestBits;
    std::shared_ptr<Buffer> m_streamingLoadRequests;
    std::shared_ptr<Buffer> m_streamingLoadCounter;
    std::shared_ptr<Buffer> m_streamingRuntimeState;

    std::vector<uint32_t> m_streamingNonResidentBitsCpu;
    std::vector<uint32_t> m_streamingActiveGroupsBitsCpu;
    std::vector<uint32_t> m_streamingPinnedGroupsBitsCpu;
    std::vector<uint32_t> m_streamingEvictionExemptBitsCpu;
    std::vector<uint32_t> m_streamingResidencyInitializedBitsCpu;
    std::vector<uint32_t> m_streamingRequestsInProgressBitsCpu;
    std::vector<uint32_t> m_pendingLoadPriorityByGroup;
    std::vector<int32_t> m_streamingParentGroupByGlobal;
    std::vector<uint64_t> m_streamingLruCurrentStampByGroup;
    std::deque<StreamingLruEntry> m_streamingLruEntries;
    uint64_t m_streamingLruSerial = 0u;
    uint32_t m_streamingResidentGroupsCount = 0u;
    uint32_t m_streamingActiveGroupScanCount = 0u;
    uint32_t m_streamingStorageGroupCapacity = CLodStreamingInitialGroupCapacity;
    bool m_streamingNonResidentBitsUploadPending = false;
    uint32_t m_streamingReadbackRingSize = 3u;
    uint32_t m_streamingReadbackScheduleCursor = 0u;
    uint32_t m_streamingCpuUploadBudgetRequests = 0u;
    uint32_t m_streamingResidentBudgetGroups = 0u;
    rg::runtime::IReadbackService* m_streamingReadbackService = nullptr;
    std::function<MeshManager*()> m_getMeshManager = []() { return nullptr; };

    uint64_t m_streamingReadbackCaptureSerial = 0;
    uint64_t m_streamingOperationSampleSerial = 0;
    std::vector<StreamingRequestReadbackState> m_streamingLoadReadbackSlots;
    std::vector<PendingStreamingRequest> m_pendingStreamingRequests;
    bool m_streamingDomainDirty = true;

    MeshManager::CLodStreamingDomainSnapshot m_cachedDomainSnapshot;
    std::vector<MeshManager::CLodGlobalResidencyRequest> m_initRequests;
    std::vector<uint32_t> m_initRequestWordAddresses;
    std::vector<uint32_t> m_initRequestBitMasks;
    std::vector<uint8_t> m_initRequestPinnedFlags;
    std::vector<MeshManager::CLodGlobalResidencyRequest> m_loadBatchRequests;
    std::vector<PendingLoadBatchEntry> m_loadBatchEntries;
};
