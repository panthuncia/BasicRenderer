#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resources/TrackedAllocation.h"

enum class AutoAliasMode : uint8_t;
enum class AutoAliasPackingStrategy : uint8_t;
class RenderGraph;

namespace rg::alias {

struct AutoAliasReasonCount {
	std::string reason;
	size_t count = 0;
};

struct AutoAliasPoolRangeDebug {
	uint64_t resourceID = 0;
	std::string resourceName;
	uint64_t startByte = 0;
	uint64_t endByte = 0;
	uint64_t sizeBytes = 0;
	size_t firstUse = 0;
	size_t lastUse = 0;
	bool overlapsByteRange = false;
};

struct AutoAliasPoolDebug {
	uint64_t poolID = 0;
	uint64_t requiredBytes = 0;
	uint64_t reservedBytes = 0;
	std::vector<AutoAliasPoolRangeDebug> ranges;
};

struct AutoAliasPlannerStats {
	size_t candidatesSeen = 0;
	size_t manuallyAssigned = 0;
	size_t autoAssigned = 0;
	size_t excluded = 0;
	uint64_t candidateBytes = 0;
	uint64_t autoAssignedBytes = 0;
	uint64_t pooledIndependentBytes = 0;
	uint64_t pooledActualBytes = 0;
	uint64_t pooledSavedBytes = 0;
};

struct AutoAliasDebugSnapshot {
	AutoAliasMode mode{};
	AutoAliasPackingStrategy packingStrategy{};
	size_t candidatesSeen = 0;
	size_t manuallyAssigned = 0;
	size_t autoAssigned = 0;
	size_t excluded = 0;
	uint64_t candidateBytes = 0;
	uint64_t autoAssignedBytes = 0;
	uint64_t pooledIndependentBytes = 0;
	uint64_t pooledActualBytes = 0;
	uint64_t pooledSavedBytes = 0;
	std::vector<AutoAliasReasonCount> exclusionReasons;
	std::vector<AutoAliasPoolDebug> poolDebug;
};

struct PersistentAliasPoolState {
	TrackedHandle allocation;
	uint64_t capacityBytes = 0;
	uint64_t alignment = 1;
	uint64_t generation = 0;
	uint64_t lastUsedFrame = 0;
	bool usedThisFrame = false;
};

struct AliasPlacementRange {
	uint64_t poolID = 0;
	uint64_t startByte = 0;
	uint64_t endByte = 0;
};

struct AliasSchedulingNode {
	size_t passIndex = 0;
	uint32_t originalOrder = 0;
	uint32_t indegree = 0;
	uint32_t criticality = 0;
	std::vector<size_t> out;
};

class RenderGraphAliasingSubsystem {
public:
	AutoAliasDebugSnapshot BuildDebugSnapshot(
		AutoAliasMode mode,
		AutoAliasPackingStrategy packingStrategy,
		const AutoAliasPlannerStats& plannerStats,
		const std::vector<AutoAliasReasonCount>& exclusionReasons,
		const std::vector<AutoAliasPoolDebug>& poolDebug) const;

	std::vector<uint64_t> GetSchedulingEquivalentIDs(
		uint64_t resourceID,
		const std::unordered_map<uint64_t, AliasPlacementRange>& aliasPlacementRangesByID) const;

	void ResetPerFrameState(RenderGraph& rg) const;
	void ResetPersistentState(RenderGraph& rg) const;
	void AutoAssignAliasingPools(RenderGraph& rg, const std::vector<AliasSchedulingNode>& nodes) const;
	void BuildAliasPlanAfterDag(RenderGraph& rg, const std::vector<AliasSchedulingNode>& nodes) const;
	void ApplyAliasQueueSynchronization(RenderGraph& rg) const;
};

} // namespace rg::alias
