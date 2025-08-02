#include <spdlog/spdlog.h>
#include <DirectXMath.h>
#include <filesystem>
#include <vector>

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/ar/packageUtils.h>
//#include <pxr/usd/ar/packageResolver.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/plug/registry.h>

#include <flecs.h>

#include "Materials/Material.h"
#include "Materials/MaterialFlags.h"
#include "Render/PSOFlags.h"
#include "Resources/Sampler.h"
#include "Import/Filetypes.h"
#include "Scene/Scene.h"
#include "Mesh/Mesh.h"
#include "Animation/Skeleton.h"
#include "Scene/Components.h"
#include "Animation/AnimationController.h"

#include "Import/USDLoader.h"
#include "Import/USDShaderGraphBuilder.h"

namespace USDLoader {

    using namespace pxr;

    struct LoadingCaches {
        std::unordered_map<std::string, std::shared_ptr<Material>> materialCache;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> meshCache;
        std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;
        std::unordered_map<std::string, std::string> uvSetCache;
        //std::unordered_map<std::string, std::shared_ptr<UsdSkelSkeleton>> unprocessedSkeletons;
        std::unordered_map<std::string, UsdPrim> primsWithSkeletons;
        std::unordered_map<std::string, std::shared_ptr<Skeleton>> skeletonMap;
        std::unordered_map<std::string, std::shared_ptr<Animation>> animationMap;
        // For storing nodes in the USD shader graph
        std::unordered_map<std::string, flecs::entity> nodeMap;

        void Clear() {
            materialCache.clear();
            meshCache.clear();
            textureCache.clear();
            uvSetCache.clear();
            primsWithSkeletons.clear();
            skeletonMap.clear();
            animationMap.clear();
            nodeMap.clear();
		}
	};

	LoadingCaches loadingCache;

    static std::vector<uint32_t> SwizzleToIndices(const std::string& swizzle) {
        std::vector<uint32_t> indices;
        // skip leading dot if present
        size_t start = (!swizzle.empty() && swizzle[0] == '.') ? 1 : 0;
        indices.reserve(swizzle.size() - start);

        for (size_t i = start; i < swizzle.size(); ++i) {
            char c = static_cast<char>(std::tolower(swizzle[i]));
            switch (c) {
            case 'r': case 'x': case 'u':
                indices.push_back(0);
                break;
            case 'g': case 'y': case 'v':
                indices.push_back(1);
                break;
            case 'b': case 'z': case 'w':
                indices.push_back(2);
                break;
            case 'a': case 'q': case 't':
                indices.push_back(3);
                break;
            default:
                spdlog::warn("SwizzleToIndices: unknown component '{}', defaulting to 0", c);
                indices.push_back(0);
                break;
            }
        }
        return indices;
    }

