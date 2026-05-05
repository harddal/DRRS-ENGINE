#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Editor/ImGuiLogSink.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"

void EditorInterface::draw_window_scene_stats()
{
	if (!m_windowData.draw_window_scene_stats) { return; }

	if (ImGui::Begin("Scene Stats", &m_windowData.draw_window_scene_stats))
	{
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
	}
	ImGui::End();
}

void EditorInterface::draw_window_console()
{
	if (!m_windowData.draw_window_console) { return; }
}

void EditorInterface::draw_window_log()
{
	if (!m_windowData.draw_window_log) { return; }

	if (ImGui::Begin("Log", &m_windowData.draw_window_log))
	{
		if (ImGui::SmallButton("Clear"))
			ImGuiLogSink::instance()->clear();

		ImGui::Separator();
		ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

		auto entries = ImGuiLogSink::instance()->snapshot();
		for (const auto& e : entries)
		{
			ImVec4 color;
			switch (e.level)
			{
				case spdlog::level::warn:     color = ImVec4(1.00f, 0.85f, 0.20f, 1.0f); break;
				case spdlog::level::err:      color = ImVec4(1.00f, 0.35f, 0.35f, 1.0f); break;
				case spdlog::level::critical: color = ImVec4(1.00f, 0.20f, 0.20f, 1.0f); break;
				case spdlog::level::debug:    color = ImVec4(0.55f, 0.75f, 1.00f, 1.0f); break;
				case spdlog::level::trace:    color = ImVec4(0.55f, 0.55f, 0.65f, 1.0f); break;
				default:                      color = ImGui::GetStyleColorVec4(ImGuiCol_Text); break;
			}
			ImGui::TextColored(color, "%s", e.text.c_str());
		}

		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndChild();
	}
	ImGui::End();
}

void EditorInterface::draw_window_editor_settings()
{
	static bool open = false;
	static EditorConfiguration config = g_sceneInteractor.getConfiguration();

	if (!m_windowData.draw_window_editor_settings) { return; }

	if (ImGui::Begin("Editor Settings", &m_windowData.draw_window_editor_settings))
	{
		if (!open) {
			config = g_sceneInteractor.getConfiguration();
			open = true;
		}

		ImGui::Checkbox("Draw Light Range Sphere", &config.drawPointLightBounds);

		if (ImGui::Button("Save"))
		{
			g_sceneInteractor.saveConfiguration(config);
			m_windowData.draw_window_editor_settings = false;
			open = false;
		}
		if (ImGui::Button("Cancel"))
		{
			m_windowData.draw_window_editor_settings = false;
			open = false;
		}
	}
	ImGui::End();
}

void EditorInterface::draw_window_entity_debug_info()
{
	if (!m_windowData.draw_window_entity_debug_info) { return; }

	ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(250, 350), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Entity Debugging Information", &m_windowData.draw_window_entity_debug_info))
	{
		if (g_currentSelectedObjectType == static_cast<unsigned int>(SELECTED_OBJECT_TYPE::ENTITY) && g_currentEntity < _entity_null_value)
		{
			auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(g_currentEntity);

			if (entity.isValid())
			{
				if (entity.hasComponent<DescriptorComponent>() && ImGui::CollapsingHeader("Descriptor"))
				{
					ImGui::Text("Entity ID: %i", entity.getComponent<DescriptorComponent>().id);
					ImGui::Text("Entity Name: %s", entity.getComponent<DescriptorComponent>().name);
				}
				if (entity.hasComponent<TransformComponent>() && ImGui::CollapsingHeader("Transform"))
				{
					ImGui::Text("Node ID: %i", entity.getComponent<TransformComponent>().node->getID());
				}
				if (entity.hasComponent<DataComponent>() && ImGui::CollapsingHeader("Data"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<RenderComponent>() && ImGui::CollapsingHeader("Render"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<MeshComponent>() && ImGui::CollapsingHeader("Mesh"))
				{
					ImGui::Text("Mesh Node ID: %i", entity.getComponent<MeshComponent>().node->getID());
				}
				if (entity.hasComponent<CameraComponent>() && ImGui::CollapsingHeader("Camera"))
				{
					ImGui::Text("Camera Node ID: %i", entity.getComponent<CameraComponent>().camera->getID());
				}
				if (entity.hasComponent<LightComponent>() && ImGui::CollapsingHeader("Light"))
				{
					ImGui::Text("Light Node ID: %i", entity.getComponent<LightComponent>().node->getID());
				}
				if (entity.hasComponent<PhysicsComponent>() && ImGui::CollapsingHeader("Physics"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<CCTComponent>() && ImGui::CollapsingHeader("CCT"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<BillboardSpriteComponent>() && ImGui::CollapsingHeader("Billboard Sprite"))
				{
					ImGui::Text("Mesh ID: %i", entity.getComponent<BillboardSpriteComponent>().node->getID());
				}
				if (entity.hasComponent<DebugSpriteComponent>() && ImGui::CollapsingHeader("DebugSprite"))
				{
					ImGui::Text("Mesh Node ID: %i", entity.getComponent<DebugSpriteComponent>().node->getID());
					ImGui::Text("Selector Node ID: %i", entity.getComponent<DebugSpriteComponent>().selectorNode->getID());
				}
				if (entity.hasComponent<DebugMeshComponent>() && ImGui::CollapsingHeader("DebugMesh"))
				{
					ImGui::Text("Mesh Node ID: %i", entity.getComponent<DebugMeshComponent>().node->getID());
				}
				if (entity.hasComponent<ScriptComponent>() && ImGui::CollapsingHeader("Script"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<LogicComponent>() && ImGui::CollapsingHeader("Logic Event"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<SoundComponent>() && ImGui::CollapsingHeader("Sound"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<MarkerComponent>() && ImGui::CollapsingHeader("Marker"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<TriggerZoneComponent>() && ImGui::CollapsingHeader("Trigger Zone"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<PrefabComponent>() && ImGui::CollapsingHeader("Prefab"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<NPCComponent>() && ImGui::CollapsingHeader("NPC"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<AutoKillComponent>() && ImGui::CollapsingHeader("Auto Kill"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<DamageReceiverComponent>() && ImGui::CollapsingHeader("Damage Receiver"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<ItemComponent>() && ImGui::CollapsingHeader("Item"))
				{
					ImGui::Text("No Data Present");
				}
				if (entity.hasComponent<SoundListenerComponent>() && ImGui::CollapsingHeader("Sound Listener"))
				{
					ImGui::Text("No Data Present");
				}
			}
			else
			{
				ImGui::Text("No valid entity selected.");
			}
		}
	}
	ImGui::End();
}
