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
#include "Mesh/VertexLayout.h"
#include "Mesh/VertexFlags.h"

using namespace pxr;

namespace {

constexpr size_t kMaxSkinInfluences = 8u;

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

static std::vector<float> ComputeFacetedNormals(
	const std::vector<float>& ctrlPos,
	const VtArray<int>& faceVertCounts,
	const VtArray<int>& faceVertIndices,
	bool reverseMeshWinding)
{
	std::vector<float> normals(faceVertCounts.size() * 3, 0.0f);

	size_t fvOffset = 0;
	for (size_t faceIndex = 0; faceIndex < faceVertCounts.size(); ++faceIndex) {
		const int fvCount = faceVertCounts[faceIndex];
		if (fvCount < 3) {
			fvOffset += fvCount;
			continue;
		}

		const size_t i0Fv = fvOffset;
		const size_t i1Fv = fvOffset + (reverseMeshWinding ? 2u : 1u);
		const size_t i2Fv = fvOffset + (reverseMeshWinding ? 1u : 2u);

		if (i2Fv >= faceVertIndices.size()) {
			spdlog::warn("Invalid face-vertex topology while computing faceted normals for face {}.", faceIndex);
			fvOffset += fvCount;
			continue;
		}

		const uint32_t i0 = static_cast<uint32_t>(faceVertIndices[i0Fv]);
		const uint32_t i1 = static_cast<uint32_t>(faceVertIndices[i1Fv]);
		const uint32_t i2 = static_cast<uint32_t>(faceVertIndices[i2Fv]);
		const size_t controlPointCount = ctrlPos.size() / 3;
		if (i0 >= controlPointCount) {
			spdlog::warn("Invalid position index {} while computing faceted normals.", i0);
			fvOffset += fvCount;
			continue;
		}
		if (i1 >= controlPointCount || i2 >= controlPointCount) {
			spdlog::warn("Invalid position index ({}, {}) while computing faceted normals.", i1, i2);
			fvOffset += fvCount;
			continue;
		}

		const GfVec3f p0(
			ctrlPos[i0 * 3 + 0],
			ctrlPos[i0 * 3 + 1],
			ctrlPos[i0 * 3 + 2]);
		const GfVec3f p1(
			ctrlPos[i1 * 3 + 0],
			ctrlPos[i1 * 3 + 1],
			ctrlPos[i1 * 3 + 2]);
		const GfVec3f p2(
			ctrlPos[i2 * 3 + 0],
			ctrlPos[i2 * 3 + 1],
			ctrlPos[i2 * 3 + 2]);

		GfVec3f faceNormal = GfCross(p1 - p0, p2 - p0);
		const float len2 = GfDot(faceNormal, faceNormal);
		if (len2 > 1e-20f) {
			faceNormal *= (1.0f / std::sqrt(len2));
			normals[faceIndex * 3 + 0] = faceNormal[0];
			normals[faceIndex * 3 + 1] = faceNormal[1];
			normals[faceIndex * 3 + 2] = faceNormal[2];
		}

		fvOffset += fvCount;
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

static bool HasMultipleAuthoredTimeSamples(const UsdAttribute& attr)
{
	if (!attr) {
		return false;
	}

	std::vector<double> timeSamples;
	if (!attr.GetTimeSamples(&timeSamples)) {
		return false;
	}

	return timeSamples.size() > 1;
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

	TfToken orientation = UsdGeomTokens->rightHanded;
	mesh.GetOrientationAttr().Get(&orientation);
	const bool reverseMeshWinding = (orientation == UsdGeomTokens->leftHanded);

	TfToken subdivisionScheme = UsdGeomTokens->catmullClark;
	mesh.GetSubdivisionSchemeAttr().Get(&subdivisionScheme);
	const bool isPolygonalMesh = (subdivisionScheme == UsdGeomTokens->none);
	const bool previewSubdiv = !isPolygonalMesh;
	const bool previewTopology =
		HasMultipleAuthoredTimeSamples(mesh.GetFaceVertexCountsAttr()) ||
		HasMultipleAuthoredTimeSamples(mesh.GetFaceVertexIndicesAttr()) ||
		HasMultipleAuthoredTimeSamples(mesh.GetHoleIndicesAttr());

	skinningVertexSize = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3)
		+ sizeof(uint32_t) * kMaxSkinInfluences + sizeof(float) * kMaxSkinInfluences;

	// subset face mask
	std::vector<uint8_t> useFace(faceVertCounts.size(), 0);
	if (subset) {
		VtArray<int> subsetFaceIndices;
		subset->GetIndicesAttr().Get(&subsetFaceIndices);
		for (int fi : subsetFaceIndices)
			if (fi >= 0 && (size_t)fi < useFace.size()) useFace[fi] = 1;
	}

	std::string primName = mesh.GetPrim().GetName().GetString();
	UsdGeomPrimvarsAPI primvarsAPI(mesh);
	if (previewSubdiv) {
		spdlog::info(
			"Mesh '{}' uses subdivision scheme '{}' at sample time {}; rendering control cage as a static preview mesh.",
			primName,
			subdivisionScheme.GetString(),
			geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
	}
	if (previewTopology) {
		spdlog::info(
			"Mesh '{}' has time-varying topology or hole data; freezing extraction to sample time {} for static preview rendering.",
			primName,
			geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
	}

	VtArray<int> holeIndices;
	if (mesh.GetHoleIndicesAttr()) {
		mesh.GetHoleIndicesAttr().Get(&holeIndices, geomTimeCode);
	}

	std::vector<uint8_t> holedFaces(faceVertCounts.size(), 0);
	for (int holeFaceIndex : holeIndices) {
		if (holeFaceIndex >= 0 && static_cast<size_t>(holeFaceIndex) < holedFaces.size()) {
			holedFaces[holeFaceIndex] = 1;
		}
		else {
			spdlog::warn(
				"Mesh '{}' authored hole face index {} outside the face range {}; ignoring it for preview extraction.",
				primName,
				holeFaceIndex,
				faceVertCounts.size());
		}
	}

	// Count output corners
	size_t cornerCount = 0;
	for (size_t faceIndex = 0; faceIndex < faceVertCounts.size(); ++faceIndex) {
		if ((subset && !useFace[faceIndex]) || holedFaces[faceIndex]) continue;
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
	bool gotNormals = false;
	InterpolationType normInterp = InterpolationType::Vertex;
	std::vector<float> rawNormals;

	UsdGeomPrimvar normalPrimvar = primvarsAPI.GetPrimvar(TfToken("normals"));
	if (normalPrimvar) {
		VtArray<GfVec3f> usdPrimvarNormals;
		if (normalPrimvar.ComputeFlattened(&usdPrimvarNormals, geomTimeCode)) {
			FlattenVecArray<GfVec3f>(usdPrimvarNormals, rawNormals, 1.0f);
			normInterp = GetInterpolationType(normalPrimvar.GetInterpolation());
			gotNormals = true;
			vertexFlags |= VertexFlags::VERTEX_NORMALS;
		}
		else {
			spdlog::warn(
				"Mesh '{}' authored primvars:normals but it could not be flattened at geometry sample time {}; falling back to legacy normals attribute if present.",
				primName,
				geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
		}
	}

	if (!gotNormals) {
		VtArray<GfVec3f> usdNormals;
		if (mesh.GetNormalsAttr().Get(&usdNormals, geomTimeCode)) {
			FlattenVecArray<GfVec3f>(usdNormals, rawNormals, 1.0f);
			normInterp = GetInterpolationType(mesh.GetNormalsInterpolation());
			gotNormals = true;
			vertexFlags |= VertexFlags::VERTEX_NORMALS;
		}
	}

	if (!gotNormals && (isPolygonalMesh || previewSubdiv || previewTopology)) {
		rawNormals = ComputeFacetedNormals(ctrlPos, faceVertCounts, faceVertIndices, reverseMeshWinding);
		if (!rawNormals.empty()) {
			gotNormals = true;
			normInterp = InterpolationType::Uniform;
			vertexFlags |= VertexFlags::VERTEX_NORMALS;
			if (previewSubdiv) {
				spdlog::info("Generated faceted preview normals for subdivision control cage '{}'.", primName);
			}
			else if (previewTopology) {
				spdlog::info("Generated faceted preview normals for frozen topology mesh '{}'.", primName);
			}
			else {
				spdlog::info("Generated faceted normals for polygon mesh '{}' because no normals attribute was authored.", primName);
			}
		}
	}

	bool gotColors = false;
	InterpolationType colorInterp = InterpolationType::Vertex;
	std::vector<float> rawColors;
	UsdGeomPrimvar displayColorPrimvar = mesh.GetDisplayColorPrimvar();
	if (displayColorPrimvar) {
		VtArray<GfVec3f> usdColors;
		if (displayColorPrimvar.ComputeFlattened(&usdColors, geomTimeCode)) {
			FlattenVecArray<GfVec3f>(usdColors, rawColors, 1.0f);
			colorInterp = GetInterpolationType(displayColorPrimvar.GetInterpolation());
			gotColors = true;
			vertexFlags |= VertexFlags::VERTEX_COLORS;
		}
		else {
			spdlog::warn(
				"Mesh '{}' authored primvars:displayColor but it could not be flattened at geometry sample time {}; ignoring vertex colors for preview extraction.",
				primName,
				geomTimeCode.IsDefault() ? -1.0 : geomTimeCode.GetValue());
		}
	}

    std::vector<std::string> uvSetNames;
    for (const std::string& requiredUvSetName : requiredUvSetNames) {
        if (!requiredUvSetName.empty()) {
            uvSetNames.push_back(requiredUvSetName);
        }
    }
    if (std::find(uvSetNames.begin(), uvSetNames.end(), "st") == uvSetNames.end()) {
        uvSetNames.push_back("st");
    }
    {
        std::set<std::string> remainingNames;
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

        for (const std::string& remainingName : remainingNames) {
            if (std::find(uvSetNames.begin(), uvSetNames.end(), remainingName) == uvSetNames.end()) {
                uvSetNames.push_back(remainingName);
            }
        }
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

    size_t primaryUvSetIndex = uvSetBuildData.size();
    for (size_t uvSetIndex = 0; uvSetIndex < uvSetBuildData.size(); ++uvSetIndex) {
        if (uvSetBuildData[uvSetIndex].available) {
            primaryUvSetIndex = uvSetIndex;
            vertexFlags |= VertexFlags::VERTEX_TEXCOORDS;
            break;
        }
    }

	vertexSize = MeshVertexLayout::VertexSize(vertexFlags);

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
		unsigned short maxInfluencesPerJoint = static_cast<unsigned short>(kMaxSkinInfluences);
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
	bool warnedColorTupleRange = false;
	bool warnedJointTupleRange = false;
	bool warnedWeightTupleRange = false;

	size_t fvOffset = 0;
	size_t outVertex = 0;
	for (size_t f = 0; f < faceVertCounts.size(); ++f) {
		int fc = faceVertCounts[f];
		if ((!subset || useFace[f]) && !holedFaces[f]) {
			for (int i = 1; i + 1 < fc; ++i) {
				int cornerIdxs[3] = { 0, reverseMeshWinding ? (i + 1) : i, reverseMeshWinding ? i : (i + 1) };
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

					if (gotNormals)
						copyTupleFloat(outPtr + MeshVertexLayout::NormalOffset, rawNormals, 3, normInterp, f, fvIndex, vertIdx, warnedNormalTupleRange, "normals");
					else
						std::memcpy(outPtr + MeshVertexLayout::NormalOffset, &defaultNormal, sizeof(defaultNormal));

                    if (primaryUvSetIndex < uvSetBuildData.size()) {
                        DirectX::XMFLOAT2 packedUv = { 0.0f, 0.0f };
                        copyTupleFloat(reinterpret_cast<std::byte*>(&packedUv), uvSetBuildData[primaryUvSetIndex].rawData, 2, uvSetBuildData[primaryUvSetIndex].interpolation, f, fvIndex, vertIdx, warnedUvTupleRange, uvSetBuildData[primaryUvSetIndex].uvSet.name.c_str());
                        std::memcpy(outPtr + MeshVertexLayout::TexcoordOffset(vertexFlags), &packedUv, sizeof(packedUv));
                    }

					if (gotColors) {
						copyTupleFloat(outPtr + MeshVertexLayout::ColorOffset(vertexFlags), rawColors, 3, colorInterp, f, fvIndex, vertIdx, warnedColorTupleRange, "displayColor");
					}

					if (hasSkinning) {
						std::byte* skinPtr = skinningData.value()->data() + outVertex * skinningVertexSize;
						std::memcpy(skinPtr, outPtr, sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3));

						const size_t jointsOffset = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3);
						const size_t weightsOffset = jointsOffset + sizeof(uint32_t) * kMaxSkinInfluences;
						copyTupleUInt(skinPtr + jointsOffset, rawJoints, kMaxSkinInfluences, jointInterp, f, fvIndex, vertIdx, warnedJointTupleRange, "jointIndices");
						copyTupleFloat(skinPtr + weightsOffset, rawWeights, kMaxSkinInfluences, weightInterp, f, fvIndex, vertIdx, warnedWeightTupleRange, "jointWeights");
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

	TfToken subdivisionScheme = UsdGeomTokens->catmullClark;
	mesh.GetSubdivisionSchemeAttr().Get(&subdivisionScheme);
	const bool previewSubdiv = (subdivisionScheme != UsdGeomTokens->none);
	const bool previewTopology =
		HasMultipleAuthoredTimeSamples(mesh.GetFaceVertexCountsAttr()) ||
		HasMultipleAuthoredTimeSamples(mesh.GetFaceVertexIndicesAttr()) ||
		HasMultipleAuthoredTimeSamples(mesh.GetHoleIndicesAttr());

	return MeshPreprocessResult(
		std::move(ingest),
		std::move(cacheIdentity),
		std::move(prebuiltData),
		previewSubdiv || previewTopology);
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
