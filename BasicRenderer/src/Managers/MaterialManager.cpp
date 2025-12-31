#include "Managers/MaterialManager.h"
#include "../generated/BuiltinResources.h"

// TODO: Use LazyDynamicStructuredBuffer and active indices buffer like draw calls? Would reduce number of no-op indirect arguments
MaterialManager::MaterialManager() {
	auto& rm = ResourceManager::GetInstance();

	// Primary material data buffer
	m_perMaterialDataBuffer = DynamicStructuredBuffer<PerMaterialCB>::CreateShared(m_compileFlagsSlotsUsed, "Builtin::PerMaterialDataBuffer", true);

	// Visibility buffer resources
    m_materialPixelCountBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(m_compileFlagsSlotsUsed, "VisUtil::MaterialPixelCountBuffer", true);
    m_materialOffsetBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(m_compileFlagsSlotsUsed, "VisUtil::MaterialOffsetBuffer", true);
	m_materialWriteCursorBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(m_compileFlagsSlotsUsed, "VisUtil::MaterialWriteCursorBuffer", true);

	// Per-block arrays for hierarchical scan
	const uint32_t numBlocks = (m_compileFlagsSlotsUsed + kScanBlockSize - 1u) / kScanBlockSize;
	m_blockSumsBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(std::max(1u, numBlocks), "VisUtil::BlockSumsBuffer", true);
	m_scannedBlockSumsBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(std::max(1u, numBlocks), "VisUtil::ScannedBlockSumsBuffer", true);

	// Indirect command buffer for material evaluation
	m_materialEvaluationCommandBuffer = DynamicStructuredBuffer<MaterialEvaluationIndirectCommand>::CreateShared(m_compileFlagsSlotsUsed, "IndirectCommandBuffers::MaterialEvaluationCommandBuffer", true);

	m_resources["Builtin::VisUtil::MaterialPixelCountBuffer"] = m_materialPixelCountBuffer;
	m_resources["Builtin::VisUtil::MaterialOffsetBuffer"] = m_materialOffsetBuffer;
	m_resources["Builtin::VisUtil::MaterialWriteCursorBuffer"] = m_materialWriteCursorBuffer;
	m_resources["Builtin::VisUtil::BlockSumsBuffer"] = m_blockSumsBuffer;
	m_resources["Builtin::VisUtil::ScannedBlockSumsBuffer"] = m_scannedBlockSumsBuffer;
	m_resources["Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer"] = m_materialEvaluationCommandBuffer;
	m_resources[Builtin::PerMaterialDataBuffer] = m_perMaterialDataBuffer;
}

void MaterialManager::IncrementMaterialUsageCount(Material& material) {
	//std::lock_guard<std::mutex> lock(m_materialSlotMappingMutex);
	auto& flags = material.Technique().compileFlags;
	unsigned int flagsSlot = GetCompileFlagsSlot(flags);
	m_compileFlagsUsageCounts[flagsSlot]++;
	uint32_t materialID = material.GetMaterialID();
	material.SetCompileFlagsID(flagsSlot);
	unsigned int materialSlot = GetMaterialSlot(materialID, material.GetData());
	m_materialUsageCounts[materialSlot]++;
}

void MaterialManager::DecrementMaterialUsageCount(const Material& material) {
	//std::lock_guard<std::mutex> lock(m_materialSlotMappingMutex);
	auto& flags = material.Technique().compileFlags;
	unsigned int flagsSlot = GetCompileFlagsSlot(flags);
	m_compileFlagsUsageCounts[flagsSlot]--;
	if (m_compileFlagsUsageCounts[flagsSlot] == 0) {
		m_freeCompileFlagsSlots.push_back(flagsSlot);
		m_compileFlagsSlotMapping.erase(flags);
		m_activeCompileFlagsSlots.erase(std::remove(m_activeCompileFlagsSlots.begin(), m_activeCompileFlagsSlots.end(), flagsSlot), m_activeCompileFlagsSlots.end());
		m_activeCompileFlags.erase(std::remove(m_activeCompileFlags.begin(), m_activeCompileFlags.end(), flags), m_activeCompileFlags.end());
	}

	m_materialUsageCounts[GetMaterialSlot(material.GetMaterialID())]--;
	if (m_materialUsageCounts[GetMaterialSlot(material.GetMaterialID())] == 0) {
		unsigned int materialSlot = GetMaterialSlot(material.GetMaterialID());
		m_freeMaterialSlots.push_back(materialSlot);
		m_materialIDSlotMapping.erase(material.GetMaterialID());
	}
}

