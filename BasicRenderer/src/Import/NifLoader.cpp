#include "Import/NifLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <spdlog/spdlog.h>

#include "Import/BRNiflyClient.h"
#include "Import/CLodCache.h"
#include "Scene/Scene.h"
#include "Utilities/CachePathUtilities.h"

namespace NifLoader {
namespace {

namespace fs = std::filesystem;
using namespace pxr;

constexpr int kAssetCacheSchemaVersion = 1;
constexpr std::string_view kAssetCacheSuffix = ".asset.usdc";
constexpr std::string_view kRootPrimPath = "/BRNifly";

std::uint64_t ElapsedMs(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
}

std::string Hex64(std::uint64_t value)
{
    std::ostringstream out;
    out << std::hex;
    out.width(16);
    out.fill('0');
    out << value;
    return out.str();
}

std::uint64_t Fnv1a64(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string NormalizeNifCacheKey(std::string_view cacheKey)
{
    std::string normalized;
    normalized.reserve(cacheKey.size());
    for (unsigned char ch : cacheKey) {
        char out = static_cast<char>(ch);
        if (out == '/') {
            out = '\\';
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(out))));
    }

    while (!normalized.empty() && (normalized.front() == '\\' || normalized.front() == '/')) {
        normalized.erase(normalized.begin());
    }
    return normalized;
}

std::string SanitizeFileStem(std::string_view value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        return "nif";
    }
    return sanitized;
}

std::string NifStemFromCacheKey(const std::string& normalizedCacheKey)
{
    fs::path path(s2ws(normalizedCacheKey));
    auto stem = ws2s(path.stem().wstring());
    return SanitizeFileStem(stem);
}

bool IsKnownNonRenderableNif(const std::string& normalizedCacheKey)
{
    fs::path path(s2ws(normalizedCacheKey));
    const std::string stem = ws2s(path.stem().wstring());
    return stem == "skeleton" ||
        stem == "skeleton_female" ||
        stem == "skeletonbeast" ||
        stem == "skeletonbeast_female" ||
        stem == "camerashake";
}

std::string MakeStableSourceIdentifier(const std::string& normalizedCacheKey, const std::string& contentHash)
{
    std::string uriPath = normalizedCacheKey;
    std::replace(uriPath.begin(), uriPath.end(), '\\', '/');
    return "sarp-nif://" + uriPath + "#brnifly=" + contentHash;
}

std::string MakeAssetFileName(const std::string& normalizedCacheKey, const std::string& pathHash, const std::string& contentHash)
{
    return NifStemFromCacheKey(normalizedCacheKey) + "__p" + pathHash + "__c" + contentHash + std::string(kAssetCacheSuffix);
}

bool HasAssetCacheSuffix(const fs::path& path)
{
    const auto fileName = path.filename().string();
    return fileName.size() >= kAssetCacheSuffix.size() &&
        fileName.ends_with(kAssetCacheSuffix);
}

std::string ExtractContentHashFromFileName(const fs::path& path)
{
    const auto fileName = path.filename().string();
    const auto marker = fileName.find("__c");
    if (marker == std::string::npos) {
        return {};
    }
    const auto begin = marker + 3;
    const auto end = fileName.find(std::string(kAssetCacheSuffix), begin);
    if (end == std::string::npos || end <= begin) {
        return {};
    }
    return fileName.substr(begin, end - begin);
}

std::string ExtractPathHashFromFileName(const fs::path& path)
{
    const auto fileName = path.filename().string();
    const auto pathMarker = fileName.find("__p");
    if (pathMarker == std::string::npos) {
        return {};
    }
    const auto begin = pathMarker + 3;
    const auto contentMarker = fileName.find("__c", begin);
    if (contentMarker == std::string::npos || contentMarker <= begin) {
        return {};
    }
    return fileName.substr(begin, contentMarker - begin);
}

fs::path CLodCacheRoot()
{
    return fs::current_path() / "cache" / "clod";
}

struct AssetCacheIndex {
    std::once_flag buildOnce;
    std::mutex mutex;
    std::unordered_map<std::string, std::vector<fs::path>> byPathHash;
};