    MaterialDescription ParseMaterialGraph(
        const pxr::UsdShadeMaterial& material,
        const std::string& directory,
        const UsdStageRefPtr& stage,
        bool isUSDZ)
    {
        USDShaderGraphBuilder builder(material);
        builder.Build();
        auto nodes = builder.GetTopologicalNodes();

        MaterialDescription result;

        // helper to load a USD texture node
        auto TryLoadTexture = [&](const pxr::UsdShadeShader& texShader) {
            pxr::SdfAssetPath asset;
            if (texShader.GetInput(pxr::TfToken("file"))
                .Get(&asset))
            {
                std::string path = asset.GetAssetPath();
                return nullptr;// LoadTextureFromDisk(directoryForTextures + "/" + path);
            }
            return nullptr;//std::shared_ptr<Texture>(nullptr);
            };

        // Walk nodes in topo order
        for (auto& node : nodes) {
            TfToken shaderId;
            if (!node.shader.GetIdAttr().Get(&shaderId)) {
                // no info:id, skip
                continue;
            }

            // The core "surface" node: UsdPreviewSurface
            if (shaderId == TfToken("UsdPreviewSurface")) {
                // first pull out unconnected constants
                for (auto const& in : node.inputs) {
                    TfToken name = in.GetBaseName();
                    if (name == TfToken("diffuseColor") && in.GetConnectedSources().empty()) {
                        GfVec3f c; in.Get(&c);
                        result.diffuseColor = { c[0],c[1],c[2],1.0f };
                    }
                    else if (name == TfToken("metallic") && in.GetConnectedSources().empty()) {
                        float v; in.Get(&v);
                        result.metallic.factor = v;
                    }
                    else if (name == TfToken("roughness") && in.GetConnectedSources().empty()) {
                        float v; in.Get(&v);
                        result.roughness.factor = v;
                    }
                    else if (name == TfToken("opacity") && in.GetConnectedSources().empty()) {
                        float v; in.Get(&v);
                        result.opacity.factor = v;
                    }
                    else if (name == TfToken("emissiveColor") && in.GetConnectedSources().empty()) {
                        GfVec3f c; in.Get(&c);
                        result.emissiveColor = { c[0],c[1],c[2],1.0f };
                    }
                }

                // follow connected inputs
                for (auto const& in : node.inputs) {
                    TfToken name = in.GetBaseName();
                    if (name != TfToken("diffuseColor") &&
                        name != TfToken("metallic") &&
                        name != TfToken("roughness") &&
                        name != TfToken("opacity") &&
                        name != TfToken("emissiveColor") &&
						name != TfToken("normal") &&
						name != TfToken("displacement") &&
						name != TfToken("ambientOcclusion"))
                        spdlog::warn("Unsupported input type: {}", in.GetBaseName().GetString());

                    // there may be zero or more connections
                    for (auto const& src : in.GetConnectedSources()) {
                        // only take the *first* UsdUVTexture we find
                        if (auto srcShader = UsdShadeShader(src.source)) {
                            TfToken srcId;
                            srcShader.GetIdAttr().Get(&srcId);

                            if (srcId == TfToken("UsdUVTexture") && !loadingCache.textureCache.contains(src.source.GetPath().GetString())) {
                                // load the texture and stash it
                                SdfAssetPath asset;
                                srcShader.GetInput(TfToken("file")).Get(&asset);
								//auto texPath = asset.GetAssetPath();
								//spdlog::info("Loading texture from path: {}", texPath);

                                UsdShadeInput csInput = srcShader.GetInput(TfToken("sourceColorSpace"));
                                TfToken colorSpaceToken;
                                std::string colorSpace = "linear";
                                if (csInput && csInput.Get(&colorSpaceToken)) {
                                    colorSpace = colorSpaceToken.GetString();
								} // TODO: Use this to set texture color space instead of correcting in shader

                                //auto tex = LoadTextureFromFile(s2ws(directory) + L"/" + s2ws(asset.GetAssetPath()));

                                auto& resolver = ArGetResolver();
                                auto ctx = stage->GetPathResolverContext();
                                ArResolverContextBinder binder(ctx);

                                // Resolve asset path
                                std::string logicalPath = asset.GetResolvedPath();

                                ArResolvedPath resolved = resolver.Resolve(logicalPath);

								// Open the asset
                                std::shared_ptr<ArAsset> arAsset = resolver.OpenAsset(resolved);
                                if (!arAsset) {
                                    throw std::runtime_error(
                                        "Unable to open asset at " + logicalPath);
                                }

                                auto tex = LoadTextureFromMemory(
                                    (void*)arAsset->GetBuffer().get(),
                                    arAsset->GetSize());

                                loadingCache.textureCache[src.source.GetPath().GetString()] = tex;

                                std::string swizzle = src.sourceName.GetString();
                                if (name == TfToken("diffuseColor")) {
                                    result.baseColor.texture = tex;
                                    result.baseColor.channels = SwizzleToIndices(swizzle);
                                    if (result.baseColor.channels.size() == 3) {
                                        // Ensure we have 4 channels
                                        result.baseColor.channels.push_back(3); // TODO: Will this break?
									}
                                }
                                else if (name == TfToken("metallic")) {
                                    result.metallic.texture = tex;
                                    result.metallic.channels = SwizzleToIndices(swizzle);
                                }
                                else if (name == TfToken("roughness")) {
                                    result.roughness.texture = tex;
                                    result.roughness.channels = SwizzleToIndices(swizzle);
                                }
                                else if (name == TfToken("opacity")) {
                                    result.opacity.texture = tex;
                                    result.opacity.channels = SwizzleToIndices(swizzle);
                                }
                                else if (name == TfToken("emissiveColor")) {
                                    result.emissive.texture = tex;
                                    result.emissive.channels = SwizzleToIndices(swizzle);
                                }
                                else if (name == TfToken("normal")) {
                                    result.normal.texture = tex;
                                    result.normal.channels = SwizzleToIndices(swizzle);
                                }
                                else if (name == TfToken("displacement")) {
                                    result.heightMap.texture = tex;
                                    result.heightMap.channels = SwizzleToIndices(swizzle);
                                }
                                else if (name == TfToken("ambientOcclusion")) {
                                    result.aoMap.texture = tex;
                                    result.aoMap.channels = SwizzleToIndices(swizzle);
                                }
                                else {
                                    spdlog::warn("Unknown texture input: {}", name.GetString());
								}

                            }

                            // now map that texture into the correct material slot:
                            auto texIt = loadingCache.textureCache.find(src.source.GetPath().GetString());
                            if (texIt != loadingCache.textureCache.end()) {
                                if (name == TfToken("diffuseColor"))      result.baseColor.texture = texIt->second;
                                else if (name == TfToken("metallic"))     result.metallic.texture = texIt->second;
                                else if (name == TfToken("roughness"))    result.roughness.texture = texIt->second;
                                else if (name == TfToken("opacity"))      result.opacity.texture = texIt->second;
                                else if (name == TfToken("emissiveColor"))result.emissive.texture = texIt->second;
                                else if (name == TfToken("normal"))       result.normal.texture = texIt->second;
                                else if (name == TfToken("displacement")) result.heightMap.texture = texIt->second;
                                else if (name == TfToken("ambientOcclusion")) result.aoMap.texture = texIt->second;
                                else {
                                    spdlog::warn("Unknown texture input: {}", name.GetString());
								}
                            }
                        }
                        else {
                            spdlog::warn("Non-shader connected to {} : skipping.", name.GetText());
                        }
                    }
                }
            }

            else if (shaderId == TfToken("UsdUVTexture")) {
                // we actually loaded these in the surface node pass
            }

            else if (shaderId == TfToken("UsdPrimvarReader_float2")) {
                std::string varnameStr;
                UsdShadeInput in = node.shader.GetInput(TfToken("varname"));
                if (in && in.Get(&varnameStr)) {
                    TfToken varname(varnameStr);
                }
                if (varnameStr.empty()) {
                    spdlog::info("UsdPrimvarReader_float2 node has no string varname set, checking for inputs:varname token.");
                    pxr::UsdAttribute attr = node.shader.GetPrim().GetAttribute(pxr::TfToken("inputs:varname"));
                    if (attr) {
                        pxr::TfToken varnameValue;
                        if (attr.Get(&varnameValue)) {
                            varnameStr = varnameValue.GetString();
                        }
                        else {
                            spdlog::warn("UsdPrimvarReader_float2 node has no varname set, skipping.");
                            continue; // No varname set, skip this node
                        }
                    }
				}
				spdlog::info("Found UsdPrimvarReader_float2 node with varname: {}", varnameStr);
                auto& path = material.GetPrim().GetPath().GetString();
                if (loadingCache.uvSetCache.contains(path) && loadingCache.uvSetCache[path] != varnameStr) {
                    throw std::runtime_error(
						"Multiple UV sets found for material, which is not supported yet. ");
				}
                loadingCache.uvSetCache[material.GetPrim().GetPath().GetString()] = varnameStr;
            }
            // TODO: Other node types?
        }
		result.name = material.GetPrim().GetName().GetString();
        return result;
    }

