#include "Managers/ViewManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/ResourceGroup.h"
#include "../../generated/BuiltinResources.h"
#include "Resources/DynamicResource.h"

ViewManager::ViewManager() {
    auto& resourceManager = ResourceManager::GetInstance();
    m_cameraBuffer = LazyDynamicStructuredBuffer<CameraInfo>::CreateShared(1, L"cameraBuffer<ViewManager>");

    m_meshletBitfieldGroup = std::make_shared<ResourceGroup>(L"MeshletCullingBitfieldGroup");
    m_meshInstanceMeshletCullingBitfieldGroup = std::make_shared<ResourceGroup>(L"MeshInstanceMeshletCullingBitfieldGroup");
    m_meshInstanceOcclusionCullingBitfieldGroup = std::make_shared<ResourceGroup>(L"MeshInstanceOcclusionCullingBitfieldGroup");

    // Register provided resources
    m_resources[Builtin::CameraBuffer] = m_cameraBuffer;
    m_resources[Builtin::MeshletCullingBitfieldGroup] = m_meshletBitfieldGroup;
    m_resources[Builtin::MeshInstanceMeshletCullingBitfieldGroup] = m_meshInstanceMeshletCullingBitfieldGroup;
    m_resources[Builtin::MeshInstanceOcclusionCullingBitfieldGroup] = m_meshInstanceOcclusionCullingBitfieldGroup;
}

void ViewManager::SetIndirectCommandBufferManager(IndirectCommandBufferManager* manager) {
    m_indirectManager = manager;
}

uint64_t ViewManager::CreateView(const CameraInfo& cameraInfo,
    const ViewFlags& flags,
    const ViewCreationParams& params) {

    uint64_t id = m_nextViewID.fetch_add(1);

    View v;
    v.id = id;
    v.cameraInfo = cameraInfo;
    v.flags = flags;
    v.lightType = params.lightType;
    v.cascadeIndex = params.cascadeIndex;
    v.parentEntityID = params.parentEntityID;

    // Indirect buffers
    if (m_indirectManager) {
        v.gpu.indirectCommandBuffers = m_indirectManager->CreateBuffersForView(id);
    }

    // Camera buffer view
    v.gpu.cameraBufferView = m_cameraBuffer->Add();
    v.gpu.cameraBufferIndex = static_cast<uint32_t>(v.gpu.cameraBufferView->GetOffset() / sizeof(CameraInfo));
    m_cameraBuffer->UpdateView(v.gpu.cameraBufferView.get(), &cameraInfo);

    // Bitfields
    AllocateBitfields(v);

    // Depth (optional)
    v.gpu.depthMap = params.depthMap;
    v.gpu.linearDepthMap = params.linearDepthMap;

    m_views.emplace(id, std::move(v));

    if (m_events.onCreated) m_events.onCreated(m_views[id]);
    return id;
}

void ViewManager::AllocateBitfields(View& v) {
    auto& rm = ResourceManager::GetInstance();

    // Meshlet bitfield (bits -> number of 32-bit words)
    auto meshletBits = m_currentMeshletBitfieldSizeBits;
    auto meshletWords = (meshletBits + 31) / 32;
    auto meshletRes = rm.CreateIndexedStructuredBuffer(meshletWords, sizeof(unsigned int), true, false);
    meshletRes->SetName(L"MeshletBitfieldBuffer(view=" + std::to_wstring(v.id) + L")");
    v.gpu.meshletBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(meshletRes);
    m_meshletBitfieldGroup->AddResource(v.gpu.meshletBitfieldBuffer);

    // Instance bitfields (packed bits into bytes)
    auto instBits = m_currentMeshInstanceBitfieldSizeBits;
    auto instBytes = (instBits + 7) / 8;
    auto instanceMeshletRes = rm.CreateIndexedStructuredBuffer(instBytes, sizeof(unsigned int), true, false);
    instanceMeshletRes->SetName(L"MeshInstanceMeshletCullingBitfield(view=" + std::to_wstring(v.id) + L")");
    v.gpu.meshInstanceMeshletCullingBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(instanceMeshletRes);
    m_meshInstanceMeshletCullingBitfieldGroup->AddResource(v.gpu.meshInstanceMeshletCullingBitfieldBuffer);

    auto instanceOccRes = rm.CreateIndexedStructuredBuffer(instBytes, sizeof(unsigned int), true, false);
    instanceOccRes->SetName(L"MeshInstanceOcclusionCullingBitfield(view=" + std::to_wstring(v.id) + L")");
    v.gpu.meshInstanceOcclusionCullingBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(instanceOccRes);
    m_meshInstanceOcclusionCullingBitfieldGroup->AddResource(v.gpu.meshInstanceOcclusionCullingBitfieldBuffer);
}

