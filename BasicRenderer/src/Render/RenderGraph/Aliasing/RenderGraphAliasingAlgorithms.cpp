#include "Render/RenderGraph/RenderGraph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include <rhi_helpers.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/PixelBuffer.h"
#include "Resources/MemoryStatisticsComponents.h"

RenderGraph::AutoAliasDebugSnapshot RenderGraph::GetAutoAliasDebugSnapshot() const {
	return m_aliasingSubsystem.BuildDebugSnapshot(
		autoAliasModeLastFrame,
		autoAliasPlannerStats,
		autoAliasExclusionReasonSummary,
		autoAliasPoolDebug);
}

namespace {
	rhi::ResourceDesc BuildAliasTextureResourceDesc(const TextureDescription& desc) {
		const uint16_t mipLevels = desc.generateMipMaps
			? CalculateMipLevels(desc.imageDimensions[0].width, desc.imageDimensions[0].height)
			: 1;

		uint32_t arraySize = desc.arraySize;
		if (!desc.isArray && !desc.isCubemap) {
			arraySize = 1;
		}

		auto width = desc.imageDimensions[0].width;
		auto height = desc.imageDimensions[0].height;
		if (desc.padInternalResolution) {
			width = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(width)))));
			height = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(height)))));
		}

		rhi::ResourceDesc textureDesc{
			.type = rhi::ResourceType::Texture2D,
			.texture = {
				.format = desc.format,
				.width = static_cast<uint32_t>(width),
				.height = static_cast<uint32_t>(height),
				.depthOrLayers = static_cast<uint16_t>(desc.isCubemap ? 6 * arraySize : arraySize),
				.mipLevels = mipLevels,
				.sampleCount = 1,
				.initialLayout = rhi::ResourceLayout::Common,
				.optimizedClear = nullptr
			}
		};

		if (desc.hasRTV) {
			textureDesc.resourceFlags |= rhi::ResourceFlags::RF_AllowRenderTarget;
		}
		if (desc.hasDSV) {
			textureDesc.resourceFlags |= rhi::ResourceFlags::RF_AllowDepthStencil;
		}
		if (desc.hasUAV) {
			textureDesc.resourceFlags |= rhi::ResourceFlags::RF_AllowUnorderedAccess;
		}

		return textureDesc;
	}

	uint64_t AlignUpU64(uint64_t value, uint64_t alignment) {
		if (alignment == 0) {
			return value;
		}
		return (value + alignment - 1ull) / alignment * alignment;
	}

	uint64_t HashCombineU64(uint64_t seed, uint64_t value) {
		seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
		return seed;
	}

	uint64_t BuildAliasPlacementSignatureValue(uint64_t poolID, size_t slotIndex, uint64_t poolGeneration) {
		uint64_t signature = 0xcbf29ce484222325ull;
		signature = HashCombineU64(signature, poolID);
		signature = HashCombineU64(signature, static_cast<uint64_t>(slotIndex));
		signature = HashCombineU64(signature, poolGeneration);
		return signature;
	}
}

