#pragma once

#include <rhi.h>

#include "Scene/Components.h"

class Scene;
class ObjectManager;
class MeshManager;
class IndirectCommandBufferManager;
class ViewManager;
class LightManager;
class EnvironmentManager;
class MaterialManager;
class PixelBuffer;

class RenderContext {
public:
	Components::DrawStats drawStats;
	ObjectManager* objectManager;
	MeshManager* meshManager;
	IndirectCommandBufferManager* indirectCommandBufferManager;
	ViewManager* viewManager;
    LightManager* lightManager;
	EnvironmentManager* environmentManager;
	MaterialManager* materialManager;

    Scene* currentScene;
    rhi::Device device;
    rhi::CommandList commandList;
	rhi::Queue commandQueue;
    rhi::DescriptorHeap textureDescriptorHeap;
	rhi::DescriptorHeap samplerDescriptorHeap;
	rhi::DescriptorHeap rtvHeap;
    UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
    UINT frameIndex;
	UINT64 frameFenceValue;
    DirectX::XMUINT2 renderResolution;
	DirectX::XMUINT2 outputResolution;
    unsigned int globalPSOFlags;
	float deltaTime;
};