    void ProcessMaterial(const pxr::UsdShadeMaterial& material, const pxr::UsdStageRefPtr& stage, bool isUSDZ, const std::string& directory) {
        if (loadingCache.materialCache.contains(material.GetPrim().GetPath().GetString())) {
            spdlog::info("Material {} already processed, skipping.", material.GetPrim().GetPath().GetString());
            return; // Already processed
        }

        auto materialDesc = ParseMaterialGraph(material, directory, stage, isUSDZ);

        auto newMaterial = Material::CreateShared(materialDesc);

        loadingCache.materialCache[material.GetPrim().GetPath().GetString()] = newMaterial;
	}

    enum class InterpolationType {
        Constant,
        Uniform,
        Varying,
        Vertex,
        FaceVarying
    };

    static InterpolationType GetInterpolationType(const TfToken& tok) {
        if (tok == UsdGeomTokens->constant)    return InterpolationType::Constant;
        if (tok == UsdGeomTokens->uniform)     return InterpolationType::Uniform;
        if (tok == UsdGeomTokens->varying)     return InterpolationType::Varying;
        if (tok == UsdGeomTokens->vertex)      return InterpolationType::Vertex;
        if (tok == UsdGeomTokens->faceVarying) return InterpolationType::FaceVarying;
        // fallback
        return InterpolationType::Vertex;
    }

    // Fan-triangulate an n-gon index list -> triangles
    static std::vector<uint32_t> TriangulateIndices(
        VtArray<int> const& faceVertCounts,
        VtArray<int> const& faceVertIndices)
    {
        std::vector<uint32_t> out;
        out.reserve(faceVertIndices.size()); // at least as many corners
        size_t offset = 0;
        for (int fvCount : faceVertCounts) {
            // a single triangle
            if (fvCount == 3) {
                out.push_back(faceVertIndices[offset + 0]);
                out.push_back(faceVertIndices[offset + 1]);
                out.push_back(faceVertIndices[offset + 2]);
            }
            else {
                // fan: (0,i,i+1)
                for (int i = 1; i + 1 < fvCount; ++i) {
                    out.push_back(faceVertIndices[offset + 0]);
                    out.push_back(faceVertIndices[offset + i]);
                    out.push_back(faceVertIndices[offset + i + 1]);
                }
            }
            offset += fvCount;
        }
        return out;
    }

    // Builds a flat float buffer from a GfVecN array
    template<typename GfVecN>
    static void FlattenVecArray(
        VtArray<GfVecN> const& src,
        std::vector<float>& dst,
        float                   scale = 1.0f)
    {
        constexpr size_t N = GfVecN::dimension;
        dst.clear();
        dst.reserve(src.size() * N);
        for (auto const& v : src) {
            for (size_t i = 0; i < N; ++i)
                dst.push_back(float(v[i] * scale));
        }
    }

