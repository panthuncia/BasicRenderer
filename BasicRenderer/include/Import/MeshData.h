#pragma once
#include "Mesh/VertexFlags.h"

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
