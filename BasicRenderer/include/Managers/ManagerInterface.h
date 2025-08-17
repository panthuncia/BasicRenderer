#pragma once

class MeshManager;
class ObjectManager;
class IndirectCommandBufferManager;
class CameraManager;
class LightManager;
class EnvironmentManager;

class ManagerInterface {
public:
	ManagerInterface() = default;
	ManagerInterface(
		MeshManager* meshManager,
		ObjectManager* objectManager,
		IndirectCommandBufferManager* indirectCommandBufferManager,
		CameraManager* cameraManager,
		LightManager* lightManager,
		EnvironmentManager*  environmentManager
	) : m_pMeshManager(meshManager),
		m_pObjectManager(objectManager),
		m_pIndirectCommandBufferManager(indirectCommandBufferManager),
		m_pCameraManager(cameraManager),
		m_pLightManager(lightManager), 
		m_pEnvironmentManager(environmentManager){
	}

	void SetManagers(MeshManager* meshManager,
		ObjectManager* objectManager,
		IndirectCommandBufferManager* indirectCommandBufferManager,
		CameraManager* cameraManager,
		LightManager* lightManager,
		EnvironmentManager* environmentManager) {
		m_pMeshManager = meshManager;
		m_pObjectManager = objectManager;
		m_pIndirectCommandBufferManager = indirectCommandBufferManager;
		m_pCameraManager = cameraManager;
		m_pLightManager = lightManager;
		m_pEnvironmentManager = environmentManager;
	}

	MeshManager* GetMeshManager() { return m_pMeshManager; }
	ObjectManager* GetObjectManager() { return m_pObjectManager; }
	IndirectCommandBufferManager* GetIndirectCommandBufferManager() { return m_pIndirectCommandBufferManager; }
	CameraManager* GetCameraManager() { return m_pCameraManager; }
	LightManager* GetLightManager() { return m_pLightManager; }
	EnvironmentManager* GetEnvironmentManager() { return m_pEnvironmentManager; }
private:
	MeshManager* m_pMeshManager = nullptr;
	ObjectManager* m_pObjectManager = nullptr;
	IndirectCommandBufferManager* m_pIndirectCommandBufferManager = nullptr;
	CameraManager* m_pCameraManager = nullptr;
	LightManager* m_pLightManager = nullptr;
	EnvironmentManager* m_pEnvironmentManager = nullptr;
};