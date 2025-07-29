#include <spdlog/spdlog.h>
#include <DirectXMath.h>
#include <filesystem>
#include <vector>

#include <pxr/usd/ar/resolver.h>
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
    std::unordered_map<std::string, std::shared_ptr<Material>> materialCache;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshCache;
	std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;
	std::unordered_map<std::string, std::string> uvSetCache;

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

    MaterialDescription ParseMaterialGraph(
        const pxr::UsdShadeMaterial& material,
        const std::string& directory)
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

                            if (srcId == TfToken("UsdUVTexture") && !textureCache.contains(src.source.GetPath().GetString())) {
                                // load the texture and stash it
                                SdfAssetPath asset;
                                srcShader.GetInput(TfToken("file")).Get(&asset);
								auto texPath = asset.GetAssetPath();
								spdlog::info("Loading texture from path: {}", texPath);
                                auto tex = LoadTextureFromFile(s2ws(directory) + L"/" + s2ws(asset.GetAssetPath()));
                                textureCache[src.source.GetPath().GetString()] = tex;
                            }

                            // now map that texture into the correct material slot:
                            auto texIt = textureCache.find(src.source.GetPath().GetString());
                            if (texIt != textureCache.end()) {
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
                TfToken varname;
                node.shader.GetInput(TfToken("varname")).Get(&varname);
                uvSetCache[node.path.GetString()] = varname.GetString();
            }
            // TODO: Other node types?
        }
		result.name = material.GetPrim().GetName().GetString();
        return result;
    }

    void ProcessMaterial(const pxr::UsdShadeMaterial& material, const pxr::UsdStageRefPtr& stage, const std::string& directory) {
        if (materialCache.contains(material.GetPrim().GetPath().GetString())) {
            spdlog::info("Material {} already processed, skipping.", material.GetPrim().GetPath().GetString());
            return; // Already processed
        }

        auto materialDesc = ParseMaterialGraph(material, directory);

        auto newMaterial = Material::CreateShared(materialDesc);

        materialCache[material.GetPrim().GetPath().GetString()] = newMaterial;
	}

    std::shared_ptr<Mesh> ProcessMesh(const UsdPrim& prim, const pxr::UsdStageRefPtr& stage, double metersPerUnit, GfRotation upRot, const std::string& directory) {
        spdlog::info("Found Mesh: {}", prim.GetName().GetString());

        if (meshCache.contains(prim.GetPath().GetString())) {
            spdlog::info("Mesh {} already processed, skipping.", prim.GetPath().GetString());
            return meshCache[prim.GetPath().GetString()]; // Already processed
        }

        MeshData geometry;

        auto rels = prim.FindAllRelationshipTargetPaths();
        bool foundMaterial = false;

        // Try with material binding API
		auto bindingAPI = UsdShadeMaterialBindingAPI(prim);
		auto boundMaterial = bindingAPI.ComputeBoundMaterial();
        if (boundMaterial) {
            spdlog::info("Found Material via binding API: {}", boundMaterial.GetPrim().GetName().GetString());
            //bool success = ProcessMaterial(boundMaterial, stage, directory);

			ProcessMaterial(boundMaterial, stage, directory);
            geometry.material = materialCache[boundMaterial.GetPrim().GetPath().GetString()];
            foundMaterial = true;
		}

        for (const auto& rel : rels) {
            spdlog::info("Relationship target: {}", rel.GetString());
            auto relPrim = stage->GetPrimAtPath(rel);
            if (relPrim.IsA<UsdShadeMaterial>()) {
                if (foundMaterial) {
                    spdlog::warn("Multiple materials found for Mesh: {}", prim.GetName().GetString());
                }
                spdlog::info("Found Material for Mesh: {}", prim.GetName().GetString());
                UsdShadeMaterial mat = UsdShadeMaterial(relPrim);
                ProcessMaterial(mat, stage, directory);
                geometry.material = materialCache[mat.GetPrim().GetPath().GetString()];
            }
        }

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
		bool hasTexcoords = mesh.GetPrim().GetAttribute(pxr::TfToken("primvars:st0")).Get(&usdTexcoords);
        if (hasTexcoords) {
            geometry.texcoords.reserve(usdTexcoords.size() * 2);
            for (auto& tc : usdTexcoords) {
                geometry.texcoords.push_back(tc[0]);
                geometry.texcoords.push_back(tc[1]);
            }
            geometry.flags |= VertexFlags::VERTEX_TEXCOORDS;
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
        meshCache[prim.GetPath().GetString()] = meshPtr;
        spdlog::info("Processed Mesh: {}", prim.GetName().GetString());
        return meshPtr;
    }

    void ParseNodeHierarchy(std::shared_ptr<Scene> scene,
        const pxr::UsdStageRefPtr& stage,
        double metersPerUnit,
        GfRotation upRot,
        const std::string& directory) {

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
                    meshes.push_back(ProcessMesh(prim, stage, metersPerUnit, upRot, directory));
                }

                std::vector<UsdPrim> childrenToRecurse;
                for (auto child : prim.GetAllChildren()) {
                    if (child.IsA<UsdGeomMesh>() && !child.IsA<UsdGeomXformable>()) {
                        meshes.push_back(ProcessMesh(child, stage, metersPerUnit, upRot, directory));
                    }
                    else {
                        childrenToRecurse.push_back(child);
                    }
                }

                flecs::entity entity;
                if (meshes.size() > 0) {
                    entity = scene->CreateRenderableEntityECS(meshes, s2ws(prim.GetName().GetString()));
                }
                else {
                    entity = scene->CreateNodeECS(s2ws(prim.GetName().GetString()));
                }

                entity.set<Components::Position>({ DirectX::XMFLOAT3(translation[0] * metersPerUnit, translation[1] * metersPerUnit, translation[2] * metersPerUnit) });
                entity.set<Components::Rotation>({ DirectX::XMFLOAT4(rot.GetImaginary()[0], rot.GetImaginary()[1], rot.GetImaginary()[2], rot.GetReal()) });
                entity.set<Components::Scale>({ DirectX::XMFLOAT3(scale[0], scale[1], scale[2]) });

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

        std::vector<std::string> plugs = { ws2s(GetExePath()+L"\\thirdPartyUSDPlugins\\httpResolver\\") };
        // Ensure paths exist
        for (auto& plugPath : plugs) {
            if (!std::filesystem::exists(plugPath)) {
                spdlog::error("Plugin path does not exist: {}", plugPath);
                return nullptr;
            }
        }
        PlugRegistry::GetInstance().RegisterPlugins(plugs);

        auto plug = PlugRegistry::GetInstance().GetPluginWithName("usd_httpResolver");

        //ArSetPreferredResolver("HttpResolver");

        pxr::TfDebug::SetDebugSymbolsByName("USD_STAGE_OPEN", true);
        pxr::TfDebug::SetDebugSymbolsByName("PLUG_LOAD", true);
        pxr::TfDebug::SetDebugSymbolsByName("SDF_LAYER", true);
        pxr::TfDebug::SetOutputFile(stderr);

        auto resolvers = ArGetAvailableResolvers();
        for (const auto& resolver : resolvers) {
            spdlog::info("Available Resolver: {}", resolver.GetTypeName());
        }

		std::set<pxr::TfType> primaryResolvers;
        PlugRegistry::GetAllDerivedTypes(
            pxr::TfType::Find<pxr::ArResolver>(), &primaryResolvers);
        for (const auto& resolverType : primaryResolvers) {
            spdlog::info("Primary Resolver: {}", resolverType.GetTypeName());
		}

        UsdStageRefPtr stage = UsdStage::Open(filePath);

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

        ParseNodeHierarchy(scene, stage, metersPerUnit, upRot, std::filesystem::path(filePath).parent_path().string());

        materialCache.clear();
        meshCache.clear();

        return scene;
    }

}