    template<typename T>
    inline void EmitPrimvar(
        std::vector<T>& dst,             // where to append
        std::vector<T> const& raw,             // flat raw array
        size_t                numComponents,   // e.g. 3 for normals, 2 for uvs, 4 for skin
        InterpolationType     interp,          // how it was authored
        size_t                faceIndex,       // current face number
        size_t                fvIndex,         // index into face vertex array
        uint32_t              vertIndex)       // control point index
    {
        size_t base = 0;
        switch (interp) {
        case InterpolationType::Constant:
            // one tuple at raw[0...numComponents-1]
            base = 0;
            break;
        case InterpolationType::Uniform:
            // one tuple per face
            base = faceIndex * numComponents;
            break;
        case InterpolationType::Vertex:
        case InterpolationType::Varying:
            // one tuple per control point
            base = vertIndex * numComponents;
            break;
        case InterpolationType::FaceVarying:
            // one tuple per face vertex
            base = fvIndex * numComponents;
            break;
        }
        // copy the tuple
        for (size_t c = 0; c < numComponents; ++c) {
            dst.push_back(raw[base + c]);
        }
    }

    MeshData LoadGeom(
        const UsdPrim& prim,
        float                    metersPerUnit,
        const std::string& uvSetName)
    {
        MeshData geom;
        UsdGeomMesh mesh(prim);

        // positions
        VtArray<GfVec3f> usdPts;
        mesh.GetPointsAttr().Get(&usdPts);

        std::vector<float> ctrlPos;
        FlattenVecArray<GfVec3f>(usdPts, ctrlPos, metersPerUnit);

		// control mesh points
        VtArray<int> faceVertCounts, faceVertIndices;
        mesh.GetFaceVertexCountsAttr().Get(&faceVertCounts);
        mesh.GetFaceVertexIndicesAttr().Get(&faceVertIndices);

        // raw index list into control mesh points
        std::vector<uint32_t> rawIndices =
            TriangulateIndices(faceVertCounts, faceVertIndices);

		std::string primName = prim.GetName().GetString();

        // Reserve final arrays
        size_t cornerCount = rawIndices.size();
        geom.positions.reserve(cornerCount * 3);
        // normals/texcoords only if present:
        bool hasNormals = mesh.GetNormalsAttr().Get(
            &usdPts /* reuse var as scratch GfVec3f*/);

		// normals
        VtArray<GfVec3f> usdNormals;
        bool gotNormals = mesh.GetNormalsAttr().Get(&usdNormals);
        InterpolationType normInterp = gotNormals
            ? GetInterpolationType(mesh.GetNormalsInterpolation())
            : InterpolationType::Vertex;
        std::vector<float> rawNormals;
        if (gotNormals) {
            FlattenVecArray<GfVec3f>(usdNormals, rawNormals, /*scale*/1.0f);
            geom.flags |= VertexFlags::VERTEX_NORMALS;
        }

		// texcoords
        UsdAttribute tcAttr = prim.GetAttribute(TfToken("primvars:" + uvSetName));
        UsdGeomPrimvar uvPrim(tcAttr);
        VtArray<GfVec2f> usdTC;
        bool gotTC = (uvPrim && uvPrim.ComputeFlattened(&usdTC));
        InterpolationType tcInterp = gotTC
            ? GetInterpolationType(uvPrim.GetInterpolation())
            : InterpolationType::Vertex;
        std::vector<float> rawTC;
        if (gotTC) {
            VtIntArray stIndices;
            // flip Y for DX
            rawTC.reserve(usdTC.size() * 2);
            for (auto const& uv : usdTC) {
                rawTC.push_back(float(uv[0]));
                rawTC.push_back(1.0f - float(uv[1]));
            }
            geom.flags |= VertexFlags::VERTEX_TEXCOORDS;
        }

		// skinning
        UsdSkelBindingAPI bindAPI(prim);
        UsdSkelSkeleton    skel = bindAPI.GetInheritedSkeleton();
        std::vector<uint32_t> rawJoints;
        std::vector<float>    rawWeights;
        InterpolationType jointInterp = InterpolationType::Vertex,
            weightInterp = InterpolationType::Vertex;

        if (skel) {

            VtTokenArray    skelJoints;
            skel.GetJointsAttr().Get(&skelJoints);

            // Get any mesh-local joint order
            VtTokenArray    meshJoints;
            bindAPI.GetJointsAttr().Get(&meshJoints);

            // Construct the query, passing through all authored bindings
            UsdSkelSkinningQuery skinQ(
                prim,
                skelJoints,
                meshJoints,
                bindAPI.GetJointIndicesAttr(),
                bindAPI.GetJointWeightsAttr(),
                bindAPI.GetSkinningMethodAttr(),
                bindAPI.GetGeomBindTransformAttr(),
                bindAPI.GetJointsAttr(),
                bindAPI.GetBlendShapesAttr(),
                bindAPI.GetBlendShapeTargetsRel()
            );

            // compute per-point joint influences
            VtIntArray   jointIndices;
            VtFloatArray jointWeights;
            skinQ.ComputeVaryingJointInfluences(
                usdPts.size(), &jointIndices, &jointWeights);

            // flatten into rawJoints/rawWeights
            rawJoints.reserve(jointIndices.size());
            rawWeights.reserve(jointWeights.size());

            unsigned int influencesPerPoint = skinQ.GetNumInfluencesPerComponent();
            // reserve numPoints*slots
            unsigned short maxInfluencesPerJoint = 4;
            rawJoints.reserve(usdPts.size() * maxInfluencesPerJoint);
            rawWeights.reserve(usdPts.size() * maxInfluencesPerJoint);

            size_t cursor = 0;
            for (size_t pt = 0; pt < usdPts.size(); ++pt) {
                for (unsigned int slot = 0; slot < maxInfluencesPerJoint; ++slot) {
                    if (slot < influencesPerPoint) {
                        if (cursor < jointIndices.size()) {
                            rawJoints.push_back((uint32_t)jointIndices[cursor]);
                            rawWeights.push_back(jointWeights[cursor]);
                            ++cursor;
                        }
                        else {
                            rawJoints.push_back(0u);
                            rawWeights.push_back(0.0f);
                        }
                    }
                    else {
						rawJoints.push_back(0u); // padding
						rawWeights.push_back(0.0f); // padding
                    }
                }
            }

            // record interpolation tokens
            jointInterp = GetInterpolationType(
                bindAPI.GetJointIndicesPrimvar().GetInterpolation());
            weightInterp = GetInterpolationType(
                bindAPI.GetJointWeightsPrimvar().GetInterpolation());

            geom.flags |= VertexFlags::VERTEX_SKINNED;
        }

		// Flatten to "vertex" arrays
        size_t fvOffset = 0; // running sum of faceVertCounts
        uint32_t vtxCounter = 0;

        for (size_t f = 0; f < faceVertCounts.size(); ++f) {
            int fc = faceVertCounts[f];
            // fan-triangulate an n-gon: (0, i, i+1)
            for (int i = 1; i + 1 < fc; ++i) {
                int cornerIdxs[3] = { 0, i, i + 1 };
                for (int corner = 0; corner < 3; ++corner) {
                    // Which face vertex and which control point:
                    size_t fvIndex = fvOffset + cornerIdxs[corner];
                    uint32_t vertIdx = faceVertIndices[fvIndex];

                    // positions (always per-vertex)
                    EmitPrimvar(geom.positions,
                        ctrlPos,      // flattened [x,y,z]
                        3,            // numComponents
                        InterpolationType::Vertex,
                        f, fvIndex, vertIdx);

					// normals
                    if (gotNormals) {
                        EmitPrimvar(geom.normals,
                            rawNormals,   // flattened normals
                            3,
                            normInterp,   // from GetInterpolationType(...)
                            f, fvIndex, vertIdx);
                    }

					// texcoords
                    if (gotTC) {
                        EmitPrimvar(geom.texcoords,
                            rawTC, // flattened uvs
                            2,
                            tcInterp,
                            f, fvIndex, vertIdx);
                    }

					// joints/weights
                    if (!rawJoints.empty()) {
                        EmitPrimvar(geom.joints,
                            rawJoints,
                            4,
                            jointInterp,
                            f, fvIndex, vertIdx);

                        EmitPrimvar(geom.weights,
                            rawWeights,
                            4,
                            weightInterp,
                            f, fvIndex, vertIdx);
                    }

					// indices
                    geom.indices.push_back(vtxCounter++);
                }
            }

            fvOffset += fc;
        }

        return geom;
    }

