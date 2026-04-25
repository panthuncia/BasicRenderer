#include "Managers/MaterialManager.h"
#include "../generated/BuiltinResources.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "Render/RasterBucketFlags.h"

#include <limits>
#include <unordered_set>

namespace {
	PerMaterialOpenPBRCB BuildOpenPBRMaterialData(const Material& material) {
		const OpenPBRMaterialParameters& materialParameters = material.GetOpenPBRMaterial();
		const OpenPBRTextureBindings& textures = material.GetOpenPBRTextures();
		constexpr uint32_t kInvalidDescriptor = std::numeric_limits<uint32_t>::max();
		PerMaterialOpenPBRCB result = {};
		result.baseWeight = materialParameters.baseWeight;
		result.baseColor = materialParameters.baseColor;
		result.baseDiffuseRoughness = materialParameters.baseDiffuseRoughness;
		result.baseMetalness = materialParameters.baseMetalness;
		result.subsurfaceWeight = materialParameters.subsurfaceWeight;
		result.subsurfaceRadius = materialParameters.subsurfaceRadius;
		result.subsurfaceColor = materialParameters.subsurfaceColor;
		result.subsurfaceScatterAnisotropy = materialParameters.subsurfaceScatterAnisotropy;
		result.subsurfaceRadiusScale = materialParameters.subsurfaceRadiusScale;
		result.specularWeight = materialParameters.specularWeight;
		result.specularColor = materialParameters.specularColor;
		result.specularRoughness = materialParameters.specularRoughness;
		result.specularRoughnessAnisotropy = materialParameters.specularRoughnessAnisotropy;
		result.specularIor = materialParameters.specularIor;
		result.specularAnisotropyRotationCosSin = materialParameters.specularAnisotropyRotationCosSin;
		result.coatWeight = materialParameters.coatWeight;
		result.coatColor = materialParameters.coatColor;
		result.coatRoughness = materialParameters.coatRoughness;
		result.coatRoughnessAnisotropy = materialParameters.coatRoughnessAnisotropy;
		result.coatIor = materialParameters.coatIor;
		result.coatDarkening = materialParameters.coatDarkening;
		result.coatAnisotropyRotationCosSin = materialParameters.coatAnisotropyRotationCosSin;
		result.fuzzWeight = materialParameters.fuzzWeight;
		result.fuzzColor = materialParameters.fuzzColor;
		result.fuzzRoughness = materialParameters.fuzzRoughness;
		result.transmissionWeight = materialParameters.transmissionWeight;
		result.transmissionColor = materialParameters.transmissionColor;
		result.transmissionDepth = materialParameters.transmissionDepth;
		result.transmissionScatter = materialParameters.transmissionScatter;
		result.transmissionScatterAnisotropy = materialParameters.transmissionScatterAnisotropy;
		result.transmissionDispersionScale = materialParameters.transmissionDispersionScale;
		result.transmissionDispersionAbbeNumber = materialParameters.transmissionDispersionAbbeNumber;
		result.thinFilmWeight = materialParameters.thinFilmWeight;
		result.thinFilmThickness = materialParameters.thinFilmThickness;
		result.thinFilmIor = materialParameters.thinFilmIor;
		result.emissionLuminance = materialParameters.emissionLuminance;
		result.emissionColor = materialParameters.emissionColor;
		result.geometryOpacity = materialParameters.geometryOpacity;
		result.geometryThinWalled = materialParameters.geometryThinWalled ? 1u : 0u;

		auto initializeColorTextureMetadata = [&](const TextureAndConstant& binding,
			uint32_t& textureIndex,
			uint32_t& samplerIndex,
			DirectX::XMUINT4& channels,
			uint32_t& uvSetIndex) {
			textureIndex = kInvalidDescriptor;
			samplerIndex = kInvalidDescriptor;
			uvSetIndex = binding.uvSetIndex;
			channels = DirectX::XMUINT4(0u, 1u, 2u, 3u);

			if (binding.texture == nullptr) {
				return;
			}

			textureIndex = binding.texture->Image().GetSRVInfo(0).slot.index;
			samplerIndex = binding.texture->SamplerDescriptorIndex();
			if (binding.channels.size() > 0u) channels.x = binding.channels[0];
			if (binding.channels.size() > 1u) channels.y = binding.channels[1];
			if (binding.channels.size() > 2u) channels.z = binding.channels[2];
			if (binding.channels.size() > 3u) channels.w = binding.channels[3];
		};

		auto initializeScalarTextureMetadata = [&](const TextureAndConstant& binding,
			uint32_t& textureIndex,
			uint32_t& samplerIndex,
			uint32_t& channel,
			uint32_t& uvSetIndex) {
			textureIndex = kInvalidDescriptor;
			samplerIndex = kInvalidDescriptor;
			channel = 0u;
			uvSetIndex = binding.uvSetIndex;

			if (binding.texture == nullptr) {
				return;
			}

			textureIndex = binding.texture->Image().GetSRVInfo(0).slot.index;
			samplerIndex = binding.texture->SamplerDescriptorIndex();
			if (!binding.channels.empty()) {
				channel = binding.channels[0];
			}
		};

		initializeColorTextureMetadata(textures.coatColor,
			result.coatColorTextureIndex,
			result.coatColorSamplerIndex,
			result.coatColorChannels,
			result.coatColorUvSetIndex);
		initializeScalarTextureMetadata(textures.coatWeight,
			result.coatWeightTextureIndex,
			result.coatWeightSamplerIndex,
			result.coatWeightChannel,
			result.coatWeightUvSetIndex);
		initializeScalarTextureMetadata(textures.coatRoughness,
			result.coatRoughnessTextureIndex,
			result.coatRoughnessSamplerIndex,
			result.coatRoughnessChannel,
			result.coatRoughnessUvSetIndex);
		initializeColorTextureMetadata(textures.fuzzColor,
			result.fuzzColorTextureIndex,
			result.fuzzColorSamplerIndex,
			result.fuzzColorChannels,
			result.fuzzColorUvSetIndex);
		initializeScalarTextureMetadata(textures.fuzzWeight,
			result.fuzzWeightTextureIndex,
			result.fuzzWeightSamplerIndex,
			result.fuzzWeightChannel,
			result.fuzzWeightUvSetIndex);
		initializeScalarTextureMetadata(textures.fuzzRoughness,
			result.fuzzRoughnessTextureIndex,
			result.fuzzRoughnessSamplerIndex,
			result.fuzzRoughnessChannel,
			result.fuzzRoughnessUvSetIndex);
		return result;
	}

