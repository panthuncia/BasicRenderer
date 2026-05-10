#include "include/cbuffers.hlsli"
#include "include/clodVirtualShadowClipmap.hlsli"
#include "include/structs.hlsli"
#include "include/skinningCommon.hlsli"
#include "include/visibilityPacking.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/debugPayload.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"
#include "include/clodStructs.hlsli"

#ifndef CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
#define CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW 0
#endif

static const uint VOXEL_RASTER_THREADS_PER_GROUP = 32u;

bool VoxelMaskTest(uint2 mask, uint bitIndex)
{
    return bitIndex < 32u ? ((mask.x & (1u << bitIndex)) != 0u) : ((mask.y & (1u << (bitIndex - 32u))) != 0u);
}

bool RayBoxIntersect(float3 rayOrigin, float3 rayDir, float3 boxMin, float3 boxMax, out float tEnter, out float tExit)
{
    tEnter = 0.0f;
    tExit = 3.402823e+38f;

    //[unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        const float origin = rayOrigin[axis];
        const float dir = rayDir[axis];
        const float bMin = boxMin[axis];
        const float bMax = boxMax[axis];

        if (abs(dir) <= 1.0e-8f)
        {
            if (origin < bMin || origin > bMax)
            {
                return false;
            }
            continue;
        }

        const float invDir = 1.0f / dir;
        float t0 = (bMin - origin) * invDir;
        float t1 = (bMax - origin) * invDir;
        if (t0 > t1)
        {
            const float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }
        tEnter = max(tEnter, t0);
        tExit = min(tExit, t1);
        if (tExit < tEnter)
        {
            return false;
        }
    }

    return tExit >= 0.0f;
}

bool RaycastVoxelCubeDDA(float3 rayOrigin, float3 rayDir, uint2 occupancyMask, out float tHit)
{
    tHit = 0.0f;

    float tEnter = 0.0f;
    float tExit = 0.0f;
    if (!RayBoxIntersect(rayOrigin, rayDir, float3(0.0f, 0.0f, 0.0f), float3(4.0f, 4.0f, 4.0f), tEnter, tExit))
    {
        return false;
    }

    float currentT = max(tEnter, 0.0f) + 1e-4f;
    const float startT = currentT;
    float3 p = clamp(rayOrigin + rayDir * startT, float3(0.0f, 0.0f, 0.0f), float3(3.9999f, 3.9999f, 3.9999f));
    int3 cell = int3(floor(p));
    const int3 stepDir = int3(rayDir.x >= 0.0f ? 1 : -1, rayDir.y >= 0.0f ? 1 : -1, rayDir.z >= 0.0f ? 1 : -1);

    float3 nextBoundary = float3(
        stepDir.x > 0 ? float(cell.x + 1) : float(cell.x),
        stepDir.y > 0 ? float(cell.y + 1) : float(cell.y),
        stepDir.z > 0 ? float(cell.z + 1) : float(cell.z));
    const float largeT = 3.402823e+38f;
    float3 tMax = float3(
        abs(rayDir.x) > 1.0e-8f ? (nextBoundary.x - rayOrigin.x) / rayDir.x : largeT,
        abs(rayDir.y) > 1.0e-8f ? (nextBoundary.y - rayOrigin.y) / rayDir.y : largeT,
        abs(rayDir.z) > 1.0e-8f ? (nextBoundary.z - rayOrigin.z) / rayDir.z : largeT);
    float3 tDelta = float3(
        abs(rayDir.x) > 1.0e-8f ? abs(1.0f / rayDir.x) : largeT,
        abs(rayDir.y) > 1.0e-8f ? abs(1.0f / rayDir.y) : largeT,
        abs(rayDir.z) > 1.0e-8f ? abs(1.0f / rayDir.z) : largeT);

    [loop]
    for (uint iter = 0u; iter < 16u; ++iter)
    {
        if (any(cell < 0) || any(cell >= 4))
        {
            break;
        }

        const uint cellIndex = (uint)cell.x | ((uint)cell.y << 2u) | ((uint)cell.z << 4u);
        if (VoxelMaskTest(occupancyMask, cellIndex))
        {
            tHit = currentT;
            return true;
        }

        if (tMax.x <= tMax.y && tMax.x <= tMax.z)
        {
            if (tMax.x > tExit) break;
            currentT = tMax.x + 1e-4f;
            cell.x += stepDir.x;
            tMax.x += tDelta.x;
        }
        else if (tMax.y <= tMax.z)
        {
            if (tMax.y > tExit) break;
            currentT = tMax.y + 1e-4f;
            cell.y += stepDir.y;
            tMax.y += tDelta.y;
        }
        else
        {
            if (tMax.z > tExit) break;
            currentT = tMax.z + 1e-4f;
            cell.z += stepDir.z;
            tMax.z += tDelta.z;
        }
    }

    return false;
}

