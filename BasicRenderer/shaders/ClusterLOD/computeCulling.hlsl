#define CLOD_COMPUTE_INCLUDE_ONLY 1
#include "ClusterLOD/workGraphCulling.hlsl"

[numthreads(1, 1, 1)]
void BuildPureComputeDispatchArgsCS()
{
    StructuredBuffer<uint> counterBuffer = ResourceDescriptorHeap[CLOD_PC_DISPATCH_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint3> dispatchArgs = ResourceDescriptorHeap[CLOD_PC_DISPATCH_ARGS_DESCRIPTOR_INDEX];
    const uint count = min(counterBuffer[0], CLOD_WG_VISIBLE_CLUSTERS_CAPACITY);
    const uint threadsPerGroup = max(1u, CLOD_PC_DISPATCH_THREADS_PER_GROUP);
    const uint groups = (count == 0u) ? 1u : ((count + threadsPerGroup - 1u) / threadsPerGroup);
    dispatchArgs[0] = uint3(groups, 1u, 1u);
}

[numthreads(64, 1, 1)]
void PureComputeObjectCullCS(const uint3 vDispatchThreadID : SV_DispatchThreadID)
{
    const uint drawIndex = vDispatchThreadID.x;
    const bool inRange = (drawIndex < CLOD_PC_OBJECT_CULL_ACTIVE_DRAW_COUNT);

    WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_THREADS, 1);
    if (inRange) {
        WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_IN_RANGE_THREADS, 1);
    }

    if (!inRange) {
        return;
    }

    StructuredBuffer<uint> activeDrawSetIndicesBuffer =
        ResourceDescriptorHeap[CLOD_PC_OBJECT_CULL_ACTIVE_DRAW_SET_SRV_INDEX];
    StructuredBuffer<DispatchMeshIndirectCommand> indirectCommandBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::IndirectCommandBuffers::Master)];

    const uint drawcallIndex = activeDrawSetIndicesBuffer[drawIndex];
    const uint perMeshInstanceBufferIndex = indirectCommandBuffer[drawcallIndex].perMeshInstanceBufferIndex;

    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    const PerMeshInstanceBuffer instanceData = perMeshInstanceBuffer[perMeshInstanceBufferIndex];

    StructuredBuffer<PerObjectBuffer> perObjectBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    const row_major matrix objectModelMatrix = perObjectBuffer[instanceData.perObjectBufferIndex].model;

    StructuredBuffer<Camera> cameras =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    const Camera camera = cameras[CLOD_PC_OBJECT_CULL_VIEW_DATA_INDEX];

    const float3 objectSpaceCenter = instanceData.boundingSphere.sphere.xyz;
    const float3 viewSpaceCenter = ToViewSpace(objectSpaceCenter, objectModelMatrix, camera.view);
    const float worldRadius = instanceData.boundingSphere.sphere.w * MaxAxisScale_RowVector(objectModelMatrix);

    bool culled = false;
    if (any(isnan(viewSpaceCenter)) || any(isinf(viewSpaceCenter)) || isnan(worldRadius) || isinf(worldRadius)) {
        WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_INVALID_BOUNDS, 1);
        culled = true;
    }
    else {
        [unroll]
        for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
        {
            const float4 plane = camera.clippingPlanes[planeIndex].plane;
            const float distanceToPlane = dot(plane.xyz, viewSpaceCenter) + plane.w;
            if (distanceToPlane < -worldRadius)
            {
                WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_REJECTED_FRUSTUM, 1);
                WGTelemetryAddObjectCullPlaneReject(planeIndex);
                culled = true;
                break;
            }
        }
    }

    if (culled) {
        return;
    }

    StructuredBuffer<MeshInstanceClodOffsets> meshInstanceClodOffsets =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
    StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];

    const MeshInstanceClodOffsets off = meshInstanceClodOffsets[perMeshInstanceBufferIndex];
    const CLodMeshMetadata clodMeshMetadata = clodMeshMetadataBuffer[off.clodMeshMetadataIndex];

    RWStructuredBuffer<TraverseNodeRecord> outFrontier = ResourceDescriptorHeap[CLOD_PC_FRONTIER_OUTPUT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> outCounter = ResourceDescriptorHeap[CLOD_PC_FRONTIER_OUTPUT_COUNT_DESCRIPTOR_INDEX];

    uint outputIndex = 0u;
    InterlockedAdd(outCounter[0], 1u, outputIndex);
    if (outputIndex >= CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
        return;
    }

    outFrontier[outputIndex].viewId = CLOD_PC_OBJECT_CULL_VIEW_DATA_INDEX;
    outFrontier[outputIndex].instanceIndex = perMeshInstanceBufferIndex;
    outFrontier[outputIndex].nodeIdPacked = PackTraverseNodeId(clodMeshMetadata.rootNode, CLOD_RECORD_SOURCE_PASS1, 1u);

    WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_VISIBLE_THREADS, 1);
    WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_TRAVERSE_RECORDS, 1);
}

