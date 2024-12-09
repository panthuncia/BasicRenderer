#include "cbuffers.hlsli"
#include "vertex.hlsli"

struct IndirectCommand {
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint3 dispatchMeshArguments;
};

[numthreads(64, 1, 1)]
void CSMain(uint dispatchID : SV_DispatchThreadID) {
    if (dispatchID > maxDrawIndex) {
        return;
    }
    StructuredBuffer<unsigned int> activeDrawSetIndicesBuffer = ResourceDescriptorHeap[activeDrawSetIndicesBufferDescriptorIndex];
    StructuredBuffer<IndirectCommand> indirectCommandBuffer = ResourceDescriptorHeap[drawSetCommandBufferDescriptorIndex];
    AppendStructuredBuffer<IndirectCommand> indirectCommandOutputBuffer = ResourceDescriptorHeap[indirectCommandBufferDescriptorIndex];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
    
    uint index = activeDrawSetIndicesBuffer[dispatchID];
    IndirectCommand command = indirectCommandBuffer[index];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera camera = cameras[lightViewIndex]; // In compute root signature, this directly indexes the camera buffer instead of using indirection through light view index buffers
    
    PerMeshBuffer perMesh = perMeshBuffer[command.perMeshBufferIndex];
    PerObjectBuffer perObject = perObjectBuffer[command.perObjectBufferIndex];
    
    // Frustrum culling
    float4 objectSpaceCenter = float4(perMesh.boundingSphere.center.xyz, 1.0);
    float4 worldSpaceCenter = mul(objectSpaceCenter, perObject.model);
    float3 viewSpaceCenter = mul(worldSpaceCenter, camera.view).xyz;
    
    // Calculate the scale factor for the bounding sphere radius
    float3 scaleFactors = float3(
    length(perObject.model[0].xyz),
    length(perObject.model[1].xyz),
    length(perObject.model[2].xyz)
);
    float maxScale = max(max(scaleFactors.x, scaleFactors.y), scaleFactors.z);
    float scaledBoundingRadius = perMesh.boundingSphere.radius * maxScale;
    
    // Disable culling for skinned meshes for now, as the bounding sphere is not updated
    if (!(perMesh.vertexFlags & VERTEX_SKINNED)) { // TODO: Implement skinned mesh culling
    
        bool bCulled = false;
    
        for (uint i = 0; i < 6; i++) {
            float4 clippingPlane = camera.clippingPlanes[i].plane; // ZYZ normal, W distance
            float distance = dot(clippingPlane.xyz, viewSpaceCenter) + clippingPlane.w;
            float boundingRadius = perMesh.boundingSphere.radius;
            bCulled |= distance < -scaledBoundingRadius; // Can I just exit here?
        }
    
        if (bCulled) {
            return;
        }
    }
    
    indirectCommandOutputBuffer.Append(command);
}