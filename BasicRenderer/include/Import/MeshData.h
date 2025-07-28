#pragma once
#include "Mesh/VertexFlags.h"

#include <vector>
#include <numeric>   // for std::iota
#include <cstddef>   // for size_t
#include <cstdint>

class Material;

struct MeshData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint32_t> indices;
    std::vector<float> texcoords;
    std::vector<UINT> joints;
    std::vector<float> weights;
    std::shared_ptr<Material> material;
    unsigned int flags = 0;
	int skinIndex = -1;
};

template<typename T>
inline std::vector<T> DeindexAttribute(
    std::vector<T> const& attr,
    size_t numComponents,
    std::vector<uint32_t> const& indices)
{
    size_t cornerCount = indices.size();
    // face-varying case?
    if (attr.size() == cornerCount * numComponents) {
        return attr;
    }

    // otherwise treat it as per-vertex
    size_t vertCount = attr.size() / numComponents;
    std::vector<T> out;
    out.reserve(cornerCount * numComponents);

    for (uint32_t idx : indices) {
        // guard: idx < vertCount ?
        size_t baseIn = size_t(idx) * numComponents;
        for (size_t c = 0; c < numComponents; ++c) {
            out.push_back(attr[baseIn + c]);
        }
    }
    return out;
}

inline void RebuildFaceVarying(MeshData& meshData)
{
    // always duplicate positions too so that positions[]/normals[]/uvs[] all
    // have the same length == indices.size()
    meshData.positions = DeindexAttribute(meshData.positions, 3, meshData.indices);
    meshData.normals = DeindexAttribute(meshData.normals, 3, meshData.indices);

    if (!meshData.texcoords.empty())
        meshData.texcoords = DeindexAttribute(meshData.texcoords, 2, meshData.indices);

    if (!meshData.joints.empty())
        meshData.joints = DeindexAttribute(meshData.joints, 4, meshData.indices);

    if (!meshData.weights.empty())
        meshData.weights = DeindexAttribute(meshData.weights, 4, meshData.indices);

    size_t newVertCount = meshData.positions.size() / 3;
    meshData.indices.resize(newVertCount);
    std::iota(meshData.indices.begin(), meshData.indices.end(), 0u);
}