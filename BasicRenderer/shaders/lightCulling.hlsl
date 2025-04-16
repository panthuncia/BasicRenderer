#include "cbuffers.hlsli"
#include "structs.hlsli"

// Returns true if a sphere (with a given center and radius) intersects an AABB
// defined by aabbMin and aabbMax
bool sphereAABBIntersection(float3 center, float radius, float3 aabbMin, float3 aabbMax) {
    // Determine the closest point on the AABB to the sphere center.
    float3 closestPoint = max(aabbMin, min(center, aabbMax));
    float3 distance = closestPoint - center;
    return dot(distance, distance) <= radius * radius;
}

bool testSphereAABB(matrix viewMatrix, float3 position, float radius, Cluster cluster) {
    // Transform the point light position into view space.
    float3 center = mul(float4(position, 1.0), viewMatrix).xyz;
    float3 aabbMin = cluster.minPoint.xyz;
    float3 aabbMax = cluster.maxPoint.xyz;
    return sphereAABBIntersection(center, radius, aabbMin, aabbMax);
}

unsigned int AllocatePage(RWStructuredBuffer<uint> LinkedListCounter) {
    // Allocate a new page for the light.
    uint index;
    InterlockedAdd(LinkedListCounter[0], 1, index);
    if (index > lightPagesPoolSize)
        index = LIGHT_PAGE_ADDRESS_NULL;
    
    return index;
}

int MakePageLink(int nAddress, int nNewHeadAddress, RWStructuredBuffer<Cluster> lightClustersUAV) {
    int nOldHeadAddress;
    InterlockedExchange(lightClustersUAV[nAddress].ptrFirstPage, nNewHeadAddress, nOldHeadAddress);
    return nOldHeadAddress;
}

// Compute shader entry point.
// Each thread is responsible for processing a single cluster.
[numthreads(128, 1, 1)]
void CSMain(uint3 groupID : SV_GroupID,
          uint3 threadID : SV_GroupThreadID) {
    // Compute the global index for the cluster.
    uint index = groupID.x * 128 + threadID.x;
    
    ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
    StructuredBuffer<uint> activeLightIndices = ResourceDescriptorHeap[perFrame.activeLightIndicesBufferIndex];
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrame.lightBufferIndex];
    uint lightCount = perFrame.numLights;
    RWStructuredBuffer<Cluster> clusters = ResourceDescriptorHeap[lightClusterBufferDescriptorIndex];
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera cam = cameras[perFrame.mainCameraIndex];
    RWStructuredBuffer<LightPage> lightPages = ResourceDescriptorHeap[lightPagesBufferDescriptorIndex];
    RWStructuredBuffer<uint> pageCounter = ResourceDescriptorHeap[lightPagesCounterDescriptorIndex];

    Cluster cluster = clusters[index];
    cluster.numLights = 0;
    cluster.ptrFirstPage = LIGHT_PAGE_ADDRESS_NULL;

    // We haven't allocated any page yet
    uint pageIndex = LIGHT_PAGE_ADDRESS_NULL;
    uint numLightsInPage = 0;

    for (uint i = 0; i < lightCount; ++i) {
        LightInfo L = lights[activeLightIndices[i]];

        bool intersects = false;
        if (L.type == 0 || L.type == 1) {
            intersects = testSphereAABB(cam.view, L.boundingSphere.center.xyz, L.boundingSphere.radius, cluster);
        }
        else {
            intersects = true; // directional
        }

        if (!intersects)
            continue;

        // First light in this cluster?  Allocate head page.
        if (pageIndex == LIGHT_PAGE_ADDRESS_NULL) {
            pageIndex = AllocatePage(pageCounter);
            if (pageIndex == LIGHT_PAGE_ADDRESS_NULL) {
                // Out of pages- stop
                break;
            }
            lightPages[pageIndex].ptrNextPage = LIGHT_PAGE_ADDRESS_NULL;
            cluster.ptrFirstPage = pageIndex;
            numLightsInPage = 0;
        }

        // If current page is full, allocate a new one and push it onto the front of the list
        if (numLightsInPage >= LIGHTS_PER_PAGE) {
            lightPages[pageIndex].numLightsInPage = LIGHTS_PER_PAGE;
            uint old = pageIndex;
            pageIndex = AllocatePage(pageCounter);
            if (pageIndex == LIGHT_PAGE_ADDRESS_NULL) {
                // No more pages— stop
                break;
            }
            lightPages[pageIndex].ptrNextPage = old;
            cluster.ptrFirstPage = pageIndex;
            numLightsInPage = 0;
        }

        // Write this light into the current page
        lightPages[pageIndex].lightIndices[numLightsInPage++] = activeLightIndices[i];
        cluster.numLights++;
    }

    // If we ever allocated a page, write out its final count
    if (pageIndex != LIGHT_PAGE_ADDRESS_NULL) {
        lightPages[pageIndex].numLightsInPage = numLightsInPage;
    }

    clusters[index] = cluster;
}