#if CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
bool VoxelRasterWriteVirtualShadow(
    uint2 pixel,
    float linearDepth,
    CLodVirtualShadowClipmapInfo clipmapInfo,
    RWTexture2DArray<uint> pageTable,
    RWTexture2D<uint> physicalPages)
{
    const float2 shadowUv = saturate((float2(pixel) + 0.5f) / max((float)clipmapInfo.virtualResolution, 1.0f));
    const uint2 virtualPageCoords = CLodVirtualShadowVirtualPageCoordsFromUv(shadowUv, clipmapInfo);
    const uint2 wrappedPageCoords = CLodVirtualShadowWrappedPageCoords(virtualPageCoords, clipmapInfo);

    const uint3 pageCoords = uint3(wrappedPageCoords, clipmapInfo.pageTableLayer);
    const uint pageEntry = pageTable[pageCoords];
    if (!CLodVirtualShadowPageEntryCanRaster(pageEntry))
    {
        return false;
    }

    const uint physicalPageIndex = pageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
    const uint2 virtualTexelCoords = CLodVirtualShadowVirtualTexelCoordsFromUv(shadowUv, clipmapInfo);
    const uint2 atlasPixel = CLodVirtualShadowPhysicalAtlasPixel(physicalPageIndex, virtualTexelCoords, clipmapInfo);

    InterlockedMin(physicalPages[atlasPixel], asuint(linearDepth));
    uint ignored = 0u;
    InterlockedOr(pageTable[pageCoords], kCLodVirtualShadowContentValidMask | kCLodVirtualShadowRerenderedThisFrameMask, ignored);
    return true;
}
#endif

[numthreads(1, 1, 1)]
void VoxelRasterBuildDispatchArgsCS()
{
    StructuredBuffer<uint> counter = ResourceDescriptorHeap[CLOD_RASTER_VOXEL_WORK_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodVoxelRasterDispatchCommand> args = ResourceDescriptorHeap[CLOD_RASTER_VOXEL_INDIRECT_ARGS_DESCRIPTOR_INDEX];
    const uint count = min(counter[0], CLOD_RASTER_VOXEL_WORK_CAPACITY);
    args[0].dispatchX = count;
    args[0].dispatchY = 1u;
    args[0].dispatchZ = 1u;
}

[numthreads(VOXEL_RASTER_THREADS_PER_GROUP, 1, 1)]
void VoxelRasterCS(uint3 groupId : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    const uint workIndex = groupId.x;
    const uint GI = groupThreadID.x;
    StructuredBuffer<uint> counter = ResourceDescriptorHeap[CLOD_RASTER_VOXEL_WORK_COUNTER_DESCRIPTOR_INDEX];
    const uint workCount = min(counter[0], CLOD_RASTER_VOXEL_WORK_CAPACITY);
    if (workIndex >= workCount)
    {
        return;
    }

    StructuredBuffer<CLodVoxelRasterWorkRecord> workRecords = ResourceDescriptorHeap[CLOD_RASTER_VOXEL_WORK_RECORDS_DESCRIPTOR_INDEX];
    const CLodVoxelRasterWorkRecord work = workRecords[workIndex];
    ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[CLOD_RASTER_VOXEL_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
    const uint4 packedCluster = CLodLoadVisibleClusterPacked(visibleClusters, work.visibleClusterIndex);
    if (!CLodVisibleClusterIsVoxel(packedCluster))
    {
        return;
    }

    StructuredBuffer<PerMeshInstanceBuffer> meshInstances = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerObjectBuffer> objects = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<MeshInstanceClodOffsets> meshInstanceClodOffsets = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
    StructuredBuffer<CLodMeshMetadata> metadataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];
    StructuredBuffer<CullingCameraInfo> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];

    const uint instanceIndex = CLodVisibleClusterInstanceID(packedCluster);
    const uint viewId = CLodVisibleClusterViewID(packedCluster);
    const uint localGroupId = CLodVisibleClusterGroupID(packedCluster);
    const uint localVoxelClusterIndex = CLodVisibleClusterVoxelClusterIndex(packedCluster);

    const PerMeshInstanceBuffer meshInstance = meshInstances[instanceIndex];
    const PerObjectBuffer objectData = objects[meshInstance.perObjectBufferIndex];
    const MeshInstanceClodOffsets offsets = meshInstanceClodOffsets[instanceIndex];
    const CLodMeshMetadata metadata = metadataBuffer[offsets.clodMeshMetadataIndex];
    const CullingCameraInfo camera = cameras[viewId];
    const ClodViewRasterInfo rasterInfo = viewRasterInfoBuffer[viewId];

    CLodVoxelGroupDescriptor descriptor;
    if (!CLodTryLoadVoxelGroupDescriptor(metadata, localGroupId, descriptor) || localVoxelClusterIndex >= descriptor.clusterCount)
    {
        return;
    }

    GroupPageMapEntry pageEntry;
    CLodVoxelPageHeader pageHeader;
    const CLodVoxelClusterRecord voxelCluster = CLodLoadVoxelCluster(metadata, descriptor, localGroupId, localVoxelClusterIndex, pageEntry, pageHeader);
    if (voxelCluster.cubeCount == 0u || voxelCluster.cubeCount > CLOD_VOXEL_MAX_CUBES_PER_CLUSTER)
    {
        return;
    }

    const float voxelWidth = descriptor.aabbMinAndVoxelWidth.w;
    if (voxelWidth <= 0.0f)
    {
        return;
    }

#if CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
    uint2 targetDims = uint2(rasterInfo.scissorMaxX, rasterInfo.scissorMaxY);
    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos =
        ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX];
    CLodVirtualShadowClipmapInfo clipmapInfo = (CLodVirtualShadowClipmapInfo)0;
    if (!CLodVirtualShadowTryGetClipmapInfoForView(viewId, clipmapInfos, clipmapInfo))
    {
        return;
    }
    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX];
    RWTexture2D<uint> physicalPages = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX];
