#include "Import/GlTFLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Materials/Material.h"
#include "Import/CLodCacheLoader.h"
#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Mesh/Mesh.h"
#include "Mesh/VertexFlags.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Utilities/Utilities.h"

using nlohmann::json;
using namespace DirectX;

namespace {

struct GLBHeader {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t length = 0;
};

struct GLBChunkSpan {
    uint32_t type = 0;
    uint32_t length = 0;
    uint64_t dataOffset = 0;
};

struct BufferViewInfo {
    size_t bufferIndex = 0;
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t byteStride = 0;
};

struct AccessorInfo {
    size_t bufferViewIndex = 0;
    size_t byteOffset = 0;
    size_t count = 0;
    int componentType = 0;
    std::string type;
};

struct PrimitiveData {
    std::shared_ptr<Mesh> mesh;
};

enum class BufferBacking {
    FileSpan,
    DataUri,
};

struct BufferSource {
    BufferBacking backing = BufferBacking::FileSpan;
    std::filesystem::path filePath;
    uint64_t fileOffset = 0;
    uint64_t fileLength = 0;
    std::string dataUri;
};

struct ParsedDocument {
    json gltf;
    std::vector<BufferSource> buffers;
};

constexpr uint32_t kGlbMagic = 0x46546C67;
constexpr uint32_t kJsonChunkType = 0x4E4F534A;
constexpr uint32_t kBinChunkType = 0x004E4942;
constexpr int kTrianglesMode = 4;

uint64_t GetFileSize(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to query file size: " + path.string());
    }
    return static_cast<uint64_t>(size);
}

std::vector<uint8_t> ReadFileRange(const std::filesystem::path& path, uint64_t offset, uint64_t size) {
    std::vector<uint8_t> out;
    TaskSchedulerManager::GetInstance().RunIoTask([&]() {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open file: " + path.string());
        }

        const uint64_t fileSize = GetFileSize(path);
        if (offset > fileSize || size > fileSize - offset) {
            throw std::runtime_error("Read range out of file bounds: " + path.string());
        }

        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        out.resize(static_cast<size_t>(size));
        if (size > 0) {
            file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
            if (!file) {
                throw std::runtime_error("Failed to read file range: " + path.string());
            }
        }
        });

    return out;
}

std::mutex& GetCacheSaveMutexForKey(const std::string& key) {
    static std::mutex cacheMutexTableGuard;
    static std::unordered_map<std::string, std::unique_ptr<std::mutex>> cacheMutexTable;

    std::lock_guard<std::mutex> lock(cacheMutexTableGuard);
    auto& mutexPtr = cacheMutexTable[key];
    if (!mutexPtr) {
        mutexPtr = std::make_unique<std::mutex>();
    }
    return *mutexPtr;
}

std::string BuildCacheSaveKey(const CLodCacheLoader::MeshCacheIdentity& cacheIdentity) {
    return cacheIdentity.sourceIdentifier + "|" + cacheIdentity.primPath + "|" + cacheIdentity.subsetName;
}

uint32_t ReadU32LE(const std::filesystem::path& path, uint64_t offset) {
    auto bytes = ReadFileRange(path, offset, sizeof(uint32_t));
    uint32_t value = 0;
    std::memcpy(&value, bytes.data(), sizeof(uint32_t));
    return value;
}

GLBHeader ReadGLBHeader(const std::filesystem::path& path) {
    auto bytes = ReadFileRange(path, 0, 12);
    GLBHeader header;
    std::memcpy(&header.magic, bytes.data(), sizeof(uint32_t));
    std::memcpy(&header.version, bytes.data() + 4, sizeof(uint32_t));
    std::memcpy(&header.length, bytes.data() + 8, sizeof(uint32_t));

    if (header.magic != kGlbMagic) {
        throw std::runtime_error("Invalid GLB magic for: " + path.string());
    }
    if (header.version != 2) {
        throw std::runtime_error("Unsupported GLB version for: " + path.string());
    }

    return header;
}

