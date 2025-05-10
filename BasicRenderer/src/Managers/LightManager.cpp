#include "Managers/LightManager.h"

#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/ShadowMaps.h"
#include "Resources/DynamicResource.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Materials/MaterialBuckets.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Managers/CameraManager.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Utilities/MathUtils.h"
#include "ShaderBuffers.h"

LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();

	m_activeLightIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(1, L"activeLightIndices");
    m_lightBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<LightInfo>(10, L"lightBuffer<LightInfo>");
    m_spotViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(1, L"spotViewInfo<matrix>");
    m_pointViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(1, L"pointViewInfo<matrix>");
    m_directionalViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(1, L"direcitonalViewInfo<matrix>");

	getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	getDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits");
	getShadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution");
	getCurrentShadowMapResourceGroup = SettingsManager::GetInstance().getSettingGetter<ShadowMaps*>("currentShadowMapsResourceGroup");
	getCurrentDownsampledShadowMapResourceGroup = SettingsManager::GetInstance().getSettingGetter<DownsampledShadowMaps*>("currentDownsampledShadowMapsResourceGroup");

	m_pLightViewInfoResourceGroup = std::make_shared<ResourceGroup>(L"LightViewInfo");
	m_pLightViewInfoResourceGroup->AddResource(m_spotViewInfo->GetBuffer());
	m_pLightViewInfoResourceGroup->AddResource(m_pointViewInfo->GetBuffer());

	m_pLightBufferResourceGroup = std::make_shared<ResourceGroup>(L"LightBufferResourceGroup");
	m_pLightBufferResourceGroup->AddResource(m_lightBuffer->GetBuffer());
	m_pLightBufferResourceGroup->AddResource(m_activeLightIndices->GetBuffer());

	auto getClusterSize = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT3>("lightClusterSize");
	auto lightClusterSize = getClusterSize();

	auto numClusters = lightClusterSize.x * lightClusterSize.y * lightClusterSize.z;
	m_pClusterBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(numClusters, sizeof(Cluster), false, true, false);
	m_pClusterBuffer->SetName(L"lightingClusterBuffer");

	static const size_t avgPagesPerCluster = 10;
	m_lightPagePoolSize = numClusters * avgPagesPerCluster;
	m_pLightPagesBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_lightPagePoolSize, sizeof(LightPage), false, true, false);
	m_pLightPagesBuffer->SetName(L"lightPagesBuffer");
}

LightManager::~LightManager() {
	auto& deletionManager = DeletionManager::GetInstance();
	deletionManager.MarkForDelete(m_lightBuffer);
	deletionManager.MarkForDelete(m_spotViewInfo);
	deletionManager.MarkForDelete(m_pointViewInfo);
	deletionManager.MarkForDelete(m_directionalViewInfo);
	deletionManager.MarkForDelete(m_activeLightIndices);
}

AddLightReturn LightManager::AddLight(LightInfo* lightInfo, uint64_t entityId) {
    auto lightBufferView = m_lightBuffer->Add(*lightInfo);
    auto lightIndex = lightBufferView->GetOffset() / sizeof(LightInfo);
    m_activeLightIndices->Insert(lightIndex);

    Components::LightViewInfo viewInfo;
    std::optional<Components::DepthMap> shadowMapComponent = std::nullopt;
    std::optional<Components::FrustrumPlanes> planes = std::nullopt;

    if (lightInfo->shadowCaster) {
        switch (lightInfo->type) {
            case Components::LightType::Point:
                std::tie(viewInfo, planes) = CreatePointLightViewInfo(*lightInfo, entityId);
                break;
            case Components::LightType::Spot:
                std::tie(viewInfo, planes) = CreateSpotLightViewInfo(*lightInfo, entityId);
                break;
            case Components::LightType::Directional:
                std::tie(viewInfo, planes) = CreateDirectionalLightViewInfo(*lightInfo, entityId);
                break;
            default:
                spdlog::warn("Unhandled light type");
                break;
        }

        auto shadowMaps = getCurrentShadowMapResourceGroup();
		auto downsampledMaps = getCurrentDownsampledShadowMapResourceGroup();
        if (shadowMaps != nullptr) {
            auto map = shadowMaps->AddMap(lightInfo, getShadowResolution());
			auto downsampledMap = downsampledMaps->AddMap(lightInfo, getShadowResolution(), map.get());
            shadowMapComponent = Components::DepthMap(map, downsampledMap);
			viewInfo.depthMap = map;
			viewInfo.downsampledDepthMap = downsampledMap;
			viewInfo.depthResX = map->GetWidth();
			viewInfo.depthResY = map->GetHeight();
        }
    }

    viewInfo.lightBufferIndex = lightIndex;
    viewInfo.lightBufferView = lightBufferView;
    
    return { viewInfo, shadowMapComponent, planes };
}


