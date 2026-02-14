#include "Managers/ViewManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Resources/ResourceGroup.h"
#include "../../generated/BuiltinResources.h"
#include "Resources/DynamicResource.h"
#include "Resources/MemoryStatisticsComponents.h"

ViewManager::ViewManager() {
    auto& resourceManager = ResourceManager::GetInstance();
    m_cameraBuffer = LazyDynamicStructuredBuffer<CameraInfo>::CreateShared(1, "cameraBuffer<ViewManager>");
	m_cullingCameraBuffer = LazyDynamicStructuredBuffer<CullingCameraInfo>::CreateShared(1, "cullingCameraBuffer<ViewManager>");

    // Register provided resources
    m_resources[Builtin::CameraBuffer] = m_cameraBuffer;
	m_resources[Builtin::CullingCameraBuffer] = m_cullingCameraBuffer;
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

    CullingCameraInfo cullCam{
        .positionWorldSpace = cameraInfo.positionWorldSpace,
        .projY = cameraInfo.jitteredProjection.r[1].m128_f32[1], // [1][1]
        .zNear = cameraInfo.zNear,
        .errorPixels = 1.0f // TODO: make configurable
    };

	v.gpu.cullingCameraBufferView = m_cullingCameraBuffer->Add();
	m_cullingCameraBuffer->UpdateView(v.gpu.cullingCameraBufferView.get(), &cullCam);

    // Depth (optional)
    v.gpu.depthMap = params.depthMap;
    v.gpu.linearDepthMap = params.linearDepthMap;

    m_views.emplace(id, std::move(v));

    if (m_events.onCreated) m_events.onCreated(m_views[id]);
    return id;
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
	m_cullingCameraBuffer->Remove(v.gpu.cullingCameraBufferView.get());

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
    if (m_events.onDepthAttached) {
        m_events.onDepthAttached(*v);
    }
}

void ViewManager::AttachVisibilityBuffer(uint64_t viewID, std::shared_ptr<PixelBuffer> visibilityBuffer) {
    auto* v = Get(viewID);
    if (!v) return;
    v->gpu.visibilityBuffer = visibilityBuffer;
    if (m_events.onVisibilityBufferAttached) {
        m_events.onVisibilityBufferAttached(*v);
    }
}

void ViewManager::UpdateCamera(uint64_t viewID, const CameraInfo& cameraInfo) {
    auto* v = Get(viewID);
    if (!v) return;
    std::lock_guard<std::mutex> lock(m_cameraUpdateMutex);
    v->cameraInfo = cameraInfo;
    m_cameraBuffer->UpdateView(v->gpu.cameraBufferView.get(), &cameraInfo);
	CullingCameraInfo cullInfo;
	cullInfo.positionWorldSpace = cameraInfo.positionWorldSpace;
	cullInfo.projY = cameraInfo.jitteredProjection.r[1].m128_f32[1]; // [1][1]
	cullInfo.zNear = cameraInfo.zNear;
	cullInfo.errorPixels = 1.0f; // TODO: make configurable
	m_cullingCameraBuffer->UpdateView(v->gpu.cullingCameraBufferView.get(), &cullInfo);
    
    if (m_events.onCameraUpdated) {
        m_events.onCameraUpdated(*v);
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

std::vector<ResourceIdentifier> ViewManager::GetSupportedResolverKeys() {
    std::vector<ResourceIdentifier> keys;
    keys.reserve(m_resolvers.size());
    for (auto const& [k, _] : m_resolvers)
        keys.push_back(k);
	return keys;
}
std::shared_ptr<IResourceResolver> ViewManager::ProvideResolver(ResourceIdentifier const& key) {
	auto it = m_resolvers.find(key);
	if (it == m_resolvers.end()) return nullptr;
	return it->second;
}