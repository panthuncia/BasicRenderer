#pragma once

#include <rhi.h>

#include "Scene/Components.h"

class Scene;
class ObjectManager;
class MeshManager;
class IndirectCommandBufferManager;
class CameraManager;
class LightManager;
class EnvironmentManager;
class PixelBuffer;

class RenderContext {
public:
	Components::DrawStats drawStats;
	ObjectManager* objectManager;
	MeshManager* meshManager;
	IndirectCommandBufferManager* indirectCommandBufferManager;
	CameraManager* cameraManager;
    LightManager* lightManager;
	EnvironmentManager* environmentManager;
    Scene* currentScene;
    rhi::Device device;
    rhi::CommandList commandList;
	rhi::Queue commandQueue;
    rhi::DescriptorHeapHandle textureDescriptorHeap;
	rhi::DescriptorHeapHandle samplerDescriptorHeap;
	rhi::DescriptorHeapHandle rtvHeap;
    UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
    UINT frameIndex;
	UINT64 frameFenceValue;
    DirectX::XMUINT2 renderResolution;
	DirectX::XMUINT2 outputResolution;
    unsigned int globalPSOFlags;
	float deltaTime;
};