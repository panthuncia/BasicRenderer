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
    size_t vertexCount = positions.size();
    size_t triangleCount = indices.size() / 3;

    // Initialize tangents and bitangents
    std::vector<XMFLOAT3> tangents(vertexCount, XMFLOAT3(0.0f, 0.0f, 0.0f));
    std::vector<XMFLOAT3> bitangents(vertexCount, XMFLOAT3(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i < triangleCount; ++i) {
        // Get the indices of the triangle vertices
        uint32_t index0 = indices[i * 3];
        uint32_t index1 = indices[i * 3 + 1];
        uint32_t index2 = indices[i * 3 + 2];

        // Get the vertex positions
        XMFLOAT3 pos0 = positions[index0];
        XMFLOAT3 pos1 = positions[index1];
        XMFLOAT3 pos2 = positions[index2];

        // Get the UV coordinates
        XMFLOAT2 uv0 = uvs[index0];
        XMFLOAT2 uv1 = uvs[index1];
        XMFLOAT2 uv2 = uvs[index2];

        // Calculate the edges of the triangle
        XMFLOAT3 edge1 = XMFLOAT3(pos1.x - pos0.x, pos1.y - pos0.y, pos1.z - pos0.z);
        XMFLOAT3 edge2 = XMFLOAT3(pos2.x - pos0.x, pos2.y - pos0.y, pos2.z - pos0.z);

        // Calculate the differences in UV coordinates
        XMFLOAT2 deltaUV1 = XMFLOAT2(uv1.x - uv0.x, uv1.y - uv0.y);
        XMFLOAT2 deltaUV2 = XMFLOAT2(uv2.x - uv0.x, uv2.y - uv0.y);

        // Calculate the denominator of the tangent/bitangent matrix
        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        // Calculate tangent
        XMFLOAT3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        // Calculate bitangent
        XMFLOAT3 bitangent;
        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        // Accumulate tangents and bitangents for each vertex of the triangle
        tangents[index0].x += tangent.x;
        tangents[index0].y += tangent.y;
        tangents[index0].z += tangent.z;

        tangents[index1].x += tangent.x;
        tangents[index1].y += tangent.y;
        tangents[index1].z += tangent.z;

        tangents[index2].x += tangent.x;
        tangents[index2].y += tangent.y;
        tangents[index2].z += tangent.z;

        bitangents[index0].x += bitangent.x;
        bitangents[index0].y += bitangent.y;
        bitangents[index0].z += bitangent.z;

        bitangents[index1].x += bitangent.x;
        bitangents[index1].y += bitangent.y;
        bitangents[index1].z += bitangent.z;

        bitangents[index2].x += bitangent.x;
        bitangents[index2].y += bitangent.y;
        bitangents[index2].z += bitangent.z;
    }

    // Normalize tangents and bitangents
    for (size_t i = 0; i < vertexCount; ++i) {
        XMVECTOR t = XMLoadFloat3(&tangents[i]);
        XMVECTOR b = XMLoadFloat3(&bitangents[i]);
        XMVECTOR n = XMLoadFloat3(&normals[i]);

        // Orthogonalize and normalize the tangent
        t = XMVector3Normalize(t - n * XMVector3Dot(n, t));
        XMStoreFloat3(&tangents[i], t);

        // Compute the handedness (bitangent should be perpendicular to both normal and tangent)
        XMVECTOR crossTB = XMVector3Cross(t, n);
        float handedness = (XMVector3Dot(crossTB, b).m128_f32[0] < 0.0f) ? -1.0f : 1.0f;

        // Recompute bitangent
        b = XMVector3Cross(n, t) * handedness;
        XMStoreFloat3(&bitangents[i], b);
    }

    return { tangents, bitangents };
}