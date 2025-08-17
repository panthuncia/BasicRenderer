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
#include <pxr/usd/usdShade/utils.h>
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
		std::unordered_map<std::string, std::vector<std::shared_ptr<Mesh>>> meshCache;
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

	struct ResolvedProducer {
		pxr::UsdShadeShader shader;
		pxr::TfToken        outputName;
	};

	using ResolveCacheKey = std::pair<pxr::SdfPath, pxr::TfToken>;
	struct ResolveCacheKeyHash {
		size_t operator()(ResolveCacheKey const& k) const noexcept {
			return TfHash()(k.first) ^ TfHash()(k.second);
		}
	};

	inline std::optional<ResolvedProducer>
		ResolveToShaderOutput(pxr::UsdShadeConnectableAPI c,
			pxr::TfToken outName,
			std::unordered_map<ResolveCacheKey, ResolvedProducer, ResolveCacheKeyHash>* cache = nullptr)
	{
		ResolveCacheKey key{ c.GetPrim().GetPath(), outName };
		if (cache) {
			auto it = cache->find(key);
			if (it != cache->end()) return it->second;
		}

		if (c.GetPrim().IsA<pxr::UsdShadeShader>()) {
			ResolvedProducer r{ pxr::UsdShadeShader(c.GetPrim()), outName };
			if (cache) (*cache)[key] = r;
			return r;
		}

		if (c.GetPrim().IsA<pxr::UsdShadeNodeGraph>()) {
			pxr::UsdShadeNodeGraph ng(c.GetPrim());
			pxr::UsdShadeOutput ngOut = ng.GetOutput(outName);
			if (!ngOut) return std::nullopt;

			auto sources = ngOut.GetConnectedSources();
			if (sources.empty()) return std::nullopt;

			// Only support single source for now
			const auto& s = sources[0];
			auto next = ResolveToShaderOutput(
				pxr::UsdShadeConnectableAPI(s.source.GetPrim()),
				s.sourceName,
				cache);
			if (next && cache) (*cache)[key] = *next;
			return next;
		}

		return std::nullopt;
	}

	void ProcessUVReader(std::optional<ResolvedProducer>& r, const UsdShadeMaterial& material) {
		std::string varnameStr;
		UsdShadeInput varnameInput = r->shader.GetInput(TfToken("varname"));
		auto attrs = UsdShadeUtils::GetValueProducingAttributes(varnameInput);
		if (!attrs.empty()) {
			auto& attr = attrs[0];
			bool success = attr.Get< std::string >(&varnameStr);
			if (!success) {
				TfToken t;
				if (attr.Get<TfToken>(&t)) {
					varnameStr = t.GetString();
				}
				else {
					spdlog::warn("UsdPrimvarReader_float2 varname input is not a string or token: {}", attr.GetName().GetString());
				}
			}
		}

		//spdlog::info("Found UsdPrimvarReader_float2 node with varname: {}", varnameStr);
		auto& path = material.GetPrim().GetPath().GetString();
		if (loadingCache.uvSetCache.contains(path) && loadingCache.uvSetCache[path] != varnameStr) {
			throw std::runtime_error(
				"Multiple UV sets found for material, which is not supported yet. ");
		}
		loadingCache.uvSetCache[material.GetPrim().GetPath().GetString()] = varnameStr;
	}

	void ProcessTexture(MaterialDescription& result, const UsdShadeConnectionSourceInfo& src, const UsdStageRefPtr& stage, const TfToken& name, const UsdShadeMaterial& material) {
		if (auto srcShader = UsdShadeShader(src.source)) {
			TfToken srcId;
			srcShader.GetIdAttr().Get(&srcId);

			if (srcId == TfToken("UsdUVTexture")) {
				// load the texture and stash it
				SdfAssetPath asset;
				srcShader.GetInput(TfToken("file")).Get(&asset);
				// Resolve asset path
				std::string logicalPath = asset.GetResolvedPath();

				if (!loadingCache.textureCache.contains(logicalPath)) {
					spdlog::info("Found texture  {} in material", src.source.GetPrim().GetName().GetString());
					//auto texPath = asset.GetAssetPath();
					//spdlog::info("Loading texture from path: {}", texPath);

					UsdShadeInput csInput = srcShader.GetInput(TfToken("sourceColorSpace"));
					TfToken colorSpaceToken;
					std::string colorSpace = "linear";
					if (csInput && csInput.Get(&colorSpaceToken)) {
						colorSpace = colorSpaceToken.GetString();
					} // TODO: Use this to set texture color space instead of correcting in shader

					auto& resolver = ArGetResolver();
					auto ctx = stage->GetPathResolverContext();
					ArResolverContextBinder binder(ctx);

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

					loadingCache.textureCache[logicalPath] = tex;

				}
			}

			// Check if this shader has an "inputs:st" input
			UsdShadeInput stInput = srcShader.GetInput(TfToken("st"));
			if (stInput) {
				if (stInput.HasConnectedSource()) {
					std::unordered_map<ResolveCacheKey, ResolvedProducer, ResolveCacheKeyHash> cache;
					auto surfSources = stInput.GetConnectedSources();
					auto resolvedSurf = ResolveToShaderOutput(
						pxr::UsdShadeConnectableAPI(surfSources[0].source.GetPrim()),
						surfSources[0].sourceName,
						&cache);
					if (resolvedSurf) {
						ProcessUVReader(resolvedSurf, material);
					}
					else {
						spdlog::warn("Unable to resolve 'st' input for texture shader {}", src.source.GetPrim().GetName().GetString());
					}
				}
			}
			else {
				spdlog::warn("Shader {} does not have 'st' input for UVs", src.source.GetPrim().GetName().GetString());
			}

			// now map that texture into the correct material slot:
			SdfAssetPath asset;
			srcShader.GetInput(TfToken("file")).Get(&asset);
			// Resolve asset path
			std::string logicalPath = asset.GetResolvedPath();
			auto texIt = loadingCache.textureCache.find(logicalPath);
			if (texIt != loadingCache.textureCache.end()) {

				auto tex = texIt->second;
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
					result.negateNormals = tex->GetFileType() == ImageFiletype::DDS ? true : false;
					result.invertNormalGreen = false;
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

				if (name == TfToken("diffuseColor"))      result.baseColor.texture = tex;
				else if (name == TfToken("metallic"))     result.metallic.texture = tex;
				else if (name == TfToken("roughness"))    result.roughness.texture = tex;
				else if (name == TfToken("opacity"))      result.opacity.texture = tex;
				else if (name == TfToken("emissiveColor"))result.emissive.texture = tex;
				else if (name == TfToken("normal"))       result.normal.texture = tex;
				else if (name == TfToken("displacement")) result.heightMap.texture = tex;
				else if (name == TfToken("ambientOcclusion")) result.aoMap.texture = tex;
				else {
					spdlog::warn("Unknown texture input: {}", name.GetString());
				}
			}
		}
	}

	MaterialDescription ParseMaterialGraph(
		const pxr::UsdShadeMaterial& material,
		const std::string& directory,
		const UsdStageRefPtr& stage,
		bool isUSDZ)
	{
		MaterialDescription result;
		
		// Get terminal output
		pxr::UsdShadeOutput surfOut =
			material.GetSurfaceOutput(pxr::UsdShadeTokens->universalRenderContext);
		if (!surfOut) return result;

		// Find the bound surface shader
		auto surfSources = surfOut.GetConnectedSources();
		if (surfSources.empty()) return result;

		// Resolve the surface producer to a shader so we can enumerate its inputs
		std::unordered_map<ResolveCacheKey, ResolvedProducer, ResolveCacheKeyHash> cache;
		auto resolvedSurf = ResolveToShaderOutput(
			pxr::UsdShadeConnectableAPI(surfSources[0].source.GetPrim()),
			surfSources[0].sourceName,
			&cache);

		if (!resolvedSurf) return result;

		pxr::UsdShadeShader surfaceShader = resolvedSurf->shader;

		// Check it's UsdPreviewSurface, then parse inputs
		pxr::TfToken id;
		if (!surfaceShader.GetIdAttr().Get(&id) || id != pxr::TfToken("UsdPreviewSurface"))
			return result;

		result.name = material.GetPrim().GetName().GetString();
		result.invertNormalGreen = true; // TODO: What is the best way to deal with this?
		result.negateNormals = false;

		for (auto const& input : surfaceShader.GetInputs()) {
			const auto name = input.GetBaseName();

			// Read constants if unconnected
			if (input.GetConnectedSources().empty()) {
				TfToken name = input.GetBaseName();
				if (name == TfToken("diffuseColor") && input.GetConnectedSources().empty()) {
					GfVec3f c; input.Get(&c);
					result.diffuseColor = { c[0],c[1],c[2],1.0f };
				}
				else if (name == TfToken("metallic") && input.GetConnectedSources().empty()) {
					float v; input.Get(&v);
					result.metallic.factor = v;
				}
				else if (name == TfToken("roughness") && input.GetConnectedSources().empty()) {
					float v; input.Get(&v);
					result.roughness.factor = v;
				}
				else if (name == TfToken("opacity") && input.GetConnectedSources().empty()) {
					float v; input.Get(&v);
					result.opacity.factor = v;
				}
				else if (name == TfToken("emissiveColor") && input.GetConnectedSources().empty()) {
					GfVec3f c; input.Get(&c);
					result.emissiveColor = { c[0],c[1],c[2],1.0f };
				}
				else if (name == TfToken("opacityThreshold") && input.GetConnectedSources().empty()) {
					float v; input.Get(&v);
					result.alphaCutoff = v;
				}
				else {
					spdlog::warn("Unknown input '{}' with no connections in UsdPreviewSurface", name.GetString());
				}
				continue;
			}

			// For each connection, normalize to the real producer shader
			for (auto const& src : input.GetConnectedSources()) {

				auto r = ResolveToShaderOutput(
					pxr::UsdShadeConnectableAPI(src.source.GetPrim()),
					src.sourceName,
					&cache);

				if (!r) continue;

				pxr::TfToken prodId;
				r->shader.GetIdAttr().Get(&prodId);

				if (prodId == pxr::TfToken("UsdUVTexture")) {
					ProcessTexture(result, src, stage, name, material);
				}
				else if (prodId == pxr::TfToken("UsdPrimvarReader_float2")) {
					ProcessUVReader(r, material);
				}
				else {
					spdlog::warn("Unsupported shader producer: {} in material {}", prodId.GetString(), material.GetPrim().GetPath().GetString());
				}
			}
		}

		return result;
	}

	void ProcessMaterial(const pxr::UsdShadeMaterial& material, const pxr::UsdStageRefPtr& stage, bool isUSDZ, const std::string& directory) {
		if (loadingCache.materialCache.contains(material.GetPrim().GetPath().GetString())) {
			spdlog::info("Material {} already processed, skipping.", material.GetPrim().GetPath().GetString());
			return; // Already processed
		}

		spdlog::info("Processing material: {}", material.GetPrim().GetPath().GetString());

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

	std::optional<UsdSkelSkinningQuery>
		ExtractSkinningQuery(const UsdGeomMesh& mesh, const UsdSkelCache& skelCache)
	{
		UsdSkelBindingAPI bindAPI(mesh.GetPrim());
		UsdSkelSkeleton skel = bindAPI.GetInheritedSkeleton();
		if (!skel)
			return std::nullopt;

		// populate & build the query
		skelCache.Populate(UsdSkelRoot(skel.GetPrim()), UsdPrimDefaultPredicate);
		VtTokenArray skelJoints, meshJoints;
		skel.GetJointsAttr().Get(&skelJoints);
		bindAPI.GetJointsAttr().Get(&meshJoints);

		UsdSkelSkinningQuery skinQ(
			mesh.GetPrim(),
			skelJoints,
			meshJoints,
			bindAPI.GetJointIndicesAttr(),
			bindAPI.GetJointWeightsAttr(),
			bindAPI.GetSkinningMethodAttr(),
			bindAPI.GetGeomBindTransformAttr(),
			bindAPI.GetJointsAttr(),
			bindAPI.GetBlendShapesAttr(),
			bindAPI.GetBlendShapeTargetsRel());
		return skinQ;
	}

	void LoadGeom(
		MeshData& meshData,
		const UsdGeomMesh& mesh,
		const std::optional<UsdGeomSubset> subset,
		double metersPerUnit,
		const std::string& uvSetName,
		const std::optional<UsdSkelSkinningQuery>& skinQ,
		const VtTokenArray& skelJointOrderRaw,
		const VtTokenArray& skelJointOrderMapped)
	{

		// If we have a skeleton, build joint mappings

		std::unordered_map<unsigned int, unsigned int> jointMapping;
		// Build integer mapping from mapped to raw joint order
		for (unsigned int i = 0; i < skelJointOrderMapped.size(); i++) {
			std::string jointName = skelJointOrderMapped[i].GetString();

			// Find the joint in the raw order
			auto it = std::find_if(skelJointOrderRaw.begin(), skelJointOrderRaw.end(),
				[&jointName](const pxr::TfToken& token) {
					return token.GetString() == jointName;
				});
			// Get index
			if (it != skelJointOrderRaw.end()) {
				unsigned int rawIndex = static_cast<unsigned int>(std::distance(skelJointOrderRaw.begin(), it));
				jointMapping[i] = rawIndex;
			}
			else {
				spdlog::error("Joint {} not found in raw joint order.", jointName);
				throw std::runtime_error("Invalid joint name in mapped joint orrder");
			}
		}

		// positions
		VtArray<GfVec3f> usdPts;
		mesh.GetPointsAttr().Get(&usdPts);

		std::vector<float> ctrlPos;
		FlattenVecArray<GfVec3f>(usdPts, ctrlPos, static_cast<float>(metersPerUnit));

		// control mesh points
		VtArray<int> faceVertCounts, faceVertIndices;
		mesh.GetFaceVertexCountsAttr().Get(&faceVertCounts);
		mesh.GetFaceVertexIndicesAttr().Get(&faceVertIndices);

		// Apply subset if present
		std::vector<uint8_t> useFace(faceVertCounts.size(), 0);
		if (subset) {
			VtArray<int> subsetFaceIndices;
			subset->GetIndicesAttr().Get(&subsetFaceIndices);
			for (int fi : subsetFaceIndices)
				if (fi >= 0 && (size_t)fi < useFace.size()) useFace[fi] = 1;
		}

		// raw index list into control mesh points
		std::vector<uint32_t> rawIndices =
			TriangulateIndices(faceVertCounts, faceVertIndices);

		std::string primName = mesh.GetPrim().GetName().GetString();

		// Reserve final arrays
		size_t cornerCount = rawIndices.size();
		meshData.positions.reserve(cornerCount * 3);

		// normals

		VtArray<GfVec3f> usdNormals;
		bool gotNormals = mesh.GetNormalsAttr().Get(&usdNormals);

		// In case we have "normals:indices"
		VtIntArray nIdx;
		UsdAttribute nIdxAttr = mesh.GetPrim().GetAttribute(TfToken("normals:indices"));
		bool hasNIdx = nIdxAttr && nIdxAttr.Get(&nIdx);

		if (gotNormals && hasNIdx) {
			VtArray<GfVec3f> deindexed;
			deindexed.resize(nIdx.size());
			for (size_t i = 0; i < nIdx.size(); ++i) {
				int src = nIdx[i];
				if (src >= 0 && (size_t)src < usdNormals.size()) {
					deindexed[i] = usdNormals[src];
				}
				else {
					spdlog::warn("Invalid normal index {} in 'normals:indices' for prim '{}'", src, primName);
				}
			}
			usdNormals.swap(deindexed);
		}

		std::vector<float> rawNormals;
		if (gotNormals) {
			FlattenVecArray<GfVec3f>(usdNormals, rawNormals, 1.0f);
			meshData.flags |= VertexFlags::VERTEX_NORMALS;
		}

		InterpolationType normInterp = gotNormals
			? GetInterpolationType(mesh.GetNormalsInterpolation())
			: InterpolationType::Vertex;

		// texcoords
		UsdAttribute tcAttr = mesh.GetPrim().GetAttribute(TfToken("primvars:" + uvSetName));
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
			meshData.flags |= VertexFlags::VERTEX_TEXCOORDS;
		}

		// skinning
		UsdSkelBindingAPI bindAPI(mesh.GetPrim());
		UsdSkelSkeleton    skel = bindAPI.GetInheritedSkeleton();
		std::vector<uint32_t> rawJoints;
		std::vector<float>    rawWeights;
		InterpolationType jointInterp = InterpolationType::Vertex,
			weightInterp = InterpolationType::Vertex;

		if (skinQ) {

			// compute per-point joint influences
			VtIntArray   jointIndices;
			VtFloatArray jointWeights;
			skinQ.value().ComputeVaryingJointInfluences(
				usdPts.size(), &jointIndices, &jointWeights);

			// flatten into rawJoints/rawWeights
			rawJoints.reserve(jointIndices.size());
			rawWeights.reserve(jointWeights.size());

			unsigned int influencesPerPoint = skinQ.value().GetNumInfluencesPerComponent();
			// reserve numPoints*slots
			unsigned short maxInfluencesPerJoint = 4; // TODO: Support arbitrary numbers of influences
			rawJoints.reserve(usdPts.size() * maxInfluencesPerJoint);
			rawWeights.reserve(usdPts.size() * maxInfluencesPerJoint);

			size_t cursor = 0;
			for (size_t pt = 0; pt < usdPts.size(); ++pt) {
				for (unsigned int slot = 0; slot < maxInfluencesPerJoint; ++slot) {
					if (slot < influencesPerPoint) {
						if (cursor < jointIndices.size()) {
							rawJoints.push_back((uint32_t)jointMapping[jointIndices[cursor]]);
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
				// Jump ahead to match influencesPerPoint
				if (maxInfluencesPerJoint < influencesPerPoint) {
					cursor += influencesPerPoint - maxInfluencesPerJoint;
				}
			}

			// record interpolation tokens
			jointInterp = GetInterpolationType(
				bindAPI.GetJointIndicesPrimvar().GetInterpolation());
			weightInterp = GetInterpolationType(
				bindAPI.GetJointWeightsPrimvar().GetInterpolation());

			meshData.flags |= VertexFlags::VERTEX_SKINNED;
		}

		// Flatten to "vertex" arrays
		// Triangulate while PRESERVING original fv order
		size_t fvOffset = 0;
		uint32_t vtxCounter = 0;
		for (size_t f = 0; f < faceVertCounts.size(); ++f) {
			int fc = faceVertCounts[f];
			if (!subset || useFace[f]) {
				for (int i = 1; i + 1 < fc; ++i) {
					int cornerIdxs[3] = { 0, i, i + 1 };
					for (int c = 0; c < 3; ++c) {
						size_t fvIndex = fvOffset + cornerIdxs[c];
						uint32_t vertIdx = faceVertIndices[fvIndex];

						EmitPrimvar(meshData.positions, ctrlPos, 3, InterpolationType::Vertex, f, fvIndex, vertIdx);

						if (gotNormals) {
							EmitPrimvar(meshData.normals, rawNormals, 3, normInterp, f, fvIndex, vertIdx);
						}

						if (gotTC) {
							EmitPrimvar(meshData.texcoords, rawTC, 2,
								tcInterp,
								f, fvIndex, vertIdx);
						}

						if (!rawJoints.empty()) {
							EmitPrimvar(meshData.joints,
								rawJoints,
								4,
								jointInterp,
								f, fvIndex, vertIdx);

							EmitPrimvar(meshData.weights,
								rawWeights,
								4,
								weightInterp,
								f, fvIndex, vertIdx);
						}

						meshData.indices.push_back(vtxCounter++);
					}
				}
			}
			fvOffset += fc;
		}
	}

	std::string& uvSetFor(const UsdShadeMaterial& mat)
	{
		auto& materialPath = mat.GetPrim().GetPath().GetString();
		return loadingCache.uvSetCache[materialPath];

	}

	std::vector<std::shared_ptr<Mesh>> ProcessMesh(
		const UsdGeomMesh& mesh,
		const pxr::UsdStageRefPtr& stage,
		double metersPerUnit,
		GfRotation upRot,
		const std::string& directory,
		bool isUSDZ,
		const UsdSkelCache& skelCache,
		VtTokenArray& skelJointOrderRaw,
		VtTokenArray& skelJointOrderMapped)
	{
		auto& cacheKey = mesh.GetPrim().GetPath().GetString();
		if (loadingCache.meshCache.contains(cacheKey)) {
			return loadingCache.meshCache[cacheKey];
		}

		// Extract skin once
		auto skinQ = ExtractSkinningQuery(mesh, skelCache);

		// Gather subsets
		UsdShadeMaterialBindingAPI  bindAPI(mesh);
		auto                        subsets = bindAPI.GetMaterialBindSubsets();

		std::vector<std::shared_ptr<Mesh>> outMeshes;

		// If no subsets: one full mesh with ComputeBoundMaterial()
		if (subsets.empty()) {
			// find whichever material is on the mesh itself
			auto matAPI = UsdShadeMaterialBindingAPI(mesh);
			auto mat = matAPI.ComputeBoundMaterial();
			ProcessMaterial(mat, stage, isUSDZ, directory);
			MeshData geomData;
			LoadGeom(geomData, mesh, std::nullopt, metersPerUnit, uvSetFor(mat), skinQ, skelJointOrderRaw, skelJointOrderMapped);
			auto mtlPtr = loadingCache.materialCache.find(
				mat.GetPrim().GetPath().GetString());
			geomData.material = mtlPtr != loadingCache.materialCache.end()
				? mtlPtr->second
				: nullptr;
			auto mPtr = MeshFromData(
				geomData,
				s2ws(mesh.GetPrim().GetName().GetString()));
			outMeshes.push_back(mPtr);
		}
		else {
			// Otherwise: one mesh per subset
			for (auto const& subset : subsets) {
				// subset familyName=="materialBind", so:
				auto mat = UsdShadeMaterialBindingAPI(subset).ComputeBoundMaterial();
				ProcessMaterial(mat, stage, isUSDZ, directory);
				MeshData geomData;
				LoadGeom(
					geomData,
					mesh,
					subset,
					metersPerUnit,
					uvSetFor(mat),
					skinQ,
					skelJointOrderRaw,
					skelJointOrderMapped);

				auto mtlPtr = loadingCache.materialCache.find(
					mat.GetPrim().GetPath().GetString());
				geomData.material = mtlPtr != loadingCache.materialCache.end()
					? mtlPtr->second
					: nullptr;
				auto mPtr = MeshFromData(
					geomData,
					s2ws(mesh.GetPrim().GetName().GetString()) +
					L"_" + s2ws(subset.GetPrim().GetName().GetString()));
				outMeshes.push_back(mPtr);
			}
		}

		loadingCache.meshCache[cacheKey] = outMeshes;
		return outMeshes;
	}

	std::shared_ptr<Skeleton> ProcessSkeleton(const UsdSkelSkeleton& skel, const VtTokenArray rawJointOrder, const UsdSkelSkeletonQuery& skelQuery, const std::shared_ptr<Scene>& scene, double metersPerUnit) {
		if (loadingCache.skeletonMap.contains(skel.GetPrim().GetPath().GetString())) {
			spdlog::info("Skeleton {} already processed, skipping.", skel.GetPrim().GetPath().GetString());
			return loadingCache.skeletonMap[skel.GetPrim().GetPath().GetString()];
		}

		const auto& topo = skelQuery.GetTopology();
		pxr::VtArray<pxr::GfMatrix4d> bindXforms;
		skel.GetBindTransformsAttr().Get(&bindXforms);

		std::vector<XMMATRIX>        invBindMats;
		std::vector<flecs::entity>   jointNodes;
		invBindMats.reserve(rawJointOrder.size());
		jointNodes.reserve(rawJointOrder.size());

		for (size_t i = 0; i < rawJointOrder.size(); ++i) {
			auto& m = bindXforms[i];
			// Convert GfMatrix4d to XMMATRIX

			// Extract translation and scale from the matrix
			auto transform = GfTransform(m);
			auto translation = transform.GetTranslation() * metersPerUnit;
			auto rotation = transform.GetRotation().GetQuaternion();
			auto& scale = transform.GetScale();

			// Create an XMMATRIX from the translation, rotation, and scale
			XMMATRIX xm = XMMatrixScaling(static_cast<float>(scale[0]), static_cast<float>(scale[1]), static_cast<float>(scale[2])) *
				XMMatrixRotationQuaternion(XMVectorSet(static_cast<float>(rotation.GetImaginary()[0]), static_cast<float>(rotation.GetImaginary()[1]), static_cast<float>(rotation.GetImaginary()[2]), static_cast<float>(rotation.GetReal()))) *
				XMMatrixTranslation(static_cast<float>(translation[0]), static_cast<float>(translation[1]), static_cast<float>(translation[2]));
			xm = XMMatrixInverse(nullptr, xm); // Invert the matrix for the inverse bind pose

			invBindMats.push_back(xm);

			// Lookup the node by name
			std::string jn = rawJointOrder[i].GetString();
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

	std::shared_ptr<Animation> ProcessAnimQuery(const UsdSkelAnimQuery& animQuery, const UsdStageRefPtr& stage, double metersPerUnit, const VtTokenArray& jointOrder) {
		if (!animQuery) {
			return nullptr;
		}
		auto timeCodesPerSecond = stage->GetTimeCodesPerSecond();
		std::string animName = animQuery.GetPrim().GetName().GetString();
		if (loadingCache.animationMap.contains(animName)) {
			spdlog::info("Animation {} already processed, skipping.", animName);
			return loadingCache.animationMap[animName]; // Already processed
		}

		auto animation = std::make_shared<Animation>(animName);

		std::vector<double> times;
		if (!animQuery.GetJointTransformTimeSamples(&times)) {
			return animation;
		}

		for (double t : times) {
			float seconds = static_cast<float>(t / timeCodesPerSecond);
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
				clip->addPositionKeyframe(seconds,
					DirectX::XMFLOAT3(p[0], p[1], p[2]));

				// rotation
				const GfQuatf& q = rotations[j];
				const GfVec3f& i = q.GetImaginary();
				clip->addRotationKeyframe(seconds,
					XMVectorSet(i[0], i[1], i[2], q.GetReal()));

				// scale
				const GfVec3h& s = scales[j];
				clip->addScaleKeyframe(seconds,
					DirectX::XMFLOAT3(s[0], s[1], s[2]));
			}
		}

		return animation;
	}

	void ProcessMeshAndAnimations(
		const UsdPrim& prim,
		std::vector<std::shared_ptr<Mesh>>& meshes,
		UsdSkelCache& skelCache,
		const UsdStageRefPtr& stage,
		std::shared_ptr<Scene>& scene,
		double metersPerUnit,
		GfRotation upRot,
		const std::string& directory,
		bool isUSDZ) {

		UsdGeomMesh mesh(prim);
		if (!mesh) {
			return; // Not a mesh prim
		}

		auto skinningQuery = ExtractSkinningQuery(mesh, skelCache);

		UsdSkelBindingAPI bindingAPI(prim);
		std::shared_ptr<Skeleton> skeleton;
		VtTokenArray skelJointOrderRaw;
		VtTokenArray skelJointOrderMapped;

		if (bindingAPI) {
			UsdSkelSkeleton skel;
			if (bindingAPI.GetSkeleton(&skel)) {
				spdlog::info("Found skeleton on prim: {}", prim.GetName().GetString());
				skelCache.Populate(UsdSkelRoot(skel.GetPrim()), UsdPrimDefaultPredicate);
				auto skelQuery = skelCache.GetSkelQuery(skel);

				skelJointOrderRaw = skelQuery.GetJointOrder();

				if (!skinningQuery) {
					throw std::runtime_error(
						"Mesh is skinned but no skinning query found.");
				}
				auto& mapper = skinningQuery->GetJointMapper();
				if (mapper && !mapper->IsIdentity()) {
					// Map the joint order to the skinning query
					mapper->Remap(skelJointOrderRaw, &skelJointOrderMapped);
				}
				else {
					skelJointOrderMapped = skelJointOrderRaw;
				}

				spdlog::info("Original skeleton joint order:");
				for (const auto& joint : skelJointOrderRaw) {
					spdlog::info("  {}", joint.GetString());
				}
				spdlog::info("Mapped skeleton joint order:");
				for (const auto& joint : skelJointOrderMapped) {
					spdlog::info("  {}", joint.GetString());
				}

				skeleton = ProcessSkeleton(skel, skelJointOrderRaw, skelQuery, scene, metersPerUnit);

				UsdSkelBindingAPI skelAPI(skel.GetPrim());
				UsdPrim animPrim;
				if (skelAPI.GetAnimationSource(&animPrim)) {
					spdlog::info("Found animation source for skeleton: {}", animPrim.GetPath().GetString());
					UsdSkelAnimation anim(animPrim);
					auto animQuery = skelCache.GetAnimQuery(anim);

					if (animQuery) {
						auto animation = ProcessAnimQuery(animQuery, stage, metersPerUnit, skelJointOrderRaw);
						skeleton->AddAnimation(animation);
						// TODO: Should sleletons be applied to all child entities? Or just to this one?
					}
				}
			}
		}

		std::vector<std::shared_ptr<Mesh>> processedMesh = ProcessMesh(mesh, stage, metersPerUnit, upRot, directory, isUSDZ, skelCache, skelJointOrderRaw, skelJointOrderMapped);
		// Push back all meshes
		for (auto& m : processedMesh) {
			meshes.push_back(m);
		}

		if (skeleton) {
			for (auto& mesh : meshes) {
				mesh->SetBaseSkin(skeleton);
			}
		}

	}

	void ParseNodeHierarchy(std::shared_ptr<Scene> scene,
		const pxr::UsdStageRefPtr& stage,
		double metersPerUnit,
		GfRotation upRot,
		const std::string& directory,
		UsdSkelCache& skelCache,
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

					spdlog::info("Xformable has transform: {}", matStr);

					if (!hasCorrectedAxis) { // Apply axis correction only on root transforms
						GfMatrix4d rotMat(upRot, GfVec3d(0.0));
						mat = mat * rotMat;
						hasCorrectedAxis = true;
					}

					// Decompose via GfTransform:
					GfTransform xf(mat);
					translation = xf.GetTranslation();
					rot = xf.GetRotation().GetQuaternion();   // as a quaternion
					scale = xf.GetScale();
				}



				std::vector<UsdPrim> childrenToRecurse;
				for (auto child : prim.GetAllChildren()) {
					childrenToRecurse.push_back(child);
				}

				std::vector<std::shared_ptr<Mesh>> meshes;
				ProcessMeshAndAnimations(prim, meshes, skelCache, stage, scene, metersPerUnit, upRot, directory, isUSDZ);

				flecs::entity entity;
				if (meshes.size() > 0) {
					entity = scene->CreateRenderableEntityECS(meshes, s2ws(prim.GetName().GetString()));
				}
				else {
					entity = scene->CreateNodeECS(s2ws(prim.GetName().GetString()));
				}
				loadingCache.nodeMap[prim.GetPath().GetString()] = entity;

				entity.set<Components::Position>({ DirectX::XMFLOAT3(static_cast<float>(translation[0] * metersPerUnit), static_cast<float>(translation[1] * metersPerUnit), static_cast<float>(translation[2] * metersPerUnit)) });
				entity.set<Components::Rotation>({ DirectX::XMFLOAT4(static_cast<float>(rot.GetImaginary()[0]), static_cast<float>(rot.GetImaginary()[1]), static_cast<float>(rot.GetImaginary()[2]), static_cast<float>(rot.GetReal())) });
				entity.set<Components::Scale>({ DirectX::XMFLOAT3(static_cast<float>(scale[0]), static_cast<float>(scale[1]), static_cast<float>(scale[2])) });

				if (parent) {
					auto parentName = parent.name().c_str();
					entity.child_of(parent);
				}
				else {
					spdlog::warn("Node {} has no parent", entity.name().c_str());
				}

				for (auto& child : childrenToRecurse) {
					RecurseHierarchy(child, entity, hasCorrectedAxis);
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

		loadingCache.Clear();

		return scene;
	}

}