std::vector<GLBChunkSpan> ReadGLBChunkSpans(const std::filesystem::path& path, uint64_t fileSize) {
    std::vector<GLBChunkSpan> chunks;
    uint64_t offset = 12;

    while (offset < fileSize) {
        if (offset + 8 > fileSize) {
            throw std::runtime_error("Invalid GLB chunk header range");
        }

        const uint32_t chunkLength = ReadU32LE(path, offset);
        const uint32_t chunkType = ReadU32LE(path, offset + 4);
        offset += 8;

        if (offset + chunkLength > fileSize) {
            throw std::runtime_error("Invalid GLB chunk length");
        }

        GLBChunkSpan chunk;
        chunk.type = chunkType;
        chunk.length = chunkLength;
        chunk.dataOffset = offset;
        chunks.push_back(chunk);

        offset += chunkLength;
    }

    return chunks;
}

int Base64CharToValue(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
}

std::vector<uint8_t> DecodeBase64(const std::string& encoded) {
    std::vector<uint8_t> decoded;
    decoded.reserve((encoded.size() * 3) / 4);

    int val = 0;
    int valb = -8;

    for (unsigned char c : encoded) {
        if (std::isspace(c) != 0) {
            continue;
        }

        int d = Base64CharToValue(c);
        if (d == -1) {
            throw std::runtime_error("Invalid base64 data");
        }
        if (d == -2) {
            break;
        }

        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return decoded;
}

std::vector<uint8_t> DecodeDataUri(const std::string& uri) {
    const auto commaPos = uri.find(',');
    if (commaPos == std::string::npos) {
        throw std::runtime_error("Invalid data URI");
    }

    const std::string metadata = uri.substr(0, commaPos);
    const std::string payload = uri.substr(commaPos + 1);

    if (metadata.find(";base64") == std::string::npos) {
        throw std::runtime_error("Only base64 data URIs are supported");
    }

    return DecodeBase64(payload);
}

size_t NumComponentsForType(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    throw std::runtime_error("Unsupported accessor type: " + type);
}

size_t BytesPerComponent(int componentType) {
    switch (componentType) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        throw std::runtime_error("Unsupported component type");
    }
}

AccessorInfo GetAccessorInfo(const json& gltf, size_t accessorIndex) {
    const auto& accessors = gltf.at("accessors");
    if (accessorIndex >= accessors.size()) {
        throw std::runtime_error("Accessor index out of range");
    }

    const auto& accessor = accessors[accessorIndex];
    if (!accessor.contains("bufferView")) {
        throw std::runtime_error("Sparse accessors are not yet supported");
    }

    AccessorInfo info;
    info.bufferViewIndex = accessor.at("bufferView").get<size_t>();
    info.byteOffset = accessor.value<size_t>("byteOffset", static_cast<size_t>(0));
    info.count = accessor.at("count").get<size_t>();
    info.componentType = accessor.at("componentType").get<int>();
    info.type = accessor.at("type").get<std::string>();
    return info;
}

BufferViewInfo GetBufferViewInfo(const json& gltf, size_t bufferViewIndex) {
    const auto& bufferViews = gltf.at("bufferViews");
    if (bufferViewIndex >= bufferViews.size()) {
        throw std::runtime_error("BufferView index out of range");
    }

    const auto& bufferView = bufferViews[bufferViewIndex];
    BufferViewInfo info;
    info.bufferIndex = bufferView.at("buffer").get<size_t>();
    info.byteOffset = bufferView.value<size_t>("byteOffset", static_cast<size_t>(0));
    info.byteLength = bufferView.at("byteLength").get<size_t>();
    info.byteStride = bufferView.value<size_t>("byteStride", static_cast<size_t>(0));
    return info;
}

template <typename T>
T ReadTyped(const std::vector<uint8_t>& buffer, size_t byteOffset) {
    if (byteOffset + sizeof(T) > buffer.size()) {
        throw std::runtime_error("Buffer read out of bounds");
    }

    T value{};
    std::memcpy(&value, buffer.data() + byteOffset, sizeof(T));
    return value;
}

