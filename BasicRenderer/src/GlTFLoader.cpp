#include "GlTFLoader.h"

#include <iostream>
#include <fstream>

#include "nlohmann/json.h"
#include "SceneNode.h"

using nlohmann::json;

struct GLBHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t length;
};

struct GLBChunk {
    uint32_t chunkLength;
    uint32_t chunkType;
    std::vector<uint8_t> chunkData;
};

struct MeshData {
    std::vector<GeometryData> geometries;
};

template <typename T>
std::vector<T> extractDataFromBuffer(const std::vector<uint8_t>& buffer, size_t offset, size_t count) {
    std::vector<T> data(count);
    std::memcpy(data.data(), buffer.data() + offset, count * sizeof(T));
    return data;
}

std::pair<size_t, size_t> getAccessorData(const json& gltfData, int accessorIndex) {
    const auto& accessor = gltfData["accessors"][accessorIndex];
    size_t byteOffset = accessor.value("byteOffset", 0);
    size_t count = accessor["count"];
    return { byteOffset, count };
}

GLBHeader parseGLBHeader(const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 12) {
        throw std::runtime_error("Buffer too small to contain GLB header.");
    }

    GLBHeader header;
    std::memcpy(&header.magic, buffer.data(), 4);
    std::memcpy(&header.version, buffer.data() + 4, 4);
    std::memcpy(&header.length, buffer.data() + 8, 4);

    if (header.magic != 0x46546C67) { // 'glTF' in ASCII
        throw std::runtime_error("Invalid GLB magic number.");
    }

    return header;
}

json decodeJSONChunk(const GLBChunk& chunk) {
    return json::parse(chunk.chunkData.begin(), chunk.chunkData.end());
}

std::vector<uint8_t> extractBinaryChunk(const GLBChunk& chunk) {
    return chunk.chunkData;
}

std::vector<GLBChunk> parseGLBChunks(const std::vector<uint8_t>& buffer) {
    std::vector<GLBChunk> chunks;
    size_t offset = 12; // Start right after the header

    while (offset < buffer.size()) {
        if (offset + 8 > buffer.size()) {
            throw std::runtime_error("Invalid chunk header size.");
        }

        GLBChunk chunk;
        std::memcpy(&chunk.chunkLength, buffer.data() + offset, 4);
        std::memcpy(&chunk.chunkType, buffer.data() + offset + 4, 4);
        offset += 8;

        if (offset + chunk.chunkLength > buffer.size()) {
            throw std::runtime_error("Chunk length exceeds buffer size.");
        }

        chunk.chunkData.resize(chunk.chunkLength);
        std::memcpy(chunk.chunkData.data(), buffer.data() + offset, chunk.chunkLength);
        offset += chunk.chunkLength;

        chunks.push_back(chunk);
    }

    return chunks;
}

void parseMeshes(const json& gltfData, const std::vector<uint8_t>& binaryData, std::vector<MeshData>* meshes) {
    for (const auto& mesh : gltfData["meshes"]) {
        MeshData meshData;
        for (const auto& primitive : mesh["primitives"]) {
            GeometryData geometryData;

            auto [positionOffset, positionCount] = getAccessorData(gltfData, primitive["attributes"]["POSITION"]);
            geometryData.positions = extractDataFromBuffer<float>(binaryData, positionOffset, positionCount);

            auto [normalOffset, normalCount] = getAccessorData(gltfData, primitive["attributes"]["NORMAL"]);
            geometryData.normals = extractDataFromBuffer<float>(binaryData, normalOffset, normalCount);

            auto [indicesOffset, indicesCount] = getAccessorData(gltfData, primitive["indices"]);
            geometryData.indices = extractDataFromBuffer<uint16_t>(binaryData, indicesOffset, indicesCount);

            geometryData.material = primitive["material"];

            if (primitive["attributes"].contains("TEXCOORD_0")) {
                auto [texcoordOffset, texcoordCount] = getAccessorData(gltfData, primitive["attributes"]["TEXCOORD_0"]);
                geometryData.texcoords = extractDataFromBuffer<float>(binaryData, texcoordOffset, texcoordCount);
            }

            if (primitive["attributes"].contains("JOINTS_0")) {
                auto [jointsOffset, jointsCount] = getAccessorData(gltfData, primitive["attributes"]["JOINTS_0"]);
                geometryData.joints = extractDataFromBuffer<float>(binaryData, jointsOffset, jointsCount);

                auto [weightsOffset, weightsCount] = getAccessorData(gltfData, primitive["attributes"]["WEIGHTS_0"]);
                geometryData.weights = extractDataFromBuffer<float>(binaryData, weightsOffset, weightsCount);
            }

            meshData.geometries.push_back(geometryData);
        }
        meshes->push_back(meshData);
    }
}

