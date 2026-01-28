#pragma once

#include <memory>

#include "Materials/Material.h"
#include "Interfaces/IResourceProvider.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Render/IndirectCommand.h"
#include "Render/RasterBucketFlags.h"

// Manages buffers for per-material-compile-flag work (e.g., visibility buffer per-material)
class MaterialManager : public IResourceProvider {
public:
	static std::unique_ptr<MaterialManager> CreateUnique() {
		return std::unique_ptr<MaterialManager>(new MaterialManager());
	}
	unsigned int GetCompileFlagsSlot(MaterialCompileFlags flags);
	unsigned int GetMaterialSlot(unsigned int materialID, std::optional<PerMaterialCB> data = std::nullopt);
	unsigned int GetRasterFlagsSlot(MaterialRasterFlags rasterFlags);

	void IncrementMaterialUsageCount(Material& material);
	void DecrementMaterialUsageCount(const Material& material);

	void UpdateMaterialDataBuffer(const Material& material) {
		m_perMaterialDataBuffer->UpdateAt(GetMaterialSlot(material.GetMaterialID()), material.GetData());
	}

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

	const std::vector<unsigned int>& GetActiveCompileFlagsSlots() const { return m_activeCompileFlagsSlots; }
	const std::vector<MaterialCompileFlags>& GetActiveCompileFlags() const { return m_activeCompileFlags; }
	unsigned int GetCompileFlagsSlotsUsed() const { return m_compileFlagsSlotsUsed; }
private:
	MaterialManager();

	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::unordered_map <MaterialCompileFlags, unsigned int> m_compileFlagsSlotMapping;
	std::atomic<unsigned int> m_nextCompileFlagsSlot;
	std::vector<unsigned int> m_freeCompileFlagsSlots;
	std::vector<unsigned int> m_compileFlagsUsageCounts = { 0 };
	std::vector<unsigned int> m_activeCompileFlagsSlots;
	std::vector<MaterialCompileFlags> m_activeCompileFlags;
	//std::mutex m_compileFlagsSlotMappingMutex;
	unsigned int m_compileFlagsSlotsUsed = 1;

	unsigned int m_materialSlotsUsed = 0;
	std::vector<unsigned int> m_freeMaterialSlots;
	std::vector<unsigned int> m_materialUsageCounts = { };
	std::unordered_map <unsigned int, unsigned int> m_materialIDSlotMapping;

	static constexpr unsigned int kBufferGrowthSize = 100;

	static constexpr unsigned int kScanBlockSize = 1024;

	// Material raster flags to raster bin mapping
	std::unordered_map<uint32_t, unsigned int> m_rasterFlagToBucketMapping;
	unsigned int m_rasterBucketsUsed = 0;
	std::vector<unsigned int> m_freeRasterBuckets;
	const unsigned int m_numFixedRasterCombinations;

	// CLod execution
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_rasterBucketsClusterCountBuffer;

	// Visibility buffer
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_materialPixelCountBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_materialOffsetBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_materialWriteCursorBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_blockSumsBuffer;
	std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_scannedBlockSumsBuffer;
	std::shared_ptr<DynamicStructuredBuffer<MaterialEvaluationIndirectCommand>> m_materialEvaluationCommandBuffer;

	std::shared_ptr<DynamicStructuredBuffer<PerMaterialCB>> m_perMaterialDataBuffer;
};