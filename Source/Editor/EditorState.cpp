#include "Editor/EditorState.h"

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Interface/EditorInterface.h"

#include "Engine/Renderer/RenderManager.h"

#include "Game/Components/MarkerComponent.h"

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

	// Take over quit requests (title-bar X, ALT+F4, File > Quit) so closing the
	// editor prompts about the current scene instead of dropping it.
	Engine::Get()->setQuitRequestHandler(&EditorInterface::function_request_quit);

	Engine::Get()->stateManager()->initState(ESID_EDITORGAME);

	WorldManager::Get()->importScene(_asset_scn_pak("TempleDungeon"));
}

void EditorState::update(float dt)
{
	WorldManager::Get()->updateEntityQueues();
	WorldManager::Get()->transformSystem()->update();
	WorldManager::Get()->renderSystem()->forceTransformUpdate();

    m_camera.update();

	g_sceneInteractor.update(dt);

	if (BrushManager::Get())
		BrushManager::Get()->rebuildDirtyChunks();

	// Feed the 3D-skybox anchor from the MT_SKY_CAMERA marker so the editor
	// previews parallax live as the marker or camera moves. No marker → sky off.
	{
		bool found = false;
		for (auto e : WorldManager::Get()->world()->getEntities())
		{
			if (e.hasComponent<MarkerComponent>() && e.hasComponent<TransformComponent>())
			{
				auto& mk = e.getComponent<MarkerComponent>();
				if (mk.type == MT_SKY_CAMERA)
				{
					RenderManager::Get()->setSkyCamera(
						e.getComponent<TransformComponent>().getPosition(), mk.skyScale, true);
					found = true;
					break;
				}
			}
		}
		if (!found)
			RenderManager::Get()->setSkyCamera(irr::core::vector3df(0, 0, 0), 16.0f, false);
	}
}

void EditorState::updateUI(float dt)
{
	EditorInterface::draw();
}

void EditorState::destroy()
{
	m_camera.destroy();
	g_sceneInteractor.destroy();

	// The prompt cannot be drawn once the editor UI is gone — let quit requests
	// through unmodified again.
	Engine::Get()->setQuitRequestHandler(nullptr);

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
