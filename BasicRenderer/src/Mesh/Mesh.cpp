#include "Mesh/Mesh.h"
#include "Mesh/ClusterLODUtilities.h"

#include <limits>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <bit>
#include <cmath>
#include <array>
#include <cstring>
#include <mutex>
#include <cassert>
#include <set>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Render/PSOFlags.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/Material.h"
#include "Mesh/VertexFlags.h"
#include "Managers/MeshManager.h"
#include "Animation/Skeleton.h"

#include "../shaders/Common/defines.h"

std::atomic<uint32_t> Mesh::globalMeshCount = 0;

namespace
{
	constexpr float kAnimatedBoundsPaddingScale = 1.0001f;
	constexpr float kAnimationBoundsMaxUniformSampleStep = 1.0f / 30.0f;
	constexpr uint32_t kAnimationBoundsMaxUniformSamples = 256u;

	float MaxAxisScale_RowVector(const DirectX::XMMATRIX& matrix)
	{
		using namespace DirectX;

		const float sx = XMVectorGetX(XMVector3Length(matrix.r[0]));
		const float sy = XMVectorGetX(XMVector3Length(matrix.r[1]));
		const float sz = XMVectorGetX(XMVector3Length(matrix.r[2]));
		return std::max(sx, std::max(sy, sz));
	}

