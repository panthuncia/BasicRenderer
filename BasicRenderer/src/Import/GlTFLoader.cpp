#include "Import/GlTFLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Import/GlTFGeometryExtractor.h"
#include "Materials/Material.h"
#include "Mesh/Mesh.h"
#include "Resources/Sampler.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Utilities/Utilities.h"

using nlohmann::json;
using namespace DirectX;

namespace {

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
    std::vector<uint8_t> inlineBytes;
};

struct GlbBinaryChunkSpan {
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct GlTFMaterialCache {
    std::unordered_map<std::string, std::shared_ptr<TextureAsset>> textureCache;
    std::vector<std::shared_ptr<Material>> materialCache;
    std::vector<BufferSource> bufferSources;
    std::string sourceKey;
};

struct SharedTextureCacheEntry {
    std::weak_ptr<TextureAsset> texture;
};

struct SharedMaterialCacheEntry {
    std::weak_ptr<Material> material;
};

std::mutex g_gltfMaterialCacheMutex;
std::unordered_map<std::string, SharedTextureCacheEntry> g_sharedTextureCache;
std::unordered_map<std::string, SharedMaterialCacheEntry> g_sharedMaterialCache;

constexpr uint32_t kGlbMagic = 0x46546C67;
constexpr uint32_t kGlbJsonChunkType = 0x4E4F534A;
constexpr uint32_t kGlbBinChunkType = 0x004E4942;
constexpr uint32_t kMaterialTextureMaxAnisotropy = 16;

std::string NormalizeSourceKey(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        normalized = path.lexically_normal();
    }

    auto key = normalized.generic_string();
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
        });
    return key;
}

std::string MakeMaterialCacheKey(const std::string& sourceKey, size_t materialIndex) {
    return sourceKey + "#mat:" + std::to_string(materialIndex);
}

std::string BuildGlTFSamplerSignature(const json& gltf, const json& textureNode) {
    int wrapS = 10497;
    int wrapT = 10497;
    int minFilter = 9987;
    int magFilter = 9729;

    if (textureNode.contains("sampler")) {
        const auto& samplers = gltf.contains("samplers") ? gltf["samplers"] : json::array();
        const size_t samplerIndex = textureNode["sampler"].get<size_t>();
        if (samplerIndex >= samplers.size()) {
            throw std::runtime_error("glTF sampler index out of range");
        }

        const auto& samplerNode = samplers[samplerIndex];
        wrapS = samplerNode.value<int>("wrapS", wrapS);
        wrapT = samplerNode.value<int>("wrapT", wrapT);
        minFilter = samplerNode.value<int>("minFilter", minFilter);
        magFilter = samplerNode.value<int>("magFilter", magFilter);
    }

    return "sampler:" +
        std::to_string(wrapS) + ":" +
        std::to_string(wrapT) + ":" +
        std::to_string(minFilter) + ":" +
        std::to_string(magFilter);
}

std::string BuildGlTFImageIdentity(
    const json& gltf,
    const std::filesystem::path& sourcePath,
    const std::string& sourceKey,
    size_t imageIndex)
{
    const auto& images = gltf.at("images");
    if (imageIndex >= images.size()) {
        throw std::runtime_error("glTF image index out of range");
    }

    const auto& image = images[imageIndex];
    if (image.contains("uri")) {
        const std::string uri = image["uri"].get<std::string>();
        if (uri.rfind("data:", 0) == 0) {
            return sourceKey + "#image-data-uri:" + std::to_string(imageIndex);
        }

        return NormalizeSourceKey(sourcePath.parent_path() / std::filesystem::path(uri));
    }

    if (image.contains("bufferView")) {
        const size_t bufferViewIndex = image["bufferView"].get<size_t>();
        const std::string mimeType = image.value("mimeType", std::string());
        return sourceKey + "#image-bv:" + std::to_string(bufferViewIndex) + ":" + mimeType;
    }

    return sourceKey + "#image:" + std::to_string(imageIndex);
}

size_t ResolveTextureImageIndex(const json& textureNode) {
    if (textureNode.contains("source")) {
        return textureNode["source"].get<size_t>();
    }
    if (textureNode.contains("extensions") && textureNode["extensions"].contains("KHR_texture_basisu")) {
        return textureNode["extensions"]["KHR_texture_basisu"].at("source").get<size_t>();
    }

    throw std::runtime_error("glTF texture has no source image");
}

