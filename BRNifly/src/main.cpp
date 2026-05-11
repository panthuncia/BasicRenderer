#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <pxr/pxr.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace pxr;

namespace {

struct Diagnostic {
    std::string level;
    std::string message;
};

json DiagnosticJson(const Diagnostic& diagnostic)
{
    return json{{"level", diagnostic.level}, {"message", diagnostic.message}};
}

void AddDiagnostic(std::vector<Diagnostic>& diagnostics, std::string level, std::string message)
{
    diagnostics.push_back(Diagnostic{std::move(level), std::move(message)});
}

std::string SanitizePrimName(std::string name)
{
    if (name.empty()) {
        return "Shape";
    }
    for (char& ch : name) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) {
            ch = '_';
        }
    }
    if (name.front() >= '0' && name.front() <= '9') {
        name.insert(name.begin(), '_');
    }
    return name;
}

uint64_t Fnv1a64(const std::string& text)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string HexHash(uint64_t hash)
{
    std::ostringstream stream;
    stream << std::hex << hash;
    return stream.str();
}

std::vector<std::string> ServiceList()
{
    return {
        "nif.open",
        "nif.close",
        "nif.describe",
        "nif.convert.usd",
        "nif.stream.blocks",
        "nif.stream.geometry",
        "nif.stream.materials",
        "nif.stream.skeleton",
        "nif.stream.animation",
        "nif.stream.morphs",
        "nif.stream.collision"
    };
}

std::vector<std::string> SupportedGames()
{
    return {
        "Skyrim LE",
        "Skyrim SE",
        "Fallout 3",
        "Fallout NV",
        "Fallout 4",
        "Fallout 76"
    };
}

#ifdef _WIN32
using LoadFn = void* (*)(const char*);
using DestroyFn = void (*)(void*);
using GetVersionFn = const int* (*)();
using GetGameNameFn = int (*)(void*, char*, int);
using GetShapesFn = int (*)(void*, void**, int, int);
using GetShapeNameFn = int (*)(void*, char*, int);
using GetVertsForShapeFn = int (*)(void*, void*, float*, int, int);
using GetNormalsForShapeFn = int (*)(void*, void*, float*, int, int);
using GetTrianglesFn = int (*)(void*, void*, uint16_t*, int, int);
using GetUVsFn = int (*)(void*, void*, float*, int, int);
using GetShaderTextureSlotFn = int (*)(void*, void*, int, char*, int);
using GetMessageLogFn = int (*)(char*, int);
using ClearMessageLogFn = void (*)();

struct NiflyApi {
    HMODULE module = nullptr;
    LoadFn load = nullptr;
    DestroyFn destroy = nullptr;
    GetVersionFn getVersion = nullptr;
    GetGameNameFn getGameName = nullptr;
    GetShapesFn getShapes = nullptr;
    GetShapeNameFn getShapeName = nullptr;
    GetVertsForShapeFn getVertsForShape = nullptr;
    GetNormalsForShapeFn getNormalsForShape = nullptr;
    GetTrianglesFn getTriangles = nullptr;
    GetUVsFn getUVs = nullptr;
    GetShaderTextureSlotFn getShaderTextureSlot = nullptr;
    GetMessageLogFn getMessageLog = nullptr;
    ClearMessageLogFn clearMessageLog = nullptr;

    NiflyApi() = default;
    NiflyApi(const NiflyApi&) = delete;
    NiflyApi& operator=(const NiflyApi&) = delete;
    NiflyApi(NiflyApi&& other) noexcept
        : module(other.module),
          load(other.load),
          destroy(other.destroy),
          getVersion(other.getVersion),
          getGameName(other.getGameName),
          getShapes(other.getShapes),
          getShapeName(other.getShapeName),
          getVertsForShape(other.getVertsForShape),
          getNormalsForShape(other.getNormalsForShape),
          getTriangles(other.getTriangles),
          getUVs(other.getUVs),
          getShaderTextureSlot(other.getShaderTextureSlot),
          getMessageLog(other.getMessageLog),
          clearMessageLog(other.clearMessageLog)
    {
        other.module = nullptr;
    }

