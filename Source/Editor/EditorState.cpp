#include "Editor/EditorState.h"

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Interface/EditorInterface.h"

#include "Engine/Renderer/RenderManager.h"

SceneInteractionManager g_sceneInteractor;

void EditorState::init(std::string args)
{
	ImGui::GetIO().MouseDrawCursor = false;
	RenderManager::Get()->device()->getCursorControl()->setVisible(true);
	RenderManager::Get()->device()->maximizeWindow();
	
    m_camera.init();

	InputManager::Get()->centerMouse();
	
	WorldManager::Get()->renderSystem()->setDebugSpriteVisible(true);

	PhysicsManager::Get()->createScene();
	
	g_sceneInteractor.init();

	Engine::Get()->stateManager()->initState(ESID_EDITORGAME);

	WorldManager::Get()->importScene(_asset_scn_pak("dm_turbine"));
}

void EditorState::update(float dt)
{
	WorldManager::Get()->updateEntityQueues();
	WorldManager::Get()->transformSystem()->update();
	WorldManager::Get()->renderSystem()->forceTransformUpdate();
	
    m_camera.update();

	g_sceneInteractor.update(dt);
}

void EditorState::updateUI(float dt)
{
	EditorInterface::draw();
}

void EditorState::destroy()
{
	m_camera.destroy();
	g_sceneInteractor.destroy();
	
	Engine::Get()->clearScene();
}

void EditorState::pause()
{
	RenderManager::Get()->device()->getCursorControl()->setVisible(false);
	WorldManager::Get()->renderSystem()->setDebugSpriteVisible(false);

	SceneDescriptor scenedesc;
	scenedesc.ambient_light = RenderManager::Get()->sceneManager()->getAmbientLight();
	scenedesc.skydome_texture = RenderManager::Get()->getCurrentSkydomeTexture();
	scenedesc.name = g_currentScene;
	WorldManager::Get()->exportScene(_asset_zip_scn(std::string("editor/") + g_currentScene));

	Engine::Get()->clearScene();
}

void EditorState::resume()
{
    InputManager::Get()->centerMouse();

	ImGui::GetIO().MouseDrawCursor = false;
	RenderManager::Get()->device()->getCursorControl()->setVisible(true);
	WorldManager::Get()->renderSystem()->setDebugSpriteVisible(true);

	PhysicsManager::Get()->createScene();
	
	WorldManager::Get()->importScene(_asset_zip_scn(std::string("editor/") + g_currentScene));

	g_sceneInteractor.clearSelectedEntities();
	
	m_camera.reset();
}