void parseGLTFNodeHierarchy(Scene& scene, const json& gltfData, const std::vector<MeshData>& meshesAndMaterials, int maxBonesPerMesh, std::vector<std::shared_ptr<SceneNode>>* rootNodes) {
    std::vector<std::shared_ptr<SceneNode>> nodes;

    // Create SceneNode instances for each GLTF node
    for (const auto& gltfNode : gltfData["nodes"]) {
        std::shared_ptr<SceneNode> node = nullptr;

        if (gltfNode.contains("mesh")) {
            int meshIndex = gltfNode["mesh"];
            const MeshData& data = meshesAndMaterials[meshIndex];
            node = std::make_shared<RenderableObject>("");//scene.CreateRenderableObject(data, gltfNode.value("name", ""), maxBonesPerMesh);
            // Skinned mesh handling (commented out for now)
            // if (gltfNode.contains("skin")) {
            //     static_cast<RenderableObject*>(node)->skinInstance = gltfNode["skin"];
            // }
        }
        else {
            node = scene.CreateNode(gltfNode.value("name", ""));
        }

        if (gltfNode.contains("matrix")) {
            auto matrixValues = gltfNode["matrix"].get<std::vector<float>>();
            XMMATRIX matrix = XMMatrixSet(
                matrixValues[0], matrixValues[1], matrixValues[2], matrixValues[3],
                matrixValues[4], matrixValues[5], matrixValues[6], matrixValues[7],
                matrixValues[8], matrixValues[9], matrixValues[10], matrixValues[11],
                matrixValues[12], matrixValues[13], matrixValues[14], matrixValues[15]
            );

            XMVECTOR position, rotation, scale;
            XMMatrixDecompose(&scale, &rotation, &position, matrix);

            node->transform.setLocalPosition(XMFLOAT3(XMVectorGetX(position), XMVectorGetY(position), XMVectorGetZ(position)));
            node->transform.setLocalScale(XMFLOAT3(XMVectorGetX(scale), XMVectorGetY(scale), XMVectorGetZ(scale)));
            node->transform.setLocalRotationFromQuaternion(rotation);
        }
        else {
            if (gltfNode.contains("translation")) {
                auto translationValues = gltfNode["translation"].get<std::vector<float>>();
                node->transform.setLocalPosition(XMFLOAT3(translationValues[0], translationValues[1], translationValues[2]));
            }
            if (gltfNode.contains("scale")) {
                auto scaleValues = gltfNode["scale"].get<std::vector<float>>();
                node->transform.setLocalScale(XMFLOAT3(scaleValues[0], scaleValues[1], scaleValues[2]));
            }
            if (gltfNode.contains("rotation")) {
                auto rotationValues = gltfNode["rotation"].get<std::vector<float>>();
                XMVECTOR quaternion = XMVectorSet(rotationValues[0], rotationValues[1], rotationValues[2], rotationValues[3]);
                node->transform.setLocalRotationFromQuaternion(quaternion);
            }
        }

        nodes.push_back(node);
    }

    // Establish parent-child relationships
    for (size_t index = 0; index < gltfData["nodes"].size(); ++index) {
        const auto& gltfNode = gltfData["nodes"][index];
        std::shared_ptr<SceneNode> node = nodes[index];
        if (gltfNode.contains("children")) {
            for (int childIndex : gltfNode["children"]) {
                std::shared_ptr<SceneNode> childNode = nodes[childIndex];
                node->addChild(childNode);
            }
        }
    }

    // Find nodes with no parents
    for (std::shared_ptr<SceneNode> node : nodes) {
        if (node->parent == nullptr) {
            rootNodes->push_back(node);
        }
    }
}

std::shared_ptr<Scene> loadGLB(std::string fileName) {
    auto scene = std::make_shared<Scene>();
    std::ifstream file(fileName, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open GLB file." << std::endl;
        return nullptr;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    try {
        GLBHeader header = parseGLBHeader(buffer);
        std::cout << "GLB Header Parsed:" << std::endl;
        std::cout << "Magic: " << header.magic << std::endl;
        std::cout << "Version: " << header.version << std::endl;
        std::cout << "Length: " << header.length << std::endl;

        std::vector<GLBChunk> chunks = parseGLBChunks(buffer);
        std::cout << "GLB Chunks Parsed:" << std::endl;
        for (const auto& chunk : chunks) {
            std::cout << "Chunk Type: " << chunk.chunkType << ", Length: " << chunk.chunkLength << std::endl;
        }

        auto jsonChunkIt = std::find_if(chunks.begin(), chunks.end(), [](const GLBChunk& chunk) {
            return chunk.chunkType == 0x4E4F534A; // 'JSON' in ASCII
            });
        if (jsonChunkIt == chunks.end()) {
            throw std::runtime_error("JSON chunk not found.");
        }

        auto binChunkIt = std::find_if(chunks.begin(), chunks.end(), [](const GLBChunk& chunk) {
            return chunk.chunkType == 0x004E4942; // 'BIN' in ASCII
            });

        json gltfData = decodeJSONChunk(*jsonChunkIt);
        std::vector<uint8_t> binaryData = extractBinaryChunk(*binChunkIt);

        std::cout << "GLTF Data:" << std::endl;
        std::cout << gltfData.dump(2) << std::endl;

        // Continue processing GLTF data and binary data...
        std::vector<MeshData> meshes;
        parseMeshes(gltfData, binaryData, &meshes);

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return nullptr;
    }
    return nullptr;
}