    NiflyApi& operator=(NiflyApi&& other) noexcept
    {
        if (this != &other) {
            if (module) {
                FreeLibrary(module);
            }
            module = other.module;
            load = other.load;
            destroy = other.destroy;
            getVersion = other.getVersion;
            getGameName = other.getGameName;
            getShapes = other.getShapes;
            getShapeName = other.getShapeName;
            getVertsForShape = other.getVertsForShape;
            getNormalsForShape = other.getNormalsForShape;
            getTriangles = other.getTriangles;
            getUVs = other.getUVs;
            getShaderTextureSlot = other.getShaderTextureSlot;
            getMessageLog = other.getMessageLog;
            clearMessageLog = other.clearMessageLog;
            other.module = nullptr;
        }
        return *this;
    }

    ~NiflyApi()
    {
        if (module) {
            FreeLibrary(module);
        }
    }
};

std::optional<fs::path> FindNiflyDll(const char* argv0)
{
    std::vector<fs::path> candidates;

    if (const char* envPath = std::getenv("NIFLYDLL_PATH")) {
        candidates.emplace_back(envPath);
    }

    const fs::path exePath = fs::absolute(argv0).parent_path();
    candidates.push_back(exePath / "NiflyDLL.dll");
    candidates.push_back(exePath / ".." / "PyNifly" / "NiflyDLL.dll");
    candidates.push_back(exePath / ".." / ".." / ".." / ".." / "PyNifly" / "NiflyDLL" / "out" / "build" / "x64-Release-vs" / "bin" / "Release" / "NiflyDLL.dll");
    candidates.push_back(exePath / ".." / "PyNifly" / "NiflyDLL" / "out" / "build" / "x64-Debug" / "NiflyDLL.dll");
    candidates.push_back(exePath / ".." / "PyNifly" / "NiflyDLL" / "out" / "build" / "x64-Release" / "NiflyDLL.dll");

    for (const fs::path& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            return fs::weakly_canonical(candidate, ec);
        }
    }

    return std::nullopt;
}

template <typename Fn>
Fn LoadProc(HMODULE module, const char* name)
{
    return reinterpret_cast<Fn>(GetProcAddress(module, name));
}

std::optional<NiflyApi> LoadNiflyApi(const char* argv0, std::vector<Diagnostic>& diagnostics)
{
    auto dllPath = FindNiflyDll(argv0);
    if (!dllPath) {
        AddDiagnostic(diagnostics, "error", "NiflyDLL.dll was not found. Set NIFLYDLL_PATH or place it next to BRNifly.exe.");
        return std::nullopt;
    }

    HMODULE module = LoadLibraryW(dllPath->wstring().c_str());
    if (!module) {
        AddDiagnostic(diagnostics, "error", "Failed to load NiflyDLL.dll from " + dllPath->string());
        return std::nullopt;
    }

    NiflyApi api{};
    api.module = module;
    api.load = LoadProc<LoadFn>(module, "load");
    api.destroy = LoadProc<DestroyFn>(module, "destroy");
    api.getVersion = LoadProc<GetVersionFn>(module, "getVersion");
    api.getGameName = LoadProc<GetGameNameFn>(module, "getGameName");
    api.getShapes = LoadProc<GetShapesFn>(module, "getShapes");
    api.getShapeName = LoadProc<GetShapeNameFn>(module, "getShapeName");
    api.getVertsForShape = LoadProc<GetVertsForShapeFn>(module, "getVertsForShape");
    api.getNormalsForShape = LoadProc<GetNormalsForShapeFn>(module, "getNormalsForShape");
    api.getTriangles = LoadProc<GetTrianglesFn>(module, "getTriangles");
    api.getUVs = LoadProc<GetUVsFn>(module, "getUVs");
    api.getShaderTextureSlot = LoadProc<GetShaderTextureSlotFn>(module, "getShaderTextureSlot");
    api.getMessageLog = LoadProc<GetMessageLogFn>(module, "getMessageLog");
    api.clearMessageLog = LoadProc<ClearMessageLogFn>(module, "clearMessageLog");

    if (!api.load || !api.destroy || !api.getShapes || !api.getShapeName || !api.getVertsForShape || !api.getTriangles) {
        AddDiagnostic(diagnostics, "error", "NiflyDLL.dll is missing required geometry entry points.");
        return std::nullopt;
    }

    AddDiagnostic(diagnostics, "info", "Loaded NiflyDLL.dll from " + dllPath->string());
    return api;
}
#else
struct NiflyApi {};
std::optional<NiflyApi> LoadNiflyApi(const char*, std::vector<Diagnostic>& diagnostics)
{
    AddDiagnostic(diagnostics, "error", "BRNifly currently supports niflyDLL loading on Windows only.");
    return std::nullopt;
}
#endif

