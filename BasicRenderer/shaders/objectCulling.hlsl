#include "include/cbuffers.hlsli"
#include "include/vertex.hlsli"
#include "include/loadingUtils.hlsli"
#include "include/Misc/sphereScreenExtents.hlsli"
#include "include/utilities.hlsli"
#include "include/occlusionCulling.hlsli"
#include "PerPassRootConstants/objectCullingRootConstants.h"
#include "include/indirectCommands.hlsli"

[numthreads(64, 1, 1)]
void ObjectCullingCSMain(uint dispatchID : SV_DispatchThreadID)
{
    if (dispatchID > maxDrawIndex)
    {
        return;
    }

    StructuredBuffer<unsigned int> activeDrawSetIndicesBuffer = ResourceDescriptorHeap[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX];
    StructuredBuffer<DispatchMeshIndirectCommand> indirectCommandBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::IndirectCommandBuffers::Master)];
    uint index = activeDrawSetIndicesBuffer[dispatchID];
    DispatchMeshIndirectCommand command = indirectCommandBuffer[index];
    
    RWByteAddressBuffer meshInstanceVisibilityBitfield = ResourceDescriptorHeap[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX];
    bool wasVisibleLastFrame = GetBit(meshInstanceVisibilityBitfield, command.perMeshInstanceBufferIndex);
    
#if defined (OCCLUDERS_PASS) && defined(OCCLUSION_CULLING)
    if (!wasVisibleLastFrame)
    {
        return;
    }