    std::shared_ptr<Mesh> ProcessMesh(const UsdPrim& prim, const pxr::UsdStageRefPtr& stage, double metersPerUnit, GfRotation upRot, const std::string& directory, bool isUSDZ) {
        spdlog::info("Found Mesh: {}", prim.GetName().GetString());

        if (loadingCache.meshCache.contains(prim.GetPath().GetString())) {
            spdlog::info("Mesh {} already processed, skipping.", prim.GetPath().GetString());
            return loadingCache.meshCache[prim.GetPath().GetString()]; // Already processed
        }


        auto rels = prim.FindAllRelationshipTargetPaths();
        bool foundMaterial = false;
        std::string materialPath;
        // Try with material binding API
		auto bindingAPI = UsdShadeMaterialBindingAPI(prim);
		auto boundMaterial = bindingAPI.ComputeBoundMaterial();
		std::shared_ptr<Material> material;
        if (boundMaterial) {
            spdlog::info("Found Material via binding API: {}", boundMaterial.GetPrim().GetName().GetString());
            //bool success = ProcessMaterial(boundMaterial, stage, directory);

			ProcessMaterial(boundMaterial, stage, isUSDZ, directory);
            material = loadingCache.materialCache[boundMaterial.GetPrim().GetPath().GetString()];
            foundMaterial = true;
			materialPath = boundMaterial.GetPrim().GetPath().GetString();
		}

        if (!foundMaterial) {
            spdlog::warn("No material found for Mesh: {}", prim.GetName().GetString());
            material = Material::GetDefaultMaterial();
        }

        std::string uvName = loadingCache.uvSetCache[materialPath];

        auto geometry = LoadGeom(prim, metersPerUnit, uvName);
		geometry.material = material;
        
        auto meshPtr = MeshFromData(geometry, s2ws(prim.GetName().GetString()));
        loadingCache.meshCache[prim.GetPath().GetString()] = meshPtr;
        spdlog::info("Processed Mesh: {}", prim.GetName().GetString());
        return meshPtr;
    }