std::string VersionString(const NiflyApi& api)
{
#ifdef _WIN32
    if (!api.getVersion) {
        return "unknown";
    }
    const int* version = api.getVersion();
    if (!version) {
        return "unknown";
    }
    return std::to_string(version[0]) + "." + std::to_string(version[1]) + "." + std::to_string(version[2]);
#else
    (void)api;
    return "unavailable";
#endif
}

std::string GetMessageLog(const NiflyApi& api)
{
#ifdef _WIN32
    if (!api.getMessageLog) {
        return {};
    }
    std::array<char, 8192> buffer{};
    const int written = api.getMessageLog(buffer.data(), static_cast<int>(buffer.size()));
    if (written <= 0) {
        return {};
    }
    return std::string(buffer.data());
#else
    (void)api;
    return {};
#endif
}

json DescribeServicesJson(const char* argv0)
{
    std::vector<Diagnostic> diagnostics;
    std::string niflyVersion = "unavailable";
    if (auto api = LoadNiflyApi(argv0, diagnostics)) {
        niflyVersion = VersionString(*api);
    }

    json response;
    response["status"] = "ok";
    response["protocolVersion"] = "1.0";
    response["niflyVersion"] = niflyVersion;
    response["openUsdVersion"] = std::to_string(PXR_MAJOR_VERSION) + "." +
        std::to_string(PXR_MINOR_VERSION) + "." + std::to_string(PXR_PATCH_VERSION);
    response["services"] = ServiceList();
    response["supportedGames"] = SupportedGames();
    response["preferredPayload"] = "usda-text";
    response["diagnostics"] = json::array();
    for (const Diagnostic& diagnostic : diagnostics) {
        response["diagnostics"].push_back(DiagnosticJson(diagnostic));
    }
    return response;
}

struct ShapeData {
    std::string name;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<uint16_t> triangles;
    std::vector<std::string> textures;
};

std::optional<std::string> ReadCString(std::function<int(char*, int)> reader)
{
    std::array<char, 1024> buffer{};
    int count = reader(buffer.data(), static_cast<int>(buffer.size()));
    if (count <= 0) {
        return std::nullopt;
    }
    return std::string(buffer.data());
}

