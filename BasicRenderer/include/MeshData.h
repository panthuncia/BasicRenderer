#pragma once
#include "Vertex.h"

class Material;

struct GeometryData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint32_t> indices;
    std::vector<float> texcoords;
    std::vector<UINT> joints;
    std::vector<float> weights;
    std::shared_ptr<Material> material;
	unsigned int flags = 0;
};

struct MeshData {
    std::vector<GeometryData> geometries;
    int skinIndex = -1;
};