AssetCacheIndex& GetAssetCacheIndex()
{
    static AssetCacheIndex index;
    return index;
}

void SortNewestFirst(std::vector<fs::path>& paths)
{
    std::sort(paths.begin(), paths.end(), [](const fs::path& lhs, const fs::path& rhs) {
        std::error_code lhsEc;
        std::error_code rhsEc;
        const auto lhsTime = fs::last_write_time(lhs, lhsEc);
        const auto rhsTime = fs::last_write_time(rhs, rhsEc);
        if (lhsEc || rhsEc || lhsTime == rhsTime) {
            return lhs.string() > rhs.string();
        }
        return lhsTime > rhsTime;
    });
}

void BuildAssetCacheIndex()
{
    auto& index = GetAssetCacheIndex();
    std::unordered_map<std::string, std::vector<fs::path>> built;
    const fs::path root = CLodCacheRoot();
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }

            const auto path = it->path();
            if (!HasAssetCacheSuffix(path)) {
                continue;
            }

            const std::string pathHash = ExtractPathHashFromFileName(path);
            if (!pathHash.empty()) {
                built[pathHash].push_back(path);
            }
        }
    }

    for (auto& [_, paths] : built) {
        SortNewestFirst(paths);
    }

    std::lock_guard<std::mutex> lock(index.mutex);
    index.byPathHash = std::move(built);
}

std::vector<fs::path> FindCachedAssets(const std::string& pathHash)
{
    auto& index = GetAssetCacheIndex();
    std::call_once(index.buildOnce, BuildAssetCacheIndex);

    std::lock_guard<std::mutex> lock(index.mutex);
    auto it = index.byPathHash.find(pathHash);
    if (it == index.byPathHash.end()) {
        return {};
    }
    return it->second;
}

void RegisterCachedAsset(const std::string& pathHash, const fs::path& cachePath)
{
    if (pathHash.empty() || cachePath.empty()) {
        return;
    }

    auto& index = GetAssetCacheIndex();
    std::call_once(index.buildOnce, BuildAssetCacheIndex);

    std::lock_guard<std::mutex> lock(index.mutex);
    auto& paths = index.byPathHash[pathHash];
    if (std::find(paths.begin(), paths.end(), cachePath) == paths.end()) {
        paths.push_back(cachePath);
        SortNewestFirst(paths);
    }
}

bool ReadStringMetadata(const UsdPrim& prim, const TfToken& key, std::string& out)
{
    VtValue value = prim.GetCustomDataByKey(key);
    if (!value.IsHolding<std::string>()) {
        return false;
    }
    out = value.UncheckedGet<std::string>();
    return true;
}

bool ValidateAssetStage(
    const UsdStageRefPtr& stage,
    const std::string& normalizedCacheKey,
    const std::string& pathHash,
    const std::string& fallbackContentHash,
    std::string& outSourceIdentifier,
    std::string& outContentHash,
    std::string& outReason)
{
    if (!stage) {
        outReason = "stage open failed";
        return false;
    }

    const UsdPrim root = stage->GetPrimAtPath(SdfPath(std::string(kRootPrimPath)));
    if (!root) {
        outReason = "missing /BRNifly root";
        return false;
    }

    int schemaVersion = 0;
    VtValue versionValue = root.GetCustomDataByKey(TfToken("sarp:nifAssetCache:schemaVersion"));
    if (!versionValue.IsHolding<int>() ||
        (schemaVersion = versionValue.UncheckedGet<int>()) != kAssetCacheSchemaVersion) {
        outReason = "asset cache schema mismatch";
        return false;
    }

    std::string metadataCacheKey;
    std::string metadataPathHash;
    if (!ReadStringMetadata(root, TfToken("sarp:nifAssetCache:normalizedGamePath"), metadataCacheKey) ||
        !ReadStringMetadata(root, TfToken("sarp:nifAssetCache:pathHash"), metadataPathHash) ||
        metadataCacheKey != normalizedCacheKey ||
        metadataPathHash != pathHash) {
        outReason = "asset cache key mismatch";
        return false;
    }

    if (!ReadStringMetadata(root, TfToken("sarp:nifAssetCache:contentHash"), outContentHash)) {
        outContentHash = fallbackContentHash;
    }
    if (!ReadStringMetadata(root, TfToken("sarp:nifAssetCache:sourceIdentifier"), outSourceIdentifier)) {
        outSourceIdentifier = MakeStableSourceIdentifier(normalizedCacheKey, outContentHash);
    }

    if (outContentHash.empty() || outSourceIdentifier.empty()) {
        outReason = "asset cache identity metadata is incomplete";
        return false;
    }

    return true;
}