void rg::alias::RenderGraphAliasingSubsystem::AutoAssignAliasingPools(RenderGraph& rg, const std::vector<AliasSchedulingNode>& nodes) const {
	auto& autoAliasPoolByID = rg.autoAliasPoolByID;
	auto& autoAliasExclusionReasonByID = rg.autoAliasExclusionReasonByID;
	auto& autoAliasExclusionReasonSummary = rg.autoAliasExclusionReasonSummary;
	auto& autoAliasPlannerStats = rg.autoAliasPlannerStats;
	auto& autoAliasModeLastFrame = rg.autoAliasModeLastFrame;
	auto& m_getAutoAliasMode = rg.m_getAutoAliasMode;
	auto& m_framePasses = rg.m_framePasses;
	auto& _registry = rg._registry;
	auto& resourcesByID = rg.resourcesByID;
	auto& m_getAutoAliasMaxMixedQueueAssignments = rg.m_getAutoAliasMaxMixedQueueAssignments;
	auto& m_getAutoAliasMaxMixedQueueBytesMB = rg.m_getAutoAliasMaxMixedQueueBytesMB;
	auto& m_getAutoAliasLogExclusionReasons = rg.m_getAutoAliasLogExclusionReasons;

	autoAliasPoolByID.clear();
	autoAliasExclusionReasonByID.clear();
	autoAliasExclusionReasonSummary.clear();
	autoAliasPlannerStats = {};

	const AutoAliasMode mode = m_getAutoAliasMode ? m_getAutoAliasMode() : AutoAliasMode::Off;
	autoAliasModeLastFrame = mode;
	if (mode == AutoAliasMode::Off) {
		return;
	}

	if (nodes.empty() || m_framePasses.empty()) {
		return;
	}

	std::vector<size_t> indeg(nodes.size(), 0);
	for (size_t i = 0; i < nodes.size(); ++i) {
		indeg[i] = nodes[i].indegree;
	}

	std::vector<size_t> ready;
	ready.reserve(nodes.size());
	for (size_t i = 0; i < nodes.size(); ++i) {
		if (indeg[i] == 0) {
			ready.push_back(i);
		}
	}

	std::vector<size_t> topoOrder;
	topoOrder.reserve(nodes.size());
	while (!ready.empty()) {
		auto bestIt = std::min_element(ready.begin(), ready.end(), [&](size_t a, size_t b) {
			if (nodes[a].originalOrder != nodes[b].originalOrder) {
				return nodes[a].originalOrder < nodes[b].originalOrder;
			}
			return a < b;
			});
		size_t u = *bestIt;
		ready.erase(bestIt);
		topoOrder.push_back(u);
		for (size_t v : nodes[u].out) {
			if (--indeg[v] == 0) {
				ready.push_back(v);
			}
		}
	}

	if (topoOrder.size() != nodes.size()) {
		return;
	}

	std::vector<size_t> passTopoRank(m_framePasses.size(), 0);
	std::vector<uint32_t> passCriticality(m_framePasses.size(), 0);
	uint32_t maxCriticality = 1;
	for (size_t rank = 0; rank < topoOrder.size(); ++rank) {
		const auto& node = nodes[topoOrder[rank]];
		if (node.passIndex < passTopoRank.size()) {
			passTopoRank[node.passIndex] = rank;
			passCriticality[node.passIndex] = node.criticality;
			maxCriticality = std::max(maxCriticality, node.criticality);
		}
	}

	struct AutoCandidate {
		uint64_t resourceID = 0;
		uint64_t sizeBytes = 0;
		uint64_t alignment = 1;
		size_t firstUse = std::numeric_limits<size_t>::max();
		size_t lastUse = 0;
		bool usesRender = false;
		bool usesCompute = false;
		bool isMaterializedAtCompile = false;
		uint32_t maxNodeCriticality = 0;
		rhi::Format format = rhi::Format::Unknown;
		std::optional<uint64_t> manualPool;
	};

	std::unordered_map<uint64_t, AutoCandidate> candidates;
	auto device = DeviceManager::GetInstance().GetDevice();

	auto collectHandle = [&](const ResourceRegistry::RegistryHandle& handle, size_t topoRank, bool isComputePass, uint32_t passCrit) {
		if (handle.IsEphemeral()) {
			return;
		}

		auto* resource = _registry.Resolve(handle);
		auto* texture = dynamic_cast<PixelBuffer*>(resource);
		if (!texture) {
			return;
		}

		const uint64_t resourceID = handle.GetGlobalResourceID();
		auto const& desc = texture->GetDescription();
		if (!desc.allowAlias) {
			autoAliasExclusionReasonByID.try_emplace(resourceID, "allowAlias is disabled");
			return;
		}

		auto [it, inserted] = candidates.try_emplace(resourceID);
		auto& candidate = it->second;
		candidate.resourceID = resourceID;
		candidate.firstUse = std::min(candidate.firstUse, topoRank);
		candidate.lastUse = std::max(candidate.lastUse, topoRank);
		candidate.maxNodeCriticality = std::max(candidate.maxNodeCriticality, passCrit);
		candidate.usesCompute = candidate.usesCompute || isComputePass;
		candidate.usesRender = candidate.usesRender || !isComputePass;
		candidate.isMaterializedAtCompile = candidate.isMaterializedAtCompile || texture->IsMaterialized();
		candidate.manualPool = desc.aliasingPoolID;
		candidate.format = desc.format;

		if (inserted || candidate.sizeBytes == 0) {
			auto resourceDesc = BuildAliasTextureResourceDesc(desc);
			rhi::ResourceAllocationInfo info{};
			device.GetResourceAllocationInfo(&resourceDesc, 1, &info);
			candidate.sizeBytes = info.sizeInBytes;
			candidate.alignment = std::max<uint64_t>(1, info.alignment);
		}
	};

	for (size_t passIdx = 0; passIdx < m_framePasses.size(); ++passIdx) {
		const auto& any = m_framePasses[passIdx];
		const size_t topoRank = passTopoRank[passIdx];
		const uint32_t passCrit = passCriticality[passIdx];

		if (any.type == RenderGraph::PassType::Render) {
			auto const& p = std::get<RenderGraph::RenderPassAndResources>(any.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				collectHandle(req.resourceHandleAndRange.resource, topoRank, false, passCrit);
			}
			for (auto const& t : p.resources.internalTransitions) {
				collectHandle(t.first.resource, topoRank, false, passCrit);
			}
		}
		else if (any.type == RenderGraph::PassType::Compute) {
			auto const& p = std::get<RenderGraph::ComputePassAndResources>(any.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				collectHandle(req.resourceHandleAndRange.resource, topoRank, true, passCrit);
			}
			for (auto const& t : p.resources.internalTransitions) {
				collectHandle(t.first.resource, topoRank, true, passCrit);
			}
		}
	}

	auto scoreCandidate = [&](const AutoCandidate& c) {
		const float benefitMB = static_cast<float>(c.sizeBytes) / (1024.0f * 1024.0f);
		const float critNorm = static_cast<float>(c.maxNodeCriticality) / static_cast<float>(maxCriticality);
		const float queuePenalty = (c.usesRender && c.usesCompute) ? 1.0f : 0.0f;
		const float materializedPenalty = c.isMaterializedAtCompile ? 1.0f : 0.0f;

		switch (mode) {
		case AutoAliasMode::Conservative:
			return benefitMB - (3.0f * queuePenalty) - (2.0f * critNorm) - (1.0f * materializedPenalty);
		case AutoAliasMode::Balanced:
			return benefitMB - (2.0f * queuePenalty) - (1.25f * critNorm) - (0.5f * materializedPenalty);
		case AutoAliasMode::Aggressive:
			return benefitMB - (1.0f * queuePenalty) - (0.5f * critNorm) - (0.25f * materializedPenalty);
		case AutoAliasMode::Off:
		default:
			return -std::numeric_limits<float>::infinity();
		}
	};

	const float inclusionThreshold = [&]() {
		switch (mode) {
		case AutoAliasMode::Conservative: return 1.0f;
		case AutoAliasMode::Balanced: return 0.25f;
		case AutoAliasMode::Aggressive: return -0.5f;
		case AutoAliasMode::Off:
		default: return std::numeric_limits<float>::infinity();
		}
	}();

	constexpr uint64_t kAutoPoolBase = 0xA171000000000000ull;

	struct MixedAssignment {
		uint64_t resourceID = 0;
		float score = 0.0f;
		uint64_t sizeBytes = 0;
	};
	std::vector<MixedAssignment> mixedAssignments;
	mixedAssignments.reserve(candidates.size());

	for (auto const& [resourceID, c] : candidates) {
		(void)resourceID;
		autoAliasPlannerStats.candidatesSeen++;
		autoAliasPlannerStats.candidateBytes += c.sizeBytes;

		if (c.manualPool.has_value()) {
			autoAliasPlannerStats.manuallyAssigned++;
			continue;
		}

		const float score = scoreCandidate(c);
		if (score < inclusionThreshold) {
			autoAliasPlannerStats.excluded++;
			autoAliasExclusionReasonByID[c.resourceID] = "score below threshold";
			continue;
		}

		uint64_t queueClass = 1;
		if (c.usesRender && c.usesCompute) {
			queueClass = 3;
		}
		else if (c.usesCompute) {
			queueClass = 2;
		}

		const uint64_t formatClass = static_cast<uint64_t>(c.format) & 0xFFFFull;
		const uint64_t poolID = kAutoPoolBase | (queueClass << 48) | (formatClass << 16);
		autoAliasPoolByID[c.resourceID] = poolID;
		autoAliasPlannerStats.autoAssigned++;
		autoAliasPlannerStats.autoAssignedBytes += c.sizeBytes;

		if (c.usesRender && c.usesCompute) {
			mixedAssignments.push_back(MixedAssignment{
				.resourceID = c.resourceID,
				.score = score,
				.sizeBytes = c.sizeBytes
				});
		}
	}

	const uint32_t mixedAssignCap = m_getAutoAliasMaxMixedQueueAssignments
		? m_getAutoAliasMaxMixedQueueAssignments()
		: 0u;
	const float mixedBytesCapMB = m_getAutoAliasMaxMixedQueueBytesMB
		? m_getAutoAliasMaxMixedQueueBytesMB()
		: 0.0f;
	const uint64_t mixedBytesCap = mixedBytesCapMB <= 0.0f
		? 0ull
		: static_cast<uint64_t>(mixedBytesCapMB * 1024.0f * 1024.0f);

	if (!mixedAssignments.empty() && (mixedAssignCap > 0 || mixedBytesCap > 0)) {
		uint64_t currentMixedBytes = 0;
		for (const auto& entry : mixedAssignments) {
			currentMixedBytes += entry.sizeBytes;
		}

		std::sort(mixedAssignments.begin(), mixedAssignments.end(), [](const MixedAssignment& a, const MixedAssignment& b) {
			if (a.score != b.score) {
				return a.score < b.score;
			}
			return a.resourceID < b.resourceID;
			});

		size_t currentMixedCount = mixedAssignments.size();
		for (const auto& entry : mixedAssignments) {
			const bool countOver = (mixedAssignCap > 0) && (currentMixedCount > mixedAssignCap);
			const bool bytesOver = (mixedBytesCap > 0) && (currentMixedBytes > mixedBytesCap);
			if (!countOver && !bytesOver) {
				break;
			}

			auto erased = autoAliasPoolByID.erase(entry.resourceID);
			if (erased == 0) {
				continue;
			}

			autoAliasExclusionReasonByID[entry.resourceID] = "rolled back by mixed-queue concurrency guard";
			autoAliasPlannerStats.rolledBackMixedQueue++;
			autoAliasPlannerStats.rolledBackMixedQueueBytes += entry.sizeBytes;
			autoAliasPlannerStats.autoAssigned--;
			autoAliasPlannerStats.autoAssignedBytes -= entry.sizeBytes;
			autoAliasPlannerStats.excluded++;

			currentMixedCount--;
			currentMixedBytes -= entry.sizeBytes;
		}
	}

	std::unordered_map<std::string, size_t> exclusionReasonCounts;
	exclusionReasonCounts.reserve(autoAliasExclusionReasonByID.size());
	for (const auto& [id, reason] : autoAliasExclusionReasonByID) {
		(void)id;
		exclusionReasonCounts[reason]++;
	}
	autoAliasExclusionReasonSummary.clear();
	autoAliasExclusionReasonSummary.reserve(exclusionReasonCounts.size());
	for (const auto& [reason, count] : exclusionReasonCounts) {
		autoAliasExclusionReasonSummary.push_back(AutoAliasReasonCount{ .reason = reason, .count = count });
	}
	std::sort(autoAliasExclusionReasonSummary.begin(), autoAliasExclusionReasonSummary.end(), [](const AutoAliasReasonCount& a, const AutoAliasReasonCount& b) {
		if (a.count != b.count) {
			return a.count > b.count;
		}
		return a.reason < b.reason;
		});

	if (autoAliasPlannerStats.candidatesSeen > 0) {
		spdlog::info(
			"RG auto alias: mode={} candidates={} manual={} auto={} excluded={} rolledBackMixed={} candidateMB={:.2f} autoMB={:.2f} rollbackMB={:.2f}",
			static_cast<uint32_t>(mode),
			autoAliasPlannerStats.candidatesSeen,
			autoAliasPlannerStats.manuallyAssigned,
			autoAliasPlannerStats.autoAssigned,
			autoAliasPlannerStats.excluded,
			autoAliasPlannerStats.rolledBackMixedQueue,
			static_cast<double>(autoAliasPlannerStats.candidateBytes) / (1024.0 * 1024.0),
			static_cast<double>(autoAliasPlannerStats.autoAssignedBytes) / (1024.0 * 1024.0),
			static_cast<double>(autoAliasPlannerStats.rolledBackMixedQueueBytes) / (1024.0 * 1024.0));

		if (!exclusionReasonCounts.empty()) {
			std::vector<std::pair<std::string, size_t>> reasonList;
			reasonList.reserve(exclusionReasonCounts.size());
			for (const auto& kv : exclusionReasonCounts) {
				reasonList.push_back(kv);
			}
			std::sort(reasonList.begin(), reasonList.end(), [](const auto& a, const auto& b) {
				if (a.second != b.second) {
					return a.second > b.second;
				}
				return a.first < b.first;
				});

			std::ostringstream summary;
			for (size_t i = 0; i < reasonList.size(); ++i) {
				if (i > 0) {
					summary << ", ";
				}
				summary << reasonList[i].first << "=" << reasonList[i].second;
			}
			spdlog::info("RG auto alias exclusions: {}", summary.str());

			const bool verboseExclusions = m_getAutoAliasLogExclusionReasons
				? m_getAutoAliasLogExclusionReasons()
				: false;
			if (verboseExclusions) {
				size_t logged = 0;
				for (const auto& [resourceID, reason] : autoAliasExclusionReasonByID) {
					if (logged >= 24) {
						break;
					}
					auto itRes = resourcesByID.find(resourceID);
					const std::string resourceName = (itRes != resourcesByID.end() && itRes->second)
						? itRes->second->GetName()
						: std::string("<unknown>");
					spdlog::info("RG auto alias exclusion detail: id={} name='{}' reason='{}'", resourceID, resourceName, reason);
					logged++;
				}
			}
		}
	}
}

