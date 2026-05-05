#include "EditorGameState.h"

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Prop/PropManager.h"
#include "SceneInteractionManager.h"

void EditorGameState::init(std::string args)
{
	
}

void EditorGameState::update(float dt)
{
	if (InputManager::Get()->isKeyPressed(KEY_ESCAPE))
	{
		Engine::Get()->stateManager()->setStatePauseResume(ESID_EDITOR);
	}

	GameManager::Get()->update(dt, true);
}

void EditorGameState::updateUI(float dt)
{
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(200, 350));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground);
	ImGui::PopStyleVar();
	{
#ifndef EDITOR_GAME_DEBUG_ENHANCED
		ImGui::Text(
			"FPS: %i\n"
			"Triangles: %i\n\n"
			"Scene Nodes:   %i\n"
			"Entities:      %i\n"
			"PhysX Objects: %i\n\n",
			RenderManager::Get()->driver()->getFPS(),
			RenderManager::Get()->driver()->getPrimitiveCountDrawn(),
			RenderManager::Get()->sceneManager()->getRootSceneNode()->getChildren().size(),
			WorldManager::Get()->world()->getEntityCount(),
			PhysicsManager::Get()->scene()->getNbActors(physx::PxActorTypeFlags(physx::PxActorTypeFlag::eRIGID_DYNAMIC)));
#endif
#ifdef EDITOR_GAME_DEBUG_ENHANCED
		ImGui::Text(
			"FPS: %i\n"
			"Triangles: %i\n\n"
			"Scene Nodes:   %i\n"
			"Entities:      %i\n"
			"PhysX Objects: %i\n\n"
			"Total Frame Time:     %i\n"
			"Physics Process Time: %i\n"
			"Render Process Time:  %i\n"
			"World Process Time:   %i\n"
			" - Camera:    %i\n"
			" - Manager:   %i\n"
			" - Physics:   %i\n"
			" - Render:    %i\n"
			" - Script:    %i\n"
			" - Sound:     %i\n"
			" - Transform: %i\n"
			" - Gameplay:  %i",
				RenderManager::Get()->driver()->getFPS(),
				RenderManager::Get()->driver()->getPrimitiveCountDrawn(),
				RenderManager::Get()->sceneManager()->getRootSceneNode()->getChildren().size(),
				WorldManager::Get()->world()->getEntityCount(),
				PhysicsManager::Get()->scene()->getNbActors(physx::PxActorTypeFlags(physx::PxActorTypeFlag::eRIGID_DYNAMIC)),
				(irr::u32)(Engine::Get()->getDeltaTime()),
				(irr::u32)Engine::Get()->getPhysicsTime(),
				(irr::u32)Engine::Get()->getRenderTime(),
				(irr::u32)WorldManager::Get()->getWorldTime(),
				(irr::u32)WorldManager::Get()->getCameraTime(),
				(irr::u32)WorldManager::Get()->getManagerTime(),
				(irr::u32)WorldManager::Get()->getPhysicsTime(),
				(irr::u32)WorldManager::Get()->getRenderTime(),
				(irr::u32)WorldManager::Get()->getScriptTime(),
				(irr::u32)WorldManager::Get()->getSoundTime(),
				(irr::u32)WorldManager::Get()->getTransformTime(),
				(irr::u32)WorldManager::Get()->getGameplayTime());
#endif
		ImGui::End();
	}

	GameManager::Get()->updateUI(dt);
}

void EditorGameState::destroy()
{
	GameManager::Get()->destroy();
}

void EditorGameState::pause()
{
	PropManager::Get()->setVegetationBatched(false);

	Engine::Get()->setEditorMode(true);

	GameManager::Get()->destroy();
}

void EditorGameState::resume()
{
	Engine::Get()->setEditorMode(false);

	ImGui::GetIO().MouseDrawCursor = false;
	InputManager::Get()->centerMouse();

	GameManager::Get()->init(_asset_zip_scn(std::string("editor/") + g_currentScene));

	PropManager::Get()->setVegetationBatched(true);
}
