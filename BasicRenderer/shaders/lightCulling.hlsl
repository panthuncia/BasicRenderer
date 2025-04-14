#include "cbuffers.hlsli"
#include "structs.hlsli"

// Returns true if a sphere (with a given center and radius) intersects an AABB
// defined by aabbMin and aabbMax.
bool sphereAABBIntersection(float3 center, float radius, float3 aabbMin, float3 aabbMax) {
    // Determine the closest point on the AABB to the sphere center.
    float3 closestPoint = max(aabbMin, min(center, aabbMax));
    float3 distance = closestPoint - center;
    return dot(distance, distance) < radius;
}

// Helper function to test whether the point light at index 'i' intersects with the cluster.
bool testSphereAABB(matrix viewMatrix, float3 position, float radius, Cluster cluster) {
    // Transform the point light position into view space.
    float3 center = mul(float4(position, 1.0), viewMatrix).xyz;
    float3 aabbMin = cluster.minPoint.xyz;
    float3 aabbMax = cluster.maxPoint.xyz;
    return sphereAABBIntersection(center, radius, aabbMin, aabbMax);
}

// Compute shader entry point.
// Each thread is responsible for processing a single cluster.
[numthreads(128, 1, 1)]
void CSMain(uint3 groupID : SV_GroupID,
          uint3 threadID : SV_GroupThreadID) {
    // Compute the global index for the cluster.
    uint index = groupID.x * 128 + threadID.x;
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<unsigned int> activeLightIndices = ResourceDescriptorHeap[perFrameBuffer.activeLightIndicesBufferIndex];
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
    uint lightCount = perFrameBuffer.numLights;
    
    RWStructuredBuffer<Cluster> clusters = ResourceDescriptorHeap[lightClusterBufferDescriptorIndex];
    Cluster cluster = clusters[index];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera primaryCamera = cameras[perFrameBuffer.mainCameraIndex];

    // Reset count since culling runs every frame.
    cluster.count = 0;

    // Process every point light.
    for (uint i = 0; i < lightCount; ++i) {
        // If the light intersects the cluster and the cluster has not reached its light limit...
        LightInfo light = lights[activeLightIndices[i]];
        switch (light.type) {
            case 0: // Point light
            case 1: // Spot light
                if (testSphereAABB(primaryCamera.view, light.boundingSphere.center.xyz, light.boundingSphere.radius * light.boundingSphere.radius, cluster) && cluster.count < 100) {
                    cluster.lightIndices[cluster.count] = i;
                    cluster.count++;
                }
                break;
            case 2:
                // Directional lights always intersect the cluster.
                cluster.lightIndices[cluster.count] = i;
                cluster.count++;
                break;
        }
    }
    // Write back the updated cluster.
    clusters[index] = cluster;
}