void LightManager::RemoveLight(LightInfo* light) {

}

unsigned int LightManager::GetLightBufferDescriptorIndex() {
    return m_lightBuffer->GetSRVInfo(0).index;
}

unsigned int LightManager::GetActiveLightIndicesBufferDescriptorIndex() {
	return m_activeLightIndices->GetSRVInfo(0).index;
}

unsigned int LightManager::GetPointCubemapMatricesDescriptorIndex() {
	return m_pointViewInfo->GetSRVInfo(0).index;
}

unsigned int LightManager::GetSpotMatricesDescriptorIndex() {
	return m_spotViewInfo->GetSRVInfo(0).index;
}

unsigned int LightManager::GetDirectionalCascadeMatricesDescriptorIndex() {
	return m_directionalViewInfo->GetSRVInfo(0).index;
}

unsigned int LightManager::GetNumLights() {
	return m_activeLightIndices->Size();
}

std::pair<Components::LightViewInfo, std::optional<Components::FrustrumPlanes>>
LightManager::CreatePointLightViewInfo(const LightInfo& info, uint64_t entityId) {
	Components::LightViewInfo viewInfo = {};
	// Assume each cubemap face uses the same projection but different view matrices.
	auto cubeViewIndex = m_pointViewInfo->Size() / 6;
	viewInfo.viewInfoBufferIndex = cubeViewIndex;
	DirectX::XMFLOAT3 pos;
	DirectX::XMStoreFloat3(&pos, info.posWorldSpace);
	auto cubemapMatrices = GetCubemapViewMatrices(pos);

	auto projection = GetProjectionMatrixForLight(info);

	// For each face of the cubemap, create a camera view
	for (int i = 0; i < 6; i++) {
		CameraInfo camera = {};
		camera.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0f };
		camera.view = cubemapMatrices[i];
		camera.projection = projection;
		camera.viewProjection = XMMatrixMultiply(cubemapMatrices[i], camera.projection);

		auto renderView = m_pCameraManager->AddCamera(camera);
		m_pointViewInfo->Add(renderView.cameraBufferIndex);
		viewInfo.renderViews.push_back(renderView);
	}	
	
	viewInfo.projectionMatrix = Components::Matrix(projection);

	return { viewInfo, std::nullopt }; // Point lights don't need extra frustum data.
}

std::pair<Components::LightViewInfo, std::optional<Components::FrustrumPlanes>>
LightManager::CreateSpotLightViewInfo(const LightInfo& info, uint64_t entityId) {
	Components::LightViewInfo viewInfo = {};
	viewInfo.viewInfoBufferIndex = m_spotViewInfo->Size();

	DirectX::XMFLOAT3 pos;
	DirectX::XMStoreFloat3(&pos, info.posWorldSpace);

	CameraInfo camera = {};
	camera.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0f };
	DirectX::XMFLOAT3 up = { 0, 1, 0 };
	camera.view = DirectX::XMMatrixLookToRH(info.posWorldSpace, info.dirWorldSpace, DirectX::XMLoadFloat3(&up));
	camera.projection = GetProjectionMatrixForLight(info);
	camera.viewProjection = DirectX::XMMatrixMultiply(camera.view, camera.projection);

	auto renderView = m_pCameraManager->AddCamera(camera);
	m_spotViewInfo->Add(renderView.cameraBufferIndex);
	viewInfo.renderViews.push_back(renderView);

	viewInfo.projectionMatrix = Components::Matrix(camera.projection);
	return { viewInfo, std::nullopt };
}

