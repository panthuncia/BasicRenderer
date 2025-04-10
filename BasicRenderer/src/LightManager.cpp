#include "LightManager.h"

#include "ResourceHandles.h"
#include "ResourceManager.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "Utilities.h"
#include "SettingsManager.h"
#include "ShadowMaps.h"
#include "DynamicResource.h"
#include "IndirectCommandBufferManager.h"
#include "MaterialBuckets.h"
#include "DeletionManager.h"
#include "CameraManager.h"
#include "SortedUnsignedIntBuffer.h"
#include "MathUtils.h"

LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();

	m_activeLightIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeLightIndices");
    m_lightBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<LightInfo>(ResourceState::ALL_SRV, 10, L"lightBuffer<LightInfo>");
    m_spotViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(ResourceState::ALL_SRV, 1, L"spotViewInfo<matrix>");
    m_pointViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(ResourceState::ALL_SRV, 1, L"pointViewInfo<matrix>");
    m_directionalViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(ResourceState::ALL_SRV, 1, L"direcitonalViewInfo<matrix>");

	getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	getDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits");
	getShadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution");
	getCurrentShadowMapResourceGroup = SettingsManager::GetInstance().getSettingGetter<ShadowMaps*>("currentShadowMapsResourceGroup");
}

LightManager::~LightManager() {
	auto& deletionManager = DeletionManager::GetInstance();
	deletionManager.MarkForDelete(m_lightBuffer);
	deletionManager.MarkForDelete(m_spotViewInfo);
	deletionManager.MarkForDelete(m_pointViewInfo);
	deletionManager.MarkForDelete(m_directionalViewInfo);
}

std::pair<Components::LightViewInfo, std::optional<Components::ShadowMap>> LightManager::AddLight(LightInfo* lightInfo, uint64_t entityId) {

	auto lightBufferView = m_lightBuffer->Add(*lightInfo);
	auto lightIndex = lightBufferView->GetOffset() / sizeof(LightInfo);
	m_activeLightIndices->Insert(lightIndex);

	Components::LightViewInfo viewInfo = {};
	std::optional<Components::ShadowMap> shadowMapComponent = std::nullopt;
	if (lightInfo->shadowCaster) {
		viewInfo = CreateLightViewInfo(*lightInfo, entityId);
		auto shadowMaps = getCurrentShadowMapResourceGroup();
		if (shadowMaps != nullptr) {
			auto map = shadowMaps->AddMap(lightInfo, getShadowResolution());
			shadowMapComponent = Components::ShadowMap(map);
		}
	}
	viewInfo.lightBufferIndex = lightIndex;
	viewInfo.lightBufferView = lightBufferView;
	
	return { viewInfo, shadowMapComponent };
}

void LightManager::RemoveLight(LightInfo* light) {

}

unsigned int LightManager::GetLightBufferDescriptorIndex() {
    return m_lightBuffer->GetSRVInfo().index;
}

unsigned int LightManager::GetActiveLightIndicesBufferDescriptorIndex() {
	return m_activeLightIndices->GetSRVInfo().index;
}

unsigned int LightManager::GetPointCubemapMatricesDescriptorIndex() {
	return m_pointViewInfo->GetSRVInfo().index;
}

unsigned int LightManager::GetSpotMatricesDescriptorIndex() {
	return m_spotViewInfo->GetSRVInfo().index;
}

unsigned int LightManager::GetDirectionalCascadeMatricesDescriptorIndex() {
	return m_directionalViewInfo->GetSRVInfo().index;
}

unsigned int LightManager::GetNumLights() {
	return m_activeLightIndices->Size();
}