[numthreads(64, 1, 1)]
void PureComputeTraverseFrontierCS(const uint3 dispatchThreadID : SV_DispatchThreadID)
{
    StructuredBuffer<TraverseNodeRecord> inputFrontier = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> inputCountBuffer = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<TraverseNodeRecord> nextFrontier = ResourceDescriptorHeap[CLOD_PC_FRONTIER_OUTPUT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> nextCounter = ResourceDescriptorHeap[CLOD_PC_FRONTIER_OUTPUT_COUNT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<MeshletBucketRecord> clusterFrontier = ResourceDescriptorHeap[CLOD_PC_CLUSTER_OUTPUT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> clusterCounter = ResourceDescriptorHeap[CLOD_PC_CLUSTER_OUTPUT_COUNT_DESCRIPTOR_INDEX];

    const uint inputCount = inputCountBuffer[0];
    const uint index = dispatchThreadID.x;
    WGTelemetryAdd(WG_COUNTER_TRAVERSE_THREADS, 1);
    if (index >= inputCount) {
        return;
    }

    const TraverseNodeRecord rec = inputFrontier[index];
    const bool parentAllowsRefine = (UnpackAllowRefine(rec.nodeIdPacked) != 0u);
    if (UnpackSourceTag(rec.nodeIdPacked) == CLOD_RECORD_SOURCE_REPLAY) {
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_TRAVERSE_RECORDS_CONSUMED, 1);
    }
    const bool replaySource = (UnpackSourceTag(rec.nodeIdPacked) == CLOD_RECORD_SOURCE_REPLAY);

    StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
    StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];
    const MeshInstanceClodOffsets off = clodOffsets[rec.instanceIndex];
    const CLodMeshMetadata clodMeshMetadata = clodMeshMetadataBuffer[off.clodMeshMetadataIndex];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    const PerMeshInstanceBuffer instanceData = perMeshInstanceBuffer[rec.instanceIndex];
    const uint objectBufferIndex = instanceData.perObjectBufferIndex;
    StructuredBuffer<PerMeshBuffer> perMeshBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    const PerMeshBuffer perMesh = perMeshBuffer[instanceData.perMeshBufferIndex];
    const bool isSkinned = (perMesh.vertexFlags & VERTEX_SKINNED) != 0u;
    StructuredBuffer<PerObjectBuffer> perObjectBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    const row_major matrix objectModelMatrix = perObjectBuffer[objectBufferIndex].model;
    StructuredBuffer<Camera> cameras =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    const uint cullViewId = rec.viewId;
    const uint lodViewId = CLodResolveLodViewId(cullViewId);
    const Camera cullCamera = cameras[cullViewId];
    const Camera lodCamera = cameras[lodViewId];
    StructuredBuffer<CullingCameraInfo> cameraInfos =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    const CullingCameraInfo lodCam = cameraInfos[lodViewId];
    StructuredBuffer<ClusterLODGroup> groups =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
    StructuredBuffer<ClusterLODNode> lodNodes =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Nodes)];

    const ClusterLODNode node = lodNodes[clodMeshMetadata.lodNodesBase + UnpackNodeId(rec.nodeIdPacked)];

    if (node.range.isLeaf == 0) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_INTERNAL_NODE_RECORDS, 1);
    }
    else {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_LEAF_NODE_RECORDS, 1);
    }

    const float objectUniformScale = MaxAxisScale_RowVector(objectModelMatrix);
    const float cullUniformScale = objectUniformScale;
    const float lodUniformScale = objectUniformScale;
    const float3 nodeCullCenterObjectSpace = isSkinned ? instanceData.boundingSphere.sphere.xyz : node.metric.cullCenterAndRadius.xyz;
    const float nodeCullRadiusObjectSpace = isSkinned ? instanceData.boundingSphere.sphere.w : node.metric.cullCenterAndRadius.w;
    const float3 nodeLodCenterObjectSpace = node.metric.lodCenterAndRadius.xyz;
    const float nodeLodRadiusObjectSpace = node.metric.lodCenterAndRadius.w;
    const float3 nodeCenterViewSpace = ToViewSpace(nodeCullCenterObjectSpace, objectModelMatrix, cullCamera.view);
    const float nodeRadiusWorld = nodeCullRadiusObjectSpace * cullUniformScale;
    const bool nodeCulled = !replaySource && SphereOutsideFrustumViewSpace(nodeCenterViewSpace, nodeRadiusWorld, cullCamera);

    if (nodeCulled) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_CULLED_NODE_RECORDS, 1);
        return;
    }

    if (node.range.isLeaf != 0) {
        const uint groupGlobalIndex = clodMeshMetadata.groupsBase + node.range.ownerGroupId;
        const ClusterLODGroup grp = groups[groupGlobalIndex];

        const float3 grpWorldCenter = mul(float4(grp.bounds.centerAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
        const float grpWorldRadius = grp.bounds.centerAndRadius.w * lodUniformScale;
        const float grpEOD = ProjectedGeometricError(
            grpWorldCenter, grpWorldRadius,
            grp.bounds.error, lodUniformScale,
            lodCam.positionWorldSpace.xyz, lodCam.zNear,
            lodCamera.isOrtho);
        const bool nodeWantsTraversal = parentAllowsRefine && (grpEOD >= lodCam.errorOverDistanceThreshold);

        if (!nodeWantsTraversal) {
            WGTelemetryAdd(WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS, 1);
            return;
        }

        StructuredBuffer<ClusterLODGroupSegment> segments =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Segments)];
        const uint segGlobalIndex = clodMeshMetadata.segmentsBase + node.range.indexOrOffset;
        const ClusterLODGroupSegment seg = segments[segGlobalIndex];

        {
            RWStructuredBuffer<uint> usedGroupsCounter =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingTouchedGroupsCounter)];
            RWStructuredBuffer<uint> usedGroupsBuffer =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingTouchedGroups)];
            uint usedSlot = 0;
            InterlockedAdd(usedGroupsCounter[0], 1u, usedSlot);
            if (usedSlot < CLOD_USED_GROUPS_CAPACITY) {
                usedGroupsBuffer[usedSlot] = groupGlobalIndex;
            }
        }

        bool shouldEmit = (seg.meshletCount != 0);
        {
            StructuredBuffer<CLodStreamingRuntimeState> runtimeState =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingRuntimeState)];
            const uint activeGroupScanCount = runtimeState[0].activeGroupScanCount;
            ByteAddressBuffer nonResidentBits =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingNonResidentBits)];
            if (shouldEmit && groupGlobalIndex < activeGroupScanCount) {
                if (CLodReadBit(nonResidentBits, groupGlobalIndex)) {
                    shouldEmit = false;
                }
            }
        }

        if (!shouldEmit) {
            return;
        }

        WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_EMIT_BUCKET_THREADS, 1);
        const GroupPageMapEntry pageEntry = LoadGroupPageMapEntry(clodMeshMetadata.pageMapBase + grp.pageMapBase, seg.pageIndex);
        const uint sourceTag = UnpackSourceTag(rec.nodeIdPacked);

        [loop]
        for (uint meshletOffset = 0u; meshletOffset < seg.meshletCount; ++meshletOffset) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex >= CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                continue;
            }

            MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
            outRecord.instanceIndex = rec.instanceIndex;
            outRecord.viewId = rec.viewId;
            outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
            outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(seg.firstMeshletInPage + meshletOffset, 1u);
            outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
            outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
            clusterFrontier[outputIndex] = outRecord;
        }
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_SEGMENT_RECORDS, 1);
        return;
    }

    const float3 lodCheckWorldCenter = mul(float4(nodeLodCenterObjectSpace, 1.0f), objectModelMatrix).xyz;
    const float lodCheckWorldRadius = nodeLodRadiusObjectSpace * lodUniformScale;
    const float nodeErrorOverDistance = ProjectedGeometricError(
        lodCheckWorldCenter,
        lodCheckWorldRadius,
        node.metric.maxQuadricError,
        lodUniformScale,
        lodCam.positionWorldSpace.xyz,
        lodCam.zNear,
        lodCamera.isOrtho);
    const bool nodeWantsTraversal = parentAllowsRefine && (nodeErrorOverDistance >= lodCam.errorOverDistanceThreshold);

    if (!nodeWantsTraversal) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS, 1);
        return;
    }

    bool occlusionCulled = false;
    if (CLodWorkGraphOcclusionEnabled() && (!cullCamera.isOrtho || CLOD_VSM_OCCLUSION_CULLING)) {
        StructuredBuffer<CLodViewDepthSRVIndex> viewDepthSRVIndices =
            ResourceDescriptorHeap[CLOD_WG_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX];
        const uint depthMapDescriptorIndex = viewDepthSRVIndices[cullViewId].linearDepthSRVIndex;
        if (depthMapDescriptorIndex != 0) {
            if (replaySource) {
                OcclusionCullingPerspectiveTexture2D(
                    occlusionCulled,
                    cullCamera,
                    nodeCenterViewSpace,
                    -nodeCenterViewSpace.z,
                    nodeRadiusWorld,
                    depthMapDescriptorIndex);
            } else {
                const row_major matrix prevModelMatrix = perObjectBuffer[objectBufferIndex].prevModel;
                const float prevNodeCullScale = MaxAxisScale_RowVector(prevModelMatrix);
                const float3 prevNodeCenterViewSpace = ToViewSpace(nodeCullCenterObjectSpace, prevModelMatrix, cullCamera.prevView);
                const float prevNodeRadiusWorld = nodeCullRadiusObjectSpace * prevNodeCullScale;
                OcclusionCullingPerspectiveTexture2D(
                    occlusionCulled,
                    cullCamera,
                    prevNodeCenterViewSpace,
                    -prevNodeCenterViewSpace.z,
                    prevNodeRadiusWorld,
                    depthMapDescriptorIndex,
                    cullCamera.prevUnjitteredProjection);
            }
        }
    }

    if (occlusionCulled) {
        if (!replaySource) {
            ReplayTryAppendNode(rec.instanceIndex, rec.viewId, UnpackNodeId(rec.nodeIdPacked));
        }
        return;
    }

    const uint childCount = min(node.range.countMinusOne + 1u, BVH_MAX_CHILDREN);
    const uint sourceTag = UnpackSourceTag(rec.nodeIdPacked);

    [loop]
    for (uint childIndex = 0; childIndex < childCount; ++childIndex) {
        const uint childNodeId = node.range.indexOrOffset + childIndex;
        const ClusterLODNode child = lodNodes[clodMeshMetadata.lodNodesBase + childNodeId];

        const float3 childCullCenterOS = isSkinned ? instanceData.boundingSphere.sphere.xyz : child.metric.cullCenterAndRadius.xyz;
        const float childCullRadiusOS = isSkinned ? instanceData.boundingSphere.sphere.w : child.metric.cullCenterAndRadius.w;
        const float3 childCenterVS = ToViewSpace(childCullCenterOS, objectModelMatrix, cullCamera.view);
        const float childRadiusWorld = childCullRadiusOS * cullUniformScale;
        if (!replaySource && SphereOutsideFrustumViewSpace(childCenterVS, childRadiusWorld, cullCamera)) {
            WGTelemetryAdd(WG_COUNTER_CHILD_PREFILTER_FRUSTUM_CULLED, 1);
            continue;
        }

        if (child.range.isLeaf == 0) {
            const float3 childWorldCenter = mul(float4(child.metric.lodCenterAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
            const float childLodRadiusWorld = child.metric.lodCenterAndRadius.w * lodUniformScale;
            const float childEOD = ProjectedGeometricError(
                childWorldCenter, childLodRadiusWorld,
                child.metric.maxQuadricError, lodUniformScale,
                lodCam.positionWorldSpace.xyz, lodCam.zNear,
                lodCamera.isOrtho);
            if (childEOD < lodCam.errorOverDistanceThreshold) {
                WGTelemetryAdd(WG_COUNTER_CHILD_PREFILTER_LOD_REJECTED, 1);
                continue;
            }
        }

        uint outputIndex = 0u;
        InterlockedAdd(nextCounter[0], 1u, outputIndex);
        if (outputIndex >= CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
            continue;
        }

        TraverseNodeRecord childRecord = (TraverseNodeRecord)0;
        childRecord.instanceIndex = rec.instanceIndex;
        childRecord.viewId = rec.viewId;
        childRecord.nodeIdPacked = PackTraverseNodeId(childNodeId, sourceTag, 1u);
        nextFrontier[outputIndex] = childRecord;
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_TRAVERSE_RECORDS, 1);
    }
}

[numthreads(32, 1, 1)]
void PureComputeClusterFrontierCS(const uint3 dispatchThreadID : SV_DispatchThreadID, const uint GI : SV_GroupIndex)
{
    StructuredBuffer<MeshletBucketRecord> inputFrontier = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> inputCountBuffer = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX];

    const uint inputCount = inputCountBuffer[0];
    const uint index = dispatchThreadID.x;
    const uint groupBase = dispatchThreadID.x - GI;
    const uint activeCount = (groupBase < inputCount) ? min(32u, inputCount - groupBase) : 0u;
    const bool hasBucket = (index < inputCount);
    MeshletBucketRecord bucket = (MeshletBucketRecord)0;
    if (hasBucket) {
        bucket = inputFrontier[index];
    }

    uint swPending = 0u;
    uint pageJobPending = 0u;
    ClusterCullBody(bucket, hasBucket, GI, activeCount, 1u, swPending, pageJobPending);
}