	BoundingSphere TransformBoundingSphere(const BoundingSphere& sphere, const DirectX::XMMATRIX& matrix)
	{
		using namespace DirectX;

		BoundingSphere transformed = sphere;
		const XMVECTOR center = XMVectorSet(sphere.sphere.x, sphere.sphere.y, sphere.sphere.z, 1.0f);
		const XMVECTOR transformedCenter = XMVector3TransformCoord(center, matrix);
		XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&transformed.sphere), transformedCenter);
		transformed.sphere.w = sphere.sphere.w * MaxAxisScale_RowVector(matrix);
		return transformed;
	}

	BoundingSphere MergeBoundingSpheres(const BoundingSphere& lhs, const BoundingSphere& rhs)
	{
		using namespace DirectX;

		const XMVECTOR lhsCenter = XMVectorSet(lhs.sphere.x, lhs.sphere.y, lhs.sphere.z, 1.0f);
		const XMVECTOR rhsCenter = XMVectorSet(rhs.sphere.x, rhs.sphere.y, rhs.sphere.z, 1.0f);
		const float lhsRadius = lhs.sphere.w;
		const float rhsRadius = rhs.sphere.w;

		const XMVECTOR delta = XMVectorSubtract(rhsCenter, lhsCenter);
		const float dist = XMVectorGetX(XMVector3Length(delta));

		if (dist + rhsRadius <= lhsRadius) {
			return lhs;
		}
		if (dist + lhsRadius <= rhsRadius) {
			return rhs;
		}

		BoundingSphere merged{};
		const float newRadius = 0.5f * (dist + lhsRadius + rhsRadius);
		const float t = (dist > 1e-12f) ? ((newRadius - lhsRadius) / dist) : 0.0f;
		const XMVECTOR mergedCenter = XMVectorAdd(lhsCenter, XMVectorScale(delta, t));
		XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&merged.sphere), mergedCenter);
		merged.sphere.w = newRadius;
		return merged;
	}

	void AppendClipKeyframeTimes(const std::shared_ptr<AnimationClip>& clip, std::vector<float>& sampleTimes)
	{
		if (!clip) {
			return;
		}

		auto appendKeyframes = [&sampleTimes](const std::vector<Keyframe>& keyframes) {
			for (const auto& keyframe : keyframes) {
				sampleTimes.push_back(keyframe.time);
			}
		};

		appendKeyframes(clip->positionKeyframes);
		appendKeyframes(clip->rotationKeyframes);
		appendKeyframes(clip->scaleKeyframes);
	}

	std::vector<float> BuildAnimationSampleTimes(const Animation& animation)
	{
		std::vector<float> sampleTimes;
		sampleTimes.push_back(0.0f);

		float duration = 0.0f;
		for (const auto& [_, clip] : animation.nodesMap) {
			AppendClipKeyframeTimes(clip, sampleTimes);
			if (clip) {
				duration = std::max(duration, clip->duration);
			}
		}

		if (duration > 0.0f) {
			sampleTimes.push_back(duration);
			const uint32_t uniformSampleCount = std::min<uint32_t>(
				kAnimationBoundsMaxUniformSamples,
				std::max<uint32_t>(1u, static_cast<uint32_t>(std::ceil(duration / kAnimationBoundsMaxUniformSampleStep))));
			for (uint32_t sampleIndex = 1; sampleIndex < uniformSampleCount; ++sampleIndex) {
				sampleTimes.push_back((duration * static_cast<float>(sampleIndex)) / static_cast<float>(uniformSampleCount));
			}
		}

		std::sort(sampleTimes.begin(), sampleTimes.end());
		sampleTimes.erase(
			std::unique(sampleTimes.begin(), sampleTimes.end(), [](float lhs, float rhs) {
				return std::abs(lhs - rhs) <= 1e-4f;
			}),
			sampleTimes.end());

		if (sampleTimes.empty()) {
			sampleTimes.push_back(0.0f);
		}

		return sampleTimes;
	}

	BoundingSphere BuildObjectBoundingSphereFromRootNode(const std::vector<ClusterLODNode>& nodes, uint32_t rootNodeIndex)
	{
		BoundingSphere sphere{};
		if (rootNodeIndex >= nodes.size()) {
			return sphere;
		}

		const ClusterLODTraversalMetric& metric = nodes[rootNodeIndex].traversalMetric;
		sphere.sphere = metric.cullingSphere;
		return sphere;
	}

	ClusterLODRuntimeSummary BuildRuntimeSummary(
		const std::vector<ClusterLODGroup>& groups,
		const std::vector<ClusterLODGroupSegment>& segments,
		const std::vector<ClusterLODGroupChunk>& groupChunks)
	{
		ClusterLODRuntimeSummary summary{};
		summary.groupChunkHints.resize(groups.size());
		summary.parentGroupByLocal.assign(groups.size(), -1);
		summary.groupErrorByLocal.resize(groups.size(), 0.0f);
		summary.firstGroupVertexByLocal.resize(groups.size(), 0u);

		if (groups.empty()) {
			return summary;
		}

		int32_t coarsestDepth = groups[0].depth;
		for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
			const auto& group = groups[groupIndex];
			summary.firstGroupVertexByLocal[groupIndex] = group.firstGroupVertex;
			summary.groupErrorByLocal[groupIndex] = group.bounds.error;
			if (groupIndex < groupChunks.size()) {
				const auto& chunk = groupChunks[groupIndex];
				auto& hint = summary.groupChunkHints[groupIndex];
				hint.groupVertexCount = chunk.groupVertexCount;
				hint.meshletCount = chunk.meshletCount;
				hint.meshletTrianglesByteCount = chunk.meshletTrianglesByteCount;
				hint.segmentCount = group.segmentCount;
				hint.pageCount = group.pageCount;
			}
			else {
				auto& hint = summary.groupChunkHints[groupIndex];
				hint.groupVertexCount = group.groupVertexCount;
				hint.meshletCount = group.meshletCount;
				hint.segmentCount = group.segmentCount;
				hint.pageCount = group.pageCount;
			}
			coarsestDepth = std::max(coarsestDepth, group.depth);
		}

		uint32_t runStart = std::numeric_limits<uint32_t>::max();
		for (uint32_t groupLocalIndex = 0u; groupLocalIndex < static_cast<uint32_t>(groups.size()); ++groupLocalIndex) {
			const bool isCoarsest = groups[groupLocalIndex].depth == coarsestDepth;
			if (isCoarsest) {
				if (runStart == std::numeric_limits<uint32_t>::max()) {
					runStart = groupLocalIndex;
				}
				continue;
			}

			if (runStart != std::numeric_limits<uint32_t>::max()) {
				summary.coarsestRanges.push_back({ runStart, groupLocalIndex - runStart });
				runStart = std::numeric_limits<uint32_t>::max();
			}
		}

		if (runStart != std::numeric_limits<uint32_t>::max()) {
			summary.coarsestRanges.push_back({
				runStart,
				static_cast<uint32_t>(groups.size()) - runStart });
		}

		const uint32_t localGroupCount = static_cast<uint32_t>(groups.size());
		for (uint32_t groupLocalIndex = 0u; groupLocalIndex < localGroupCount; ++groupLocalIndex) {
			const auto& group = groups[groupLocalIndex];
			const uint32_t segBegin = group.firstSegment;
			const uint32_t segEnd = std::min<uint32_t>(segBegin + group.segmentCount, static_cast<uint32_t>(segments.size()));
			for (uint32_t segIndex = segBegin; segIndex < segEnd; ++segIndex) {
				const int32_t refinedGroupLocal = segments[segIndex].refinedGroup;
				if (refinedGroupLocal < 0) {
					continue;
				}

				const uint32_t refinedGroupLocalU32 = static_cast<uint32_t>(refinedGroupLocal);
				if (refinedGroupLocalU32 >= localGroupCount) {
					continue;
				}

				summary.parentGroupByLocal[refinedGroupLocalU32] = static_cast<int32_t>(groupLocalIndex);
			}
		}

		return summary;
	}
}