Components::LightViewInfo LightManager::CreateLightViewInfo(LightInfo info, uint64_t entityId) {

    auto projectionMatrix = GetProjectionMatrixForLight(info);

	Components::LightViewInfo viewInfo = {};

	XMFLOAT3 pos;
	XMStoreFloat3(&pos, info.posWorldSpace);

	switch (info.type) {
	case Components::LightType::Point: {
		auto cubeViewIndex = m_pointViewInfo->Size() / 6;
		viewInfo.viewInfoBufferIndex = cubeViewIndex;
		auto cubemapMatrices = GetCubemapViewMatrices(pos);
		for (int i = 0; i < 6; i++) {
			CameraInfo info = {};
			info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
			info.view = cubemapMatrices[i];
			info.projection = projectionMatrix;
			info.viewProjection = XMMatrixMultiply(cubemapMatrices[i], projectionMatrix);
			auto view = m_pCameraManager->AddCamera(info);
			viewInfo.cameraBufferViews.push_back(view);
			m_pointViewInfo->Add(view->GetOffset()/sizeof(CameraInfo));
			viewInfo.commandBuffers.opaqueIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::Opaque));
			viewInfo.commandBuffers.alphaTestIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::AlphaTest));
			viewInfo.commandBuffers.blendIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::Blend));
		}
		break;
	}
	case Components::LightType::Spot: {
		viewInfo.viewInfoBufferIndex = m_spotViewInfo->Size();
		CameraInfo camera = {};
		camera.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
		auto up = XMFLOAT3(0, 1, 0);
		camera.view = XMMatrixLookToRH(XMLoadFloat3(&pos), info.dirWorldSpace, XMLoadFloat3(&up));
		camera.projection = projectionMatrix;
		camera.viewProjection = XMMatrixMultiply(camera.view, projectionMatrix);
		auto view = m_pCameraManager->AddCamera(camera);
		viewInfo.cameraBufferViews.push_back(view);
		m_spotViewInfo->Add(view->GetOffset() / sizeof(CameraInfo));
		viewInfo.commandBuffers.opaqueIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::Opaque));
		viewInfo.commandBuffers.alphaTestIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::AlphaTest));
		viewInfo.commandBuffers.blendIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::Blend));
		break;
	}
	case Components::LightType::Directional: {
		auto numCascades = getNumDirectionalLightCascades();
		viewInfo.viewInfoBufferIndex = m_directionalViewInfo->Size() / numCascades;

		if (!m_currentCamera.is_valid()) {
			spdlog::warn("Camera must be provided for directional light shadow mapping");
			return viewInfo;
		}

		auto camera = m_currentCamera.get<Components::Camera>();
		auto& matrix = m_currentCamera.get<Components::Matrix>()->matrix;
		auto posFloats = GetGlobalPositionFromMatrix(matrix);
		auto cascades = setupCascades(numCascades, info.dirWorldSpace, XMLoadFloat3(&posFloats), GetForwardFromMatrix(matrix), GetUpFromMatrix(matrix), camera->zNear, camera->fov, camera->aspect, getDirectionalLightCascadeSplits());
		std::vector<std::shared_ptr<BufferView>> views;
		for (int i = 0; i < numCascades; i++) {
			CameraInfo info = {};
			info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
			info.view = cascades[i].viewMatrix;
			info.projection = cascades[i].orthoMatrix;
			info.viewProjection = XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix);
			auto view = m_pCameraManager->AddCamera(info);
			viewInfo.cameraBufferViews.push_back(view);
			m_directionalViewInfo->Add(view->GetOffset() / sizeof(CameraInfo));
			viewInfo.commandBuffers.opaqueIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::Opaque));
			viewInfo.commandBuffers.alphaTestIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::AlphaTest));
			viewInfo.commandBuffers.blendIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(entityId, MaterialBuckets::Blend));
		}
		break;
	}
	}
	return viewInfo;
}

