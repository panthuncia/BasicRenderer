#pragma once

class Material;

struct GeometryData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint32_t> indices;
    std::vector<float> texcoords;
    std::vector<UINT> joints;
    std::vector<float> weights;
    std::shared_ptr<Material> material;
};

struct MeshData {
    std::vector<GeometryData> geometries;
};