std::shared_ptr<Mesh> MeshIngestBuilder::Build(
	const std::shared_ptr<Material>& material,
	std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD,
	MeshCpuDataPolicy cpuDataPolicy) {
	auto vertices = std::make_unique<std::vector<std::byte>>(std::move(m_vertices));
	auto uvSets = std::move(m_uvSets);

	std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices;
	if (!m_skinningVertices.empty()) {
		skinningVertices = std::make_unique<std::vector<std::byte>>(std::move(m_skinningVertices));
	}

	return Mesh::CreateSharedFromIngest(
		std::move(vertices),
		m_vertexSize,
		std::move(skinningVertices),
		m_skinningVertexSize,
		std::move(m_indices),
		std::move(uvSets),
		material,
		m_flags,
		std::move(prebuiltClusterLOD),
		cpuDataPolicy);
}

Mesh::Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, std::vector<MeshUvSetData>&& uvSets, const std::shared_ptr<Material> material, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD, MeshCpuDataPolicy cpuDataPolicy, bool deferResourceCreation) {
	if (prebuiltClusterLOD.has_value()) {
		m_prebuiltClusterLOD = std::move(prebuiltClusterLOD);
	}
	m_uvSets = std::move(uvSets);
	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = 0; //static_cast<uint32_t>(m_vertices->size() / vertexSize);
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;

	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = 0; // static_cast<uint32_t>(m_vertices->size() / vertexSize);
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;
	m_perMeshBufferData.clodMeshletBufferOffset = 0;
	m_perMeshBufferData.clodMeshletVerticesBufferOffset = 0;
	m_perMeshBufferData.clodMeshletTrianglesBufferOffset = 0;
	m_perMeshBufferData.clodNumMeshlets = 0;

	m_skinningVertexSize = skinningVertexSize;
    this->material = material;

	if (!deferResourceCreation) {
		CreateBuffers(indices);

		m_globalMeshID = GetNextGlobalIndex();
	}
	else {
		m_globalMeshID = 0;
	}
}

void Mesh::ClearCLodCacheBuildChunkData(bool shrinkToFit)
{
	auto clearChunkStorage = [shrinkToFit](auto& chunks) {
		chunks.clear();
		if (shrinkToFit) {
			chunks.shrink_to_fit();
		}
	};

	clearChunkStorage(m_clodCacheBuildChunkData.groupPageBlobs);
}

void Mesh::ReleaseCLodChunkUploadData()
{
	ClearCLodCacheBuildChunkData(true);
}

void Mesh::ReleaseCLodHierarchyCpuData()
{
	m_clodGroups.clear();
	m_clodGroups.shrink_to_fit();
	m_clodSegments.clear();
	m_clodSegments.shrink_to_fit();
}

