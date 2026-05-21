#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "SPK.h"
#include "Game/Components.h"
#include "Engine/Script/ScriptManager.h"
#include "Engine/Renderer/IrrAssimp/IrrAssimpImport.h"
#include <tinyxml2.h>

#include <string>

static void s_loadAnimList(MeshComponent& mc)
{
    mc.animationList.clear();
    if (!mc.isAnimated || mc.mesh.empty())
        return;

    const std::string animPath = mc.mesh.substr(0, mc.mesh.find_last_of('.') + 1) + "anim";

    generateAnimFile(mc.mesh);

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(animPath.c_str()) != tinyxml2::XML_SUCCESS)
        return;

    auto* root = doc.FirstChild();
    if (!root) return;
    root = root->NextSibling();
    if (!root) return;

    for (auto* elem = root->FirstChildElement(); elem; elem = elem->NextSiblingElement())
    {
        const std::string tag = elem->Name();
        if (tag == "fps")
        {
            mc.fps = atoi(elem->GetText());
        }
        else if (tag == "animationList")
        {
            for (auto* sub = elem->FirstChildElement(); sub; sub = sub->NextSiblingElement())
            {
                std::string frames   = sub->GetText();
                std::string name     = sub->Attribute("name");
                bool        loop     = Utility::ProcessBoolStatement(std::string(sub->Attribute("loop")));
                mc.animationList.push_back(sAnimationData(
                    name,
                    std::stoi(frames.substr(0, frames.find_first_of(','))),
                    std::stoi(frames.substr(frames.find_first_of(',') + 1)),
                    loop));
            }
        }
    }
}

void EditorInterface::draw_window_add_component()
{
	if (!m_windowData.draw_window_add_component) { return; }

	if (ImGui::Begin("Add Component", &m_windowData.draw_window_add_component, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking))
	{
		static int current_selected_component = 0;
		const char* component_list =
			"Auto Kill\0"
			"Billboard Sprite\0"
			"Camera\0"
			"Character Controller\0"
			"Damage Receiver\0"
			"Data\0"
			"Debug Mesh\0"
			"Debug Sprite\0"
			"Descriptor\0"
			"Interaction\0"
			"Item\0"
			"Light\0"
			"Logic\0"
			"Marker\0"
			"Mesh\0"
			"NPC\0"
			"Physics\0"
			"Prefab\0"
			"Render\0"
			"Script\0"
			"Sound\0"
			"Sound Listener\0"
			"Transform\0"
			"Trigger Zone\0"
			"Dialog\0"
			"Tween\0"
			"Nav Agent\0"
			"Water\0"
			"Particle\0\0"; // 29

		ImGui::Combo("Component", &current_selected_component, component_list, 29);
		ImGui::SameLine();

		{
			if (ImGui::Button("Add"))
			{
				auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(g_currentEntity);

				if (entity.isValid())
				{
					switch (static_cast<ENTITY_COMPONENT>(current_selected_component))
					{
					case ENTITY_COMPONENT::BILLBOARDSPRITE:
						if (entity.hasComponent<BillboardSpriteComponent>()) break;
						entity.addComponent<BillboardSpriteComponent>();
						break;
					case ENTITY_COMPONENT::CAMERA:
						if (entity.hasComponent<CameraComponent>()) break;
						entity.addComponent<CameraComponent>();
						break;
					case ENTITY_COMPONENT::CHARACTERCONTROLLER:
						if (entity.hasComponent<CCTComponent>()) break;
						entity.addComponent<CCTComponent>();
						break;
					case ENTITY_COMPONENT::DEBUGMESH:
						if (entity.hasComponent<DebugMeshComponent>()) break;
						entity.addComponent<DebugMeshComponent>();
						break;
					case ENTITY_COMPONENT::DEBUGSPRITE:
						if (entity.hasComponent<DebugSpriteComponent>()) break;
						entity.addComponent<DebugSpriteComponent>();
						break;
					case ENTITY_COMPONENT::DESCRIPTOR:
						if (entity.hasComponent<DescriptorComponent>()) break;
						entity.addComponent<DescriptorComponent>();
						break;
					case ENTITY_COMPONENT::LIGHT:
						if (entity.hasComponent<LightComponent>()) break;
						entity.addComponent<LightComponent>();
						break;
					case ENTITY_COMPONENT::MESH:
						if (entity.hasComponent<MeshComponent>()) break;
						entity.addComponent<MeshComponent>();
						break;
					case ENTITY_COMPONENT::PHYSICS:
						if (entity.hasComponent<PhysicsComponent>()) break;
						entity.addComponent<PhysicsComponent>();
						break;
					case ENTITY_COMPONENT::PREFAB:
						if (entity.hasComponent<PrefabComponent>()) break;
						entity.addComponent<PrefabComponent>();
						break;
					case ENTITY_COMPONENT::RENDER:
						if (entity.hasComponent<RenderComponent>()) break;
						entity.addComponent<RenderComponent>();
						break;
					case ENTITY_COMPONENT::SCRIPT:
						if (entity.hasComponent<ScriptComponent>()) break;
						entity.addComponent<ScriptComponent>();
						break;
					case ENTITY_COMPONENT::SOUND:
						if (entity.hasComponent<SoundComponent>()) break;
						entity.addComponent<SoundComponent>();
						break;
					case ENTITY_COMPONENT::SOUNDLISTENER:
						if (entity.hasComponent<SoundListenerComponent>()) break;
						entity.addComponent<SoundListenerComponent>();
						break;
					case ENTITY_COMPONENT::TRANSFORM:
						if (entity.hasComponent<TransformComponent>()) break;
						entity.addComponent<TransformComponent>();
						break;
					case ENTITY_COMPONENT::AUTOKILL:
						if (entity.hasComponent<AutoKillComponent>()) break;
						entity.addComponent<AutoKillComponent>();
						break;
					case ENTITY_COMPONENT::DAMAGERECEIVER:
						if (entity.hasComponent<DamageReceiverComponent>()) break;
						entity.addComponent<DamageReceiverComponent>();
						break;
					case ENTITY_COMPONENT::DATA:
						if (entity.hasComponent<DataComponent>()) break;
						entity.addComponent<DataComponent>();
						break;
					case ENTITY_COMPONENT::INTERACTION:
						if (entity.hasComponent<InteractionComponent>()) break;
						entity.addComponent<InteractionComponent>();
						break;
					case ENTITY_COMPONENT::ITEM:
						if (entity.hasComponent<ItemComponent>()) break;
						entity.addComponent<ItemComponent>();
						break;
					case ENTITY_COMPONENT::LOGIC:
						if (entity.hasComponent<LogicComponent>()) break;
						entity.addComponent<LogicComponent>();
						break;
					case ENTITY_COMPONENT::MARKER:
						if (entity.hasComponent<MarkerComponent>()) break;
						entity.addComponent<MarkerComponent>();
						break;
					case ENTITY_COMPONENT::NPC:
						if (entity.hasComponent<NPCComponent>()) break;
						entity.addComponent<NPCComponent>();
						break;
					case ENTITY_COMPONENT::TRIGGERZONE:
						if (entity.hasComponent<TriggerZoneComponent>()) break;
						entity.addComponent<TriggerZoneComponent>();
						break;
					case ENTITY_COMPONENT::DIALOG:
						if (entity.hasComponent<DialogComponent>()) break;
						entity.addComponent<DialogComponent>();
						break;
					case ENTITY_COMPONENT::TWEEN:
						if (entity.hasComponent<TweenComponent>()) break;
						entity.addComponent<TweenComponent>();
						break;
					case ENTITY_COMPONENT::NAVAGENT:
						if (entity.hasComponent<NavAgentComponent>()) break;
						entity.addComponent<NavAgentComponent>();
						break;
					case ENTITY_COMPONENT::WATER:
						if (entity.hasComponent<WaterComponent>()) break;
						entity.addComponent<WaterComponent>();
						break;
					case ENTITY_COMPONENT::PARTICLE:
						if (entity.hasComponent<ParticleComponent>()) break;
						entity.addComponent<ParticleComponent>();
						break;
					}

					WorldManager::Get()->world()->refresh();
				}

				m_windowData.draw_window_add_component = false;
			}

			ImGui::SameLine();
		}

		if (ImGui::Button("Cancel"))
		{
			m_windowData.draw_window_add_component = false;
		}
	}
	ImGui::End();
}

