#include "Import/GlTFLoader.h"

#include <memory>
#include <string>
#include <vector>

#include <DirectXMath.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Import/GlTFGeometryExtractor.h"
#include "Materials/Material.h"
#include "Mesh/Mesh.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Utilities/Utilities.h"

using nlohmann::json;
using namespace DirectX;

namespace {

struct PrimitiveData {
    std::shared_ptr<Mesh> mesh;
};



void ApplyNodeTransform(const json& gltfNode, flecs::entity entity) {
    XMVECTOR translation = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR rotation = XMQuaternionIdentity();
    XMVECTOR scale = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);

    if (gltfNode.contains("matrix")) {
        const auto matrixValues = gltfNode["matrix"].get<std::vector<float>>();
        if (matrixValues.size() != 16) {
            throw std::runtime_error("Node matrix must contain 16 floats");
        }

        XMMATRIX matrix = XMMatrixSet(
            matrixValues[0], matrixValues[1], matrixValues[2], matrixValues[3],
            matrixValues[4], matrixValues[5], matrixValues[6], matrixValues[7],
            matrixValues[8], matrixValues[9], matrixValues[10], matrixValues[11],
            matrixValues[12], matrixValues[13], matrixValues[14], matrixValues[15]);
        XMMatrixDecompose(&scale, &rotation, &translation, matrix);
    }
    else {
        if (gltfNode.contains("translation")) {
            const auto t = gltfNode["translation"].get<std::vector<float>>();
            if (t.size() == 3) {
                translation = XMVectorSet(t[0], t[1], t[2], 0.0f);
            }
        }
        if (gltfNode.contains("rotation")) {
            const auto r = gltfNode["rotation"].get<std::vector<float>>();
            if (r.size() == 4) {
                rotation = XMVectorSet(r[0], r[1], r[2], r[3]);
            }
        }
        if (gltfNode.contains("scale")) {
            const auto s = gltfNode["scale"].get<std::vector<float>>();
            if (s.size() == 3) {
                scale = XMVectorSet(s[0], s[1], s[2], 0.0f);
            }
        }
    }

    entity.set<Components::Position>({ translation });
    entity.set<Components::Rotation>({ rotation });
    entity.set<Components::Scale>({ scale });
}

void BuildNodeHierarchy(
    std::shared_ptr<Scene> scene,
    const json& gltf,
    const std::vector<std::vector<PrimitiveData>>& meshes)
{
    if (!gltf.contains("nodes")) {
        return;
    }

    const auto& nodeArray = gltf["nodes"];
    std::vector<flecs::entity> entities(nodeArray.size());
    std::vector<bool> hasParent(nodeArray.size(), false);

    for (size_t nodeIndex = 0; nodeIndex < nodeArray.size(); ++nodeIndex) {
        const auto& gltfNode = nodeArray[nodeIndex];
        std::string nodeName = gltfNode.value("name", "glTF_Node_" + std::to_string(nodeIndex));

        if (gltfNode.contains("mesh")) {
            const size_t meshIndex = gltfNode["mesh"].get<size_t>();
            if (meshIndex >= meshes.size()) {
                throw std::runtime_error("Node mesh index out of range");
            }

            std::vector<std::shared_ptr<Mesh>> nodeMeshes;
            nodeMeshes.reserve(meshes[meshIndex].size());
            for (const auto& primitive : meshes[meshIndex]) {
                if (primitive.mesh != nullptr) {
                    nodeMeshes.push_back(primitive.mesh);
                }
            }

            entities[nodeIndex] = scene->CreateRenderableEntityECS(nodeMeshes, s2ws(nodeName));
        }
        else {
            entities[nodeIndex] = scene->CreateNodeECS(s2ws(nodeName));
        }

        ApplyNodeTransform(gltfNode, entities[nodeIndex]);
    }

    for (size_t nodeIndex = 0; nodeIndex < nodeArray.size(); ++nodeIndex) {
        const auto& gltfNode = nodeArray[nodeIndex];
        if (!gltfNode.contains("children")) {
            continue;
        }

        for (const auto& childIndexValue : gltfNode["children"]) {
            const size_t childIndex = childIndexValue.get<size_t>();
            if (childIndex >= entities.size()) {
                throw std::runtime_error("Node child index out of range");
            }

            entities[childIndex].child_of(entities[nodeIndex]);
            hasParent[childIndex] = true;
        }
    }

    std::vector<size_t> rootNodes;
    if (gltf.contains("scenes") && gltf["scenes"].is_array() && !gltf["scenes"].empty()) {
        size_t sceneIndex = gltf.value<size_t>("scene", static_cast<size_t>(0));
        if (sceneIndex >= gltf["scenes"].size()) {
            throw std::runtime_error("Default scene index out of range");
        }

        const auto& selectedScene = gltf["scenes"][sceneIndex];
        if (selectedScene.contains("nodes") && selectedScene["nodes"].is_array()) {
            rootNodes.reserve(selectedScene["nodes"].size());
            for (const auto& nodeValue : selectedScene["nodes"]) {
                const size_t rootIndex = nodeValue.get<size_t>();
                if (rootIndex >= entities.size()) {
                    throw std::runtime_error("Scene root node index out of range");
                }
                rootNodes.push_back(rootIndex);
            }
        }
    }

    if (rootNodes.empty()) {
        for (size_t nodeIndex = 0; nodeIndex < entities.size(); ++nodeIndex) {
            if (!hasParent[nodeIndex]) {
                rootNodes.push_back(nodeIndex);
            }
        }
    }

    flecs::entity sceneRoot = scene->GetRoot();
    for (const size_t rootIndex : rootNodes) {
        entities[rootIndex].child_of(sceneRoot);
    }
}

} // namespace

namespace GlTFLoader {

std::shared_ptr<Scene> LoadModel(std::string filePath) {
    try {
        auto extraction = GlTFGeometryExtractor::ExtractAll(filePath);

        auto scene = std::make_shared<Scene>();
        auto defaultMaterial = Material::GetDefaultMaterial();

        // Build mesh/primitive structure from glTF JSON
        const size_t meshCount = extraction.gltf.contains("meshes") ? extraction.gltf["meshes"].size() : 0;
        std::vector<std::vector<PrimitiveData>> allMeshes(meshCount);
        for (size_t mi = 0; mi < meshCount; ++mi) {
            const auto& meshNode = extraction.gltf["meshes"][mi];
            if (meshNode.contains("primitives")) {
                allMeshes[mi].resize(meshNode["primitives"].size());
            }
        }

        // Build GPU meshes from extracted geometry
        for (auto& ep : extraction.primitives) {
            auto mesh = ep.result.ingest.Build(
                defaultMaterial,
                std::move(ep.result.prebuiltData),
                MeshCpuDataPolicy::ReleaseAfterUpload);
            allMeshes[ep.meshIndex][ep.primitiveIndex].mesh = mesh;
        }

        BuildNodeHierarchy(scene, extraction.gltf, allMeshes);

        if (extraction.gltf.contains("animations")) {
            spdlog::warn("glTF animations are not enabled yet in custom loader: {}", filePath);
        }
        if (extraction.gltf.contains("skins")) {
            spdlog::warn("glTF skinning is not enabled yet in custom loader: {}", filePath);
        }

        return scene;
    }
    catch (const std::exception& e) {
        spdlog::error("GlTFLoader failed for {}: {}", filePath, e.what());
        return nullptr;
    }
}

} // namespace GlTFLoader
