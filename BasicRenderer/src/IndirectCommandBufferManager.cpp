#include "IndirectCommandBufferManager.h"

#include "ResourceManager.h"
#include "DeletionManager.h"
#include "ResourceGroup.h"
#include "GloballyIndexedResource.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    m_resourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
}

// Add a single buffer to an existing ID
std::shared_ptr<DynamicGloballyIndexedResource> IndirectCommandBufferManager::CreateBuffer(const int entityID) {
    if (entityID < 0) {
        spdlog::error("Invalid entity ID for buffer creation");
        return nullptr;
    }
    auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(m_commandBufferSize, ResourceState::UNORDERED_ACCESS, false, true);
    std::shared_ptr<DynamicGloballyIndexedResource> pResource = std::make_shared<DynamicGloballyIndexedResource>(resource.dataBuffer);
    m_buffers[entityID].push_back(pResource);

    uint64_t naturalEntityID = entityID + 1; // 0 is not a natural number, might not work in Cantor pairing function
    uint64_t bufferIndex = m_buffers[entityID].size(); // 1-indexed
    uint64_t uniqueID = ((uint64_t)(naturalEntityID + bufferIndex) * (naturalEntityID + bufferIndex + 1)) / 2 + bufferIndex; // Cantor pairing function
    m_resourceGroup->AddIndexedResource(pResource, uniqueID);
    return pResource;
}

// Remove buffers associated with an ID
void IndirectCommandBufferManager::UnregisterBuffers(const int entityID) {
    uint64_t bufferIndex = 0;
    uint64_t naturalEntityID = entityID + 1; // 0 is not a natural number, might not work in Cantor pairing function
    for (std::shared_ptr<DynamicGloballyIndexedResource> buffer : m_buffers[entityID]) {
        bufferIndex++;
        DeletionManager::GetInstance().MarkForDelete(buffer); // Delay deletion until after the current frame
        uint64_t uniqueID = ((uint64_t)(naturalEntityID + bufferIndex) * (naturalEntityID + bufferIndex + 1)) / 2 + bufferIndex; // Cantor pairing function
        m_resourceGroup->RemoveIndexedResource(uniqueID);
    }
    m_buffers.erase(entityID);
}

// Update all buffers when the number of draws changes
void IndirectCommandBufferManager::UpdateAllBuffers(unsigned int numDraws) {
    unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
	if (newSize == m_commandBufferSize) {
		return;
	}
	m_commandBufferSize = newSize;
    auto& deletionManager = DeletionManager::GetInstance();
    for (auto& pair : m_buffers) {
        for (auto& buffer : pair.second) {
            deletionManager.MarkForDelete(buffer); // Delay deletion until after the current frame
            auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(m_commandBufferSize, ResourceState::UNORDERED_ACCESS, false, true);
            buffer->SetResource(resource.dataBuffer);
        }
    }
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}
