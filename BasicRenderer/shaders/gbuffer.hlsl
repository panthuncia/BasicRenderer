#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/meshletCommon.hlsli"
#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"

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

float3 LoadPositionOnly(uint baseByteOffset, ByteAddressBuffer buffer)
{
    return LoadFloat3(baseByteOffset, buffer); // position is first
}

float3 LoadNormalOnly(uint baseByteOffset, ByteAddressBuffer buffer)
{
    return LoadFloat3(baseByteOffset + 12u, buffer); // normal after position
}

float2 LoadTexcoordOnly(uint baseByteOffset, ByteAddressBuffer buffer, uint flags)
{
    if (flags & VERTEX_TEXCOORDS)
        return LoadFloat2(baseByteOffset + 24u, buffer); // texcoord after pos+normal

    return float2(0.0, 0.0);
}

void ComputeTriVertexByteOffsets(in MeshletSetup setup, uint3 triIdx, out uint o0, out uint o1, out uint o2)
{
    uint stride = setup.meshBuffer.vertexByteSize;
    uint base = setup.postSkinningBufferOffset + setup.vertOffset * stride;

    o0 = base + triIdx.x * stride;
    o1 = base + triIdx.y * stride;
    o2 = base + triIdx.z * stride;
}

groupshared uint gs_screenW;
groupshared uint gs_screenH;
groupshared float3 gs_camPos;

groupshared float4x4 gs_view;
groupshared float4x4 gs_prevView;
groupshared float4x4 gs_proj; // jittered, matches visibility pass
groupshared float4x4 gs_unjitteredProj;

groupshared float4x4 gs_viewProj; // view * proj (row-vector convention)
groupshared float4x4 gs_unjitteredViewProj; // view * unjitteredProj
groupshared float4x4 gs_prevUnjitteredViewProj; // prevView * unjitteredProj

void InitGroupConstants(uint groupIndex)
{
    if (groupIndex == 0)
    {
        ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
        gs_screenW = perFrame.screenResX;
        gs_screenH = perFrame.screenResY;

        StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        Camera cam = cameras[perFrame.mainCameraIndex];

        gs_camPos = cam.positionWorldSpace.xyz;

        gs_view = cam.view;
        gs_prevView = cam.prevView;
        gs_proj = cam.projection;
        gs_unjitteredProj = cam.unjitteredProjection;

        gs_viewProj = mul(gs_view, gs_proj);
        gs_unjitteredViewProj = mul(gs_view, gs_unjitteredProj);
        gs_prevUnjitteredViewProj = mul(gs_prevView, gs_unjitteredProj);
    }

    GroupMemoryBarrierWithGroupSync();
}

struct MeshletResolveData {
    // Identifiers
    uint2 drawcallAndMeshlet; // x = perMeshInstanceBufferIndex (drawcall), y = drawCallMeshletIndex
    uint2 objAndMesh; // x = perObjectBufferIndex, y = perMeshBufferIndex

    // Mesh info
    uint4 meshInfo; // x = vertexByteSize, y = vertexFlags, z = numVertices, w = meshletTrianglesBufferOffset
    uint materialDataIndex;

    // Meshlet info
    uint4 meshletInfo; // x = vertOffset, y = triOffset, z = vertCount, w = triCount

    // Post-skinning ping-pong byte offsets
    uint2 postBases; // x = postSkinningBase, y = prevPostSkinningBase
};