    std::shared_ptr<Skeleton> ProcessSkeleton(const UsdSkelSkeleton& skel, const UsdSkelCache& skelCache, const std::shared_ptr<Scene>& scene, float metersPerUnit) {
		if (loadingCache.skeletonMap.contains(skel.GetPrim().GetPath().GetString())) {
			spdlog::info("Skeleton {} already processed, skipping.", skel.GetPrim().GetPath().GetString());
			return loadingCache.skeletonMap[skel.GetPrim().GetPath().GetString()];
		}
        
        skelCache.Populate(UsdSkelRoot(skel.GetPrim()), UsdPrimDefaultPredicate);
		auto skelQuery = skelCache.GetSkelQuery(skel);

        auto jointOrder = skelQuery.GetJointOrder();
        const auto& topo = skelQuery.GetTopology();
        pxr::VtArray<pxr::GfMatrix4d> bindXforms;
        skel.GetBindTransformsAttr().Get(&bindXforms);

        std::vector<XMMATRIX>        invBindMats;
        std::vector<flecs::entity>   jointNodes;
        invBindMats.reserve(jointOrder.size());
        jointNodes.reserve(jointOrder.size());

        for (size_t i = 0; i < jointOrder.size(); ++i) {
            auto& m = bindXforms[i];
			// Convert GfMatrix4d to XMMATRIX

			// Extract translation and scale from the matrix
            auto transform = GfTransform(m);
			auto translation = transform.GetTranslation() * metersPerUnit;
			auto rotation = transform.GetRotation().GetQuaternion();
			auto& scale = transform.GetScale();
			
			// Create an XMMATRIX from the translation, rotation, and scale
            XMMATRIX xm = XMMatrixScaling(scale[0], scale[1], scale[2]) *
                XMMatrixRotationQuaternion(XMVectorSet(rotation.GetImaginary()[0], rotation.GetImaginary()[1], rotation.GetImaginary()[2], rotation.GetReal())) *
				XMMatrixTranslation(translation[0], translation[1], translation[2]);

            invBindMats.push_back(xm);

            // Lookup the node by name
            std::string jn = jointOrder[i].GetString();
            auto it = loadingCache.nodeMap.find(jn);
            if (it != loadingCache.nodeMap.end()) {
                throw std::runtime_error("Not implemented. Does the USD spec allow this?");
            }
            
            auto boneNode = scene->CreateNodeECS(s2ws(jn));
            if (!boneNode.has<AnimationController>()) {
                // Create a new AnimationController for this bone
                boneNode.add<AnimationController>();
                boneNode.set<Components::AnimationName>({ jn });
            }
            jointNodes.push_back(boneNode);
            auto parentIdx = topo.GetParent(i);
            if (parentIdx > -1) {
                boneNode.child_of(jointNodes[parentIdx]);
            }
        }

        auto skeleton = std::make_shared<Skeleton>(jointNodes, invBindMats);

        loadingCache.skeletonMap[skel.GetPrim().GetPath().GetString()] = skeleton;
		return skeleton;
    }

    std::shared_ptr<Animation> ProcessAnimQuery(const UsdSkelAnimQuery& animQuery, float metersPerUnit) {
        if (!animQuery) {
            return nullptr;
        }

        std::string animName = animQuery.GetPrim().GetName().GetString();
        if (loadingCache.animationMap.contains(animName)) {
            spdlog::info("Animation {} already processed, skipping.", animName);
            return loadingCache.animationMap[animName]; // Already processed
		}

        auto animation = std::make_shared<Animation>(animName);

        VtTokenArray jointOrder = animQuery.GetJointOrder();

        std::vector<double> times;
        if (!animQuery.GetJointTransformTimeSamples(&times)) {
            return animation;
        }

        for (double t : times) {
            UsdTimeCode timeCode(t);

            VtVec3fArray translations;
            VtQuatfArray rotations;
            VtVec3hArray scales;

            bool ok = animQuery.ComputeJointLocalTransformComponents(
                &translations, &rotations, &scales, timeCode);
            if (!ok) {
                continue;
            }

            for (size_t j = 0; j < jointOrder.size(); ++j) {
                const std::string nodeName = jointOrder[j].GetString();

                if (animation->nodesMap.find(nodeName) == animation->nodesMap.end()) {
                    animation->nodesMap[nodeName] = std::make_shared<AnimationClip>();
                }
                auto& clip = animation->nodesMap[nodeName];

                // position
                const GfVec3f& p = translations[j] * metersPerUnit;
                clip->addPositionKeyframe(static_cast<float>(t),
                    DirectX::XMFLOAT3(p[0], p[1], p[2]));

                // rotation
                const GfQuatf& q = rotations[j];
                const GfVec3f& i = q.GetImaginary();
                clip->addRotationKeyframe(static_cast<float>(t),
                    XMVectorSet(i[0], i[1], i[2], q.GetReal()));

                // scale
                const GfVec3h& s = scales[j];
                clip->addScaleKeyframe(static_cast<float>(t),
                    DirectX::XMFLOAT3(s[0], s[1], s[2]));
            }
        }

        return animation;
    }

