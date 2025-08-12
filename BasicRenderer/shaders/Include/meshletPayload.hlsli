#ifndef __MESHLET_PAYLOAD_HLSLI__
#define __MESHLET_PAYLOAD_HLSLI__
#include "Common/defines.h"

struct Payload
{
    uint MeshletIndices[AS_GROUP_SIZE];
};

#endif // __MESHLET_PAYLOAD_HLSLI__