// TODO: Don't grow buffers one slot at a time
// TODO: C++26 will allow optional references
unsigned int MaterialManager::GetMaterialSlot(unsigned int materialID, std::optional<PerMaterialCB> data) {
	unsigned int slot;
	auto it = m_materialIDSlotMapping.find(materialID);
	if (it != m_materialIDSlotMapping.end()) {
		slot = it->second;
		return slot;
	}
	if (!m_freeMaterialSlots.empty()) {
		slot = m_freeMaterialSlots.back();
		m_freeMaterialSlots.pop_back();
		if (data.has_value()) {
			m_perMaterialDataBuffer->UpdateAt(slot, data.value());
		}
	}
	else {
		slot = m_materialSlotsUsed++;
		m_materialUsageCounts.push_back(0);
		// Resize resources to accommodate new material slot
		m_perMaterialDataBuffer->Resize(m_materialSlotsUsed);
		if (data.has_value()) {
			m_perMaterialDataBuffer->UpdateAt(slot, data.value());
		}
	}
	m_materialIDSlotMapping[materialID] = slot;
	return slot;
}

unsigned int MaterialManager::GetCompileFlagsSlot(MaterialCompileFlags flags) {
	unsigned int slot;
	auto it = m_compileFlagsSlotMapping.find(flags);
	if (it != m_compileFlagsSlotMapping.end()) {
		slot = it->second;
		return slot;
	}
	if (!m_freeCompileFlagsSlots.empty()) {
		slot = m_freeCompileFlagsSlots.back();
		m_freeCompileFlagsSlots.pop_back();
	}
	else {
		slot = m_nextCompileFlagsSlot++;
		m_compileFlagsSlotsUsed++;
		m_compileFlagsUsageCounts.push_back(0);
		// Resize resources to accommodate new material slot
		m_materialPixelCountBuffer->Resize(m_compileFlagsSlotsUsed);
		m_materialOffsetBuffer->Resize(m_compileFlagsSlotsUsed);
		m_materialWriteCursorBuffer->Resize(m_compileFlagsSlotsUsed);
		m_materialEvaluationCommandBuffer->Resize(m_compileFlagsSlotsUsed);

		// Resize per-block buffers to match new block count
		const uint32_t numBlocks = (m_compileFlagsSlotsUsed + kScanBlockSize - 1u) / kScanBlockSize;
		m_blockSumsBuffer->Resize(std::max(1u, numBlocks));
		m_scannedBlockSumsBuffer->Resize(std::max(1u, numBlocks));		
	}
	m_compileFlagsSlotMapping[flags] = slot;
	if (std::find(m_activeCompileFlagsSlots.begin(), m_activeCompileFlagsSlots.end(), slot) == m_activeCompileFlagsSlots.end()) {
		m_activeCompileFlagsSlots.push_back(slot);
	}
	if (std::find(m_activeCompileFlags.begin(), m_activeCompileFlags.end(), flags) == m_activeCompileFlags.end()) {
		m_activeCompileFlags.push_back(flags);
	}
	return slot;
}

std::shared_ptr<Resource> MaterialManager::ProvideResource(ResourceIdentifier const& key) {
	return m_resources[key];
}

std::vector<ResourceIdentifier> MaterialManager::GetSupportedKeys() {
	std::vector<ResourceIdentifier> keys;
	keys.reserve(m_resources.size());
	for (auto const& [key, _] : m_resources) {
		keys.push_back(key);
	}

	return keys;
}