    void BuildSkeletons(std::shared_ptr<Scene> scene,
        const pxr::UsdStageRefPtr& stage,
        const std::string& directory,
        const UsdSkelCache& skelCache,
        float metersPerUnit) {

        std::vector<UsdSkelRoot> skelRoots;
        for (auto prim : stage->Traverse()) {
            UsdSkelRoot root(prim);
            if (root) {        // Is this prim a SkelRoot?
                skelRoots.push_back(root);
            }
        }

        for (auto const& skelRoot : skelRoots) {
            spdlog::info("Found SkelRoot: {}", skelRoot.GetPrim().GetPath().GetString());
            skelCache.Populate(skelRoot, UsdPrimDefaultPredicate);
            std::vector<UsdSkelBinding> bindings;
            skelCache.ComputeSkelBindings(skelRoot, &bindings, UsdTraverseInstanceProxies());

            for (auto const& binding : bindings) {
                // Which skeleton?
                const UsdSkelSkeleton& skel = binding.GetSkeleton();
                std::cout << "Skeleton: " << skel.GetPrim().GetPath() << "\n";

				// Process the skeleton
				auto skeleton = ProcessSkeleton(skel, skelCache, scene, metersPerUnit);

                // Which prims does it drive?
                for (auto const& skinQuery : binding.GetSkinningTargets()) {
                    std::cout << "  drives: " << skinQuery.GetPrim().GetPath() << "\n";
                }

                UsdSkelBindingAPI skelAPI(skel.GetPrim());
                UsdPrim animPrim;
                if (skelAPI.GetAnimationSource(&animPrim)) {
                    spdlog::info("Found animation source for skeleton: {}", animPrim.GetPath().GetString());
                    UsdSkelAnimation anim(animPrim);
                    auto animQuery = skelCache.GetAnimQuery(anim);
                    if (animQuery) {
                        auto animation = ProcessAnimQuery(animQuery, metersPerUnit);
						skeleton->AddAnimation(animation);
                    }
                }
            }
		}

	}