MeshletResolveData LoadMeshletResolveData_Wave(uint clusterIndex)
{
    MeshletResolveData d = (MeshletResolveData) 0;

    uint4 mask = WaveMatch(clusterIndex);
    uint leader = WaveFirstLaneFromMask(mask);
    bool isLeader = (WaveGetLaneIndex() == leader);

    if (isLeader)
    {
        ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];

        StructuredBuffer<VisibleClusterInfo> visibleClusterTable =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibleClusterTable)];
        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        StructuredBuffer<PerMeshBuffer> perMeshBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
        StructuredBuffer<Meshlet> meshletBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletOffsets)];

        VisibleClusterInfo clusterData = visibleClusterTable[clusterIndex];
        d.drawcallAndMeshlet = clusterData.drawcallIndexAndMeshletIndex;

        PerMeshInstanceBuffer inst = perMeshInstanceBuffer[d.drawcallAndMeshlet.x];
        d.objAndMesh = uint2(inst.perObjectBufferIndex, inst.perMeshBufferIndex);

        PerMeshBuffer mesh = perMeshBuffer[d.objAndMesh.y];

        d.meshInfo = uint4(mesh.vertexByteSize, mesh.vertexFlags, mesh.numVertices, mesh.meshletTrianglesBufferOffset);
        d.materialDataIndex = mesh.materialDataIndex;

        Meshlet m = meshletBuffer[mesh.meshletBufferOffset + d.drawcallAndMeshlet.y];
        d.meshletInfo = uint4(m.VertOffset, m.TriOffset, m.VertCount, m.TriCount);

        // ping-pong base offsets
        uint postBase = inst.postSkinningVertexBufferOffset;
        uint prevBase = postBase;

        if (mesh.vertexFlags & VERTEX_SKINNED)
        {
            uint strideBytes = mesh.vertexByteSize * mesh.numVertices;
            postBase += strideBytes * (perFrame.frameIndex % 2);
            prevBase += strideBytes * ((perFrame.frameIndex + 1) % 2);
        }

        d.postBases = uint2(postBase, prevBase);
    }

    // Broadcast
    d.drawcallAndMeshlet = WaveReadLaneAt(d.drawcallAndMeshlet, leader);
    d.objAndMesh = WaveReadLaneAt(d.objAndMesh, leader);
    d.meshInfo = WaveReadLaneAt(d.meshInfo, leader);
    d.materialDataIndex = WaveReadLaneAt(d.materialDataIndex, leader);
    d.meshletInfo = WaveReadLaneAt(d.meshletInfo, leader);
    d.postBases = WaveReadLaneAt(d.postBases, leader);

    return d;
}

uint3 DecodeTriangleCompact(uint triLocalIndex, MeshletResolveData d, ByteAddressBuffer meshletTrianglesBuffer)
{
    // triOffsetBytes = meshletTrianglesBufferOffset + meshletTriOffset + triLocalIndex * 3
    uint triOffset = d.meshInfo.w + d.meshletInfo.y + triLocalIndex * 3u;

    uint alignedOffset = (triOffset / 4u) * 4u;
    uint firstWord = meshletTrianglesBuffer.Load(alignedOffset);
    uint byteOffset = triOffset % 4u;

    uint b0 = (firstWord >> (byteOffset * 8u)) & 0xFFu;
    uint b1, b2;

    if (byteOffset <= 1u)
    {
        b1 = (firstWord >> ((byteOffset + 1u) * 8u)) & 0xFFu;
        b2 = (firstWord >> ((byteOffset + 2u) * 8u)) & 0xFFu;
    }
    else if (byteOffset == 2u)
    {
        b1 = (firstWord >> ((byteOffset + 1u) * 8u)) & 0xFFu;
        uint secondWord = meshletTrianglesBuffer.Load(alignedOffset + 4u);
        b2 = secondWord & 0xFFu;
    }
    else // byteOffset == 3
    {
        uint secondWord = meshletTrianglesBuffer.Load(alignedOffset + 4u);
        b1 = secondWord & 0xFFu;
        b2 = (secondWord >> 8u) & 0xFFu;
    }

    return uint3(b0, b1, b2);
}

void ComputeTriVertexByteOffsetsCompact(MeshletResolveData d, uint3 triIdx, out uint o0, out uint o1, out uint o2)
{
    uint stride = d.meshInfo.x; // vertexByteSize
    uint base = d.postBases.x + d.meshletInfo.x * stride; // postBase + vertOffset * stride

    o0 = base + triIdx.x * stride;
    o1 = base + triIdx.y * stride;
    o2 = base + triIdx.z * stride;
}


