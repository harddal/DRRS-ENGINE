#include "CameraSystem.h"

#include "Engine/Engine.h"
#include "Utility/Utility.h"


using namespace anax;
using namespace irr;
using namespace core;

void CameraSystem::onEntityAdded(Entity& entity)
{
    auto& cameraComponent = entity.getComponent<CameraComponent>();
    auto& transformComponent = entity.getComponent<TransformComponent>();

	// Pass makeActive=false so Irrlicht does not auto-switch the active camera.
	// onEntityAdded fires after cereal writes all component values (entity.activate()
	// is called post-archive), so isWorldCamera is reliable here.
    cameraComponent.camera = RenderManager::Get()->sceneManager()->addCameraSceneNode(
        nullptr, vector3df(0, 0, 0), vector3df(0, 0, 100), -1, false);
	cameraComponent.camera->setID(entity.getComponent<DescriptorComponent>().id);

    cameraComponent.camera->setAspectRatio(static_cast<float>(RenderManager::Get()->getConfiguration().width) / static_cast<float>(RenderManager::Get()->getConfiguration().height));

    cameraComponent.camera->setNearValue(CAMERA_NEAR);
    cameraComponent.camera->setFarValue(CAMERA_FAR);

    cameraComponent.camera->setPosition(vector3df(0, 0, 0));
    cameraComponent.camera->setRotation(vector3df(0, 0, 0));

    cameraComponent.targetNode = RenderManager::Get()->sceneManager()->addEmptySceneNode();
    cameraComponent.targetNode->setPosition(cameraComponent.target);
    cameraComponent.targetNode->setParent(cameraComponent.camera);

    cameraComponent.nearTargetNode = RenderManager::Get()->sceneManager()->addEmptySceneNode();
    cameraComponent.nearTargetNode->setPosition(cameraComponent.neartarget);
    cameraComponent.nearTargetNode->setParent(cameraComponent.camera);

    cameraComponent.camera->setPosition(transformComponent.position);
    cameraComponent.camera->setRotation(transformComponent.rotation);

	// Activate only the player camera in game mode; world cameras are activated
	// explicitly via switchToCamera(). Editor mode never touches the active camera.
	if (!Engine::Get()->isEditorMode() && !cameraComponent.isWorldCamera)
		RenderManager::Get()->sceneManager()->setActiveCamera(cameraComponent.camera);

	cameraComponent.camera->updateAbsolutePosition();
}

void CameraSystem::onEntityRemoved(Entity& entity)
{
    auto& cameraComponent = entity.getComponent<CameraComponent>();

    RenderManager::Get()->sceneManager()->addToDeletionQueue(cameraComponent.camera);
    RenderManager::Get()->sceneManager()->addToDeletionQueue(cameraComponent.targetNode);
    RenderManager::Get()->sceneManager()->addToDeletionQueue(cameraComponent.nearTargetNode);
}

void CameraSystem::update()
{
    auto& entities = getEntities();

    for (auto& entity : entities) {
        auto& cameraComponent = entity.getComponent<CameraComponent>();
        auto& transformComponent = entity.getComponent<TransformComponent>();

        auto& camera = cameraComponent.camera;
        auto& target = cameraComponent.targetNode;
        auto& nearTarget = cameraComponent.nearTargetNode;

        camera->setPosition(
            transformComponent.position + cameraComponent.offset);
        camera->setRotation(transformComponent.rotation);

        camera->updateAbsolutePosition();
        target->updateAbsolutePosition();
        nearTarget->updateAbsolutePosition();

        camera->setTarget(target->getAbsolutePosition());
		
        cameraComponent.lookat = Math::GetDirectionVector(camera->getRotation());
    }

	// If a world camera override is active, re-assert it each frame to prevent
	// other code from inadvertently changing the active camera.
	// Skip in editor mode so the editor freecam is not displaced when returning from game mode.
	if (!Engine::Get()->isEditorMode() && m_overrideCameraID != _entity_null_value)
	{
		auto& overrideEntity = WorldManager::Get()->managerSystem()->getEntityByID(m_overrideCameraID);
		if (overrideEntity.isValid() && overrideEntity.hasComponent<CameraComponent>())
			RenderManager::Get()->sceneManager()->setActiveCamera(
				overrideEntity.getComponent<CameraComponent>().camera);
		else
			m_overrideCameraID = _entity_null_value; // Entity was removed; clear the override
	}
}

void CameraSystem::switchToCamera(entityid id)
{
	auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(id);
	if (!entity.isValid() || !entity.hasComponent<CameraComponent>())
		return;

	m_overrideCameraID = id;
	RenderManager::Get()->sceneManager()->setActiveCamera(
		entity.getComponent<CameraComponent>().camera);
}

void CameraSystem::switchToPlayerCamera()
{
	m_overrideCameraID = _entity_null_value;

	// Restore player camera; fall back to freecamera if no player is present
	auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	anax::Entity* camEntity = player.isValid() ? &player : nullptr;

	if (!camEntity)
	{
		auto& fc = WorldManager::Get()->managerSystem()->getEntityByName("freecamera");
		if (fc.isValid()) camEntity = &fc;
	}

	if (camEntity && camEntity->hasComponent<CameraComponent>())
		RenderManager::Get()->sceneManager()->setActiveCamera(
			camEntity->getComponent<CameraComponent>().camera);
}
