#define CLOD_COMPUTE_INCLUDE_ONLY 1
#include "ClusterLOD/workGraphCulling.hlsl"

[numthreads(1, 1, 1)]
void BuildPureComputeDispatchArgsCS()
{
    StructuredBuffer<uint> counterBuffer = ResourceDescriptorHeap[CLOD_PC_DISPATCH_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint3> dispatchArgs = ResourceDescriptorHeap[CLOD_PC_DISPATCH_ARGS_DESCRIPTOR_INDEX];
    const uint count = min(counterBuffer[0], max(1u, CLOD_PC_DISPATCH_COUNT_LIMIT));
    const uint threadsPerGroup = max(1u, CLOD_PC_DISPATCH_THREADS_PER_GROUP);
    const uint groups = (count == 0u) ? 1u : ((count + threadsPerGroup - 1u) / threadsPerGroup);
    dispatchArgs[0] = uint3(groups, 1u, 1u);
}

[numthreads(1, 1, 1)]
void BuildPureComputeReplayDispatchArgsCS()
{
    RWStructuredBuffer<CLodReplayBufferState> replayState = ResourceDescriptorHeap[CLOD_WG_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint3> dispatchArgs = ResourceDescriptorHeap[CLOD_PC_DISPATCH_ARGS_DESCRIPTOR_INDEX];
    const uint rawCount = (CLOD_PC_REPLAY_SOURCE_INDEX == 0u)
        ? replayState[0].nodeWriteCount
        : replayState[0].meshletWriteCount;
    const uint count = min(rawCount, CLOD_WG_VISIBLE_CLUSTERS_CAPACITY);
    const uint threadsPerGroup = max(1u, CLOD_PC_DISPATCH_THREADS_PER_GROUP);
    const uint groups = (count == 0u) ? 1u : ((count + threadsPerGroup - 1u) / threadsPerGroup);
    dispatchArgs[0] = uint3(groups, 1u, 1u);
}

[numthreads(64, 1, 1)]
void SeedPureComputeReplayNodesCS(const uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWByteAddressBuffer replayBuffer = ResourceDescriptorHeap[CLOD_WG_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReplayBufferState> replayState = ResourceDescriptorHeap[CLOD_WG_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<TraverseNodeRecord> outFrontier = ResourceDescriptorHeap[CLOD_PC_FRONTIER_OUTPUT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> outCounter = ResourceDescriptorHeap[CLOD_PC_FRONTIER_OUTPUT_COUNT_DESCRIPTOR_INDEX];

    const uint count = min(replayState[0].nodeWriteCount, CLOD_WG_VISIBLE_CLUSTERS_CAPACITY);
    const uint index = dispatchThreadID.x;
    if (index == 0u) {
        outCounter[0] = count;
    }
    if (index >= count) {
        return;
    }

    const uint byteOffset = index * CLOD_NODE_REPLAY_STRIDE_BYTES;
    const uint2 header = replayBuffer.Load2(byteOffset);

    TraverseNodeRecord record = (TraverseNodeRecord)0;
    record.instanceIndex = header.x;
    record.nodeIdPacked = header.y;
    record.viewId = replayBuffer.Load(byteOffset + 8u);
    outFrontier[index] = record;
}

[numthreads(32, 1, 1)]
void SeedPureComputeReplayClustersCS(const uint3 dispatchThreadID : SV_DispatchThreadID)
{
    RWByteAddressBuffer replayBuffer = ResourceDescriptorHeap[CLOD_WG_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReplayBufferState> replayState = ResourceDescriptorHeap[CLOD_WG_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<MeshletBucketRecord> outFrontier = ResourceDescriptorHeap[CLOD_PC_CLUSTER_OUTPUT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> outCounter = ResourceDescriptorHeap[CLOD_PC_CLUSTER_OUTPUT_COUNT_DESCRIPTOR_INDEX];

    const uint count = min(replayState[0].meshletWriteCount, CLOD_WG_VISIBLE_CLUSTERS_CAPACITY);
    const uint index = dispatchThreadID.x;
    if (index == 0u) {
        outCounter[0] = count;
    }
    if (index >= count) {
        return;
    }

    const uint byteOffset = CLOD_REPLAY_MESHLET_REGION_OFFSET + index * CLOD_MESHLET_REPLAY_STRIDE_BYTES;
    const uint4 head = replayBuffer.Load4(byteOffset);
    const uint2 tail = replayBuffer.Load2(byteOffset + 16u);

    MeshletBucketRecord record = (MeshletBucketRecord)0;
    record.instanceIndex = head.x;
    record.viewId = head.y;
    record.groupIdPacked = head.z;
    record.meshletIndexAndCount = head.w;
    record.pageSlabDescriptorIndex = tail.x;
    record.pageSlabByteOffset = tail.y;
    outFrontier[index] = record;
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
    outFrontier[outputIndex].nodeIdPacked = PackTraverseNodeId(CLodResolveTraversalRootNode(clodMeshMetadata), CLOD_RECORD_SOURCE_PASS1, 1u);

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
    const bool forceLodDecision = CLodForcedTraversalDepthRootEnabled(clodMeshMetadata);
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
    StructuredBuffer<ClusterLODNode> lodNodes =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Nodes)];

    const ClusterLODNode node = lodNodes[clodMeshMetadata.lodNodesBase + UnpackNodeId(rec.nodeIdPacked)];

    if (node.range.isLeaf == CLOD_NODE_INTERNAL) {
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

#if CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
    const bool objectInvalidatedThisFrame = CLodVirtualShadowInstanceInvalidatedThisFrame(rec.instanceIndex);
    const bool dirtyPageCullingEnabled = CLodWorkGraphShadowDirtyPageCullingEnabled() && !objectInvalidatedThisFrame;
#endif

    if (nodeCulled) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_CULLED_NODE_RECORDS, 1);
        return;
    }

    if (node.range.isLeaf != CLOD_NODE_INTERNAL) {
        bool nodeTouchesDirtyPages = true;
#if CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
        if (dirtyPageCullingEnabled) {
            const float3 nodeCullCenterWorld = mul(float4(nodeCullCenterObjectSpace, 1.0f), objectModelMatrix).xyz;
            nodeTouchesDirtyPages = CLodVirtualShadowBoundsTouchDirtyPages(nodeCullCenterWorld, nodeRadiusWorld, rec.viewId);
        }
#endif

        CLodRenderableLeaf leaf;
        if (!CLodPrepareRenderableLeaf(
            clodMeshMetadata,
            node,
            parentAllowsRefine,
            objectModelMatrix,
            lodUniformScale,
            lodCam,
            lodCamera.isOrtho,
            nodeTouchesDirtyPages,
            forceLodDecision,
            leaf))
        {
            return;
        }

        if (!forceLodDecision && CLodRefinedChildSuppressesParent(
            clodMeshMetadata.groupsBase,
            node.range.countMinusOne - 1u,
            node.range.countMinusOne != 0u,
            objectModelMatrix,
            lodUniformScale,
            lodCam,
            lodCamera.isOrtho,
            rec.instanceIndex,
            instanceData.perMeshBufferIndex,
            rec.viewId,
            leaf.errorOverDistance,
            0.0f.xxx,
            -1.0f,
            false))
        {
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_CONDITION2, 1);
            return;
        }

        if (leaf.isVoxel)
        {
            CLodVoxelGroupDescriptor voxelDescriptor;
            if (CLodTryLoadVoxelDescriptorByLocalIndex(clodMeshMetadata, node.range.indexOrOffset, voxelDescriptor))
            {
                WGTelemetryAdd(WG_COUNTER_TRAVERSE_VOXEL_DESCRIPTOR_HITS, 1);
                CLodAppendVoxelRasterCubeWork(
                    clodMeshMetadata,
                    rec.instanceIndex,
                    rec.viewId,
                    node.range.ownerGroupId,
                    leaf.group,
                    voxelDescriptor,
                    objectModelMatrix,
                    lodUniformScale,
                    lodCam,
                    lodCamera.isOrtho,
                    instanceData.perMeshBufferIndex,
                    leaf.errorOverDistance);
            }
            else
            {
                WGTelemetryAdd(WG_COUNTER_TRAVERSE_VOXEL_DESCRIPTOR_MISSES, 1);
            }
            return;
        }

        StructuredBuffer<ClusterLODGroupSegment> segments =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Segments)];
        const uint segGlobalIndex = clodMeshMetadata.segmentsBase + node.range.indexOrOffset;
        const ClusterLODGroupSegment seg = segments[segGlobalIndex];

        if (seg.meshletCount == 0u) {
            return;
        }

        WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_EMIT_BUCKET_THREADS, 1);
        const GroupPageMapEntry pageEntry = LoadGroupPageMapEntry(clodMeshMetadata.pageMapBase + leaf.group.pageMapBase, seg.pageIndex);
        const uint sourceTag = UnpackSourceTag(rec.nodeIdPacked);

        uint tail = seg.meshletCount;
        uint budget = MAX_RECORDS_PER_SEGMENT;
        uint n64 = min(tail / 64u, budget);
        tail -= n64 * 64u;
        budget -= n64;

        uint n32 = 0u;
        uint n16 = 0u;
        uint n8 = 0u;
        uint n4 = 0u;
        uint n2 = 0u;
        uint n1 = 0u;

        if (tail >= 32u && budget >= 2u) { n32 = 1u; tail -= 32u; budget--; }
        if (tail >= 16u && budget >= 2u) { n16 = 1u; tail -= 16u; budget--; }
        if (tail >= 8u  && budget >= 2u) { n8  = 1u; tail -= 8u;  budget--; }
        if (tail >= 4u  && budget >= 2u) { n4  = 1u; tail -= 4u;  budget--; }
        if (tail >= 2u  && budget >= 2u) { n2  = 1u; tail -= 2u;  budget--; }

        if      (tail > 32u) { n64++; }
        else if (tail > 16u) { n32++; }
        else if (tail > 8u)  { n16++; }
        else if (tail > 4u)  { n8++;  }
        else if (tail > 2u)  { n4++;  }
        else if (tail > 1u)  { n2++;  }
        else if (tail > 0u)  { n1 = 1u; }

        uint meshletBase = seg.firstMeshletInPage;

        [loop]
        for (uint bucketIndex64 = 0u; bucketIndex64 < n64; ++bucketIndex64) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex < CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
                outRecord.instanceIndex = rec.instanceIndex;
                outRecord.viewId = rec.viewId;
                outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
                outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(meshletBase, 64u);
                outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
                outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
                clusterFrontier[outputIndex] = outRecord;
            }
            meshletBase += 64u;
        }

        [loop]
        for (uint bucketIndex32 = 0u; bucketIndex32 < n32; ++bucketIndex32) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex < CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
                outRecord.instanceIndex = rec.instanceIndex;
                outRecord.viewId = rec.viewId;
                outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
                outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(meshletBase, 32u);
                outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
                outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
                clusterFrontier[outputIndex] = outRecord;
            }
            meshletBase += 32u;
        }

        [loop]
        for (uint bucketIndex16 = 0u; bucketIndex16 < n16; ++bucketIndex16) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex < CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
                outRecord.instanceIndex = rec.instanceIndex;
                outRecord.viewId = rec.viewId;
                outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
                outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(meshletBase, 16u);
                outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
                outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
                clusterFrontier[outputIndex] = outRecord;
            }
            meshletBase += 16u;
        }

        [loop]
        for (uint bucketIndex8 = 0u; bucketIndex8 < n8; ++bucketIndex8) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex < CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
                outRecord.instanceIndex = rec.instanceIndex;
                outRecord.viewId = rec.viewId;
                outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
                outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(meshletBase, 8u);
                outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
                outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
                clusterFrontier[outputIndex] = outRecord;
            }
            meshletBase += 8u;
        }

        [loop]
        for (uint bucketIndex4 = 0u; bucketIndex4 < n4; ++bucketIndex4) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex < CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
                outRecord.instanceIndex = rec.instanceIndex;
                outRecord.viewId = rec.viewId;
                outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
                outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(meshletBase, 4u);
                outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
                outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
                clusterFrontier[outputIndex] = outRecord;
            }
            meshletBase += 4u;
        }

        [loop]
        for (uint bucketIndex2 = 0u; bucketIndex2 < n2; ++bucketIndex2) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex < CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
                outRecord.instanceIndex = rec.instanceIndex;
                outRecord.viewId = rec.viewId;
                outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
                outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(meshletBase, 2u);
                outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
                outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
                clusterFrontier[outputIndex] = outRecord;
            }
            meshletBase += 2u;
        }

        [loop]
        for (uint bucketIndex1 = 0u; bucketIndex1 < n1; ++bucketIndex1) {
            uint outputIndex = 0u;
            InterlockedAdd(clusterCounter[0], 1u, outputIndex);
            if (outputIndex < CLOD_WG_VISIBLE_CLUSTERS_CAPACITY) {
                MeshletBucketRecord outRecord = (MeshletBucketRecord)0;
                outRecord.instanceIndex = rec.instanceIndex;
                outRecord.viewId = rec.viewId;
                outRecord.groupIdPacked = PackGroupId(node.range.ownerGroupId, sourceTag);
                outRecord.meshletIndexAndCount = PackMeshletIndexAndCount(meshletBase, 1u);
                outRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
                outRecord.pageSlabByteOffset = pageEntry.slabByteOffset;
                clusterFrontier[outputIndex] = outRecord;
            }
            meshletBase += 1u;
        }
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
    const bool nodeWantsTraversal =
        forceLodDecision ||
        (parentAllowsRefine && (nodeErrorOverDistance >= lodCam.errorOverDistanceThreshold));

    if (!nodeWantsTraversal) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS, 1);
        return;
    }