void Mesh::ReleaseCLodGroupChunkMetadataCpuData()
{
	m_clodGroupChunks.clear();
	m_clodGroupChunks.shrink_to_fit();
}

void Mesh::ApplyPrebuiltClusterLODData(const ClusterLODPrebuiltData& data)
{
	const bool hasDiskBackedStreamingSource = !data.groupDiskLocators.empty() && !data.cacheSource.containerFileName.empty();

	m_clodGroups = data.groups;
	m_clodSegments = data.segments;
	m_clodGroupChunks = data.groupChunks;
	if (hasDiskBackedStreamingSource && m_clodGroupChunks.size() != m_clodGroups.size()) {
		m_clodGroupChunks.assign(m_clodGroups.size(), {});
		for (size_t groupIndex = 0; groupIndex < m_clodGroups.size(); ++groupIndex) {
			m_clodGroupChunks[groupIndex].groupVertexCount = m_clodGroups[groupIndex].groupVertexCount;
			m_clodGroupChunks[groupIndex].meshletCount = m_clodGroups[groupIndex].meshletCount;
		}
	}
	m_clodRuntimeSummary = BuildRuntimeSummary(m_clodGroups, m_clodSegments, m_clodGroupChunks);
	ClearCLodCacheBuildChunkData(false);
	m_clodGroupDiskLocators = data.groupDiskLocators;
	m_clodCacheSource = data.cacheSource;
	m_clodNodes = data.nodes;
	m_clodTopRootNode = 0;
	m_perMeshBufferData.boundingSphere = data.objectBoundingSphere;
	m_voxelGroupMapping = data.voxelGroupMapping;

	m_clodMaxDepth = 0;
	for (const auto& group : m_clodGroups) {
		m_clodMaxDepth = std::max<uint32_t>(m_clodMaxDepth, static_cast<uint32_t>(std::max(group.depth, 0)));
	}
	m_clodLodLevelRoots.resize(m_clodMaxDepth + 1);
	for (uint32_t depth = 0; depth <= m_clodMaxDepth; ++depth) {
		m_clodLodLevelRoots[depth] = 1u + depth;
	}
	m_clodLodNodeRanges.clear();

	m_perMeshBufferData.numVertices = 0; // TODO: Remove, clod doesn't need it

	if (m_perMeshBufferData.boundingSphere.sphere.w <= 0.0f && !m_clodNodes.empty())
	{
		m_perMeshBufferData.boundingSphere = BuildObjectBoundingSphereFromRootNode(m_clodNodes, m_clodTopRootNode);
	}
}

void Mesh::AdoptCLodDiskStreamingMetadata(const ClusterLODPrebuiltData& data)
{
	if (data.groupDiskLocators.empty() || data.cacheSource.containerFileName.empty()) {
		return;
	}

	m_clodGroupDiskLocators = data.groupDiskLocators;
	m_clodCacheSource = data.cacheSource;

	if (m_clodGroupChunks.size() != m_clodGroups.size()) {
		m_clodGroupChunks.assign(m_clodGroups.size(), {});
		for (size_t groupIndex = 0; groupIndex < m_clodGroups.size(); ++groupIndex) {
			m_clodGroupChunks[groupIndex].groupVertexCount = m_clodGroups[groupIndex].groupVertexCount;
			m_clodGroupChunks[groupIndex].meshletCount = m_clodGroups[groupIndex].meshletCount;
		}
	}

	m_clodRuntimeSummary = BuildRuntimeSummary(m_clodGroups, m_clodSegments, m_clodGroupChunks);

	ClearCLodCacheBuildChunkData(false);
}

ClusterLODPrebuiltData Mesh::GetClusterLODPrebuiltData() const
{
	ClusterLODPrebuiltData out{};
	out.groups = m_clodGroups;
	out.segments = m_clodSegments;
	out.objectBoundingSphere = m_perMeshBufferData.boundingSphere;
	out.groupChunks = m_clodGroupChunks;
	out.groupDiskLocators = m_clodGroupDiskLocators;
	out.cacheSource = m_clodCacheSource;
	out.nodes = m_clodNodes;
	out.voxelGroupMapping = m_voxelGroupMapping;
	return out;
}

