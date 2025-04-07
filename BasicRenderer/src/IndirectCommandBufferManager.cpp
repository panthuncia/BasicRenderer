#include "IndirectCommandBufferManager.h"

#include "ResourceManager.h"
#include "DeletionManager.h"
#include "ResourceGroup.h"
#include "GloballyIndexedResource.h"
#include "IndirectCommand.h"
#include "PSOManager.h"
#include "CommandSignatureManager.h"
#include "DynamicResource.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    m_parentResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_opaqueResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_alphaTestResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_blendResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_parentResourceGroup->AddResource(m_opaqueResourceGroup);
	m_parentResourceGroup->AddResource(m_alphaTestResourceGroup);
	m_parentResourceGroup->AddResource(m_blendResourceGroup);
    for (auto type : MaterialBucketTypes) {
		m_buffers[type] = std::unordered_map<int, std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>>();
    }

    // Initialize with one dummy draw
	UpdateBuffersForBucket(MaterialBuckets::Opaque, 1000);
	UpdateBuffersForBucket(MaterialBuckets::AlphaTest, 1000);
	UpdateBuffersForBucket(MaterialBuckets::Blend, 1000);
}

IndirectCommandBufferManager::~IndirectCommandBufferManager() {
	for (auto type : MaterialBucketTypes) {
		for (auto& pair : m_buffers[type]) {
			for (auto& buffer : pair.second) {
				DeletionManager::GetInstance().MarkForDelete(buffer->GetResource()); // Delay deletion until after the current frame
				//DebugSharedPtrManager::GetInstance().StorePermenantly(buffer->GetResource());
			}
		}
	}

	//DebugSharedPtrManager::GetInstance().StorePermenantly(m_clearBufferOpaque);
	//DebugSharedPtrManager::GetInstance().StorePermenantly(m_clearBufferAlphaTest);
	//DebugSharedPtrManager::GetInstance().StorePermenantly(m_clearBufferBlend);

 //   DeletionManager::GetInstance().MarkForDelete(m_clearBufferOpaque);
	//DeletionManager::GetInstance().MarkForDelete(m_clearBufferAlphaTest);
	//DeletionManager::GetInstance().MarkForDelete(m_clearBufferBlend);

	DeletionManager::GetInstance().MarkForDelete(m_parentResourceGroup);
}

// Add a single buffer to an existing ID
std::shared_ptr<DynamicGloballyIndexedResource> IndirectCommandBufferManager::CreateBuffer(uint64_t entityID, MaterialBuckets bucket) {
    if (entityID < 0) {
        spdlog::error("Invalid entity ID for buffer creation");
        return nullptr;
    }
	unsigned int commandBufferSize = 0;
	switch (bucket) {
	case MaterialBuckets::Opaque:
		commandBufferSize = m_opaqueCommandBufferSize;
		break;
	case MaterialBuckets::AlphaTest:
		commandBufferSize = m_alphaTestCommandBufferSize;
		break;
	case MaterialBuckets::Blend:
		commandBufferSize = m_blendCommandBufferSize;
		break;
	}
    auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(commandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::UNORDERED_ACCESS, false, true, true);
	resource->SetName(L"IndirectCommandBuffer ("+std::to_wstring(entityID)+L")");
    std::shared_ptr<DynamicGloballyIndexedResource> pResource = std::make_shared<DynamicGloballyIndexedResource>(resource);
    m_buffers[bucket][entityID].push_back(pResource);

    switch (bucket) {
	case MaterialBuckets::Opaque:
		m_opaqueResourceGroup->AddResource(pResource);
		break;
    case MaterialBuckets::AlphaTest:
		m_alphaTestResourceGroup->AddResource(pResource);
		break;
    case MaterialBuckets::Blend:
		m_blendResourceGroup->AddResource(pResource);
		break;
    }
    return pResource;
}

// Remove buffers associated with an ID
void IndirectCommandBufferManager::UnregisterBuffers(uint64_t entityID) {

    for (auto type : MaterialBucketTypes) {
        for (std::shared_ptr<DynamicGloballyIndexedResource> buffer : m_buffers[type][entityID]) {
            DeletionManager::GetInstance().MarkForDelete(buffer); // Delay deletion until after the current frame
            switch (type) {
			case MaterialBuckets::Opaque:
				m_opaqueResourceGroup->RemoveResource(buffer.get());
				break;
			case MaterialBuckets::AlphaTest:
                m_alphaTestResourceGroup->RemoveResource(buffer.get());
                break;
			case MaterialBuckets::Blend:
				m_blendResourceGroup->RemoveResource(buffer.get());
				break;
            }
        }
        m_buffers[type].erase(entityID);
    }
}

// Update all buffers when the number of draws changes
void IndirectCommandBufferManager::UpdateBuffersForBucket(MaterialBuckets bucket, unsigned int numDraws) {

    // Update clear buffers
    switch (bucket) {
    case MaterialBuckets::Opaque: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize <= m_opaqueCommandBufferSize) {
            return;
        }
        m_opaqueCommandBufferSize = newSize;
        //auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_opaqueCommandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::ALL_SRV, false, true, true);
        //DeletionManager::GetInstance().MarkForDelete(m_clearBufferOpaque); // Delay deletion until after the current frame
        //m_clearBufferOpaque = resource;
        //m_clearBufferOpaque->SetName(L"ClearBufferOpaque");
        break;
    }
    case MaterialBuckets::AlphaTest: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize <= m_alphaTestCommandBufferSize) {
            return;
        }
        m_alphaTestCommandBufferSize = newSize;
        //auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_alphaTestCommandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::COPY_SOURCE, false, true, true);
        //DeletionManager::GetInstance().MarkForDelete(m_clearBufferAlphaTest); // Delay deletion until after the current frame
        //m_clearBufferAlphaTest = resource;
        //m_clearBufferAlphaTest->SetName(L"ClearBufferAlphaTest");
        break;
    }
    case MaterialBuckets::Blend: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize <= m_blendCommandBufferSize) {
            return;
        }
        m_blendCommandBufferSize = newSize;
        //auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_blendCommandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::COPY_SOURCE, false, true, true);
        //DeletionManager::GetInstance().MarkForDelete(m_clearBufferBlend); // Delay deletion until after the current frame
        //m_clearBufferBlend = resource;
        //m_clearBufferBlend->SetName(L"ClearBufferBlend");
        break;
    }
    }
    
    // Update per-view buffers
    auto& deletionManager = DeletionManager::GetInstance();
    for (auto& pair : m_buffers[bucket]) {
        for (auto& buffer : pair.second) {
            deletionManager.MarkForDelete(buffer->GetResource()); // Delay deletion until after the current frame
            unsigned int commandBufferSize = 0;
			switch (bucket) {
			case MaterialBuckets::Opaque:
				commandBufferSize = m_opaqueCommandBufferSize;
				break;
			case MaterialBuckets::AlphaTest:
				commandBufferSize = m_alphaTestCommandBufferSize;
				break;
            case MaterialBuckets::Blend:
				commandBufferSize = m_blendCommandBufferSize;
			}
            auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(commandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::UNORDERED_ACCESS, false, true, true);
            buffer->SetResource(resource);
        }
    }
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}