std::pair<Components::LightViewInfo, std::optional<Components::FrustrumPlanes>>
LightManager::CreateDirectionalLightViewInfo(const LightInfo& info, uint64_t entityId) {
	Components::LightViewInfo viewInfo = {};
	std::optional<Components::FrustrumPlanes> cascadePlanes = std::nullopt;
	auto numCascades = getNumDirectionalLightCascades();
	viewInfo.viewInfoBufferIndex = m_directionalViewInfo->Size() / numCascades;

	if (!m_currentCamera.is_valid()) {
		spdlog::warn("Camera must be provided for directional light shadow mapping");
		return { viewInfo, cascadePlanes };
	}

	auto camera = m_currentCamera.get<Components::Camera>();
	auto& matrix = m_currentCamera.get<Components::Matrix>()->matrix;
	auto posFloats = GetGlobalPositionFromMatrix(matrix);

	// Compute cascades (each cascade carries its own view and ortho projection matrix).
	auto cascades = setupCascades(numCascades, info.dirWorldSpace,
		DirectX::XMLoadFloat3(&posFloats), 
		GetForwardFromMatrix(matrix),
		GetUpFromMatrix(matrix),
		camera->zNear, camera->fov, camera->aspect,
		getDirectionalLightCascadeSplits());

	// Collect the frustum planes from each cascade.
	cascadePlanes = Components::FrustrumPlanes();
	for (const auto& cascade : cascades) {
		cascadePlanes->frustumPlanes.push_back(cascade.frustumPlanes);
	}

	// Create a camera and command buffers for each cascade.
	for (int i = 0; i < numCascades; i++) {
		CameraInfo cameraInfo = {};
		cameraInfo.positionWorldSpace = { posFloats.x, posFloats.y, posFloats.z, 1.0f };
		cameraInfo.view = cascades[i].viewMatrix;
		cameraInfo.projection = cascades[i].orthoMatrix;
		cameraInfo.viewProjection = DirectX::XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix);
		cameraInfo.aspectRatio = camera->aspect;
		cameraInfo.clippingPlanes[0] = cascades[i].frustumPlanes[0];
		cameraInfo.clippingPlanes[1] = cascades[i].frustumPlanes[1];
		cameraInfo.clippingPlanes[2] = cascades[i].frustumPlanes[2];
		cameraInfo.clippingPlanes[3] = cascades[i].frustumPlanes[3];
		cameraInfo.clippingPlanes[4] = cascades[i].frustumPlanes[4];
		cameraInfo.clippingPlanes[5] = cascades[i].frustumPlanes[5];
		cameraInfo.depthBufferArrayIndex = i;
		cameraInfo.depthResX = getShadowResolution();
		cameraInfo.depthResY = getShadowResolution();
		// TODO: Needs near and far for depth unprojection
		auto renderView = m_pCameraManager->AddCamera(cameraInfo);
		m_directionalViewInfo->Add(renderView.cameraBufferIndex);
		viewInfo.renderViews.push_back(renderView);
	}
	return { viewInfo, cascadePlanes };
}