#ifdef _WIN32
std::vector<ShapeData> ReadShapes(const NiflyApi& api, void* nifHandle, std::vector<Diagnostic>& diagnostics)
{
    const int shapeCount = api.getShapes(nifHandle, nullptr, 0, 0);
    if (shapeCount <= 0) {
        return {};
    }

    std::vector<void*> shapeHandles(static_cast<size_t>(shapeCount));
    api.getShapes(nifHandle, shapeHandles.data(), shapeCount, 0);

    std::vector<ShapeData> shapes;
    shapes.reserve(shapeHandles.size());

    for (size_t shapeIndex = 0; shapeIndex < shapeHandles.size(); ++shapeIndex) {
        void* shapeHandle = shapeHandles[shapeIndex];
        ShapeData shape{};
        shape.name = ReadCString([&](char* buffer, int size) { return api.getShapeName(shapeHandle, buffer, size); })
            .value_or("Shape_" + std::to_string(shapeIndex));

        const int vertexCount = api.getVertsForShape(nifHandle, shapeHandle, nullptr, 0, 0);
        const int triangleCount = api.getTriangles(nifHandle, shapeHandle, nullptr, 0, 0);
        if (vertexCount <= 0 || triangleCount <= 0) {
            AddDiagnostic(diagnostics, "warning", "Skipping shape '" + shape.name + "' because it has no drawable geometry.");
            continue;
        }

        shape.positions.resize(static_cast<size_t>(vertexCount) * 3u);
        api.getVertsForShape(nifHandle, shapeHandle, shape.positions.data(), static_cast<int>(shape.positions.size()), 0);

        if (api.getNormalsForShape) {
            const int normalCount = api.getNormalsForShape(nifHandle, shapeHandle, nullptr, 0, 0);
            if (normalCount == vertexCount) {
                shape.normals.resize(static_cast<size_t>(vertexCount) * 3u);
                api.getNormalsForShape(nifHandle, shapeHandle, shape.normals.data(), static_cast<int>(shape.normals.size()), 0);
            }
        }

        if (api.getUVs) {
            const int uvCount = api.getUVs(nifHandle, shapeHandle, nullptr, 0, 0);
            if (uvCount == vertexCount) {
                shape.uvs.resize(static_cast<size_t>(vertexCount) * 2u);
                api.getUVs(nifHandle, shapeHandle, shape.uvs.data(), static_cast<int>(shape.uvs.size()), 0);
            }
        }

        shape.triangles.resize(static_cast<size_t>(triangleCount) * 3u);
        api.getTriangles(nifHandle, shapeHandle, shape.triangles.data(), static_cast<int>(shape.triangles.size()), 0);

        if (api.getShaderTextureSlot) {
            for (int slot = 0; slot < 10; ++slot) {
                std::array<char, 1024> textureBuffer{};
                const int len = api.getShaderTextureSlot(nifHandle, shapeHandle, slot, textureBuffer.data(), static_cast<int>(textureBuffer.size()));
                if (len > 0 && textureBuffer[0] != '\0') {
                    shape.textures.emplace_back(textureBuffer.data());
                }
            }
        }

        shapes.push_back(std::move(shape));
    }

    return shapes;
}
#endif

