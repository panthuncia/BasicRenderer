#include "include/cbuffers.hlsli"
#include "include/vertex.hlsli"
#include "include/loadingUtils.hlsli"
#include "include/Misc/sphereScreenExtents.hlsli"
#include "include/utilities.hlsli"
#include "include/occlusionCulling.hlsli"
#include "PerPassRootConstants/meshletCullingRootConstants.h"
#include "include/indirectCommands.hlsli"

void WriteMeshletVisibilityUnpackData(uint clusterToVisibleClusterTableStartIndex, uint meshletIndex)
{
    // uint2, .x = mesh instance index, .y = meshlet local index
    RWStructuredBuffer<VisibleClusterInfo> visibleClusterTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibleClusterTable)];
    RWStructuredBuffer<uint> visibleClusterTableCounter = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibleClusterTableCounter)];
    
    // Allocate cluster index
    uint newAddress;
    InterlockedAdd(visibleClusterTableCounter[0], 1, newAddress);
    
    visibleClusterTable[newAddress].drawcallIndexAndMeshletIndex = uint2(perMeshInstanceBufferIndex, meshletIndex);
    
    // Update cluster index in mesh info
    RWStructuredBuffer<uint> clusterToVisibleClusterIndexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer)];
    clusterToVisibleClusterIndexBuffer[clusterToVisibleClusterTableStartIndex + meshletIndex] = newAddress;
}

// Meshlet culling, one thread per meshlet, one dispatch per mesh, similar to mesh shader
[numthreads(64, 1, 1)]
void MeshletCullingCSMain(const uint3 vDispatchThreadID : SV_DispatchThreadID)
{
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    
    if (perMeshBuffer[perMeshBufferIndex].numMeshlets <= vDispatchThreadID.x)
    {
        return;
    }
    PerMeshBuffer perMesh = perMeshBuffer[perMeshBufferIndex];
    
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    uint meshletBoundsIndex = meshInstanceBuffer.meshletBoundsBufferStartIndex + vDispatchThreadID.x;
    
    StructuredBuffer<BoundingSphere> meshletBoundsBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletBounds)];
    BoundingSphere meshletBounds = meshletBoundsBuffer[meshletBoundsIndex];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera camera = cameras[lightViewIndex];
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer perObject = perObjectBuffer[perObjectBufferIndex];

    float4 objectSpaceCenter = float4(meshletBounds.sphere.xyz, 1.0);
    float4 worldSpaceCenter = mul(objectSpaceCenter, perObject.model);
    float3 viewSpaceCenter = mul(worldSpaceCenter, camera.view).xyz;
    
    float3 scaleFactors = float3(
    length(perObject.model[0].xyz),
    length(perObject.model[1].xyz),
    length(perObject.model[2].xyz)
    );
    
    float maxScale = max(max(scaleFactors.x, scaleFactors.y), scaleFactors.z);
    float scaledBoundingRadius = meshletBounds.sphere.w * maxScale;

    RWByteAddressBuffer meshletBitfieldBuffer = ResourceDescriptorHeap[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX];
    
    uint meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + vDispatchThreadID.x;
    
    // Disable culling for skinned meshes for now, as the bounding sphere is not updated
    if (!(perMesh.vertexFlags & VERTEX_SKINNED))
    { // TODO: Implement skinned mesh culling
#if defined (REMAINDERS_PASS) // In the remainders pass, we want to invert the bitfield, and frustrum+occlusion cull the remaining meshlets
        
        bool alreadyCulled = GetBit(meshletBitfieldBuffer, meshletBitfieldIndex);
        if (!alreadyCulled)
        {
            SetBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex); // Mark as culled
            return; // If the occluders pass will already render this, we don't need to render it in the remainders pass
        }
#endif
#if defined (OCCLUDERS_PASS1) // TODO: What was this for?
        bool culledInRemainderPass = GetBit(meshletBitfieldBuffer, meshletBitfieldIndex);
        if (!culledInRemainderPass){ // If the remainder pass drew this, we don't need to cull it again
            return;
        }
#endif
        bool bCulled = false;
    
        for (uint i = 0; i < 6; i++)
        {
            float4 clippingPlane = camera.clippingPlanes[i].plane; // ZYZ normal, W distance
            float distance = dot(clippingPlane.xyz, viewSpaceCenter) + clippingPlane.w;
            float boundingRadius = perMesh.boundingSphere.sphere.w;
            bCulled |= distance < -scaledBoundingRadius;
        }
        
#if defined (OCCLUSION_CULLING)
        bool occlusionCulled = false;
        OcclusionCulling(occlusionCulled, camera, viewSpaceCenter.xyz, -viewSpaceCenter.z, scaledBoundingRadius, camera.viewProjection, LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX);
        
        bCulled |= occlusionCulled;
#endif
        
        if (bCulled)
        {
        // TODO: Could avoid the atomic if we use eight ExecuteIndirect calls, one for each bit of a byte
//#if !defined (OCCLUDERS_PASS)
            SetBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex);
            //#endif
            return;
        }
    }
//#if !defined (OCCLUDERS_PASS)
    ClearBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex);

#if defined (WRITE_VISIBILITY_UNPACK_DATA) // Only needed for primary camera view, when using visibility buffer. Used to allow visibility to pack into uint32.    
    WriteMeshletVisibilityUnpackData(meshInstanceBuffer.clusterToVisibleClusterTableStartIndex, vDispatchThreadID.x);
#endif
    
    //#endif
}

[numthreads(64, 1, 1)]
void ClearMeshletCullingCSMain(const uint3 vDispatchThreadID : SV_DispatchThreadID)
{
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    
    if (perMeshBuffer[perMeshBufferIndex].numMeshlets <= vDispatchThreadID.x)
    {
        return;
    }
    
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    RWByteAddressBuffer meshletBitfieldBuffer = ResourceDescriptorHeap[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX];
    
    uint meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + vDispatchThreadID.x;
    
    ClearBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex);
}

// Rewrite occluder meshlet visibility to ensure that all meshlets that were
// marked visible last frame are included in this frame's visible cluster table.
[numthreads(64, 1, 1)]
void RewriteOccluderMeshletVisibilityCS(const uint3 vDispatchThreadID : SV_DispatchThreadID)
{
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    
    if (perMeshBuffer[perMeshBufferIndex].numMeshlets <= vDispatchThreadID.x)
    {
        return;
    }

    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];

    RWByteAddressBuffer meshletBitfieldBuffer = ResourceDescriptorHeap[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX];

    uint meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + vDispatchThreadID.x;
    
    // If meshlet was culled last frame, skip.
    if (GetBit(meshletBitfieldBuffer, meshletBitfieldIndex))
    {
        return;
    }
    
    WriteMeshletVisibilityUnpackData(meshInstanceBuffer.clusterToVisibleClusterTableStartIndex, vDispatchThreadID.x);
}