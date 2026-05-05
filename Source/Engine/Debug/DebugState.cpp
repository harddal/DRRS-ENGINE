#include "DebugState.h"

#include "Interface/DebugInterface.h"

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"

void DebugState::init(std::string args)
{
	ImGui::GetIO().MouseDrawCursor = false;
	RenderManager::Get()->device()->getCursorControl()->setVisible(true);

	m_camera.init();

	InputManager::Get()->centerMouse();

	WorldManager::Get()->renderSystem()->setDebugSpriteVisible(true);

	PhysicsManager::Get()->createScene();
}

void DebugState::update(float dt)
{
	WorldManager::Get()->updateEntityQueues();
	WorldManager::Get()->transformSystem()->update();
	WorldManager::Get()->renderSystem()->forceTransformUpdate();

	m_camera.update();
}

void DebugState::updateUI(float dt)
{
	DebugInterface::draw();
}

void DebugState::destroy()
{
	WorldManager::Get()->killAllEntities();
	WorldManager::Get()->clearCVars();

	m_camera.destroy();

	PhysicsManager::Get()->destroyScene();
}

void DebugState::pause()
{

}

void DebugState::resume()
{

}
