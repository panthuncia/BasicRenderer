#include <spdlog/spdlog.h>
#include <DirectXMath.h>
#include <filesystem>
#include <vector>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/transform.h>

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

namespace USDLoader {

    using namespace pxr;
	std::unordered_map<std::string, std::shared_ptr<Material>> materialCache;
	std::unordered_map<std::string, std::shared_ptr<Mesh>> meshCache;

    std::vector<std::shared_ptr<Material>>
        LoadMaterialsFromUsdStage(const pxr::UsdStageRefPtr& stage,
            const std::string& directory,
            bool sRGB)
    {
        using namespace pxr;
        std::vector<std::shared_ptr<Material>> materials;
        // Cache to avoid dup loads
        std::unordered_map<std::string, std::shared_ptr<Texture>> texCache;

        //stage->GetPseudoRoot().GetAllChildren();

        for (const UsdPrim& prim : stage->Traverse()) {
            spdlog::info("Primitive: {}", prim.GetTypeName().GetString());
			UsdAttributeVector attribs = prim.GetAttributes();
            for (auto& attrib : attribs) {
				spdlog::info("Attribute: {}", attrib.GetName().GetString());
            }
        }

        return materials;
    }

    bool ProcessMaterial(const pxr::UsdShadeMaterial& material,
        const pxr::UsdStageRefPtr& stage,
        const std::string& directory)
    {   

        if(materialCache.contains(material.GetPrim().GetPath().GetString())) {
            spdlog::info("Material {} already processed, skipping.", material.GetPrim().GetPath().GetString());
            return true; // Already processed
		}

        UsdShadeOutput mdlOut =
            material.GetOutput(pxr::TfToken("mdl:surface"));
        if (!mdlOut) {
            spdlog::warn("No mdl:surface output found on {}",
                material.GetPrim().GetName().GetString());
            return false;
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
            } else {
                spdlog::warn("Source is not a UsdShadeShader: {}",
                    shaderPrim.GetTypeName().GetString());
			}
        }

        if (!shader) {
            spdlog::warn("No shader found for material: {}",
				material.GetPrim().GetName().GetString());
        }

		bool enableEmission = false;
		bool enableOpacity = false;
        
		bool enableOpacityTexture = false;

		DirectX::XMFLOAT4 diffuseColor(1.f, 1.f, 1.f, 1.f);
		DirectX::XMFLOAT4 emissiveColor(0.f, 0.f, 0.f, 1.f);
		float metallicFactor = 0.f;
		float roughnessFactor = 0.5f;
		float opacityConstant = 1.f;
		float alphaCutoff = 0.5f;

		GfVec2f textureScale(1.f, 1.f);
		GfVec2f textureOffset(0.f, 0.f);
		float textureRotation = 0.f;

        auto inputs = shader.GetInputs();
        for (const auto& input : inputs) {
            spdlog::info("Input: {} {}", input.GetTypeName().GetCPPTypeName(), input.GetBaseName().GetString());
            if (input.GetTypeName() == pxr::SdfValueTypeNames->Asset) {
                pxr::SdfAssetPath assetPath;
                input.Get(&assetPath);
                spdlog::info("Asset Path: {}", assetPath.GetAssetPath());
            }
            else if (input.GetTypeName() == pxr::SdfValueTypeNames->Color3f) {
                GfVec3f color;
                input.Get(&color);
                if (input.GetBaseName() == pxr::TfToken("diffuse_color_constant")) {
                    diffuseColor = { color[0], color[1], color[2], 1.0f };
                }
                else if (input.GetBaseName() == pxr::TfToken("emissive_color")) {
                    emissiveColor = { color[0], color[1], color[2], 1.0f};
                }
            }
            else if (input.GetTypeName() == pxr::SdfValueTypeNames->Float) {
                float value;
                input.Get(&value);
                if (input.GetBaseName() == pxr::TfToken("metallic")) {
                    metallicFactor = value;
                }
                else if (input.GetBaseName() == pxr::TfToken("roughness")) {
                    roughnessFactor = value;
                }
                else if (input.GetBaseName() == pxr::TfToken("opacity_constant")) {
                    opacityConstant = value;
                    enableOpacity = true;
                }
            }
            else if (input.GetTypeName() == pxr::SdfValueTypeNames->Float2) {
                GfVec2f vec2f;
                input.Get(&vec2f);
                if (input.GetBaseName() == pxr::TfToken("texture_scale")) {
                    textureScale = vec2f;
                }
                else if (input.GetBaseName() == pxr::TfToken("texture_offset")) {
                    textureOffset = vec2f;
                }
            }
		}

