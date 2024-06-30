//#include "MeshUtilities.h"
//
//#include <set>
//#include <map>
//#include <stdexcept>
//#include <string>
//#include "Mesh.h"
//
//struct MeshData {
//    std::vector<Vertex> vertices;
//    std::vector<uint16_t> indices;
//    std::map<int, int> indexMap;
//    std::set<int> jointSet;
//    std::map<int, int> jointMap;
//    std::map<int, int> inverseJointMap;
//};
//
//void calculateTangentsBitangentsIndexed(
//    std::vector<Vertex>& vertices,
//    const std::vector<uint16_t>& indices
//) {
//    // Initialize tangents and bitangents
//    for (auto& vertex : vertices) {
//        vertex.tangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
//        vertex.bitangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
//    }
//
//    for (size_t i = 0; i < indices.size(); i += 3) {
//        // Indices
//        uint16_t i0 = indices[i];
//        uint16_t i1 = indices[i + 1];
//        uint16_t i2 = indices[i + 2];
//
//        // vertices
//        XMFLOAT3 v0 = vertices[i0].position;
//        XMFLOAT3 v1 = vertices[i1].position;
//        XMFLOAT3 v2 = vertices[i2].position;
//
//        // uvs
//        XMFLOAT2 uv0 = vertices[i0].texcoord.value_or(XMFLOAT2(0.0f, 0.0f));
//        XMFLOAT2 uv1 = vertices[i1].texcoord.value_or(XMFLOAT2(0.0f, 0.0f));
//        XMFLOAT2 uv2 = vertices[i2].texcoord.value_or(XMFLOAT2(0.0f, 0.0f));
//
//        // deltas
//        XMFLOAT3 deltaPos1(
//            v1.x - v0.x,
//            v1.y - v0.y,
//            v1.z - v0.z
//        );
//        XMFLOAT3 deltaPos2(
//            v2.x - v0.x,
//            v2.y - v0.y,
//            v2.z - v0.z
//        );
//        XMFLOAT2 deltaUV1(
//            uv1.x - uv0.x,
//            uv1.y - uv0.y
//        );
//        XMFLOAT2 deltaUV2(
//            uv2.x - uv0.x,
//            uv2.y - uv0.y
//        );
//
//        // tangent and bitangent
//        float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
//
//        XMFLOAT3 tangent(
//            (deltaPos1.x * deltaUV2.y - deltaPos2.x * deltaUV1.y) * r,
//            (deltaPos1.y * deltaUV2.y - deltaPos2.y * deltaUV1.y) * r,
//            (deltaPos1.z * deltaUV2.y - deltaPos2.z * deltaUV1.y) * r
//        );
//
//        XMFLOAT3 bitangent(
//            (deltaPos2.x * deltaUV1.x - deltaPos1.x * deltaUV2.x) * r,
//            (deltaPos2.y * deltaUV1.x - deltaPos1.y * deltaUV2.x) * r,
//            (deltaPos2.z * deltaUV1.x - deltaPos1.z * deltaUV2.x) * r
//        );
//
//        // A vertex can belong to multiple triangles
//        for (uint16_t idx : {i0, i1, i2}) {
//            vertices[idx].tangent.value().x += tangent.x;
//            vertices[idx].tangent.value().y += tangent.y;
//            vertices[idx].tangent.value().z += tangent.z;
//
//            vertices[idx].bitangent.value().x += bitangent.x;
//            vertices[idx].bitangent.value().y += bitangent.y;
//            vertices[idx].bitangent.value().z += bitangent.z;
//        }
//    }
//}
