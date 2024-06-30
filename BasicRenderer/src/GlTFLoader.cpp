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

size_t numComponentsForType(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    throw std::invalid_argument("Invalid accessor type");
}

size_t bytesPerComponent(int componentType) {
    switch (componentType) {
    case 5120: // BYTE
    case 5121: // UNSIGNED_BYTE
        return 1;
    case 5122: // SHORT
    case 5123: // UNSIGNED_SHORT
        return 2;
    case 5125: // UNSIGNED_INT
    case 5126: // FLOAT
        return 4;
    default:
        throw std::invalid_argument("Invalid component type");
    }
}

double readComponent(const std::vector<uint8_t>& binaryData, int componentType, size_t offset) {
    switch (componentType) {
    case 5120: // BYTE
        return static_cast<double>(*(reinterpret_cast<const int8_t*>(binaryData.data() + offset)));
    case 5121: // UNSIGNED_BYTE
        return static_cast<double>(*(reinterpret_cast<const uint8_t*>(binaryData.data() + offset)));
    case 5122: // SHORT
        return static_cast<double>(*(reinterpret_cast<const int16_t*>(binaryData.data() + offset)));
    case 5123: // UNSIGNED_SHORT
        return static_cast<double>(*(reinterpret_cast<const uint16_t*>(binaryData.data() + offset)));
    case 5125: // UNSIGNED_INT
        return static_cast<double>(*(reinterpret_cast<const uint32_t*>(binaryData.data() + offset)));
    case 5126: // FLOAT
        return static_cast<double>(*(reinterpret_cast<const float*>(binaryData.data() + offset)));
    default:
        throw std::invalid_argument("Invalid component type");
    }
}

void writeComponent(std::vector<uint8_t>& buffer, int componentType, size_t index, double value) {
    switch (componentType) {
    case 5120: { // BYTE
        int8_t v = static_cast<int8_t>(value);
        std::memcpy(buffer.data() + index * sizeof(int8_t), &v, sizeof(int8_t));
        break;
    }
    case 5121: { // UNSIGNED_BYTE
        uint8_t v = static_cast<uint8_t>(value);
        std::memcpy(buffer.data() + index * sizeof(uint8_t), &v, sizeof(uint8_t));
        break;
    }
    case 5122: { // SHORT
        int16_t v = static_cast<int16_t>(value);
        std::memcpy(buffer.data() + index * sizeof(int16_t), &v, sizeof(int16_t));
        break;
    }
    case 5123: { // UNSIGNED_SHORT
        uint16_t v = static_cast<uint16_t>(value);
        std::memcpy(buffer.data() + index * sizeof(uint16_t), &v, sizeof(uint16_t));
        break;
    }
    case 5125: { // UNSIGNED_INT
        uint32_t v = static_cast<uint32_t>(value);
        std::memcpy(buffer.data() + index * sizeof(uint32_t), &v, sizeof(uint32_t));
        break;
    }
    case 5126: { // FLOAT
        float v = static_cast<float>(value);
        std::memcpy(buffer.data() + index * sizeof(float), &v, sizeof(float));
        break;
    }
    default:
        throw std::invalid_argument("Invalid component type");
    }
}

struct Accessor {
    std::string type;
    int componentType;
    size_t count;
    size_t byteOffset;
};

struct BufferView {
    size_t byteStride;
    size_t byteOffset;
};

struct AccessorData {
    Accessor accessor;
    BufferView bufferView;
};

template <typename T>
std::vector<T> extractDataFromBuffer(const std::vector<uint8_t>& binaryData, const AccessorData& accessorData) {
    const Accessor& accessor = accessorData.accessor;
    const BufferView& bufferView = accessorData.bufferView;
    size_t numComponents = numComponentsForType(accessor.type);

    // Calculate byte stride; use the provided byteStride from bufferView if present, otherwise calculate it
    size_t byteStride = bufferView.byteStride ? bufferView.byteStride : numComponents * bytesPerComponent(accessor.componentType);

    size_t effectiveByteOffset = bufferView.byteOffset;
    if (effectiveByteOffset == (std::numeric_limits<size_t>::max)()) {
        effectiveByteOffset = 0;
    }
    if (accessor.byteOffset != (std::numeric_limits<size_t>::max)()) {
        effectiveByteOffset += accessor.byteOffset;
    }

    std::vector<T> data(accessor.count * numComponents);

    if (byteStride == numComponents * bytesPerComponent(accessor.componentType)) {
        // Non-interleaved data, we can proceed as before
        std::memcpy(data.data(), binaryData.data() + effectiveByteOffset, accessor.count * numComponents * sizeof(T));
    }
    else {
        // Interleaved data, need to manually assemble the typed array
        size_t elementSize = bytesPerComponent(accessor.componentType) * numComponents;
        size_t dataOffset = 0;

        for (size_t i = 0, byteOffset = accessor.byteOffset; i < accessor.count; i++, byteOffset += byteStride) {
            for (size_t componentIndex = 0; componentIndex < numComponents; componentIndex++) {
                size_t componentByteOffset = byteOffset + bytesPerComponent(accessor.componentType) * componentIndex;
                double value = readComponent(binaryData, accessor.componentType, effectiveByteOffset + componentByteOffset);
                data[dataOffset + componentIndex] = static_cast<T>(value);
            }
            dataOffset += numComponents;
        }
    }
    return data;
}

