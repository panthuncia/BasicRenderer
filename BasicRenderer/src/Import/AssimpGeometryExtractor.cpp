#include "Import/AssimpGeometryExtractor.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <DirectXMath.h>
#include <spdlog/spdlog.h>

#include "Import/CLodCacheLoader.h"
#include "Mesh/ClusterLODTypes.h"
#include "Mesh/VertexFlags.h"
#include "Utilities/CachePathUtilities.h"

namespace {

CLodCacheLoader::MeshCacheIdentity BuildAssimpCacheIdentity(
	const std::string& sourceFilePath,
	const aiMesh* mesh,
	unsigned int meshIndex)
{
	CLodCacheLoader::MeshCacheIdentity identity{};
	identity.sourceIdentifier = NormalizeCacheSourcePath(sourceFilePath);
	identity.primPath = "/Assimp/Mesh/" + std::to_string(meshIndex);
	if (mesh != nullptr && mesh->mName.length > 0) {
		identity.primPath += "/" + std::string(mesh->mName.C_Str());
	}
	identity.subsetName = "";
	return identity;
}

}

namespace AssimpGeometryExtractor {

ExtractionResult ExtractAll(const aiScene* pScene, const std::string& sourceFilePath) {
	ExtractionResult result;

	for (unsigned int i = 0; i < pScene->mNumMeshes; ++i) {
		aiMesh* aMesh = pScene->mMeshes[i];

		const bool hasBones = aMesh->HasBones();
		const bool hasNormals = aMesh->HasNormals();
		const bool hasTexcoords = aMesh->HasTextureCoords(0);

		const uint32_t numVertices = static_cast<uint32_t>(aMesh->mNumVertices);
		const unsigned int vertexSize = static_cast<unsigned int>(
			sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3) + (hasTexcoords ? sizeof(DirectX::XMFLOAT2) : 0));
		const unsigned int skinningVertexSize =
			sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMUINT4) + sizeof(DirectX::XMFLOAT4);

		// Pack vertex data
		std::vector<std::byte> rawData(static_cast<size_t>(numVertices) * vertexSize);
		std::vector<std::byte> skinningData;
		if (hasBones) {
			skinningData.resize(static_cast<size_t>(numVertices) * skinningVertexSize);
		}

		const DirectX::XMFLOAT3 defaultNormal{ 0.0f, 0.0f, 0.0f };
		for (uint32_t v = 0; v < numVertices; ++v) {
			const auto& pos = aMesh->mVertices[v];
			const DirectX::XMFLOAT3 position{ pos.x, pos.y, pos.z };
			const DirectX::XMFLOAT3 normal = hasNormals
				? DirectX::XMFLOAT3{ aMesh->mNormals[v].x, aMesh->mNormals[v].y, aMesh->mNormals[v].z }
				: defaultNormal;

			const size_t baseOffset = static_cast<size_t>(v) * vertexSize;
			std::memcpy(rawData.data() + baseOffset, &position, sizeof(position));
			size_t offset = sizeof(DirectX::XMFLOAT3);
			std::memcpy(rawData.data() + baseOffset + offset, &normal, sizeof(normal));
			offset += sizeof(DirectX::XMFLOAT3);

			if (hasTexcoords) {
				const DirectX::XMFLOAT2 texcoord{ aMesh->mTextureCoords[0][v].x, -aMesh->mTextureCoords[0][v].y };
				std::memcpy(rawData.data() + baseOffset + offset, &texcoord, sizeof(texcoord));
			}

			if (hasBones) {
				const size_t skinBaseOffset = static_cast<size_t>(v) * skinningVertexSize;
				std::memcpy(skinningData.data() + skinBaseOffset, &position, sizeof(position));
				size_t skinOffset = sizeof(DirectX::XMFLOAT3);
				std::memcpy(skinningData.data() + skinBaseOffset + skinOffset, &normal, sizeof(normal));
			}
		}

		// Pack index data
		std::vector<UINT32> indices;
		indices.reserve(aMesh->mNumFaces * 3);
		for (unsigned int f = 0; f < aMesh->mNumFaces; f++) {
			const aiFace& face = aMesh->mFaces[f];
			if (face.mNumIndices != 3) {
				throw std::runtime_error("Assimp mesh contains non-triangle face; expected triangulated input");
			}
			for (unsigned int idx = 0; idx < face.mNumIndices; idx++) {
				indices.push_back(face.mIndices[idx]);
			}
		}

