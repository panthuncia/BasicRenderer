#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/meshletCommon.hlsli"
#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "include/visibilityPacking.hlsli"
#include "include/clodStructs.hlsli"
#include "include/clodPageAccess.hlsli"

#define CLOD_COMPRESSED_POSITIONS 1u
#define CLOD_COMPRESSED_NORMALS 4u

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
    uint4 meshInfo; // x = vertexByteSize, y = vertexFlags, z = numVertices, w = unused
    uint materialDataIndex;

    // Per-meshlet counts
    uint vertexCount;
    uint triangleCount;

    // Per-meshlet compression from CLodMeshletDescriptor
    uint bitsX;
    uint bitsY;
    uint bitsZ;
    int3 minQ;
    uint positionBitOffset;     // bit offset within page position bitstream
    uint normalWordOffset;      // word offset within page normal array
    uint triangleByteOffset;    // byte offset within page triangle stream

    // Page-level stream base byte offsets (absolute in slab)
    uint positionBitstreamBase;
    uint normalArrayBase;
    uint triangleStreamBase;

    // Mesh-wide quantization
    uint compressedPositionQuantExp;

    // Page-pool addressing (0 = not loaded)
    uint pagePoolSlabDescriptorIndex;
};

uint ReadPackedBits32(StructuredBuffer<uint> words, uint startBit, uint bitCount)
{
    if (bitCount == 0u)
    {
        return 0u;
    }

    uint wordIndex = startBit >> 5;
    uint bitOffset = startBit & 31u;
    uint packed = words[wordIndex] >> bitOffset;
    if (bitOffset + bitCount > 32u)
    {
        packed |= words[wordIndex + 1u] << (32u - bitOffset);
    }

    uint mask = (bitCount >= 32u) ? 0xffffffffu : ((1u << bitCount) - 1u);
    return packed & mask;
}

// ByteAddressBuffer variant for page-pool slab reads.
uint ReadPackedBits32_BA(ByteAddressBuffer buf, uint startBit, uint bitCount)
{
    if (bitCount == 0u)
    {
        return 0u;
    }

    uint wordIndex = startBit >> 5;
    uint bitOffset = startBit & 31u;
    uint packed = buf.Load(wordIndex * 4u) >> bitOffset;
    if (bitOffset + bitCount > 32u)
    {
        packed |= buf.Load((wordIndex + 1u) * 4u) << (32u - bitOffset);
    }

    uint mask = (bitCount >= 32u) ? 0xffffffffu : ((1u << bitCount) - 1u);
    return packed & mask;
}

float3 DecodeCompressedPosition(uint meshletLocalVertex, MeshletResolveData d)
{
    uint bitsPerVertex = d.bitsX + d.bitsY + d.bitsZ;
    uint bitCursor = d.positionBitstreamBase * 8u + d.positionBitOffset + meshletLocalVertex * bitsPerVertex;

    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint px = ReadPackedBits32_BA(slab, bitCursor, d.bitsX);
    bitCursor += d.bitsX;
    uint py = ReadPackedBits32_BA(slab, bitCursor, d.bitsY);
    bitCursor += d.bitsY;
    uint pz = ReadPackedBits32_BA(slab, bitCursor, d.bitsZ);

    int3 q = int3(px, py, pz) + d.minQ;
    float invScale = 1.0f / float(1u << d.compressedPositionQuantExp);
    return float3(q) * invScale;
}

float2 UnpackSnorm16x2(uint packed)
{
    int signedPacked = asint(packed);
    int x = (signedPacked << 16) >> 16;
    int y = signedPacked >> 16;
    float sx = max(-1.0f, (float)x / 32767.0f);
    float sy = max(-1.0f, (float)y / 32767.0f);
    return float2(sx, sy);
}

float3 OctDecodeNormal(float2 e)
{
    float3 v = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (v.z < 0.0f)
    {
        float2 folded = (1.0f - abs(v.yx)) * float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
        v.x = folded.x;
        v.y = folded.y;
    }
    return normalize(v);
}

float3 DecodeCompressedNormal(uint meshletLocalVertex, MeshletResolveData d)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint addr = d.normalArrayBase + (d.normalWordOffset + meshletLocalVertex) * 4u;
    uint packed = slab.Load(addr);
    return OctDecodeNormal(UnpackSnorm16x2(packed));
}

