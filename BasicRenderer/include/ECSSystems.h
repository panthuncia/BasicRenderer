#pragma once
#include <flecs.h>

class LightManager;
class MeshManager;
class ObjectManager;
class IndirectCommandBufferManager;
class CameraManager;
class ManagerInterface;

void EvaluateTransformationHierarchy(flecs::entity root, ManagerInterface* managers);
void RegisterAllSystems(flecs::world& world, LightManager* lightManager, MeshManager* meshManager, ObjectManager* objectManager, IndirectCommandBufferManager* indirectCommandManager, CameraManager* cameraManager);