		unsigned int meshFlags = 0;
		if (hasNormals) {
			meshFlags |= VertexFlags::VERTEX_NORMALS;
		}
		if (hasTexcoords) {
			meshFlags |= VertexFlags::VERTEX_TEXCOORDS;
		}

		if (hasBones) {
			meshFlags |= VertexFlags::VERTEX_SKINNED;

			std::vector<unsigned int> jointCount(aMesh->mNumVertices, 0);
			for (unsigned int b = 0; b < aMesh->mNumBones; b++) {
				aiBone* bone = aMesh->mBones[b];
				for (unsigned int w = 0; w < bone->mNumWeights; w++) {
					const aiVertexWeight& vw = bone->mWeights[w];
					unsigned int vertexID = vw.mVertexId;
					float weight = vw.mWeight;

					unsigned int& count = jointCount[vertexID];
					if (count < 4 && weight > 0.0f) {
						const size_t vertexBase = static_cast<size_t>(vertexID) * skinningVertexSize;
						const size_t jointsOffset = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT3);
						const size_t weightsOffset = jointsOffset + sizeof(DirectX::XMUINT4);

						auto* joints = reinterpret_cast<uint32_t*>(skinningData.data() + vertexBase + jointsOffset);
						auto* weights = reinterpret_cast<float*>(skinningData.data() + vertexBase + weightsOffset);
						joints[count] = b;
						weights[count] = weight;
						count++;
					}
					else if (weight > 0.0f) {
						throw std::runtime_error("Vertex has more than 4 non-zero bone influences");
					}
				}
			}
		}

		// CLod cache identity + try load
		auto cacheIdentity = BuildAssimpCacheIdentity(sourceFilePath, aMesh, i);
		auto prebuiltData = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);

		// Populate MeshIngestBuilder
		MeshIngestBuilder ingest(vertexSize, hasBones ? skinningVertexSize : 0, meshFlags);
		ingest.ReserveVertices(numVertices);
		if (hasBones) {
			ingest.ReserveVertices(numVertices);
		}
		for (uint32_t v = 0; v < numVertices; ++v) {
			const size_t baseOffset = static_cast<size_t>(v) * vertexSize;
			ingest.AppendVertexBytes(rawData.data() + baseOffset, vertexSize);
			if (hasBones) {
				const size_t skinBaseOffset = static_cast<size_t>(v) * skinningVertexSize;
				ingest.AppendSkinningVertexBytes(skinningData.data() + skinBaseOffset, skinningVertexSize);
			}
		}

		ingest.ReserveIndices(indices.size());
		ingest.AppendIndices(indices.data(), indices.size());

		// Build CLod cache if needed
		if (!prebuiltData.has_value()) {
			ClusterLODPrebuildArtifacts artifacts = ingest.BuildClusterLODArtifacts();

			auto loadedBeforeSave = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
			if (loadedBeforeSave.has_value()) {
				prebuiltData = std::move(loadedBeforeSave);
			}
			else if (CLodCacheLoader::SavePrebuiltLocked(cacheIdentity, artifacts.prebuiltData, artifacts.cacheBuildData.AsPayload())) {
				auto diskBackedPrebuilt = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
				if (diskBackedPrebuilt.has_value()) {
					prebuiltData = std::move(diskBackedPrebuilt);
				}
				else {
					prebuiltData = std::move(artifacts.prebuiltData);
				}
			}
			else {
				spdlog::warn("Failed to save CLOD cache for {} (mesh {})", sourceFilePath, i);
				prebuiltData = std::move(artifacts.prebuiltData);
			}
		}

		result.meshes.emplace_back(
			i,
			aMesh->mMaterialIndex,
			hasBones,
			MeshPreprocessResult(std::move(ingest), std::move(cacheIdentity), std::move(prebuiltData)));
	}

	return result;
}

ExtractionResult ExtractAll(const std::string& filePath) {
	Assimp::Importer importer;
	constexpr unsigned int kAssimpProcessFlags =
		aiProcess_Triangulate |
		aiProcess_FlipUVs |
		aiProcess_OptimizeGraph |
		aiProcess_OptimizeMeshes;
	const aiScene* pScene = importer.ReadFile(filePath, kAssimpProcessFlags);

	if (!pScene || pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pScene->mRootNode) {
		spdlog::error("Assimp loading failed for {}. Error: {}", filePath, importer.GetErrorString());
		return {};
	}

	return ExtractAll(pScene, filePath);
}

} // namespace AssimpGeometryExtractor
