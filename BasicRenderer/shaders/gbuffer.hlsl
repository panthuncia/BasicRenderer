#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/meshletCommon.hlsli"
#include "include/vertex.hlsli"
#include "include/utilities.hlsli"

// http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
struct BarycentricDeriv
{
    float3 m_lambda;
    float3 m_ddx;
    float3 m_ddy;
};

BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
    BarycentricDeriv ret = (BarycentricDeriv) 0;

    float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

    float2 ndc0 = pt0.xy * invW.x;
    float2 ndc1 = pt1.xy * invW.y;
    float2 ndc2 = pt2.xy * invW.z;

    float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
    ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    float ddxSum = dot(ret.m_ddx, float3(1, 1, 1));
    float ddySum = dot(ret.m_ddy, float3(1, 1, 1));

    float2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = rcp(interpInvW);

    ret.m_lambda.x = interpW * (invW[0] + deltaVec.x * ret.m_ddx.x + deltaVec.y * ret.m_ddy.x);
    ret.m_lambda.y = interpW * (0.0f + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y);
    ret.m_lambda.z = interpW * (0.0f + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z);

    ret.m_ddx *= (2.0f / winSize.x);
    ret.m_ddy *= (2.0f / winSize.y);
    ddxSum *= (2.0f / winSize.x);
    ddySum *= (2.0f / winSize.y);

    ret.m_ddy *= -1.0f;
    ddySum *= -1.0f;

    float interpW_ddx = 1.0f / (interpInvW + ddxSum);
    float interpW_ddy = 1.0f / (interpInvW + ddySum);

    ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
    ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

    return ret;
}

float3 InterpolateWithDeriv(BarycentricDeriv deriv, float v0, float v1, float v2)
{
    float3 mergedV = float3(v0, v1, v2);
    float3 ret;
    ret.x = dot(mergedV, deriv.m_lambda);
    ret.y = dot(mergedV, deriv.m_ddx);
    ret.z = dot(mergedV, deriv.m_ddy);
    return ret;
}

void LoadTriangleVertices(
    uint drawCallID,
    uint meshletLocalIndex,
    uint triLocalIndex,
    in MeshletSetup setup,
    in uint3 triangleVertexIndices,
    out Vertex v0,
    out Vertex v1,
    out Vertex v2)
{
    // Compute byte offsets into the post-skinning vertex buffer
    uint byteOffset0 = setup.postSkinningBufferOffset + (setup.vertOffset + triangleVertexIndices.x) * setup.meshBuffer.vertexByteSize;
    uint byteOffset1 = setup.postSkinningBufferOffset + (setup.vertOffset + triangleVertexIndices.y) * setup.meshBuffer.vertexByteSize;
    uint byteOffset2 = setup.postSkinningBufferOffset + (setup.vertOffset + triangleVertexIndices.z) * setup.meshBuffer.vertexByteSize;

    // Load vertices
    v0 = LoadVertex(byteOffset0, setup.vertexBuffer, setup.meshBuffer.vertexFlags);
    v1 = LoadVertex(byteOffset1, setup.vertexBuffer, setup.meshBuffer.vertexFlags);
    v2 = LoadVertex(byteOffset2, setup.vertexBuffer, setup.meshBuffer.vertexFlags);
}

