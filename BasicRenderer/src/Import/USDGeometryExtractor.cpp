#include "Import/USDGeometryExtractor.h"

#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <algorithm>

#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec2f.h>

#include <DirectXMath.h>
#include <spdlog/spdlog.h>

#include "Import/CLodCacheLoader.h"
#include "Mesh/ClusterLODTypes.h"
#include "Mesh/VertexFlags.h"

using namespace pxr;

namespace {

// Interpolation

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
	return InterpolationType::Vertex;
}

// Triangulation

static std::vector<uint32_t> TriangulateIndices(
	VtArray<int> const& faceVertCounts,
	VtArray<int> const& faceVertIndices)
{
	std::vector<uint32_t> out;
	out.reserve(faceVertIndices.size());
	size_t offset = 0;
	for (int fvCount : faceVertCounts) {
		if (fvCount == 3) {
			out.push_back(faceVertIndices[offset + 0]);
			out.push_back(faceVertIndices[offset + 1]);
			out.push_back(faceVertIndices[offset + 2]);
		}
		else {
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

template<typename GfVecN>
static void FlattenVecArray(
	VtArray<GfVecN> const& src,
	std::vector<float>& dst,
	float scale = 1.0f)
{
	constexpr size_t N = GfVecN::dimension;
	dst.clear();
	dst.reserve(src.size() * N);
	for (auto const& v : src) {
		for (size_t i = 0; i < N; ++i)
			dst.push_back(float(v[i] * scale));
	}
}

static std::vector<float> ComputeSmoothNormals(
	const std::vector<float>& ctrlPos,
	const VtArray<int>& faceVertCounts,
	const VtArray<int>& faceVertIndices,
	const std::vector<uint8_t>* useFaceMask)
{
	const size_t controlPointCount = ctrlPos.size() / 3;
	std::vector<GfVec3f> accumulated(controlPointCount, GfVec3f(0.0f));

	size_t fvOffset = 0;
	for (size_t faceIndex = 0; faceIndex < faceVertCounts.size(); ++faceIndex) {
		const int fvCount = faceVertCounts[faceIndex];
		if (fvCount < 3) {
			fvOffset += fvCount;
			continue;
		}

		if (useFaceMask && !useFaceMask->empty() && !(*useFaceMask)[faceIndex]) {
			fvOffset += fvCount;
			continue;
		}

		const uint32_t i0 = static_cast<uint32_t>(faceVertIndices[fvOffset]);
		if (i0 >= controlPointCount) {
			spdlog::warn("Invalid position index {} while computing smooth normals.", i0);
			fvOffset += fvCount;
			continue;
		}

		const GfVec3f p0(
			ctrlPos[i0 * 3 + 0],
			ctrlPos[i0 * 3 + 1],
			ctrlPos[i0 * 3 + 2]);

		for (int corner = 1; corner + 1 < fvCount; ++corner) {
			const uint32_t i1 = static_cast<uint32_t>(faceVertIndices[fvOffset + corner]);
			const uint32_t i2 = static_cast<uint32_t>(faceVertIndices[fvOffset + corner + 1]);

			if (i1 >= controlPointCount || i2 >= controlPointCount) {
				spdlog::warn("Invalid position index ({}, {}) while computing smooth normals.", i1, i2);
				continue;
			}

			const GfVec3f p1(
				ctrlPos[i1 * 3 + 0],
				ctrlPos[i1 * 3 + 1],
				ctrlPos[i1 * 3 + 2]);
			const GfVec3f p2(
				ctrlPos[i2 * 3 + 0],
				ctrlPos[i2 * 3 + 1],
				ctrlPos[i2 * 3 + 2]);

			const GfVec3f e1 = p1 - p0;
			const GfVec3f e2 = p2 - p0;
			const GfVec3f faceNormal = GfCross(e1, e2);

			if (GfDot(faceNormal, faceNormal) <= 1e-20f) {
				continue;
			}

			accumulated[i0] += faceNormal;
			accumulated[i1] += faceNormal;
			accumulated[i2] += faceNormal;
		}

		fvOffset += fvCount;
	}

	std::vector<float> normals;
	normals.resize(controlPointCount * 3, 0.0f);
	for (size_t i = 0; i < controlPointCount; ++i) {
		GfVec3f n = accumulated[i];
		const float len2 = GfDot(n, n);
		if (len2 > 1e-20f) {
			n *= (1.0f / std::sqrt(len2));
		}
		normals[i * 3 + 0] = n[0];
		normals[i * 3 + 1] = n[1];
		normals[i * 3 + 2] = n[2];
	}

	return normals;
}

static UsdTimeCode GetUsdGeometrySampleTime(const UsdStageRefPtr& stage)
{
	if (stage && stage->HasAuthoredTimeCodeRange()) {
		return UsdTimeCode(stage->GetStartTimeCode());
	}

	return UsdTimeCode::Default();
}

// LoadGeom
// Extracts raw vertex + index arrays from a UsdGeomMesh (optionally
// limited to a subset).  Handles face-varying expansion, optional
// normals, texture coordinates, and skinning vertex streams.

static void LoadGeom(
	std::unique_ptr<std::vector<std::byte>>& rawData,
	std::optional<std::unique_ptr<std::vector<std::byte>>>& skinningData,
	unsigned int& vertexSize,
	unsigned int& skinningVertexSize,
	std::vector<UINT32>& indices,
	unsigned int& vertexFlags,
    std::vector<MeshUvSetData>& uvSets,
	const UsdGeomMesh& mesh,
	const std::optional<UsdGeomSubset> subset,
	UsdTimeCode geomTimeCode,
	double metersPerUnit,
	const std::vector<std::string>& requiredUvSetNames,
	const std::optional<UsdSkelSkinningQuery>& skinQ,
	const VtTokenArray& skelJointOrderRaw,
	const VtTokenArray& skelJointOrderMapped)
{
	rawData = std::make_unique<std::vector<std::byte>>();
	skinningData.reset();
	indices.clear();
	vertexFlags = 0;
    uvSets.clear();
    (void)requiredUvSetNames;

	// If we have a skeleton, build joint mappings
	std::unordered_map<unsigned int, unsigned int> jointMapping;
	for (unsigned int i = 0; i < skelJointOrderMapped.size(); i++) {
		std::string jointName = skelJointOrderMapped[i].GetString();
		auto it = std::find_if(skelJointOrderRaw.begin(), skelJointOrderRaw.end(),
			[&jointName](const pxr::TfToken& token) {
				return token.GetString() == jointName;
			});
		if (it != skelJointOrderRaw.end()) {
			unsigned int rawIndex = static_cast<unsigned int>(std::distance(skelJointOrderRaw.begin(), it));
			jointMapping[i] = rawIndex;
		}
		else {
			spdlog::error("Joint {} not found in raw joint order.", jointName);
			throw std::runtime_error("Invalid joint name in mapped joint order");
		}
	}

	// positions
	VtArray<GfVec3f> usdPts;
	mesh.GetPointsAttr().Get(&usdPts, geomTimeCode);

	std::vector<float> ctrlPos;
	FlattenVecArray<GfVec3f>(usdPts, ctrlPos, static_cast<float>(metersPerUnit));

	// control mesh topology
	VtArray<int> faceVertCounts, faceVertIndices;
	mesh.GetFaceVertexCountsAttr().Get(&faceVertCounts, geomTimeCode);
	mesh.GetFaceVertexIndicesAttr().Get(&faceVertIndices, geomTimeCode);

	vertexSize = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3)
		+ sizeof(DirectX::XMFLOAT2);
	skinningVertexSize = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3)
		+ sizeof(DirectX::XMUINT4) + sizeof(DirectX::XMFLOAT4);

	// subset face mask
	std::vector<uint8_t> useFace(faceVertCounts.size(), 0);
	if (subset) {
		VtArray<int> subsetFaceIndices;
		subset->GetIndicesAttr().Get(&subsetFaceIndices);
		for (int fi : subsetFaceIndices)
			if (fi >= 0 && (size_t)fi < useFace.size()) useFace[fi] = 1;
	}

	std::string primName = mesh.GetPrim().GetName().GetString();

	// Count output corners
	size_t cornerCount = 0;
	for (size_t faceIndex = 0; faceIndex < faceVertCounts.size(); ++faceIndex) {
		if (subset && !useFace[faceIndex]) continue;
		const int fvCount = faceVertCounts[faceIndex];
		if (fvCount == 3)
			cornerCount += 3;
		else if (fvCount > 3)
			cornerCount += static_cast<size_t>(fvCount - 2) * 3;
	}

	if (cornerCount > 0 && ctrlPos.empty()) {
		spdlog::warn(
			"Mesh '{}' has topology at geometry sample time {} but no readable positions; skipping mesh.",
			primName,
			geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
		return;
	}

	// normals
	VtArray<GfVec3f> usdNormals;
	bool gotNormals = mesh.GetNormalsAttr().Get(&usdNormals, geomTimeCode);
	bool generatedSmoothNormals = false;

	VtIntArray nIdx;
	UsdAttribute nIdxAttr = mesh.GetPrim().GetAttribute(TfToken("normals:indices"));
	bool hasNIdx = nIdxAttr && nIdxAttr.Get(&nIdx, geomTimeCode);

	if (gotNormals && hasNIdx) {
		VtArray<GfVec3f> deindexed;
		deindexed.resize(nIdx.size());
		for (size_t i = 0; i < nIdx.size(); ++i) {
			int src = nIdx[i];
			if (src >= 0 && (size_t)src < usdNormals.size())
				deindexed[i] = usdNormals[src];
			else
				spdlog::warn("Invalid normal index {} in 'normals:indices' for prim '{}'", src, primName);
		}
		usdNormals.swap(deindexed);
	}

	std::vector<float> rawNormals;
	if (gotNormals) {
		FlattenVecArray<GfVec3f>(usdNormals, rawNormals, 1.0f);
		vertexFlags |= VertexFlags::VERTEX_NORMALS;
	}
	else {
		rawNormals = ComputeSmoothNormals(ctrlPos, faceVertCounts, faceVertIndices, subset ? &useFace : nullptr);
		if (!rawNormals.empty()) {
			gotNormals = true;
			generatedSmoothNormals = true;
			vertexFlags |= VertexFlags::VERTEX_NORMALS;
			spdlog::info("Generated smooth normals for mesh '{}' because no normals attribute was authored.", primName);
		}
	}

	InterpolationType normInterp = InterpolationType::Vertex;
	if (gotNormals && !generatedSmoothNormals)
		normInterp = GetInterpolationType(mesh.GetNormalsInterpolation());

    std::vector<std::string> uvSetNames;
    uvSetNames.push_back("st");
    {
        std::set<std::string> remainingNames;
        UsdGeomPrimvarsAPI primvarsAPI(mesh);
        for (const UsdGeomPrimvar& primvar : primvarsAPI.GetPrimvars()) {
            if (!primvar) {
                continue;
            }

            const auto typeName = primvar.GetTypeName();
            if (typeName != SdfValueTypeNames->TexCoord2fArray &&
                typeName != SdfValueTypeNames->Float2Array) {
                continue;
            }

            const std::string primvarName = primvar.GetPrimvarName().GetString();
            if (primvarName == "st") {
                continue;
            }

            remainingNames.insert(primvarName);
        }

        uvSetNames.insert(uvSetNames.end(), remainingNames.begin(), remainingNames.end());
    }

    struct UvSetBuildData {
        MeshUvSetData uvSet;
        bool available = false;
        InterpolationType interpolation = InterpolationType::Vertex;
        std::vector<float> rawData;
    };

    std::vector<UvSetBuildData> uvSetBuildData;
    uvSetBuildData.reserve(uvSetNames.size());
    for (const std::string& uvSetName : uvSetNames) {
        UvSetBuildData uvData;
        uvData.uvSet.name = uvSetName;

        UsdAttribute tcAttr = mesh.GetPrim().GetAttribute(TfToken("primvars:" + uvSetName));
        UsdGeomPrimvar uvPrim(tcAttr);
        VtArray<GfVec2f> usdTC;
        uvData.available = (uvPrim && uvPrim.ComputeFlattened(&usdTC, geomTimeCode));
        uvData.interpolation = uvData.available ? GetInterpolationType(uvPrim.GetInterpolation()) : InterpolationType::Vertex;
        if (uvData.available) {
            uvData.rawData.reserve(usdTC.size() * 2);
            for (auto const& uv : usdTC) {
                uvData.rawData.push_back(float(uv[0]));
                uvData.rawData.push_back(1.0f - float(uv[1]));
            }
        }

        uvSetBuildData.push_back(std::move(uvData));
    }

    vertexFlags |= VertexFlags::VERTEX_TEXCOORDS;

	// skinning
	UsdSkelBindingAPI bindAPI(mesh.GetPrim());
	UsdSkelSkeleton skel = bindAPI.GetInheritedSkeleton();
	std::vector<uint32_t> rawJoints;
	std::vector<float>    rawWeights;
	InterpolationType jointInterp = InterpolationType::Vertex,
		weightInterp = InterpolationType::Vertex;

	if (skinQ) {
		VtIntArray   jointIndices;
		VtFloatArray jointWeights;
		skinQ.value().ComputeVaryingJointInfluences(
			usdPts.size(), &jointIndices, &jointWeights);

		unsigned int influencesPerPoint = skinQ.value().GetNumInfluencesPerComponent();
		unsigned short maxInfluencesPerJoint = 4;
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
					rawJoints.push_back(0u);
					rawWeights.push_back(0.0f);
				}
			}
			if (maxInfluencesPerJoint < influencesPerPoint)
				cursor += influencesPerPoint - maxInfluencesPerJoint;
		}

		jointInterp = GetInterpolationType(
			bindAPI.GetJointIndicesPrimvar().GetInterpolation());
		weightInterp = GetInterpolationType(
			bindAPI.GetJointWeightsPrimvar().GetInterpolation());

		vertexFlags |= VertexFlags::VERTEX_SKINNED;
	}

	// allocate output buffers
	rawData->resize(cornerCount * vertexSize);
	indices.reserve(cornerCount);

	const bool hasSkinning = !rawJoints.empty();
	if (hasSkinning) {
		skinningData = std::make_unique<std::vector<std::byte>>(cornerCount * skinningVertexSize);
	}

	// tuple-copy helpers
	auto tupleBase = [](InterpolationType interp, size_t faceIndex,
		size_t fvIndex, uint32_t vertIndex, size_t numComponents) -> size_t
	{
		switch (interp) {
		case InterpolationType::Constant:   return 0;
		case InterpolationType::Uniform:    return faceIndex * numComponents;
		case InterpolationType::Vertex:
		case InterpolationType::Varying:    return static_cast<size_t>(vertIndex) * numComponents;
		case InterpolationType::FaceVarying:return fvIndex * numComponents;
		}
		return 0;
	};

	auto copyTupleFloat = [&](std::byte* dst, const std::vector<float>& raw,
		size_t numComponents, InterpolationType interp,
		size_t faceIndex, size_t fvIndex, uint32_t vertIndex,
		bool& warned, const char* attributeName)
	{
		const size_t base = tupleBase(interp, faceIndex, fvIndex, vertIndex, numComponents);
		if (base + numComponents > raw.size()) {
			std::memset(dst, 0, numComponents * sizeof(float));
			if (!warned) {
				spdlog::warn(
					"Mesh '{}' sampled '{}' tuple data out of range at geometry sample time {}; zero-filling missing values.",
					primName,
					attributeName,
					geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
				warned = true;
			}
			return;
		}
		std::memcpy(dst, raw.data() + base, numComponents * sizeof(float));
	};

	auto copyTupleUInt = [&](std::byte* dst, const std::vector<uint32_t>& raw,
		size_t numComponents, InterpolationType interp,
		size_t faceIndex, size_t fvIndex, uint32_t vertIndex,
		bool& warned, const char* attributeName)
	{
		const size_t base = tupleBase(interp, faceIndex, fvIndex, vertIndex, numComponents);
		if (base + numComponents > raw.size()) {
			std::memset(dst, 0, numComponents * sizeof(uint32_t));
			if (!warned) {
				spdlog::warn(
					"Mesh '{}' sampled '{}' tuple data out of range at geometry sample time {}; zero-filling missing values.",
					primName,
					attributeName,
					geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
				warned = true;
			}
			return;
		}
		std::memcpy(dst, raw.data() + base, numComponents * sizeof(uint32_t));
	};

	// emit vertices
	const DirectX::XMFLOAT3 defaultNormal{ 0.0f, 0.0f, 0.0f };
	bool warnedInvalidPositionIndex = false;
	bool warnedInvalidFaceVertexIndex = false;
	bool warnedNormalTupleRange = false;
	bool warnedUvTupleRange = false;
	bool warnedJointTupleRange = false;
	bool warnedWeightTupleRange = false;

	size_t fvOffset = 0;
	size_t outVertex = 0;
	for (size_t f = 0; f < faceVertCounts.size(); ++f) {
		int fc = faceVertCounts[f];
		if (!subset || useFace[f]) {
			for (int i = 1; i + 1 < fc; ++i) {
				int cornerIdxs[3] = { 0, i, i + 1 };
				uint32_t triVertIdxs[3] = {};
				bool validTriangle = true;
				for (int c = 0; c < 3; ++c) {
					const size_t fvIndex = fvOffset + static_cast<size_t>(cornerIdxs[c]);
					if (fvIndex >= faceVertIndices.size()) {
						if (!warnedInvalidFaceVertexIndex) {
							spdlog::warn(
								"Mesh '{}' has face-vertex index {} out of range for topology buffer size {}; skipping malformed triangle.",
								primName,
								fvIndex,
								faceVertIndices.size());
							warnedInvalidFaceVertexIndex = true;
						}
						validTriangle = false;
						break;
					}

					const uint32_t vertIdx = static_cast<uint32_t>(faceVertIndices[fvIndex]);
					if (static_cast<size_t>(vertIdx) >= ctrlPos.size() / 3) {
						if (!warnedInvalidPositionIndex) {
							spdlog::warn(
								"Mesh '{}' has position index {} out of range for {} control points at geometry sample time {}; skipping malformed triangle.",
								primName,
								vertIdx,
								ctrlPos.size() / 3,
								geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
							warnedInvalidPositionIndex = true;
						}
						validTriangle = false;
						break;
					}
					triVertIdxs[c] = vertIdx;
				}

				if (!validTriangle) {
					continue;
				}

				for (int c = 0; c < 3; ++c) {
					size_t fvIndex = fvOffset + cornerIdxs[c];
					uint32_t vertIdx = triVertIdxs[c];

					const size_t outBase = outVertex * vertexSize;
					std::byte* outPtr = rawData->data() + outBase;

					const size_t posBase = static_cast<size_t>(vertIdx) * 3;
					std::memcpy(outPtr, ctrlPos.data() + posBase, sizeof(DirectX::XMFLOAT3));

					size_t offset = sizeof(DirectX::XMFLOAT3);
					if (gotNormals)
						copyTupleFloat(outPtr + offset, rawNormals, 3, normInterp, f, fvIndex, vertIdx, warnedNormalTupleRange, "normals");
					else
						std::memcpy(outPtr + offset, &defaultNormal, sizeof(defaultNormal));
					offset += sizeof(DirectX::XMFLOAT3);

                    DirectX::XMFLOAT2 packedUv = { 0.0f, 0.0f };
                    if (!uvSetBuildData.empty() && uvSetBuildData[0].available) {
                        copyTupleFloat(reinterpret_cast<std::byte*>(&packedUv), uvSetBuildData[0].rawData, 2, uvSetBuildData[0].interpolation, f, fvIndex, vertIdx, warnedUvTupleRange, uvSetBuildData[0].uvSet.name.c_str());
                    }
                    std::memcpy(outPtr + offset, &packedUv, sizeof(packedUv));

					if (hasSkinning) {
						std::byte* skinPtr = skinningData.value()->data() + outVertex * skinningVertexSize;
						std::memcpy(skinPtr, outPtr, sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3));

						const size_t jointsOffset = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3);
						const size_t weightsOffset = jointsOffset + sizeof(DirectX::XMUINT4);
						copyTupleUInt(skinPtr + jointsOffset, rawJoints, 4, jointInterp, f, fvIndex, vertIdx, warnedJointTupleRange, "jointIndices");
						copyTupleFloat(skinPtr + weightsOffset, rawWeights, 4, weightInterp, f, fvIndex, vertIdx, warnedWeightTupleRange, "jointWeights");
					}

					indices.push_back(static_cast<UINT32>(outVertex));
					outVertex++;
                    for (size_t uvSetIndex = 0; uvSetIndex < uvSetBuildData.size(); ++uvSetIndex) {
                        DirectX::XMFLOAT2 uvValue = { 0.0f, 0.0f };
                        if (uvSetBuildData[uvSetIndex].available) {
                            copyTupleFloat(reinterpret_cast<std::byte*>(&uvValue), uvSetBuildData[uvSetIndex].rawData, 2, uvSetBuildData[uvSetIndex].interpolation, f, fvIndex, vertIdx, warnedUvTupleRange, uvSetBuildData[uvSetIndex].uvSet.name.c_str());
                        }
                        if (uvSets.size() <= uvSetIndex) {
                            uvSets.resize(uvSetIndex + 1u);
                            uvSets[uvSetIndex].name = uvSetBuildData[uvSetIndex].uvSet.name;
                        }
                        uvSets[uvSetIndex].values.push_back(uvValue);
                    }
				}
			}
		}
		fvOffset += fc;
	}

	rawData->resize(outVertex * static_cast<size_t>(vertexSize));
	if (hasSkinning) {
		skinningData.value()->resize(outVertex * static_cast<size_t>(skinningVertexSize));
	}
}

}