USDLoader::InMemoryStageOptions MakeStageOptions(
    const std::string& sourceIdentifier,
    const std::string& sourceDirectory,
    const std::string& layerIdentifierHint)
{
    USDLoader::InMemoryStageOptions options{};
    options.sourceIdentifier = sourceIdentifier;
    options.sourceDirectory = sourceDirectory;
    options.layerIdentifierHint = layerIdentifierHint;
    options.isUsdPackage = false;
    return options;
}

void SetStringMetadata(const UsdPrim& prim, const char* key, const std::string& value)
{
    prim.SetCustomDataByKey(TfToken(key), VtValue(value));
}

void SetAssetCacheMetadata(
    const UsdPrim& root,
    const std::string& normalizedCacheKey,
    const std::string& pathHash,
    const std::string& contentHash,
    const std::string& sourceIdentifier,
    const std::string& sourcePath)
{
    root.SetCustomDataByKey(TfToken("sarp:nifAssetCache:schemaVersion"), VtValue(kAssetCacheSchemaVersion));
    SetStringMetadata(root, "sarp:nifAssetCache:normalizedGamePath", normalizedCacheKey);
    SetStringMetadata(root, "sarp:nifAssetCache:pathHash", pathHash);
    SetStringMetadata(root, "sarp:nifAssetCache:contentHash", contentHash);
    SetStringMetadata(root, "sarp:nifAssetCache:sourceIdentifier", sourceIdentifier);
    SetStringMetadata(root, "sarp:nifAssetCache:sourcePath", sourcePath);
}

std::optional<fs::path> WriteAssetCache(
    const BRNiflyClient::UsdAssetPackage& package,
    const std::string& normalizedCacheKey,
    const std::string& pathHash,
    const std::string& stableSourceIdentifier,
    const std::string& sourceDirectory)
{
    static std::mutex usdAssetCacheWriteMutex;
    std::lock_guard<std::mutex> usdAssetCacheWriteLock(usdAssetCacheWriteMutex);

    const std::string fileName = MakeAssetFileName(normalizedCacheKey, pathHash, package.contentHash);
    const fs::path cachePath = CLodCache::GetCacheFilePathForSource(s2ws(fileName), stableSourceIdentifier);
    std::error_code existsEc;
    if (fs::is_regular_file(cachePath, existsEc)) {
        RegisterCachedAsset(pathHash, cachePath);
        return cachePath;
    }

    SdfLayerRefPtr sourceLayer = SdfLayer::CreateAnonymous("brnifly_asset_cache_source.usda");
    if (!sourceLayer || !sourceLayer->ImportFromString(package.rootLayerText)) {
        spdlog::warn(
            "nif_asset_cache=write_failed game='{}' content_hash='{}' reason='failed to import BRNifly USDA'",
            normalizedCacheKey,
            package.contentHash);
        return std::nullopt;
    }

    UsdStageRefPtr stage = UsdStage::Open(sourceLayer);
    if (!stage) {
        spdlog::warn(
            "nif_asset_cache=write_failed game='{}' content_hash='{}' reason='failed to open BRNifly stage'",
            normalizedCacheKey,
            package.contentHash);
        return std::nullopt;
    }

    UsdPrim root = stage->GetPrimAtPath(SdfPath(std::string(kRootPrimPath)));
    if (!root) {
        spdlog::warn(
            "nif_asset_cache=write_failed game='{}' content_hash='{}' reason='missing /BRNifly root'",
            normalizedCacheKey,
            package.contentHash);
        return std::nullopt;
    }

    SetAssetCacheMetadata(root, normalizedCacheKey, pathHash, package.contentHash, stableSourceIdentifier, package.sourcePath);

    std::error_code ec;
    fs::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        spdlog::warn(
            "nif_asset_cache=write_failed game='{}' path='{}' reason='{}'",
            normalizedCacheKey,
            cachePath.string(),
            ec.message());
        return std::nullopt;
    }

    if (!stage->GetRootLayer()->Export(cachePath.string())) {
        spdlog::warn(
            "nif_asset_cache=write_failed game='{}' path='{}' reason='USD export failed'",
            normalizedCacheKey,
            cachePath.string());
        return std::nullopt;
    }

    spdlog::info(
        "nif_asset_cache=write game='{}' path='{}' content_hash='{}' source_identifier='{}'",
        normalizedCacheKey,
        cachePath.string(),
        package.contentHash,
        stableSourceIdentifier);
    RegisterCachedAsset(pathHash, cachePath);
    return cachePath;
}

