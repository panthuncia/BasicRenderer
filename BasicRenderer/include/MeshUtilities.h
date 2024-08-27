#pragma once

#include <vector>
#include <DirectXMath.h>

#include "Vertex.h"

struct TangentBitangent {
    std::vector<XMFLOAT3> tangents;
    std::vector<XMFLOAT3> bitangents;
};

TangentBitangent calculateTangentsBitangentsIndexed(
    const std::vector<XMFLOAT3>& positions,
    const std::vector<XMFLOAT3>& normals,
    const std::vector<XMFLOAT2>& uvs,
    const std::vector<uint32_t>& indices
);