void rg::alias::RenderGraphAliasingSubsystem::BuildAliasPlanAfterDag(RenderGraph& rg, const std::vector<AliasSchedulingNode>& nodes) const {
	auto& aliasMaterializeOptionsByID = rg.aliasMaterializeOptionsByID;
	auto& aliasActivationPending = rg.aliasActivationPending;
	auto& autoAliasPlannerStats = rg.autoAliasPlannerStats;
	auto& autoAliasPoolDebug = rg.autoAliasPoolDebug;
	auto& aliasPoolPlanFrameIndex = rg.aliasPoolPlanFrameIndex;
	auto& aliasPoolRetireIdleFrames = rg.aliasPoolRetireIdleFrames;
	auto& m_getAutoAliasPoolRetireIdleFrames = rg.m_getAutoAliasPoolRetireIdleFrames;
	auto& aliasPoolGrowthHeadroom = rg.aliasPoolGrowthHeadroom;
	auto& m_getAutoAliasPoolGrowthHeadroom = rg.m_getAutoAliasPoolGrowthHeadroom;
	auto& persistentAliasPools = rg.persistentAliasPools;
	auto& m_framePasses = rg.m_framePasses;
	auto& _registry = rg._registry;
	auto& autoAliasPoolByID = rg.autoAliasPoolByID;
	auto& resourcesByID = rg.resourcesByID;
	auto& aliasPlacementPoolByID = rg.aliasPlacementPoolByID;
	auto& aliasPlacementSignatureByID = rg.aliasPlacementSignatureByID;

	aliasMaterializeOptionsByID.clear();
	aliasActivationPending.clear();
	autoAliasPlannerStats.pooledIndependentBytes = 0;
	autoAliasPlannerStats.pooledActualBytes = 0;
	autoAliasPlannerStats.pooledSavedBytes = 0;
	autoAliasPoolDebug.clear();
	uint64_t pooledReservedBytes = 0;
	aliasPoolPlanFrameIndex++;
	aliasPoolRetireIdleFrames = m_getAutoAliasPoolRetireIdleFrames
		? m_getAutoAliasPoolRetireIdleFrames()
		: aliasPoolRetireIdleFrames;
	aliasPoolGrowthHeadroom = m_getAutoAliasPoolGrowthHeadroom
		? std::max(1.0f, m_getAutoAliasPoolGrowthHeadroom())
		: std::max(1.0f, aliasPoolGrowthHeadroom);

	for (auto& [poolID, poolState] : persistentAliasPools) {
		(void)poolID;
		poolState.usedThisFrame = false;
	}

	std::vector<size_t> indeg(nodes.size(), 0);
	for (size_t i = 0; i < nodes.size(); ++i) {
		indeg[i] = nodes[i].indegree;
	}

	std::vector<size_t> ready;
	ready.reserve(nodes.size());
	for (size_t i = 0; i < nodes.size(); ++i) {
		if (indeg[i] == 0) {
			ready.push_back(i);
		}
	}

	std::vector<size_t> topoOrder;
	topoOrder.reserve(nodes.size());
	while (!ready.empty()) {
		auto bestIt = std::min_element(ready.begin(), ready.end(), [&](size_t a, size_t b) {
			if (nodes[a].originalOrder != nodes[b].originalOrder) {
				return nodes[a].originalOrder < nodes[b].originalOrder;
			}
			return a < b;
			});
		size_t u = *bestIt;
		ready.erase(bestIt);
		topoOrder.push_back(u);
		for (size_t v : nodes[u].out) {
			if (--indeg[v] == 0) {
				ready.push_back(v);
			}
		}
	}

	if (topoOrder.size() != nodes.size()) {
		throw std::runtime_error("RenderGraphAliasingSubsystem::BuildAliasPlanAfterDag received non-DAG node data");
	}

	std::vector<size_t> passTopoRank(m_framePasses.size(), 0);
	for (size_t rank = 0; rank < topoOrder.size(); ++rank) {
		const auto& node = nodes[topoOrder[rank]];
		if (node.passIndex < passTopoRank.size()) {
			passTopoRank[node.passIndex] = rank;
		}
	}

	struct Candidate {
		uint64_t resourceID = 0;
		uint64_t poolID = 0;
		uint64_t sizeBytes = 0;
		uint64_t alignment = 1;
		size_t firstUse = std::numeric_limits<size_t>::max();
		size_t lastUse = 0;
		bool firstUseIsWrite = false;
		bool manualPoolAssigned = false;
	};

	std::unordered_map<uint64_t, Candidate> candidates;
	auto device = DeviceManager::GetInstance().GetDevice();

	for (size_t passIdx = 0; passIdx < m_framePasses.size(); ++passIdx) {
		const size_t usageOrder = passTopoRank[passIdx];
		auto collectHandle = [&](const ResourceRegistry::RegistryHandle& handle, bool isWrite) {
			if (handle.IsEphemeral()) {
				return;
			}
			auto* resource = _registry.Resolve(handle);
			auto* texture = dynamic_cast<PixelBuffer*>(resource);
			if (!texture) {
				return;
			}
			auto const& desc = texture->GetDescription();
			if (!desc.allowAlias) {
				return;
			}

			std::optional<uint64_t> poolID = desc.aliasingPoolID;
			if (!poolID.has_value()) {
				auto itAuto = autoAliasPoolByID.find(handle.GetGlobalResourceID());
				if (itAuto != autoAliasPoolByID.end()) {
					poolID = itAuto->second;
				}
			}

			if (!poolID.has_value()) {
				return;
			}

			auto [it, inserted] = candidates.try_emplace(handle.GetGlobalResourceID());
			auto& c = it->second;
			c.resourceID = handle.GetGlobalResourceID();
			c.poolID = poolID.value();
			if (usageOrder < c.firstUse) {
				c.firstUse = usageOrder;
				c.firstUseIsWrite = isWrite;
			}
			else if (usageOrder == c.firstUse) {
				c.firstUseIsWrite = c.firstUseIsWrite || isWrite;
			}
			c.lastUse = std::max(c.lastUse, usageOrder);
			c.manualPoolAssigned = c.manualPoolAssigned || texture->GetDescription().aliasingPoolID.has_value();

			if (inserted || c.sizeBytes == 0) {
				auto resourceDesc = BuildAliasTextureResourceDesc(desc);
				rhi::ResourceAllocationInfo info{};
				device.GetResourceAllocationInfo(&resourceDesc, 1, &info);
				c.sizeBytes = info.sizeInBytes;
				c.alignment = std::max<uint64_t>(1, info.alignment);
			}
		};

		auto const& any = m_framePasses[passIdx];
		if (any.type == RenderGraph::PassType::Render) {
			auto const& p = std::get<RenderGraph::RenderPassAndResources>(any.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				collectHandle(req.resourceHandleAndRange.resource, AccessTypeIsWriteType(req.state.access));
			}
			for (auto const& t : p.resources.internalTransitions) {
				collectHandle(t.first.resource, true);
			}
		}
		else if (any.type == RenderGraph::PassType::Compute) {
			auto const& p = std::get<RenderGraph::ComputePassAndResources>(any.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				collectHandle(req.resourceHandleAndRange.resource, AccessTypeIsWriteType(req.state.access));
			}
			for (auto const& t : p.resources.internalTransitions) {
				collectHandle(t.first.resource, true);
			}
		}
	}

	std::unordered_map<uint64_t, std::vector<Candidate>> byPool;
	for (auto const& [id, c] : candidates) {
		(void)id;
		if (c.firstUse == std::numeric_limits<size_t>::max()) {
			continue;
		}

		if (!c.firstUseIsWrite) {
			auto itRes = resourcesByID.find(c.resourceID);
			const std::string resourceName = (itRes != resourcesByID.end() && itRes->second)
				? itRes->second->GetName()
				: std::string("<unknown>");

			throw std::runtime_error(
				"Aliasing candidate has first-use READ (explicit alias initialization unavailable). "
				"resourceId=" + std::to_string(c.resourceID) +
				" name='" + resourceName + "'" +
				" poolId=" + std::to_string(c.poolID) +
				" manualPool=" + std::to_string(c.manualPoolAssigned ? 1 : 0) +
				" firstUseTopoRank=" + std::to_string(static_cast<uint64_t>(c.firstUse)) +
				". Resource should either be non-aliased, initialized before first read, or first-used as write.");
		}

		byPool[c.poolID].push_back(c);
	}

	if (!byPool.empty()) {
		size_t totalCandidates = 0;
		for (const auto& [poolID, poolCandidates] : byPool) {
			(void)poolID;
			totalCandidates += poolCandidates.size();
		}
		spdlog::info("RG alias plan: pools={} candidates={}", byPool.size(), totalCandidates);
	}

	for (auto& [poolID, poolCandidates] : byPool) {
		AutoAliasPoolDebug poolDebug{};
		poolDebug.poolID = poolID;

		uint64_t poolIndependentBytes = 0;
		for (const auto& c : poolCandidates) {
			poolIndependentBytes += c.sizeBytes;
		}

		std::sort(poolCandidates.begin(), poolCandidates.end(), [](const Candidate& a, const Candidate& b) {
			if (a.firstUse != b.firstUse) {
				return a.firstUse < b.firstUse;
			}
			if (a.lastUse != b.lastUse) {
				return a.lastUse < b.lastUse;
			}
			if (a.sizeBytes != b.sizeBytes) {
				return a.sizeBytes > b.sizeBytes;
			}
			return a.resourceID < b.resourceID;
		});

		struct Slot {
			size_t lastUse = 0;
			uint64_t maxSize = 0;
			uint64_t alignment = 1;
			uint64_t offset = 0;
		};

		std::vector<Slot> slots;
		std::unordered_map<uint64_t, size_t> resourceToSlot;

		for (auto const& c : poolCandidates) {
			size_t chosenSlot = std::numeric_limits<size_t>::max();
			uint64_t bestGrowthBytes = std::numeric_limits<uint64_t>::max();
			size_t bestLastUse = std::numeric_limits<size_t>::max();
			for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
				if (c.firstUse > slots[slotIndex].lastUse) {
					const uint64_t growthBytes = c.sizeBytes > slots[slotIndex].maxSize
						? (c.sizeBytes - slots[slotIndex].maxSize)
						: 0ull;

					if (growthBytes < bestGrowthBytes ||
						(growthBytes == bestGrowthBytes && slots[slotIndex].lastUse < bestLastUse)) {
						bestGrowthBytes = growthBytes;
						bestLastUse = slots[slotIndex].lastUse;
						chosenSlot = slotIndex;
					}
				}
			}

			if (chosenSlot == std::numeric_limits<size_t>::max()) {
				chosenSlot = slots.size();
				slots.push_back({});
			}

			auto& slot = slots[chosenSlot];
			slot.lastUse = std::max(slot.lastUse, c.lastUse);
			slot.maxSize = std::max(slot.maxSize, c.sizeBytes);
			slot.alignment = std::max(slot.alignment, c.alignment);
			resourceToSlot[c.resourceID] = chosenSlot;
		}

		uint64_t heapSize = 0;
		for (auto& slot : slots) {
			heapSize = AlignUpU64(heapSize, slot.alignment);
			slot.offset = heapSize;
			heapSize += slot.maxSize;
		}

		if (heapSize == 0) {
			continue;
		}
		poolDebug.requiredBytes = heapSize;

		autoAliasPlannerStats.pooledIndependentBytes += poolIndependentBytes;

		uint64_t poolAlignment = 1;
		for (const auto& slot : slots) {
			poolAlignment = std::max(poolAlignment, slot.alignment);
		}

		auto& poolState = persistentAliasPools[poolID];
		const bool needsInitialAllocation = !static_cast<bool>(poolState.allocation);
		const bool needsLargerHeap = heapSize > poolState.capacityBytes;
		const bool needsHigherAlignment = poolAlignment > poolState.alignment;

		if (needsInitialAllocation || needsLargerHeap || needsHigherAlignment) {
			uint64_t newCapacity = heapSize;
			if (!needsInitialAllocation && needsLargerHeap && poolState.capacityBytes > 0) {
				const double grownTarget = static_cast<double>(poolState.capacityBytes) * static_cast<double>(aliasPoolGrowthHeadroom);
				const uint64_t grownCapacity = std::max<uint64_t>(
					heapSize,
					static_cast<uint64_t>(std::ceil(grownTarget)));
				newCapacity = std::max(newCapacity, grownCapacity);
			}

			rhi::ma::AllocationDesc allocDesc{};
			allocDesc.heapType = rhi::HeapType::DeviceLocal;
			allocDesc.flags = rhi::ma::AllocationFlagCanAlias;

			rhi::ResourceAllocationInfo allocInfo{};
			allocInfo.offset = 0;
			allocInfo.alignment = poolAlignment;
			allocInfo.sizeInBytes = newCapacity;

			TrackedHandle newAliasPool;
			AllocationTrackDesc trackDesc(0);
			trackDesc.attach
				.Set<MemoryStatisticsComponents::ResourceName>({ "RenderGraph Alias Pool" })
				.Set<MemoryStatisticsComponents::ResourceType>({ rhi::ResourceType::Unknown })
				.Set<MemoryStatisticsComponents::AliasingPool>({ poolID });

			const auto allocResult = DeviceManager::GetInstance().AllocateMemoryTracked(allocDesc, allocInfo, newAliasPool, trackDesc);
			if (!rhi::IsOk(allocResult)) {
				throw std::runtime_error("Failed to allocate alias pool memory");
			}

			if (poolState.allocation) {
				DeletionManager::GetInstance().MarkForDelete(std::move(poolState.allocation));
			}

			poolState.allocation = std::move(newAliasPool);
			poolState.capacityBytes = newCapacity;
			poolState.alignment = poolAlignment;
			poolState.generation++;

			spdlog::info(
				"RG alias pool {}: pool={} capacity={} required={} alignment={} slots={} generation={}",
				needsInitialAllocation ? "allocated" : "grew",
				poolID,
				newCapacity,
				heapSize,
				poolAlignment,
				slots.size(),
				poolState.generation);
		}

		poolState.usedThisFrame = true;
		poolState.lastUsedFrame = aliasPoolPlanFrameIndex;
		autoAliasPlannerStats.pooledActualBytes += heapSize;
		pooledReservedBytes += poolState.capacityBytes;
		poolDebug.reservedBytes = poolState.capacityBytes;

		auto* allocation = poolState.allocation.GetAllocation();
		if (!allocation) {
			throw std::runtime_error("Failed to allocate alias pool memory");
		}

		for (auto const& c : poolCandidates) {
			auto slotIndex = resourceToSlot[c.resourceID];
			const auto& slot = slots[slotIndex];

			auto itResDebug = resourcesByID.find(c.resourceID);
			const std::string resourceNameDebug = (itResDebug != resourcesByID.end() && itResDebug->second)
				? itResDebug->second->GetName()
				: std::string("<unknown>");

			poolDebug.ranges.push_back(AutoAliasPoolRangeDebug{
				.resourceID = c.resourceID,
				.resourceName = resourceNameDebug,
				.startByte = slot.offset,
				.endByte = slot.offset + c.sizeBytes,
				.sizeBytes = c.sizeBytes,
				.firstUse = c.firstUse,
				.lastUse = c.lastUse,
				.overlapsByteRange = false
				});

			PixelBuffer::MaterializeOptions options{};
			options.aliasPlacement = TextureAliasPlacement{
				.allocation = allocation,
				.offset = slot.offset,
				.poolID = poolID,
			};
			aliasMaterializeOptionsByID[c.resourceID] = options;
			aliasPlacementPoolByID[c.resourceID] = poolID;

			auto itResName = resourcesByID.find(c.resourceID);
			const std::string resourceName = (itResName != resourcesByID.end() && itResName->second)
				? itResName->second->GetName()
				: std::string("<unknown>");
			spdlog::info(
				"RG alias bind: pool={} resourceId={} name='{}' slot={} offset={} size={} firstUse={} lastUse={}",
				poolID,
				c.resourceID,
				resourceName,
				slotIndex,
				slot.offset,
				c.sizeBytes,
				c.firstUse,
				c.lastUse);

			const uint64_t newSignature = BuildAliasPlacementSignatureValue(poolID, slotIndex, poolState.generation);
			auto itRes = resourcesByID.find(c.resourceID);
			if (itRes != resourcesByID.end()) {
				auto texture = std::dynamic_pointer_cast<PixelBuffer>(itRes->second);
				if (texture && texture->IsMaterialized()) {
					auto itSig = aliasPlacementSignatureByID.find(c.resourceID);
					if (itSig == aliasPlacementSignatureByID.end() || itSig->second != newSignature) {
						texture->Dematerialize();
						aliasActivationPending.insert(c.resourceID);
					}
				}
			}
			else {
				auto itSig = aliasPlacementSignatureByID.find(c.resourceID);
				if (itSig == aliasPlacementSignatureByID.end() || itSig->second != newSignature) {
					aliasActivationPending.insert(c.resourceID);
				}
			}
			aliasPlacementSignatureByID[c.resourceID] = newSignature;
		}

		for (size_t i = 0; i < poolDebug.ranges.size(); ++i) {
			for (size_t j = i + 1; j < poolDebug.ranges.size(); ++j) {
				auto& a = poolDebug.ranges[i];
				auto& b = poolDebug.ranges[j];
				const uint64_t overlapStart = std::max(a.startByte, b.startByte);
				const uint64_t overlapEnd = std::min(a.endByte, b.endByte);
				if (overlapStart < overlapEnd) {
					a.overlapsByteRange = true;
					b.overlapsByteRange = true;
				}
			}
		}

		autoAliasPoolDebug.push_back(std::move(poolDebug));
	}

	std::sort(autoAliasPoolDebug.begin(), autoAliasPoolDebug.end(), [](const AutoAliasPoolDebug& a, const AutoAliasPoolDebug& b) {
		return a.poolID < b.poolID;
		});

	if (aliasPoolRetireIdleFrames > 0) {
		for (auto itPool = persistentAliasPools.begin(); itPool != persistentAliasPools.end(); ) {
			auto& poolState = itPool->second;
			if (poolState.usedThisFrame) {
				++itPool;
				continue;
			}

			const uint64_t idleFrames = (aliasPoolPlanFrameIndex > poolState.lastUsedFrame)
				? (aliasPoolPlanFrameIndex - poolState.lastUsedFrame)
				: 0ull;
			if (idleFrames < aliasPoolRetireIdleFrames) {
				++itPool;
				continue;
			}

			const uint64_t retiredPoolID = itPool->first;
			std::vector<uint64_t> resourcesToClear;
			resourcesToClear.reserve(aliasPlacementPoolByID.size());

			for (const auto& [resourceID, assignedPoolID] : aliasPlacementPoolByID) {
				if (assignedPoolID != retiredPoolID) {
					continue;
				}

				resourcesToClear.push_back(resourceID);
				auto itRes = resourcesByID.find(resourceID);
				if (itRes != resourcesByID.end() && itRes->second) {
					auto texture = std::dynamic_pointer_cast<PixelBuffer>(itRes->second);
					if (texture && texture->IsMaterialized()) {
						texture->Dematerialize();
					}
				}
			}

			for (uint64_t resourceID : resourcesToClear) {
				aliasPlacementPoolByID.erase(resourceID);
				aliasPlacementSignatureByID.erase(resourceID);
				aliasActivationPending.erase(resourceID);
			}

			if (poolState.allocation) {
				DeletionManager::GetInstance().MarkForDelete(std::move(poolState.allocation));
			}

			spdlog::info(
				"RG alias pool retired: pool={} idleFrames={} capacity={} generation={}",
				retiredPoolID,
				idleFrames,
				poolState.capacityBytes,
				poolState.generation);

			itPool = persistentAliasPools.erase(itPool);
		}
	}

	autoAliasPlannerStats.pooledSavedBytes =
		autoAliasPlannerStats.pooledIndependentBytes > autoAliasPlannerStats.pooledActualBytes
		? (autoAliasPlannerStats.pooledIndependentBytes - autoAliasPlannerStats.pooledActualBytes)
		: 0;

	if (autoAliasPlannerStats.pooledIndependentBytes > 0) {
		const double independentMB = static_cast<double>(autoAliasPlannerStats.pooledIndependentBytes) / (1024.0 * 1024.0);
		const double pooledMB = static_cast<double>(autoAliasPlannerStats.pooledActualBytes) / (1024.0 * 1024.0);
		const double pooledReservedMB = static_cast<double>(pooledReservedBytes) / (1024.0 * 1024.0);
		const double savedMB = static_cast<double>(autoAliasPlannerStats.pooledSavedBytes) / (1024.0 * 1024.0);
		const double savedPct = (independentMB > 0.0)
			? ((savedMB / independentMB) * 100.0)
			: 0.0;
		spdlog::info(
			"RG alias memory: independentMB={:.2f} pooledRequiredMB={:.2f} pooledReservedMB={:.2f} savedMB={:.2f} savedPct={:.1f}",
			independentMB,
			pooledMB,
			pooledReservedMB,
			savedMB,
			savedPct);
	}
}