std::optional<std::string> ConvertShapesToUsd(
    const std::vector<ShapeData>& shapes,
    const fs::path& nifPath,
    const std::string& gameName,
    std::vector<Diagnostic>& diagnostics)
{
    SdfLayerRefPtr rootLayer = SdfLayer::CreateAnonymous("brnifly.usda");
    UsdStageRefPtr stage = UsdStage::Open(rootLayer);
    if (!stage) {
        AddDiagnostic(diagnostics, "error", "Failed to create OpenUSD stage.");
        return std::nullopt;
    }

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->z);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);

    UsdGeomXform root = UsdGeomXform::Define(stage, SdfPath("/BRNifly"));
    stage->SetDefaultPrim(root.GetPrim());
    root.GetPrim().SetCustomDataByKey(TfToken("brnifly:sourcePath"), VtValue(nifPath.string()));
    root.GetPrim().SetCustomDataByKey(TfToken("brnifly:gameName"), VtValue(gameName));
    root.GetPrim().SetCustomDataByKey(TfToken("brnifly:unsupportedDataPolicy"), VtValue(std::string("preserve-via-service-streams")));

    size_t emittedMeshes = 0;
    for (size_t shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex) {
        const ShapeData& shape = shapes[shapeIndex];
        const std::string primName = SanitizePrimName(shape.name.empty() ? "Shape_" + std::to_string(shapeIndex) : shape.name);
        const SdfPath meshPath = SdfPath("/BRNifly").AppendChild(TfToken(primName));
        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, meshPath);
        if (!mesh) {
            AddDiagnostic(diagnostics, "warning", "Failed to define USD mesh for shape '" + shape.name + "'.");
            continue;
        }

        VtArray<GfVec3f> points;
        points.reserve(shape.positions.size() / 3u);
        for (size_t i = 0; i + 2 < shape.positions.size(); i += 3) {
            points.push_back(GfVec3f(shape.positions[i], shape.positions[i + 1], shape.positions[i + 2]));
        }
        mesh.CreatePointsAttr(VtValue(points));

        VtArray<int> faceVertexCounts;
        VtArray<int> faceVertexIndices;
        faceVertexCounts.reserve(shape.triangles.size() / 3u);
        faceVertexIndices.reserve(shape.triangles.size());
        for (size_t i = 0; i + 2 < shape.triangles.size(); i += 3) {
            faceVertexCounts.push_back(3);
            faceVertexIndices.push_back(static_cast<int>(shape.triangles[i]));
            faceVertexIndices.push_back(static_cast<int>(shape.triangles[i + 1]));
            faceVertexIndices.push_back(static_cast<int>(shape.triangles[i + 2]));
        }
        mesh.CreateFaceVertexCountsAttr(VtValue(faceVertexCounts));
        mesh.CreateFaceVertexIndicesAttr(VtValue(faceVertexIndices));
        mesh.CreateSubdivisionSchemeAttr(VtValue(UsdGeomTokens->none));

        if (shape.normals.size() == shape.positions.size()) {
            VtArray<GfVec3f> normals;
            normals.reserve(shape.normals.size() / 3u);
            for (size_t i = 0; i + 2 < shape.normals.size(); i += 3) {
                normals.push_back(GfVec3f(shape.normals[i], shape.normals[i + 1], shape.normals[i + 2]));
            }
            mesh.CreateNormalsAttr(VtValue(normals));
            mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
        }

        if (shape.uvs.size() == points.size() * 2u) {
            VtArray<GfVec2f> uvValues;
            uvValues.reserve(points.size());
            for (size_t i = 0; i + 1 < shape.uvs.size(); i += 2) {
                uvValues.push_back(GfVec2f(shape.uvs[i], 1.0f - shape.uvs[i + 1]));
            }
            UsdGeomPrimvarsAPI primvars(mesh.GetPrim());
            UsdGeomPrimvar st = primvars.CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray, UsdGeomTokens->vertex);
            st.Set(uvValues);
        }

        if (!shape.textures.empty()) {
            json textureMetadata = shape.textures;
            mesh.GetPrim().SetCustomDataByKey(TfToken("brnifly:textures"), VtValue(textureMetadata.dump()));
        }

        ++emittedMeshes;
    }

    if (emittedMeshes == 0) {
        AddDiagnostic(diagnostics, "error", "No drawable meshes were emitted from the NIF.");
        return std::nullopt;
    }

    std::string usdText;
    if (!rootLayer->ExportToString(&usdText)) {
        AddDiagnostic(diagnostics, "error", "OpenUSD failed to export the generated layer.");
        return std::nullopt;
    }

    AddDiagnostic(diagnostics, "info", "Converted " + std::to_string(emittedMeshes) + " NIF shape(s) to USD mesh prims.");
    return usdText;
}