std::string BuildTextureResourceKey(
    const json& gltf,
    const std::filesystem::path& sourcePath,
    const GlTFMaterialCache& cache,
    size_t textureIndex,
    bool preferSRGB)
{
    const auto& textures = gltf.at("textures");
    if (textureIndex >= textures.size()) {
        throw std::runtime_error("glTF texture index out of range");
    }

    const auto& textureNode = textures[textureIndex];
    const size_t imageIndex = ResolveTextureImageIndex(textureNode);
    const std::string imageIdentity = BuildGlTFImageIdentity(gltf, sourcePath, cache.sourceKey, imageIndex);
    const std::string samplerSignature = BuildGlTFSamplerSignature(gltf, textureNode);
    return imageIdentity + "|" + samplerSignature + (preferSRGB ? "|srgb" : "|linear");
}

uint64_t GetFileSize(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to query file size: " + path.string());
    }
    return static_cast<uint64_t>(size);
}

std::vector<uint8_t> ReadFileRange(const std::filesystem::path& path, uint64_t offset, uint64_t size) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    const uint64_t fileSize = GetFileSize(path);
    if (offset > fileSize || size > fileSize - offset) {
        throw std::runtime_error("Read range out of file bounds: " + path.string());
    }

    std::vector<uint8_t> out(static_cast<size_t>(size));
    if (size == 0) {
        return out;
    }

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    if (!file) {
        throw std::runtime_error("Failed to read file range: " + path.string());
    }

    return out;
}

uint32_t ReadU32LE(const std::filesystem::path& path, uint64_t offset) {
    const auto bytes = ReadFileRange(path, offset, sizeof(uint32_t));
    uint32_t value = 0;
    std::memcpy(&value, bytes.data(), sizeof(uint32_t));
    return value;
}

std::optional<GlbBinaryChunkSpan> GetGlbBinaryChunkSpan(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || path.extension() != ".glb") {
        return std::nullopt;
    }

    const uint64_t fileSize = GetFileSize(path);
    if (fileSize < 12) {
        throw std::runtime_error("GLB file too small: " + path.string());
    }

    const uint32_t magic = ReadU32LE(path, 0);
    if (magic != kGlbMagic) {
        throw std::runtime_error("Invalid GLB magic: " + path.string());
    }

    uint64_t offset = 12;
    while (offset + 8 <= fileSize) {
        const uint32_t chunkLength = ReadU32LE(path, offset);
        const uint32_t chunkType = ReadU32LE(path, offset + 4);
        offset += 8;

        if (offset + chunkLength > fileSize) {
            throw std::runtime_error("Invalid GLB chunk length in: " + path.string());
        }

        if (chunkType == kGlbBinChunkType) {
            return GlbBinaryChunkSpan{ offset, chunkLength };
        }

        if (chunkType != kGlbJsonChunkType) {
            spdlog::debug("Skipping non-JSON/ BIN GLB chunk type {} in {}", chunkType, path.string());
        }

        offset += chunkLength;
    }

    return std::nullopt;
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

    int value = 0;
    int bitCount = -8;
    for (const unsigned char c : encoded) {
        if (std::isspace(c) != 0) {
            continue;
        }

        const int digit = Base64CharToValue(c);
        if (digit == -1) {
            throw std::runtime_error("Invalid base64 data in data URI");
        }
        if (digit == -2) {
            break;
        }

        value = (value << 6) + digit;
        bitCount += 6;
        if (bitCount >= 0) {
            decoded.push_back(static_cast<uint8_t>((value >> bitCount) & 0xFF));
            bitCount -= 8;
        }
    }

    return decoded;
}

