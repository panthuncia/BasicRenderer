#pragma once

#include <memory>

#include "Materials/Material.h"
#include "Interfaces/IResourceProvider.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"

// Manages buffers for per-material-compile-flag work (e.g., visibility buffer per-material)
class MaterialManager : public IResourceProvider {
public:
	static std::unique_ptr<MaterialManager> CreateUnique() {
		return std::unique_ptr<MaterialManager>(new MaterialManager());
	}
	unsigned int GetMaterialSlot(MaterialCompileFlags flags);

	void IncrementMaterialUsageCount(MaterialCompileFlags flags);
	void DecrementMaterialUsageCount(MaterialCompileFlags flags);

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

	unsigned int GetMaterialSlotsUsed() const { return m_materialSlotsUsed; }
private:
	MaterialManager();

	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::unordered_map <MaterialCompileFlags, unsigned int> m_materialSlotMapping;
	std::atomic<unsigned int> m_nextMaterialSlot;
	std::vector<unsigned int> m_freeMaterialSlots;
	std::vector<unsigned int> m_materialUsageCounts = { 0 };
	std::mutex m_materialSlotMappingMutex;
	unsigned int m_materialSlotsUsed = 1;

	static constexpr unsigned int kScanBlockSize = 1024;

	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_materialPixelCountBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_materialOffsetBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_materialWriteCursorBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_blockSumsBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_scannedBlockSumsBuffer;
};