// Note: relies on gs_* groupshared values being initialized.
void EvaluateGBufferOptimized(uint2 pixel)
{
    Texture2D<uint2> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    uint2 vis = visibilityTexture[pixel];

    uint depthBitsRaw = vis.y;
    bool isFrontFace = ((depthBitsRaw & 0x80000000u) != 0);

    uint clusterIndex = vis.x & 0x1FFFFFFu;
    if (clusterIndex == 0x1FFFFFFu)
        return;

    uint meshletTriangleIndex = vis.x >> 25;

    // Meshlet-level wave dedup (key = clusterIndex)
    // This optimization did not help much in testing; keeping because it didn't hurt.
    MeshletResolveData md = LoadMeshletResolveData_Wave(clusterIndex);

    // per-lane validity checks using broadcasted triCount
    if (meshletTriangleIndex >= md.meshletInfo.w) // triCount
        return;

    // Resources (no need to broadcast handles)
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)];

    // Triangle-level wave dedup (key = triKey) for DecodeTriangle + position loads
    // This optimization did help somewhat
    uint triKey = (clusterIndex << 7) | (meshletTriangleIndex & 0x7Fu);

    uint4 triMask = WaveMatch(triKey);
    uint triLeader = WaveFirstLaneFromMask(triMask);
    bool triIsLeader = (WaveGetLaneIndex() == triLeader);

    uint3 triIdx = 0;
    float3 p0 = 0, p1 = 0, p2 = 0;

    if (triIsLeader)
    {
        triIdx = DecodeTriangleCompact(meshletTriangleIndex, md, meshletTrianglesBuffer);

        uint o0, o1, o2;
        ComputeTriVertexByteOffsetsCompact(md, triIdx, o0, o1, o2);

        p0 = LoadPositionOnly(o0, vertexBuffer);
        p1 = LoadPositionOnly(o1, vertexBuffer);
        p2 = LoadPositionOnly(o2, vertexBuffer);
    }

    triIdx = WaveReadLaneAt(triIdx, triLeader);
    p0 = WaveReadLaneAt(p0, triLeader);
    p1 = WaveReadLaneAt(p1, triLeader);
    p2 = WaveReadLaneAt(p2, triLeader);

    uint o0, o1, o2;
    ComputeTriVertexByteOffsetsCompact(md, triIdx, o0, o1, o2);

    // Object buffer (per-lane; is it worth broadcasting?)
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer obj = perObjectBuffer[md.objAndMesh.x];

    float4x4 objectToClip = mul(obj.model, gs_viewProj);
    float4 clip0 = mul(float4(p0, 1.0f), objectToClip);
    float4 clip1 = mul(float4(p1, 1.0f), objectToClip);
    float4 clip2 = mul(float4(p2, 1.0f), objectToClip);

    float2 winSize = float2(gs_screenW, gs_screenH);
    float2 pixelUv = (float2(pixel) + 0.5f) / winSize;
    float2 pixelNdc = float2(pixelUv.x * 2.0f - 1.0f, (1.0f - pixelUv.y) * 2.0f - 1.0f);

    BarycentricDeriv bary = CalcFullBary(clip0, clip1, clip2, pixelNdc, winSize);

    // Attribute-only loads
    float3 n0 = LoadNormalOnly(o0, vertexBuffer);
    float3 n1 = LoadNormalOnly(o1, vertexBuffer);
    float3 n2 = LoadNormalOnly(o2, vertexBuffer);

    float2 uv0 = LoadTexcoordOnly(o0, vertexBuffer, md.meshInfo.y);
    float2 uv1 = LoadTexcoordOnly(o1, vertexBuffer, md.meshInfo.y);
    float2 uv2 = LoadTexcoordOnly(o2, vertexBuffer, md.meshInfo.y);

    // Interpolate UV + derivs
    float3 interpU = InterpolateWithDeriv(bary, uv0.x, uv1.x, uv2.x);
    float3 interpV = InterpolateWithDeriv(bary, uv0.y, uv1.y, uv2.y);

    float2 uv = float2(interpU.x, interpV.x);
    float2 dudx = float2(interpU.y, interpV.y);
    float2 dudy = float2(interpU.z, interpV.z);

    // Interpolate position in post-skinning/object space, then transform once
    float3 interpPosX = InterpolateWithDeriv(bary, p0.x, p1.x, p2.x);
    float3 interpPosY = InterpolateWithDeriv(bary, p0.y, p1.y, p2.y);
    float3 interpPosZ = InterpolateWithDeriv(bary, p0.z, p1.z, p2.z);

    float3 posOS = float3(interpPosX.x, interpPosY.x, interpPosZ.x);
    float3 dpdxOS = float3(interpPosX.y, interpPosY.y, interpPosZ.y);
    float3 dpdyOS = float3(interpPosX.z, interpPosY.z, interpPosZ.z);

    float3 worldPosition = mul(float4(posOS, 1.0f), obj.model).xyz;

    float3x3 M = (float3x3) obj.model;
    float3 dpdx = mul(dpdxOS, M);
    float3 dpdy = mul(dpdyOS, M);

    // Interpolate normal in object space
    float3 interpNX = InterpolateWithDeriv(bary, n0.x, n1.x, n2.x).x;
    float3 interpNY = InterpolateWithDeriv(bary, n0.y, n1.y, n2.y).x;
    float3 interpNZ = InterpolateWithDeriv(bary, n0.z, n1.z, n2.z).x;
    float3 normalOS = normalize(float3(interpNX.x, interpNY.x, interpNZ.x));

    StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::NormalMatrixBuffer)];
    float3x3 normalMatrix = (float3x3) normalMatrixBuffer[obj.normalMatrixBufferIndex];

    float3 worldNormal = normalize(mul(normalOS, normalMatrix));

    if (!isFrontFace) {
        worldNormal = -worldNormal;
    }

    // Material fetch
    MaterialInputs materialInputs;
    SampleMaterialCS(
        uv,
        worldNormal,
        worldPosition,
        md.materialDataIndex,
        dpdx, dpdy, dudx, dudy,
        materialInputs);

    // Motion vectors from per-pixel world position, no per-vertex reprojection
    // TODO: I think this is correct? Validate.
    float4 clipCur = mul(float4(worldPosition, 1.0f), gs_unjitteredViewProj);

    float3 prevWorldPosition = worldPosition;

    uint vertexFlags = md.meshInfo.y;
    if (vertexFlags & VERTEX_SKINNED)
    {
        uint stride = md.meshInfo.x;
        uint vertOffset = md.meshletInfo.x;
        uint prevBase = md.postBases.y;
        // For skinned meshes, reconstruct prev position from prev post-skinning buffer

        uint o0Prev = prevBase + triIdx.x * stride;
        uint o1Prev = prevBase + triIdx.y * stride;
        uint o2Prev = prevBase + triIdx.z * stride;

        float3 p0Prev = LoadPositionOnly(o0Prev, vertexBuffer);
        float3 p1Prev = LoadPositionOnly(o1Prev, vertexBuffer);
        float3 p2Prev = LoadPositionOnly(o2Prev, vertexBuffer);

        float3 prevX = InterpolateWithDeriv(bary, p0Prev.x, p1Prev.x, p2Prev.x);
        float3 prevY = InterpolateWithDeriv(bary, p0Prev.y, p1Prev.y, p2Prev.y);
        float3 prevZ = InterpolateWithDeriv(bary, p0Prev.z, p1Prev.z, p2Prev.z);

        float3 prevPosOS = float3(prevX.x, prevY.x, prevZ.x);
        prevWorldPosition = mul(float4(prevPosOS, 1.0f), obj.prevModel).xyz;
    }

    float4 clipPrev = mul(float4(prevWorldPosition, 1.0f), gs_prevUnjitteredViewProj);

    float2 ndcCur = (clipCur.xy / clipCur.w);
    float2 ndcPrev = (clipPrev.xy / clipPrev.w);

    float2 motionVector = ndcCur - ndcPrev;

    // Write G-buffer (still unpacked, TODO)
    RWTexture2D<float4> normalsTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Normals)];
    RWTexture2D<float4> albedoTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Albedo)];
    RWTexture2D<float4> emissiveTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Emissive)];
    RWTexture2D<float2> metallicRoughnessTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::MetallicRoughness)];
    RWTexture2D<float2> motionVectorTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::MotionVectors)];

    normalsTexture[pixel].xyz = materialInputs.normalWS;
    albedoTexture[pixel] = float4(materialInputs.albedo, materialInputs.ambientOcclusion);
    emissiveTexture[pixel].xyz = materialInputs.emissive;
    metallicRoughnessTexture[pixel] = float2(materialInputs.metallic, materialInputs.roughness);
    motionVectorTexture[pixel] = motionVector;
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
    float depth = asfloat(0x7F7FFFFF); // FLT_MAX
    if (!(visibilityDataY == 0xFFFFFFFFu))
    {
        depth = asfloat(visibilityDataY);
        // Sign bit was stolen for face orientation; clear it
        depth = asfloat(asuint(depth) & 0x7FFFFFFF);
    }

    RWTexture2D<float> linearDepthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::LinearDepthMap)];
    linearDepthTexture[pixel] = depth;
}