ClusterLODCacheBuildPayload Mesh::GetClusterLODCacheBuildPayload() const
{
	ClusterLODCacheBuildPayload payload{};
	payload.groupPageBlobs = &m_clodCacheBuildChunkData.groupPageBlobs;
	return payload;
}

ClusterLODCacheBuildOwnedData Mesh::GetClusterLODCacheBuildOwnedData() const
{
	ClusterLODCacheBuildOwnedData owned{};
	owned.groupPageBlobs = m_clodCacheBuildChunkData.groupPageBlobs;
	return owned;
}

void Mesh::CreateBuffers(const std::vector<UINT32>& indices) {
	if (m_prebuiltClusterLOD.has_value()) {
		ApplyPrebuiltClusterLODData(m_prebuiltClusterLOD.value());
		m_prebuiltClusterLOD.reset();
	} else {
		throw std::runtime_error("Mesh was created without prebuilt ClusterLOD data");
	}
}

int Mesh::GetNextGlobalIndex() {
    return globalMeshCount.fetch_add(1, std::memory_order_relaxed);
}

uint64_t Mesh::GetGlobalID() const {
	return m_globalMeshID;
}

void Mesh::SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_preSkinningVertexBufferView = std::move(view);
	if (m_preSkinningVertexBufferView != nullptr) {
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_preSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max
	}

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_postSkinningVertexBufferView = std::move(view);
	if (m_postSkinningVertexBufferView != nullptr) {
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_postSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max
	}

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

BufferView* Mesh::GetPreSkinningVertexBufferView() {
	return m_preSkinningVertexBufferView.get();
}

BufferView* Mesh::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
}

