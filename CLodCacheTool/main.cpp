// CLodCacheTool - Offline ClusterLOD cache builder
//
// Usage:  CLodCacheTool <file1> [file2 ...]
//
// Supported formats (auto-detected by extension):
//   .usd / .usda / .usdc / .usdz    -> USD
//   .gltf / .glb                     -> glTF
//   anything else (fbx, obj, ...)    -> Assimp
//
// Caches are written to the same location the renderer would use,
// so a subsequent renderer launch will hit the cache instead of rebuilding.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Import/GlTFGeometryExtractor.h"
#include "Import/AssimpGeometryExtractor.h"
#include "Import/USDGeometryExtractor.h"

namespace fs = std::filesystem;

// Helpers

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

enum class AssetFormat { USD, GlTF, Assimp };

static AssetFormat DetectFormat(const fs::path& path) {
    auto ext = ToLower(path.extension().string());
    if (ext == ".usd" || ext == ".usda" || ext == ".usdc" || ext == ".usdz")
        return AssetFormat::USD;
    if (ext == ".gltf" || ext == ".glb")
        return AssetFormat::GlTF;
    return AssetFormat::Assimp;   // fbx, obj, dae, ...
}

static const char* FormatName(AssetFormat f) {
    switch (f) {
        case AssetFormat::USD:   return "USD";
        case AssetFormat::GlTF:  return "glTF";
        case AssetFormat::Assimp: return "Assimp";
    }
    return "?";
}

// Processing

static bool ProcessFile(const fs::path& path) {
    auto canonical = fs::weakly_canonical(path);
    auto pathStr   = canonical.string();
    auto fmt       = DetectFormat(canonical);

    spdlog::info("---------------------------------------------------");
    spdlog::info("[{}] Processing: {}", FormatName(fmt), pathStr);
    spdlog::info("  File size: {} bytes", fs::file_size(canonical));

    auto t0 = std::chrono::steady_clock::now();

    try {
        switch (fmt) {
        case AssetFormat::USD: {
            spdlog::info("  Opening USD stage...");
            auto result = USDGeometryExtractor::ExtractAll(pathStr);
            spdlog::info("  USD result: meshes={}, submeshes={}, caches_built={}",
                         result.meshesProcessed,
                         result.submeshesProcessed,
                         result.cachesBuilt);
            if (result.meshesProcessed == 0)
                spdlog::warn("  No UsdGeomMesh prims found in stage!");
            break;
        }
        case AssetFormat::GlTF: {
            spdlog::info("  Loading glTF...");
            auto result = GlTFGeometryExtractor::ExtractAll(pathStr);
            spdlog::info("  glTF result: primitives extracted: {}",
                         result.primitives.size());
            if (result.primitives.empty())
                spdlog::warn("  No primitives found in glTF file!");
            break;
        }
        case AssetFormat::Assimp: {
            spdlog::info("  Loading via Assimp...");
            auto result = AssimpGeometryExtractor::ExtractAll(pathStr);
            spdlog::info("  Assimp result: meshes extracted: {}",
                         result.meshes.size());
            if (result.meshes.empty())
                spdlog::warn("  No meshes found via Assimp!");
            break;
        }
        }
    } catch (const std::exception& ex) {
        spdlog::error("  EXCEPTION: {}", ex.what());
        return false;
    } catch (...) {
        spdlog::error("  UNKNOWN EXCEPTION (non-std::exception)");
        return false;
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    spdlog::info("  Elapsed: {} ms", ms);
    return true;
}

int main(int argc, char* argv[]) {
    
    spdlog::set_default_logger(spdlog::stdout_color_mt("CLodCacheTool"));
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::flush_on(spdlog::level::info);

    spdlog::info("CLodCacheTool starting  (argc={})", argc);
    spdlog::info("Working directory: {}", fs::current_path().string());

    {
        auto cacheRoot = fs::current_path() / "cache";
        spdlog::info("Cache root directory: {}", cacheRoot.string());
        if (fs::exists(cacheRoot))
            spdlog::info("  (cache root already exists)");
        else
            spdlog::info("  (cache root does NOT exist yet - will be created on first save)");
    }

    if (argc < 2) {
        spdlog::error("No arguments provided.");
        std::cerr << "Usage: CLodCacheTool <file1|dir1> [file2|dir2 ...]\n";
        return 1;
    }

    for (int i = 1; i < argc; ++i)
        spdlog::info("  argv[{}] = \"{}\"", i, argv[i]);

    // Initialise task scheduler
    spdlog::info("Initialising task scheduler...");
    auto& scheduler = br::TaskSchedulerManager::GetInstance();
    scheduler.Initialize();
    spdlog::info("Task scheduler ready.");

    // Gather files
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) {
        fs::path p(argv[i]);
        if (!fs::exists(p)) {
            spdlog::warn("Skipping non-existent path: {}", argv[i]);
            continue;
        }
        if (fs::is_directory(p)) {
            spdlog::info("Scanning directory: {}", fs::weakly_canonical(p).string());
            size_t before = files.size();
            for (auto& entry : fs::recursive_directory_iterator(p)) {
                if (entry.is_regular_file())
                    files.push_back(entry.path());
            }
            spdlog::info("  Found {} file(s) in directory.", files.size() - before);
        } else {
            files.push_back(p);
        }
    }

    if (files.empty()) {
        spdlog::error("No input files found after scanning arguments.");
        scheduler.Cleanup();
        return 1;
    }

    spdlog::info("Gathered {} file(s):", files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        auto ext = ToLower(files[i].extension().string());
        auto fmt = DetectFormat(files[i]);
        spdlog::info("  [{}] ({}) {}", i, FormatName(fmt), files[i].string());
    }

    auto totalT0 = std::chrono::steady_clock::now();

    int successes = 0;
    int failures  = 0;

    for (auto& f : files) {
        if (ProcessFile(f))
            ++successes;
        else
            ++failures;
    }

    auto totalT1 = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalT1 - totalT0).count();

    spdlog::info("=====================================================");
    spdlog::info("Done.  {} succeeded, {} failed.  Total time: {} ms", successes, failures, totalMs);

    // Report cache directory contents
    {
        auto cacheRoot = fs::current_path() / "cache";
        if (fs::exists(cacheRoot)) {
            size_t cacheFileCount = 0;
            uintmax_t cacheBytes = 0;
            for (auto& entry : fs::recursive_directory_iterator(cacheRoot)) {
                if (entry.is_regular_file()) {
                    ++cacheFileCount;
                    cacheBytes += entry.file_size();
                }
            }
            spdlog::info("Cache directory: {} file(s), {:.2f} MB total",
                         cacheFileCount, cacheBytes / (1024.0 * 1024.0));
        } else {
            spdlog::warn("Cache directory was NOT created - no caches were written.");
        }
    }

    scheduler.Cleanup();
    return failures > 0 ? 1 : 0;
}
