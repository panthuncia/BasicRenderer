#include "Render/RenderGraph/RenderGraph.h"

#include <algorithm>
#include <boost/functional/hash.hpp>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>
#include <tuple>

#include <rhi_helpers.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/MemoryStatisticsComponents.h"

RenderGraph::AutoAliasDebugSnapshot RenderGraph::GetAutoAliasDebugSnapshot() const {
	return m_aliasingSubsystem.BuildDebugSnapshot(
		autoAliasModeLastFrame,
		autoAliasPackingStrategyLastFrame,
		autoAliasPlannerStats,
		autoAliasExclusionReasonSummary,
		autoAliasPoolDebug);
}

namespace {
	uint16_t CalculateMipLevels(uint16_t width, uint16_t height) {
		return static_cast<uint16_t>(std::floor(std::log2((std::max)(width, height)))) + 1;
	}

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

	rhi::ResourceDesc BuildAliasBufferResourceDesc(uint64_t sizeBytes, bool unorderedAccess, rhi::HeapType heapType) {
		auto desc = rhi::helpers::ResourceDesc::Buffer(sizeBytes);
		desc.heapType = heapType;
		if (unorderedAccess) {
			desc.resourceFlags |= rhi::ResourceFlags::RF_AllowUnorderedAccess;
		}
		return desc;
	}

	uint64_t AlignUpU64(uint64_t value, uint64_t alignment) {
		if (alignment == 0) {
			return value;
		}
		return (value + alignment - 1ull) / alignment * alignment;
	}

	uint64_t BuildAliasPlacementSignatureValue(uint64_t poolID, uint64_t startByte, uint64_t endByte, uint64_t poolGeneration) {
		size_t signature = static_cast<size_t>(0xcbf29ce484222325ull);
		boost::hash_combine(signature, poolID);
		boost::hash_combine(signature, startByte);
		boost::hash_combine(signature, endByte);
		boost::hash_combine(signature, poolGeneration);
		return static_cast<uint64_t>(signature);
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
		bool isMaterializedAtCompile = false;
		uint32_t maxNodeCriticality = 0;
		std::optional<uint64_t> manualPool;
	};

	std::unordered_map<uint64_t, AutoCandidate> candidates;
	auto device = DeviceManager::GetInstance().GetDevice();

