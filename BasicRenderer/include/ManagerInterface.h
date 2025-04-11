#pragma once

class MeshManager;
class ObjectManager;
class IndirectCommandBufferManager;
class CameraManager;
class LightManager;

class ManagerInterface {
public:
	ManagerInterface() = default;
	ManagerInterface(
		MeshManager* meshManager,
		ObjectManager* objectManager,
		IndirectCommandBufferManager* indirectCommandBufferManager,
		CameraManager* cameraManager,
		LightManager* lightManager
	) : m_pMeshManager(meshManager),
		m_pObjectManager(objectManager),
		m_pIndirectCommandBufferManager(indirectCommandBufferManager),
		m_pCameraManager(cameraManager),
		m_pLightManager(lightManager) {
	}
	~ManagerInterface() = default;

	void SetManagers(MeshManager* meshManager,
		ObjectManager* objectManager,
		IndirectCommandBufferManager* indirectCommandBufferManager,
		CameraManager* cameraManager,
		LightManager* lightManager) {
		m_pMeshManager = meshManager;
		m_pObjectManager = objectManager;
		m_pIndirectCommandBufferManager = indirectCommandBufferManager;
		m_pCameraManager = cameraManager;
		m_pLightManager = lightManager;
	}

	MeshManager* GetMeshManager() { return m_pMeshManager; }
	ObjectManager* GetObjectManager() { return m_pObjectManager; }
	IndirectCommandBufferManager* GetIndirectCommandBufferManager() { return m_pIndirectCommandBufferManager; }
	CameraManager* GetCameraManager() { return m_pCameraManager; }
	LightManager* GetLightManager() { return m_pLightManager; }
private:
	MeshManager* m_pMeshManager = nullptr;
	ObjectManager* m_pObjectManager = nullptr;
	IndirectCommandBufferManager* m_pIndirectCommandBufferManager = nullptr;
	CameraManager* m_pCameraManager = nullptr;
	LightManager* m_pLightManager = nullptr;
};