// Public API

namespace USDGeometryExtractor {

std::optional<UsdSkelSkinningQuery> GetSkinningQuery(
	const UsdGeomMesh& mesh,
	const UsdSkelCache& skelCache)
{
	UsdSkelBindingAPI bindAPI(mesh.GetPrim());
	UsdSkelSkeleton skel = bindAPI.GetInheritedSkeleton();
	if (!skel)
		return std::nullopt;

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

MeshPreprocessResult ExtractSubMesh(
	const UsdGeomMesh& mesh,
	const std::optional<UsdGeomSubset>& subset,
	const UsdStageRefPtr& stage,
	UsdTimeCode geomTimeCode,
	double metersPerUnit,
	const std::vector<std::string>& requiredUvSetNames,
	const std::optional<UsdSkelSkinningQuery>& skinQ,
	const VtTokenArray& skelJointOrderRaw,
	const VtTokenArray& skelJointOrderMapped)
{
	std::string subsetName = subset
		? subset->GetPrim().GetName().GetString()
		: std::string{};

	auto cacheIdentity = CLodCacheLoader::BuildIdentity(mesh, stage, subsetName);
	spdlog::info("    ExtractSubMesh: prim='{}' subset='{}' source='{}'",
		cacheIdentity.primPath, subsetName, cacheIdentity.sourceIdentifier);
	spdlog::info("    Geometry sample time for prim='{}' is {}",
		cacheIdentity.primPath,
		geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());

	auto prebuiltData = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
	if (prebuiltData.has_value())
		spdlog::info("    Cache HIT for prim='{}' subset='{}'", cacheIdentity.primPath, subsetName);
	else
		spdlog::info("    Cache MISS for prim='{}' subset='{}' — will build", cacheIdentity.primPath, subsetName);

	// Load raw geometry
	std::unique_ptr<std::vector<std::byte>> rawData;
	std::optional<std::unique_ptr<std::vector<std::byte>>> skinningData;
	unsigned int vertexSize = 0;
	unsigned int skinningVertexSize = 0;
	std::vector<UINT32> indices;
	unsigned int vertexFlags = 0;
    std::vector<MeshUvSetData> uvSets;
	LoadGeom(rawData, skinningData, vertexSize, skinningVertexSize,
		indices, vertexFlags, uvSets, mesh, subset, geomTimeCode, metersPerUnit, requiredUvSetNames,
		skinQ, skelJointOrderRaw, skelJointOrderMapped);

	const size_t loadedVertCount = rawData ? (rawData->size() / static_cast<size_t>(vertexSize > 0 ? vertexSize : 1)) : 0;
	spdlog::info("    LoadGeom done: {} verts, {} indices, vertexSize={}, flags=0x{:X}",
		loadedVertCount, indices.size(), vertexSize, vertexFlags);

	// Populate MeshIngestBuilder
	MeshIngestBuilder ingest(vertexSize,
		(skinningData && *skinningData) ? skinningVertexSize : 0,
		vertexFlags);
    ingest.SetUvSets(std::move(uvSets));

	const size_t vertexCount = rawData->size() / static_cast<size_t>(vertexSize);
	ingest.ReserveVertices(vertexCount);
	for (size_t v = 0; v < vertexCount; ++v) {
		const std::byte* vb = rawData->data() + v * static_cast<size_t>(vertexSize);
		ingest.AppendVertexBytes(vb, vertexSize);
	}

	if (skinningData && *skinningData) {
		const size_t skinVertCount = (*skinningData)->size() / static_cast<size_t>(skinningVertexSize);
		ingest.ReserveVertices(skinVertCount);
		for (size_t v = 0; v < skinVertCount; ++v) {
			const std::byte* sb = (*skinningData)->data() + v * static_cast<size_t>(skinningVertexSize);
			ingest.AppendSkinningVertexBytes(sb, skinningVertexSize);
		}
	}

	ingest.ReserveIndices(indices.size());
	ingest.AppendIndices(indices.data(), indices.size());

	// Build CLod cache if needed
	if (!prebuiltData.has_value()) {
		spdlog::info("    Building CLod artifacts...");
		ClusterLODPrebuildArtifacts artifacts = ingest.BuildClusterLODArtifacts();
		spdlog::info("    CLod artifacts built: {} groups, {} nodes",
			artifacts.prebuiltData.groups.size(), artifacts.prebuiltData.nodes.size());

		spdlog::info("    Saving cache to disk...");
		if (CLodCacheLoader::SavePrebuiltLocked(cacheIdentity, artifacts.prebuiltData,
			artifacts.cacheBuildData.AsPayload()))
		{
			spdlog::info("    Cache SAVED successfully.");
			auto diskBackedPrebuilt = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
			if (diskBackedPrebuilt.has_value())
				prebuiltData = std::move(diskBackedPrebuilt);
			else
				prebuiltData = std::move(artifacts.prebuiltData);
		}
		else {
			spdlog::warn("    Cache save FAILED — using in-memory artifacts only.");
			prebuiltData = std::move(artifacts.prebuiltData);
		}
	}

	return MeshPreprocessResult(
		std::move(ingest),
		std::move(cacheIdentity),
		std::move(prebuiltData));
}

StageExtractionResult ExtractAll(const std::string& filePath) {
	StageExtractionResult result;

	spdlog::info("  USD ExtractAll: opening stage '{}'", filePath);
	UsdStageRefPtr stage = UsdStage::Open(filePath);
	if (!stage) {
		spdlog::error("  USD stage open FAILED for '{}'", filePath);
		return result;
	}
	spdlog::info("  USD stage opened successfully.");

	double metersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
	spdlog::info("  metersPerUnit = {}", metersPerUnit);

	UsdSkelCache skelCache;

	size_t totalPrims = 0;
	for (auto const& prim : UsdPrimRange(stage->GetPseudoRoot()))
		++totalPrims;
	spdlog::info("  Stage has {} total prims.", totalPrims);

	for (auto const& prim : UsdPrimRange(stage->GetPseudoRoot())) {
		UsdGeomMesh mesh(prim);
		if (!mesh)
			continue;

		result.meshesProcessed++;
		spdlog::info("  Found mesh #{}: '{}'", result.meshesProcessed, prim.GetPath().GetString());

		// Attempt skinning query TODO: CLod skinning
		auto skinQ = GetSkinningQuery(mesh, skelCache);
		VtTokenArray skelJointOrderRaw, skelJointOrderMapped;

		if (skinQ) {
			UsdSkelBindingAPI bindAPI(mesh.GetPrim());
			UsdSkelSkeleton skel = bindAPI.GetInheritedSkeleton();
			if (skel) {
				skelCache.Populate(UsdSkelRoot(skel.GetPrim()), UsdPrimDefaultPredicate);
				auto skelQuery = skelCache.GetSkelQuery(skel);
				skelJointOrderRaw = skelQuery.GetJointOrder();

				auto& mapper = skinQ->GetJointMapper();
				if (mapper && !mapper->IsIdentity())
					mapper->Remap(skelJointOrderRaw, &skelJointOrderMapped);
				else
					skelJointOrderMapped = skelJointOrderRaw;
			}
		}

		// Determine subsets
		UsdShadeMaterialBindingAPI bindAPI(mesh);
		auto subsets = bindAPI.GetMaterialBindSubsets();
		spdlog::info("    {} material subset(s) for mesh '{}'",
			subsets.size(), prim.GetPath().GetString());

		// Default UV set for CLI
		const std::vector<std::string> requiredUvSetNames = { "st" };
		const UsdTimeCode geomTimeCode = GetUsdGeometrySampleTime(stage);

		if (subsets.empty()) {
			ExtractSubMesh(mesh, std::nullopt, stage, geomTimeCode, metersPerUnit,
				requiredUvSetNames, skinQ, skelJointOrderRaw, skelJointOrderMapped);
			result.submeshesProcessed++;
			result.cachesBuilt++;
		}
		else {
			for (auto const& subset : subsets) {
				ExtractSubMesh(mesh, std::make_optional(subset), stage, geomTimeCode, metersPerUnit,
					requiredUvSetNames, skinQ, skelJointOrderRaw, skelJointOrderMapped);
				result.submeshesProcessed++;
				result.cachesBuilt++;
			}
		}
	}

	return result;
}

} // namespace USDGeometryExtractor