double ReadComponentAsDouble(const std::vector<uint8_t>& source, int componentType, size_t byteOffset) {
    switch (componentType) {
    case 5120: return static_cast<double>(ReadTyped<int8_t>(source, byteOffset));
    case 5121: return static_cast<double>(ReadTyped<uint8_t>(source, byteOffset));
    case 5122: return static_cast<double>(ReadTyped<int16_t>(source, byteOffset));
    case 5123: return static_cast<double>(ReadTyped<uint16_t>(source, byteOffset));
    case 5125: return static_cast<double>(ReadTyped<uint32_t>(source, byteOffset));
    case 5126: return static_cast<double>(ReadTyped<float>(source, byteOffset));
    default:
        throw std::runtime_error("Unsupported accessor component type");
    }
}

std::vector<uint8_t> ReadFromSource(const BufferSource& source, uint64_t offset, uint64_t size) {
    if (source.backing == BufferBacking::DataUri) {
        const auto bytes = DecodeDataUri(source.dataUri);
        if (offset > bytes.size() || size > bytes.size() - offset) {
            throw std::runtime_error("Data URI range out of bounds");
        }

        std::vector<uint8_t> slice(static_cast<size_t>(size));
        if (size > 0) {
            std::memcpy(slice.data(), bytes.data() + offset, static_cast<size_t>(size));
        }
        return slice;
    }

    if (offset > source.fileLength || size > source.fileLength - offset) {
        throw std::runtime_error("Buffer source range out of bounds: " + source.filePath.string());
    }

    return ReadFileRange(source.filePath, source.fileOffset + offset, size);
}

std::vector<uint8_t> ReadAccessorRawWindow(const ParsedDocument& doc, size_t accessorIndex, size_t firstElement, size_t elementCount, size_t* outStride, size_t* outComponentCount, int* outComponentType) {
    const AccessorInfo accessor = GetAccessorInfo(doc.gltf, accessorIndex);
    const BufferViewInfo view = GetBufferViewInfo(doc.gltf, accessor.bufferViewIndex);

    if (view.bufferIndex >= doc.buffers.size()) {
        throw std::runtime_error("Buffer index out of range");
    }

    const size_t componentCount = NumComponentsForType(accessor.type);
    const size_t componentBytes = BytesPerComponent(accessor.componentType);
    const size_t packedElementSize = componentCount * componentBytes;
    const size_t stride = view.byteStride == 0 ? packedElementSize : view.byteStride;

    if (elementCount == 0) {
        *outStride = stride;
        *outComponentCount = componentCount;
        *outComponentType = accessor.componentType;
        return {};
    }

    if (firstElement > accessor.count || elementCount > accessor.count - firstElement) {
        throw std::runtime_error("Accessor window out of bounds");
    }

    const uint64_t relativeStart = static_cast<uint64_t>(view.byteOffset + accessor.byteOffset + firstElement * stride);
    const uint64_t relativeEnd = relativeStart + static_cast<uint64_t>((elementCount - 1) * stride + packedElementSize);
    const uint64_t viewEnd = static_cast<uint64_t>(view.byteOffset + view.byteLength);

    if (relativeEnd > viewEnd) {
        throw std::runtime_error("Accessor range exceeds bufferView bounds");
    }

    *outStride = stride;
    *outComponentCount = componentCount;
    *outComponentType = accessor.componentType;

    return ReadFromSource(doc.buffers[view.bufferIndex], relativeStart, relativeEnd - relativeStart);
}

std::vector<uint32_t> GenerateSequentialIndices(size_t vertexCount) {
    std::vector<uint32_t> indices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        indices[i] = static_cast<uint32_t>(i);
    }
    return indices;
}

