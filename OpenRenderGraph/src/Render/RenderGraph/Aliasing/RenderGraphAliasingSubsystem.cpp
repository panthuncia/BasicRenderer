#include "Render/RenderGraph/Aliasing/RenderGraphAliasingSubsystem.h"

#include <algorithm>

#include "Render/RenderGraph/RenderGraph.h"
#include "Managers/Singletons/DeletionManager.h"

rg::alias::AutoAliasDebugSnapshot rg::alias::RenderGraphAliasingSubsystem::BuildDebugSnapshot(
	AutoAliasMode mode,
	AutoAliasPackingStrategy packingStrategy,
	const rg::alias::AutoAliasPlannerStats& plannerStats,
	const std::vector<rg::alias::AutoAliasReasonCount>& exclusionReasons,
	const std::vector<rg::alias::AutoAliasPoolDebug>& poolDebug) const
{
	rg::alias::AutoAliasDebugSnapshot out{};
	out.mode = mode;
	out.packingStrategy = packingStrategy;
	out.candidatesSeen = plannerStats.candidatesSeen;
	out.manuallyAssigned = plannerStats.manuallyAssigned;
	out.autoAssigned = plannerStats.autoAssigned;
	out.excluded = plannerStats.excluded;
	out.candidateBytes = plannerStats.candidateBytes;
	out.autoAssignedBytes = plannerStats.autoAssignedBytes;
	out.pooledIndependentBytes = plannerStats.pooledIndependentBytes;
	out.pooledActualBytes = plannerStats.pooledActualBytes;
	out.pooledSavedBytes = plannerStats.pooledSavedBytes;
	out.exclusionReasons = exclusionReasons;
	out.poolDebug = poolDebug;
	return out;
}

std::vector<uint64_t> rg::alias::RenderGraphAliasingSubsystem::GetSchedulingEquivalentIDs(
	uint64_t resourceID,
	const std::unordered_map<uint64_t, rg::alias::AliasPlacementRange>& aliasPlacementRangesByID) const
{
	auto it = aliasPlacementRangesByID.find(resourceID);
	if (it == aliasPlacementRangesByID.end()) {
		return { resourceID };
	}

	const rg::alias::AliasPlacementRange& placement = it->second;
	std::vector<uint64_t> out;
	out.reserve(8);

	for (const auto& [id, otherPlacement] : aliasPlacementRangesByID) {
		if (otherPlacement.poolID != placement.poolID) {
			continue;
		}

		const uint64_t overlapStart = (std::max)(placement.startByte, otherPlacement.startByte);
		const uint64_t overlapEnd = (std::min)(placement.endByte, otherPlacement.endByte);
		if (overlapStart < overlapEnd) {
			out.push_back(id);
		}
	}

	if (out.empty()) {
		out.push_back(resourceID);
	}

	return out;
}

void rg::alias::RenderGraphAliasingSubsystem::ResetPerFrameState(RenderGraph& renderGraph) const {
	renderGraph.aliasMaterializeOptionsByID.clear();
	renderGraph.aliasActivationPending.clear();
	renderGraph.autoAliasPoolByID.clear();
	renderGraph.autoAliasExclusionReasonByID.clear();
	renderGraph.autoAliasExclusionReasonSummary.clear();
	renderGraph.autoAliasPlannerStats = {};
	renderGraph.autoAliasModeLastFrame = AutoAliasMode::Off;
}

void rg::alias::RenderGraphAliasingSubsystem::ResetPersistentState(RenderGraph& renderGraph) const {
	ResetPerFrameState(renderGraph);
	renderGraph.aliasPlacementSignatureByID.clear();
	renderGraph.aliasPlacementRangesByID.clear();
	renderGraph.aliasPlacementPoolByID.clear();

	for (auto& [poolID, poolState] : renderGraph.persistentAliasPools) {
		(void)poolID;
		if (poolState.allocation) {
			DeletionManager::GetInstance().MarkForDelete(std::move(poolState.allocation));
		}
	}
	renderGraph.persistentAliasPools.clear();
	renderGraph.aliasPoolPlanFrameIndex = 0;
}