void EvaluateGBuffer(in uint2 pixel)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    uint screenW = perFrameBuffer.screenResX;
    uint screenH = perFrameBuffer.screenResY;

    // .x = 7 bits for meshlet triangle index, 25 bits for visible cluster index
    // .y = 32-bit depth
    Texture2D<uint2> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    uint2 visibilityData = visibilityTexture[pixel];
    
    uint clusterIndex = visibilityData.x & 0x1FFFFFFu; // low 25 bits
    if (clusterIndex == 0x1FFFFFFu)
    {
        return; // No visible cluster
    }
    uint meshletTriangleIndex = visibilityData.x >> 25; // high 7 bits
    
    // .x = drawcall index, .y = meshlet-local triangle index
    StructuredBuffer<VisibleClusterInfo> visibleClusterTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibleClusterTable)];
    VisibleClusterInfo clusterData = visibleClusterTable[clusterIndex];

    uint perMeshInstanceBufferIndex = clusterData.drawcallIndexAndMeshletIndex.x;    
    uint drawCallMeshletIndex = clusterData.drawcallIndexAndMeshletIndex.y;
    
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer instanceData = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    float4 clip0, clip1, clip2;
    MeshletSetup meshletSetup;
    uint3 triangleIndices;
    if (!LoadTriangleClipPositions(perMeshInstanceBufferIndex, drawCallMeshletIndex, meshletTriangleIndex, meshletSetup, triangleIndices, clip0, clip1, clip2))
    {
        return; // Invalid; skip
    }

    // NDC: x in [-1,1], y in [-1,1] with Y up    
    float2 pixelUv = (float2(pixel) + 0.5f) / float2(screenW, screenH);
    float2 pixelNdc = float2(
        pixelUv.x * 2.0f - 1.0f,
        (1.0f - pixelUv.y) * 2.0f - 1.0f
    );

    BarycentricDeriv bary = CalcFullBary(clip0, clip1, clip2, pixelNdc, float2(screenW, screenH));
    
    Vertex v0, v1, v2;
    LoadTriangleVertices( // TODO: This loads positions again; optimize by saving positions earlier and loading a position-less vertex here
        perMeshInstanceBufferIndex,
        drawCallMeshletIndex,
        meshletTriangleIndex,
        meshletSetup,
        triangleIndices,
        v0,
        v1,
        v2);
    
    // TODO: Color, tangent, etc.
    float3 interpTexcoordX = InterpolateWithDeriv(bary, v0.texcoord.x, v1.texcoord.x, v2.texcoord.x);
    float3 interpTexcoordY = InterpolateWithDeriv(bary, v0.texcoord.y, v1.texcoord.y, v2.texcoord.y);
    
    float3 interpNormalX = InterpolateWithDeriv(bary, v0.normal.x, v1.normal.x, v2.normal.x);
    float3 interpNormalY = InterpolateWithDeriv(bary, v0.normal.y, v1.normal.y, v2.normal.y);
    float3 interpNormalZ = InterpolateWithDeriv(bary, v0.normal.z, v1.normal.z, v2.normal.z);
    
    // World positions
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer objectBuffer = perObjectBuffer[instanceData.perObjectBufferIndex]; // TODO: Store earlier? Store in instance buffer?
    
    float3 v0WorldPos = mul(float4(v0.position, 1.0f), objectBuffer.model).xyz;
    float3 v1WorldPos = mul(float4(v1.position, 1.0f), objectBuffer.model).xyz;
    float3 v2WorldPos = mul(float4(v2.position, 1.0f), objectBuffer.model).xyz;
    
    // TODO: Benchmark against calculating from depth
    float3 interpPositionX = InterpolateWithDeriv(bary, v0WorldPos.x, v1WorldPos.x, v2WorldPos.x);
    float3 interpPositionY = InterpolateWithDeriv(bary, v0WorldPos.y, v1WorldPos.y, v2WorldPos.y);
    float3 interpPositionZ = InterpolateWithDeriv(bary, v0WorldPos.z, v1WorldPos.z, v2WorldPos.z);
    
    float2 interpTexcoord = float2(interpTexcoordX.x, interpTexcoordY.x);
    float3 interpNormal = normalize(float3(interpNormalX.x, interpNormalY.x, interpNormalZ.x));
    float3 worldPosition = float3(interpPositionX.x, interpPositionY.x, interpPositionZ.x);
    
    StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::NormalMatrixBuffer)];
    float3x3 normalMatrix = (float3x3) normalMatrixBuffer[objectBuffer.normalMatrixBufferIndex];
    
    float3 worldNormal = normalize(mul(interpNormal, normalMatrix)).xyz;
    
    // Get material info
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[instanceData.perMeshBufferIndex];
    
    float3 dpdx = float3(interpPositionX.y, interpPositionY.y, interpPositionZ.y);
    float3 dpdy = float3(interpPositionX.z, interpPositionY.z, interpPositionZ.z);
    
    float2 dudx = float2(interpTexcoordX.y, interpTexcoordY.y);
    float2 dudy = float2(interpTexcoordX.z, interpTexcoordY.z);
    
    MaterialInputs materialInputs;
    SampleMaterialCS(
        interpTexcoord,
        worldNormal,
        worldPosition,
        meshBuffer.materialDataIndex,
        dpdx, dpdy, dudx, dudy,
        materialInputs);
    
    // Write to G-buffer
    RWTexture2D<float4> normalsTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Normals)];
    RWTexture2D<float4> albedoTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Albedo)];
    RWTexture2D<float4> emissiveTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Emissive)];
    RWTexture2D<float2> metallicRoughnessTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::MetallicRoughness)];

    normalsTexture[pixel].xyz = materialInputs.normalWS;
    albedoTexture[pixel] = float4(materialInputs.albedo, materialInputs.ambientOcclusion);
    emissiveTexture[pixel] = float4(materialInputs.emissive, 0.0f);
    metallicRoughnessTexture[pixel] = float2(materialInputs.metallic, materialInputs.roughness);
}

[numthreads(8, 8, 1)]
void PrimaryDepthCopyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    uint screenW = perFrameBuffer.screenResX;
    uint screenH = perFrameBuffer.screenResY;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
    {
        return;
    }
    
    uint2 pixel = dispatchThreadId.xy;
    // .x = 7 bits for meshlet triangle index, 25 bits for visible cluster index
    // .y = 32-bit depth
    Texture2D<uint2> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    
    uint visibilityDataY = visibilityTexture[pixel].y;
    float depth = 2147483647; // TODO: Can we get true float max in a shader?
    if (!(visibilityDataY == 0xFFFFFFFFu))
    {
        depth = asfloat(visibilityDataY);
    }

    RWTexture2D<float> linearDepthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::LinearDepthMap)];
    linearDepthTexture[pixel] = depth;
}