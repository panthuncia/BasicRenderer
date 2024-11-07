#include "structs.hlsli"

cbuffer SphereParams : register(b1) {
    float4 center;
    float radius;
    uint objectBufferIndex;
    uint cameraBufferDescriptorIndex;
    uint objectBufferDescriptorIndex;
}

struct PSInput {
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void MSMain(
    const uint uGroupThreadID : SV_GroupThreadID,
    const uint3 vGroupID : SV_GroupID,
    out vertices PSInput outputVertices[42],
    out indices uint3 outputTriangles[80]) {
    SetMeshOutputCounts(42, 80);

    const float3 positions[42] = {
        float3(-0.525731, 0.850651, 0.000000),
    float3(0.525731, 0.850651, 0.000000),
    float3(-0.525731, -0.850651, 0.000000),
    float3(0.525731, -0.850651, 0.000000),
    float3(0.000000, -0.525731, 0.850651),
    float3(0.000000, 0.525731, 0.850651),
    float3(0.000000, -0.525731, -0.850651),
    float3(0.000000, 0.525731, -0.850651),
    float3(0.850651, 0.000000, -0.525731),
    float3(0.850651, 0.000000, 0.525731),
    float3(-0.850651, 0.000000, -0.525731),
    float3(-0.850651, 0.000000, 0.525731),
    float3(-0.809017, 0.500000, 0.309017),
    float3(-0.500000, 0.309017, 0.809017),
    float3(-0.309017, 0.809017, 0.500000),
    float3(0.309017, 0.809017, 0.500000),
    float3(0.000000, 1.000000, 0.000000),
    float3(0.309017, 0.809017, -0.500000),
    float3(-0.309017, 0.809017, -0.500000),
    float3(-0.500000, 0.309017, -0.809017),
    float3(-0.809017, 0.500000, -0.309017),
    float3(-1.000000, 0.000000, 0.000000),
    float3(0.500000, 0.309017, 0.809017),
    float3(0.809017, 0.500000, 0.309017),
    float3(-0.500000, -0.309017, 0.809017),
    float3(0.000000, 0.000000, 1.000000),
    float3(-0.809017, -0.500000, -0.309017),
    float3(-0.809017, -0.500000, 0.309017),
    float3(0.000000, 0.000000, -1.000000),
    float3(-0.500000, -0.309017, -0.809017),
    float3(0.809017, 0.500000, -0.309017),
    float3(0.500000, 0.309017, -0.809017),
    float3(0.809017, -0.500000, 0.309017),
    float3(0.500000, -0.309017, 0.809017),
    float3(0.309017, -0.809017, 0.500000),
    float3(-0.309017, -0.809017, 0.500000),
    float3(0.000000, -1.000000, 0.000000),
    float3(-0.309017, -0.809017, -0.500000),
    float3(0.309017, -0.809017, -0.500000),
    float3(0.500000, -0.309017, -0.809017),
    float3(0.809017, -0.500000, -0.309017),
    float3(1.000000, 0.000000, 0.000000), //
    };
    
    // Scale and translate vertices to match the sphere's center and radius
    float3 thisPosition = positions[uGroupThreadID] * radius + center.xyz;

    const uint3 indices[80] = {
        uint3(0, 12, 14),
    uint3(11, 13, 12),
    uint3(5, 14, 13),
    uint3(12, 13, 14),
    uint3(0, 14, 16),
    uint3(5, 15, 14),
    uint3(1, 16, 15),
    uint3(14, 15, 16),
    uint3(0, 16, 18),
    uint3(1, 17, 16),
    uint3(7, 18, 17),
    uint3(16, 17, 18),
    uint3(0, 18, 20),
    uint3(7, 19, 18),
    uint3(10, 20, 19),
    uint3(18, 19, 20),
    uint3(0, 20, 12),
    uint3(10, 21, 20),
    uint3(11, 12, 21),
    uint3(20, 21, 12),
    uint3(1, 15, 23),
    uint3(5, 22, 15),
    uint3(9, 23, 22),
    uint3(15, 22, 23),
    uint3(5, 13, 25),
    uint3(11, 24, 13),
    uint3(4, 25, 24),
    uint3(13, 24, 25),
    uint3(11, 21, 27),
    uint3(10, 26, 21),
    uint3(2, 27, 26),
    uint3(21, 26, 27),
    uint3(10, 19, 29),
    uint3(7, 28, 19),
    uint3(6, 29, 28),
    uint3(19, 28, 29),
    uint3(7, 17, 31),
    uint3(1, 30, 17),
    uint3(8, 31, 30),
    uint3(17, 30, 31),
    uint3(3, 32, 34),
    uint3(9, 33, 32),
    uint3(4, 34, 33),
    uint3(32, 33, 34),
    uint3(3, 34, 36),
    uint3(4, 35, 34),
    uint3(2, 36, 35),
    uint3(34, 35, 36),
    uint3(3, 36, 38),
    uint3(2, 37, 36),
    uint3(6, 38, 37),
    uint3(36, 37, 38),
    uint3(3, 38, 40),
    uint3(6, 39, 38),
    uint3(8, 40, 39),
    uint3(38, 39, 40),
    uint3(3, 40, 32),
    uint3(8, 41, 40),
    uint3(9, 32, 41),
    uint3(40, 41, 32),
    uint3(4, 33, 25),
    uint3(9, 22, 33),
    uint3(5, 25, 22),
    uint3(33, 22, 25),
    uint3(2, 35, 27),
    uint3(4, 24, 35),
    uint3(11, 27, 24),
    uint3(35, 24, 27),
    uint3(6, 37, 29),
    uint3(2, 26, 37),
    uint3(10, 29, 26),
    uint3(37, 26, 29),
    uint3(8, 39, 31),
    uint3(6, 28, 39),
    uint3(7, 31, 28),
    uint3(39, 28, 31),
    uint3(9, 41, 23),
    uint3(8, 30, 41),
    uint3(1, 23, 30),
    uint3(41, 30, 23), //
    };


    // Output vertices
    if (uGroupThreadID < 42) {
        
        ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
        StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
        StructuredBuffer<PerObjectBuffer> objects = ResourceDescriptorHeap[objectBufferDescriptorIndex];
        
        PerObjectBuffer object = objects[objectBufferIndex];
        Camera camera = cameras[perFrameBuffer.mainCameraIndex];
        
        PSInput vertex;
        float4 pos = float4(thisPosition, 1.0f);
        float4 worldPosition = mul(mul(pos, object.model), camera.view);
        vertex.position = mul(worldPosition, camera.projection);
        
        float4 normal = float4(normalize(thisPosition - center.xyz), 1.0);
        vertex.normal = normalize(mul(mul(normal, camera.view), camera.projection).xyz);
        outputVertices[uGroupThreadID] = vertex;
    }

    // Output indices
    if (uGroupThreadID < 80) {
        outputTriangles[uGroupThreadID] = indices[uGroupThreadID];
    }
}

float4 PSMain(PSInput input) : SV_TARGET {
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}