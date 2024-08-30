#include "GlTFLoader.h"

#include <iostream>
#include <fstream>
#include <streambuf>

#include "nlohmann/json.h"
#include "SceneNode.h"
#include "stb/stb_image.h"
#include "Texture.h"
#include "Material.h"
#include "PSOManager.h"
#include "Sampler.h"
#include "PixelBuffer.h"
#include "Animation.h"
#include "Skeleton.h"

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


template<typename T>
std::vector<uint32_t> convertToUint32(const std::vector<T>& input) {
    std::vector<uint32_t> output(input.size());
    std::transform(input.begin(), input.end(), output.begin(),
        [](T val) { return static_cast<uint32_t>(val); });
    return output;
}

std::vector<uint32_t> extractIndexDataAsUint32(const std::vector<uint8_t>& binaryData, const AccessorData& accessorData) {
    // Determine the type and perform the necessary conversion
    switch (accessorData.accessor.componentType) {
    case 5121:  // UNSIGNED_BYTE
        return convertToUint32(extractDataFromBuffer<uint8_t>(binaryData, accessorData));
    case 5123:  // UNSIGNED_SHORT
        return convertToUint32(extractDataFromBuffer<uint16_t>(binaryData, accessorData));
    case 5125:  // UNSIGNED_INT
        return extractDataFromBuffer<uint32_t>(binaryData, accessorData);
    default:
        throw std::invalid_argument("Unsupported index component type");
    }
}