	std::vector<std::shared_ptr<Resource>> CollectMaterialTextureResources(const Material& material) {
		std::vector<std::shared_ptr<Resource>> textures;
		std::unordered_set<uint64_t> seenResourceIds;

		material.ForEachReferencedTexture([&](const std::shared_ptr<TextureAsset>& texture) {
			std::shared_ptr<Resource> image = texture ? texture->ImagePtr() : nullptr;
			if (!image) {
				return;
			}

			if (seenResourceIds.insert(image->GetGlobalResourceID()).second) {
				textures.push_back(std::move(image));
			}
		});

		return textures;
	}
}

// TODO: Use LazyDynamicStructuredBuffer and active indices buffer like draw calls? Would reduce number of no-op indirect arguments
MaterialManager::MaterialManager() {
	auto& rm = ResourceManager::GetInstance();
	m_activeMaterialTextureGroup = std::make_shared<ResourceGroup>("ActiveMaterialTextures");

	// Primary material data buffer
	m_perMaterialDataBuffer = DynamicStructuredBuffer<PerMaterialCB>::CreateShared(m_compileFlagsSlotsUsed, "Builtin::PerMaterialDataBuffer", true);
	m_perMaterialOpenPBRDataBuffer = DynamicStructuredBuffer<PerMaterialOpenPBRCB>::CreateShared(m_compileFlagsSlotsUsed, "Builtin::PerMaterialOpenPBRDataBuffer", true);
	rg::memory::SetResourceUsageHint(*m_perMaterialDataBuffer, "Material buffers");
	rg::memory::SetResourceUsageHint(*m_perMaterialOpenPBRDataBuffer, "Material buffers");

	// Visibility buffer resources
    m_materialPixelCountBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(m_compileFlagsSlotsUsed, "VisUtil::MaterialPixelCountBuffer", true);
    m_materialOffsetBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(m_compileFlagsSlotsUsed, "VisUtil::MaterialOffsetBuffer", true);
	m_materialWriteCursorBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(m_compileFlagsSlotsUsed, "VisUtil::MaterialWriteCursorBuffer", true);
	rg::memory::SetResourceUsageHint(*m_materialPixelCountBuffer, "Material evaluation buffers");
	rg::memory::SetResourceUsageHint(*m_materialOffsetBuffer, "Material evaluation buffers");
	rg::memory::SetResourceUsageHint(*m_materialWriteCursorBuffer, "Material evaluation buffers");

	// Per-block arrays for hierarchical scan
	const uint32_t numBlocks = (m_compileFlagsSlotsUsed + kScanBlockSize - 1u) / kScanBlockSize;
	m_blockSumsBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(std::max(1u, numBlocks), "VisUtil::BlockSumsBuffer", true);
	m_scannedBlockSumsBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(std::max(1u, numBlocks), "VisUtil::ScannedBlockSumsBuffer", true);
	rg::memory::SetResourceUsageHint(*m_blockSumsBuffer, "Material evaluation buffers");
	rg::memory::SetResourceUsageHint(*m_scannedBlockSumsBuffer, "Material evaluation buffers");

	// Indirect command buffer for material evaluation
	m_materialEvaluationCommandBuffer = DynamicStructuredBuffer<MaterialEvaluationIndirectCommand>::CreateShared(m_compileFlagsSlotsUsed, "IndirectCommandBuffers::MaterialEvaluationCommandBuffer", true);
	rg::memory::SetResourceUsageHint(*m_materialEvaluationCommandBuffer, "Indirect command buffers");

	m_resources["Builtin::VisUtil::MaterialPixelCountBuffer"] = m_materialPixelCountBuffer;
	m_resources["Builtin::VisUtil::MaterialOffsetBuffer"] = m_materialOffsetBuffer;
	m_resources["Builtin::VisUtil::MaterialWriteCursorBuffer"] = m_materialWriteCursorBuffer;
	m_resources["Builtin::VisUtil::BlockSumsBuffer"] = m_blockSumsBuffer;
	m_resources["Builtin::VisUtil::ScannedBlockSumsBuffer"] = m_scannedBlockSumsBuffer;
	m_resources["Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer"] = m_materialEvaluationCommandBuffer;
	m_resources[Builtin::PerMaterialDataBuffer] = m_perMaterialDataBuffer;
	m_resources[Builtin::PerMaterialOpenPBRDataBuffer] = m_perMaterialOpenPBRDataBuffer;
	m_resolvers[Builtin::Material::TextureGroup] = std::make_shared<ResourceGroupResolver>(m_activeMaterialTextureGroup);
}