std::shared_ptr<Scene> LoadCachedStage(
    const fs::path& cachePath,
    const std::string& sourceIdentifier,
    const std::string& contentHash,
    const USDLoader::ImportSettings& settings)
{
    const auto options = MakeStageOptions(
        sourceIdentifier,
        cachePath.parent_path().string(),
        "brnifly_" + contentHash + ".asset.usdc");
    return USDLoader::LoadModelFromFile(cachePath.string(), options, settings);
}

} // namespace

std::optional<CachedAssetLoadResult> TryLoadCachedModel(std::string cacheKey, const USDLoader::ImportSettings& settings, LoadTimingStats* stats)
{
    const auto probeBegin = std::chrono::steady_clock::now();
    const std::string normalizedCacheKey = NormalizeNifCacheKey(cacheKey);
    if (normalizedCacheKey.empty()) {
        if (stats) {
            stats->cacheProbeMs += ElapsedMs(probeBegin, std::chrono::steady_clock::now());
        }
        return std::nullopt;
    }

    const std::string pathHash = Hex64(Fnv1a64(normalizedCacheKey));
    auto candidates = FindCachedAssets(pathHash);
    if (candidates.empty()) {
        spdlog::info("nif_asset_cache=miss game='{}' path_hash='{}' reason='no asset usdc found'", normalizedCacheKey, pathHash);
        if (stats) {
            stats->cacheProbeMs += ElapsedMs(probeBegin, std::chrono::steady_clock::now());
        }
        return std::nullopt;
    }

    for (const auto& cachePath : candidates) {
        const std::string fileContentHash = ExtractContentHashFromFileName(cachePath);
        std::string sourceIdentifier;
        std::string contentHash;
        std::string reason;

        UsdStageRefPtr stage = UsdStage::Open(cachePath.string(), UsdStage::LoadNone);
        if (!ValidateAssetStage(stage, normalizedCacheKey, pathHash, fileContentHash, sourceIdentifier, contentHash, reason)) {
            spdlog::warn(
                "nif_asset_cache=fallback game='{}' path='{}' reason='{}'",
                normalizedCacheKey,
                cachePath.string(),
                reason);
            continue;
        }

        if (stats) {
            stats->cacheProbeMs += ElapsedMs(probeBegin, std::chrono::steady_clock::now());
        }
        const auto usdLoadBegin = std::chrono::steady_clock::now();
        auto scene = LoadCachedStage(cachePath, sourceIdentifier, contentHash, settings);
        if (stats) {
            stats->usdLoadMs += ElapsedMs(usdLoadBegin, std::chrono::steady_clock::now());
        }
        if (!scene) {
            spdlog::warn(
                "nif_asset_cache=fallback game='{}' path='{}' content_hash='{}' reason='cached USD load failed'",
                normalizedCacheKey,
                cachePath.string(),
                contentHash);
            continue;
        }

        spdlog::info(
            "nif_asset_cache=hit game='{}' path='{}' content_hash='{}' source_identifier='{}'",
            normalizedCacheKey,
            cachePath.string(),
                contentHash,
                sourceIdentifier);
        if (stats) {
            stats->cacheHit = true;
            stats->cachePath = cachePath;
            stats->sourceIdentifier = sourceIdentifier;
            stats->contentHash = contentHash;
        }
        return CachedAssetLoadResult{
            .scene = std::move(scene),
            .cachePath = cachePath,
            .sourceIdentifier = std::move(sourceIdentifier),
            .contentHash = std::move(contentHash)
        };
    }

    if (stats) {
        stats->cacheProbeMs += ElapsedMs(probeBegin, std::chrono::steady_clock::now());
    }
    return std::nullopt;
}