#if CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
    if (dirtyPageCullingEnabled) {
        const float3 nodeCullCenterWorld = mul(float4(nodeCullCenterObjectSpace, 1.0f), objectModelMatrix).xyz;
        if (!CLodVirtualShadowBoundsTouchDirtyPages(nodeCullCenterWorld, nodeRadiusWorld, rec.viewId)) {
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_CLEAN_PAGES, 1);
            return;
        }
    }
#endif

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

        if (!forceLodDecision && child.range.isLeaf == CLOD_NODE_INTERNAL) {
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

#if CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
        if (dirtyPageCullingEnabled) {
            const float3 childCullCenterWorld = mul(float4(childCullCenterOS, 1.0f), objectModelMatrix).xyz;
            if (!CLodVirtualShadowBoundsTouchDirtyPages(childCullCenterWorld, childRadiusWorld, rec.viewId)) {
                WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_CLEAN_PAGES, 1);
                continue;
            }
        }
#endif

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
    // ClusterCullBody uses wave ops inside its meshlet loop, so all lanes in the wave
    // must execute the same iteration count even when bucket sizes differ.
    ClusterCullBody(bucket, hasBucket, true, GI, activeCount, 64u, swPending, pageJobPending);
}

groupshared uint gs_denseBaseIndex;

[numthreads(64, 1, 1)]
void PureComputeExpandClusterFrontierCS(
    const uint3 dispatchThreadID : SV_DispatchThreadID,
    const uint3 groupID : SV_GroupID,
    const uint GI : SV_GroupIndex)
{
    StructuredBuffer<MeshletBucketRecord> inputFrontier = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> inputCountBuffer = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodDenseClusterWorkRecord> denseClusterWork = ResourceDescriptorHeap[CLOD_PC_CLUSTER_OUTPUT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> denseClusterWorkCount = ResourceDescriptorHeap[CLOD_PC_CLUSTER_OUTPUT_COUNT_DESCRIPTOR_INDEX];

    const uint bucketIndex = groupID.x;
    const uint inputCount = inputCountBuffer[0];
    const bool hasBucket = bucketIndex < inputCount;
    MeshletBucketRecord bucket = (MeshletBucketRecord)0;
    uint firstLocalMeshletIndex = 0u;
    uint meshletCount = 0u;
    if (hasBucket) {
        bucket = inputFrontier[bucketIndex];
        firstLocalMeshletIndex = UnpackMeshletFirstIndex(bucket.meshletIndexAndCount);
        meshletCount = UnpackMeshletCount(bucket.meshletIndexAndCount);
    }

    if (GI == 0u) {
        gs_denseBaseIndex = 0u;
        if (hasBucket && meshletCount > 0u) {
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_DENSE_EXPANSION_BUCKETS, 1u);
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_DENSE_CLUSTERS_DISPATCHED, meshletCount);
            if (UnpackGroupSourceTag(bucket.groupIdPacked) == CLOD_RECORD_SOURCE_REPLAY) {
                WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_CLUSTER_BUCKET_RECORDS_CONSUMED, 1u);
            }
            InterlockedAdd(denseClusterWorkCount[0], meshletCount, gs_denseBaseIndex);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (hasBucket && GI < meshletCount) {
        const uint denseIndex = gs_denseBaseIndex + GI;
        CLodDenseClusterWorkRecord work = (CLodDenseClusterWorkRecord)0;
        work.instanceIndex = bucket.instanceIndex;
        work.viewId = bucket.viewId;
        work.groupIdPacked = bucket.groupIdPacked;
        work.localMeshletIndex = firstLocalMeshletIndex + GI;
        work.pageSlabDescriptorIndex = bucket.pageSlabDescriptorIndex;
        work.pageSlabByteOffset = bucket.pageSlabByteOffset;
        denseClusterWork[denseIndex] = work;
    }
}

