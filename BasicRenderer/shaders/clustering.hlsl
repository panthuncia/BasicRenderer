#include "cbuffers.hlsli"
#include "structs.hlsli"

// Helper function: Returns the intersection between a line (from startPoint to endPoint)
// and a plane perpendicular to the Z-axis (at zDistance).
float3 GetPositionVS(float2 texcoord, float depth, matrix inverseProjection) {
	float4 clipSpaceLocation;
	clipSpaceLocation.xy = texcoord * 2.0f - 1.0f; // convert from [0,1] to [-1,1]
	clipSpaceLocation.y *= -1;
	clipSpaceLocation.z = depth;
	clipSpaceLocation.w = 1.0f;
	float4 homogenousLocation = mul(clipSpaceLocation, inverseProjection);
	return homogenousLocation.xyz / homogenousLocation.w;
}

float3 IntersectionZPlane(float3 B, float z_dist) {
	//Because this is a Z based normal this is fixed
	float3 normal = float3(0.0, 0.0, -1.0);
	float3 d = B;
	//Computing the intersection length for the line and the plane
	float t = z_dist / d.z; //dot(normal, d);

	//Computing the actual xyz position of the point along the line
	float3 result = t * d;

	return result;
}

[numthreads(1,1,1)]
void CSMain(uint3 groupID : SV_GroupID) {
	
	ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
	float2 screenDimensions = float2(perFrame.screenResX, perFrame.screenResY);
	
	uint tileIndex = groupID.x +
                     (groupID.y * perFrame.lightClusterGridSizeX) +
                     (groupID.z * perFrame.lightClusterGridSizeX * perFrame.lightClusterGridSizeY);
	
	float2 clusterSize = rcp(float2(perFrame.lightClusterGridSizeX, perFrame.lightClusterGridSizeY));

	float2 texcoordMax = (groupID.xy + 1) * clusterSize;
	float2 texcoordMin = groupID.xy * clusterSize;

	float3 maxPointVS = max(GetPositionVS(texcoordMax, 1.0f, 0), GetPositionVS(texcoordMax, 1.0f, 1));
	float3 minPointVS = min(GetPositionVS(texcoordMin, 1.0f, 0), GetPositionVS(texcoordMin, 1.0f, 1));

    // Compute the near and far Z-values for the current depth slice (grid cell in z).
	StructuredBuffer<Camera> cameraBuffer = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
	Camera mainCamera = cameraBuffer[perFrame.mainCameraIndex];
	float clusterNear = mainCamera.zNear * pow(mainCamera.zFar / mainCamera.zNear, (float) groupID.z / (float) perFrame.lightClusterGridSizeZ);
	float clusterFar = mainCamera.zNear * pow(mainCamera.zFar / mainCamera.zNear, ((float) groupID.z + 1.0) / (float) perFrame.lightClusterGridSizeZ);

	float3 minPointNear = IntersectionZPlane(minPointVS, clusterNear);
	float3 minPointFar = IntersectionZPlane(minPointVS, clusterFar);
	float3 maxPointNear = IntersectionZPlane(maxPointVS, clusterNear);
	float3 maxPointFar = IntersectionZPlane(maxPointVS, clusterFar);

	float3 minPointAABB = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
	float3 maxPointAABB = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

	RWStructuredBuffer<Cluster> clusters = ResourceDescriptorHeap[lightClusterBufferDescriptorIndex];
    // Set the cluster bounds using component-wise minimum and maximum.
	clusters[tileIndex].minPoint = float4(min(minPointNear, minPointFar), 0.0);
	clusters[tileIndex].maxPoint = float4(max(maxPointNear, maxPointFar), 0.0);
	clusters[tileIndex].near = clusterNear;
	clusters[tileIndex].far = clusterFar;
}