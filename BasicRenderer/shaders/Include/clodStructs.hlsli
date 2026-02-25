#ifndef CLOD_STRUCTS_HLSLI
#define CLOD_STRUCTS_HLSLI

struct MeshInstanceClodOffsets
{
    uint groupsBase;
    uint childrenBase;
    uint meshletsBase;

    uint meshletBoundsBase;
    uint lodNodesBase;
    uint rootNode; // node index (relative to lodNodesBase) to start traversal from
    uint groupChunkTableBase;
    uint groupChunkTableCount;
};

struct ClusterLODGroupChunk
{
    uint vertexChunkByteOffset;
    uint meshletVerticesBase;
    uint groupVertexCount;
    uint meshletVertexCount;
};
struct ClodBounds
{
    float4 centerAndRadius; // xyz = center, w = radius
    float error; // simplification error in mesh space
};

struct ClusterLODChild
{
    int refinedGroup; // -1 => terminal meshlets bucket
    uint firstLocalMeshletIndex; // group-local contiguous start meshlet index
    uint localMeshletCount;
    uint pad0;
};

struct ClusterLODGroup
{
    ClodBounds bounds; // center/radius/error
    
    uint firstMeshlet;
    uint meshletCount;
    int depth;

    uint firstGroupVertex;
    uint groupVertexCount;
    uint firstChild;
    uint childCount;

    uint terminalChildCount;
    uint pad0[3];
};

#endif // CLOD_STRUCTS_HLSLI