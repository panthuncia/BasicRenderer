#include "cbuffers.hlsli"
#include "vertex.hlsli"
#include "loadingUtils.hlsli"

struct DispatchMeshIndirectCommand {
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
    uint dispatchMeshX;
    uint dispatchMeshY;
    uint dispatchMeshZ;
};

struct DispatchIndirectCommand {
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
};

// Object culling, one thread per object
[numthreads(64, 1, 1)]
void CSMain(uint dispatchID : SV_DispatchThreadID) {
    if (dispatchID > maxDrawIndex) {
        return;
    }
    StructuredBuffer<unsigned int> activeDrawSetIndicesBuffer = ResourceDescriptorHeap[activeDrawSetIndicesBufferDescriptorIndex];
    StructuredBuffer<DispatchMeshIndirectCommand> indirectCommandBuffer = ResourceDescriptorHeap[drawSetCommandBufferDescriptorIndex];
    // Per-drawset indirect command buffer
    AppendStructuredBuffer<DispatchMeshIndirectCommand> indirectCommandOutputBuffer = ResourceDescriptorHeap[indirectCommandBufferDescriptorIndex];
    // Meshlets from all drawsets are culled together
    AppendStructuredBuffer<DispatchIndirectCommand> meshletFrustrumCullingIndirectCommandOutputBuffer = ResourceDescriptorHeap[meshletCullingIndirectCommandBufferDescriptorIndex];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
    
    uint index = activeDrawSetIndicesBuffer[dispatchID];
    DispatchMeshIndirectCommand command = indirectCommandBuffer[index];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera camera = cameras[lightViewIndex]; // In compute root signature, this directly indexes the camera buffer instead of using indirection through light view index buffers
    
    PerMeshBuffer perMesh = perMeshBuffer[command.perMeshBufferIndex];
    PerObjectBuffer perObject = perObjectBuffer[command.perObjectBufferIndex];
    
    // Frustrum culling
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
    
    RWByteAddressBuffer meshInstanceCullingBitfield = ResourceDescriptorHeap[UintRootConstant0];
    bool wasPartiallyCulledLastFrame = GetBit(meshInstanceCullingBitfield, command.perMeshInstanceBufferIndex);
    
    bool fullyInside = true;
    
    // Disable culling for skinned meshes for now, as the bounding sphere is not updated
    if (!(perMesh.vertexFlags & VERTEX_SKINNED)) { // TODO: Implement skinned mesh culling    
        for (uint i = 0; i < 6; i++) {
            float4 P = camera.clippingPlanes[i].plane; // plane normal.xyz, plane.w
            float distance = dot(P.xyz, viewSpaceCenter) + P.w;

            // fully outside?
            if (distance < -scaledBoundingRadius)
            {
                return; // reject whole object
            }

            // does it intersect this plane?
            if (abs(distance) < scaledBoundingRadius)
            {
                // Mark the per-mesh bitfield, since we'll need to reset the meshlet bitfield when this is fully inside later
                SetBitAtomic(meshInstanceCullingBitfield, command.perMeshInstanceBufferIndex);
                fullyInside = false;
            }
        }

    }
    
    indirectCommandOutputBuffer.Append(command);
    
    if (fullyInside)
    {
        if (wasPartiallyCulledLastFrame) { // Meshlet bitfield needs to be cleared, or this object may be missing meshlets
            AppendStructuredBuffer<DispatchIndirectCommand> meshletFrustrumCullingResetCommandBuffer = ResourceDescriptorHeap[UintRootConstant1];
            DispatchIndirectCommand meshletFrustrumCullingResetCommand;
            meshletFrustrumCullingResetCommand.dispatchX = command.dispatchMeshX;
            meshletFrustrumCullingResetCommand.dispatchY = command.dispatchMeshY;
            meshletFrustrumCullingResetCommand.dispatchZ = command.dispatchMeshZ;
            meshletFrustrumCullingResetCommand.perMeshBufferIndex = command.perMeshBufferIndex;
            meshletFrustrumCullingResetCommand.perMeshInstanceBufferIndex = command.perMeshInstanceBufferIndex;
            meshletFrustrumCullingResetCommand.perObjectBufferIndex = command.perObjectBufferIndex;
            
            meshletFrustrumCullingResetCommandBuffer.Append(meshletFrustrumCullingResetCommand);
            
            // Mark as not meshlet culled
            ClearBitAtomic(meshInstanceCullingBitfield, command.perMeshInstanceBufferIndex);
        }
        // If the object is fully inside the frustrum, we can skip the meshlet culling
        return;
    }
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
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    
    if (perMeshBuffer[perMeshBufferIndex].numMeshlets <= vDispatchThreadID.x)
    {
        return;
    }
    PerMeshBuffer perMesh = perMeshBuffer[perMeshBufferIndex];
    
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[perMeshInstanceBufferDescriptorIndex];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    uint meshletBoundsIndex = meshInstanceBuffer.meshletBoundsBufferStartIndex + vDispatchThreadID.x;
    
    StructuredBuffer<BoundingSphere> meshletBoundsBuffer = ResourceDescriptorHeap[UintRootConstant0];
    BoundingSphere meshletBounds = meshletBoundsBuffer[meshletBoundsIndex];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera camera = cameras[lightViewIndex];
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
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
    
        bool bCulled = false;
    
        for (uint i = 0; i < 6; i++)
        {
            float4 clippingPlane = camera.clippingPlanes[i].plane; // ZYZ normal, W distance
            float distance = dot(clippingPlane.xyz, viewSpaceCenter) + clippingPlane.w;
            float boundingRadius = perMesh.boundingSphere.sphere.w;
            bCulled |= distance < -scaledBoundingRadius;
        }
    
        if (bCulled)
        {
            // TODO: Could avoid the atomic if we use eight ExecuteIndirect calls, one for each bit of a byte
            SetBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex);
            return;
        }
    }
    ClearBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex);
}

[numthreads(64, 1, 1)]
void ClearMeshletFrustrumCullingCSMain(const uint3 vDispatchThreadID : SV_DispatchThreadID)
{
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    
    if (perMeshBuffer[perMeshBufferIndex].numMeshlets <= vDispatchThreadID.x)
    {
        return;
    }
    
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[perMeshInstanceBufferDescriptorIndex];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    RWByteAddressBuffer meshletBitfieldBuffer = ResourceDescriptorHeap[UintRootConstant1];
    
    uint meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + vDispatchThreadID.x;
    
    ClearBitAtomic(meshletBitfieldBuffer, meshletBitfieldIndex);
}
