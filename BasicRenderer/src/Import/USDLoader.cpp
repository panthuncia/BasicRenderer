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

    UsdShadeShader GetUSDShaderFromMaterial(const pxr::UsdShadeMaterial& material) {
		UsdShadeShader shader;
        UsdShadeOutput surfOut =
            material.GetSurfaceOutput(UsdShadeTokens->universalRenderContext);
        if (!surfOut) {
            spdlog::warn("No surface output on material {}",
                material.GetPrim().GetPath().GetText());
            return shader;
        }

            // 2) Resolve the connected shader for that output:
        TfToken       srcName;
        UsdShadeAttributeType srcType;
        shader =
            material.ComputeSurfaceSource(
                UsdShadeTokens->universalRenderContext,
                &srcName, &srcType);
        if (!shader) {
            spdlog::warn("No shader bound to {}'s surface",
                material.GetPrim().GetPath().GetText());
            return shader;
        }
        spdlog::info("Surface shader = {} (via {})",
            shader.GetPath().GetText(),
            srcName.GetText());

        // 3) Inspect its ID to see what node type it is:
        TfToken shaderId;
        shader.GetIdAttr().Get(&shaderId);
        spdlog::info("Shader info:id = {}", shaderId.GetText());

        return shader;
	}

    UsdShadeShader GetMdlShaderFromMaterial(const pxr::UsdShadeMaterial& material) {
        UsdShadeOutput mdlOut =
            material.GetOutput(pxr::TfToken("mdl:surface"));
        if (!mdlOut) {
            spdlog::warn("No mdl:surface output found on {}",
                material.GetPrim().GetName().GetString());
			return UsdShadeShader(); // Return an invalid shader
        }

        UsdShadeShader shader;
        auto sources = mdlOut.GetConnectedSources();
        if (!sources.empty()) {
            // sources[0].source is a UsdShadeConnectableAPI wrapping the shader prim
            spdlog::info("Found shader source: {} at {}", sources[0].source.GetPrim().GetName().GetString(), sources[0].source.GetPath().GetString());
            UsdShadeConnectableAPI srcAPI = sources[0].source;
            UsdPrim shaderPrim = srcAPI.GetPrim();
            if (shaderPrim.IsA<UsdShadeShader>()) {
                shader = UsdShadeShader(shaderPrim);
            }
            else {
                spdlog::warn("Source is not a UsdShadeShader: {}",
                    shaderPrim.GetTypeName().GetString());
            }
        }

        return shader;
	}

    UsdShadeShader GetShaderFromMaterial(const pxr::UsdShadeMaterial& material) {
        
		auto shader = GetUSDShaderFromMaterial(material);
        if (!shader) {
            shader = GetMdlShaderFromMaterial(material);
        }

		return shader;
	}

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

    std::shared_ptr<Mesh> ProcessMesh(const UsdPrim& prim, const pxr::UsdStageRefPtr& stage, double metersPerUnit, GfRotation upRot, const std::string& directory, bool isUSDZ) {
        spdlog::info("Found Mesh: {}", prim.GetName().GetString());

        if (loadingCache.meshCache.contains(prim.GetPath().GetString())) {
            spdlog::info("Mesh {} already processed, skipping.", prim.GetPath().GetString());
            return loadingCache.meshCache[prim.GetPath().GetString()]; // Already processed
        }

        MeshData geometry;

        auto rels = prim.FindAllRelationshipTargetPaths();
        bool foundMaterial = false;
        std::string materialPath;
        // Try with material binding API
		auto bindingAPI = UsdShadeMaterialBindingAPI(prim);
		auto boundMaterial = bindingAPI.ComputeBoundMaterial();
        if (boundMaterial) {
            spdlog::info("Found Material via binding API: {}", boundMaterial.GetPrim().GetName().GetString());
            //bool success = ProcessMaterial(boundMaterial, stage, directory);

			ProcessMaterial(boundMaterial, stage, isUSDZ, directory);
            geometry.material = loadingCache.materialCache[boundMaterial.GetPrim().GetPath().GetString()];
            foundMaterial = true;
			materialPath = boundMaterial.GetPrim().GetPath().GetString();
		}

        //for (const auto& rel : rels) {
        //    spdlog::info("Relationship target: {}", rel.GetString());
        //    auto relPrim = stage->GetPrimAtPath(rel);
        //    if (relPrim.IsA<UsdShadeMaterial>()) {
        //        if (foundMaterial) {
        //            spdlog::warn("Multiple materials found for Mesh: {}", prim.GetName().GetString());
        //        }
        //        spdlog::info("Found Material for Mesh: {}", prim.GetName().GetString());
        //        UsdShadeMaterial mat = UsdShadeMaterial(relPrim);
        //        ProcessMaterial(mat, stage, isUSDZ, directory);
        //        geometry.material = loadingCache.materialCache[mat.GetPrim().GetPath().GetString()];
        //    }
        //}

        if (!foundMaterial) {
            spdlog::warn("No material found for Mesh: {}", prim.GetName().GetString());
            geometry.material = Material::GetDefaultMaterial();
        }

        UsdGeomMesh mesh(prim);
        VtArray<GfVec3f> usdPositions;
        mesh.GetPointsAttr().Get(&usdPositions);
        geometry.positions.reserve(usdPositions.size() * 3);

        for (auto& p : usdPositions) {
            GfVec3d dpYup = p * metersPerUnit;
            geometry.positions.push_back(float(dpYup[0]));
            geometry.positions.push_back(float(dpYup[1]));
            geometry.positions.push_back(float(dpYup[2]));
        }

        VtArray<int> faceVertexCounts, faceVertexIndices;
        mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
        mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

        VtArray<GfVec3f> usdNormals;
        bool hasNormals = mesh.GetNormalsAttr().Get(&usdNormals);
        TfToken interp = mesh.GetNormalsInterpolation();

        if (hasNormals) {
            // Reserve worst-case size (faceVarying == one normal per index)
            geometry.normals.reserve(faceVertexIndices.size() * 3);

            if (interp == UsdGeomTokens->faceVarying) {
                // one normal per face-vertex -> match faceVertexIndices
                for (auto& n : usdNormals) {
                    n = n.GetNormalized();
                    geometry.normals.push_back((float)n[0]);
                    geometry.normals.push_back((float)n[1]);
                    geometry.normals.push_back((float)n[2]);
                }
            }
            else if (interp == UsdGeomTokens->uniform) {
                // one normal per face -> duplicate per corner
                size_t idxOff = 0;
                for (size_t f = 0; f < faceVertexCounts.size(); ++f) {
                    GfVec3f n = usdNormals[f].GetNormalized();
                    for (int corner = 0; corner < faceVertexCounts[f]; ++corner) {
                        geometry.normals.push_back((float)n[0]);
                        geometry.normals.push_back((float)n[1]);
                        geometry.normals.push_back((float)n[2]);
                    }
                    idxOff += faceVertexCounts[f];
                }
            }
            else if (interp == UsdGeomTokens->vertex ||
                interp == UsdGeomTokens->varying) {
                // one normal per point
                for (auto& n : usdNormals) {
                    n = n.GetNormalized();
                    geometry.normals.push_back((float)n[0]);
                    geometry.normals.push_back((float)n[1]);
                    geometry.normals.push_back((float)n[2]);
                }
            }
            else if (interp == UsdGeomTokens->constant) {
                // single normal -> replicate for every face-vertex
                GfVec3f n = usdNormals[0].GetNormalized();
                for (size_t i = 0; i < faceVertexIndices.size(); ++i) {
                    geometry.normals.push_back((float)n[0]);
                    geometry.normals.push_back((float)n[1]);
                    geometry.normals.push_back((float)n[2]);
                }
            }
            else {
                spdlog::warn("Unhandled normals interpolation: {}", interp.GetString());
            }

            geometry.flags |= VertexFlags::VERTEX_NORMALS;
        }
        else {
            // no authored normals -> treat as faceted: compute flat normals from indices
            geometry.normals.resize(faceVertexIndices.size() * 3);
        }

        VtArray<GfVec2f> usdTexcoords;
		bool hasTexcoords = mesh.GetPrim().GetAttribute(pxr::TfToken("primvars:"+loadingCache.uvSetCache[materialPath])).Get(&usdTexcoords);
        if (hasTexcoords) {
            geometry.texcoords.reserve(usdTexcoords.size() * 2);
            for (auto& tc : usdTexcoords) {
                geometry.texcoords.push_back(tc[0]);
                geometry.texcoords.push_back(tc[1]);
            }
            geometry.flags |= VertexFlags::VERTEX_TEXCOORDS;
		}

		// Joints and weights
        UsdSkelBindingAPI bindAPI(prim);

        // Get the skeleton's joint order
        UsdSkelSkeleton skel = bindAPI.GetInheritedSkeleton();
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

        size_t numPoints = usdPositions.size();

        VtIntArray indices;
        VtFloatArray weights;
		unsigned short maxInfluencesPerVertex = 4;
        if (skinQ.ComputeVaryingJointInfluences(numPoints, &indices, &weights)) {
			spdlog::info("Mesh {} has {} joints with varying influences", prim.GetName().GetString(), indices.size());
            int numJointsPerVertex = skinQ.GetNumInfluencesPerComponent();
            spdlog::info("Mesh {} has {} joints per vertex", prim.GetName().GetString(), numJointsPerVertex);
            if (numJointsPerVertex > maxInfluencesPerVertex) {
                spdlog::error("Mesh {} has more than 4 joints per vertex, clamping to 4", prim.GetName().GetString());
				throw std::runtime_error("Mesh has more than 4 joints per vertex, which is not supported.");
			}
			unsigned int currentJoint = 0;
            for (size_t i = 0; i < numPoints; ++i) {
                for (int j = 0; j < maxInfluencesPerVertex; ++j) {
                    //size_t idx = i * numJointsPerVertex + j;
                    if (j < numJointsPerVertex) {
                        if (currentJoint < indices.size()) {
                            geometry.joints.push_back(indices[currentJoint]);
                            geometry.weights.push_back(weights[currentJoint]);
                        }
                        else {
                            geometry.joints.push_back(0);
                            geometry.weights.push_back(0.0f);
                        }
                        currentJoint++;
                    }
                    else {
                        // TODO: Make # of influences per vertex dynamic
                        geometry.joints.push_back(0);
                        geometry.weights.push_back(0.0f);
                    }
                }
			}
            geometry.flags |= VertexFlags::VERTEX_SKINNED;
        }

        size_t indexOffset = 0;
        for (int faceVertCount : faceVertexCounts) {
            if (faceVertCount == 3) {
                // simple triangle
                geometry.indices.push_back(faceVertexIndices[indexOffset + 0]);
                geometry.indices.push_back(faceVertexIndices[indexOffset + 1]);
                geometry.indices.push_back(faceVertexIndices[indexOffset + 2]);
            }
            else if (faceVertCount == 4) {
                // quad -> two tris
                unsigned int i0 = faceVertexIndices[indexOffset + 0];
                unsigned int i1 = faceVertexIndices[indexOffset + 1];
                unsigned int i2 = faceVertexIndices[indexOffset + 2];
                unsigned int i3 = faceVertexIndices[indexOffset + 3];
                geometry.indices.insert(geometry.indices.end(), { i0,i1,i2, i0,i2,i3 });
            }
            else {
                // n-gon: fan-triangulate
                for (int v = 1; v + 1 < faceVertCount; ++v) {
                    geometry.indices.push_back(faceVertexIndices[indexOffset + 0]);
                    geometry.indices.push_back(faceVertexIndices[indexOffset + v]);
                    geometry.indices.push_back(faceVertexIndices[indexOffset + v + 1]);
                }
            }
            indexOffset += faceVertCount;
        }
		RebuildFaceVarying(geometry); // TODO: Make separate shader path for face-varying attributes. For now, we just duplicate the attributes
        auto meshPtr = MeshFromData(geometry, s2ws(prim.GetName().GetString()));
        loadingCache.meshCache[prim.GetPath().GetString()] = meshPtr;
        spdlog::info("Processed Mesh: {}", prim.GetName().GetString());
        return meshPtr;
    }

    std::shared_ptr<Skeleton> ProcessSkeleton(const UsdSkelSkeleton& skel, const UsdSkelCache& skelCache, const std::shared_ptr<Scene>& scene) {
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
            DirectX::XMMATRIX xm = DirectX::XMMATRIX(
                m[0][0], m[0][1], m[0][2], m[0][3],
                m[1][0], m[1][1], m[1][2], m[1][3],
                m[2][0], m[2][1], m[2][2], m[2][3],
				m[3][0], m[3][1], m[3][2], m[3][3]);

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

    std::shared_ptr<Animation> ProcessAnimQuery(const UsdSkelAnimQuery& animQuery) {
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
                const GfVec3f& p = translations[j];
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
        const UsdSkelCache& skelCache) {

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
				auto skeleton = ProcessSkeleton(skel, skelCache, scene);

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
                        auto animation = ProcessAnimQuery(animQuery);
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
                        auto skeleton = ProcessSkeleton(skel, skelCache, scene);

                        UsdSkelBindingAPI skelAPI(skel.GetPrim());
                        UsdPrim animPrim;
                        if (skelAPI.GetAnimationSource(&animPrim)) {
                            spdlog::info("Found animation source for skeleton: {}", animPrim.GetPath().GetString());
                            UsdSkelAnimation anim(animPrim);
                            auto animQuery = skelCache.GetAnimQuery(anim);
                            if (animQuery) {
                                auto animation = ProcessAnimQuery(animQuery);
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