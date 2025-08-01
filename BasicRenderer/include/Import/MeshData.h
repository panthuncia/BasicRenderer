#pragma once
#include "Mesh/VertexFlags.h"

#include <vector>
#include <numeric>   // for std::iota
#include <cstddef>   // for size_t
#include <cstdint>

class Material;

enum class InterpolationType {
    Constant,
    Uniform,
    Varying,
    Vertex,
    FaceVarying
};

struct MeshData {
    std::vector<float> positions;
    std::vector<float> normals;
	InterpolationType normalInterpolation = InterpolationType::Vertex;
    std::vector<uint32_t> indices;
    std::vector<float> texcoords;
	InterpolationType texcoordInterpolation = InterpolationType::Vertex;
    std::vector<UINT> joints;
	InterpolationType jointInterpolation = InterpolationType::Vertex;
    std::vector<float> weights;
	InterpolationType weightInterpolation = InterpolationType::Vertex;
    std::shared_ptr<Material> material;
    unsigned int flags = 0;
	int skinIndex = -1;
};

template<typename T>
static std::vector<T> RemapAttribute(
    std::vector<T> const& attr,
    size_t numComponents,
    std::vector<uint32_t> const& indices,
    InterpolationType interp)
{
    size_t cornerCount = indices.size();
    switch (interp) {
    case InterpolationType::FaceVarying:
        // one element per face vertex
        assert(attr.size() == cornerCount * numComponents);
        return attr;

    case InterpolationType::Vertex:
    case InterpolationType::Varying:
        // one element per point -> duplicate per corner
    {
        size_t vertCount = attr.size() / numComponents;
        std::vector<T> out;
        out.reserve(cornerCount * numComponents);
        for (uint32_t idx : indices) {
            assert(idx < vertCount);
            size_t base = size_t(idx) * numComponents;
            for (size_t c = 0; c < numComponents; ++c)
                out.push_back(attr[base + c]);
        }
        return out;
    }

    case InterpolationType::Uniform:
        // one element per face (assume triangles), so attr.size()==faceCount*numComp
    {
        size_t faceCount = cornerCount / 3;
        assert(attr.size() == faceCount * numComponents);
        std::vector<T> out;
        out.reserve(cornerCount * numComponents);
        for (size_t f = 0; f < faceCount; ++f) {
            size_t base = f * numComponents;
            // replicate that face value for each of its 3 corners
            for (int corner = 0; corner < 3; ++corner)
                for (size_t c = 0; c < numComponents; ++c)
                    out.push_back(attr[base + c]);
        }
        return out;
    }

    case InterpolationType::Constant:
        // one element for entire mesh
        assert(attr.size() == numComponents);
        {
            std::vector<T> out;
            out.reserve(cornerCount * numComponents);
            for (size_t i = 0; i < cornerCount; ++i)
                for (size_t c = 0; c < numComponents; ++c)
                    out.push_back(attr[c]);
            return out;
        }

    default:
        // fallback to vertex behaviour
        return RemapAttribute(attr, numComponents, indices, InterpolationType::Vertex);
    }
}

inline void RebuildFaceVarying(MeshData& meshData)
{

    meshData.positions = RemapAttribute<float>(
        meshData.positions, 3, meshData.indices, InterpolationType::Vertex);

    // normals, texcoords, joints, weights honor their own interp setting:
    meshData.normals = RemapAttribute<float>(
        meshData.normals, 3, meshData.indices, meshData.normalInterpolation);

    if (!meshData.texcoords.empty())
        meshData.texcoords = RemapAttribute<float>(
            meshData.texcoords, 2, meshData.indices, meshData.texcoordInterpolation);

    if (!meshData.joints.empty())
        meshData.joints = RemapAttribute<UINT>(
            meshData.joints, 4, meshData.indices, meshData.jointInterpolation);

    if (!meshData.weights.empty())
        meshData.weights = RemapAttribute<float>(
            meshData.weights, 4, meshData.indices, meshData.weightInterpolation);

    size_t newVertCount = meshData.positions.size() / 3;
    meshData.indices.resize(newVertCount);
    std::iota(meshData.indices.begin(), meshData.indices.end(), 0u);
}