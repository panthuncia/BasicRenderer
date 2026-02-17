#include "Render/RenderGraph/Aliasing/RenderGraphAliasingSubsystem.h"

#include "Render/RenderGraph/RenderGraph.h"
#include "Managers/Singletons/DeletionManager.h"

namespace rg::alias {

AutoAliasDebugSnapshot RenderGraphAliasingSubsystem::BuildDebugSnapshot(
	AutoAliasMode mode,
	const AutoAliasPlannerStats& plannerStats,
	const std::vector<AutoAliasReasonCount>& exclusionReasons,
	const std::vector<AutoAliasPoolDebug>& poolDebug) const
{
	AutoAliasDebugSnapshot out{};
	out.mode = mode;
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

std::vector<uint64_t> RenderGraphAliasingSubsystem::GetSchedulingEquivalentIDs(
	uint64_t resourceID,
	const std::unordered_map<uint64_t, uint64_t>& aliasPlacementSignatureByID) const
{
	auto it = aliasPlacementSignatureByID.find(resourceID);
	if (it == aliasPlacementSignatureByID.end()) {
		return { resourceID };
	}

	const uint64_t signature = it->second;
	std::vector<uint64_t> out;
	out.reserve(8);

	for (const auto& [id, sig] : aliasPlacementSignatureByID) {
		if (sig == signature) {
			out.push_back(id);
		}
	}

	if (out.empty()) {
		out.push_back(resourceID);
	}

	return out;
}

void RenderGraphAliasingSubsystem::ResetPerFrameState(RenderGraph& rg) const {
	rg.aliasMaterializeOptionsByID.clear();
	rg.aliasActivationPending.clear();
	rg.autoAliasPoolByID.clear();
	rg.autoAliasExclusionReasonByID.clear();
	rg.autoAliasExclusionReasonSummary.clear();
	rg.autoAliasPlannerStats = {};
	rg.autoAliasModeLastFrame = AutoAliasMode::Off;
}

void RenderGraphAliasingSubsystem::ResetPersistentState(RenderGraph& rg) const {
	ResetPerFrameState(rg);
	rg.aliasPlacementSignatureByID.clear();
	rg.aliasPlacementPoolByID.clear();

	for (auto& [poolID, poolState] : rg.persistentAliasPools) {
		(void)poolID;
		if (poolState.allocation) {
			DeletionManager::GetInstance().MarkForDelete(std::move(poolState.allocation));
		}
	}
	rg.persistentAliasPools.clear();
	rg.aliasPoolPlanFrameIndex = 0;
}

} // namespace rg::alias
