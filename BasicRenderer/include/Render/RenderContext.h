#pragma once

#include <rhi.h>

#include "Scene/Components.h"
#include "Render/ImmediateExecution/ImmediateCommandList.h"

class Scene;
class ObjectManager;
class MeshManager;
class IndirectCommandBufferManager;
class ViewManager;
class LightManager;
class EnvironmentManager;
class MaterialManager;
class PixelBuffer;

struct RenderContext {
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

struct ImmediateContext {
	rhi::Device device;
	rg::imm::ImmediateCommandList list;
};