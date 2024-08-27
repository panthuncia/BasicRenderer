#include "MeshUtilities.h"

#include <set>
#include <map>
#include <stdexcept>
#include <string>

TangentBitangent calculateTangentsBitangentsIndexed(
    const std::vector<XMFLOAT3>& positions,
    const std::vector<XMFLOAT3>& normals,
    const std::vector<XMFLOAT2>& uvs,
    const std::vector<uint32_t>& indices
) {
    TangentBitangent result;
    result.tangents.resize(positions.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
    result.bitangents.resize(positions.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        // Indices
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        // vertices
        XMFLOAT3 v0 = positions[i0];
        XMFLOAT3 v1 = positions[i1];
        XMFLOAT3 v2 = positions[i2];

        // uvs
        XMFLOAT2 uv0 = uvs[i0];
        XMFLOAT2 uv1 = uvs[i1];
        XMFLOAT2 uv2 = uvs[i2];

        // deltas
        XMFLOAT3 deltaPos1(
            v1.x - v0.x,
            v1.y - v0.y,
            v1.z - v0.z
        );
        XMFLOAT3 deltaPos2(
            v2.x - v0.x,
            v2.y - v0.y,
            v2.z - v0.z
        );
        XMFLOAT2 deltaUV1(
            uv1.x - uv0.x,
            uv1.y - uv0.y
        );
        XMFLOAT2 deltaUV2(
            uv2.x - uv0.x,
            uv2.y - uv0.y
        );

        // tangent and bitangent
        float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);

        XMFLOAT3 tangent(
            (deltaPos1.x * deltaUV2.y - deltaPos2.x * deltaUV1.y) * r,
            (deltaPos1.y * deltaUV2.y - deltaPos2.y * deltaUV1.y) * r,
            (deltaPos1.z * deltaUV2.y - deltaPos2.z * deltaUV1.y) * r
        );

        XMFLOAT3 bitangent(
            (deltaPos2.x * deltaUV1.x - deltaPos1.x * deltaUV2.x) * r,
            (deltaPos2.y * deltaUV1.x - deltaPos1.y * deltaUV2.x) * r,
            (deltaPos2.z * deltaUV1.x - deltaPos1.z * deltaUV2.x) * r
        );

        // A vertex can belong to multiple triangles
        for (uint16_t idx : {i0, i1, i2}) {
            result.tangents[idx].x += tangent.x;
            result.tangents[idx].y += tangent.y;
            result.tangents[idx].z += tangent.z;

            result.bitangents[idx].x += bitangent.x;
            result.bitangents[idx].y += bitangent.y;
            result.bitangents[idx].z += bitangent.z;
        }
    }

    return result;
}