ParsedDocument ParseDocument(const std::string& filePath) {
    const std::filesystem::path path(filePath);
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("glTF file not found: " + filePath);
    }

    ParsedDocument doc;
    const std::string extension = path.extension().string();

    std::optional<GLBChunkSpan> glbJsonChunk;
    std::optional<GLBChunkSpan> glbBinChunk;

    if (extension == ".glb" || extension == ".GLB") {
        const uint64_t fileSize = GetFileSize(path);
        const GLBHeader header = ReadGLBHeader(path);
        if (header.length != fileSize) {
            spdlog::warn("GLB length header ({}) differs from file size ({}) for {}",
                header.length, fileSize, filePath);
        }

        const auto chunks = ReadGLBChunkSpans(path, fileSize);
        for (const auto& chunk : chunks) {
            if (chunk.type == kJsonChunkType && !glbJsonChunk.has_value()) {
                glbJsonChunk = chunk;
            }
            else if (chunk.type == kBinChunkType && !glbBinChunk.has_value()) {
                glbBinChunk = chunk;
            }
        }

        if (!glbJsonChunk.has_value()) {
            throw std::runtime_error("GLB JSON chunk not found");
        }

        auto jsonBytes = ReadFileRange(path, glbJsonChunk->dataOffset, glbJsonChunk->length);
        doc.gltf = json::parse(jsonBytes.begin(), jsonBytes.end());
    }
    else {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error("Failed to open glTF JSON: " + filePath);
        }
        stream >> doc.gltf;
    }

    if (!doc.gltf.contains("buffers") || doc.gltf["buffers"].empty()) {
        throw std::runtime_error("glTF contains no buffers");
    }

    const std::filesystem::path parent = path.parent_path();

    for (size_t bufferIndex = 0; bufferIndex < doc.gltf["buffers"].size(); ++bufferIndex) {
        const auto& buffer = doc.gltf["buffers"][bufferIndex];
        BufferSource source;

        if (buffer.contains("uri")) {
            const std::string uri = buffer["uri"].get<std::string>();
            if (uri.rfind("data:", 0) == 0) {
                source.backing = BufferBacking::DataUri;
                source.dataUri = uri;
                source.fileLength = buffer.value<uint64_t>("byteLength", static_cast<uint64_t>(0));
            }
            else {
                source.backing = BufferBacking::FileSpan;
                source.filePath = parent / std::filesystem::path(uri);
                source.fileOffset = 0;
                const uint64_t actualLength = GetFileSize(source.filePath);
                const uint64_t declaredLength = buffer.value<uint64_t>("byteLength", actualLength);
                if (declaredLength != actualLength) {
                    spdlog::warn(
                        "glTF buffer byteLength mismatch for {} (declared={}, actual={}); using actual size",
                        source.filePath.string(), declaredLength, actualLength);
                }
                source.fileLength = actualLength;
            }
        }
        else {
            if (!(extension == ".glb" || extension == ".GLB")) {
                throw std::runtime_error("Non-GLB buffer missing URI");
            }
            if (!glbBinChunk.has_value()) {
                throw std::runtime_error("GLB binary chunk not found");
            }

            source.backing = BufferBacking::FileSpan;
            source.filePath = path;
            source.fileOffset = glbBinChunk->dataOffset;
            const uint64_t actualLength = static_cast<uint64_t>(glbBinChunk->length);
            const uint64_t declaredLength = buffer.value<uint64_t>("byteLength", actualLength);
            if (declaredLength != actualLength) {
                spdlog::warn(
                    "GLB buffer byteLength mismatch in {} (declared={}, binChunk={}); using bin chunk size",
                    filePath, declaredLength, actualLength);
            }
            source.fileLength = actualLength;
        }

        doc.buffers.push_back(std::move(source));
    }

    return doc;
}