std::shared_ptr<Scene> LoadModelWithCacheKey(std::string filePath, std::string cacheKey, const USDLoader::ImportSettings& settings, LoadTimingStats* stats)
{
    const std::string normalizedCacheKey = NormalizeNifCacheKey(cacheKey.empty() ? filePath : cacheKey);
    const std::string pathHash = Hex64(Fnv1a64(normalizedCacheKey));
    if (IsKnownNonRenderableNif(normalizedCacheKey)) {
        spdlog::info("Skipping known non-renderable NIF '{}'", normalizedCacheKey);
        return nullptr;
    }

    std::string errorMessage;
    const auto brniflyBegin = std::chrono::steady_clock::now();
    auto package = BRNiflyClient::ConvertNifToUsd(filePath, {}, &errorMessage);
    if (stats) {
        stats->brniflyMs += ElapsedMs(brniflyBegin, std::chrono::steady_clock::now());
    }
    if (!package) {
        spdlog::error("NIF import failed for '{}': {}", filePath, errorMessage);
        return nullptr;
    }

    for (const auto& diagnostic : package->diagnostics) {
        if (diagnostic.level == "warning") {
            spdlog::warn("BRNifly: {}", diagnostic.message);
        }
        else if (diagnostic.level == "error") {
            spdlog::error("BRNifly: {}", diagnostic.message);
        }
        else {
            spdlog::info("BRNifly: {}", diagnostic.message);
        }
    }

    const std::string stableSourceIdentifier = MakeStableSourceIdentifier(normalizedCacheKey, package->contentHash);
    const std::string sourceDirectory = fs::path(filePath).parent_path().string();
    const auto assetWriteBegin = std::chrono::steady_clock::now();
    const auto cachePath = WriteAssetCache(package.value(), normalizedCacheKey, pathHash, stableSourceIdentifier, sourceDirectory);
    if (stats) {
        stats->assetWriteMs += ElapsedMs(assetWriteBegin, std::chrono::steady_clock::now());
    }
    if (cachePath) {
        if (stats) {
            stats->assetCacheWritten = true;
            stats->cachePath = *cachePath;
            stats->sourceIdentifier = stableSourceIdentifier;
            stats->contentHash = package->contentHash;
        }
        const auto usdLoadBegin = std::chrono::steady_clock::now();
        auto scene = LoadCachedStage(*cachePath, stableSourceIdentifier, package->contentHash, settings);
        if (stats) {
            stats->usdLoadMs += ElapsedMs(usdLoadBegin, std::chrono::steady_clock::now());
        }
        if (scene) {
            return scene;
        }
        spdlog::warn(
            "nif_asset_cache=fallback game='{}' path='{}' content_hash='{}' reason='fresh cached USD load failed; loading in-memory USD'",
            normalizedCacheKey,
            cachePath->string(),
            package->contentHash);
    }

    auto options = MakeStageOptions(
        stableSourceIdentifier,
        sourceDirectory,
        "brnifly_" + package->contentHash + ".usda");
    const auto usdLoadBegin = std::chrono::steady_clock::now();
    auto scene = USDLoader::LoadModelFromUsdBytes(package->rootLayerText, options, settings);
    if (stats) {
        stats->usdLoadMs += ElapsedMs(usdLoadBegin, std::chrono::steady_clock::now());
    }
    return scene;
}

std::shared_ptr<Scene> LoadModel(std::string filePath, const USDLoader::ImportSettings& settings)
{
    if (auto cached = TryLoadCachedModel(filePath, settings, nullptr)) {
        return cached->scene;
    }

    return LoadModelWithCacheKey(filePath, filePath, settings, nullptr);
}

} // namespace NifLoader
