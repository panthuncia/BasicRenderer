#include "cbuffers.hlsli"
#include "vertex.hlsli"
#include "loadingUtils.hlsli"
#include "Misc/sphereScreenExtents.hlsli"
#include "utilities.hlsli"

struct DispatchMeshIndirectCommand
{
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
    uint dispatchMeshX;
    uint dispatchMeshY;
    uint dispatchMeshZ;
};

struct DispatchIndirectCommand
{
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
};

// Assemble occluders, one thread per object
[numthreads(64, 1, 1)]
void BuildOccluderDrawCommandsCSMain(uint dispatchID : SV_DispatchThreadID)
{   
    if (dispatchID > maxDrawIndex)
    {
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

    RWByteAddressBuffer meshInstanceIsFrustrumCulledBitfield = ResourceDescriptorHeap[UintRootConstant0];
    bool wasPartiallyCulledLastFrame = GetBit(meshInstanceIsFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
    
    RWByteAddressBuffer meshInstanceVisibilityBitfield = ResourceDescriptorHeap[UintRootConstant3];
    bool wasVisibleLastFrame = GetBit(meshInstanceVisibilityBitfield, command.perMeshInstanceBufferIndex);
    
    if (!wasVisibleLastFrame)
    {
        return; // This was not an occluder
    }
    
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
    
    indirectCommandOutputBuffer.Append(command);
        
    if (fullyInside)
    {
        if (wasPartiallyCulledLastFrame)
        { // Meshlet bitfield needs to be cleared, or this object may be missing meshlets
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
            ClearBitAtomic(meshInstanceIsFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
        }
        // If the object is fully inside the frustrum, we can skip the meshlet culling
        return;
    }
    
    // Mark the per-mesh meshlet-culling bitfield, since we'll need to reset the meshlet bitfield when this is fully inside later
    SetBitAtomic(meshInstanceIsFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
    
    DispatchIndirectCommand meshletFrustrumCullingCommand;
    meshletFrustrumCullingCommand.dispatchX = command.dispatchMeshX;
    meshletFrustrumCullingCommand.dispatchY = command.dispatchMeshY;
    meshletFrustrumCullingCommand.dispatchZ = command.dispatchMeshZ;
    meshletFrustrumCullingCommand.perMeshBufferIndex = command.perMeshBufferIndex;
    meshletFrustrumCullingCommand.perMeshInstanceBufferIndex = command.perMeshInstanceBufferIndex;
    meshletFrustrumCullingCommand.perObjectBufferIndex = command.perObjectBufferIndex;
        
    meshletFrustrumCullingIndirectCommandOutputBuffer.Append(meshletFrustrumCullingCommand);
}

void OcclusionCulling(out bool fullyCulled, out bool partiallyCulled, in const Camera camera, float3 worldSpaceCenter, float boundingSphereDepth, float scaledBoundingRadius, matrix viewProjection)
{
    // Occlusion culling
    float3 vHZB = float3(camera.depthResX, camera.depthResY, camera.numDepthMips);
    float4 vLBRT = sphere_screen_extents(worldSpaceCenter.xyz, scaledBoundingRadius, camera.viewProjection);
    float4 vToUV = float4(0.5f, -0.5f, 0.5f, -0.5f);
    float4 vUV = saturate(vLBRT.xwzy * vToUV + 0.5f);
    float4 vAABB = vUV * vHZB.xyxy; // vHZB = [w, h, l]
    float2 vExtents = vAABB.zw - vAABB.xy; // In pixels
    
    float fMipLevel = ceil(log2(max(vExtents.x, vExtents.y)));
    fMipLevel = clamp(fMipLevel, 0.0f, vHZB.z - 1.0f);
    
    float4 occlusionDepth;
    if (camera.depthBufferArrayIndex < 0)
    { // Not a texture array
        Texture2D<float> depthBuffer = ResourceDescriptorHeap[UintRootConstant2];
        occlusionDepth = float4(
            depthBuffer.SampleLevel(g_pointClamp, vUV.xy, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.zy, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.zw, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.xw, fMipLevel));
    }
    else
    {
        Texture2DArray<float> depthBuffer = ResourceDescriptorHeap[UintRootConstant2];
        occlusionDepth = float4(
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.xy, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.zy, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.zw, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.xw, camera.depthBufferArrayIndex), fMipLevel));
    }
    occlusionDepth.x = unprojectDepth(occlusionDepth.x, camera.zNear, camera.zFar);
    occlusionDepth.y = unprojectDepth(occlusionDepth.y, camera.zNear, camera.zFar);
    occlusionDepth.z = unprojectDepth(occlusionDepth.z, camera.zNear, camera.zFar);
    occlusionDepth.w = unprojectDepth(occlusionDepth.w, camera.zNear, camera.zFar);
    
    float fMaxOcclusionDepth = max(max(occlusionDepth.x, occlusionDepth.y), max(occlusionDepth.z, occlusionDepth.w));
    fullyCulled = fMaxOcclusionDepth < boundingSphereDepth - scaledBoundingRadius;
    partiallyCulled = !fullyCulled && (fMaxOcclusionDepth > boundingSphereDepth - scaledBoundingRadius);
}

// UintRootConstant0 is a bitfield that marks meshes that will be culled per-meshlet
// UintRootConstant1 is the meshlet frustrum culling reset command buffer
// UintRootConstant2 is the downsampled depth SRV index

// Object culling, one thread per object
[numthreads(64, 1, 1)]
void CSMain(uint dispatchID : SV_DispatchThreadID)
{
    if (dispatchID > maxDrawIndex)
    {
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
   
    RWByteAddressBuffer meshInstanceVisibilityBitfield = ResourceDescriptorHeap[UintRootConstant3];

    bool occlusionCulled = false;
    bool partiallyOcclusionCulled = false;
    OcclusionCulling(occlusionCulled, partiallyOcclusionCulled, camera, worldSpaceCenter.xyz, -viewSpaceCenter.z, scaledBoundingRadius, camera.viewProjection);
    if (occlusionCulled)
    {
        // Update bitfield
        ClearBitAtomic(meshInstanceVisibilityBitfield, command.perMeshInstanceBufferIndex);
        return; // reject whole object
    }
    

    RWByteAddressBuffer meshInstanceIsFrustrumCulledBitfield = ResourceDescriptorHeap[UintRootConstant0];
    bool wasPartiallyCulledLastFrame = GetBit(meshInstanceIsFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
    
    bool wasVisibleLastFrame = GetBit(meshInstanceVisibilityBitfield, command.perMeshInstanceBufferIndex);
    
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
    
    if (wasVisibleLastFrame)
    {
        //return; // This will have been drawn by the occluders pass
    }
    
    indirectCommandOutputBuffer.Append(command);
        
    if (fullyInside)
    {
        if (wasPartiallyCulledLastFrame)
        { // Meshlet bitfield needs to be cleared, or this object may be missing meshlets
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
            ClearBitAtomic(meshInstanceIsFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
        }
        // If the object is fully inside the frustrum, we can skip the meshlet culling
        return;
    }
    
    // Mark the per-mesh meshlet-culling bitfield, since we'll need to reset the meshlet bitfield when this is fully inside later
    SetBitAtomic(meshInstanceIsFrustrumCulledBitfield, command.perMeshInstanceBufferIndex);
    
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