void rg::alias::RenderGraphAliasingSubsystem::ApplyAliasQueueSynchronization(RenderGraph& rg) const {
	auto& batches = rg.batches;
	auto& aliasPlacementSignatureByID = rg.aliasPlacementSignatureByID;
	struct QueueUsage {
		bool usesRender = false;
		bool usesCompute = false;
	};

	struct SlotOwner {
		uint64_t resourceID = 0;
		size_t batchIndex = 0;
		QueueUsage usage;
	};

	std::unordered_map<uint64_t, SlotOwner> lastOwnerBySignature;

	auto markUsage = [](QueueUsage& usage, bool render, bool compute) {
		usage.usesRender = usage.usesRender || render;
		usage.usesCompute = usage.usesCompute || compute;
	};

	for (size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
		auto& batch = batches[batchIndex];
		std::unordered_map<uint64_t, std::pair<uint64_t, QueueUsage>> currentBySignature;

		auto accumulateFromReqs = [&](const std::vector<ResourceRequirement>& reqs, bool render, bool compute) {
			for (auto const& req : reqs) {
				const uint64_t resourceID = req.resourceHandleAndRange.resource.GetGlobalResourceID();
				auto itSig = aliasPlacementSignatureByID.find(resourceID);
				if (itSig == aliasPlacementSignatureByID.end()) {
					continue;
				}

				auto& entry = currentBySignature[itSig->second];
				if (entry.first == 0) {
					entry.first = resourceID;
				}
				markUsage(entry.second, render, compute);
			}
		};

		for (auto const& rp : batch.renderPasses) {
			accumulateFromReqs(rp.resources.frameResourceRequirements, true, false);
		}
		for (auto const& cp : batch.computePasses) {
			accumulateFromReqs(cp.resources.frameResourceRequirements, false, true);
		}

		for (auto const& [signature, current] : currentBySignature) {
			const uint64_t currentResourceID = current.first;
			const QueueUsage& currentUsage = current.second;

			auto prevIt = lastOwnerBySignature.find(signature);
			if (prevIt != lastOwnerBySignature.end() && prevIt->second.resourceID != currentResourceID) {
				auto& prevBatch = batches[prevIt->second.batchIndex];

				if (prevIt->second.usage.usesRender && currentUsage.usesCompute) {
					prevBatch.renderCompletionSignal = true;
					batch.computeQueueWaitOnRenderQueueBeforeTransition = true;
					batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue =
						std::max(batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue,
							prevBatch.renderCompletionFenceValue);
				}

				if (prevIt->second.usage.usesCompute && currentUsage.usesRender) {
					prevBatch.computeCompletionSignal = true;
					batch.renderQueueWaitOnComputeQueueBeforeTransition = true;
					batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue =
						std::max(batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue,
							prevBatch.computeCompletionFenceValue);
				}
			}

			lastOwnerBySignature[signature] = SlotOwner{
				.resourceID = currentResourceID,
				.batchIndex = batchIndex,
				.usage = currentUsage,
			};
		}
	}
}