void LightManager::UpdateLightViewInfo(flecs::entity light) {
	//auto projectionMatrix = light.get<Components::ProjectionMatrix>();
	auto viewInfo = light.get<Components::LightViewInfo>();
	auto& renderViews = viewInfo->renderViews;
	auto lightInfo = light.get<Components::Light>();
	auto lightMatrix = light.get<Components::Matrix>();
	auto planes = light.get<Components::FrustrumPlanes>()->frustumPlanes;
	auto globalPos = GetGlobalPositionFromMatrix(lightMatrix->matrix);
	switch (lightInfo->type) {
	case Components::LightType::Point: {
		auto cubemapMatrices = GetCubemapViewMatrices(globalPos);
		for (int i = 0; i < 6; i++) {
			const CameraInfo* oldInfo = light.get<CameraInfo>();
			CameraInfo info = {};
			info.positionWorldSpace = { globalPos.x, globalPos.y, globalPos.z, 1.0 };
			info.view = cubemapMatrices[i];
			info.projection = viewInfo->projectionMatrix.matrix;
			info.viewProjection = XMMatrixMultiply(cubemapMatrices[i], viewInfo->projectionMatrix.matrix);
			info.clippingPlanes[0] = planes[i][0];
			info.clippingPlanes[1] = planes[i][1];
			info.clippingPlanes[2] = planes[i][2];
			info.clippingPlanes[3] = planes[i][3];
			info.clippingPlanes[4] = planes[i][4];
			info.clippingPlanes[5] = planes[i][5];
			info.depthBufferArrayIndex = i;
			info.depthResX = viewInfo->depthResX;
			info.depthResY = viewInfo->depthResY;
			m_pCameraManager->UpdateCamera(renderViews[i], info);
		}
		break;
	}
	case Components::LightType::Spot: {
		CameraInfo camera = {};
		camera.positionWorldSpace = { globalPos.x, globalPos.y, globalPos.z, 1.0 };
		auto up = DirectX::XMFLOAT3(0, 1, 0);
		camera.view = DirectX::XMMatrixLookToRH(DirectX::XMLoadFloat3(&globalPos), DirectX::XMVector3Normalize(lightMatrix->matrix.r[2]), XMLoadFloat3(&up));
		camera.projection = viewInfo->projectionMatrix.matrix;
		camera.viewProjection = DirectX::XMMatrixMultiply(camera.view, viewInfo->projectionMatrix.matrix);
		camera.clippingPlanes[0] = planes[0][0];
		camera.clippingPlanes[1] = planes[0][1];
		camera.clippingPlanes[2] = planes[0][2];
		camera.clippingPlanes[3] = planes[0][3];
		camera.clippingPlanes[4] = planes[0][4];
		camera.clippingPlanes[5] = planes[0][5];
		camera.depthBufferArrayIndex = 0;
		camera.depthResX = viewInfo->depthResX;
		camera.depthResY = viewInfo->depthResY;
		m_pCameraManager->UpdateCamera(renderViews[0], camera);
		break;
	}
	case Components::LightType::Directional: {
		if (!m_currentCamera.is_valid()) {
			spdlog::warn("Camera must be provided for directional light shadow mapping");
			return;
		}
		auto numCascades = getNumDirectionalLightCascades();
		auto dir = DirectX::XMVector3Normalize(lightMatrix->matrix.r[2]);
		auto camera = m_currentCamera.get<Components::Camera>();
		auto& matrix = m_currentCamera.get<Components::Matrix>()->matrix;
		auto posFloats = GetGlobalPositionFromMatrix(matrix);
		auto cascades = setupCascades(numCascades, lightInfo->lightInfo.dirWorldSpace, DirectX::XMLoadFloat3(&posFloats), GetForwardFromMatrix(matrix), GetUpFromMatrix(matrix), camera->zNear, camera->fov, camera->aspect, getDirectionalLightCascadeSplits());
		for (int i = 0; i < numCascades; i++) {
			CameraInfo info = {};
			info.positionWorldSpace = { globalPos.x, globalPos.y, globalPos.z, 1.0 };
			info.view = cascades[i].viewMatrix;
			info.projection = cascades[i].orthoMatrix;
			info.viewProjection = DirectX::XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix);
			info.clippingPlanes[0] = cascades[i].frustumPlanes[0];
			info.clippingPlanes[1] = cascades[i].frustumPlanes[1];
			info.clippingPlanes[2] = cascades[i].frustumPlanes[2];
			info.clippingPlanes[3] = cascades[i].frustumPlanes[3];
			info.clippingPlanes[4] = cascades[i].frustumPlanes[4];
			info.clippingPlanes[5] = cascades[i].frustumPlanes[5];
			info.depthBufferArrayIndex = i;
			info.depthResX = viewInfo->depthResX;
			info.depthResY = viewInfo->depthResY;
			m_pCameraManager->UpdateCamera(renderViews[i], info);
		}
		break;
	}
	default:
		spdlog::warn("Light type not recognized");
	}
}

void LightManager::RemoveLightViewInfo(flecs::entity light) {

	//m_pCommandBufferManager->UnregisterBuffers(light.id()); // Remove indirect command buffers
	auto lightInfo = light.get<Components::Light>();
	auto viewInfo = light.get<Components::LightViewInfo>();
	switch (lightInfo->type) {
	case Components::LightType::Point: {
		auto& views = viewInfo->renderViews;
		for (int i = 0; i < 6; i++) {
			m_pCameraManager->RemoveCamera(views[i]);
		}
		break;
	}
	case Components::LightType::Spot: {
		m_pCameraManager->RemoveCamera(viewInfo->renderViews[0]);
		break;
	}
	case Components::LightType::Directional: {
		auto& views = viewInfo->renderViews;
		for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
			m_pCameraManager->RemoveCamera(views[i]);
		}
		break;
	}
	default:
		spdlog::warn("Light type not recognized");
	}
}

void LightManager::SetCurrentCamera(flecs::entity camera) {
	m_currentCamera = camera;
}


void LightManager::SetCameraManager(CameraManager* cameraManager) {
	m_pCameraManager = cameraManager;
}

void LightManager::UpdateLightBufferView(BufferView* view, LightInfo& data) {
	std::lock_guard<std::mutex> lock(m_lightUpdateMutex);
	m_lightBuffer->UpdateView(view, &data);
}

std::shared_ptr<ResourceGroup>& LightManager::GetLightViewInfoResourceGroup() {
	return m_pLightViewInfoResourceGroup;
}
std::shared_ptr<ResourceGroup>& LightManager::GetLightBufferResourceGroup() {
	return m_pLightBufferResourceGroup;
}

std::shared_ptr<Buffer>& LightManager::GetClusterBuffer() {
	return m_pClusterBuffer;
}

std::shared_ptr<Buffer>& LightManager::GetLightPagesBuffer() {
	return m_pLightPagesBuffer;
}