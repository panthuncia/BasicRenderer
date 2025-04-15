#include "cbuffers.hlsli"
#include "structs.hlsli"

// Helper function: Returns the intersection between a line (from startPoint to endPoint)
// and a plane perpendicular to the Z-axis (at zDistance).
float3 lineIntersectionWithZPlane(float3 startPoint, float3 endPoint, float zDistance) {
    float3 direction = endPoint - startPoint;
    float3 normal = float3(0.0, 0.0, 1.0);
    // Compute parameter t along the line (note: assumes the line is not parallel to the plane)
    float t = (zDistance - dot(normal, startPoint)) / dot(normal, direction);
    return startPoint + t * direction;
}

// Helper function: Converts screen-space coordinates to view space.
// The screen coordinates are normalized to [-1,1] and assumed to lie on the near plane.
float3 screenToView(float3 screenCoord, float2 screenRes, matrix inverseProjection) {
    // Normalize screenCoord to the range [-1, 1]
    float2 normCoord = float2(screenCoord.x / screenRes.x, (screenRes.y - screenCoord.y) / screenRes.y);
    //float4 ndc = float4(normCoord * 2.0 - 1.0, 0, 1.0);
    float3 ndc = float3(
        2.0 * (screenCoord.x) / screenRes.x - 1.0,
        2.0 * (screenRes.y - screenCoord.y - 1) / screenRes.y - 1.0, // y is flipped
        screenCoord.z // -> [0, 1]
    );
    // Transform to view space using the inverse projection matrix.
    float4 viewCoord = mul(float4(ndc, 1.0), inverseProjection);
    viewCoord /= viewCoord.w;
    return viewCoord.xyz;
}

[numthreads(1, 1, 1)]
void CSMain(uint3 groupID : SV_GroupID) {
	
    ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
    float2 screenDimensions = float2(perFrame.screenResX, perFrame.screenResY);
	
    uint tileIndex = groupID.x +
                     (groupID.y * perFrame.lightClusterGridSizeX) +
                     (groupID.z * perFrame.lightClusterGridSizeX * perFrame.lightClusterGridSizeY);
	
    float2 tileSize = screenDimensions / float2(perFrame.lightClusterGridSizeX, perFrame.lightClusterGridSizeY);
	
	// Determine the minimal and maximal screen-space coordinates of the tile.
    float2 minTile_screenspace = groupID.xy * tileSize;
    float2 maxTile_screenspace = (groupID.xy + 1.0) * tileSize;

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera mainCamera = cameras[perFrame.mainCameraIndex];
	
    // Convert the screen-space tile corners to view space.
    float3 minTile = screenToView(float3(minTile_screenspace, 1.0), screenDimensions, mainCamera.projectionInverse);
    float3 maxTile = screenToView(float3(maxTile_screenspace, 1.0), screenDimensions, mainCamera.projectionInverse);

    // Compute the near and far Z-values for the current depth slice (grid cell in z).
    float planeNear = -mainCamera.zNear * pow(mainCamera.zFar / mainCamera.zNear, (float) groupID.z / (float) perFrame.lightClusterGridSizeZ);
    float planeFar = -mainCamera.zNear * pow(mainCamera.zFar / mainCamera.zNear, ((float) groupID.z + 1.0) / (float) perFrame.lightClusterGridSizeZ);

    // For each tile, compute the intersection points between lines through the tile and the near/far planes.
    float3 p0 = lineIntersectionWithZPlane(float3(0, 0, 0), minTile, planeNear);
    float3 p1 = lineIntersectionWithZPlane(float3(0, 0, 0), maxTile, planeNear);
    float3 p2 = lineIntersectionWithZPlane(float3(0, 0, 0), minTile, planeFar);
    float3 p3 = lineIntersectionWithZPlane(float3(0, 0, 0), maxTile, planeFar);

    RWStructuredBuffer<Cluster> clusters = ResourceDescriptorHeap[lightClusterBufferDescriptorIndex];
    // Set the cluster bounds using component-wise minimum and maximum.
    float3 aabbMin = min(min(p0, p1), min(p2, p3));
    float3 aabbMax = max(max(p0, p1), max(p2, p3));

    clusters[tileIndex].minPoint = float4(aabbMin, 0.0);
    clusters[tileIndex].maxPoint = float4(aabbMax, 0.0);
}