        std::shared_ptr<Texture> baseColorTexture = nullptr;
        std::shared_ptr<Texture> normalTexture = nullptr;
        std::shared_ptr<Texture> metallicTex = nullptr;
        std::shared_ptr<Texture> roughnessTex = nullptr;
        std::shared_ptr<Texture> aoMap = nullptr;
        std::shared_ptr<Texture> emissiveTexture = nullptr;
        std::shared_ptr<Texture> heightMap = nullptr;
		
        uint32_t materialFlags = 0;
		uint32_t psoFlags = 0;

		BlendState blendMode = BlendState::BLEND_STATE_OPAQUE;

        auto newMaterial = std::make_shared<Material>(
            material.GetPrim().GetName().GetString(),
            materialFlags,
            psoFlags,
            baseColorTexture,
            normalTexture,
            aoMap,
            heightMap,
            metallicTex,
            roughnessTex,
            emissiveTexture,
            metallicFactor,
            roughnessFactor,
            diffuseColor,
            emissiveColor,
            blendMode,
            alphaCutoff
        );

		materialCache[material.GetPrim().GetPath().GetString()] = newMaterial;
		return true; // Successfully processed material
	}

    std::shared_ptr<Mesh> ProcessMesh(const UsdPrim& prim, const pxr::UsdStageRefPtr& stage, const std::string& directory) {
        spdlog::info("Found Mesh: {}", prim.GetName().GetString());

        if (meshCache.contains(prim.GetPath().GetString())) {
            spdlog::info("Mesh {} already processed, skipping.", prim.GetPath().GetString());
            return meshCache[prim.GetPath().GetString()]; // Already processed
		}

        MeshData geometry;

        auto rels = prim.FindAllRelationshipTargetPaths();
        bool foundMaterial = false;

        for (const auto& rel : rels) {
            spdlog::info("Relationship target: {}", rel.GetString());
            auto relPrim = stage->GetPrimAtPath(rel);
            if (relPrim.IsA<UsdShadeMaterial>()) {
                if (foundMaterial) {
                    spdlog::warn("Multiple materials found for Mesh: {}", prim.GetName().GetString());
                }
                spdlog::info("Found Material for Mesh: {}", prim.GetName().GetString());
                UsdShadeMaterial mat = UsdShadeMaterial(relPrim);
                bool success = ProcessMaterial(mat, stage, directory);
                if (success) {
                    geometry.material = materialCache[mat.GetPrim().GetPath().GetString()];
                }
            }
        }

        UsdGeomMesh mesh(prim);
        VtArray<GfVec3f> usdPositions;
        mesh.GetPointsAttr().Get(&usdPositions);
        geometry.positions.resize(usdPositions.size() * 3);
        memcpy(geometry.positions.data(), usdPositions.data(), usdPositions.size() * sizeof(GfVec3f));

        VtArray<GfVec3f> usdNormals;
        bool hasNormals = mesh.GetNormalsAttr().Get(&usdNormals);
        if (hasNormals) {
            geometry.normals.resize(usdNormals.size() * 3);
            memcpy(geometry.normals.data(), usdNormals.data(), usdNormals.size() * sizeof(GfVec3f));
            geometry.flags |= VertexFlags::VERTEX_NORMALS;
        }


        VtArray<int> faceVertexCounts, faceVertexIndices;
        mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
        mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);
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
        auto meshPtr = MeshFromData(geometry, s2ws(prim.GetName().GetString()));
		meshCache[prim.GetPath().GetString()] = meshPtr;
        spdlog::info("Processed Mesh: {}", prim.GetName().GetString());
		return meshPtr;
    }

    void ParseNodeHierarchy(std::shared_ptr<Scene> scene,
        const pxr::UsdStageRefPtr& stage,
        const std::string& directory) {
        
        std::function<void(const UsdPrim& prim, 
            flecs::entity parent)> RecurseHierarchy = [&](const UsdPrim& prim, flecs::entity parent) {
            //spdlog::info("Prim type: {}", prim.GetTypeName().GetString());

            GfVec3d translation = {0, 0, 0};
            GfQuaternion rot = GfQuaternion(1);
            GfVec3d scale = {1, 1, 1};
            // If this node has a transform, get it
            if (prim.IsA<UsdGeomXformable>()) {
				UsdGeomXformable xform(prim);
                UsdGeomXformable xformable(prim);
                UsdGeomXformable::XformQuery query(xformable);

                GfMatrix4d mat;
                bool resets = query.GetLocalTransformation(&mat, UsdTimeCode::Default());

                // Decompose via GfTransform:
                GfTransform xf(mat);
                translation = xf.GetTranslation();
                rot = xf.GetRotation().GetQuaternion();   // as a quaternion
                scale = xf.GetScale();
            }

            //if (prim.IsA<UsdShadeMaterial>()) {
            //    spdlog::info("Found Material: {}", prim.GetName().GetString());
                //auto material = UsdShadeMaterial(prim);
            //}

			std::vector<std::shared_ptr<Mesh>> meshes;
            for (auto child : prim.GetAllChildren()) {
                if (child.IsA<UsdGeomMesh>()) {
                    meshes.push_back(ProcessMesh(child, stage, directory));
                }
            }

            flecs::entity entity;
            if (meshes.size() > 0) {
                entity = scene->CreateRenderableEntityECS(meshes, s2ws(prim.GetName().GetString()));
            }
            else {
                entity = scene->CreateNodeECS(s2ws(prim.GetName().GetString()));
            }

			entity.set<Components::Position>({ DirectX::XMFLOAT3(translation[0], translation[1], translation[2]) });
			entity.set<Components::Rotation>({ DirectX::XMFLOAT4(rot.GetImaginary()[0], rot.GetImaginary()[1], rot.GetImaginary()[2], rot.GetReal()) });
			entity.set<Components::Scale>({ DirectX::XMFLOAT3(scale[0], scale[1], scale[2]) });

            if (parent) {
                auto parentName = parent.name().c_str();
                entity.child_of(parent);
            }
            else {
                spdlog::warn("Node {} has no parent", entity.name().c_str());
            }

            for (auto child : prim.GetAllChildren()) {
                    RecurseHierarchy(child, entity);
            }
            };

        RecurseHierarchy(stage->GetPseudoRoot(), flecs::entity());

    }

    std::shared_ptr<Scene> LoadModel(std::string filePath) {

        pxr::TfDebug::SetDebugSymbolsByName("USD_STAGE_OPEN", true);
        pxr::TfDebug::SetDebugSymbolsByName("PLUG_LOAD", true);
        pxr::TfDebug::SetDebugSymbolsByName("SDF_LAYER", true);
        pxr::TfDebug::SetOutputFile(stderr);

        UsdStageRefPtr stage = UsdStage::Open(filePath);

        auto scene = std::make_shared<Scene>();

		//LoadMaterialsFromUsdStage(stage, std::filesystem::path(filePath).parent_path().string(), true);
		ParseNodeHierarchy(scene, stage, std::filesystem::path(filePath).parent_path().string());

        materialCache.clear();
        meshCache.clear();

        return scene;
    }

}