#else
    if (rasterInfo.visibilityUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }
    RWTexture2D<uint64_t> visibilityBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(rasterInfo.visibilityUAVDescriptorIndex)];
    uint2 targetDims;
    visibilityBuffer.GetDimensions(targetDims.x, targetDims.y);
#endif

    RWTexture2D<uint2> debugVisTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    const bool debugMode = perFrameBuffer.outputType == OUTPUT_SW_RASTER;
    const row_major matrix objectModel = objectData.model;
    const float2 targetDimsInv = rcp(max(float2(targetDims), float2(1.0f, 1.0f)));

    for (uint cubeOffset = 0u; cubeOffset < voxelCluster.cubeCount; ++cubeOffset)
    {
        const CLodVoxelCubeRecord cube = CLodLoadVoxelCubeFromPage(
            pageEntry.slabDescriptorIndex,
            pageEntry.slabByteOffset,
            pageHeader.cubeRecordsOffset,
            voxelCluster.firstCube + cubeOffset);
        if (cube.occupancyMask.x == 0u && cube.occupancyMask.y == 0u)
        {
            continue;
        }

        const uint3 cubeCoord = CLodVoxelDecodeCubeCoord(cube.cubeCoord);
        const float cubeObjectWidth = voxelWidth * 4.0f;
        const float3 cubeMinObject = descriptor.aabbMinAndVoxelWidth.xyz + float3(cubeCoord) * cubeObjectWidth;
        const float3 cubeMaxObject = cubeMinObject + cubeObjectWidth;

        float4x4 skinMatrix = IdentitySkinMatrix();
        float4x4 inverseSkinMatrix = IdentitySkinMatrix();
        if (cube.dominantBoneIndex != CLOD_VOXEL_STATIC_BONE_INDEX)
        {
            skinMatrix = LoadBoneSkinMatrix(meshInstance.skinningInstanceSlot, cube.dominantBoneIndex);
            inverseSkinMatrix = LoadBoneInverseSkinMatrix(meshInstance.skinningInstanceSlot, cube.dominantBoneIndex);
        }
        const row_major matrix localToWorld = mul(skinMatrix, objectModel);
        const row_major matrix worldToLocal = mul(objectData.modelInverse, inverseSkinMatrix);
        const row_major matrix localToClip = mul(localToWorld, camera.viewProjection);
        const float4 localViewZ = mul(localToWorld, camera.viewZ);

        float2 screenMin = float2(3.402823e+38f, 3.402823e+38f);
        float2 screenMax = float2(-3.402823e+38f, -3.402823e+38f);
        bool validProjection = false;
        //[unroll]
        for (uint cornerIndex = 0u; cornerIndex < 8u; ++cornerIndex)
        {
            const float3 corner = float3(
                (cornerIndex & 1u) ? cubeMaxObject.x : cubeMinObject.x,
                (cornerIndex & 2u) ? cubeMaxObject.y : cubeMinObject.y,
                (cornerIndex & 4u) ? cubeMaxObject.z : cubeMinObject.z);
            const float4 clip = mul(float4(corner, 1.0f), localToClip);
            if (clip.w <= 0.0f)
            {
                continue;
            }
            const float2 ndc = clip.xy / clip.w;
            const float2 screen = float2(
                (ndc.x + 1.0f) * 0.5f * float(rasterInfo.scissorMaxX - rasterInfo.scissorMinX) + float(rasterInfo.scissorMinX),
                (1.0f - ndc.y) * 0.5f * float(rasterInfo.scissorMaxY - rasterInfo.scissorMinY) + float(rasterInfo.scissorMinY));
            screenMin = min(screenMin, screen);
            screenMax = max(screenMax, screen);
            validProjection = true;
        }
        if (!validProjection)
        {
            continue;
        }

        int2 minPx = int2(floor(screenMin));
        int2 maxPx = int2(floor(screenMax));
        minPx = max(minPx, int2(rasterInfo.scissorMinX, rasterInfo.scissorMinY));
        maxPx = min(maxPx, int2(int(rasterInfo.scissorMaxX) - 1, int(rasterInfo.scissorMaxY) - 1));
        minPx = max(minPx, int2(0, 0));
        maxPx = min(maxPx, int2(int(targetDims.x) - 1, int(targetDims.y) - 1));
        if (minPx.x > maxPx.x || minPx.y > maxPx.y)
        {
            continue;
        }

        const float3 cameraOriginLocal = mul(float4(camera.positionWorldSpace.xyz, 1.0f), worldToLocal).xyz;
        const uint pixelWidth = uint(maxPx.x - minPx.x + 1);
        const uint pixelHeight = uint(maxPx.y - minPx.y + 1);
        const uint pixelCount = pixelWidth * pixelHeight;
        for (uint pixelLinear = GI; pixelLinear < pixelCount; pixelLinear += VOXEL_RASTER_THREADS_PER_GROUP)
        {
            const int px = minPx.x + int(pixelLinear % pixelWidth);
            const int py = minPx.y + int(pixelLinear / pixelWidth);
            const float2 uv = (float2(px, py) + 0.5f) * targetDimsInv;
            const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
            float4 viewNear = mul(float4(ndc, 0.0f, 1.0f), camera.projectionInverse);
            viewNear.xyz /= max(viewNear.w, 1.0e-6f);
            float4 worldNear = mul(float4(viewNear.xyz, 1.0f), camera.viewInverse);
            const float3 worldPoint = worldNear.xyz / max(worldNear.w, 1.0e-6f);
            const float3 localPoint = mul(float4(worldPoint, 1.0f), worldToLocal).xyz;
            const float3 rayOriginObject = cameraOriginLocal;
            const float3 rayDirObject = normalize(localPoint - rayOriginObject);

            const float3 rayOriginCube = (rayOriginObject - cubeMinObject) / voxelWidth;
            const float3 rayDirCube = rayDirObject / voxelWidth;

            float tHitCube = 0.0f;
            if (!RaycastVoxelCubeDDA(rayOriginCube, rayDirCube, cube.occupancyMask, tHitCube))
            {
                continue;
            }

            const float3 hitObject = rayOriginObject + rayDirObject * tHitCube;
            const float linearDepth = -dot(float4(hitObject, 1.0f), localViewZ);
            if (linearDepth <= 0.0f)
            {
                continue;
            }

            if (debugMode)
            {
                WriteDebugPixel(debugVisTex, uint2(px, py), PackDebugUint(2u));
            }

#if CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW
            VoxelRasterWriteVirtualShadow(uint2(px, py), linearDepth, clipmapInfo, pageTable, physicalPages);
#else
            const uint64_t visKey = PackVisKey(linearDepth, work.visibleClusterIndex, cubeOffset);
            InterlockedMin(visibilityBuffer[uint2(px, py)], visKey);
#endif
        }
    }
}