std::shared_ptr<Mesh> BuildMeshFromPrimitive(
    const ParsedDocument& doc,
    const json& primitive,
    const std::shared_ptr<Material>& defaultMaterial,
    const std::string& sourceFilePath,
    size_t meshIndex,
    size_t primitiveIndex)
{
    const int primitiveMode = primitive.value("mode", kTrianglesMode);
    if (primitiveMode != kTrianglesMode) {
        throw std::runtime_error("Only TRIANGLES primitive mode is supported in v1");
    }

    if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) {
        throw std::runtime_error("Primitive missing POSITION accessor");
    }

    const auto& attributes = primitive["attributes"];

    const size_t positionAccessorIndex = attributes["POSITION"].get<size_t>();
    const AccessorInfo positionAccessor = GetAccessorInfo(doc.gltf, positionAccessorIndex);
    if (NumComponentsForType(positionAccessor.type) != 3) {
        throw std::runtime_error("POSITION accessor must be VEC3");
    }

    const size_t vertexCount = positionAccessor.count;

    bool hasNormals = false;
    size_t normalAccessorIndex = 0;
    AccessorInfo normalAccessor;
    if (attributes.contains("NORMAL")) {
        normalAccessorIndex = attributes["NORMAL"].get<size_t>();
        normalAccessor = GetAccessorInfo(doc.gltf, normalAccessorIndex);
        if (normalAccessor.count != vertexCount || NumComponentsForType(normalAccessor.type) != 3) {
            throw std::runtime_error("NORMAL accessor size/type mismatch");
        }
        hasNormals = true;
    }

    bool hasTexcoords = false;
    size_t texcoordAccessorIndex = 0;
    AccessorInfo texcoordAccessor;
    if (attributes.contains("TEXCOORD_0")) {
        texcoordAccessorIndex = attributes["TEXCOORD_0"].get<size_t>();
        texcoordAccessor = GetAccessorInfo(doc.gltf, texcoordAccessorIndex);
        if (texcoordAccessor.count != vertexCount || NumComponentsForType(texcoordAccessor.type) != 2) {
            throw std::runtime_error("TEXCOORD_0 accessor size/type mismatch");
        }
        hasTexcoords = true;
    }

    unsigned int meshFlags = 0;
    if (hasNormals) {
        meshFlags |= VertexFlags::VERTEX_NORMALS;
    }
    if (hasTexcoords) {
        meshFlags |= VertexFlags::VERTEX_TEXCOORDS;
    }

    const uint8_t vertexSize = static_cast<uint8_t>(sizeof(XMFLOAT3) + sizeof(XMFLOAT3) + (hasTexcoords ? sizeof(XMFLOAT2) : 0));
    MeshIngestBuilder ingest(vertexSize, 0, meshFlags);
    ingest.ReserveVertices(vertexCount);

    constexpr size_t kVertexChunkSize = 8192;
    for (size_t firstVertex = 0; firstVertex < vertexCount; firstVertex += kVertexChunkSize) {
        const size_t chunkVertexCount = std::min(kVertexChunkSize, vertexCount - firstVertex);

        size_t positionStride = 0;
        size_t positionComponents = 0;
        int positionComponentType = 0;
        const auto positionBytes = ReadAccessorRawWindow(doc, positionAccessorIndex, firstVertex, chunkVertexCount, &positionStride, &positionComponents, &positionComponentType);
        const size_t positionComponentBytes = BytesPerComponent(positionComponentType);

        size_t normalStride = 0;
        size_t normalComponents = 0;
        int normalComponentType = 0;
        std::vector<uint8_t> normalBytes;
        size_t normalComponentBytes = 0;
        if (hasNormals) {
            normalBytes = ReadAccessorRawWindow(doc, normalAccessorIndex, firstVertex, chunkVertexCount, &normalStride, &normalComponents, &normalComponentType);
            normalComponentBytes = BytesPerComponent(normalComponentType);
        }

        size_t texcoordStride = 0;
        size_t texcoordComponents = 0;
        int texcoordComponentType = 0;
        std::vector<uint8_t> texcoordBytes;
        size_t texcoordComponentBytes = 0;
        if (hasTexcoords) {
            texcoordBytes = ReadAccessorRawWindow(doc, texcoordAccessorIndex, firstVertex, chunkVertexCount, &texcoordStride, &texcoordComponents, &texcoordComponentType);
            texcoordComponentBytes = BytesPerComponent(texcoordComponentType);
        }

        for (size_t i = 0; i < chunkVertexCount; ++i) {
            const size_t positionBase = i * positionStride;
            const XMFLOAT3 pos(
                static_cast<float>(ReadComponentAsDouble(positionBytes, positionComponentType, positionBase + positionComponentBytes * 0)),
                static_cast<float>(ReadComponentAsDouble(positionBytes, positionComponentType, positionBase + positionComponentBytes * 1)),
                static_cast<float>(ReadComponentAsDouble(positionBytes, positionComponentType, positionBase + positionComponentBytes * 2)));

            XMFLOAT3 normal(0.0f, 0.0f, 0.0f);
            if (hasNormals) {
                const size_t normalBase = i * normalStride;
                normal = XMFLOAT3(
                    static_cast<float>(ReadComponentAsDouble(normalBytes, normalComponentType, normalBase + normalComponentBytes * 0)),
                    static_cast<float>(ReadComponentAsDouble(normalBytes, normalComponentType, normalBase + normalComponentBytes * 1)),
                    static_cast<float>(ReadComponentAsDouble(normalBytes, normalComponentType, normalBase + normalComponentBytes * 2))
                );
            }

            std::array<std::byte, sizeof(XMFLOAT3) + sizeof(XMFLOAT3) + sizeof(XMFLOAT2)> packedVertex{};
            std::memcpy(packedVertex.data(), &pos, sizeof(XMFLOAT3));
            size_t offset = sizeof(XMFLOAT3);
            std::memcpy(packedVertex.data() + offset, &normal, sizeof(XMFLOAT3));
            offset += sizeof(XMFLOAT3);

            if (hasTexcoords) {
                const size_t texcoordBase = i * texcoordStride;
                const XMFLOAT2 uv(
                    static_cast<float>(ReadComponentAsDouble(texcoordBytes, texcoordComponentType, texcoordBase + texcoordComponentBytes * 0)),
                    static_cast<float>(ReadComponentAsDouble(texcoordBytes, texcoordComponentType, texcoordBase + texcoordComponentBytes * 1))
                );
                std::memcpy(packedVertex.data() + offset, &uv, sizeof(XMFLOAT2));
            }

            ingest.AppendVertexBytes(packedVertex.data(), vertexSize);
        }
    }

    if (primitive.contains("indices")) {
        const size_t accessorIndex = primitive["indices"].get<size_t>();
        const AccessorInfo indexAccessor = GetAccessorInfo(doc.gltf, accessorIndex);
        if (NumComponentsForType(indexAccessor.type) != 1) {
            throw std::runtime_error("Index accessor must be SCALAR");
        }

        ingest.ReserveIndices(indexAccessor.count);

        constexpr size_t kIndexChunkSize = 32768;
        for (size_t firstIndex = 0; firstIndex < indexAccessor.count; firstIndex += kIndexChunkSize) {
            const size_t chunkIndexCount = std::min(kIndexChunkSize, indexAccessor.count - firstIndex);
            size_t indexStride = 0;
            size_t indexComponents = 0;
            int indexComponentType = 0;
            const auto indexBytes = ReadAccessorRawWindow(doc, accessorIndex, firstIndex, chunkIndexCount, &indexStride, &indexComponents, &indexComponentType);
            if (indexComponents != 1) {
                throw std::runtime_error("Index accessor component mismatch");
            }

            for (size_t i = 0; i < chunkIndexCount; ++i) {
                const size_t indexBase = i * indexStride;
                ingest.AppendIndex(static_cast<uint32_t>(ReadComponentAsDouble(indexBytes, indexComponentType, indexBase)));
            }
        }
    }
    else {
        ingest.ReserveIndices(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            ingest.AppendIndex(static_cast<uint32_t>(i));
        }
    }

    CLodCacheLoader::MeshCacheIdentity cacheIdentity{};
    cacheIdentity.sourceIdentifier = sourceFilePath;
    cacheIdentity.primPath = "/glTF/Mesh/" + std::to_string(meshIndex) + "/Primitive/" + std::to_string(primitiveIndex);
    cacheIdentity.subsetName = "";

    auto prebuiltData = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
    const bool hadPrebuiltData = prebuiltData.has_value();

    auto mesh = ingest.Build(defaultMaterial, std::move(prebuiltData), MeshCpuDataPolicy::ReleaseAfterUpload);

    if (!hadPrebuiltData) {
        const std::string cacheSaveKey = BuildCacheSaveKey(cacheIdentity);
        std::lock_guard<std::mutex> cacheSaveGuard(GetCacheSaveMutexForKey(cacheSaveKey));

        auto prebuiltSavedByOtherWorker = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
        if (prebuiltSavedByOtherWorker.has_value()) {
            mesh->AdoptCLodDiskStreamingMetadata(prebuiltSavedByOtherWorker.value());
        }
        else {
            const bool cacheSaved = CLodCacheLoader::SavePrebuilt(cacheIdentity, mesh->GetClusterLODPrebuiltData(), mesh->GetClusterLODCacheBuildPayload());
            if (!cacheSaved) {
                spdlog::warn("Failed to save CLOD cache for {} (mesh {}, primitive {})", sourceFilePath, meshIndex, primitiveIndex);
            }
            else {
                auto diskBackedPrebuilt = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
                if (diskBackedPrebuilt.has_value()) {
                    mesh->AdoptCLodDiskStreamingMetadata(diskBackedPrebuilt.value());
                }
            }
        }
    }

    return mesh;
}