[numthreads(64, 1, 1)]
void PureComputeDenseClusterWorkCS(const uint3 dispatchThreadID : SV_DispatchThreadID, const uint GI : SV_GroupIndex)
{
    StructuredBuffer<CLodDenseClusterWorkRecord> inputWork = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> inputCountBuffer = ResourceDescriptorHeap[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX];

    const uint inputCount = inputCountBuffer[0];
    const uint index = dispatchThreadID.x;
    const uint groupBase = dispatchThreadID.x - GI;
    const uint activeCount = (groupBase < inputCount) ? min(64u, inputCount - groupBase) : 0u;
    if (index >= inputCount) {
        return;
    }

    const CLodDenseClusterWorkRecord work = inputWork[index];
    MeshletBucketRecord bucket = (MeshletBucketRecord)0;
    bucket.instanceIndex = work.instanceIndex;
    bucket.viewId = work.viewId;
    bucket.groupIdPacked = work.groupIdPacked;
    bucket.meshletIndexAndCount = (1u << 16u) | (work.localMeshletIndex & 0xFFFFu);
    bucket.pageSlabDescriptorIndex = work.pageSlabDescriptorIndex;
    bucket.pageSlabByteOffset = work.pageSlabByteOffset;

    uint swPending = 0u;
    uint pageJobPending = 0u;
    ClusterCullBody(bucket, true, false, GI, activeCount, 1u, swPending, pageJobPending);
}
