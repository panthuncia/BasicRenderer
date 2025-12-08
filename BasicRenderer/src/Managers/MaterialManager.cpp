#include "Managers/MaterialManager.h"

MaterialManager::MaterialManager() {
	auto& rm = ResourceManager::GetInstance();

    m_materialPixelCountBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(m_materialSlotsUsed, L"VisUtil::MaterialPixelCountBuffer", true);
    m_materialOffsetBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(m_materialSlotsUsed, L"VisUtil::MaterialOffsetBuffer", true);
	m_materialWriteCursorBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(m_materialSlotsUsed, L"VisUtil::MaterialWriteCursorBuffer", true);

	// Per-block arrays for hierarchical scan
	const uint32_t numBlocks = (m_materialSlotsUsed + kScanBlockSize - 1u) / kScanBlockSize;
	m_blockSumsBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(std::max(1u, numBlocks), L"VisUtil::BlockSumsBuffer", true);
	m_scannedBlockSumsBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(std::max(1u, numBlocks), L"VisUtil::ScannedBlockSumsBuffer", true);

	// Indirect command buffer for material evaluation
	m_materialEvaluationCommandBuffer = rm.CreateIndexedDynamicStructuredBuffer<MaterialEvaluationIndirectCommand>(m_materialSlotsUsed, L"IndirectCommandBuffers::MaterialEvaluationCommandBuffer", true);

	m_resources["Builtin::VisUtil::MaterialPixelCountBuffer"] = m_materialPixelCountBuffer;
	m_resources["Builtin::VisUtil::MaterialOffsetBuffer"] = m_materialOffsetBuffer;
	m_resources["Builtin::VisUtil::MaterialWriteCursorBuffer"] = m_materialWriteCursorBuffer;
	m_resources["Builtin::VisUtil::BlockSumsBuffer"] = m_blockSumsBuffer;
	m_resources["Builtin::VisUtil::ScannedBlockSumsBuffer"] = m_scannedBlockSumsBuffer;
	m_resources["Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer"] = m_materialEvaluationCommandBuffer;
}

void MaterialManager::IncrementMaterialUsageCount(MaterialCompileFlags flags) {
	std::lock_guard<std::mutex> lock(m_materialSlotMappingMutex);
	unsigned int slot = GetMaterialSlot(flags);
	m_materialUsageCounts[slot]++;
}

void MaterialManager::DecrementMaterialUsageCount(MaterialCompileFlags flags) {
	std::lock_guard<std::mutex> lock(m_materialSlotMappingMutex);
	unsigned int slot = GetMaterialSlot(flags);
	m_materialUsageCounts[slot]--;
	if (m_materialUsageCounts[slot] == 0) {
		m_freeMaterialSlots.push_back(slot);
		m_materialSlotMapping.erase(flags);
		m_activeMaterialSlots.erase(std::remove(m_activeMaterialSlots.begin(), m_activeMaterialSlots.end(), slot), m_activeMaterialSlots.end());
	}
}

unsigned int MaterialManager::GetMaterialSlot(MaterialCompileFlags flags) {
	unsigned int slot;
	auto it = m_materialSlotMapping.find(flags);
	if (it != m_materialSlotMapping.end()) {
		slot = it->second;
		return slot;
	}
	if (!m_freeMaterialSlots.empty()) {
		slot = m_freeMaterialSlots.back();
		m_freeMaterialSlots.pop_back();
	}
	else {
		slot = m_nextMaterialSlot++;
		m_materialUsageCounts.push_back(0);
		// Resize resources to accommodate new material slot
		m_materialPixelCountBuffer->Resize(m_materialSlotsUsed);
		m_materialOffsetBuffer->Resize(m_materialSlotsUsed);
		m_materialWriteCursorBuffer->Resize(m_materialSlotsUsed);
		m_materialEvaluationCommandBuffer->Resize(m_materialSlotsUsed);

		// Resize per-block buffers to match new block count
		const uint32_t numBlocks = (m_materialSlotsUsed + kScanBlockSize - 1u) / kScanBlockSize;
		m_blockSumsBuffer->Resize(std::max(1u, numBlocks));
		m_scannedBlockSumsBuffer->Resize(std::max(1u, numBlocks));
	}
	m_materialSlotMapping[flags] = slot;
	if (std::find(m_activeMaterialSlots.begin(), m_activeMaterialSlots.end(), slot) == m_activeMaterialSlots.end()) {
		m_activeMaterialSlots.push_back(slot);
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