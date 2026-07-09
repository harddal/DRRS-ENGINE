#include "Engine/Interface/DebugConsole.h"

#include "Engine/Resource/FilePaths.h"

#include <algorithm>

#include <IMGUI/imgui.h>

#include "Engine/Engine.h"

#include "Game/Components.h"


void DebugConsole::clearInputBuffer() { std::fill(m_inputBuffer, m_inputBuffer + sizeof m_inputBuffer, '\0'); }

void DebugConsole::draw_stats()
{
    if (Engine::Get()->isDefaultStatsDrawingEnabled())
    {
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(200, 200));
		ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
		{
#ifdef NDEBUG
			ImGui::Text("RELEASE MODE\n\nFPS : %i\nDEL : %i\n\nTRI : %i\n\nSCN : %i\nENT : %i\nPHY : %i",
#else
			ImGui::Text("FPS : %i\nDEL : %i\n\nTRI : %i\n\nSCN : %i\nENT : %i\nPHY : %i",
#endif
				RenderManager::Get()->driver()->getFPS() + 1,
				static_cast<int>(Engine::Get()->getDeltaTime()) - 1,
				RenderManager::Get()->driver()->getPrimitiveCountDrawn(),
				RenderManager::Get()->sceneManager()->getRootSceneNode()->getChildren().size(),
				WorldManager::Get()->world()->getEntityCount(),
				PhysicsManager::Get()->scene()->getNbActors(physx::PxActorTypeFlags(physx::PxActorTypeFlag::eRIGID_DYNAMIC)));

			ImGui::End();
		}
    }
}

// Depends on the player being named 'player'
void DebugConsole::drawPlayerInfo()
{
    if (m_drawPlayer)
    {
		if (!Engine::Get()->isDebugConsoleVisible())
		{
			auto windowWidth = 320, windowHeight = 320;
			ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)));
			ImGui::SetNextWindowPos(ImVec2(0, 0));

			auto& ent_player = WorldManager::Get()->managerSystem()->getEntityByName("player");
			auto& transform = ent_player.getComponent<TransformComponent>();
			auto& camera = ent_player.getComponent<CameraComponent>();

			if (ImGui::Begin("PlayerInfo", reinterpret_cast<bool*>(1),
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize))
			{
				ImGui::Text("EPX %f", transform.position.X);
				ImGui::Text("EPY %f", transform.position.Y);
				ImGui::Text("EPZ %f", transform.position.Z);
				ImGui::Text("ERX %f", transform.rotation.X);
				ImGui::Text("ERY %f", transform.rotation.Y);
				ImGui::Text("ERZ %f", transform.rotation.Z);
				ImGui::Text("CPX %f", camera.camera->getPosition().X);
				ImGui::Text("CPY %f", camera.camera->getPosition().Y);
				ImGui::Text("CPZ %f", camera.camera->getPosition().Z);
				ImGui::Text("CRX %f", camera.camera->getRotation().X);
				ImGui::Text("CRY %f", camera.camera->getRotation().Y);
				ImGui::Text("CRZ %f", camera.camera->getRotation().Z);
				ImGui::Text("CNX %f", camera.lookat.X);
				ImGui::Text("CNY %f", camera.lookat.Y);
				ImGui::Text("CNZ %f", camera.lookat.Z);

				ImGui::End();
			}
		}
    }
}