void parseMeshes(const json& gltfData, const std::vector<uint8_t>& binaryData, const std::vector<std::shared_ptr<Material>>& materials, std::vector<MeshData>* meshes) {
    for (const auto& mesh : gltfData["meshes"]) {
        MeshData meshData;
        for (const auto& primitive : mesh["primitives"]) {
            GeometryData geometryData;

            auto accessor = getAccessorData(gltfData, primitive["attributes"]["POSITION"]);
            auto positions = extractDataFromBuffer<float>(binaryData, accessor);
            for (size_t i = 0; i < positions.size(); i += 3) {
                positions[i + 2] = -positions[i + 2]; // Flip Z-coordinate
            }
            geometryData.positions = positions;

            accessor = getAccessorData(gltfData, primitive["attributes"]["NORMAL"]);
            auto normals = extractDataFromBuffer<float>(binaryData, accessor);
            for (size_t i = 0; i < normals.size(); i += 3) {
                normals[i + 2] = -normals[i + 2]; // Flip Z-component of normals
            }
            geometryData.normals = normals;

            accessor = getAccessorData(gltfData, primitive["indices"]);
            auto indices = extractIndexDataAsUint32(binaryData, accessor);
            for (size_t i = 0; i < indices.size(); i += 3) {
                std::swap(indices[i + 1], indices[i + 2]); // Reverse winding order
            }
            geometryData.indices = indices;

            geometryData.material = materials[primitive["material"]];

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

void parseGLTFNodeHierarchy(std::shared_ptr<Scene>& scene, const json& gltfData, const std::vector<MeshData>& meshes, std::vector<std::shared_ptr<SceneNode>>* nodes) {

    // Create SceneNode instances for each GLTF node
    for (const auto& gltfNode : gltfData["nodes"]) {
        std::shared_ptr<SceneNode> node = nullptr;

        if (gltfNode.contains("mesh")) {
            int meshIndex = gltfNode["mesh"];
            const MeshData& data = meshes[meshIndex];
            node = scene->CreateRenderableObject(data, gltfNode.value("name", ""));
            
            // Skinned mesh handling. Identify them now, bind to skins later
            if (gltfNode.contains("skin")) {
                auto renderableNode = std::dynamic_pointer_cast<RenderableObject>(node);
                if (renderableNode) {
                    renderableNode->m_fileLocalSkinIndex = gltfNode["skin"]; //hack for setting skins later
                }
            }
        }
        else {
            node = scene->CreateNode(gltfNode.value("name", ""));
        }

        if (gltfNode.contains("matrix")) {
            auto matrixValues = gltfNode["matrix"].get<std::vector<float>>();
            // Manually flip the Z-axis translation components in the transformation matrix
            matrixValues[8] = -matrixValues[8];
            matrixValues[9] = -matrixValues[9];
            matrixValues[10] = -matrixValues[10];
            matrixValues[11] = -matrixValues[11];

            XMMATRIX matrix = XMMatrixSet(
                matrixValues[0], matrixValues[1], matrixValues[2], matrixValues[3],
                matrixValues[4], matrixValues[5], matrixValues[6], matrixValues[7],
                matrixValues[8], matrixValues[9], matrixValues[10], matrixValues[11],
                matrixValues[12], matrixValues[13], matrixValues[14], matrixValues[15]
            );


            XMVECTOR position, rotation, scale;
            XMMatrixDecompose(&scale, &rotation, &position, matrix);

            node->transform.setLocalPosition(XMFLOAT3(XMVectorGetX(position), XMVectorGetY(position), -XMVectorGetZ(position)));
            node->transform.setLocalScale(XMFLOAT3(XMVectorGetX(scale), XMVectorGetY(scale), XMVectorGetZ(scale)));
            node->transform.setLocalRotationFromQuaternion(rotation);
        }
        else {
            if (gltfNode.contains("translation")) {
                auto translationValues = gltfNode["translation"].get<std::vector<float>>();
                node->transform.setLocalPosition(XMFLOAT3(translationValues[0], translationValues[1], -translationValues[2]));
            }
            if (gltfNode.contains("scale")) {
                auto scaleValues = gltfNode["scale"].get<std::vector<float>>();
                node->transform.setLocalScale(XMFLOAT3(scaleValues[0], scaleValues[1], scaleValues[2]));
            }
            if (gltfNode.contains("rotation")) {
                auto rotationValues = gltfNode["rotation"].get<std::vector<float>>();
                XMVECTOR quaternion = XMVectorSet(rotationValues[0], rotationValues[1], -rotationValues[2], -rotationValues[3]);
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


// Function to decode base64 data URI (if needed)
std::vector<uint8_t> decodeDataUri(const std::string& uri) {
    const std::string base64Prefix = "data:image/";
    if (uri.find(base64Prefix) != 0) {
        throw std::runtime_error("Unsupported URI format");
    }

    std::string base64Data = uri.substr(uri.find(",") + 1);
    std::vector<uint8_t> decodedData(base64Data.size() * 3 / 4);
    // Decode base64 data (use a base64 decoding library or implement one)
    // This part is omitted for brevity
    return decodedData;
}

// Function to load images stored in binary format
std::vector<std::vector<uint8_t>> getGLTFImagesFromBinary(const json& gltfData, const std::vector<uint8_t>& binaryData) {
    std::vector<std::vector<uint8_t>> images;
    if (!gltfData.contains("images")) {
        return images;
    }

    for (const auto& gltfImage : gltfData["images"]) {
        std::vector<uint8_t> imageBuffer;

        if (gltfImage.contains("uri")) {
            std::string uri = gltfImage["uri"];
            if (uri.rfind("data:", 0) == 0) {
                imageBuffer = decodeDataUri(uri);
                images.push_back(imageBuffer);
            }
            else {
                throw std::runtime_error("External URIs unsupported in glb files");
            }
        }
        else if (gltfImage.contains("bufferView")) {
            int bufferViewIndex = gltfImage["bufferView"];
            const auto& bufferView = gltfData["bufferViews"][bufferViewIndex];
            size_t byteOffset = bufferView["byteOffset"];
            size_t byteLength = bufferView["byteLength"];
            imageBuffer.assign(binaryData.begin() + byteOffset, binaryData.begin() + byteOffset + byteLength);
            images.push_back(imageBuffer);
        }
    }

    return images;
}


std::string loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::vector<std::shared_ptr<Texture>> loadTexturesFromImages(const json& gltfData, const std::vector<std::vector<uint8_t>>& images, std::vector<std::shared_ptr<Sampler>>& samplers, bool sRGB) {
    // Create buffers from images
    std::vector<std::shared_ptr<PixelBuffer>> pixelBuffers;
    for (const auto& imageData : images) {
        int width, height, channels;
        stbi_uc* image = stbi_load_from_memory(imageData.data(), static_cast<int>(imageData.size()), &width, &height, &channels, 4);
        if (!image) {
            throw std::runtime_error("Failed to load texture image from binary data");
        }

        auto pBuffer = std::make_shared<PixelBuffer>(image, width, height, sRGB);
        pixelBuffers.push_back(std::move(pBuffer));

        stbi_image_free(image);
    }

    // Associate buffers and samplers into textures
    std::vector<std::shared_ptr<Texture>> textures;
    for (const auto& gltfTexture : gltfData["textures"]) {
        const uint32_t imageIndex = gltfTexture["source"].get<uint32_t>();
        const uint32_t samplerIndex = gltfTexture["sampler"].get<uint32_t>();
        auto pTexture = std::make_shared<Texture>(pixelBuffers[imageIndex], samplers[samplerIndex]);
        textures.push_back(std::move(pTexture));
    }

    return textures;
}

// Helper functions to translate glTF sampler parameters to D3D12 equivalents
D3D12_TEXTURE_ADDRESS_MODE translateWrapMode(int wrapMode) {
    switch (wrapMode) {
    case 33071: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case 33648: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case 10497: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    default:    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

D3D12_FILTER translateFilterMode(int magFilter, int minFilter) {
    if (magFilter == 9729 && minFilter == 9729) {
        return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    }
    else if (magFilter == 9728 && minFilter == 9728) {
        return D3D12_FILTER_MIN_MAG_MIP_POINT;
    }
    else if (magFilter == 9729 && minFilter == 9728) {
        return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    }
    else if (magFilter == 9728 && minFilter == 9729) {
        return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    }
    else {
        // May need to add more cases for extensions
        return D3D12_FILTER_MIN_MAG_MIP_LINEAR; // Default fallback
    }
}

std::vector<std::shared_ptr<Sampler>> parseGLTFSamplers(const json& gltfData) {
    std::vector<std::shared_ptr<Sampler>> samplers;
    if (gltfData.contains("samplers")) {
        for (const auto& gltfSampler : gltfData["samplers"]) {
            D3D12_SAMPLER_DESC samplerDesc = {};

            // Set filtering modes based on the glTF sampler definitions
            samplerDesc.Filter = translateFilterMode(gltfSampler.value("magFilter", 9729),
                gltfSampler.value("minFilter", 9729));
            samplerDesc.AddressU = translateWrapMode(gltfSampler.value("wrapS", 10497));
            samplerDesc.AddressV = translateWrapMode(gltfSampler.value("wrapT", 10497));
            samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.MipLODBias = 0.0f;
            samplerDesc.MaxAnisotropy = 1;
            samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            samplerDesc.BorderColor[0] = 1.0f;
            samplerDesc.BorderColor[1] = 1.0f;
            samplerDesc.BorderColor[2] = 1.0f;
            samplerDesc.BorderColor[3] = 1.0f;
            samplerDesc.MinLOD = 0.f;
            samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

            auto pSampler = std::make_shared<Sampler>(samplerDesc);
            samplers.push_back(std::move(pSampler));
        }
    }
    return samplers;
}

std::vector<std::shared_ptr<Material>> parseGLTFMaterials(const json& gltfData, const std::string& dir, bool linearBaseColor = false, const std::vector<uint8_t>& binaryData = {}) {
    std::unordered_map<int, std::shared_ptr<Texture>> linearTextures;
    std::unordered_map<int, std::shared_ptr<Texture>> srgbTextures;
    std::vector<std::shared_ptr<Material>> materials;

    std::shared_ptr<Texture> defaultTexture = Material::createDefaultTexture();

    // Load images from binary data
    auto imageBuffers = getGLTFImagesFromBinary(gltfData, binaryData);
    auto samplers = parseGLTFSamplers(gltfData);
    auto textures = loadTexturesFromImages(gltfData, imageBuffers, samplers, linearBaseColor);

    // Map textures to indices
    for (size_t i = 0; i < textures.size(); ++i) {
        linearTextures[i] = textures[i];
        srgbTextures[i] = textures[i];
    }

    // Create materials
    for (const auto& gltfMaterial : gltfData["materials"]) {
        UINT psoFlags = 0;
        std::shared_ptr<Texture> baseColorTexture = nullptr;
        std::shared_ptr<Texture> normalTexture = nullptr;
        std::shared_ptr<Texture> aoMap = nullptr;
        std::shared_ptr<Texture> metallicRoughnessTexture = nullptr;
        std::shared_ptr<Texture> emissiveTexture = nullptr;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        DirectX::XMFLOAT4 baseColorFactor(1.0f, 1.0f, 1.0f, 1.0f);
        DirectX::XMFLOAT4 emissiveFactor(0.0f, 0.0f, 0.0f, 1.0f);
        BlendState blendMode = BlendState::BLEND_STATE_OPAQUE; // Opaque blend mode

        if (gltfMaterial.contains("pbrMetallicRoughness")) {
            psoFlags |= PSOFlags::PBR;
            const auto& pbr = gltfMaterial["pbrMetallicRoughness"];
            if (pbr.contains("baseColorTexture")) {
                psoFlags |= PSOFlags::BASE_COLOR_TEXTURE;
                int textureIndex = pbr["baseColorTexture"]["index"];
                if (linearBaseColor) {
                    baseColorTexture = linearTextures[textureIndex];
                    //srgbTextures[textureIndex]->textureResource.Reset();
                }
                else {
                    baseColorTexture = srgbTextures[textureIndex];
                    //linearTextures[textureIndex]->textureResource.Reset();
                }
            }
            if (pbr.contains("metallicRoughnessTexture")) {
                psoFlags |= PSOFlags::PBR_MAPS;
                int textureIndex = pbr["metallicRoughnessTexture"]["index"];
                metallicRoughnessTexture = linearTextures[textureIndex];
                //srgbTextures[textureIndex]->textureResource.Reset();
            }
            if (pbr.contains("metallicFactor")) {
                metallicFactor = pbr["metallicFactor"];
            }
            if (pbr.contains("roughnessFactor")) {
                roughnessFactor = pbr["roughnessFactor"];
            }
            if (pbr.contains("baseColorFactor")) {
                baseColorFactor = { pbr["baseColorFactor"][0], pbr["baseColorFactor"][1], pbr["baseColorFactor"][2], pbr["baseColorFactor"][3] };
            }
        }

        if (gltfMaterial.contains("normalTexture")) {
            psoFlags |= PSOFlags::NORMAL_MAP;
            int textureIndex = gltfMaterial["normalTexture"]["index"];
            normalTexture = linearTextures[textureIndex];
            //srgbTextures[textureIndex]->textureResource.Reset();
        }
        if (gltfMaterial.contains("occlusionTexture")) {
            psoFlags |= PSOFlags::AO_TEXTURE;
            int textureIndex = gltfMaterial["occlusionTexture"]["index"];
            aoMap = linearTextures[textureIndex];
            //srgbTextures[textureIndex]->textureResource.Reset();
        }
        if (gltfMaterial.contains("emissiveTexture")) {
            psoFlags |= PSOFlags::EMISSIVE_TEXTURE;
            int textureIndex = gltfMaterial["emissiveTexture"]["index"];
            emissiveTexture = srgbTextures[textureIndex];
            //linearTextures[textureIndex]->textureResource.Reset();
            emissiveFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        if (gltfMaterial.contains("emissiveFactor")) {
            emissiveFactor = { gltfMaterial["emissiveFactor"][0], gltfMaterial["emissiveFactor"][1], gltfMaterial["emissiveFactor"][2], 1.0f };
        }

        if (gltfMaterial.contains("alphaMode")) {
            std::string alphaMode = gltfMaterial["alphaMode"];
            if (alphaMode == "MASK") {
                blendMode = BlendState::BLEND_STATE_MASK;
            }
            else if (alphaMode == "BLEND") {
                blendMode = BlendState::BLEND_STATE_BLEND;
            }
        }
        std::shared_ptr<Material> newMaterial = std::make_shared<Material>(gltfMaterial["name"], psoFlags,
            baseColorTexture,
            normalTexture,
            aoMap,
            nullptr,
            metallicRoughnessTexture,
            metallicFactor,
            roughnessFactor,
            baseColorFactor,
            emissiveFactor,
            blendMode);
        materials.push_back(newMaterial);
    }

    return materials;
}

UINT numComponentsForPath(std::string path) {
    if (path == "translation" || path == "scale") {
        return 3;
    }
    else if (path == "rotation") {
        return 4;
    }
    else {
        spdlog::error("Unsupported path in glTF animation");
        return 0;
    }
}

std::shared_ptr<Animation> parseGLTFAnimationToClips(const json& gltfAnimation, const json& gltfData, const std::vector<uint8_t>& binaryData, const std::vector<std::shared_ptr<SceneNode>>& nodes) {
    auto animation = std::make_shared<Animation>(gltfAnimation.value("name", ""));

    for (const auto& channel : gltfAnimation["channels"]) {
        int samplerIndex = channel["sampler"].get<int>();
        const auto& sampler = gltfAnimation["samplers"][samplerIndex];
        AccessorData inputAccessor = getAccessorData(gltfData, sampler["input"]);
        AccessorData outputAccessor = getAccessorData(gltfData, sampler["output"]);

        std::vector<float> inputs = extractDataFromBuffer<float>(binaryData, inputAccessor); // Time keyframes
        std::vector<float> outputs = extractDataFromBuffer<float>(binaryData, outputAccessor); // Value keyframes

        std::string path = channel["target"]["path"];
        int node = nodes[channel["target"]["node"]]->localID;

        if (animation->nodesMap.find(node) == animation->nodesMap.end()) {
            animation->nodesMap[node] = std::make_shared<AnimationClip>();
        }

        for (size_t i = 0; i < inputs.size(); ++i) {
            float time = inputs[i];
            size_t numComponents = numComponentsForPath(path);
            std::vector<float> value(outputs.begin() + i * numComponents, outputs.begin() + (i + 1) * numComponents);

            if (path == "translation") {
                animation->nodesMap[node]->addPositionKeyframe(time, XMFLOAT3(value[0], value[1], value[2]));
            }
            else if (path == "rotation") {
                animation->nodesMap[node]->addRotationKeyframe(time, XMVectorSet(value[0], value[1], value[2], value[3]));
            }
            else if (path == "scale") {
                animation->nodesMap[node]->addScaleKeyframe(time, XMFLOAT3(value[0], value[1], value[2]));
            }
        }
    }

    return animation;
}

std::vector<std::shared_ptr<Animation>> parseGLTFAnimations(const json& gltfData, std::vector<uint8_t>& binaryData, std::vector<std::shared_ptr<SceneNode>>& nodes) {
    std::vector<std::shared_ptr<Animation>> animations;

    if (!gltfData.contains("animations")) {
        return animations;
    }

    for (const auto& animation : gltfData["animations"]) {
        std::shared_ptr<Animation> parsedAnimation = parseGLTFAnimationToClips(animation, gltfData, binaryData, nodes);
        animations.push_back(parsedAnimation);
    }

    return animations;
}

std::vector<std::shared_ptr<Skeleton>> parseGLTFSkins(const json& gltfData, std::vector<uint8_t>& binaryData, std::vector<std::shared_ptr<SceneNode>>& nodes, std::vector<std::shared_ptr<Animation>>& animations) {
    std::vector<std::shared_ptr<Skeleton>> skins;
    if (!gltfData.contains("skins")) {
        return skins;
    }
    for (const auto& skin : gltfData["skins"]) {
        std::vector<float> inverseBindMatrices = extractDataFromBuffer<float>(binaryData, getAccessorData(gltfData, skin["inverseBindMatrices"]));
        std::vector<std::shared_ptr<SceneNode>> joints;
        for (const auto& joint : skin["joints"]) {
            joints.push_back(nodes[joint.get<uint32_t>()]);
        }
        auto skeleton = std::make_shared<Skeleton>(joints, inverseBindMatrices);

        // Register animations that reference joints in this skeleton
        for (const auto& joint : skin["joints"]) {
            int jointIndex = joint.get<int>();
            for (const auto& animation : animations) {
                if (animation->nodesMap.find(nodes[jointIndex]->localID) != animation->nodesMap.end() && skeleton->animationsByName.find(animation->name) == skeleton->animationsByName.end()) {
                    skeleton->AddAnimation(animation);
                }
            }
        }

        skins.push_back(skeleton);
    }
}

void SetSkins(std::vector<std::shared_ptr<SceneNode>>& nodes, std::vector<std::shared_ptr<Skeleton>> skins) {
    for (auto& node : nodes) {
        auto renderableNode = std::dynamic_pointer_cast<RenderableObject>(node);
        if (renderableNode && renderableNode->m_fileLocalSkinIndex != -1) {
            renderableNode->SetSkin(skins[renderableNode->m_fileLocalSkinIndex]);
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
        auto materials = parseGLTFMaterials(gltfData, "", false, binaryData);
        std::vector<MeshData> meshes;
        parseMeshes(gltfData, binaryData, materials, &meshes);
        std::vector<std::shared_ptr<SceneNode>> nodes;
        parseGLTFNodeHierarchy(scene, gltfData, meshes, &nodes);
        std::vector<std::shared_ptr<Animation>> animations = parseGLTFAnimations(gltfData, binaryData, nodes);
        std::vector<std::shared_ptr<Skeleton>> skins = parseGLTFSkins(gltfData, binaryData, nodes, animations);

        for (std::shared_ptr<Skeleton> skin : skins) {
            scene->AddSkeleton(skin);
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return nullptr;
    }
    return scene;
}