std::vector<std::vector<PrimitiveData>> BuildMeshes(
    const ParsedDocument& doc,
    const std::shared_ptr<Material>& defaultMaterial,
    const std::string& sourceFilePath)
{
    std::vector<std::vector<PrimitiveData>> allMeshes;

    if (!doc.gltf.contains("meshes")) {
        return allMeshes;
    }

    struct PrimitiveWorkItem {
        size_t meshIndex = 0;
        size_t primitiveIndex = 0;
        const json* primitive = nullptr;
    };

    const auto& meshArray = doc.gltf["meshes"];
    allMeshes.resize(meshArray.size());

    std::vector<PrimitiveWorkItem> workItems;
    workItems.reserve(meshArray.size());

    for (size_t meshIndex = 0; meshIndex < meshArray.size(); ++meshIndex) {
        const auto& mesh = meshArray[meshIndex];
        if (!mesh.contains("primitives")) {
            continue;
        }

        const auto& primitiveArray = mesh["primitives"];
        allMeshes[meshIndex].resize(primitiveArray.size());
        workItems.reserve(workItems.size() + primitiveArray.size());

        for (size_t primitiveIndex = 0; primitiveIndex < primitiveArray.size(); ++primitiveIndex) {
            workItems.push_back(PrimitiveWorkItem{
                .meshIndex = meshIndex,
                .primitiveIndex = primitiveIndex,
                .primitive = &primitiveArray[primitiveIndex]
                });
        }
    }

    TaskSchedulerManager::GetInstance().ParallelFor(workItems.size(), [&](size_t workIndex) {
        const PrimitiveWorkItem& workItem = workItems[workIndex];
        allMeshes[workItem.meshIndex][workItem.primitiveIndex].mesh = BuildMeshFromPrimitive(
            doc,
            *workItem.primitive,
            defaultMaterial,
            sourceFilePath,
            workItem.meshIndex,
            workItem.primitiveIndex);
        });

    return allMeshes;
}

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

        matrix = XMMatrixTranspose(matrix);
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

    flecs::entity sceneRoot = scene->GetRoot();
    for (size_t nodeIndex = 0; nodeIndex < entities.size(); ++nodeIndex) {
        if (!hasParent[nodeIndex]) {
            entities[nodeIndex].child_of(sceneRoot);
        }
    }
}

} // namespace

namespace GlTFLoader {

std::shared_ptr<Scene> LoadModel(std::string filePath) {
    try {
        ParsedDocument doc = ParseDocument(filePath);

        auto scene = std::make_shared<Scene>();
        auto defaultMaterial = Material::GetDefaultMaterial();
        auto meshes = BuildMeshes(doc, defaultMaterial, filePath);
        BuildNodeHierarchy(scene, doc.gltf, meshes);

        if (doc.gltf.contains("animations")) {
            spdlog::warn("glTF animations are not enabled yet in custom loader: {}", filePath);
        }
        if (doc.gltf.contains("skins")) {
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
