#include "IndirectCommandBufferManager.h"

#include "ResourceManager.h"
#include "DeletionManager.h"
#include "ResourceGroup.h"
#include "GloballyIndexedResource.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    m_resourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
    for (auto type : MaterialBucketTypes) {
		m_buffers[type] = std::unordered_map<int, std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>>();

    }
}

// Add a single buffer to an existing ID
std::shared_ptr<DynamicGloballyIndexedResource> IndirectCommandBufferManager::CreateBuffer(const int entityID, MaterialBuckets bucket) {
    if (entityID < 0) {
        spdlog::error("Invalid entity ID for buffer creation");
        return nullptr;
    }
	unsigned int commandBufferSize = 0;
	switch (bucket) {
	case MaterialBuckets::Opaque:
		commandBufferSize = m_opaqueCommandBufferSize;
		break;
	case MaterialBuckets::Transparent:
		commandBufferSize = m_transparentCommandBufferSize;
		break;
	}
    auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(commandBufferSize, ResourceState::UNORDERED_ACCESS, false, true);
    std::shared_ptr<DynamicGloballyIndexedResource> pResource = std::make_shared<DynamicGloballyIndexedResource>(resource.dataBuffer);
    m_buffers[bucket][entityID].push_back(pResource);

    uint64_t naturalEntityID = entityID + 1; // 0 is not a natural number, might not work in Cantor pairing function
    uint64_t bufferIndex = m_buffers[bucket][entityID].size(); // 1-indexed
    uint64_t uniqueID = ((uint64_t)(naturalEntityID + bufferIndex) * (naturalEntityID + bufferIndex + 1)) / 2 + bufferIndex; // Cantor pairing function
    m_resourceGroup->AddIndexedResource(pResource, uniqueID);
    return pResource;
}

// Remove buffers associated with an ID
void IndirectCommandBufferManager::UnregisterBuffers(const int entityID) {

    for (auto type : MaterialBucketTypes) {
        uint64_t bufferIndex = 0;
        uint64_t naturalEntityID = entityID + 1; // 0 is not a natural number, might not work in Cantor pairing function
        for (std::shared_ptr<DynamicGloballyIndexedResource> buffer : m_buffers[type][entityID]) {
            bufferIndex++;
            DeletionManager::GetInstance().MarkForDelete(buffer); // Delay deletion until after the current frame
            uint64_t uniqueID = ((uint64_t)(naturalEntityID + bufferIndex) * (naturalEntityID + bufferIndex + 1)) / 2 + bufferIndex; // Cantor pairing function
            m_resourceGroup->RemoveIndexedResource(uniqueID);
        }
        m_buffers[type].erase(entityID);
    }
}

// Update all buffers when the number of draws changes
void IndirectCommandBufferManager::UpdateBuffersForBucket(MaterialBuckets bucket, unsigned int numDraws) {
    switch (bucket) {
    case MaterialBuckets::Opaque: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize == m_opaqueCommandBufferSize) {
            return;
        }
        m_opaqueCommandBufferSize = newSize;
        auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(m_opaqueCommandBufferSize, ResourceState::ALL_SRV, false, true);
		DeletionManager::GetInstance().MarkForDelete(m_clearBufferOpaque); // Delay deletion until after the current frame
        m_clearBufferOpaque = resource.dataBuffer;
		m_clearBufferOpaque->SetName(L"ClearBufferOpaque");
        break;
    }
    case MaterialBuckets::Transparent: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize == m_transparentCommandBufferSize) {
            return;
        }
        m_transparentCommandBufferSize = newSize;
        auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(m_transparentCommandBufferSize, ResourceState::COPY_SOURCE, false, true);
		m_clearBufferTransparent = resource.dataBuffer;
        DeletionManager::GetInstance().MarkForDelete(m_clearBufferTransparent); // Delay deletion until after the current frame
		m_clearBufferTransparent->SetName(L"ClearBufferTransparent");
        break;
    }
    }
    
    auto& deletionManager = DeletionManager::GetInstance();
    for (auto& pair : m_buffers[bucket]) {
        for (auto& buffer : pair.second) {
            deletionManager.MarkForDelete(buffer); // Delay deletion until after the current frame
            unsigned int commandBufferSize = 0;
			switch (bucket) {
			case MaterialBuckets::Opaque:
				commandBufferSize = m_opaqueCommandBufferSize;
				break;
			case MaterialBuckets::Transparent:
				commandBufferSize = m_transparentCommandBufferSize;
				break;
			}
            auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(commandBufferSize, ResourceState::UNORDERED_ACCESS, false, true);
            buffer->SetResource(resource.dataBuffer);
        }
    }
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}
