#include "Managers/Singletons/DeviceManager.h"

static std::string AutoBreadcrumbOpToString(D3D12_AUTO_BREADCRUMB_OP op) {
    switch (op) {
    case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
    case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "ExecuteIndirect";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
    case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "CopyBufferRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE: return "ResolveSubresource";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return "ClearRenderTargetView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW: return "ClearDepthStencilView";
    case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE: return "ExecuteBundle";
    case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "ResolveQueryData";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION: return "BeginSubmission";
    case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION: return "EndSubmission";
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME: return "DecodeFrame";
    case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES: return "ProcessFrames";
    case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT: return "AtomicCopyBufferUINT";
    case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64: return "AtomicCopyBufferUINT64";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION: return "ResolveSubresourceRegion";
    case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE: return "WriteBufferImmediate";
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1: return "DecodeFrame1";
    case D3D12_AUTO_BREADCRUMB_OP_SETPROTECTEDRESOURCESESSION: return "SetProtectedResourceSession";
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME2: return "DecodeFrame2";
    case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1: return "ProcessFrames1";
    case D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE: return "BuildRaytracingAccelerationStructure";
    case D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO: return "EmitRaytracingAccelerationStructurePostBuildInfo";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS: return "DispatchRays";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW: return "ClearUnorderedAccessView";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCHMESH: return "DispatchMesh";
    case D3D12_AUTO_BREADCRUMB_OP_BARRIER: return "Barrier";
    default: return "UnknownOp";
    }
}
static const char* DredAllocationTypeToString(D3D12_DRED_ALLOCATION_TYPE type) {
    switch (type) {
    case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE: return "COMMAND_QUEUE";
    case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR: return "COMMAND_ALLOCATOR";
    case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE: return "PIPELINE_STATE";
    case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST: return "COMMAND_LIST";
    case D3D12_DRED_ALLOCATION_TYPE_FENCE: return "FENCE";
    case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP: return "DESCRIPTOR_HEAP";
    case D3D12_DRED_ALLOCATION_TYPE_HEAP: return "HEAP";
    case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP: return "QUERY_HEAP";
    case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE: return "COMMAND_SIGNATURE";
    case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY: return "PIPELINE_LIBRARY";
    case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER: return "VIDEO_DECODER";
    case D3D12_DRED_ALLOCATION_TYPE_VIDEO_PROCESSOR: return "VIDEO_PROCESSOR";
    case D3D12_DRED_ALLOCATION_TYPE_RESOURCE: return "RESOURCE";
    case D3D12_DRED_ALLOCATION_TYPE_PASS: return "PASS";
    case D3D12_DRED_ALLOCATION_TYPE_PROTECTEDRESOURCESESSION: return "PROTECTEDRESOURCESESSION";
    case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSION: return "CRYPTOSESSION";
    case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSIONPOLICY: return "CRYPTOSESSIONPOLICY";
    case D3D12_DRED_ALLOCATION_TYPE_COMMAND_POOL: return "COMMAND_POOL";
    case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT: return "STATE_OBJECT";
    case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND: return "METACOMMAND";
    case D3D12_DRED_ALLOCATION_TYPE_SCHEDULINGGROUP: return "SCHEDULINGGROUP";
    case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_ESTIMATOR: return "VIDEO_MOTION_ESTIMATOR";
    case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_VECTOR_HEAP: return "VIDEO_MOTION_VECTOR_HEAP";
    case D3D12_DRED_ALLOCATION_TYPE_VIDEO_EXTENSION_COMMAND: return "VIDEO_EXTENSION_COMMAND";
    case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER_HEAP: return "VIDEO_DECODER_HEAP";
    case D3D12_DRED_ALLOCATION_TYPE_COMMAND_RECORDER: return "COMMAND_RECORDER";
    default: return "UNKNOWN";
    }
}
void LogBreadcrumbs(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT& breadcrumbs) {
    const D3D12_AUTO_BREADCRUMB_NODE* pNode = breadcrumbs.pHeadAutoBreadcrumbNode;
    while (pNode) {
        std::wstring commandListName = pNode->pCommandListDebugNameW ? pNode->pCommandListDebugNameW : L"<unnamed>";
        std::wstring commandQueueName = pNode->pCommandQueueDebugNameW ? pNode->pCommandQueueDebugNameW : L"<unnamed>";

        spdlog::info("--- AutoBreadcrumb Node ---");
        spdlog::info("Command List: {}", std::string(commandListName.begin(), commandListName.end()));
        spdlog::info("Command Queue: {}", std::string(commandQueueName.begin(), commandQueueName.end()));
        spdlog::info("Breadcrumb Count: {}", pNode->BreadcrumbCount);
        spdlog::info("Operations:");

        for (UINT i = 0; i < pNode->BreadcrumbCount; ++i) {
            D3D12_AUTO_BREADCRUMB_OP op = pNode->pCommandHistory[i];
            spdlog::info("  [{}]: {}", i, AutoBreadcrumbOpToString(op));
        }

        pNode = pNode->pNext;
    }
}

void LogPageFaults(const D3D12_DRED_PAGE_FAULT_OUTPUT& pageFault) {
    if (pageFault.PageFaultVA == 0 &&
        pageFault.pHeadExistingAllocationNode == nullptr &&
        pageFault.pHeadRecentFreedAllocationNode == nullptr)
    {
        spdlog::info("No page fault details available.");
        return;
    }

    spdlog::info("--- Page Fault Details ---");
    // Use spdlog's format specifiers instead of << operators
    spdlog::info("PageFault VA: 0x{:X}", pageFault.PageFaultVA);

    auto LogAllocationNodes = [&](const D3D12_DRED_ALLOCATION_NODE* pNode, const char* nodeType) {
        const D3D12_DRED_ALLOCATION_NODE* current = pNode;
        while (current) {
            std::wstring objName = current->ObjectNameW ? current->ObjectNameW : L"<unnamed>";

            const char* allocTypeStr = DredAllocationTypeToString(current->AllocationType);
            spdlog::info("[{}] ObjectName: {}, AllocationType: {}",
                nodeType,
                std::string(objName.begin(), objName.end()),
                allocTypeStr
            );
            current = current->pNext;
        }
        };

    LogAllocationNodes(pageFault.pHeadExistingAllocationNode, "ExistingAllocation");
    LogAllocationNodes(pageFault.pHeadRecentFreedAllocationNode, "RecentFreedAllocation");
}

void DeviceManager::DiagnoseDeviceRemoval() {
    //HRESULT reason = device->GetDeviceRemovedReason();
    //spdlog::error("Device removed reason: 0x{:X}", static_cast<unsigned>(reason));

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
        D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};

        HRESULT br = dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
        HRESULT pr = dred->GetPageFaultAllocationOutput(&pageFault);

        if (SUCCEEDED(br)) {
            LogBreadcrumbs(breadcrumbs);
        }
        else {
            spdlog::warn("Failed to get breadcrumbs output.");
        }

        if (SUCCEEDED(pr)) {
            LogPageFaults(pageFault);
        }
        else {
            spdlog::warn("Failed to get page fault output.");
        }
}