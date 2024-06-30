#pragma once

#include <memory>

#include "Scene.h"

struct GeometryData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint16_t> indices;
    std::vector<float> texcoords;
    std::vector<float> joints;
    std::vector<float> weights;
    int material;
};

std::shared_ptr<Scene> loadGLB(std::string file);