void MaterialManager::IncrementMaterialUsageCount(Material& material) {
	//std::lock_guard<std::mutex> lock(m_materialSlotMappingMutex);
	auto& flags = material.Technique().compileFlags;
	unsigned int flagsSlot = GetCompileFlagsSlot(flags);
	m_compileFlagsUsageCounts[flagsSlot]++;
	uint32_t materialID = material.GetMaterialID();
	material.SetCompileFlagsID(flagsSlot);
	UpdateMaterialDataBuffer(material);
	unsigned int materialSlot = GetMaterialSlot(materialID);

	m_materialUsageCounts[materialSlot]++;
	if (m_materialUsageCounts[materialSlot] == 1u) {
		UpdateMaterialTextureUsage(material, 1);
	}
}

void MaterialManager::UpdateMaterialDataBuffer(Material& material) {
	const unsigned int materialSlot = GetMaterialSlot(material.GetMaterialID());
	material.SetOpenPBRMaterialDataIndex(materialSlot);
	m_perMaterialDataBuffer->UpdateAt(materialSlot, material.GetData());
	m_perMaterialOpenPBRDataBuffer->UpdateAt(materialSlot, BuildOpenPBRMaterialData(material));
	RefreshMaterialTextureUsage(material);
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
		UpdateMaterialTextureUsage(material, -1);
		unsigned int materialSlot = GetMaterialSlot(material.GetMaterialID());
		m_freeMaterialSlots.push_back(materialSlot);
		m_materialIDSlotMapping.erase(material.GetMaterialID());
	}
}