void Mesh::SetCLodBufferViews(
	std::unique_ptr<BufferView> clusterLODGroupsView,
	std::unique_ptr<BufferView> clusterLODSegmentsView,
	std::unique_ptr<BufferView> clusterLODNodesView
) {
	m_clusterLODGroupsView = std::move(clusterLODGroupsView);
	m_clusterLODSegmentsView = std::move(clusterLODSegmentsView);
	m_clusterLODNodesView = std::move(clusterLODNodesView);

	auto firstChunkOffsetDiv = [](const auto& chunkViews, uint32_t divisor) -> uint32_t {
		uint32_t minGroupIndex = std::numeric_limits<uint32_t>::max();
		uint64_t selectedOffset = 0u;
		bool found = false;
		for (const auto& [groupIndex, chunkView] : chunkViews) {
			if (chunkView != nullptr && groupIndex < minGroupIndex) {
				minGroupIndex = groupIndex;
				selectedOffset = chunkView->GetOffset();
				found = true;
			}
		}
		if (found) {
			return static_cast<uint32_t>(selectedOffset / divisor);
		}
		return 0u;
	};
	auto firstChunkOffsetBytes = [](const auto& chunkViews) -> uint32_t {
		uint32_t minGroupIndex = std::numeric_limits<uint32_t>::max();
		uint64_t selectedOffset = 0u;
		bool found = false;
		for (const auto& [groupIndex, chunkView] : chunkViews) {
			if (chunkView != nullptr && groupIndex < minGroupIndex) {
				minGroupIndex = groupIndex;
				selectedOffset = chunkView->GetOffset();
				found = true;
			}
		}
		if (found) {
			return static_cast<uint32_t>(selectedOffset);
		}
		return 0u;
	};

	m_perMeshBufferData.clodMeshletBufferOffset = firstChunkOffsetDiv(m_clodMeshletChunkViews, sizeof(meshopt_Meshlet));
	m_perMeshBufferData.clodMeshletVerticesBufferOffset = firstChunkOffsetDiv(m_clodMeshletVertexChunkViews, sizeof(uint32_t));
	m_perMeshBufferData.clodMeshletTrianglesBufferOffset = firstChunkOffsetBytes(m_clodMeshletTriangleChunkViews); // Intentionally in bytes to index byteaddressbuffer
	uint32_t totalMeshletCount = 0;
	for (const auto& group : m_clodGroups) {
		totalMeshletCount += group.meshletCount;
	}
	m_perMeshBufferData.clodNumMeshlets = totalMeshletCount;
	if (m_pCurrentMeshManager != nullptr && m_perMeshBufferView != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetBaseSkin(std::shared_ptr<Skeleton> skeleton) {
	m_baseSkeleton = skeleton;
	m_animationBoundingSpheres.clear();
	EnsureAnimatedBoundingSpheresBuilt_();
}

BoundingSphere Mesh::GetAnimatedBoundingSphere(size_t animationIndex) const
{
	EnsureAnimatedBoundingSpheresBuilt_();
	if (animationIndex >= m_animationBoundingSpheres.size()) {
		return m_perMeshBufferData.boundingSphere;
	}

	return m_animationBoundingSpheres[animationIndex];
}

void Mesh::EnsureAnimatedBoundingSpheresBuilt_() const
{
	if (!m_baseSkeleton) {
		m_animationBoundingSpheres.clear();
		return;
	}

	const size_t animationCount = m_baseSkeleton->animations.size();
	if (m_animationBoundingSpheres.size() == animationCount) {
		return;
	}

	m_animationBoundingSpheres.clear();
	m_animationBoundingSpheres.reserve(animationCount);

	if (animationCount == 0) {
		return;
	}

	const BoundingSphere staticSphere = m_perMeshBufferData.boundingSphere;
	if (staticSphere.sphere.w <= 0.0f) {
		m_animationBoundingSpheres.assign(animationCount, staticSphere);
		return;
	}

	auto samplingSkeleton = m_baseSkeleton->CopySkeleton();
	if (!samplingSkeleton) {
		m_animationBoundingSpheres.assign(animationCount, staticSphere);
		return;
	}

	const auto inverseBindMatrices = m_baseSkeleton->GetInverseBindMatrices();
	const size_t boneCount = m_baseSkeleton->GetBoneCount();
	if (boneCount == 0 || inverseBindMatrices.size() < boneCount) {
		m_animationBoundingSpheres.assign(animationCount, staticSphere);
		return;
	}

	for (size_t animationIndex = 0; animationIndex < animationCount; ++animationIndex) {
		const auto& animation = m_baseSkeleton->animations[animationIndex];
		if (!animation) {
			m_animationBoundingSpheres.push_back(staticSphere);
			continue;
		}

		samplingSkeleton->SetAnimation(animationIndex);
		const std::vector<float> sampleTimes = BuildAnimationSampleTimes(*animation);
		BoundingSphere animationSphere = staticSphere;
		float previousSampleTime = 0.0f;
		bool initialized = false;

		for (float sampleTime : sampleTimes) {
			const float deltaTime = sampleTime - previousSampleTime;
			samplingSkeleton->UpdateTransforms(deltaTime, true);
			previousSampleTime = sampleTime;

			const auto boneMatrices = samplingSkeleton->GetBoneMatrices();
			if (boneMatrices.size() < boneCount) {
				continue;
			}

			BoundingSphere sampledSphere{};
			for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
				const DirectX::XMMATRIX skinMatrix = DirectX::XMMatrixMultiply(boneMatrices[boneIndex], inverseBindMatrices[boneIndex]);
				const BoundingSphere transformedSphere = TransformBoundingSphere(staticSphere, skinMatrix);
				if (!initialized && boneIndex == 0) {
					sampledSphere = transformedSphere;
				}
				else {
					sampledSphere = MergeBoundingSpheres(sampledSphere, transformedSphere);
				}
			}

			if (!initialized) {
				animationSphere = sampledSphere;
				initialized = true;
			}
			else {
				animationSphere = MergeBoundingSpheres(animationSphere, sampledSphere);
			}
		}

		if (!initialized) {
			animationSphere = staticSphere;
		}
		animationSphere.sphere.w *= kAnimatedBoundsPaddingScale;
		m_animationBoundingSpheres.push_back(animationSphere);
	}
}

void Mesh::SetMaterialDataIndex(unsigned int index) {
	m_perMeshBufferData.materialDataIndex = index;
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