MeshletResolveData LoadMeshletResolveData_Wave(uint clusterIndex)
{
    MeshletResolveData d = (MeshletResolveData) 0;

    uint4 mask = WaveMatch(clusterIndex);
    uint leader = WaveFirstLaneFromMask(mask);
    bool isLeader = (WaveGetLaneIndex() == leader);

    if (isLeader)
    {
        StructuredBuffer<VisibleCluster> visibleClusterBuffer =
            ResourceDescriptorHeap[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        StructuredBuffer<PerMeshBuffer> perMeshBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];

        VisibleCluster clusterData = visibleClusterBuffer[clusterIndex];
        d.drawcallAndMeshlet.x = clusterData.instanceID;
        d.drawcallAndMeshlet.y = clusterData.localMeshletIndex;

        PerMeshInstanceBuffer inst = perMeshInstanceBuffer[d.drawcallAndMeshlet.x];
        d.objAndMesh = uint2(inst.perObjectBufferIndex, inst.perMeshBufferIndex);

        PerMeshBuffer mesh = perMeshBuffer[d.objAndMesh.y];

        const uint pageSlabDesc = clusterData.pageSlabDescriptorIndex;
        const uint pageSlabOff  = clusterData.pageSlabByteOffset;

        CLodPageHeader hdr = LoadPageHeader(pageSlabDesc, pageSlabOff);

        // Load per-meshlet descriptor
        CLodMeshletDescriptor desc = LoadMeshletDescriptor(
            pageSlabDesc, pageSlabOff, hdr.descriptorOffset, d.drawcallAndMeshlet.y);

        d.meshInfo = uint4(mesh.vertexByteSize, mesh.vertexFlags, mesh.numVertices, 0);
        d.materialDataIndex = mesh.materialDataIndex;

        // Per-meshlet from descriptor
        d.vertexCount = CLodDescVertexCount(desc);
        d.triangleCount = CLodDescTriangleCount(desc);
        d.bitsX = CLodDescBitsX(desc);
        d.bitsY = CLodDescBitsY(desc);
        d.bitsZ = CLodDescBitsZ(desc);
        d.minQ = int3(desc.minQx, desc.minQy, desc.minQz);
        d.positionBitOffset = desc.positionBitOffset;
        d.normalWordOffset = desc.normalWordOffset;
        d.triangleByteOffset = desc.triangleByteOffset;

        // Page-level stream base offsets (absolute in slab)
        d.positionBitstreamBase = pageSlabOff + hdr.positionBitstreamOffset;
        d.normalArrayBase = pageSlabOff + hdr.normalArrayOffset;
        d.triangleStreamBase = pageSlabOff + hdr.triangleStreamOffset;

        d.compressedPositionQuantExp = hdr.compressedPositionQuantExp;
        d.pagePoolSlabDescriptorIndex = pageSlabDesc;
    }

    d.drawcallAndMeshlet = WaveReadLaneAt(d.drawcallAndMeshlet, leader);
    d.objAndMesh = WaveReadLaneAt(d.objAndMesh, leader);
    d.meshInfo = WaveReadLaneAt(d.meshInfo, leader);
    d.materialDataIndex = WaveReadLaneAt(d.materialDataIndex, leader);
    d.vertexCount = WaveReadLaneAt(d.vertexCount, leader);
    d.triangleCount = WaveReadLaneAt(d.triangleCount, leader);
    d.bitsX = WaveReadLaneAt(d.bitsX, leader);
    d.bitsY = WaveReadLaneAt(d.bitsY, leader);
    d.bitsZ = WaveReadLaneAt(d.bitsZ, leader);
    d.minQ.x = WaveReadLaneAt(d.minQ.x, leader);
    d.minQ.y = WaveReadLaneAt(d.minQ.y, leader);
    d.minQ.z = WaveReadLaneAt(d.minQ.z, leader);
    d.positionBitOffset = WaveReadLaneAt(d.positionBitOffset, leader);
    d.normalWordOffset = WaveReadLaneAt(d.normalWordOffset, leader);
    d.triangleByteOffset = WaveReadLaneAt(d.triangleByteOffset, leader);
    d.positionBitstreamBase = WaveReadLaneAt(d.positionBitstreamBase, leader);
    d.normalArrayBase = WaveReadLaneAt(d.normalArrayBase, leader);
    d.triangleStreamBase = WaveReadLaneAt(d.triangleStreamBase, leader);
    d.compressedPositionQuantExp = WaveReadLaneAt(d.compressedPositionQuantExp, leader);
    d.pagePoolSlabDescriptorIndex = WaveReadLaneAt(d.pagePoolSlabDescriptorIndex, leader);

    return d;
}

uint3 DecodeTriangleCompact(uint triLocalIndex, MeshletResolveData d)
{
    uint triOffset = d.triangleStreamBase + d.triangleByteOffset + triLocalIndex * 3u;

    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint alignedOffset = (triOffset / 4u) * 4u;
    uint firstWord = slab.Load(alignedOffset);
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
        uint secondWord = slab.Load(alignedOffset + 4u);
        b2 = secondWord & 0xFFu;
    }
    else // byteOffset == 3
    {
        uint secondWord = slab.Load(alignedOffset + 4u);
        b1 = secondWord & 0xFFu;
        b2 = (secondWord >> 8u) & 0xFFu;
    }

    return uint3(b0, b1, b2);
}

