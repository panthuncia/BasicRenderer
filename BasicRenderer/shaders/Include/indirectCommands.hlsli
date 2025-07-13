#ifndef __INDIRECT_COMMANDS_HLSLI__
#define __INDIRECT_COMMANDS_HLSLI__

struct DispatchMeshIndirectCommand
{
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
    uint dispatchMeshX;
    uint dispatchMeshY;
    uint dispatchMeshZ;
};

struct DispatchIndirectCommand
{
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
};

#endif // __INDIRECT_COMMANDS_HLSLI__