    void ParseNodeHierarchy(std::shared_ptr<Scene> scene,
        const pxr::UsdStageRefPtr& stage,
        double metersPerUnit,
        GfRotation upRot,
        const std::string& directory,
		const UsdSkelCache& skelCache,
        bool isUSDZ) {

        std::function<void(const UsdPrim& prim,
            flecs::entity parent, bool hasCorrectedAxis)> RecurseHierarchy = [&](const UsdPrim& prim, flecs::entity parent, bool hasCorrectedAxis) {
                spdlog::info("Prim: {}", prim.GetName().GetString());

                GfVec3d translation = { 0, 0, 0 };
                GfQuaternion rot = GfQuaternion(1);
                GfVec3d scale = { 1, 1, 1 };
                // If this node has a transform, get it
                if (prim.IsA<UsdGeomXformable>()) {
                    UsdGeomXformable xform(prim);
                    UsdGeomXformable xformable(prim);
                    UsdGeomXformable::XformQuery query(xformable);

                    GfMatrix4d mat;
                    bool result = query.GetLocalTransformation(&mat, UsdTimeCode::Default());
					bool resets = query.GetResetXformStack(); // TODO: Handle reset xform stack

                    // Serialize mat
					std::string matStr;
                    for (int i = 0; i < 4; ++i) {
                        for (int j = 0; j < 4; ++j) {
                            matStr += std::to_string(mat[i][j]) + " ";
                        }
                        matStr += "\n";
					}

                    if (prim.GetName().GetString() == "Plane_036") {
						spdlog::info("plane036");
                    }
                    if (prim.GetName().GetString() == "Cylinder_011") {
                        spdlog::info("plane036");
                    }
					spdlog::info("Xformable has transform: {}", matStr);

                    if (!hasCorrectedAxis) { // Apply axis correction only on root transforms
                        GfMatrix4d rotMat(upRot, GfVec3d(0.0));
                        mat = rotMat * mat;
						hasCorrectedAxis = true;
                    }

                    // Decompose via GfTransform:
                    GfTransform xf(mat);
                    translation = xf.GetTranslation();
                    rot = xf.GetRotation().GetQuaternion();   // as a quaternion
                    scale = xf.GetScale();
                }
                std::vector<std::shared_ptr<Mesh>> meshes;
                if (prim.IsA<UsdGeomMesh>()) {
                    meshes.push_back(ProcessMesh(prim, stage, metersPerUnit, upRot, directory, isUSDZ));
                }

                std::vector<UsdPrim> childrenToRecurse;
                for (auto child : prim.GetAllChildren()) {
                    //if (child.IsA<UsdGeomMesh>() && !child.IsA<UsdGeomXformable>()) {
                    //    meshes.push_back(ProcessMesh(child, stage, metersPerUnit, upRot, directory, isUSDZ));
                    //}
                    //else {
                        childrenToRecurse.push_back(child);
                    //}
                }

                flecs::entity entity;
                if (meshes.size() > 0) {
                    entity = scene->CreateRenderableEntityECS(meshes, s2ws(prim.GetName().GetString()));
                }
                else {
                    entity = scene->CreateNodeECS(s2ws(prim.GetName().GetString()));
                }
                loadingCache.nodeMap[prim.GetPath().GetString()] = entity;
                
                entity.set<Components::Position>({ DirectX::XMFLOAT3(translation[0] * metersPerUnit, translation[1] * metersPerUnit, translation[2] * metersPerUnit) });
                entity.set<Components::Rotation>({ DirectX::XMFLOAT4(rot.GetImaginary()[0], rot.GetImaginary()[1], rot.GetImaginary()[2], rot.GetReal()) });
                entity.set<Components::Scale>({ DirectX::XMFLOAT3(scale[0], scale[1], scale[2]) });

                UsdSkelBindingAPI bindingAPI(prim);
                if (bindingAPI) {
                    UsdSkelSkeleton skel;
                    if (bindingAPI.GetSkeleton(&skel)) {
                        spdlog::info("Found skeleton on prim: {}", prim.GetName().GetString());
                        auto skeleton = ProcessSkeleton(skel, skelCache, scene, metersPerUnit);

                        UsdSkelBindingAPI skelAPI(skel.GetPrim());
                        UsdPrim animPrim;
                        if (skelAPI.GetAnimationSource(&animPrim)) {
                            spdlog::info("Found animation source for skeleton: {}", animPrim.GetPath().GetString());
                            UsdSkelAnimation anim(animPrim);
                            auto animQuery = skelCache.GetAnimQuery(anim);
                            if (animQuery) {
                                auto animation = ProcessAnimQuery(animQuery, metersPerUnit);
                                skeleton->AddAnimation(animation);
                                // TODO: Should sleletons be applied to all child entities? Or just to this one?
                                for (auto& mesh : meshes) {
                                    mesh->SetBaseSkin(skeleton);
                                }
                            }
                        }
                    }
                }

                if (parent) {
                    auto parentName = parent.name().c_str();
                    entity.child_of(parent);
                }
                else {
                    spdlog::warn("Node {} has no parent", entity.name().c_str());
                }

                for (auto& child : childrenToRecurse) {
                    if (child.IsA<UsdGeomXformable>()) {
                        RecurseHierarchy(child, entity, hasCorrectedAxis);
                    }
                }
            };

        RecurseHierarchy(stage->GetPseudoRoot(), flecs::entity(), false);

    }

    std::shared_ptr<Scene> LoadModel(std::string filePath) {

        UsdStageRefPtr stage = UsdStage::Open(filePath);

        auto& resolver = ArGetResolver();

        // Grab the context USD created for this stage:
        auto ctx = stage->GetPathResolverContext();

        // Bind it (in this thread) so Resolve() knows about local files:
        ArResolverContextBinder binder(ctx);

        spdlog::info("Context empty? {}", ctx.IsEmpty());
        spdlog::info("Context debug string: {}", ArGetDebugString(ctx));

        if (auto defCtx = ctx.Get<ArDefaultResolverContext>()) {
            for (auto& p : defCtx->GetSearchPath()) {
                spdlog::info("  search path: {}", p);
            }
        }

        for (auto& layer : stage->GetLayerStack()) {
            spdlog::info("Loaded layer: {}", layer->GetIdentifier());
        }

        auto metersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
        TfToken upAxis = UsdGeomGetStageUpAxis(stage); // TODO
        GfRotation upRot;  // identity by default
        if (upAxis == UsdGeomTokens->z) {
            // Rotate frame: Z-up -> Y-up = rotate -90 about X
            upRot = GfRotation(GfVec3d(1, 0, 0), -90.0);
        }
        else if (upAxis == UsdGeomTokens->y) {
            // Y-up is default, no rotation needed
            upRot = GfRotation(GfVec3d(0, 1, 0), 0);
        }
        else if (upAxis == UsdGeomTokens->x) {
            // X-up -> Z-up = rotate -90 about Y
            upRot = GfRotation(GfVec3d(0, 1, 0), -90.0);
        }
        else {
            spdlog::warn("Unknown Up Axis: {}", upAxis.GetString());
        }

        auto scene = std::make_shared<Scene>();

        UsdSkelCache skelCache;

		// Check if this is a USDZ file
		bool isUSDZ = false;
        if (std::filesystem::path(filePath).extension() == ".usdz") {
            isUSDZ = true;
        }

        ParseNodeHierarchy(scene, stage, metersPerUnit, upRot, std::filesystem::path(filePath).parent_path().string(), skelCache, isUSDZ);

		//BuildSkeletons(scene, stage, std::filesystem::path(filePath).parent_path().string(), skelCache);

        loadingCache.Clear();
        
        return scene;
    }

}