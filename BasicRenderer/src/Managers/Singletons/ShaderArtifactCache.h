#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Render/PipelineState.h"

namespace shadercache {

inline constexpr uint32_t kSchemaVersion = 1;

enum class ArtifactKind : uint8_t {
    Bundle = 1,
    Library = 2,
};

enum class BlobKind : uint8_t {
    Vertex = 1,
    Pixel = 2,
    Amplification = 3,
    Mesh = 4,
    Compute = 5,
    Library = 6,
};

struct CacheKey {
    ArtifactKind artifactKind = ArtifactKind::Bundle;
    uint64_t identityHash = 0;
};

struct CachedShaderBlob {
    BlobKind kind = BlobKind::Vertex;
    std::string entryPoint;
    std::string target;
    std::vector<std::byte> dxil;
};

struct CacheData {
    uint32_t schemaVersion = kSchemaVersion;
    uint64_t buildConfigHash = 0;
    ArtifactKind artifactKind = ArtifactKind::Bundle;
    std::vector<CachedShaderBlob> blobs;
    PipelineResources resourceDescriptorSlots;
    uint64_t resourceIDsHash = 0;
};

std::wstring BuildCacheFileName(const CacheKey& key, uint64_t buildConfigHash);
std::optional<CacheData> TryLoad(const CacheKey& key, uint64_t expectedBuildConfigHash);
bool Save(const CacheKey& key, const CacheData& data);

} // namespace shadercache