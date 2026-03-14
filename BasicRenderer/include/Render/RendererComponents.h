#pragma once

#include <BasicScene/Components.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "Materials/TechniqueDescriptor.h"
#include "Resources/Buffers/BufferView.h"
#include "ShaderBuffers.h"

class PixelBuffer;

namespace Components {

    struct DepthMap {
        DepthMap() = default;
        DepthMap(std::shared_ptr<PixelBuffer> depthMap, std::shared_ptr<PixelBuffer> linearDepthMap)
            : depthMap(depthMap), linearDepthMap(linearDepthMap) {
        }
        std::shared_ptr<PixelBuffer> depthMap;
        std::shared_ptr<PixelBuffer> linearDepthMap;
    };

    struct RenderViewRef {
        uint64_t viewID = 0;
    };

    struct LightViewInfo {
        std::vector<uint64_t> viewIDs;
        std::shared_ptr<BufferView> lightBufferView;
        uint32_t lightBufferIndex = 0;
        uint32_t viewInfoBufferIndex = 0;
        Matrix projectionMatrix;
        std::shared_ptr<PixelBuffer> depthMap;
        std::shared_ptr<PixelBuffer> linearDepthMap;
        uint32_t depthResX = 0;
        uint32_t depthResY = 0;
    };

    struct RenderableObject {
        RenderableObject() = default;
        RenderableObject(PerObjectCB cb) : perObjectCB(cb) {}
        PerObjectCB perObjectCB;
    };

    struct IndirectDrawInfo {
        std::vector<unsigned int> indices;
        std::vector<std::shared_ptr<BufferView>> views;
        std::vector<MaterialCompileFlags> materialTechniques;
    };

    struct ObjectDrawInfo {
        IndirectDrawInfo drawInfo;
        std::shared_ptr<BufferView> perObjectCBView;
        uint32_t perObjectCBIndex;
        std::shared_ptr<BufferView> normalMatrixView;
        uint32_t normalMatrixIndex;
    };

    struct PerPassMeshes {
        std::unordered_map<uint64_t, std::vector<std::shared_ptr<MeshInstance>>> meshesByPass;
    };

} // namespace Components