std::vector<uint8_t> DecodeDataUri(const std::string& uri) {
    const size_t commaPos = uri.find(',');
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

std::vector<BufferSource> BuildBufferSources(const json& gltf, const std::filesystem::path& sourcePath) {
    std::vector<BufferSource> bufferSources;
    if (!gltf.contains("buffers") || !gltf["buffers"].is_array()) {
        return bufferSources;
    }

    const auto binaryChunk = GetGlbBinaryChunkSpan(sourcePath);
    const auto& buffers = gltf["buffers"];
    bufferSources.resize(buffers.size());

    bool usedBinaryChunk = false;
    for (size_t bufferIndex = 0; bufferIndex < buffers.size(); ++bufferIndex) {
        const auto& buffer = buffers[bufferIndex];
        BufferSource source;

        if (buffer.contains("uri")) {
            const std::string uri = buffer["uri"].get<std::string>();
            if (uri.rfind("data:", 0) == 0) {
                source.backing = BufferBacking::DataUri;
                source.inlineBytes = DecodeDataUri(uri);
                source.fileLength = source.inlineBytes.size();
            }
            else {
                const std::filesystem::path resolvedPath = sourcePath.parent_path() / std::filesystem::path(uri);
                source.backing = BufferBacking::FileSpan;
                source.filePath = resolvedPath;
                source.fileLength = GetFileSize(resolvedPath);
            }
        }
        else {
            if (!binaryChunk.has_value()) {
                throw std::runtime_error("glTF buffer has no URI and source is not a GLB: " + sourcePath.string());
            }
            if (usedBinaryChunk) {
                throw std::runtime_error("Multiple URI-less glTF buffers are not supported in: " + sourcePath.string());
            }

            source.backing = BufferBacking::FileSpan;
            source.filePath = sourcePath;
            source.fileOffset = binaryChunk->offset;
            source.fileLength = binaryChunk->length;
            usedBinaryChunk = true;
        }

        bufferSources[bufferIndex] = std::move(source);
    }

    return bufferSources;
}

std::vector<uint8_t> ReadBufferSlice(const BufferSource& source, uint64_t offset, uint64_t size) {
    if (source.backing == BufferBacking::DataUri) {
        if (offset > source.inlineBytes.size() || size > source.inlineBytes.size() - offset) {
            throw std::runtime_error("Data URI buffer slice out of bounds");
        }

        return std::vector<uint8_t>(
            source.inlineBytes.begin() + static_cast<std::ptrdiff_t>(offset),
            source.inlineBytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    }

    if (offset > source.fileLength || size > source.fileLength - offset) {
        throw std::runtime_error("Buffer slice out of bounds for: " + source.filePath.string());
    }

    return ReadFileRange(source.filePath, source.fileOffset + offset, size);
}

std::vector<uint8_t> ReadImageBytes(
    const json& gltf,
    const std::filesystem::path& sourcePath,
    const std::vector<BufferSource>& bufferSources,
    size_t imageIndex)
{
    const auto& images = gltf.at("images");
    if (imageIndex >= images.size()) {
        throw std::runtime_error("glTF image index out of range");
    }

    const auto& image = images[imageIndex];
    if (image.contains("uri")) {
        const std::string uri = image["uri"].get<std::string>();
        if (uri.rfind("data:", 0) == 0) {
            return DecodeDataUri(uri);
        }

        const auto imagePath = sourcePath.parent_path() / std::filesystem::path(uri);
        return ReadFileRange(imagePath, 0, GetFileSize(imagePath));
    }

    if (image.contains("bufferView")) {
        const auto& bufferViews = gltf.at("bufferViews");
        const size_t bufferViewIndex = image["bufferView"].get<size_t>();
        if (bufferViewIndex >= bufferViews.size()) {
            throw std::runtime_error("glTF image bufferView index out of range");
        }

        const auto& bufferView = bufferViews[bufferViewIndex];
        const size_t bufferIndex = bufferView.at("buffer").get<size_t>();
        if (bufferIndex >= bufferSources.size()) {
            throw std::runtime_error("glTF image buffer index out of range");
        }

        const uint64_t byteOffset = bufferView.value<uint64_t>("byteOffset", 0);
        const uint64_t byteLength = bufferView.at("byteLength").get<uint64_t>();
        return ReadBufferSlice(bufferSources[bufferIndex], byteOffset, byteLength);
    }

    throw std::runtime_error("glTF image has neither uri nor bufferView");
}

rhi::AddressMode ConvertWrapMode(int wrapMode) {
    switch (wrapMode) {
    case 33071:
        return rhi::AddressMode::Clamp;
    case 33648:
        return rhi::AddressMode::Mirror;
    case 10497:
    default:
        return rhi::AddressMode::Wrap;
    }
}

std::shared_ptr<Sampler> CreateTextureSampler(const json& gltf, const json& textureNode) {
    rhi::SamplerDesc samplerDesc = {};
    samplerDesc.addressU = rhi::AddressMode::Wrap;
    samplerDesc.addressV = rhi::AddressMode::Wrap;
    samplerDesc.addressW = rhi::AddressMode::Wrap;
    samplerDesc.mipLodBias = 0.0f;
    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = (std::numeric_limits<float>::max)();
    samplerDesc.maxAnisotropy = kMaterialTextureMaxAnisotropy;
    samplerDesc.compareEnable = false;
    samplerDesc.compareOp = rhi::CompareOp::Always;
    samplerDesc.reduction = rhi::ReductionMode::Standard;
    samplerDesc.borderPreset = rhi::BorderPreset::TransparentBlack;
    samplerDesc.minFilter = rhi::Filter::Linear;
    samplerDesc.magFilter = rhi::Filter::Linear;
    samplerDesc.mipFilter = rhi::MipFilter::Linear;

    if (!textureNode.contains("sampler")) {
        return Sampler::CreateSampler(samplerDesc);
    }

    const auto& samplers = gltf.contains("samplers") ? gltf["samplers"] : json::array();
    const size_t samplerIndex = textureNode["sampler"].get<size_t>();
    if (samplerIndex >= samplers.size()) {
        throw std::runtime_error("glTF sampler index out of range");
    }

    const auto& samplerNode = samplers[samplerIndex];
    samplerDesc.addressU = ConvertWrapMode(samplerNode.value<int>("wrapS", 10497));
    samplerDesc.addressV = ConvertWrapMode(samplerNode.value<int>("wrapT", 10497));

    const int minFilter = samplerNode.value<int>("minFilter", 9987);
    const int magFilter = samplerNode.value<int>("magFilter", 9729);

    samplerDesc.magFilter = (magFilter == 9728) ? rhi::Filter::Nearest : rhi::Filter::Linear;
    switch (minFilter) {
    case 9728:
        samplerDesc.minFilter = rhi::Filter::Nearest;
        samplerDesc.mipFilter = rhi::MipFilter::None;
        break;
    case 9729:
        samplerDesc.minFilter = rhi::Filter::Linear;
        samplerDesc.mipFilter = rhi::MipFilter::None;
        break;
    case 9984:
        samplerDesc.minFilter = rhi::Filter::Nearest;
        samplerDesc.mipFilter = rhi::MipFilter::Nearest;
        break;
    case 9985:
        samplerDesc.minFilter = rhi::Filter::Linear;
        samplerDesc.mipFilter = rhi::MipFilter::Nearest;
        break;
    case 9986:
        samplerDesc.minFilter = rhi::Filter::Nearest;
        samplerDesc.mipFilter = rhi::MipFilter::Linear;
        break;
    case 9987:
    default:
        samplerDesc.minFilter = rhi::Filter::Linear;
        samplerDesc.mipFilter = rhi::MipFilter::Linear;
        break;
    }

    return Sampler::CreateSampler(samplerDesc);
}

std::shared_ptr<TextureAsset> LoadTexture(
    const json& gltf,
    const std::filesystem::path& sourcePath,
    GlTFMaterialCache& cache,
    size_t textureIndex,
    bool preferSRGB)
{
    const std::string cacheKey = BuildTextureResourceKey(gltf, sourcePath, cache, textureIndex, preferSRGB);
    auto existingTexture = cache.textureCache.find(cacheKey);
    if (existingTexture != cache.textureCache.end()) {
        return existingTexture->second;
    }

    const std::string sharedCacheKey = cacheKey;
    {
        std::lock_guard<std::mutex> lock(g_gltfMaterialCacheMutex);
        auto sharedIt = g_sharedTextureCache.find(sharedCacheKey);
        if (sharedIt != g_sharedTextureCache.end()) {
            if (auto texture = sharedIt->second.texture.lock()) {
                cache.textureCache[cacheKey] = texture;
                return texture;
            }

            g_sharedTextureCache.erase(sharedIt);
        }
    }

    const auto& textures = gltf.at("textures");
    if (textureIndex >= textures.size()) {
        throw std::runtime_error("glTF texture index out of range");
    }

    const auto& textureNode = textures[textureIndex];
    const size_t imageIndex = ResolveTextureImageIndex(textureNode);

    auto textureBytes = ReadImageBytes(gltf, sourcePath, cache.bufferSources, imageIndex);
    auto sampler = CreateTextureSampler(gltf, textureNode);
    auto texture = LoadTextureFromMemory(textureBytes.data(), textureBytes.size(), sampler, {}, preferSRGB);
    texture->SetGenerateMipmaps(true);
    cache.textureCache[cacheKey] = texture;

    {
        std::lock_guard<std::mutex> lock(g_gltfMaterialCacheMutex);
        g_sharedTextureCache[sharedCacheKey] = SharedTextureCacheEntry{ texture };
    }

    return texture;
}

DirectX::XMFLOAT4 ReadFloat4(const json& values, const DirectX::XMFLOAT4& fallback) {
    if (!values.is_array() || values.size() != 4) {
        return fallback;
    }

    return DirectX::XMFLOAT4(
        values[0].get<float>(),
        values[1].get<float>(),
        values[2].get<float>(),
        values[3].get<float>());
}

DirectX::XMFLOAT3 ReadFloat3(const json& values, const DirectX::XMFLOAT3& fallback) {
    if (!values.is_array() || values.size() != 3) {
        return fallback;
    }

    return DirectX::XMFLOAT3(
        values[0].get<float>(),
        values[1].get<float>(),
        values[2].get<float>());
}

uint32_t ReadTextureUvSetIndex(const json& textureInfoNode) {
    return textureInfoNode.value<uint32_t>("texCoord", 0u);
}

std::shared_ptr<Material> LoadMaterial(
    const json& gltf,
    const std::filesystem::path& sourcePath,
    GlTFMaterialCache& cache,
    size_t materialIndex)
{
    if (materialIndex >= cache.materialCache.size()) {
        throw std::runtime_error("glTF material index out of range");
    }
    if (cache.materialCache[materialIndex] != nullptr) {
        return cache.materialCache[materialIndex];
    }

    const std::string sharedCacheKey = MakeMaterialCacheKey(cache.sourceKey, materialIndex);
    {
        std::lock_guard<std::mutex> lock(g_gltfMaterialCacheMutex);
        auto sharedIt = g_sharedMaterialCache.find(sharedCacheKey);
        if (sharedIt != g_sharedMaterialCache.end()) {
            if (auto material = sharedIt->second.material.lock()) {
                cache.materialCache[materialIndex] = material;
                return material;
            }

            g_sharedMaterialCache.erase(sharedIt);
        }
    }

    const auto& materialNode = gltf.at("materials").at(materialIndex);

    MaterialDescription desc;
    desc.name = materialNode.value("name", "glTF_Material_" + std::to_string(materialIndex));
    desc.baseColor = { nullptr, 1.0f, { 0, 1, 2, 3 } };
    desc.metallic = { nullptr, 0.0f, { 2 } };
    desc.roughness = { nullptr, 1.0f, { 1 } };
    desc.emissive = { nullptr, 1.0f, { 0, 1, 2 } };
    desc.opacity = { nullptr, 1.0f, { 3 } };
    desc.aoMap = { nullptr, 1.0f, { 0 } };
    desc.heightMap = { nullptr, 1.0f, { 0 } };
    desc.normal = { nullptr, 1.0f, { 0, 1, 2 } };
    desc.invertNormalGreen = true;

    if (materialNode.contains("pbrMetallicRoughness")) {
        const auto& pbr = materialNode["pbrMetallicRoughness"];
        if (pbr.contains("baseColorFactor")) {
            desc.diffuseColor = ReadFloat4(pbr["baseColorFactor"], desc.diffuseColor);
        }
        if (pbr.contains("metallicFactor")) {
            desc.metallic.factor = pbr["metallicFactor"].get<float>();
        }
        if (pbr.contains("roughnessFactor")) {
            desc.roughness.factor = pbr["roughnessFactor"].get<float>();
        }
        if (pbr.contains("baseColorTexture")) {
            const auto& textureInfo = pbr["baseColorTexture"];
            const size_t textureIndex = textureInfo.at("index").get<size_t>();
            desc.baseColor.texture = LoadTexture(gltf, sourcePath, cache, textureIndex, true);
            desc.baseColor.uvSetIndex = ReadTextureUvSetIndex(textureInfo);
        }
        if (pbr.contains("metallicRoughnessTexture")) {
            const auto& textureInfo = pbr["metallicRoughnessTexture"];
            const size_t textureIndex = textureInfo.at("index").get<size_t>();
            auto texture = LoadTexture(gltf, sourcePath, cache, textureIndex, false);
            desc.metallic.texture = texture;
            desc.roughness.texture = texture;
            const uint32_t uvSetIndex = ReadTextureUvSetIndex(textureInfo);
            desc.metallic.uvSetIndex = uvSetIndex;
            desc.roughness.uvSetIndex = uvSetIndex;
        }
    }

    if (materialNode.contains("normalTexture")) {
        const auto& textureInfo = materialNode["normalTexture"];
        const size_t textureIndex = textureInfo.at("index").get<size_t>();
        desc.normal.texture = LoadTexture(gltf, sourcePath, cache, textureIndex, false);
        desc.normal.uvSetIndex = ReadTextureUvSetIndex(textureInfo);
    }

    if (materialNode.contains("occlusionTexture")) {
        const auto& occlusion = materialNode["occlusionTexture"];
        const size_t textureIndex = occlusion.at("index").get<size_t>();
        desc.aoMap.texture = LoadTexture(gltf, sourcePath, cache, textureIndex, false);
        desc.aoMap.uvSetIndex = ReadTextureUvSetIndex(occlusion);
        if (occlusion.contains("strength")) {
            desc.aoMap.factor = occlusion["strength"].get<float>();
        }
    }

    if (materialNode.contains("emissiveFactor")) {
        const auto emissive = ReadFloat3(materialNode["emissiveFactor"], { 0.0f, 0.0f, 0.0f });
        desc.emissiveColor = { emissive.x, emissive.y, emissive.z, 1.0f };
    }
    if (materialNode.contains("emissiveTexture")) {
        const auto& textureInfo = materialNode["emissiveTexture"];
        const size_t textureIndex = textureInfo.at("index").get<size_t>();
        desc.emissive.texture = LoadTexture(gltf, sourcePath, cache, textureIndex, true);
        desc.emissive.uvSetIndex = ReadTextureUvSetIndex(textureInfo);
    }

    const std::string alphaMode = materialNode.value("alphaMode", std::string("OPAQUE"));
    if (alphaMode == "MASK" || alphaMode == "BLEND") {
        desc.blendState = BlendState::BLEND_STATE_MASK;
        desc.alphaCutoff = materialNode.value("alphaCutoff", 0.5f);
    }
    else if (alphaMode == "BLEND") {
        desc.blendState = BlendState::BLEND_STATE_BLEND;
        desc.opacity.factor = desc.diffuseColor.w;
        if (desc.baseColor.texture != nullptr) {
            desc.opacity.texture = desc.baseColor.texture;
            desc.opacity.uvSetIndex = desc.baseColor.uvSetIndex;
        }
    }
    else {
        desc.diffuseColor.w = 1.0f;
        desc.opacity.factor = 1.0f;
    }

    if (materialNode.value("doubleSided", false)) {
        spdlog::debug("glTF material '{}' is marked doubleSided; current material descriptor only applies this indirectly through technique heuristics.", desc.name);
    }

    cache.materialCache[materialIndex] = Material::CreateShared(desc);

    {
        std::lock_guard<std::mutex> lock(g_gltfMaterialCacheMutex);
        g_sharedMaterialCache[sharedCacheKey] = SharedMaterialCacheEntry{ cache.materialCache[materialIndex] };
    }

    return cache.materialCache[materialIndex];
}

std::shared_ptr<Material> ResolvePrimitiveMaterial(
    const json& gltf,
    const std::filesystem::path& sourcePath,
    GlTFMaterialCache& cache,
    const json& primitiveNode,
    const std::shared_ptr<Material>& defaultMaterial)
{
    if (!primitiveNode.contains("material") || !gltf.contains("materials") || !gltf["materials"].is_array()) {
        return defaultMaterial;
    }

    return LoadMaterial(gltf, sourcePath, cache, primitiveNode["material"].get<size_t>());
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
        const std::filesystem::path sourcePath(filePath);

        auto scene = std::make_shared<Scene>();
        auto defaultMaterial = Material::GetDefaultMaterial();
        GlTFMaterialCache materialCache;
        materialCache.sourceKey = NormalizeSourceKey(sourcePath);
        materialCache.bufferSources = BuildBufferSources(extraction.gltf, sourcePath);
        if (extraction.gltf.contains("materials") && extraction.gltf["materials"].is_array()) {
            materialCache.materialCache.resize(extraction.gltf["materials"].size());
        }

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
            const auto& primitiveNode = extraction.gltf["meshes"][ep.meshIndex]["primitives"][ep.primitiveIndex];
            auto material = ResolvePrimitiveMaterial(
                extraction.gltf,
                sourcePath,
                materialCache,
                primitiveNode,
                defaultMaterial);
            auto mesh = ep.result.ingest.Build(
                material,
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