void ViewManager::DestroyView(uint64_t viewID) {
    auto it = m_views.find(viewID);
    if (it == m_views.end()) return;

    auto& v = it->second;

    // Remove indirect buffers
    if (m_indirectManager) {
        m_indirectManager->UnregisterBuffers(viewID);
    }

    // Camera buffer view
    m_cameraBuffer->Remove(v.gpu.cameraBufferView.get());

    // Bitfields
    if (v.gpu.meshletBitfieldBuffer) {
        m_meshletBitfieldGroup->RemoveResource(v.gpu.meshletBitfieldBuffer.get());
    }
    if (v.gpu.meshInstanceMeshletCullingBitfieldBuffer) {
        m_meshInstanceMeshletCullingBitfieldGroup->RemoveResource(v.gpu.meshInstanceMeshletCullingBitfieldBuffer.get());
    }
    if (v.gpu.meshInstanceOcclusionCullingBitfieldBuffer) {
        m_meshInstanceOcclusionCullingBitfieldGroup->RemoveResource(v.gpu.meshInstanceOcclusionCullingBitfieldBuffer.get());
    }

    m_views.erase(it);
    if (m_events.onDestroyed) m_events.onDestroyed(viewID);
}

void ViewManager::AttachDepth(uint64_t viewID,
    std::shared_ptr<PixelBuffer> depth,
    std::shared_ptr<PixelBuffer> linearDepth) {
    auto* v = Get(viewID);
    if (!v) return;
    v->gpu.depthMap = depth;
    v->gpu.linearDepthMap = linearDepth;
    if (m_events.onDepthAttached) m_events.onDepthAttached(*v);
}

void ViewManager::UpdateCamera(uint64_t viewID, const CameraInfo& cameraInfo) {
    auto* v = Get(viewID);
    if (!v) return;
    std::lock_guard<std::mutex> lock(m_cameraUpdateMutex);
    v->cameraInfo = cameraInfo;
    m_cameraBuffer->UpdateView(v->gpu.cameraBufferView.get(), &cameraInfo);
    if (m_events.onCameraUpdated) m_events.onCameraUpdated(*v);
}

void ViewManager::ResizeMeshletBitfields(uint64_t numMeshlets) {
    m_currentMeshletBitfieldSizeBits = numMeshlets;
    auto& rm = ResourceManager::GetInstance();
    for (auto& [id, v] : m_views) {
        auto words = (numMeshlets + 31) / 32;
        auto res = rm.CreateIndexedStructuredBuffer(words, sizeof(unsigned int), true, false);
        res->SetName(L"MeshletBitfieldBuffer(view=" + std::to_wstring(id) + L")");
        v.gpu.meshletBitfieldBuffer->SetResource(res);
    }
}

void ViewManager::ResizeInstanceBitfields(uint32_t numInstances) {
    m_currentMeshInstanceBitfieldSizeBits = numInstances;
    auto& rm = ResourceManager::GetInstance();
    auto bytes = (numInstances + 7) / 8;
    for (auto& [id, v] : m_views) {
        // Meshlet culling
        auto cullRes = rm.CreateIndexedStructuredBuffer(bytes, sizeof(unsigned int), true, false);
        cullRes->SetName(L"MeshInstanceMeshletCullingBitfield(view=" + std::to_wstring(id) + L")");
        v.gpu.meshInstanceMeshletCullingBitfieldBuffer->SetResource(cullRes);

        // Occlusion
        auto occRes = rm.CreateIndexedStructuredBuffer(bytes, sizeof(unsigned int), true, false);
        occRes->SetName(L"MeshInstanceOcclusionCullingBitfield(view=" + std::to_wstring(id) + L")");
        v.gpu.meshInstanceOcclusionCullingBitfieldBuffer->SetResource(occRes);
    }
}

View* ViewManager::Get(uint64_t viewID) {
    auto it = m_views.find(viewID);
    if (it == m_views.end()) return nullptr;
    return &it->second;
}

const View* ViewManager::Get(uint64_t viewID) const {
    auto it = m_views.find(viewID);
    if (it == m_views.end()) return nullptr;
    return &it->second;
}

void ViewManager::BakeDescriptorIndices() {
    // Optional optimization: cache descriptor indices after SRV/UAV registration is done.
    for (auto& [id, v] : m_views) {
        if (v.gpu.meshletBitfieldBuffer)
            v.gpu.meshletBitfieldSRVIndex = v.gpu.meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
        if (v.gpu.meshInstanceMeshletCullingBitfieldBuffer)
            v.gpu.meshInstanceMeshletCullingBitfieldSRVIndex = v.gpu.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
        if (v.gpu.meshInstanceOcclusionCullingBitfieldBuffer)
            v.gpu.meshInstanceOcclusionCullingBitfieldSRVIndex = v.gpu.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
    }
}

std::shared_ptr<Resource> ViewManager::ProvideResource(ResourceIdentifier const& key) {
    auto it = m_resources.find(key);
    if (it == m_resources.end()) return nullptr;
    return it->second;
}

std::vector<ResourceIdentifier> ViewManager::GetSupportedKeys() {
    std::vector<ResourceIdentifier> keys;
    keys.reserve(m_resources.size());
    for (auto const& [k, _] : m_resources)
        keys.push_back(k);
    return keys;
}