	auto collectHandle = [&](const ResourceRegistry::RegistryHandle& handle, size_t topoRank, uint32_t passCrit) {
		if (handle.IsEphemeral()) {
			return;
		}

		auto* resource = _registry.Resolve(handle);
		const uint64_t resourceID = handle.GetGlobalResourceID();
		auto* texture = dynamic_cast<PixelBuffer*>(resource);
		if (!texture) {
			auto* buffer = dynamic_cast<Buffer*>(resource);
			if (!buffer) {
				return;
			}

			if (!buffer->IsAliasingAllowed()) {
				autoAliasExclusionReasonByID.try_emplace(resourceID, "allowAlias is disabled");
				return;
			}

			if (buffer->GetAccessType() != rhi::HeapType::DeviceLocal) {
				autoAliasExclusionReasonByID.try_emplace(resourceID, "buffer heap is not DeviceLocal");
				return;
			}

			auto [it, inserted] = candidates.try_emplace(resourceID);
			auto& candidate = it->second;
			candidate.resourceID = resourceID;
			candidate.firstUse = std::min(candidate.firstUse, topoRank);
			candidate.lastUse = std::max(candidate.lastUse, topoRank);
			candidate.maxNodeCriticality = std::max(candidate.maxNodeCriticality, passCrit);
			candidate.isMaterializedAtCompile = candidate.isMaterializedAtCompile || buffer->IsMaterialized();
			candidate.manualPool = buffer->GetAliasingPoolHint();

			if (inserted || candidate.sizeBytes == 0) {
				auto resourceDesc = BuildAliasBufferResourceDesc(
					buffer->GetBufferSize(),
					buffer->IsUnorderedAccessEnabled(),
					buffer->GetAccessType());
				rhi::ResourceAllocationInfo info{};
				device.GetResourceAllocationInfo(&resourceDesc, 1, &info);
				candidate.sizeBytes = info.sizeInBytes;
				candidate.alignment = std::max<uint64_t>(1, info.alignment);
			}
			return;
		}

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
		candidate.isMaterializedAtCompile = candidate.isMaterializedAtCompile || texture->IsMaterialized();
		candidate.manualPool = desc.aliasingPoolID;

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
				collectHandle(req.resourceHandleAndRange.resource, topoRank, passCrit);
			}
			for (auto const& t : p.resources.internalTransitions) {
				collectHandle(t.first.resource, topoRank, passCrit);
			}
		}
		else if (any.type == RenderGraph::PassType::Compute) {
			auto const& p = std::get<RenderGraph::ComputePassAndResources>(any.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				collectHandle(req.resourceHandleAndRange.resource, topoRank, passCrit);
			}
			for (auto const& t : p.resources.internalTransitions) {
				collectHandle(t.first.resource, topoRank, passCrit);
			}
		}
	}

	auto scoreCandidate = [&](const AutoCandidate& c) {
		const float benefitMB = static_cast<float>(c.sizeBytes) / (1024.0f * 1024.0f);
		const float critNorm = static_cast<float>(c.maxNodeCriticality) / static_cast<float>(maxCriticality);
		const float materializedPenalty = c.isMaterializedAtCompile ? 1.0f : 0.0f;

		switch (mode) {
		case AutoAliasMode::Conservative:
			return benefitMB - (2.0f * critNorm) - (1.0f * materializedPenalty);
		case AutoAliasMode::Balanced:
			return benefitMB - (1.25f * critNorm) - (0.5f * materializedPenalty);
		case AutoAliasMode::Aggressive:
			return benefitMB - (0.5f * critNorm) - (0.25f * materializedPenalty);
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

	constexpr uint64_t kAutoPoolGlobal = 0xA171000000000000ull;

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

		autoAliasPoolByID[c.resourceID] = kAutoPoolGlobal;
		autoAliasPlannerStats.autoAssigned++;
		autoAliasPlannerStats.autoAssignedBytes += c.sizeBytes;
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
			"RG auto alias: mode={} candidates={} manual={} auto={} excluded={} candidateMB={:.2f} autoMB={:.2f}",
			static_cast<uint32_t>(mode),
			autoAliasPlannerStats.candidatesSeen,
			autoAliasPlannerStats.manuallyAssigned,
			autoAliasPlannerStats.autoAssigned,
			autoAliasPlannerStats.excluded,
			static_cast<double>(autoAliasPlannerStats.candidateBytes) / (1024.0 * 1024.0),
			static_cast<double>(autoAliasPlannerStats.autoAssignedBytes) / (1024.0 * 1024.0));

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

bool AccessTypeIsWriteOrCommon(rhi::ResourceAccessType t) {
	return AccessTypeIsWriteType(t) || t == rhi::ResourceAccessType::Common;
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
	auto& autoAliasPackingStrategyLastFrame = rg.autoAliasPackingStrategyLastFrame;
	auto& m_getAutoAliasPackingStrategy = rg.m_getAutoAliasPackingStrategy;
	auto& persistentAliasPools = rg.persistentAliasPools;
	auto& m_framePasses = rg.m_framePasses;
	auto& _registry = rg._registry;
	auto& autoAliasPoolByID = rg.autoAliasPoolByID;
	auto& resourcesByID = rg.resourcesByID;
	auto& aliasPlacementPoolByID = rg.aliasPlacementPoolByID;
	auto& aliasPlacementRangesByID = rg.aliasPlacementRangesByID;
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
	const AutoAliasPackingStrategy previousPackingStrategy = autoAliasPackingStrategyLastFrame;
	const AutoAliasPackingStrategy packingStrategy = m_getAutoAliasPackingStrategy
		? m_getAutoAliasPackingStrategy()
		: AutoAliasPackingStrategy::GreedySweepLine;
	const bool packingStrategyChanged = previousPackingStrategy != packingStrategy;

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
		enum class Kind : uint8_t {
			Texture,
			Buffer
		};

		uint64_t resourceID = 0;
		uint64_t poolID = 0;
		uint64_t sizeBytes = 0;
		uint64_t alignment = 1;
		size_t firstUse = std::numeric_limits<size_t>::max();
		size_t lastUse = 0;
		bool firstUseIsWrite = false;
		bool manualPoolAssigned = false;
		Kind kind = Kind::Texture;
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
			const uint64_t resourceID = handle.GetGlobalResourceID();
			auto* texture = dynamic_cast<PixelBuffer*>(resource);
			if (!texture) {
				auto* buffer = dynamic_cast<Buffer*>(resource);
				if (!buffer || !buffer->IsAliasingAllowed()) {
					return;
				}

				if (buffer->GetAccessType() != rhi::HeapType::DeviceLocal) {
					return;
				}

				std::optional<uint64_t> poolID = buffer->GetAliasingPoolHint();
				if (!poolID.has_value()) {
					auto itAuto = autoAliasPoolByID.find(resourceID);
					if (itAuto != autoAliasPoolByID.end()) {
						poolID = itAuto->second;
					}
				}

				if (!poolID.has_value()) {
					return;
				}

				auto [it, inserted] = candidates.try_emplace(resourceID);
				auto& c = it->second;
				c.kind = Candidate::Kind::Buffer;
				c.resourceID = resourceID;
				c.poolID = poolID.value();
				if (usageOrder < c.firstUse) {
					c.firstUse = usageOrder;
					c.firstUseIsWrite = isWrite;
				}
				else if (usageOrder == c.firstUse) {
					c.firstUseIsWrite = c.firstUseIsWrite || isWrite;
				}
				c.lastUse = std::max(c.lastUse, usageOrder);
				c.manualPoolAssigned = c.manualPoolAssigned || buffer->GetAliasingPoolHint().has_value();

				if (inserted || c.sizeBytes == 0) {
					auto resourceDesc = BuildAliasBufferResourceDesc(
						buffer->GetBufferSize(),
						buffer->IsUnorderedAccessEnabled(),
						buffer->GetAccessType());
					rhi::ResourceAllocationInfo info{};
					device.GetResourceAllocationInfo(&resourceDesc, 1, &info);
					c.sizeBytes = info.sizeInBytes;
					c.alignment = std::max<uint64_t>(1, info.alignment);
				}
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

			auto [it, inserted] = candidates.try_emplace(resourceID);
			auto& c = it->second;
			c.kind = Candidate::Kind::Texture;
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
				collectHandle(req.resourceHandleAndRange.resource, AccessTypeIsWriteOrCommon(req.state.access));
			}
			for (auto const& t : p.resources.internalTransitions) {
				collectHandle(t.first.resource, true);
			}
		}
		else if (any.type == RenderGraph::PassType::Compute) {
			auto const& p = std::get<RenderGraph::ComputePassAndResources>(any.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				collectHandle(req.resourceHandleAndRange.resource, AccessTypeIsWriteOrCommon(req.state.access));
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
			if (a.sizeBytes != b.sizeBytes) {
				return a.sizeBytes > b.sizeBytes;
			}
			if (a.lastUse != b.lastUse) {
				return a.lastUse < b.lastUse;
			}
			return a.resourceID < b.resourceID;
		});

		struct ActiveRange {
			size_t lastUse = 0;
			uint64_t startByte = 0;
			uint64_t endByte = 0;
		};

		struct FreeRange {
			uint64_t startByte = 0;
			uint64_t endByte = 0;
		};

		struct Placement {
			uint64_t offset = 0;
			uint64_t sizeBytes = 0;
			uint64_t alignment = 1;
			size_t firstUse = 0;
			size_t lastUse = 0;
		};

		auto mergeFreeRanges = [](std::vector<FreeRange>& freeRanges) {
			if (freeRanges.empty()) {
				return;
			}

			std::sort(freeRanges.begin(), freeRanges.end(), [](const FreeRange& a, const FreeRange& b) {
				if (a.startByte != b.startByte) {
					return a.startByte < b.startByte;
				}
				return a.endByte < b.endByte;
			});

			size_t writeIndex = 0;
			for (size_t i = 1; i < freeRanges.size(); ++i) {
				auto& current = freeRanges[writeIndex];
				const auto& next = freeRanges[i];
				if (next.startByte <= current.endByte) {
					current.endByte = std::max(current.endByte, next.endByte);
				}
				else {
					++writeIndex;
					freeRanges[writeIndex] = next;
				}
			}

			freeRanges.resize(writeIndex + 1);
		};

		auto planWithGreedySweepLine = [&]() {
			std::vector<ActiveRange> activeRanges;
			std::vector<FreeRange> freeRanges;
			std::unordered_map<uint64_t, Placement> resourcePlacements;
			resourcePlacements.reserve(poolCandidates.size());

			uint64_t heapEnd = 0;
			uint64_t poolAlignment = 1;

			for (const auto& c : poolCandidates) {
				std::vector<ActiveRange> stillActive;
				stillActive.reserve(activeRanges.size() + 1);
				for (const auto& active : activeRanges) {
					if (active.lastUse < c.firstUse) {
						freeRanges.push_back(FreeRange{
							.startByte = active.startByte,
							.endByte = active.endByte,
							});
					}
					else {
						stillActive.push_back(active);
					}
				}
				activeRanges = std::move(stillActive);
				mergeFreeRanges(freeRanges);

				bool found = false;
				size_t bestRangeIndex = std::numeric_limits<size_t>::max();
				uint64_t bestStartByte = 0;
				uint64_t bestSlackBytes = std::numeric_limits<uint64_t>::max();

				for (size_t rangeIndex = 0; rangeIndex < freeRanges.size(); ++rangeIndex) {
					const auto& range = freeRanges[rangeIndex];
					const uint64_t alignedStart = AlignUpU64(range.startByte, c.alignment);
					const uint64_t alignedEnd = alignedStart + c.sizeBytes;
					if (alignedStart < range.startByte || alignedEnd > range.endByte) {
						continue;
					}

					const uint64_t slackBytes = range.endByte - alignedEnd;
					if (!found || slackBytes < bestSlackBytes || (slackBytes == bestSlackBytes && alignedStart < bestStartByte)) {
						found = true;
						bestRangeIndex = rangeIndex;
						bestStartByte = alignedStart;
						bestSlackBytes = slackBytes;
					}
				}

				uint64_t startByte = 0;
				if (found) {
					const auto selected = freeRanges[bestRangeIndex];
					startByte = bestStartByte;
					const uint64_t endByte = startByte + c.sizeBytes;

					std::vector<FreeRange> replacement;
					replacement.reserve(2);
					if (selected.startByte < startByte) {
						replacement.push_back(FreeRange{
							.startByte = selected.startByte,
							.endByte = startByte,
							});
					}
					if (endByte < selected.endByte) {
						replacement.push_back(FreeRange{
							.startByte = endByte,
							.endByte = selected.endByte,
							});
					}

					freeRanges.erase(freeRanges.begin() + bestRangeIndex);
					freeRanges.insert(freeRanges.end(), replacement.begin(), replacement.end());
				}
				else {
					startByte = AlignUpU64(heapEnd, c.alignment);
					heapEnd = startByte + c.sizeBytes;
				}

				const uint64_t endByte = startByte + c.sizeBytes;
				heapEnd = std::max(heapEnd, endByte);
				poolAlignment = std::max(poolAlignment, c.alignment);

				activeRanges.push_back(ActiveRange{
					.lastUse = c.lastUse,
					.startByte = startByte,
					.endByte = endByte,
					});

				resourcePlacements[c.resourceID] = Placement{
					.offset = startByte,
					.sizeBytes = c.sizeBytes,
					.alignment = c.alignment,
					.firstUse = c.firstUse,
					.lastUse = c.lastUse,
				};
			}

			return std::make_tuple(std::move(resourcePlacements), heapEnd, poolAlignment);
		};

		auto planWithBeamSearch = [&]() {
			struct PlannedRange {
				size_t candidateIndex = 0;
				uint64_t startByte = 0;
				uint64_t endByte = 0;
			};

			struct BeamState {
				std::vector<PlannedRange> placedRanges;
				std::vector<uint8_t> placedMask;
				uint64_t heapSize = 0;
				double score = 0.0;
			};

			std::unordered_map<uint64_t, Placement> bestPlacements;
			uint64_t bestHeapSize = std::numeric_limits<uint64_t>::max();
			uint64_t poolAlignment = 1;
			for (const auto& c : poolCandidates) {
				poolAlignment = std::max(poolAlignment, c.alignment);
			}

			auto [greedyPlacements, greedyHeapSize, greedyAlignment] = planWithGreedySweepLine();
			bestPlacements = std::move(greedyPlacements);
			bestHeapSize = greedyHeapSize;
			poolAlignment = std::max(poolAlignment, greedyAlignment);

			std::vector<size_t> candidateOrder;
			candidateOrder.reserve(poolCandidates.size());
			for (size_t i = 0; i < poolCandidates.size(); ++i) {
				candidateOrder.push_back(i);
			}

			std::sort(candidateOrder.begin(), candidateOrder.end(), [&](size_t aIdx, size_t bIdx) {
				const auto& a = poolCandidates[aIdx];
				const auto& b = poolCandidates[bIdx];
				const uint64_t aSpan = static_cast<uint64_t>(a.lastUse - a.firstUse + 1ull);
				const uint64_t bSpan = static_cast<uint64_t>(b.lastUse - b.firstUse + 1ull);
				const uint64_t aWeight = a.sizeBytes * aSpan;
				const uint64_t bWeight = b.sizeBytes * bSpan;
				if (aWeight != bWeight) {
					return aWeight > bWeight;
				}
				if (a.sizeBytes != b.sizeBytes) {
					return a.sizeBytes > b.sizeBytes;
				}
				if (a.firstUse != b.firstUse) {
					return a.firstUse < b.firstUse;
				}
				return a.resourceID < b.resourceID;
			});

			auto lifetimesOverlap = [&](const Candidate& lhs, const Candidate& rhs) {
				return !(lhs.lastUse < rhs.firstUse || rhs.lastUse < lhs.firstUse);
			};

			auto intervalOverlaps = [](uint64_t aStart, uint64_t aEnd, uint64_t bStart, uint64_t bEnd) {
				const uint64_t overlapStart = std::max(aStart, bStart);
				const uint64_t overlapEnd = std::min(aEnd, bEnd);
				return overlapStart < overlapEnd;
			};

			auto buildPlacementMap = [&](const std::vector<PlannedRange>& placedRanges) {
				std::unordered_map<uint64_t, Placement> out;
				out.reserve(placedRanges.size());
				for (const auto& placed : placedRanges) {
					const auto& c = poolCandidates[placed.candidateIndex];
					out[c.resourceID] = Placement{
						.offset = placed.startByte,
						.sizeBytes = c.sizeBytes,
						.alignment = c.alignment,
						.firstUse = c.firstUse,
						.lastUse = c.lastUse,
					};
				}
				return out;
			};

			constexpr size_t kBeamWidth = 24;
			constexpr size_t kCandidateStartsPerState = 8;
			bool truncated = false;

			auto scoreState = [](const BeamState& state) {
				double wastePenalty = 0.0;
				for (const auto& placed : state.placedRanges) {
					wastePenalty += static_cast<double>(placed.endByte - placed.startByte);
				}
				return static_cast<double>(state.heapSize) + (0.000001 * wastePenalty);
			};

			BeamState initialState{};
			initialState.heapSize = 0;
			initialState.placedMask.assign(poolCandidates.size(), 0);
			initialState.placedRanges.reserve(poolCandidates.size());
			initialState.score = 0.0;

			std::vector<BeamState> beam;
			beam.push_back(std::move(initialState));

			for (size_t depth = 0; depth < poolCandidates.size() && !beam.empty(); ++depth) {
				std::vector<BeamState> nextBeam;
				nextBeam.reserve(beam.size() * kCandidateStartsPerState);

				for (const auto& state : beam) {
					size_t nextCandidateIndex = std::numeric_limits<size_t>::max();
					for (size_t orderedIndex : candidateOrder) {
						if (!state.placedMask[orderedIndex]) {
							nextCandidateIndex = orderedIndex;
							break;
						}
					}
					if (nextCandidateIndex == std::numeric_limits<size_t>::max()) {
						if (state.heapSize < bestHeapSize) {
							bestHeapSize = state.heapSize;
							bestPlacements = buildPlacementMap(state.placedRanges);
						}
						continue;
					}

					const auto& nextCandidate = poolCandidates[nextCandidateIndex];
					std::vector<uint64_t> candidateStarts;
					candidateStarts.reserve(1 + state.placedRanges.size());
					candidateStarts.push_back(0ull);

					for (const auto& placed : state.placedRanges) {
						const auto& placedCandidate = poolCandidates[placed.candidateIndex];
						if (lifetimesOverlap(nextCandidate, placedCandidate)) {
							candidateStarts.push_back(placed.endByte);
						}
					}

					std::vector<std::pair<uint64_t, uint64_t>> feasibleStarts;
					feasibleStarts.reserve(candidateStarts.size());
					std::unordered_set<uint64_t> dedupStarts;
					dedupStarts.reserve(candidateStarts.size() * 2 + 1);

					for (uint64_t rawStart : candidateStarts) {
						const uint64_t alignedStart = AlignUpU64(rawStart, nextCandidate.alignment);
						if (!dedupStarts.insert(alignedStart).second) {
							continue;
						}

						const uint64_t alignedEnd = alignedStart + nextCandidate.sizeBytes;
						bool conflicts = false;
						for (const auto& placed : state.placedRanges) {
							const auto& placedCandidate = poolCandidates[placed.candidateIndex];
							if (!lifetimesOverlap(nextCandidate, placedCandidate)) {
								continue;
							}
							if (intervalOverlaps(alignedStart, alignedEnd, placed.startByte, placed.endByte)) {
								conflicts = true;
								break;
							}
						}

						if (!conflicts) {
							const uint64_t resultingHeap = std::max(state.heapSize, alignedEnd);
							if (resultingHeap < bestHeapSize) {
								feasibleStarts.emplace_back(alignedStart, resultingHeap);
							}
						}
					}

					if (feasibleStarts.empty()) {
						const uint64_t appendStart = AlignUpU64(state.heapSize, nextCandidate.alignment);
						const uint64_t appendEnd = appendStart + nextCandidate.sizeBytes;
						if (appendEnd < bestHeapSize) {
							feasibleStarts.emplace_back(appendStart, appendEnd);
						}
					}

					std::sort(feasibleStarts.begin(), feasibleStarts.end(), [](const auto& a, const auto& b) {
						if (a.second != b.second) {
							return a.second < b.second;
						}
						return a.first < b.first;
					});

					if (feasibleStarts.size() > kCandidateStartsPerState) {
						feasibleStarts.resize(kCandidateStartsPerState);
						truncated = true;
					}

					for (const auto& [startByte, resultingHeap] : feasibleStarts) {
						BeamState newState = state;
						newState.placedMask[nextCandidateIndex] = 1;
						newState.heapSize = resultingHeap;
						newState.placedRanges.push_back(PlannedRange{
							.candidateIndex = nextCandidateIndex,
							.startByte = startByte,
							.endByte = startByte + nextCandidate.sizeBytes,
						});
						newState.score = scoreState(newState);
						nextBeam.push_back(std::move(newState));
					}
				}

				if (nextBeam.empty()) {
					break;
				}

				std::sort(nextBeam.begin(), nextBeam.end(), [](const BeamState& a, const BeamState& b) {
					if (a.score != b.score) {
						return a.score < b.score;
					}
					return a.heapSize < b.heapSize;
				});

				if (nextBeam.size() > kBeamWidth) {
					nextBeam.resize(kBeamWidth);
					truncated = true;
				}

				beam = std::move(nextBeam);
			}

			for (const auto& state : beam) {
				if (state.placedRanges.size() == poolCandidates.size() && state.heapSize < bestHeapSize) {
					bestHeapSize = state.heapSize;
					bestPlacements = buildPlacementMap(state.placedRanges);
				}
			}

			if (bestPlacements.empty()) {
				auto [fallbackPlacements, fallbackHeapSize, fallbackAlignment] = planWithGreedySweepLine();
				bestPlacements = std::move(fallbackPlacements);
				bestHeapSize = fallbackHeapSize;
				poolAlignment = std::max(poolAlignment, fallbackAlignment);
				truncated = true;
			}

			return std::make_tuple(std::move(bestPlacements), bestHeapSize, poolAlignment, truncated);
		};

		std::unordered_map<uint64_t, Placement> placements;
		uint64_t heapSize = 0;
		uint64_t poolAlignment = 1;
		switch (packingStrategy) {
		case AutoAliasPackingStrategy::GreedySweepLine: {
			auto [plannedPlacements, plannedHeapSize, plannedPoolAlignment] = planWithGreedySweepLine();
			placements = std::move(plannedPlacements);
			heapSize = plannedHeapSize;
			poolAlignment = plannedPoolAlignment;
			break;
		}
		case AutoAliasPackingStrategy::BranchAndBound: {
			auto [plannedPlacements, plannedHeapSize, plannedPoolAlignment, searchTruncated] = planWithBeamSearch();
			placements = std::move(plannedPlacements);
			heapSize = plannedHeapSize;
			poolAlignment = plannedPoolAlignment;
			if (searchTruncated) {
				spdlog::info(
					"RG alias beam search truncated: pool={} candidates={} resultingRequiredBytes={}",
					poolID,
					poolCandidates.size(),
					heapSize);
			}
			break;
		}
		default:
			throw std::runtime_error("Unsupported alias packing strategy");
		}

		if (heapSize == 0) {
			continue;
		}
		poolDebug.requiredBytes = heapSize;

		autoAliasPlannerStats.pooledIndependentBytes += poolIndependentBytes;

		auto& poolState = persistentAliasPools[poolID];
		const bool needsInitialAllocation = !static_cast<bool>(poolState.allocation);
		const bool needsLargerHeap = heapSize > poolState.capacityBytes;
		const bool needsHigherAlignment = poolAlignment > poolState.alignment;
		const bool shouldShrinkForStrategyChange =
			packingStrategyChanged &&
			!needsInitialAllocation &&
			poolState.capacityBytes > heapSize;

		if (needsInitialAllocation || needsLargerHeap || needsHigherAlignment || shouldShrinkForStrategyChange) {
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
				"RG alias pool {}: pool={} capacity={} required={} alignment={} placements={} generation={}",
				needsInitialAllocation
					? "allocated"
					: (shouldShrinkForStrategyChange ? "resized" : "grew"),
				poolID,
				newCapacity,
				heapSize,
				poolAlignment,
				placements.size(),
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
			auto placementIt = placements.find(c.resourceID);
			if (placementIt == placements.end()) {
				throw std::runtime_error("Missing alias placement for candidate resource");
			}
			const auto& placement = placementIt->second;

			auto itResDebug = resourcesByID.find(c.resourceID);
			const std::string resourceNameDebug = (itResDebug != resourcesByID.end() && itResDebug->second)
				? itResDebug->second->GetName()
				: std::string("<unknown>");

			poolDebug.ranges.push_back(AutoAliasPoolRangeDebug{
				.resourceID = c.resourceID,
				.resourceName = resourceNameDebug,
				.startByte = placement.offset,
				.endByte = placement.offset + c.sizeBytes,
				.sizeBytes = c.sizeBytes,
				.firstUse = c.firstUse,
				.lastUse = c.lastUse,
				.overlapsByteRange = false
				});

			if (c.kind == Candidate::Kind::Texture) {
				PixelBuffer::MaterializeOptions options{};
				options.aliasPlacement = TextureAliasPlacement{
					.allocation = allocation,
					.offset = placement.offset,
					.poolID = poolID,
				};
				aliasMaterializeOptionsByID[c.resourceID] = RenderGraph::ResourceMaterializeOptions(options);
			}
			else {
				BufferBase::MaterializeOptions options{};
				options.aliasPlacement = BufferAliasPlacement{
					.allocation = allocation,
					.offset = placement.offset,
					.poolID = poolID,
				};
				aliasMaterializeOptionsByID[c.resourceID] = RenderGraph::ResourceMaterializeOptions(options);
			}
			aliasPlacementPoolByID[c.resourceID] = poolID;
			aliasPlacementRangesByID[c.resourceID] = AliasPlacementRange{
				.poolID = poolID,
				.startByte = placement.offset,
				.endByte = placement.offset + c.sizeBytes,
			};

			auto itResName = resourcesByID.find(c.resourceID);
			const std::string resourceName = (itResName != resourcesByID.end() && itResName->second)
				? itResName->second->GetName()
				: std::string("<unknown>");
			spdlog::info(
				"RG alias bind: pool={} resourceId={} name='{}' kind={} offset={} size={} firstUse={} lastUse={}",
				poolID,
				c.resourceID,
				resourceName,
				c.kind == Candidate::Kind::Texture ? "texture" : "buffer",
				placement.offset,
				c.sizeBytes,
				c.firstUse,
				c.lastUse);

			const uint64_t newSignature = BuildAliasPlacementSignatureValue(
				poolID,
				placement.offset,
				placement.offset + c.sizeBytes,
				poolState.generation);
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
				auto buffer = std::dynamic_pointer_cast<Buffer>(itRes->second);
				if (buffer && buffer->IsMaterialized()) {
					auto itSig = aliasPlacementSignatureByID.find(c.resourceID);
					if (itSig == aliasPlacementSignatureByID.end() || itSig->second != newSignature) {
						buffer->Dematerialize();
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

					auto buffer = std::dynamic_pointer_cast<Buffer>(itRes->second);
					if (buffer && buffer->IsMaterialized()) {
						buffer->Dematerialize();
					}
				}
			}

			for (uint64_t resourceID : resourcesToClear) {
				aliasPlacementPoolByID.erase(resourceID);
				aliasPlacementRangesByID.erase(resourceID);
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

	autoAliasPackingStrategyLastFrame = packingStrategy;
}

void rg::alias::RenderGraphAliasingSubsystem::ApplyAliasQueueSynchronization(RenderGraph& rg) const {
	auto& batches = rg.batches;
	auto& aliasPlacementRangesByID = rg.aliasPlacementRangesByID;
	struct QueueUsage {
		bool usesRender = false;
		bool usesCompute = false;
	};

	struct RangeOwner {
		uint64_t resourceID = 0;
		uint64_t startByte = 0;
		uint64_t endByte = 0;
		size_t batchIndex = 0;
		QueueUsage usage;
	};

	std::unordered_map<uint64_t, std::vector<RangeOwner>> lastOwnerByPool;

	auto rangesOverlap = [](uint64_t aStart, uint64_t aEnd, uint64_t bStart, uint64_t bEnd) {
		const uint64_t overlapStart = std::max(aStart, bStart);
		const uint64_t overlapEnd = std::min(aEnd, bEnd);
		return overlapStart < overlapEnd;
	};

	auto markUsage = [](QueueUsage& usage, bool render, bool compute) {
		usage.usesRender = usage.usesRender || render;
		usage.usesCompute = usage.usesCompute || compute;
	};

	for (size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
		auto& batch = batches[batchIndex];
		std::unordered_map<uint64_t, QueueUsage> usageByResourceID;
		usageByResourceID.reserve(batch.renderPasses.size() + batch.computePasses.size());

		auto accumulateFromReqs = [&](const std::vector<ResourceRequirement>& reqs, bool render, bool compute) {
			for (auto const& req : reqs) {
				const uint64_t resourceID = req.resourceHandleAndRange.resource.GetGlobalResourceID();
				auto itPlacement = aliasPlacementRangesByID.find(resourceID);
				if (itPlacement == aliasPlacementRangesByID.end()) {
					continue;
				}
				markUsage(usageByResourceID[resourceID], render, compute);
			}
		};

		for (auto const& rp : batch.renderPasses) {
			accumulateFromReqs(rp.resources.frameResourceRequirements, true, false);
		}
		for (auto const& cp : batch.computePasses) {
			accumulateFromReqs(cp.resources.frameResourceRequirements, false, true);
		}

		for (auto const& [resourceID, usage] : usageByResourceID) {
			auto placementIt = aliasPlacementRangesByID.find(resourceID);
			if (placementIt == aliasPlacementRangesByID.end()) {
				continue;
			}

			const auto& placement = placementIt->second;
			auto& previousOwners = lastOwnerByPool[placement.poolID];

			for (const auto& prevOwner : previousOwners) {
				if (prevOwner.resourceID == resourceID) {
					continue;
				}

				if (!rangesOverlap(
					placement.startByte,
					placement.endByte,
					prevOwner.startByte,
					prevOwner.endByte)) {
					continue;
				}

				auto& prevBatch = batches[prevOwner.batchIndex];
				if (prevOwner.usage.usesRender && usage.usesCompute) {
					prevBatch.renderCompletionSignal = true;
					batch.computeQueueWaitOnRenderQueueBeforeTransition = true;
					batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue =
						std::max(
							batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue,
							prevBatch.renderCompletionFenceValue);
				}

				if (prevOwner.usage.usesCompute && usage.usesRender) {
					prevBatch.computeCompletionSignal = true;
					batch.renderQueueWaitOnComputeQueueBeforeTransition = true;
					batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue =
						std::max(
							batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue,
							prevBatch.computeCompletionFenceValue);
				}
			}

			previousOwners.erase(
				std::remove_if(
					previousOwners.begin(),
					previousOwners.end(),
					[&](const RangeOwner& owner) {
						return rangesOverlap(
							placement.startByte,
							placement.endByte,
							owner.startByte,
							owner.endByte);
					}),
				previousOwners.end());

			previousOwners.push_back(RangeOwner{
				.resourceID = resourceID,
				.startByte = placement.startByte,
				.endByte = placement.endByte,
				.batchIndex = batchIndex,
				.usage = usage,
			});
		}
	}
}