// Note: relies on gs_* groupshared values being initialized.
void EvaluateGBufferOptimized(uint2 pixel)
{
    Texture2D<uint64_t> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    uint64_t vis = visibilityTexture[pixel];

   
    if (vis == 0xFFFFFFFFFFFFFFFF) // no visible geometry
        return;

    float depth;
    uint clusterIndex;
    uint meshletTriangleIndex;
    bool isBackface = false; // TODO
    UnpackVisKey(vis, depth, clusterIndex, meshletTriangleIndex);

    // Meshlet-level wave dedup (key = clusterIndex)
    // This optimization did not help much in testing; keeping because it didn't hurt.
    MeshletResolveData md = LoadMeshletResolveData_Wave(clusterIndex);

    // per-lane validity checks using broadcasted triCount
    if (meshletTriangleIndex >= md.triangleCount)
        return;

    // Triangle-level wave dedup (key = triKey) for DecodeTriangle + position loads
    // This optimization did help somewhat
    uint triKey = (clusterIndex << 7) | (meshletTriangleIndex & 0x7Fu);

    uint4 triMask = WaveMatch(triKey);
    uint triLeader = WaveFirstLaneFromMask(triMask);
    bool triIsLeader = (WaveGetLaneIndex() == triLeader);

    uint3 triIdx = DecodeTriangleCompact(meshletTriangleIndex, md);

    // Triangle indices are meshlet-local vertex indices (0..vertexCount-1).
    // Positions and normals decoded directly from per-meshlet compressed streams.
    float3 p0 = DecodeCompressedPosition(triIdx.x, md);
    float3 p1 = DecodeCompressedPosition(triIdx.y, md);
    float3 p2 = DecodeCompressedPosition(triIdx.z, md);

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

    // Attribute-only loads — normals from per-meshlet compressed stream
    float3 n0 = DecodeCompressedNormal(triIdx.x, md);
    float3 n1 = DecodeCompressedNormal(triIdx.y, md);
    float3 n2 = DecodeCompressedNormal(triIdx.z, md);

    // TODO: texcoord stream not yet in per-meshlet format
    float2 uv0 = float2(0, 0);
    float2 uv1 = float2(0, 0);
    float2 uv2 = float2(0, 0);

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

    if (isBackface) {
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

    // Motion vectors — CLod meshes are not skinned, so no previous-frame position
    float4 clipCur = mul(float4(worldPosition, 1.0f), gs_unjitteredViewProj);
    float3 prevWorldPosition = worldPosition;

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

    //Debug meshlets
    float3 viewDir = normalize(gs_camPos - worldPosition);
    float4 debugColor = lightUints(md.drawcallAndMeshlet.y, worldNormal, viewDir);

    normalsTexture[pixel].xyz = materialInputs.normalWS;
    //albedoTexture[pixel] = float4(materialInputs.albedo, materialInputs.ambientOcclusion);
    albedoTexture[pixel] = float4(debugColor.xyz, materialInputs.ambientOcclusion);
    emissiveTexture[pixel].xyz = materialInputs.emissive;
    metallicRoughnessTexture[pixel] = float2(materialInputs.metallic, materialInputs.roughness);
    motionVectorTexture[pixel] = motionVector;
}

[numthreads(8, 8, 1)]
void PerViewPrimaryDepthCopyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint screenW = UintRootConstant2;
    uint screenH = UintRootConstant3;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
    {
        return;
    }

    uint2 pixel = dispatchThreadId.xy;
    Texture2D<uint64_t> visibilityTexture = ResourceDescriptorHeap[UintRootConstant0];
    uint64_t vis = visibilityTexture[pixel];

    float depth;
    if (vis == 0xFFFFFFFFFFFFFFFF)
    {
        depth = asfloat(0x7F7FFFFF);
    }
    else
    {
        uint clusterIndex;
        uint meshletTriangleIndex;
        UnpackVisKey(vis, depth, clusterIndex, meshletTriangleIndex);
    }

    RWTexture2D<float> linearDepthTexture = ResourceDescriptorHeap[UintRootConstant1];
    linearDepthTexture[pixel] = depth;
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
    Texture2D<uint64_t> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    
    uint64_t vis = visibilityTexture[pixel];
    float depth;
    if (vis == 0xFFFFFFFFFFFFFFFF) // no visible geometry
    {
        depth = asfloat(0x7F7FFFFF); // FLT_MAX
    }
    else
    {
        uint clusterIndex;
        uint meshletTriangleIndex;
        UnpackVisKey(vis, depth, clusterIndex, meshletTriangleIndex);
    }

    RWTexture2D<float> linearDepthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::LinearDepthMap)];
    linearDepthTexture[pixel] = depth;
}