json ConvertUsdJson(const char* argv0, const fs::path& nifPath)
{
    std::vector<Diagnostic> diagnostics;
    json response;
    response["status"] = "error";
    response["sourcePath"] = nifPath.string();

    auto api = LoadNiflyApi(argv0, diagnostics);
    if (!api) {
        response["message"] = "Unable to load niflyDLL.";
        response["diagnostics"] = json::array();
        for (const Diagnostic& diagnostic : diagnostics) {
            response["diagnostics"].push_back(DiagnosticJson(diagnostic));
        }
        return response;
    }

#ifdef _WIN32
    if (api->clearMessageLog) {
        api->clearMessageLog();
    }

    void* nifHandle = api->load(nifPath.string().c_str());
    if (!nifHandle) {
        AddDiagnostic(diagnostics, "error", "niflyDLL failed to load '" + nifPath.string() + "'. " + GetMessageLog(*api));
        response["message"] = "niflyDLL failed to load the NIF file.";
        response["diagnostics"] = json::array();
        for (const Diagnostic& diagnostic : diagnostics) {
            response["diagnostics"].push_back(DiagnosticJson(diagnostic));
        }
        return response;
    }

    std::string gameName = "unknown";
    if (api->getGameName) {
        gameName = ReadCString([&](char* buffer, int size) { return api->getGameName(nifHandle, buffer, size); }).value_or("unknown");
    }

    std::vector<ShapeData> shapes = ReadShapes(*api, nifHandle, diagnostics);
    api->destroy(nifHandle);

    auto usdText = ConvertShapesToUsd(shapes, nifPath, gameName, diagnostics);
    if (!usdText) {
        response["message"] = "NIF-to-USD conversion failed.";
        response["diagnostics"] = json::array();
        for (const Diagnostic& diagnostic : diagnostics) {
            response["diagnostics"].push_back(DiagnosticJson(diagnostic));
        }
        return response;
    }

    const std::string hash = HexHash(Fnv1a64(nifPath.string() + *usdText));
    response["status"] = "ok";
    response["protocolVersion"] = "1.0";
    response["sourcePath"] = nifPath.string();
    response["sourceIdentifier"] = nifPath.string() + "#brnifly=" + hash;
    response["contentHash"] = hash;
    response["rootLayerText"] = *usdText;
    response["dependencies"] = json::array();
    response["textureSearchRoots"] = json::array({nifPath.parent_path().string()});
    response["diagnostics"] = json::array();
    for (const Diagnostic& diagnostic : diagnostics) {
        response["diagnostics"].push_back(DiagnosticJson(diagnostic));
    }
    return response;
#else
    response["message"] = "BRNifly conversion is currently implemented for Windows only.";
    return response;
#endif
}

int PrintUsage()
{
    std::cerr << "Usage:\n"
        << "  BRNifly --describe-services\n"
        << "  BRNifly --convert-usd-json <file.nif>\n"
        << "  BRNifly --convert-usd-json-file <file.nif> <response.json>\n"
        << "  BRNifly --convert <file.nif> --out <file.usda>\n";
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    spdlog::set_level(spdlog::level::warn);

    if (argc == 2 && std::string(argv[1]) == "--describe-services") {
        std::cout << DescribeServicesJson(argv[0]).dump(2) << std::endl;
        return 0;
    }

    if (argc == 3 && std::string(argv[1]) == "--convert-usd-json") {
        std::cout << ConvertUsdJson(argv[0], fs::path(argv[2])).dump(2) << std::endl;
        return 0;
    }

    if (argc == 4 && std::string(argv[1]) == "--convert-usd-json-file") {
        json response = ConvertUsdJson(argv[0], fs::path(argv[2]));
        std::ofstream out(argv[3], std::ios::binary);
        if (!out) {
            std::cerr << "Failed to open response file: " << argv[3] << std::endl;
            return 1;
        }
        out << response.dump(2);
        std::cout << "{\"status\":\"ok\",\"responseFile\":" << json(argv[3]).dump() << "}" << std::endl;
        return 0;
    }

    if (argc == 5 && std::string(argv[1]) == "--convert" && std::string(argv[3]) == "--out") {
        json response = ConvertUsdJson(argv[0], fs::path(argv[2]));
        if (response.value("status", "") != "ok") {
            std::cerr << response.dump(2) << std::endl;
            return 1;
        }
        std::ofstream out(argv[4], std::ios::binary);
        out << response.value("rootLayerText", "");
        return 0;
    }

    return PrintUsage();
}
