#include "IndirectCommandBufferManager.h"

#include "ResourceManager.h"
#include "DeletionManager.h"
#include "ResourceGroup.h"
#include "GloballyIndexedResource.h"
#include "IndirectCommand.h"
#include "PSOManager.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    m_parentResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_opaqueResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_transparentResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_parentResourceGroup->AddResource(m_opaqueResourceGroup);
	m_parentResourceGroup->AddResource(m_transparentResourceGroup);
    for (auto type : MaterialBucketTypes) {
		m_buffers[type] = std::unordered_map<int, std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>>();
    }

    D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
    argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argumentDescs[0].Constant.RootParameterIndex = 0;
    argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
    argumentDescs[0].Constant.Num32BitValuesToSet = 1;

    argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argumentDescs[1].Constant.RootParameterIndex = 1;
    argumentDescs[1].Constant.DestOffsetIn32BitValues = 0;
    argumentDescs[1].Constant.Num32BitValuesToSet = 1;

    argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.pArgumentDescs = argumentDescs;
    commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
    commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

	auto device = DeviceManager::GetInstance().GetDevice();
    auto rootSignature = PSOManager::GetInstance().GetRootSignature();
	ThrowIfFailed(device->CreateCommandSignature(&commandSignatureDesc, rootSignature.Get(), IID_PPV_ARGS(&m_commandSignature)));

    // Initialize with one dummy draw
	UpdateBuffersForBucket(MaterialBuckets::Opaque, 1);
	UpdateBuffersForBucket(MaterialBuckets::Transparent, 1);
}

IndirectCommandBufferManager::~IndirectCommandBufferManager() {
	for (auto type : MaterialBucketTypes) {
		for (auto& pair : m_buffers[type]) {
			for (auto& buffer : pair.second) {
				DeletionManager::GetInstance().MarkForDelete(buffer->GetResource()); // Delay deletion until after the current frame
			}
		}
	}
	DeletionManager::GetInstance().MarkForDelete(m_clearBufferOpaque); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(m_clearBufferTransparent); // Delay deletion until after the current frame
	//DeletionManager::GetInstance().MarkForDelete(m_opaqueResourceGroup); // Delay deletion until after the current frame
	//DeletionManager::GetInstance().MarkForDelete(m_transparentResourceGroup); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(m_parentResourceGroup); // Delay deletion until after the current frame
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
    auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(commandBufferSize, sizeof(IndirectCommand), ResourceState::UNORDERED_ACCESS, false, true, true);
	resource.dataBuffer->SetName(L"IndirectCommandBuffer ("+std::to_wstring(entityID)+L")");
    std::shared_ptr<DynamicGloballyIndexedResource> pResource = std::make_shared<DynamicGloballyIndexedResource>(resource.dataBuffer);
    m_buffers[bucket][entityID].push_back(pResource);

    uint64_t naturalEntityID = entityID + 1; // 0 is not a natural number, might not work in Cantor pairing function
    uint64_t bufferIndex = m_buffers[bucket][entityID].size(); // 1-indexed
    uint64_t uniqueID = ((uint64_t)(naturalEntityID + bufferIndex) * (naturalEntityID + bufferIndex + 1)) / 2 + bufferIndex; // Cantor pairing function
    switch (bucket) {
	case MaterialBuckets::Opaque:
		m_opaqueResourceGroup->AddIndexedResource(pResource, uniqueID);
		break;
    case MaterialBuckets::Transparent:
		m_transparentResourceGroup->AddIndexedResource(pResource, uniqueID);
		break;
    }
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
            switch (type) {
			case MaterialBuckets::Opaque:
				m_opaqueResourceGroup->RemoveIndexedResource(uniqueID);
				break;
			case MaterialBuckets::Transparent:
                m_transparentResourceGroup->RemoveIndexedResource(uniqueID);
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
        if (newSize == m_opaqueCommandBufferSize) {
            return;
        }
        m_opaqueCommandBufferSize = newSize;
        auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_opaqueCommandBufferSize, sizeof(IndirectCommand), ResourceState::ALL_SRV, false, true, true);
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
        auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_transparentCommandBufferSize, sizeof(IndirectCommand), ResourceState::COPY_SOURCE, false, true, true);
        DeletionManager::GetInstance().MarkForDelete(m_clearBufferTransparent); // Delay deletion until after the current frame
        m_clearBufferTransparent = resource.dataBuffer;
		m_clearBufferTransparent->SetName(L"ClearBufferTransparent");
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
			case MaterialBuckets::Transparent:
				commandBufferSize = m_transparentCommandBufferSize;
				break;
			}
            auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(commandBufferSize, sizeof(IndirectCommand), ResourceState::UNORDERED_ACCESS, false, true, true);
            buffer->SetResource(resource.dataBuffer);
        }
    }
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}