AccessorData getAccessorData(const json& gltfData, int accessorIndex) {
    const json& accessorJson = gltfData["accessors"][accessorIndex];
    Accessor accessor;
    accessor.type = accessorJson["type"].get<std::string>();
    accessor.componentType = accessorJson["componentType"].get<int>();
    accessor.count = accessorJson["count"].get<size_t>();
    accessor.byteOffset = accessorJson.value("byteOffset", 0);

    int bufferViewIndex = accessorJson["bufferView"].get<int>();
    const json& bufferViewJson = gltfData["bufferViews"][bufferViewIndex];
    BufferView bufferView;
    bufferView.byteStride = bufferViewJson.value("byteStride", 0);
    bufferView.byteOffset = bufferViewJson.value("byteOffset", 0);

    AccessorData accessorData = { accessor, bufferView };
    return accessorData;
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

            auto accessor = getAccessorData(gltfData, primitive["attributes"]["POSITION"]);
            geometryData.positions = extractDataFromBuffer<float>(binaryData, accessor);

            accessor = getAccessorData(gltfData, primitive["attributes"]["NORMAL"]);
            geometryData.normals = extractDataFromBuffer<float>(binaryData, accessor);

            accessor = getAccessorData(gltfData, primitive["indices"]);
            geometryData.indices = extractDataFromBuffer<uint16_t>(binaryData, accessor);

            geometryData.material = primitive["material"];

            if (primitive["attributes"].contains("TEXCOORD_0")) {
                accessor = getAccessorData(gltfData, primitive["attributes"]["TEXCOORD_0"]);
                geometryData.texcoords = extractDataFromBuffer<float>(binaryData, accessor);
            }

            if (primitive["attributes"].contains("JOINTS_0")) {
                accessor = getAccessorData(gltfData, primitive["attributes"]["JOINTS_0"]);
                geometryData.joints = extractDataFromBuffer<float>(binaryData, accessor);

                accessor = getAccessorData(gltfData, primitive["attributes"]["WEIGHTS_0"]);
                geometryData.weights = extractDataFromBuffer<float>(binaryData, accessor);
            }

            meshData.geometries.push_back(geometryData);
        }
        meshes->push_back(meshData);
    }
}

void parseGLTFNodeHierarchy(std::shared_ptr<Scene>& scene, const json& gltfData, const std::vector<MeshData>& meshesAndMaterials, std::vector<std::shared_ptr<SceneNode>>* nodes) {

    // Create SceneNode instances for each GLTF node
    for (const auto& gltfNode : gltfData["nodes"]) {
        std::shared_ptr<SceneNode> node = nullptr;

        if (gltfNode.contains("mesh")) {
            int meshIndex = gltfNode["mesh"];
            const MeshData& data = meshesAndMaterials[meshIndex];
            node = scene->CreateRenderableObject(data, gltfNode.value("name", ""));
            // Skinned mesh handling (commented out for now)
            // if (gltfNode.contains("skin")) {
            //     static_cast<RenderableObject*>(node)->skinInstance = gltfNode["skin"];
            // }
        }
        else {
            node = scene->CreateNode(gltfNode.value("name", ""));
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

        nodes->push_back(node);
    }

    // Establish parent-child relationships
    for (size_t index = 0; index < gltfData["nodes"].size(); ++index) {
        const auto& gltfNode = gltfData["nodes"][index];
        std::shared_ptr<SceneNode> node = (*nodes)[index];
        if (gltfNode.contains("children")) {
            for (int childIndex : gltfNode["children"]) {
                std::shared_ptr<SceneNode> childNode = (*nodes)[childIndex];
                node->addChild(childNode);
            }
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
        std::vector<std::shared_ptr<SceneNode>> nodes;
        parseGLTFNodeHierarchy(scene, gltfData, meshes, &nodes);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return nullptr;
    }
    return scene;
}