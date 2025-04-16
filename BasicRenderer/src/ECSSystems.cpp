#include "ECSSystems.h"

#include <spdlog/spdlog.h>

#include "Components.h"
#include "Managers/ObjectManager.h"
#include "Managers/MeshManager.h"
#include "Managers/LightManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Managers/CameraManager.h"
#include "MeshInstance.h"
#include "Managers/ManagerInterface.h"
#include "Utilities/Utilities.h"

void IterateTree(flecs::entity e, const DirectX::XMMATRIX& parentTransform, ManagerInterface* managers) {
	// Get the current entity's transformation matrix.
	XMMATRIX matRotation = XMMatrixRotationQuaternion(e.get<Components::Rotation>()->rot);
	XMMATRIX matTranslation = XMMatrixTranslationFromVector(e.get<Components::Position>()->pos);
	XMMATRIX matScale = XMMatrixScalingFromVector(e.get<Components::Scale>()->scale);
	XMMATRIX result = (matScale * matRotation * matTranslation)* parentTransform;

	e.set<Components::Matrix>({ result });

	if (e.has<Components::RenderableObject>()) {
		Components::RenderableObject* object = e.get_mut<Components::RenderableObject>();
		Components::ObjectDrawInfo* drawInfo = e.get_mut<Components::ObjectDrawInfo>();
		auto& modelMatrix = object->perObjectCB.modelMatrix;
		modelMatrix = result;
		managers->GetObjectManager()->UpdatePerObjectBuffer(drawInfo->perObjectCBView.get(), object->perObjectCB);

		XMMATRIX upperLeft3x3 = XMMatrixSet(
			XMVectorGetX(modelMatrix.r[0]), XMVectorGetY(modelMatrix.r[0]), XMVectorGetZ(modelMatrix.r[0]), 0.0f,
			XMVectorGetX(modelMatrix.r[1]), XMVectorGetY(modelMatrix.r[1]), XMVectorGetZ(modelMatrix.r[1]), 0.0f,
			XMVectorGetX(modelMatrix.r[2]), XMVectorGetY(modelMatrix.r[2]), XMVectorGetZ(modelMatrix.r[2]), 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
		XMMATRIX normalMat = XMMatrixInverse(nullptr, upperLeft3x3);
		managers->GetObjectManager()->UpdateNormalMatrixBuffer(drawInfo->normalMatrixView.get(), &normalMat);
	}

	if (e.has<Components::Camera>()) {
		auto inverseMatrix = XMMatrixInverse(nullptr, result);

		Components::Camera* camera = e.get_mut<Components::Camera>();
		camera->info.view = RemoveScalingFromMatrix(inverseMatrix);
		camera->info.viewProjection = XMMatrixMultiply(camera->info.view, camera->info.projection);

		auto pos = GetGlobalPositionFromMatrix(result);
		camera->info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };

		auto cameraBufferView = e.get_mut<Components::CameraBufferView>();
		auto buffer = cameraBufferView->view->GetBuffer();
		buffer->UpdateView(cameraBufferView->view.get(), &camera->info);
	}

	if (e.has<Components::Light>()) {
		Components::Light* light = e.get_mut<Components::Light>();
		light->lightInfo.posWorldSpace = XMVectorSet(result.r[3].m128_f32[0],  // _41
													result.r[3].m128_f32[1],  // _42
													result.r[3].m128_f32[2],  // _43
													1.0f);
		XMVECTOR worldForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		light->lightInfo.dirWorldSpace = XMVector3Normalize(XMVector3TransformNormal(worldForward, result));
	}

	// Iterate through all child entities.
	e.children([&](flecs::entity child) {
		IterateTree(child, result, managers);
		});
}

void EvaluateTransformationHierarchy(flecs::entity root, ManagerInterface* managers) {
	const DirectX::XMMATRIX* matrix = &root.get<Components::Matrix>()->matrix;
	if (!matrix) {
		root.set<Components::Matrix>(DirectX::XMMatrixIdentity());
		matrix = &root.get_mut<Components::Matrix>()->matrix;
	}
	IterateTree(root, *matrix, managers);
}

void RegisterAllSystems(flecs::world& world,  LightManager* lightManager, MeshManager* meshManager, ObjectManager* objectManager, IndirectCommandBufferManager* indirectCommandManager, CameraManager* cameraManager) {

	/**/
	// System: Process entities on OnDelete of RenderableObjects, but only if they are in an active scene.
	world.observer<Components::RenderableObject>()
		.event(flecs::OnDelete)
		.with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
		.each([&](flecs::entity e, Components::RenderableObject&) {
		spdlog::info("[System] RenderableObject removed in active scene on entity: {}", e.name().c_str());
		objectManager->RemoveObject(e.get<Components::ObjectDrawInfo>());
		e.remove<Components::ObjectDrawInfo>();
		auto opaqueMeshInstances = e.get_mut<Components::OpaqueMeshInstances>();
		unsigned int drawCount = 0;
		unsigned int drawCountOpaque = 0;
		unsigned int drawCountAlphaTest = 0;
		unsigned int drawCountBlend = 0;
		if (opaqueMeshInstances) {
			for (auto& meshInstance : opaqueMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
				drawCount++;
				drawCountOpaque++;
			}
		}
		auto alphaTestMeshInstances = e.get_mut<Components::AlphaTestMeshInstances>();
		if (alphaTestMeshInstances) {
			for (auto& meshInstance : alphaTestMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
				drawCount++;
				drawCountAlphaTest++;
			}
		}
		auto blendMeshInstances = e.get_mut<Components::BlendMeshInstances>();
		if (blendMeshInstances) {
			for (auto& meshInstance : blendMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
				drawCount++;
				drawCountBlend++;
			}
		}
		e.remove<Components::OpaqueMeshInstances>();
		e.remove<Components::AlphaTestMeshInstances>();
		e.remove<Components::BlendMeshInstances>();
		auto drawStats = world.get_mut<Components::DrawStats>();
		if (drawStats) {
			drawStats->numDrawsInScene -= drawCount;
			drawStats->numOpaqueDraws -= drawCountOpaque;
			drawStats->numAlphaTestDraws -= drawCountAlphaTest;
			drawStats->numBlendDraws -= drawCountBlend;
		}
		spdlog::info("[System] DrawInfo removed from RenderableObject: {}", e.name().c_str());
			});

	world.system<Components::Position, Components::Rotation, Components::Scale, Components::Matrix>()
		.each([](flecs::entity e, Components::Position& pos, Components::Rotation& rot, Components::Scale& scale, Components::Matrix& out_matrix) {
		// Compute local matrix from Position, Rotation and Scale.
		XMMATRIX local = XMMatrixScalingFromVector(scale.scale) *
			XMMatrixRotationQuaternion(rot.rot) *
			XMMatrixTranslationFromVector(pos.pos);

		// Check for parent relationship; if present, get parent's global matrix.
		auto parent = e.parent();  // flecs provides parent() if you use child_of
		if (parent.is_valid() && parent.has<Components::Matrix>()) {
			auto parent_matrix = parent.get<Components::Matrix>()->matrix;
			out_matrix.matrix = local * parent_matrix;
		} else {
			out_matrix.matrix = local;
		}
			});
}