#endif
    
    // Per-drawset indirect command buffer
    AppendStructuredBuffer<DispatchMeshIndirectCommand> indirectCommandOutputBuffer = ResourceDescriptorHeap[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX];
    // Meshlets from all drawsets are culled together
    AppendStructuredBuffer<DispatchIndirectCommand> meshletFrustrumCullingIndirectCommandOutputBuffer = ResourceDescriptorHeap[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera camera = cameras[lightViewIndex]; // In compute root signature, this directly indexes the camera buffer instead of using indirection through light view index buffers
    
    PerMeshBuffer perMesh = perMeshBuffer[command.perMeshBufferIndex];
    PerObjectBuffer perObject = perObjectBuffer[command.perObjectBufferIndex];
    
    // Culling
    float4 objectSpaceCenter = float4(perMesh.boundingSphere.sphere.xyz, 1.0);
    float4 worldSpaceCenter = mul(objectSpaceCenter, perObject.model);
    float3 viewSpaceCenter = mul(worldSpaceCenter, camera.view).xyz;
    
    // Calculate the scale factor for the bounding sphere radius
    float3 scaleFactors = float3(
    length(perObject.model[0].xyz),
    length(perObject.model[1].xyz),
    length(perObject.model[2].xyz)
);
    float maxScale = max(max(scaleFactors.x, scaleFactors.y), scaleFactors.z);
    float scaledBoundingRadius = perMesh.boundingSphere.sphere.w * maxScale;
    
#if defined (OCCLUSION_CULLING)

    bool occlusionCulled = false;
    OcclusionCulling(occlusionCulled, camera, viewSpaceCenter.xyz, -viewSpaceCenter.z, scaledBoundingRadius, camera.viewProjection);
    if (occlusionCulled)
    {
        // Update bitfield
        ClearBitAtomic(meshInstanceVisibilityBitfield, command.perMeshInstanceBufferIndex);
        return; // reject whole object
    }
#endif // defined (OCCLUSION_CULLING)
    
    bool fullyInside = true;
    
    // Disable culling for skinned meshes for now, as the bounding sphere is not updated
    if (!(perMesh.vertexFlags & VERTEX_SKINNED))
    { // TODO: Implement skinned mesh culling    
        for (uint i = 0; i < 6; i++)
        {
            float4 P = camera.clippingPlanes[i].plane; // plane normal.xyz, plane.w
            float distance = dot(P.xyz, viewSpaceCenter) + P.w;

            // fully outside?
            if (distance < -scaledBoundingRadius)
            {
                // Update bitfield
                ClearBitAtomic(meshInstanceVisibilityBitfield, command.perMeshInstanceBufferIndex);
                return; // reject whole object
            }
            // Update bitfield
            SetBitAtomic(meshInstanceVisibilityBitfield, command.perMeshInstanceBufferIndex);

            // does it intersect this plane?
            if (abs(distance) < scaledBoundingRadius)
            {
                fullyInside = false;
            }
        }

    }

#if defined (OCCLUSION_CULLING)
    bool alreadyAddedForCulling = false;
#if !defined(OCCLUDERS_PASS)
    if (!wasVisibleLastFrame) { // Don't render occluders in the new objects pass
            indirectCommandOutputBuffer.Append(command);
    } else { // All occluders should be meshlet occlusion culled    
    
#if defined(BLEND_OBJECTS) // Transparent objects cannot be occluders, so we need to draw them here
        indirectCommandOutputBuffer.Append(command);
#endif
    
        DispatchIndirectCommand meshletFrustrumCullingCommand;
        meshletFrustrumCullingCommand.dispatchX = command.dispatchMeshX;
        meshletFrustrumCullingCommand.dispatchY = command.dispatchMeshY;
        meshletFrustrumCullingCommand.dispatchZ = command.dispatchMeshZ;
        meshletFrustrumCullingCommand.perMeshBufferIndex = command.perMeshBufferIndex;
        meshletFrustrumCullingCommand.perMeshInstanceBufferIndex = command.perMeshInstanceBufferIndex;
        meshletFrustrumCullingCommand.perObjectBufferIndex = command.perObjectBufferIndex;
        
        meshletFrustrumCullingIndirectCommandOutputBuffer.Append(meshletFrustrumCullingCommand);
        alreadyAddedForCulling = true;
    }
#else
    if (wasVisibleLastFrame) { // For occluder pass, we want to draw and (remainder) cull this
        indirectCommandOutputBuffer.Append(command);
    
        DispatchIndirectCommand meshletFrustrumCullingCommand;
        meshletFrustrumCullingCommand.dispatchX = command.dispatchMeshX;
        meshletFrustrumCullingCommand.dispatchY = command.dispatchMeshY;
        meshletFrustrumCullingCommand.dispatchZ = command.dispatchMeshZ;
        meshletFrustrumCullingCommand.perMeshBufferIndex = command.perMeshBufferIndex;
        meshletFrustrumCullingCommand.perMeshInstanceBufferIndex = command.perMeshInstanceBufferIndex;
        meshletFrustrumCullingCommand.perObjectBufferIndex = command.perObjectBufferIndex;
        
        meshletFrustrumCullingIndirectCommandOutputBuffer.Append(meshletFrustrumCullingCommand);
        alreadyAddedForCulling = true;
    }
#endif // OCCLUDERS_PASS
#else // If not using occlusion culling, just output to command buffer
    indirectCommandOutputBuffer.Append(command);
#endif // OCCLUSION_CULLING
    
    
    RWByteAddressBuffer meshInstanceIsMeshletFrustrumCulledBitfield = ResourceDescriptorHeap[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX];
    // Meshlet frustrum culling
        if (fullyInside)
        {
        bool wasPartiallyFrustrumCulledLastFrame = GetBit(meshInstanceIsMeshletFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
            if (wasPartiallyFrustrumCulledLastFrame)
            { // Meshlet bitfield needs to be cleared, or this object may be missing meshlets
            AppendStructuredBuffer<DispatchIndirectCommand> meshletCullingResetCommandBuffer = ResourceDescriptorHeap[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX];
                DispatchIndirectCommand meshletFrustrumCullingResetCommand;
                meshletFrustrumCullingResetCommand.dispatchX = command.dispatchMeshX;
                meshletFrustrumCullingResetCommand.dispatchY = command.dispatchMeshY;
                meshletFrustrumCullingResetCommand.dispatchZ = command.dispatchMeshZ;
                meshletFrustrumCullingResetCommand.perMeshBufferIndex = command.perMeshBufferIndex;
                meshletFrustrumCullingResetCommand.perMeshInstanceBufferIndex = command.perMeshInstanceBufferIndex;
                meshletFrustrumCullingResetCommand.perObjectBufferIndex = command.perObjectBufferIndex;
            
                meshletCullingResetCommandBuffer.Append(meshletFrustrumCullingResetCommand);
            
                // Mark as not meshlet culled
                ClearBitAtomic(meshInstanceIsMeshletFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
        }
        // If the object is fully inside the frustrum, we can skip the meshlet culling
#if defined (OCCLUSION_CULLING)
//#if !defined (OCCLUDERS_PASS) // Except for occluders pass, where we will run a (cheaper) meshlet culling on all occluders
            return;
//#endif // !defined (OCCLUDERS_PASS)
    }
    
    if (alreadyAddedForCulling)
    {
        return; // Already added for culling
    }
#else
        }
#endif // defined (OCCLUSION_CULLING)
    
    // Mark the per-mesh meshlet-culling bitfield, since we'll need to reset the meshlet bitfield when this is fully inside later
        SetBitAtomic(meshInstanceIsMeshletFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
    
        DispatchIndirectCommand meshletFrustrumCullingCommand;
        meshletFrustrumCullingCommand.dispatchX = command.dispatchMeshX;
        meshletFrustrumCullingCommand.dispatchY = command.dispatchMeshY;
        meshletFrustrumCullingCommand.dispatchZ = command.dispatchMeshZ;
        meshletFrustrumCullingCommand.perMeshBufferIndex = command.perMeshBufferIndex;
        meshletFrustrumCullingCommand.perMeshInstanceBufferIndex = command.perMeshInstanceBufferIndex;
        meshletFrustrumCullingCommand.perObjectBufferIndex = command.perObjectBufferIndex;
        
        meshletFrustrumCullingIndirectCommandOutputBuffer.Append(meshletFrustrumCullingCommand);
    }

// Meshlet culling, one thread per meshlet, one dispatch per mesh, similar to mesh shader
[numthreads(64, 1, 1)]
void MeshletFrustrumCullingCSMain(const uint3 vDispatchThreadID : SV_DispatchThreadID)
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
    
    StructuredBuffer<BoundingSphere> meshletBoundsBuffer = ResourceDescriptorHeap[UintRootConstant0];
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

    RWByteAddressBuffer meshletBitfieldBuffer = ResourceDescriptorHeap[UintRootConstant1];
    
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
#if defined (OCCLUDERS_PASS1)
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
        OcclusionCulling(occlusionCulled, camera, viewSpaceCenter.xyz, -viewSpaceCenter.z, scaledBoundingRadius, camera.viewProjection);
        
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
    //#endif
}

[numthreads(64, 1, 1)]
void ClearMeshletFrustrumCullingCSMain(const uint3 vDispatchThreadID : SV_DispatchThreadID)
{
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    
    if (perMeshBuffer[perMeshBufferIndex].numMeshlets <= vDispatchThreadID.x)
    {
        return;
    }
    
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    RWByteAddressBuffer meshletBitfieldBuffer = ResourceDescriptorHeap[UintRootConstant1];
    
    uint meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + vDispatchThreadID.x;
    
    ClearBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex);
}
