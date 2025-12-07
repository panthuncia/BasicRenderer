#include "Managers/MaterialManager.h"

MaterialManager::MaterialManager() {
	auto& rm = ResourceManager::GetInstance();

    m_materialPixelCountBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(m_materialSlotsUsed, L"VisUtil::MaterialPixelCountBuffer");

    m_materialOffsetBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(m_materialSlotsUsed, L"VisUtil::MaterialOffsetBuffer");

	m_materialWriteCursorBuffer = rm.CreateIndexedDynamicStructuredBuffer<uint32_t>(m_materialSlotsUsed, L"VisUtil::MaterialWriteCursorBuffer");

	m_resources["Builtin::VisUtil::MaterialPixelCountBuffer"] = m_materialPixelCountBuffer;
	m_resources["Builtin::VisUtil::MaterialOffsetBuffer"] = m_materialOffsetBuffer;
	m_resources["Builtin::VisUtil::MaterialWriteCursorBuffer"] = m_materialWriteCursorBuffer;
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
	}
}

unsigned int MaterialManager::GetMaterialSlot(MaterialCompileFlags flags) {
	std::lock_guard<std::mutex> lock(m_materialSlotMappingMutex);
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
		slot = ++m_nextMaterialSlot;
		m_materialUsageCounts.push_back(0);
		// Resize resources to accommodate new material slot
		m_materialPixelCountBuffer->Resize(m_materialSlotsUsed);
		m_materialOffsetBuffer->Resize(m_materialSlotsUsed);
	}
	m_materialSlotMapping[flags] = slot;
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