bool EditorInterface::draw_component_properties(ENTITY_COMPONENT component, anax::Entity& entity)
{
	switch (component)
	{
	case ENTITY_COMPONENT::AUTOKILL:
		{
			if (!entity.hasComponent<AutoKillComponent>())
				return false;

			ImGui::Text("Component contains no adjustable properties");
			break;
		}
	case ENTITY_COMPONENT::BILLBOARDSPRITE:
		{
			if (!entity.hasComponent<BillboardSpriteComponent>())
				return false;

			auto& billboardsprite = entity.getComponent<BillboardSpriteComponent>();

			if (ImGui::BeginTable("##bb_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Visible");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##bb_vis", &billboardsprite.isVisible);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Animated");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##bb_anim", &billboardsprite.animated);

				if (billboardsprite.animated)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Loop");
					ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##bb_loop", &billboardsprite.loop);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("FPS");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##bb_fps", &billboardsprite.fps);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Frame Width");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##bb_fw", &billboardsprite.split_x);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Frame Height");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##bb_fh", &billboardsprite.split_y);
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Scale X");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##bb_sx", &billboardsprite.scale_x);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Scale Y");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##bb_sy", &billboardsprite.scale_y);

				ImGui::EndTable();
			}

			ImGui::Spacing();

			auto tstr = billboardsprite.sprite;
			char buf[256];
			static bool is_searching = false;

			memset(buf, '\0', 256);
			for (auto c = 0U; c < tstr.size(); c++) buf[c] = tstr[c];

			ImGui::PushID("textureInputManager::Get()_");
			if (ImGui::InputText("", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tstr = buf;
				billboardsprite.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
			}
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID("texture_browse_");
			if (ImGui::Button("..."))
			{
				show_window_texture_browser();
				is_searching = true;
			}
			ImGui::PopID();

			if (is_searching && g_currentSelectedTexture != "null")
			{
				billboardsprite.sprite = _asset_tex(g_currentSelectedTexture);
				billboardsprite.node->setMaterialTexture(
					0, RenderManager::Get()->driver()->getTexture(billboardsprite.sprite.c_str()));
				is_searching = false;
				g_currentSelectedTexture = "null";
			}

			break;
		}
	case ENTITY_COMPONENT::CAMERA:
		{
			if (!entity.hasComponent<CameraComponent>())
				return false;

			auto& camera = entity.getComponent<CameraComponent>();

			if (ImGui::BeginTable("##camera_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("World Camera");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##world_cam", &camera.isWorldCamera);

				float fl3_off[3]; camera.offset.getAs3Values(fl3_off);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Position");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputFloat3("##cam_off", fl3_off, "%.2f"))
					camera.offset = irr::core::vector3df(fl3_off[0], fl3_off[1], fl3_off[2]);

				float fl3_tar[3]; camera.target.getAs3Values(fl3_tar);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Rotation");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputFloat3("##cam_tar", fl3_tar, "%.2f"))
					camera.target = irr::core::vector3df(fl3_tar[0], fl3_tar[1], fl3_tar[2]);

				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::CHARACTERCONTROLLER:
		{
			if (!entity.hasComponent<CCTComponent>())
				return false;

			auto& cct = entity.getComponent<CCTComponent>();

			if (ImGui::BeginTable("##cct_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Active");
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Checkbox("##cct_active", &cct.active))
					WorldManager::Get()->renderSystem()->forceTransformUpdate();
				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::DAMAGERECEIVER:
		{
			if (!entity.hasComponent<DamageReceiverComponent>())
				return false;

			auto& dam = entity.getComponent<DamageReceiverComponent>();

			if (ImGui::BeginTable("##dam_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Health");
				ImGui::TableSetColumnIndex(1); ImGui::InputInt("##health", &dam.threshold);
				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::DATA:
		{
			if (!entity.hasComponent<DataComponent>())
				return false;

			auto& data = entity.getComponent<DataComponent>();

			char buffer[256];
			memset(buffer, 0, sizeof buffer);

			for (auto i = 0U; i < data.data.size(); i++)
			{
				for (auto n = 0U; n < data.data.at(i).length(); n++)
					buffer[n] = data.data.at(i).at(n);

				ImGui::PushID(std::to_string(i).c_str());
				if (ImGui::Button(" - "))
					data.data.erase(data.data.begin() + i);
				ImGui::PopID();

				ImGui::SameLine();

				if (ImGui::InputText(std::string("Slot " + std::to_string(i)).c_str(), buffer, sizeof buffer,
				                     ImGuiInputTextFlags_EnterReturnsTrue))
					data.data.at(i) = buffer;

				memset(buffer, 0, sizeof buffer);
			}

			if (ImGui::Button("Add Slot"))
				data.data.emplace_back(std::string());

			break;
		}
	case ENTITY_COMPONENT::DEBUGMESH:
		{
			if (!entity.hasComponent<DebugMeshComponent>())
				return false;

			auto& debugmesh = entity.getComponent<DebugMeshComponent>();

			char buf[256];
			memset(buf, 0, 256);
			for (auto i = 0U; i < debugmesh.mesh.size() && i < 256; i++) buf[i] = debugmesh.mesh[i];
			if (ImGui::InputText("Mesh", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				debugmesh.mesh = buf;
				WorldManager::Get()->renderSystem()->setDebugMeshComponentData(entity);
			}

			auto tstr = debugmesh.texture;
			static bool is_searching = false;

			memset(buf, 0, 256);
			for (auto c = 0U; c < tstr.size(); c++) buf[c] = tstr[c];

			ImGui::PushID("textureInputManager::Get()_");
			if (ImGui::InputText("", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tstr = buf;
				debugmesh.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
			}
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID("texture_browse_");
			if (ImGui::Button("..."))
			{
				show_window_texture_browser();
				is_searching = true;
			}
			ImGui::PopID();

			if (is_searching && g_currentSelectedTexture != "null")
			{
				debugmesh.texture = _asset_tex(g_currentSelectedTexture);
				debugmesh.node->setMaterialTexture(
					0, RenderManager::Get()->driver()->getTexture(debugmesh.texture.c_str()));
				is_searching = false;
				g_currentSelectedTexture = "null";
			}

			break;
		}
	case ENTITY_COMPONENT::DEBUGSPRITE:
		{
			if (!entity.hasComponent<DebugSpriteComponent>())
				return false;

			auto& debugsprite = entity.getComponent<DebugSpriteComponent>();

			auto tstr = debugsprite.sprite;
			char buf[256];
			static bool is_searching = false;

			memset(buf, '\0', 256);
			for (auto c = 0U; c < tstr.size(); c++) buf[c] = tstr[c];

			ImGui::PushID("textureInputManager::Get()_");
			if (ImGui::InputText("", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tstr = buf;
				debugsprite.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
			}
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID("texture_browse_");
			if (ImGui::Button("..."))
			{
				show_window_texture_browser();
				is_searching = true;
			}
			ImGui::PopID();

			if (is_searching && g_currentSelectedTexture != "null")
			{
				debugsprite.sprite = _asset_tex(g_currentSelectedTexture);
				debugsprite.node->setMaterialTexture(
					0, RenderManager::Get()->driver()->getTexture(debugsprite.sprite.c_str()));
				is_searching = false;
				g_currentSelectedTexture = "null";
			}

			break;
		}
	case ENTITY_COMPONENT::DESCRIPTOR:
		{
			if (!entity.hasComponent<DescriptorComponent>())
				return false;

			auto& descriptor = entity.getComponent<DescriptorComponent>();

			char buf[256];
			memset(buf, 0, 256);
			for (auto i = 0U; i < descriptor.name.size() && i < 256; i++) buf[i] = descriptor.name[i];

			std::string str_type;
			switch (descriptor.type)
			{
			case ET_NULL:    str_type = "NULL";    break;
			case ET_STATIC:  str_type = "STATIC";  break;
			case ET_DYNAMIC: str_type = "DYNAMIC"; break;
			case ET_PLAYER:  str_type = "PLAYER";  break;
			default:         str_type = "NULL";    break;
			}

			if (ImGui::BeginTable("##desc_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("ID");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%d", descriptor.id);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Name");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::PushID("DescriptorNameInput");
				if (ImGui::InputText("##desc_name", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					descriptor.name = buf;
				ImGui::PopID();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Type");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", str_type.c_str());
				ImGui::SameLine();
				int ect = static_cast<unsigned int>(descriptor.type);
				ImGui::PushID("DescriptorType");
				ImGui::SetNextItemWidth(60.0f);
				if (ImGui::InputInt("##desc_type", &ect, 1, 1))
				{
					ect = ect < 0 ? 0 : (ect > 2 ? 2 : ect);
					descriptor.type = static_cast<ENTITY_TYPE>(ect);
				}
				ImGui::PopID();

				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::INTERACTION:
		{
			if (!entity.hasComponent<InteractionComponent>())
				return false;

			ImGui::Text("Component contains no adjustable properties");
			break;
		}
	case ENTITY_COMPONENT::ITEM:
		{
			if (!entity.hasComponent<ItemComponent>())
				return false;

			ImGui::Text("Component contains no adjustable properties");
			break;
		}
	case ENTITY_COMPONENT::LIGHT:
		{
			if (!entity.hasComponent<LightComponent>())
				return false;

			auto& light = entity.getComponent<LightComponent>();

			{
				std::string str_type;
				switch (light.type)
				{
				case LT_POINT: str_type = "POINT"; break;
				case LT_SPOT:  str_type = "SPOT";  break;
				case LT_AREA:  str_type = "AREA";  break;
				default:       str_type = "NULL";  break;
				}
				ImGui::Text(std::string("Type: " + str_type).c_str());
			}
			{
				std::string str_type;
				switch (light.mode)
				{
				case LM_STATIC:         str_type = "STATIC";         break;
				case LM_DYNAMIC:        str_type = "DYNAMIC";        break;
				case LM_DYNAMIC_SHADOW: str_type = "DYNAMIC_SHADOW"; break;
				default:                str_type = "NULL";           break;
				}
				ImGui::Text(std::string("Mode: " + str_type).c_str());
			}

			if (ImGui::BeginTable("##light_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Radius");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##light_radius", &light.radius, 0.1f, 0.0f, "%.2f");

				if (light.type == LT_SPOT)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Inner Cone");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputFloat("##light_inner", &light.innerCone, 0.1f, 0.0f, "%.2f");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Outer Cone");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputFloat("##light_outer", &light.outerCone, 0.1f, 0.0f, "%.2f");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Attenuation");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputFloat("##light_atten", &light.falloff, 0.1f, 0.0f, "%.2f");
				}

				ImGui::EndTable();
			}

			ImColor color(light.color_diffuse.r, light.color_diffuse.g, light.color_diffuse.b, light.color_diffuse.a);
			ImColorPicker("", &color);
			light.color_diffuse.r = color.Value.x;
			light.color_diffuse.g = color.Value.y;
			light.color_diffuse.b = color.Value.z;
			light.color_diffuse.a = color.Value.w;

			ImGui::PushID("LIGHT_INTENSITY");
			ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 8.0f, "%.2f");
			ImGui::PopID();

			{
				bool coronaChanged = ImGui::Checkbox("Corona", &light.showCorona);
				if (light.showCorona) {
					ImGui::PushID("CORONA_PROPS");
					if (ImGui::BeginTable("##corona_tbl", 2, ImGuiTableFlags_SizingFixedFit)) {
						ImGui::TableSetupColumn("##cl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
						ImGui::TableSetupColumn("##cv", ImGuiTableColumnFlags_WidthStretch);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Size");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##corona_size", &light.coronaSize, 0.05f, 0.1f, 20.0f, "%.2f");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Full Range");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##corona_near", &light.coronaFadeNear, 0.25f, 0.0f, 200.0f, "%.1f");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Fade Range");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##corona_far", &light.coronaFadeFar, 0.5f, 0.0f, 500.0f, "%.1f");

						ImGui::EndTable();
					}
					ImGui::PopID();
				}
				if (coronaChanged)
					WorldManager::Get()->renderSystem()->setLightComponentData(entity);
			}

			light.update_component_data = true;
			WorldManager::Get()->renderSystem()->forceTransformUpdate();

			break;
		}
	case ENTITY_COMPONENT::LOGIC:
		{
			if (!entity.hasComponent<LogicComponent>())
				return false;

			auto& logic = entity.getComponent<LogicComponent>();

			char buf[256];
			memset(buf, 0, 256);
			for (auto i = 0U; i < logic.receiver.size() && i < 256; i++) buf[i] = logic.receiver[i];

			if (ImGui::BeginTable("##logic_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Event Receiver");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##logic_recv", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					logic.receiver = buf;
				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::MARKER:
		{
			if (!entity.hasComponent<MarkerComponent>())
				return false;

			auto& marker = entity.getComponent<MarkerComponent>();

			std::string str_type;
			switch (marker.type)
			{
			case MARKER_TYPE::MT_NULL:         str_type = "MT_NULL";             break;
			case MARKER_TYPE::MT_PLAYER_START: str_type = "MT_PLAYER_START";     break;
			case MARKER_TYPE::MT_FREECAMERA:   str_type = "MT_FREECAMERA_START"; break;
			}

			ImGui::Text(std::string("Type: " + str_type).c_str());

			break;
		}
	case ENTITY_COMPONENT::MESH:
		{
			if (!entity.hasComponent<MeshComponent>())
				return false;

			auto& mesh = entity.getComponent<MeshComponent>();
			{
				char buf[256];
				memset(buf, 0, 256);
				for (auto i = 0U; i < mesh.mesh.size() && i < 256; i++) buf[i] = mesh.mesh[i];
				ImGui::PushID("mesh_file");
				if (ImGui::InputText("Mesh", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					mesh.mesh = buf;
					WorldManager::Get()->renderSystem()->setMeshComponentData(entity);
					s_loadAnimList(mesh);
				}
				ImGui::SameLine();
				if (ImGui::Button("...##mesh_browse"))
				{
					std::string chosen = Utility::RemoveAbsDir(Utility::OpenFileDialog(dialog_filter_mesh, "content\\mesh"));
					if (!chosen.empty())
					{
						mesh.mesh = chosen;
						WorldManager::Get()->renderSystem()->setMeshComponentData(entity);
						s_loadAnimList(mesh);
					}
				}
				ImGui::PopID();
			}

			if (ImGui::Checkbox("Animated", &mesh.isAnimated))
			{
				WorldManager::Get()->renderSystem()->setMeshComponentData(entity);
				s_loadAnimList(mesh);
			}
			ImGui::Checkbox("Cast Shadows   ", &mesh.castShadows);
			ImGui::Checkbox("Receive Shadows", &mesh.receiveShadows);
			ImGui::Checkbox("Use Point Filtering", &mesh.texturePointFilter);
			ImGui::Checkbox("Recalculate Normals", &mesh.recalculateNormals);
			ImGui::Checkbox("Cook Navigation", &mesh.navCookable);

			ImGui::Text("Texture(s):");

			char buf[256];
			auto iter = 0U;
			for (auto& tstr : mesh.textures)
			{
				memset(buf, '\0', 256);

				ImGui::PushID(("texture_remove" + std::to_string(iter)).c_str());
				if (ImGui::Button(" - "))
				{
					if (iter > 0)
						mesh.textures.erase(mesh.textures.begin() + iter);
				}
				ImGui::PopID();

				ImGui::SameLine();

				for (auto c = 0U; c < tstr.size(); c++) buf[c] = tstr[c];
				ImGui::PushID(("textureInputManager::Get()_" + std::to_string(iter)).c_str());
				if (ImGui::InputText("", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					tstr = buf;
					if (mesh.node)
						mesh.node->setMaterialTexture(iter, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
				}
				ImGui::PopID();

				ImGui::SameLine();

				ImGui::PushID(("texture_browse_" + std::to_string(iter)).c_str());
				if (ImGui::Button("..."))
				{
					std::string chosen = Utility::RemoveAbsDir(Utility::OpenFileDialog(dialog_filter_image, "content\\texture"));
					if (!chosen.empty())
					{
						tstr = chosen;
						if (mesh.node)
							mesh.node->setMaterialTexture(iter, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
					}
				}
				ImGui::PopID();

				iter++;
			}

			if (ImGui::Button("Add"))
			{
				if (mesh.textures.size() < _IRR_MATERIAL_MAX_TEXTURES_)
					mesh.textures.emplace_back(std::string());
			}

			ImGui::Spacing();

			{
				const auto& allShaders = ShaderMaterialManager::getAll();
				std::vector<const char*> shaderNames;
				shaderNames.reserve(allShaders.size());
				for (const auto& s : allShaders)
					shaderNames.push_back(s.name.c_str());

				int currentIdx = 0;
				for (int si = 0; si < (int)shaderNames.size(); ++si)
					if (mesh.shaderName == shaderNames[si]) { currentIdx = si; break; }

				ImGui::SetNextItemWidth(-1);
				if (ImGui::Combo("##meshShader", &currentIdx, shaderNames.data(), (int)shaderNames.size()))
				{
					mesh.shaderName = shaderNames[currentIdx];
					auto mat = ShaderMaterialManager::get(mesh.shaderName);
					for (irr::u32 mi = 0; mi < mesh.node->getMaterialCount(); ++mi)
						mesh.node->getMaterial(mi).MaterialType = mat;
				}
			}

			ImGui::DragFloat("Roughness", &mesh.node->getMaterial(0).Shininess, 1.0f, 0.0f, 128.00);

			static auto specular = static_cast<irr::f32>(mesh.node->getMaterial(0).SpecularColor.getAlpha()) / 255.0f;
			if (ImGui::DragFloat("Emissive", &specular, 0.05f, 0.0f, 1.0f))
				mesh.node->getMaterial(0).SpecularColor.setAlpha(static_cast<irr::u32>(specular * 255.0f));

			ImGui::Spacing();

			if (mesh.isAnimated)
				ImGui::InputInt("FPS", &mesh.fps, 1.f, 1.f, 1);

			if (mesh.animationList.size() > 0)
			{
				static int s_previewAnimIdx = -1;

				if (!s_previewNode)
					s_previewAnimIdx = -1;

				if (ImGui::CollapsingHeader("Animations", ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (int i = 0; i < static_cast<int>(mesh.animationList.size()); i++)
					{
						const auto& an = mesh.animationList[i];
						ImGui::PushID(i);

						const bool isActive = (i == s_previewAnimIdx);

						if (isActive)
							ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "%-24s %d - %d",
							                  an.name.c_str(),
							                  static_cast<int>(an.frames.X),
							                  static_cast<int>(an.frames.Y));
						else
							ImGui::Text("%-24s %d - %d", an.name.c_str(),
							            static_cast<int>(an.frames.X),
							            static_cast<int>(an.frames.Y));

						ImGui::SameLine();

						if (ImGui::SmallButton(isActive ? "Playing" : "Play"))
						{
							if (s_previewNode)
							{
								s_previewAnimIdx = i;
								s_previewNode->setAnimationSpeed(static_cast<irr::f32>(mesh.fps));
								s_previewNode->setFrameLoop(an.frames.X, an.frames.Y);
								s_previewNode->setLoopMode(an.loop);
							}
						}

						ImGui::PopID();
					}
				}
			}

			break;
		}
	case ENTITY_COMPONENT::NPC:
		{
			if (!entity.hasComponent<NPCComponent>())
				return false;

			auto& npc = entity.getComponent<NPCComponent>();

			char npc_name_buf[256] = {};
			for (auto i = 0U; i < npc.name.length() && i < 256; i++) npc_name_buf[i] = npc.name[i];
			char npc_wp_buf[256] = {};
			for (auto i = 0U; i < npc.start_waypoint.length() && i < 256; i++) npc_wp_buf[i] = npc.start_waypoint[i];
			char npc_cwp_buf[256] = {};
			for (auto i = 0U; i < npc.current_waypoint.length() && i < 256; i++) npc_cwp_buf[i] = npc.current_waypoint[i];

			std::string npc_state_str;
			switch (npc.state)
			{
			case NPC_AI_STATE::INACTIVE: npc_state_str = "INACTIVE"; break;
			case NPC_AI_STATE::IDLE:     npc_state_str = "IDLE";     break;
			case NPC_AI_STATE::PATROL:   npc_state_str = "PATROL";   break;
			case NPC_AI_STATE::ALERT:    npc_state_str = "ALERT";    break;
			case NPC_AI_STATE::ATTACK:   npc_state_str = "ATTACK";   break;
			case NPC_AI_STATE::CHASE:    npc_state_str = "CHASE";    break;
			case NPC_AI_STATE::FLEE:     npc_state_str = "FLEE";     break;
			case NPC_AI_STATE::DEAD:     npc_state_str = "DEAD";     break;
			default:                     npc_state_str = "NULL";     break;
			}
			std::string npc_disp_str;
			switch (npc.disposition)
			{
			case NPC_AI_DISPOSITION::NEUTRAL:  npc_disp_str = "NEUTRAL";  break;
			case NPC_AI_DISPOSITION::ENEMY:    npc_disp_str = "ENEMY";    break;
			case NPC_AI_DISPOSITION::FRIENDLY: npc_disp_str = "FRIENDLY"; break;
			default:                           npc_disp_str = "NULL";     break;
			}

			if (ImGui::BeginTable("##npc_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Display Name");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##npc_name", npc_name_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					npc.name = npc_name_buf;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Vision Range");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##npc_vision", &npc.visionRange, 1, 10, "%.2f");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Chase Range");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##npc_chase", &npc.chaseRange, 1, 10, "%.2f");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Attack Range");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##npc_atk", &npc.attackRange, 1, 10, "%.2f");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Attack Delay");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##npc_delay", &npc.attackDelay, 1, 10, "%.2f");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("AI State");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", npc_state_str.c_str());
				ImGui::SameLine();
				int mtype = static_cast<unsigned int>(npc.state);
				ImGui::PushID(1);
				ImGui::SetNextItemWidth(50.0f);
				if (ImGui::InputInt("##npc_state", &mtype, 1, 1))
				{
					mtype = mtype < 0 ? 0 : (mtype > 7 ? 0 : mtype);
					npc.state = static_cast<NPC_AI_STATE>(mtype);
				}
				ImGui::PopID();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("AI Disposition");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", npc_disp_str.c_str());
				ImGui::SameLine();
				int mtype1 = static_cast<unsigned int>(npc.disposition);
				ImGui::PushID(2);
				ImGui::SetNextItemWidth(50.0f);
				if (ImGui::InputInt("##npc_disp", &mtype1, 1, 1))
				{
					mtype1 = mtype1 < 0 ? 0 : (mtype1 > 2 ? 0 : mtype1);
					npc.disposition = static_cast<NPC_AI_DISPOSITION>(mtype1);
				}
				ImGui::PopID();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Init Waypoint");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##npc_wp", npc_wp_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					npc.start_waypoint = npc_wp_buf;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Curr Waypoint");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##npc_cwp", npc_cwp_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					npc.current_waypoint = npc_cwp_buf;

				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::PHYSICS:
		{
			if (!entity.hasComponent<PhysicsComponent>())
				return false;

			auto& physics = entity.getComponent<PhysicsComponent>();

			std::string str_type;
			switch (physics.type)
			{
			case PHYSICS_COLLIDER_TYPE::PCT_BOX:      str_type = "BOX";      break;
			case PHYSICS_COLLIDER_TYPE::PCT_CAPSULE:  str_type = "CAPSULE";  break;
			case PHYSICS_COLLIDER_TYPE::PCT_CONVEX:   str_type = "CONVEX";   break;
			case PHYSICS_COLLIDER_TYPE::PCT_PLANE:    str_type = "PLANE";    break;
			case PHYSICS_COLLIDER_TYPE::PCT_SPHERE:   str_type = "SPHERE";   break;
			case PHYSICS_COLLIDER_TYPE::PCT_TRIANGLE: str_type = "TRIANGLE"; break;
			default:                                  str_type = "NULL";     break;
			}

			if (ImGui::BeginTable("##physics_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Collider");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", str_type.c_str());
				ImGui::SameLine();
				int pct = static_cast<unsigned int>(physics.type);
				ImGui::PushID("PhysicsType");
				ImGui::SetNextItemWidth(50.0f);
				if (ImGui::InputInt("##phys_type", &pct, 1, 1))
				{
					pct = pct < 0 ? 0 : (pct > 5 ? 5 : pct);
					physics.type = static_cast<PHYSICS_COLLIDER_TYPE>(pct);
				}
				ImGui::PopID();

				if (physics.type == PHYSICS_COLLIDER_TYPE::PCT_TRIANGLE)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Mesh");
					ImGui::TableSetColumnIndex(1);
					ImGui::TextDisabled("%s", physics.collisionMesh.empty() ? "None" : physics.collisionMesh.c_str());
					ImGui::SameLine();
					ImGui::PushID("LoadCollsionMeshButton");
					if (ImGui::Button("Load..."))
					{
						auto mesh = Utility::OpenFileDialog(dialog_filter_mesh, "content\\mesh");
						std::size_t found = mesh.find("content\\");
						if (found != std::string::npos)
							physics.collisionMesh = mesh.substr(found);
						else
							Utility::Warning("Assets must exist in local path!");
					}
					ImGui::PopID();
				}
				else
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Kinematic");
					ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##phys_kin", &physics.kinematic);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Density");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::DragFloat("##phys_density", &physics.density, 0.5f, 0.0f, 10.0f, "%.0f");
				}

				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::PREFAB:
		{
			if (!entity.hasComponent<PrefabComponent>())
				return false;

			auto& prefab = entity.getComponent<PrefabComponent>();

			char buf[256];
			memset(buf, 0, 256);
			for (auto i = 0U; i < prefab.parent.length() && i < 256; i++) buf[i] = prefab.parent[i];

			if (ImGui::BeginTable("##prefab_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Parent");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##prefab_parent", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					prefab.parent = buf;
					prefab.isChild = prefab.parent.length() > 0;
				}
				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::RENDER:
		{
			if (!entity.hasComponent<RenderComponent>())
				return false;

			auto& render = entity.getComponent<RenderComponent>();

			if (ImGui::BeginTable("##render_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Visible");
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Checkbox("##render_vis", &render.isVisible))
					WorldManager::Get()->renderSystem()->forceTransformUpdate();
				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::SCRIPT:
		{
			if (!entity.hasComponent<ScriptComponent>())
				return false;

			auto& script = entity.getComponent<ScriptComponent>();

			static bool invalid_script = false;
			char buf[256];
			memset(buf, 0, 256);
			for (auto i = 0U; i < script.script.size() && i < 256; i++) buf[i] = script.script[i];

			ImGui::Text("Script: ");
			ImGui::SameLine();
			ImGui::PushID("ScriptFile");
			if (ImGui::InputText("", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				script.script = buf;
				if (ScriptManager::Get()->compile(entity.getComponent<ScriptComponent>()) < 0)
					invalid_script = true;
				else
					invalid_script = false;
			}
			ImGui::PopID();
			ImGui::SameLine();
			if (ImGui::Button("...##script_browse"))
			{
				std::string chosen = Utility::RemoveAbsDir(Utility::OpenFileDialog(dialog_filter_script, "content\\script"));
				if (!chosen.empty())
					script.script = chosen;
			}

			if (invalid_script)
				ImGui::Text("Invalid script file!");

			ImGui::Spacing();

			unsigned int i = 0;
			for (auto gbls : script.globals)
			{
				bool exposed = false;
				for (auto gl_idx : script.exposed_global_indexes)
				{
					if (gbls.first == gl_idx)
					{
						exposed = true;
					}
				}

				if (exposed)
				{
					std::string var_name = gbls.second.substr(gbls.second.find_first_of(' ') + 1);

					if (var_name.size() > 2)
					{
						if (var_name.at(0) == 'g' && var_name.at(1) == '_')
							var_name = var_name.substr(2);
					}

					switch (script.global_values.at(i).first)
					{
					case AS_DATA_TYPE::INT:
						{
							auto* value = reinterpret_cast<int*>(script.global_values.at(i).second);
							ImGui::InputInt(var_name.c_str(), value, 1, 10);
							break;
						}
					case AS_DATA_TYPE::BOOL:
						{
							auto* value = reinterpret_cast<bool*>(script.global_values.at(i).second);
							ImGui::Checkbox(var_name.c_str(), value);
							break;
						}
					case AS_DATA_TYPE::FLOAT:
						{
							auto* value = reinterpret_cast<float*>(script.global_values.at(i).second);
							ImGui::InputFloat(var_name.c_str(), value, 0, 0, "%.2f");
							break;
						}
					case AS_DATA_TYPE::STRING:
						{
							auto* value = reinterpret_cast<std::string*>(script.global_values.at(i).second);
							char strbuf[256];
							memset(strbuf, 0, 256);
							for (auto j = 0U; j < value->size() && j < 256; j++) strbuf[j] = value->at(j);
							if (ImGui::InputText(var_name.c_str(), strbuf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
								value->assign(strbuf);
							break;
						}
					case AS_DATA_TYPE::VECTOR2:
						{
							auto* value = reinterpret_cast<irr::core::vector2df*>(script.global_values.at(i).second);
							float f2v[2] = { value->X, value->Y };
							if (ImGui::InputFloat2(var_name.c_str(), f2v, "%.2f"))
							{
								value->X = f2v[0];
								value->Y = f2v[1];
							}
							break;
						}
					case AS_DATA_TYPE::VECTOR3:
						{
							auto* value = reinterpret_cast<irr::core::vector3df*>(script.global_values.at(i).second);
							float f3v[3];
							value->getAs3Values(f3v);
							if (ImGui::InputFloat3(var_name.c_str(), f3v, "%.2f"))
							{
								value->X = f3v[0];
								value->Y = f3v[1];
								value->Z = f3v[2];
							}
							break;
						}
					default:
						break;
					}
				}

				i++;
			}

			break;
		}
	case ENTITY_COMPONENT::SOUND:
		{
			if (!entity.hasComponent<SoundComponent>())
				return false;

			auto& sound = entity.getComponent<SoundComponent>();

			int eraseIdx = -1;
			for (auto iter = 0U; iter < sound.sounds.size(); iter++)
			{
				auto& s = sound.sounds[iter];
				ImGui::PushID(static_cast<int>(iter));

				bool open = ImGui::TreeNode("##snd_tree", "%s", s.name.empty() ? "(unnamed)" : s.name.c_str());
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.10f, 0.10f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.15f, 0.15f, 1.0f));
				if (ImGui::SmallButton("X"))
					eraseIdx = static_cast<int>(iter);
				ImGui::PopStyleColor(2);

				if (open)
				{
					char nameBuf[256] = {};
					for (auto i = 0U; i < s.name.size() && i < 255; i++) nameBuf[i] = s.name[i];
					char fileBuf[256] = {};
					for (auto i = 0U; i < s.file.size() && i < 255; i++) fileBuf[i] = s.file[i];

					const float btnW = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;

					if (ImGui::BeginTable("##snd_props", 2, ImGuiTableFlags_SizingFixedFit))
					{
						ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
						ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Name");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::InputText("##snd_name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
							if (strlen(nameBuf) > 0) s.name = nameBuf;

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("File");
						ImGui::TableSetColumnIndex(1);
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
						if (ImGui::InputText("##snd_file", fileBuf, sizeof(fileBuf), ImGuiInputTextFlags_EnterReturnsTrue))
							s.file = fileBuf;
						ImGui::SameLine();
						if (ImGui::Button("...##snd_browse"))
						{
							std::string chosen = Utility::RemoveAbsDir(
								Utility::OpenFileDialog(dialog_filter_sound, "content\\sound"));
							if (!chosen.empty()) s.file = chosen;
						}

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Volume");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##snd_vol", &s.volume, 1.0f, 0.0f, 100.0f, "%.0f");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Min Dist");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##snd_mindist", &s.minDist, 0.1f, 0.1f, 100.0f, "%.1f");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("3D");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##snd_3d", &s.is3D);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Loop");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##snd_loop", &s.loop);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Start Paused");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##snd_paused", &s.startPaused);

						ImGui::EndTable();
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}

			if (eraseIdx >= 0)
				sound.sounds.erase(sound.sounds.begin() + eraseIdx);

			ImGui::Spacing();
			if (ImGui::Button("+ Add Sound"))
				sound.sounds.emplace_back();

			break;
		}
	case ENTITY_COMPONENT::SOUNDLISTENER:
		{
			if (!entity.hasComponent<SoundListenerComponent>())
				return false;

			ImGui::Text("Component contains no adjustable properties");
			break;
		}
	case ENTITY_COMPONENT::TRANSFORM:
		{
			if (!entity.hasComponent<TransformComponent>())
				return false;

			auto& transform = entity.getComponent<TransformComponent>();

			float fl3_pos[3]; transform.getPosition().getAs3Values(fl3_pos);
			float fl3_rot[3]; transform.getRotation().getAs3Values(fl3_rot);
			float fl3_scl[3]; transform.getScale().getAs3Values(fl3_scl);

			const ImVec4 ax_col[3][3] = {
				{ {0.40f,0.08f,0.08f,1.0f}, {0.55f,0.12f,0.12f,1.0f}, {0.65f,0.15f,0.15f,1.0f} },
				{ {0.08f,0.35f,0.08f,1.0f}, {0.12f,0.48f,0.12f,1.0f}, {0.15f,0.58f,0.15f,1.0f} },
				{ {0.08f,0.15f,0.45f,1.0f}, {0.12f,0.22f,0.60f,1.0f}, {0.15f,0.28f,0.72f,1.0f} },
			};
			auto xyzDrag = [&](const char** ids, float v[], float speed) -> bool {
				float fw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
				bool ch = false;
				for (int i = 0; i < 3; ++i) {
					if (i > 0) ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_FrameBg,        ax_col[i][0]);
					ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ax_col[i][1]);
					ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ax_col[i][2]);
					ImGui::SetNextItemWidth(fw);
					ch |= ImGui::DragFloat(ids[i], &v[i], speed, 0.0f, 0.0f, "%.3f");
					ImGui::PopStyleColor(3);
				}
				return ch;
			};

			if (ImGui::BeginTable("##transform_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				const char* pos_ids[] = { "##posX", "##posY", "##posZ" };
				const char* rot_ids[] = { "##rotX", "##rotY", "##rotZ" };
				const char* scl_ids[] = { "##sclX", "##sclY", "##sclZ" };

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Position");
				ImGui::TableSetColumnIndex(1);
				if (xyzDrag(pos_ids, fl3_pos, 0.1f))
				{
					transform.setPosition(irr::core::vector3df(fl3_pos[0], fl3_pos[1], fl3_pos[2]));
					WorldManager::Get()->renderSystem()->forceTransformUpdate();
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Rotation");
				ImGui::TableSetColumnIndex(1);
				if (xyzDrag(rot_ids, fl3_rot, 0.5f))
				{
					transform.setRotation(irr::core::vector3df(fl3_rot[0], fl3_rot[1], fl3_rot[2]));
					WorldManager::Get()->renderSystem()->forceTransformUpdate();
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Scale");
				ImGui::TableSetColumnIndex(1);
				if (xyzDrag(scl_ids, fl3_scl, 0.01f))
				{
					transform.setScale(irr::core::vector3df(fl3_scl[0], fl3_scl[1], fl3_scl[2]));
					WorldManager::Get()->renderSystem()->forceTransformUpdate();
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();

			if (transform.isChild)
			{
				auto &parent_ent = WorldManager::Get()->managerSystem()->getEntityByID(transform.parent);
				if (parent_ent.isValid())
				{
					ImGui::Text("--- Parent ---");
					ImGui::Spacing();
					auto pname = parent_ent.getComponent<DescriptorComponent>().name;
					auto pid = parent_ent.getComponent<DescriptorComponent>().id;
					ImGui::Text("Parent Name: %s", pname);
					ImGui::Text("Parent ID  : %i", pid);
				}
				else
				{
					ImGui::Text("Parent entity is not valid!");
				}
			}

			if (transform.isParent && transform.isChild)
			{
				ImGui::Spacing();
				ImGui::Spacing();
			}

			if (transform.isParent)
			{
				if (!transform.children.empty())
				{
					ImGui::Text("--- %i Children ---", transform.children.size());
					ImGui::Spacing();

					for (auto child_id : transform.children)
					{
						auto &child_ent = WorldManager::Get()->managerSystem()->getEntityByID(child_id);
						if (child_ent.isValid())
						{
							auto pname = child_ent.getComponent<DescriptorComponent>().name;
							auto pid   = child_ent.getComponent<DescriptorComponent>().id;
							ImGui::Text("   Child Name: %s", pname);
							ImGui::Text("   Child ID  : %i", pid);
						}
						else
						{
							ImGui::Text("   Child entity is not valid!");
						}

						ImGui::Spacing();
					}
				}

				static char buf[8];
				ImGui::InputText("Add Child by ID", buf, 8, ImGuiInputTextFlags_CharsDecimal);
				ImGui::SameLine();
				if (ImGui::Button("Add"))
				{
					auto &new_child = WorldManager::Get()->managerSystem()->getEntityByID(std::stoi(std::string(buf)));
					memset(buf, 0, 8);

					if (new_child.isValid())
					{
						if (new_child.hasComponent<TransformComponent>())
							transform.addChild(new_child.getComponent<TransformComponent>().node);
					}
				}
			}

			break;
		}
	case ENTITY_COMPONENT::TRIGGERZONE:
		{
			if (!entity.hasComponent<TriggerZoneComponent>())
				return false;

			auto& zone = entity.getComponent<TriggerZoneComponent>();

			char zone_ent_buf[256] = {};
			for (auto i = 0U; i < zone.entity.size() && i < 256; i++) zone_ent_buf[i] = zone.entity[i];
			char zone_trig_buf[256] = {};
			for (auto i = 0U; i < zone.triggered_entity.size() && i < 256; i++) zone_trig_buf[i] = zone.triggered_entity[i];

			std::string mask_type;
			switch (zone.mask)
			{
			case TRIGGER_ZONE_MASK::NONE:        mask_type = "NONE";        break;
			case TRIGGER_ZONE_MASK::PLAYER_ONLY: mask_type = "PLAYER_ONLY"; break;
			case TRIGGER_ZONE_MASK::ENTITY_NAME: mask_type = "ENTITY_NAME"; break;
			default:                             mask_type = "INVALID";     break;
			}

			if (ImGui::BeginTable("##zone_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Detect Entity");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##zone_ent", zone_ent_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					zone.entity = zone_ent_buf;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Trigger Entity");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##zone_trig", zone_trig_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					zone.triggered_entity = zone_trig_buf;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Mask");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", mask_type.c_str());
				ImGui::SameLine();
				int mtype = static_cast<unsigned int>(zone.mask);
				ImGui::PushID(1);
				ImGui::SetNextItemWidth(50.0f);
				if (ImGui::InputInt("##zone_mask", &mtype, 1, 1))
				{
					mtype = mtype < 0 ? 0 : (mtype > 2 ? 0 : mtype);
					zone.mask = static_cast<TRIGGER_ZONE_MASK>(mtype);
				}
				ImGui::PopID();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Single Use");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##zone_single", &zone.single_use);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Toggle");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##zone_toggle", &zone.toggle);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Invert Output");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##zone_invert", &zone.invert);

				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::DIALOG:
		{
			if (!entity.hasComponent<DialogComponent>())
				return false;

			auto& dialog_component = entity.getComponent<DialogComponent>();

			ImGui::Text("Dialog(s):");

			char buf[256];
			auto iter = 0U;
			for (auto& dstr : dialog_component.data)
			{
				memset(buf, '\0', 256);

				ImGui::PushID(("dialog_remove" + std::to_string(iter)).c_str());
				if (ImGui::Button(" - "))
				{
					if (iter > 0)
						dialog_component.data.erase(dialog_component.data.begin() + iter);
				}
				ImGui::PopID();

				ImGui::SameLine();

				for (auto c = 0U; c < dstr.size(); c++) buf[c] = dstr[c];
				ImGui::PushID(("textureInputManager::Get()_" + std::to_string(iter)).c_str());
				if (ImGui::InputText("", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					dstr = buf;
				ImGui::PopID();

				iter++;
			}

			if (ImGui::Button("Add"))
			{
				if (dialog_component.data.size() < 128)
					dialog_component.data.emplace_back(std::string());
			}

			break;
		}
	case ENTITY_COMPONENT::TWEEN:
		{
			if (!entity.hasComponent<TweenComponent>())
				return false;

			auto& tw = entity.getComponent<TweenComponent>();

			ImGui::Text("Buffer Index: %d", tw.movingBufferIndex);
			ImGui::SameLine();
			ImGui::TextDisabled("(requires reload to change)");
			ImGui::Spacing();

			float start_pos[3];
			tw.startPosition.getAs3Values(start_pos);
			if (ImGui::InputFloat3("Start Pos", start_pos, "%.3f"))
				tw.startPosition = irr::core::vector3df(start_pos[0], start_pos[1], start_pos[2]);

			float start_rot[3];
			tw.startRotation.getAs3Values(start_rot);
			if (ImGui::InputFloat3("Start Rot", start_rot, "%.3f"))
				tw.startRotation = irr::core::vector3df(start_rot[0], start_rot[1], start_rot[2]);

			ImGui::Spacing();

			float target_pos[3];
			tw.targetPosition.getAs3Values(target_pos);
			if (ImGui::InputFloat3("Target Pos", target_pos, "%.3f"))
				tw.targetPosition = irr::core::vector3df(target_pos[0], target_pos[1], target_pos[2]);

			float target_rot[3];
			tw.targetRotation.getAs3Values(target_rot);
			if (ImGui::InputFloat3("Target Rot", target_rot, "%.3f"))
				tw.targetRotation = irr::core::vector3df(target_rot[0], target_rot[1], target_rot[2]);

			ImGui::Spacing();
			ImGui::SliderFloat("Speed", &tw.speed, 0.01f, 1.0f, "%.2f");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::Text("t: %.2f  active: %s", tw.t, tw.active ? "true" : "false");
			if (ImGui::Button(tw.active ? "Set Inactive" : "Set Active"))
				tw.active = !tw.active;

			break;
		}
	case ENTITY_COMPONENT::NAVAGENT:
		{
			if (!entity.hasComponent<NavAgentComponent>())
				return false;

			auto& agent = entity.getComponent<NavAgentComponent>();

			ImGui::InputFloat("Move Speed",      &agent.moveSpeed,      0.1f, 1.0f, "%.2f");
			ImGui::InputFloat("Arrival Radius",  &agent.arrivalRadius,  0.05f, 0.25f, "%.2f");

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (agent.hasPath)
			{
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Following path");
				ImGui::Text("Waypoint %d / %d", agent.pathIndex + 1, static_cast<int>(agent.path.size()));
			}
			else
			{
				ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No active path");
			}

			break;
		}
	case ENTITY_COMPONENT::WATER:
		{
			if (!entity.hasComponent<WaterComponent>())
				return false;

			auto& water = entity.getComponent<WaterComponent>();

			bool changed = false;

			ImGui::Text("Shallow Color");
			ImColor shallowCol(water.shallowColor[0], water.shallowColor[1], water.shallowColor[2]);
			ImGui::PushID("water_shallow");
			if (ImColorPicker("", &shallowCol))
			{
				water.shallowColor[0] = shallowCol.Value.x;
				water.shallowColor[1] = shallowCol.Value.y;
				water.shallowColor[2] = shallowCol.Value.z;
				changed = true;
			}
			ImGui::PopID();

			ImGui::Spacing();

			ImGui::Text("Deep Color");
			ImColor deepCol(water.deepColor[0], water.deepColor[1], water.deepColor[2]);
			ImGui::PushID("water_deep");
			if (ImColorPicker("", &deepCol))
			{
				water.deepColor[0] = deepCol.Value.x;
				water.deepColor[1] = deepCol.Value.y;
				water.deepColor[2] = deepCol.Value.z;
				changed = true;
			}
			ImGui::PopID();

			if (changed && entity.hasComponent<MeshComponent>())
			{
				auto& mesh = entity.getComponent<MeshComponent>();
				if (mesh.node)
				{
					const auto& sc = water.shallowColor;
					const auto& dc = water.deepColor;
					for (irr::u32 i = 0; i < mesh.node->getMaterialCount(); ++i)
					{
						auto& mat = mesh.node->getMaterial(i);
						mat.AmbientColor.set(255,
							static_cast<irr::u32>(sc[0] * 255), static_cast<irr::u32>(sc[1] * 255), static_cast<irr::u32>(sc[2] * 255));
						mat.DiffuseColor.set(204,
							static_cast<irr::u32>(dc[0] * 255), static_cast<irr::u32>(dc[1] * 255), static_cast<irr::u32>(dc[2] * 255));
					}
				}
			}

			break;
		}
	case ENTITY_COMPONENT::PARTICLE:
		{
			if (!entity.hasComponent<ParticleComponent>())
				return false;

			auto& pc = entity.getComponent<ParticleComponent>();

			if (ImGui::BeginTable("##particle_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				// Effect Path
				char pathBuf[256] = {};
				for (auto i = 0U; i < pc.effectPath.size() && i < 255; i++) pathBuf[i] = pc.effectPath[i];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Effect Path");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x
					- ImGui::CalcTextSize("...").x
					- ImGui::GetStyle().FramePadding.x * 4.0f
					- ImGui::GetStyle().ItemSpacing.x);
				if (ImGui::InputText("##pc_path", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
					pc.effectPath = pathBuf;
				ImGui::SameLine();
				if (ImGui::Button("...##pc_browse"))
				{
					std::string chosen = Utility::RemoveAbsDir(
						Utility::OpenFileDialog("PSYS Files (*.psys)\0*.psys\0All Files\0*.*\0", "content\\particle"));
					if (!chosen.empty())
						pc.effectPath = chosen;
				}

				// Effect Name
				char nameBuf[256] = {};
				for (auto i = 0U; i < pc.effectName.size() && i < 255; i++) nameBuf[i] = pc.effectName[i];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Effect Name");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##pc_name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
					pc.effectName = nameBuf;

				// Loop
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Loop");
				ImGui::TableSetColumnIndex(1);
				ImGui::Checkbox("##pc_loop", &pc.loop);

				// Handle (read-only)
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Handle");
				ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%u", pc.handle);

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Editor preview: Play / Stop
			if (pc.handle != 0)
			{
				if (ImGui::Button("Stop##pc_stop") && ParticleManager::Get())
				{
					ParticleManager::Get()->destroy(pc.handle);
					pc.handle = 0;
				}
			}
			else
			{
				if (ImGui::Button("Play##pc_play") && ParticleManager::Get() && !pc.effectPath.empty())
				{
					const std::string& path = pc.effectPath;
					std::string name = pc.effectName;
					if (name.empty())
					{
						name = path;
						const auto sl = name.find_last_of("/\\");
						if (sl != std::string::npos) name = name.substr(sl + 1);
						if (name.size() > 5 && name.substr(name.size() - 5) == ".psys")
							name = name.substr(0, name.size() - 5);
					}

					ParticleManager::Get()->precache(name, path);

					irr::core::vector3df spawnPos(0.0f, 0.0f, 0.0f);
					if (entity.hasComponent<TransformComponent>())
					{
						auto& tc = entity.getComponent<TransformComponent>();
						if (tc.node) spawnPos = tc.node->getAbsolutePosition();
						else         spawnPos = tc.position;
					}

					pc.handle = ParticleManager::Get()->spawn(
						name,
						SPK::Vector3D(spawnPos.X, spawnPos.Y, spawnPos.Z),
						pc.loop);
				}
			}

			break;
		}
	default:
		return false;
	}

	return true;
}

void EditorInterface::add_component(ENTITY_COMPONENT component, anax::Entity& entity)
{
	switch (component)
	{
	case ENTITY_COMPONENT::AUTOKILL:            entity.addComponent<AutoKillComponent>();          break;
	case ENTITY_COMPONENT::BILLBOARDSPRITE:     entity.addComponent<BillboardSpriteComponent>();   break;
	case ENTITY_COMPONENT::CAMERA:              entity.addComponent<CameraComponent>();            break;
	case ENTITY_COMPONENT::CHARACTERCONTROLLER: entity.addComponent<CCTComponent>();               break;
	case ENTITY_COMPONENT::DAMAGERECEIVER:      entity.addComponent<DamageReceiverComponent>();    break;
	case ENTITY_COMPONENT::DATA:                entity.addComponent<DataComponent>();              break;
	case ENTITY_COMPONENT::DEBUGMESH:           entity.addComponent<DebugMeshComponent>();         break;
	case ENTITY_COMPONENT::DEBUGSPRITE:         entity.addComponent<DebugSpriteComponent>();       break;
	case ENTITY_COMPONENT::DESCRIPTOR:          entity.addComponent<DescriptorComponent>();        break;
	case ENTITY_COMPONENT::INTERACTION:         entity.addComponent<InteractionComponent>();       break;
	case ENTITY_COMPONENT::ITEM:                entity.addComponent<ItemComponent>();              break;
	case ENTITY_COMPONENT::LIGHT:               entity.addComponent<LightComponent>();             break;
	case ENTITY_COMPONENT::LOGIC:               entity.addComponent<LogicComponent>();             break;
	case ENTITY_COMPONENT::MARKER:              entity.addComponent<MarkerComponent>();            break;
	case ENTITY_COMPONENT::MESH:                entity.addComponent<MeshComponent>();              break;
	case ENTITY_COMPONENT::NPC:                 entity.addComponent<NPCComponent>();               break;
	case ENTITY_COMPONENT::PHYSICS:             entity.addComponent<PhysicsComponent>();           break;
	case ENTITY_COMPONENT::PREFAB:              entity.addComponent<PrefabComponent>();            break;
	case ENTITY_COMPONENT::RENDER:              entity.addComponent<RenderComponent>();            break;
	case ENTITY_COMPONENT::SCRIPT:              entity.addComponent<ScriptComponent>();            break;
	case ENTITY_COMPONENT::SOUND:               entity.addComponent<SoundComponent>();             break;
	case ENTITY_COMPONENT::SOUNDLISTENER:       entity.addComponent<SoundListenerComponent>();     break;
	case ENTITY_COMPONENT::TRANSFORM:           entity.addComponent<TransformComponent>();         break;
	case ENTITY_COMPONENT::TRIGGERZONE:         entity.addComponent<TriggerZoneComponent>();       break;
	case ENTITY_COMPONENT::DIALOG:              entity.addComponent<DialogComponent>();            break;
	case ENTITY_COMPONENT::TWEEN:               entity.addComponent<TweenComponent>();             break;
	case ENTITY_COMPONENT::NAVAGENT:            entity.addComponent<NavAgentComponent>();          break;
	case ENTITY_COMPONENT::WATER:               entity.addComponent<WaterComponent>();             break;
	case ENTITY_COMPONENT::PARTICLE:            entity.addComponent<ParticleComponent>();          break;
	}
}

bool EditorInterface::has_component(ENTITY_COMPONENT component, anax::Entity& entity)
{
	switch (component)
	{
	case ENTITY_COMPONENT::AUTOKILL:            return entity.hasComponent<AutoKillComponent>();
	case ENTITY_COMPONENT::BILLBOARDSPRITE:     return entity.hasComponent<BillboardSpriteComponent>();
	case ENTITY_COMPONENT::CAMERA:              return entity.hasComponent<CameraComponent>();
	case ENTITY_COMPONENT::CHARACTERCONTROLLER: return entity.hasComponent<CCTComponent>();
	case ENTITY_COMPONENT::DAMAGERECEIVER:      return entity.hasComponent<DamageReceiverComponent>();
	case ENTITY_COMPONENT::DATA:                return entity.hasComponent<DataComponent>();
	case ENTITY_COMPONENT::DEBUGMESH:           return entity.hasComponent<DebugMeshComponent>();
	case ENTITY_COMPONENT::DEBUGSPRITE:         return entity.hasComponent<DebugSpriteComponent>();
	case ENTITY_COMPONENT::DESCRIPTOR:          return entity.hasComponent<DescriptorComponent>();
	case ENTITY_COMPONENT::INTERACTION:         return entity.hasComponent<InteractionComponent>();
	case ENTITY_COMPONENT::ITEM:                return entity.hasComponent<ItemComponent>();
	case ENTITY_COMPONENT::LIGHT:               return entity.hasComponent<LightComponent>();
	case ENTITY_COMPONENT::LOGIC:               return entity.hasComponent<LogicComponent>();
	case ENTITY_COMPONENT::MARKER:              return entity.hasComponent<MarkerComponent>();
	case ENTITY_COMPONENT::MESH:                return entity.hasComponent<MeshComponent>();
	case ENTITY_COMPONENT::NPC:                 return entity.hasComponent<NPCComponent>();
	case ENTITY_COMPONENT::PHYSICS:             return entity.hasComponent<PhysicsComponent>();
	case ENTITY_COMPONENT::PREFAB:              return entity.hasComponent<PrefabComponent>();
	case ENTITY_COMPONENT::RENDER:              return entity.hasComponent<RenderComponent>();
	case ENTITY_COMPONENT::SCRIPT:              return entity.hasComponent<ScriptComponent>();
	case ENTITY_COMPONENT::SOUND:               return entity.hasComponent<SoundComponent>();
	case ENTITY_COMPONENT::SOUNDLISTENER:       return entity.hasComponent<SoundListenerComponent>();
	case ENTITY_COMPONENT::TRANSFORM:           return entity.hasComponent<TransformComponent>();
	case ENTITY_COMPONENT::TRIGGERZONE:         return entity.hasComponent<TriggerZoneComponent>();
	case ENTITY_COMPONENT::DIALOG:              return entity.hasComponent<DialogComponent>();
	case ENTITY_COMPONENT::TWEEN:               return entity.hasComponent<TweenComponent>();
	case ENTITY_COMPONENT::NAVAGENT:            return entity.hasComponent<NavAgentComponent>();
	case ENTITY_COMPONENT::WATER:               return entity.hasComponent<WaterComponent>();
	case ENTITY_COMPONENT::PARTICLE:            return entity.hasComponent<ParticleComponent>();
	default:                                    return false;
	}
}