void MaterialManager::UpdateMaterialTextureUsage(const Material& material, int delta) {
	const uint32_t materialId = material.GetMaterialID();
	if (delta > 0) {
		auto textures = CollectMaterialTextureResources(material);
		m_trackedMaterialTextures[materialId] = textures;
		UpdateTrackedMaterialTextureRefs(textures, delta);
		return;
	}

	auto trackedIt = m_trackedMaterialTextures.find(materialId);
	if (trackedIt == m_trackedMaterialTextures.end()) {
		return;
	}

	UpdateTrackedMaterialTextureRefs(trackedIt->second, delta);
	m_trackedMaterialTextures.erase(trackedIt);
}

void MaterialManager::RefreshMaterialTextureUsage(const Material& material) {
	auto slotIt = m_materialIDSlotMapping.find(material.GetMaterialID());
	if (slotIt == m_materialIDSlotMapping.end() || slotIt->second >= m_materialUsageCounts.size()) {
		return;
	}

	if (m_materialUsageCounts[slotIt->second] == 0u) {
		return;
	}

	auto currentTextures = CollectMaterialTextureResources(material);
	auto& trackedTextures = m_trackedMaterialTextures[material.GetMaterialID()];

	std::unordered_set<uint64_t> currentIds;
	currentIds.reserve(currentTextures.size());
	for (const auto& texture : currentTextures) {
		if (texture) {
			currentIds.insert(texture->GetGlobalResourceID());
		}
	}

	std::unordered_set<uint64_t> trackedIds;
	trackedIds.reserve(trackedTextures.size());
	for (const auto& texture : trackedTextures) {
		if (texture) {
			trackedIds.insert(texture->GetGlobalResourceID());
		}
	}

	std::vector<std::shared_ptr<Resource>> removedTextures;
	for (const auto& texture : trackedTextures) {
		if (texture && !currentIds.contains(texture->GetGlobalResourceID())) {
			removedTextures.push_back(texture);
		}
	}

	std::vector<std::shared_ptr<Resource>> addedTextures;
	for (const auto& texture : currentTextures) {
		if (texture && !trackedIds.contains(texture->GetGlobalResourceID())) {
			addedTextures.push_back(texture);
		}
	}

	UpdateTrackedMaterialTextureRefs(removedTextures, -1);
	UpdateTrackedMaterialTextureRefs(addedTextures, 1);
	trackedTextures = std::move(currentTextures);
}

