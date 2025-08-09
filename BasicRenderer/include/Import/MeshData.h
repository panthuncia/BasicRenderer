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