void LightManager::UpdateLightViewInfo(flecs::entity light) {
	auto projectionMatrix = light.get<Components::ProjectionMatrix>();
	auto& views = light.get<Components::LightViewInfo>()->cameraBufferViews;
	auto lightInfo = light.get<Components::Light>();
	auto lightMatrix = light.get<Components::Matrix>();
	auto planes = light.get<Components::FrustrumPlanes>()->m_frustumPlanes;
	auto globalPos = GetGlobalPositionFromMatrix(lightMatrix->matrix);
	switch (lightInfo->type) {
	case Components::LightType::Point: {
		auto cubemapMatrices = GetCubemapViewMatrices(globalPos);
		for (int i = 0; i < 6; i++) {
			CameraInfo info = {};
			info.positionWorldSpace = { globalPos.x, globalPos.y, globalPos.z, 1.0 };
			info.view = cubemapMatrices[i];
			info.projection = projectionMatrix->matrix;
			info.viewProjection = XMMatrixMultiply(cubemapMatrices[i], projectionMatrix->matrix);
			info.clippingPlanes[0] = planes[i][0];
			info.clippingPlanes[1] = planes[i][1];
			info.clippingPlanes[2] = planes[i][2];
			info.clippingPlanes[3] = planes[i][3];
			info.clippingPlanes[4] = planes[i][4];
			info.clippingPlanes[5] = planes[i][5];
			m_pCameraManager->UpdateCamera(views[i], info);
		}
		break;
	}
	case Components::LightType::Spot: {
		CameraInfo camera = {};
		camera.positionWorldSpace = { globalPos.x, globalPos.y, globalPos.z, 1.0 };
		auto up = XMFLOAT3(0, 1, 0);
		camera.view = XMMatrixLookToRH(XMLoadFloat3(&globalPos), XMVector3Normalize(lightMatrix->matrix.r[2]), XMLoadFloat3(&up));
		camera.projection = projectionMatrix->matrix;
		camera.viewProjection = XMMatrixMultiply(camera.view, projectionMatrix->matrix);
		camera.clippingPlanes[0] = planes[0][0];
		camera.clippingPlanes[1] = planes[0][1];
		camera.clippingPlanes[2] = planes[0][2];
		camera.clippingPlanes[3] = planes[0][3];
		camera.clippingPlanes[4] = planes[0][4];
		camera.clippingPlanes[5] = planes[0][5];
		m_pCameraManager->UpdateCamera(views[0], camera);
		break;
	}
	case Components::LightType::Directional: {
		if (!m_currentCamera.is_valid()) {
			spdlog::warn("Camera must be provided for directional light shadow mapping");
			return;
		}
		auto numCascades = getNumDirectionalLightCascades();
		auto dir = XMVector3Normalize(lightMatrix->matrix.r[2]);
		auto camera = m_currentCamera.get<Components::Camera>();
		auto& matrix = m_currentCamera.get<Components::Matrix>()->matrix;
		auto posFloats = GetGlobalPositionFromMatrix(matrix);
		auto cascades = setupCascades(numCascades, lightInfo->lightInfo.dirWorldSpace, XMLoadFloat3(&posFloats), GetForwardFromMatrix(matrix), GetUpFromMatrix(matrix), camera->zNear, camera->fov, camera->aspect, getDirectionalLightCascadeSplits());
		for (int i = 0; i < numCascades; i++) {
			CameraInfo info = {};
			info.positionWorldSpace = { globalPos.x, globalPos.y, globalPos.z, 1.0 };
			info.view = cascades[i].viewMatrix;
			info.projection = cascades[i].orthoMatrix;
			info.viewProjection = XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix);
			info.clippingPlanes[0] = cascades[i].frustumPlanes[0];
			info.clippingPlanes[1] = cascades[i].frustumPlanes[1];
			info.clippingPlanes[2] = cascades[i].frustumPlanes[2];
			info.clippingPlanes[3] = cascades[i].frustumPlanes[3];
			info.clippingPlanes[4] = cascades[i].frustumPlanes[4];
			info.clippingPlanes[5] = cascades[i].frustumPlanes[5];
			m_pCameraManager->UpdateCamera(views[i], info);
		}
		break;
	}
	default:
		spdlog::warn("Light type not recognized");
	}
}

void LightManager::RemoveLightViewInfo(flecs::entity light) {

	m_pCommandBufferManager->UnregisterBuffers(light.id()); // Remove indirect command buffers
	auto lightInfo = light.get<Components::Light>();
	auto viewInfo = light.get<Components::LightViewInfo>();
	switch (lightInfo->type) {
	case Components::LightType::Point: {
		auto& views = viewInfo->cameraBufferViews;
		for (int i = 0; i < 6; i++) {
			m_pCameraManager->RemoveCamera(views[i]);
		}
		break;
	}
	case Components::LightType::Spot: {
		m_pCameraManager->RemoveCamera(viewInfo->cameraBufferViews[0]);
		break;
	}
	case Components::LightType::Directional: {
		auto& views = viewInfo->cameraBufferViews;
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

void LightManager::SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager) {
	m_pCommandBufferManager = commandBufferManager;
}

void LightManager::SetCameraManager(CameraManager* cameraManager) {
	m_pCameraManager = cameraManager;
}