void MaterialManager::UpdateTrackedMaterialTextureRefs(const std::vector<std::shared_ptr<Resource>>& textures, int delta) {
	if (delta == 0) {
		return;
	}

	for (const auto& texture : textures) {
		if (!texture) {
			continue;
		}

		const uint64_t resourceId = texture->GetGlobalResourceID();
		if (delta > 0) {
			auto& usageCount = m_materialTextureUsageCounts[resourceId];
			usageCount += static_cast<uint32_t>(delta);
			m_activeMaterialTextureGroup->AddResource(texture);
			continue;
		}

		auto usageIt = m_materialTextureUsageCounts.find(resourceId);
		if (usageIt == m_materialTextureUsageCounts.end()) {
			continue;
		}

		const uint32_t releaseCount = static_cast<uint32_t>(-delta);
		if (usageIt->second <= releaseCount) {
			m_materialTextureUsageCounts.erase(usageIt);
			m_activeMaterialTextureGroup->RemoveResource(texture.get());
			continue;
		}

		usageIt->second -= releaseCount;
	}
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

std::vector<ResourceIdentifier> MaterialManager::GetSupportedResolverKeys() {
	std::vector<ResourceIdentifier> keys;
	keys.reserve(m_resolvers.size());
	for (auto const& [key, _] : m_resolvers) {
		keys.push_back(key);
	}
	return keys;
}

std::shared_ptr<IResourceResolver> MaterialManager::ProvideResolver(ResourceIdentifier const& key) {
	auto it = m_resolvers.find(key);
	if (it == m_resolvers.end()) {
		return nullptr;
	}
	return it->second;
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
		else {
			m_perMaterialDataBuffer->UpdateAt(slot, PerMaterialCB{});
		}
		m_perMaterialOpenPBRDataBuffer->UpdateAt(slot, PerMaterialOpenPBRCB{});
	}
	else {
		slot = m_materialSlotsUsed++;
		m_materialUsageCounts.push_back(0);
		// Resize resources to accommodate new material slot
		m_perMaterialDataBuffer->Resize(m_materialSlotsUsed);
		m_perMaterialOpenPBRDataBuffer->Resize(m_materialSlotsUsed);
		if (data.has_value()) {
			m_perMaterialDataBuffer->UpdateAt(slot, data.value());
		}
		else {
			m_perMaterialDataBuffer->UpdateAt(slot, PerMaterialCB{});
		}
		m_perMaterialOpenPBRDataBuffer->UpdateAt(slot, PerMaterialOpenPBRCB{});
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

unsigned int MaterialManager::AcquireRasterBucket(MaterialRasterFlags rasterFlags) {
	unsigned int slot;
	auto it = m_rasterFlagToBucketMapping.find(static_cast<uint32_t>(rasterFlags));
	if (it != m_rasterFlagToBucketMapping.end()) {
		slot = it->second;
		m_rasterBucketUsageCounts[slot]++;
		return slot;
	}
	if (!m_freeRasterBuckets.empty()) {
		slot = m_freeRasterBuckets.back();
		m_freeRasterBuckets.pop_back();
		m_bucketToRasterFlagMapping[slot] = rasterFlags;
		m_rasterBucketUsageCounts[slot] = 1u;
	}
	else {
		slot = m_rasterBucketsUsed++;
		m_bucketToRasterFlagMapping.push_back(rasterFlags);
		m_rasterBucketUsageCounts.push_back(1u);
	}

	m_rasterFlagToBucketMapping[static_cast<uint32_t>(rasterFlags)] = slot;
	return slot;
}

void MaterialManager::ReleaseRasterBucket(MaterialRasterFlags rasterFlags) {
	const auto it = m_rasterFlagToBucketMapping.find(static_cast<uint32_t>(rasterFlags));
	if (it == m_rasterFlagToBucketMapping.end()) {
		spdlog::error("Raster flags not found in mapping during release!");
		return;
	}

	const unsigned int slot = it->second;
	if (slot >= m_rasterBucketUsageCounts.size() || m_rasterBucketUsageCounts[slot] == 0u) {
		spdlog::error("Raster bucket usage underflow for slot {}!", slot);
		return;
	}

	m_rasterBucketUsageCounts[slot]--;
	if (m_rasterBucketUsageCounts[slot] != 0u) {
		return;
	}

	m_rasterFlagToBucketMapping.erase(it);
	m_bucketToRasterFlagMapping[slot] = MaterialRasterFlagsNone;
	m_freeRasterBuckets.push_back(slot);

	while (m_rasterBucketsUsed > 0u) {
		const unsigned int tailSlot = m_rasterBucketsUsed - 1u;
		if (tailSlot >= m_rasterBucketUsageCounts.size() ||
			m_rasterBucketUsageCounts[tailSlot] != 0u ||
			m_bucketToRasterFlagMapping[tailSlot] != MaterialRasterFlagsNone) {
			break;
		}

		m_rasterBucketsUsed--;
		m_bucketToRasterFlagMapping.pop_back();
		m_rasterBucketUsageCounts.pop_back();
		m_freeRasterBuckets.erase(
			std::remove(m_freeRasterBuckets.begin(), m_freeRasterBuckets.end(), tailSlot),
			m_freeRasterBuckets.end());
	}
}
