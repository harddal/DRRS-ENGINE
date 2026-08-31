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
#include "Game/LogicLinks.h"
#include "Game/Gore/FractureManager.h"
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
			"Particle\0"
			"Behavior\0"
			"Skybox\0\0"; // 31

		ImGui::Combo("Component", &current_selected_component, component_list, 31);
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
					case ENTITY_COMPONENT::BEHAVIOR:
						if (entity.hasComponent<BehaviorComponent>()) break;
						entity.addComponent<BehaviorComponent>();
						break;
					case ENTITY_COMPONENT::SKYBOX:
						if (entity.hasComponent<SkyboxComponent>()) break;
						entity.addComponent<SkyboxComponent>();
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

// LINK button for an entity-name field: starts a one-shot pick handled by
// SceneInteractionManager (viewport Shift+Click or hierarchy click).
static void s_drawLinkPickButton(entityid hostId, LinkPickField field, const char* tooltip)
{
	ImGui::PushID(static_cast<int>(field));

	const bool pickingThis = g_sceneInteractor.isLinkPicking()
		&& g_sceneInteractor.linkPickField() == field
		&& g_sceneInteractor.linkPickHost() == hostId;

	if (pickingThis)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.10f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.65f, 0.15f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.45f, 0.05f, 1.0f));
		if (ImGui::SmallButton("Picking... (Esc)"))
			g_sceneInteractor.cancelLinkPick();
		ImGui::PopStyleColor(3);
	}
	else
	{
		if (ImGui::SmallButton("Link..."))
			g_sceneInteractor.beginLinkPick(hostId, field);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);
	}

	ImGui::PopID();
}

// Removable token chips for a comma-separated entity-name list. Chips turn red
// when the name resolves to no entity and yellow when it resolves to several.
// Shared with the brush editor panel (declared in EditorInterface_Internal.h).
void s_drawNameListChips(std::string& csv, const char* idLabel)
{
	if (csv.empty() || csv == "null")
		return;

	ImGui::PushID(idLabel);

	auto tokens = LogicLinks::splitNameList(csv);
	std::string removed;
	const float windowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

	for (auto i = 0U; i < tokens.size(); i++)
	{
		const auto& tok = tokens[i];
		const auto matches = WorldManager::Get()->managerSystem()->getEntitiesByName(tok).size();

		int styleCount = 0;
		if (matches == 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
			styleCount = 2;
		}
		else if (matches > 1)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.55f, 0.10f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.70f, 0.20f, 1.0f));
			styleCount = 2;
		}

		const std::string label = tok + " x";

		if (i > 0)
		{
			// Wrap chips when the next one would overflow the window
			const float nextWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth < windowRight)
				ImGui::SameLine();
		}

		ImGui::PushID(static_cast<int>(i));
		if (ImGui::SmallButton(label.c_str()))
			removed = tok;
		ImGui::PopID();

		if (ImGui::IsItemHovered())
		{
			if (matches == 0)
				ImGui::SetTooltip("No entity named '%s' - click to remove", tok.c_str());
			else if (matches > 1)
				ImGui::SetTooltip("%d entities share this name (all will activate) - click to remove", static_cast<int>(matches));
			else
				ImGui::SetTooltip("Click to remove");
		}

		if (styleCount > 0)
			ImGui::PopStyleColor(styleCount);
	}

	if (!removed.empty())
		LogicLinks::removeName(csv, removed);

	ImGui::PopID();
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
				ImGui::SetItemTooltip("Play the sprite as a flipbook animation.\nThe texture is treated as a sprite sheet and cut into frames of Frame Width x Frame Height pixels.");

				if (billboardsprite.animated)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Loop");
					ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##bb_loop", &billboardsprite.loop);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("FPS");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##bb_fps", &billboardsprite.fps);
					ImGui::SetItemTooltip("Flipbook playback speed in frames per second.");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Frame Width");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##bb_fw", &billboardsprite.split_x);
					ImGui::SetItemTooltip("Width in pixels of a single frame in the sprite sheet.");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Frame Height");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##bb_fh", &billboardsprite.split_y);
					ImGui::SetItemTooltip("Height in pixels of a single frame in the sprite sheet.");
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

			memset(buf, '\0', 256);
			for (auto c = 0U; c < tstr.size(); c++) buf[c] = tstr[c];

			if (ImGui::InputText("##bb_sprite_tex", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tstr = buf;
				billboardsprite.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
			}
			ImGui::SameLine();
			if (ImGui::Button("...##bb_sprite_browse"))
				show_window_texture_browser("mesh2d_sprite");

			if (g_textureBrowserRequestID == "mesh2d_sprite" && g_currentSelectedTexture != "null")
			{
				billboardsprite.sprite = g_currentSelectedTexture;
				billboardsprite.node->setMaterialTexture(
					0, RenderManager::Get()->driver()->getTexture(billboardsprite.sprite.c_str()));
				g_currentSelectedTexture = "null";
				g_textureBrowserRequestID.clear();
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
				ImGui::SetItemTooltip("When checked, this camera does not become the active view when it spawns.\nUse for cameras that are switched to later (cutscenes, sky camera, security monitors).");

				float fl3_off[3]; camera.offset.getAs3Values(fl3_off);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Position");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputFloat3("##cam_off", fl3_off, "%.2f"))
					camera.offset = irr::core::vector3df(fl3_off[0], fl3_off[1], fl3_off[2]);
				ImGui::SetItemTooltip("Camera position offset from the entity's origin.");

				float fl3_tar[3]; camera.target.getAs3Values(fl3_tar);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Rotation");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputFloat3("##cam_tar", fl3_tar, "%.2f"))
					camera.target = irr::core::vector3df(fl3_tar[0], fl3_tar[1], fl3_tar[2]);
				ImGui::SetItemTooltip("Look-at target point relative to the entity - a position the camera aims at,\nnot Euler angles (default 0,0,100 looks straight ahead).");

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
				ImGui::SetItemTooltip("Enable the physics character-controller capsule that moves this entity\nand collides it with the world (used by the player and walking NPCs).");
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
				ImGui::SetItemTooltip("Total damage this entity can take before it dies.\nAlso used as its starting health (default 100).");

				{
					static const char* impact_surface_names[IMPACT_SURFACE_COUNT] = {
						"Auto", "Flesh", "Wood", "Metal", "Stone", "Glass", "Dirt", "None"
					};

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Impact FX");
					ImGui::TableSetColumnIndex(1);
					ImGui::Combo("##impactsurface", &dam.impactSurface, impact_surface_names, IM_ARRAYSIZE(impact_surface_names));
					ImGui::SetItemTooltip("Particle + decal thrown by a hit that does NOT fracture the prop.\n"
					                      "Auto: the entity's behavior decides first (a creature behavior\n"
					                      "bleeds); otherwise it is classified from its first texture name\n"
					                      "(crate_wood -> Wood, ...). Set explicitly only when Auto guesses\n"
					                      "wrong. None = silent.");
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Fracture");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##fracture", &dam.fractureOnDeath);
				ImGui::SetItemTooltip("Break into shards on death instead of playing the gore effects.\n"
				                      "Static meshes only - a skinned mesh falls back to gibs.\n"
				                      "Shards are visual only: they cannot be shot and do not block the player.");

				if (dam.fractureOnDeath)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Shards");
					ImGui::TableSetColumnIndex(1); ImGui::SliderInt("##fracturecells", &dam.fractureCells, 2, 32);
					ImGui::SetItemTooltip("Target number of pieces. Clamped to 2-32 at fracture time.");

					const char* fracture_profiles[FRACTURE_PROFILE_COUNT];

					for (int fp = 0; fp < FRACTURE_PROFILE_COUNT; ++fp)
						fracture_profiles[fp] = fractureProfileName(fp);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Material");
					ImGui::TableSetColumnIndex(1);
					ImGui::Combo("##fractureprofile", &dam.fractureProfile, fracture_profiles, IM_ARRAYSIZE(fracture_profiles));
					ImGui::SetItemTooltip("How the shards bounce and tumble once they are in the air.");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Hollow");
					ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##fracturehollow", &dam.fractureHollow);
					ImGui::SetItemTooltip("Tick for props modelled as a thin shell with no wall thickness\n"
					                      "(barrels, pipes, most hollow props).\n"
					                      "Shell fragments are not capped and render double-sided.\n"
					                      "Leave off for anything modelled solid - those get the interior material.");
				}
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
			ImGui::SetItemTooltip("Add a free-form text slot. Slots hold arbitrary strings that\nscripts and game code can read from this entity.");

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
			if (ImGui::InputText("Mesh##debugmesh", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				debugmesh.mesh = buf;
				WorldManager::Get()->renderSystem()->setDebugMeshComponentData(entity);
			}

			auto tstr = debugmesh.texture;

			memset(buf, 0, 256);
			for (auto c = 0U; c < tstr.size(); c++) buf[c] = tstr[c];

			if (ImGui::InputText("##debugmesh_tex", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tstr = buf;
				debugmesh.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
			}
			ImGui::SameLine();
			if (ImGui::Button("...##debugmesh_browse"))
				show_window_texture_browser("debugmesh_texture");

			if (g_textureBrowserRequestID == "debugmesh_texture" && g_currentSelectedTexture != "null")
			{
				debugmesh.texture = g_currentSelectedTexture;
				debugmesh.node->setMaterialTexture(
					0, RenderManager::Get()->driver()->getTexture(debugmesh.texture.c_str()));
				g_currentSelectedTexture = "null";
				g_textureBrowserRequestID.clear();
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

			memset(buf, '\0', 256);
			for (auto c = 0U; c < tstr.size(); c++) buf[c] = tstr[c];

			if (ImGui::InputText("##debugsprite_tex", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tstr = buf;
				debugsprite.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
			}
			ImGui::SameLine();
			if (ImGui::Button("...##debugsprite_browse"))
				show_window_texture_browser("debugsprite_texture");

			if (g_textureBrowserRequestID == "debugsprite_texture" && g_currentSelectedTexture != "null")
			{
				debugsprite.sprite = g_currentSelectedTexture;
				debugsprite.node->setMaterialTexture(
					0, RenderManager::Get()->driver()->getTexture(debugsprite.sprite.c_str()));
				g_currentSelectedTexture = "null";
				g_textureBrowserRequestID.clear();
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
				ImGui::SetItemTooltip("0 = NULL, 1 = STATIC, 2 = DYNAMIC.\nSTATIC marks immovable scenery - only static entities are included in lightmap baking.\nDYNAMIC entities can move at runtime.");
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
				ImGui::SetItemTooltip("Reach of the light in world units - illumination falls off to zero at this distance.");

				if (light.type == LT_SPOT)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Inner Cone");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputFloat("##light_inner", &light.innerCone, 0.1f, 0.0f, "%.2f");
					ImGui::SetItemTooltip("Cone angle (degrees) that receives full brightness.");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Outer Cone");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputFloat("##light_outer", &light.outerCone, 0.1f, 0.0f, "%.2f");
					ImGui::SetItemTooltip("Cone angle (degrees) at which the light has faded to nothing.\nWiden the gap to Inner Cone for a softer spotlight edge.");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Attenuation");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::InputFloat("##light_atten", &light.falloff, 0.1f, 0.0f, "%.2f");
					ImGui::SetItemTooltip("Falloff exponent between the inner and outer cone - higher values give a sharper edge.");
				}

				ImGui::EndTable();
			}

			ImColor color(light.color_diffuse.r, light.color_diffuse.g, light.color_diffuse.b, light.color_diffuse.a);
			ImColorPicker("##light_color", &color);
			light.color_diffuse.r = color.Value.x;
			light.color_diffuse.g = color.Value.y;
			light.color_diffuse.b = color.Value.z;
			light.color_diffuse.a = color.Value.w;

			ImGui::PushID("LIGHT_INTENSITY");
			ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 8.0f, "%.2f");
			ImGui::PopID();

			{
				bool coronaChanged = ImGui::Checkbox("Corona", &light.showCorona);
				ImGui::SetItemTooltip("Draw a camera-facing glow sprite at the light's position\n(visible bulb glow / distant light flare).");
				if (light.showCorona) {
					ImGui::PushID("CORONA_PROPS");
					if (ImGui::BeginTable("##corona_tbl", 2, ImGuiTableFlags_SizingFixedFit)) {
						ImGui::TableSetupColumn("##cl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
						ImGui::TableSetupColumn("##cv", ImGuiTableColumnFlags_WidthStretch);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Size");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##corona_size", &light.coronaSize, 0.05f, 0.1f, 20.0f, "%.2f");
						ImGui::SetItemTooltip("World-space size of the glow sprite.");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Full Range");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##corona_near", &light.coronaFadeNear, 0.25f, 0.0f, 200.0f, "%.1f");
						ImGui::SetItemTooltip("Camera distance up to which the corona shows at full brightness.");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Fade Range");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##corona_far", &light.coronaFadeFar, 0.5f, 0.0f, 500.0f, "%.1f");
						ImGui::SetItemTooltip("Camera distance at which the corona has fully faded out\n(fades linearly between Full Range and this).");

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

			char buf[512];
			memset(buf, 0, 512);
			for (auto i = 0U; i < logic.receiver.size() && i < 511; i++) buf[i] = logic.receiver[i];

			if (ImGui::BeginTable("##logic_props", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Event Receiver");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##logic_recv", buf, 512, ImGuiInputTextFlags_EnterReturnsTrue))
					logic.receiver = buf;
				ImGui::SetItemTooltip("Comma-separated names of entities to activate when this entity fires\n(doors opening, lights toggling, sounds playing...).");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				s_drawLinkPickButton(entity.getComponent<DescriptorComponent>().id,
					LinkPickField::LOGIC_RECEIVER,
					"Pick an entity to add as receiver (Shift+Click in viewport or click in hierarchy)");
				ImGui::TableSetColumnIndex(1);
				s_drawNameListChips(logic.receiver, "##logic_recv_chips");

				ImGui::EndTable();
			}

			break;
		}
	case ENTITY_COMPONENT::MARKER:
		{
			if (!entity.hasComponent<MarkerComponent>())
				return false;

			auto& marker = entity.getComponent<MarkerComponent>();

			// Editable marker type. String order must match the MARKER_TYPE enum.
			const char* marker_types =
				"MT_NULL\0"
				"MT_PLAYER_START\0"
				"MT_FREECAMERA\0"
				"MT_WAYPOINT\0"
				"MT_SKY_CAMERA\0\0";
			int type_idx = static_cast<int>(marker.type);
			if (ImGui::Combo("Type", &type_idx, marker_types, 5))
				marker.type = static_cast<MARKER_TYPE>(type_idx);
			ImGui::SetItemTooltip("PLAYER_START - where the player spawns\nFREECAMERA - free-fly camera start point\nWAYPOINT - NPC patrol route node (link waypoints by name)\nSKY_CAMERA - anchor of the 3D skybox miniature");

			if (marker.type == MARKER_TYPE::MT_SKY_CAMERA)
			{
				ImGui::DragFloat("Sky Scale", &marker.skyScale, 0.5f, 1.0f, 1000.0f);
				ImGui::TextWrapped(
					"3D skybox parallax scale. The sky camera moves 1/scale as far "
					"as the player, so the tagged miniature reads as 'scale' times "
					"larger and farther. 16 is typical.");
			}

			break;
		}
	case ENTITY_COMPONENT::SKYBOX:
		{
			if (!entity.hasComponent<SkyboxComponent>())
				return false;

			ImGui::TextWrapped(
				"This entity's mesh is rendered into the 3D skybox miniature "
				"(distant, parallaxing background). Place an MT_SKY_CAMERA marker "
				"to set the anchor and parallax scale.");

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
			ImGui::SetItemTooltip("Load the mesh as an animated model and read its clips\nfrom the matching .anim file next to the mesh.");
			ImGui::Checkbox("Cast Shadows   ", &mesh.castShadows);
			ImGui::Checkbox("Receive Shadows", &mesh.receiveShadows);
			ImGui::Checkbox("Use Point Filtering", &mesh.texturePointFilter);
			ImGui::SetItemTooltip("Nearest-neighbour texture sampling - crisp pixelated texels\ninstead of smooth blurring. Good for low-res/retro textures.");
			ImGui::Checkbox("Recalculate Normals", &mesh.recalculateNormals);
			ImGui::SetItemTooltip("Rebuild vertex normals when the mesh loads.\nUse when an imported mesh shades wrong (black faces, inverted lighting).");
			ImGui::Checkbox("Cook Navigation", &mesh.navCookable);
			ImGui::SetItemTooltip("Include this mesh's triangles when building the NPC navigation mesh\n(walkable floors, blocking walls).");
			if (ImGui::Checkbox("Transparent", &mesh.transparent))
				WorldManager::Get()->renderSystem()->setMeshComponentData(entity);
			ImGui::SetItemTooltip("Draw with alpha blending in the transparent pass.\nRequired for the Opacity slider and for textures with soft alpha.");
			if (ImGui::Checkbox("Disable Z Write", &mesh.disableZDraw))
				WorldManager::Get()->renderSystem()->setMeshComponentData(entity);
			ImGui::SetItemTooltip("Don't write this mesh to the depth buffer, so it can't hide things drawn after it.\nUse for glow layers and overlapping transparent effects that sort badly.");

			// Opacity -> uAlpha (via DiffuseColor alpha). Only visible on transparent
			// material types; lets mappers fade cloud layers without re-exporting textures.
			if (ImGui::DragFloat("Opacity", &mesh.opacity, 0.01f, 0.0f, 1.0f, "%.2f"))
			{
				if (mesh.node)
				{
					const irr::u32 a = static_cast<irr::u32>(
						irr::core::clamp(mesh.opacity, 0.0f, 1.0f) * 255.0f);
					for (auto i = 0U; i < mesh.node->getMaterialCount(); i++)
						mesh.node->getMaterial(i).DiffuseColor.setAlpha(a);
				}
			}
			if (!mesh.transparent)
				ImGui::TextDisabled("(Opacity needs a transparent material)");

			// UV animation (MaterialTypeParams[4..7]) — drives uTexScroll/uTexWarp*
			// in phong_perpixel.frag. Used for scrolling cloud layers, water, etc.
			{
				// Tiling lives in params[0..1]; 0 means "untiled" in the shader, so
				// show it as 1.0 rather than a confusing 0.00 on untouched meshes.
				float uvTiling[2] = {
					mesh.materialTypeParams[0] != 0.0f ? mesh.materialTypeParams[0] : 1.0f,
					mesh.materialTypeParams[1] != 0.0f ? mesh.materialTypeParams[1] : 1.0f };
				bool tilingChanged = ImGui::DragFloat2("UV Tiling", uvTiling, 0.1f, -64.0f, 64.0f, "%.2f");
				ImGui::SetItemTooltip("How many times the texture repeats across the surface (1 = unchanged mapping).\nNegative values mirror the texture.");
				if (tilingChanged)
				{
					mesh.materialTypeParams[0] = uvTiling[0];
					mesh.materialTypeParams[1] = uvTiling[1];
					if (mesh.node)
						for (auto i = 0U; i < mesh.node->getMaterialCount(); i++)
						{
							mesh.node->getMaterial(i).MaterialTypeParams[0] = uvTiling[0];
							mesh.node->getMaterial(i).MaterialTypeParams[1] = uvTiling[1];
						}
				}

				float uvScroll[2] = { mesh.materialTypeParams[4], mesh.materialTypeParams[5] };
				bool scrollChanged = ImGui::DragFloat2("UV Scroll", uvScroll, 0.01f, -5.0f, 5.0f, "%.3f");
				ImGui::SetItemTooltip("Constant texture scroll speed in UVs per second - scrolling clouds,\nwaterfalls, conveyors. Needs a shader that supports scrolling\n(phong variants do; additive_color does not).");
				if (scrollChanged)
				{
					mesh.materialTypeParams[4] = uvScroll[0];
					mesh.materialTypeParams[5] = uvScroll[1];
					if (mesh.node)
						for (auto i = 0U; i < mesh.node->getMaterialCount(); i++)
						{
							mesh.node->getMaterial(i).MaterialTypeParams[4] = uvScroll[0];
							mesh.node->getMaterial(i).MaterialTypeParams[5] = uvScroll[1];
						}
				}

				if (ImGui::DragFloat("UV Warp Amp", &mesh.materialTypeParams[6], 0.005f, 0.0f, 0.5f, "%.3f"))
					if (mesh.node)
						for (auto i = 0U; i < mesh.node->getMaterialCount(); i++)
							mesh.node->getMaterial(i).MaterialTypeParams[6] = mesh.materialTypeParams[6];
				ImGui::SetItemTooltip("Strength of the sine-wave UV wobble (heat haze, underwater shimmer). 0 = off.");

				if (ImGui::DragFloat("UV Warp Speed", &mesh.materialTypeParams[7], 0.1f, 0.0f, 20.0f, "%.1f"))
					if (mesh.node)
						for (auto i = 0U; i < mesh.node->getMaterialCount(); i++)
							mesh.node->getMaterial(i).MaterialTypeParams[7] = mesh.materialTypeParams[7];
				ImGui::SetItemTooltip("How fast the UV wobble oscillates.");
			}

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
				ImGui::PushID(("mesh_tex_input_" + std::to_string(iter)).c_str());
				if (ImGui::InputText("##path", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					tstr = buf;
					if (mesh.node)
						mesh.node->setMaterialTexture(iter, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
				}
				ImGui::PopID();

				ImGui::SameLine();

				ImGui::PushID(("texture_browse_" + std::to_string(iter)).c_str());
				const std::string meshTexRequestId = "mesh_tex_" + std::to_string(iter);
				if (ImGui::Button("..."))
					show_window_texture_browser(meshTexRequestId);
				if (g_textureBrowserRequestID == meshTexRequestId && g_currentSelectedTexture != "null")
				{
					tstr = g_currentSelectedTexture;
					if (mesh.node)
						mesh.node->setMaterialTexture(iter, RenderManager::Get()->driver()->getTexture(tstr.c_str()));
					g_currentSelectedTexture = "null";
					g_textureBrowserRequestID.clear();
				}
				ImGui::PopID();

				iter++;
			}

			if (ImGui::Button("Add##mesh_texture"))
			{
				if (mesh.textures.size() < _IRR_MATERIAL_MAX_TEXTURES_)
					mesh.textures.emplace_back(std::string());
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("PBR Maps");

			{
				auto pbrRow = [&](const char* label, const char* id, std::string& path, int slot)
				{
					char pbr_buf[256] = {};
					auto n = path.size() < 255 ? path.size() : 255u;
					memcpy(pbr_buf, path.c_str(), n);

					ImGui::Text("%-9s", label);
					ImGui::SameLine();
					ImGui::PushID(id);
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 36);
					if (ImGui::InputText("##path", pbr_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					{
						path = pbr_buf;
						if (mesh.node && !path.empty())
							mesh.node->setMaterialTexture(slot, RenderManager::Get()->driver()->getTexture(path.c_str()));
					}
					ImGui::SameLine();
					const std::string pbrRequestId = id; // id is already a unique per-row tag, e.g. "pbr_n"
					if (ImGui::Button("..."))
						show_window_texture_browser(pbrRequestId);
					if (g_textureBrowserRequestID == pbrRequestId && g_currentSelectedTexture != "null")
					{
						path = g_currentSelectedTexture;
						if (mesh.node)
							mesh.node->setMaterialTexture(slot, RenderManager::Get()->driver()->getTexture(path.c_str()));
						g_currentSelectedTexture = "null";
						g_textureBrowserRequestID.clear();
					}
					ImGui::PopID();
				};

				pbrRow("Normal",    "pbr_n", mesh.texNormal,    SLOT_NORMAL);
				pbrRow("Roughness", "pbr_r", mesh.texRoughness, SLOT_ROUGHNESS);
				pbrRow("Metallic",  "pbr_m", mesh.texMetallic,  SLOT_METALLIC);
				pbrRow("Emission",  "pbr_e", mesh.texEmission,  SLOT_EMISSION);
			}

			ImGui::Separator();

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
				ImGui::SetItemTooltip("Shader material used to draw this mesh (lit, transparent, additive...).");
			}

			ImGui::DragFloat("Roughness", &mesh.node->getMaterial(0).Shininess, 1.0f, 0.0f, 128.00);
			ImGui::SetItemTooltip("Specular glossiness: 0 = matte, 128 = tight sharp highlight.\nA Roughness map, if assigned above, takes precedence per-pixel.");

			static auto specular = static_cast<irr::f32>(mesh.node->getMaterial(0).SpecularColor.getAlpha()) / 255.0f;
			if (ImGui::DragFloat("Emissive", &specular, 0.05f, 0.0f, 1.0f))
				mesh.node->getMaterial(0).SpecularColor.setAlpha(static_cast<irr::u32>(specular * 255.0f));
			ImGui::SetItemTooltip("Self-illumination amount (0-1): the surface glows regardless of scene lighting.\nAn Emission map, if assigned above, adds on top of this.");

			ImGui::Spacing();

			if (mesh.isAnimated)
			{
				ImGui::InputInt("FPS", &mesh.fps, 1.f, 1.f, 1);
				ImGui::SetItemTooltip("Animation playback rate in frames per second (initial value comes from the .anim file).");
			}

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
				ImGui::SetItemTooltip("Distance (world units) at which the NPC can spot its target and react.");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Chase Range");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##npc_chase", &npc.chaseRange, 1, 10, "%.2f");
				ImGui::SetItemTooltip("Maximum pursuit distance - beyond this the NPC gives up the chase.");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Attack Range");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##npc_atk", &npc.attackRange, 1, 10, "%.2f");
				ImGui::SetItemTooltip("Distance at which the NPC stops moving and starts attacking.");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Attack Delay");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				ImGui::InputFloat("##npc_delay", &npc.attackDelay, 1, 10, "%.2f");
				ImGui::SetItemTooltip("Time between attacks, in seconds.");

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
				ImGui::SetItemTooltip("Starting/current AI state:\n0 INACTIVE, 1 IDLE, 2 PATROL, 3 ALERT, 4 ATTACK, 5 CHASE, 6 FLEE, 7 DEAD");
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
				ImGui::SetItemTooltip("Attitude toward the player:\n0 NEUTRAL, 1 ENEMY (attacks on sight), 2 FRIENDLY");
				ImGui::PopID();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Init Waypoint");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##npc_wp", npc_wp_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					npc.start_waypoint = npc_wp_buf;
				ImGui::SetItemTooltip("Name of the WAYPOINT marker where this NPC begins its patrol route.");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Curr Waypoint");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##npc_cwp", npc_cwp_buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					npc.current_waypoint = npc_cwp_buf;
				ImGui::SetItemTooltip("Waypoint the NPC is currently heading to. Runtime state - normally leave alone.");

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
				ImGui::SetItemTooltip("Collision shape:\n0 BOX, 1 CAPSULE, 2 CONVEX (simplified hull), 3 PLANE, 4 SPHERE,\n5 TRIANGLE (exact static mesh - immovable, uses the collision mesh below)");
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
					ImGui::SetItemTooltip("Mesh used for exact triangle collision.\nCan be a simplified version of the visual mesh for performance.");
					ImGui::PopID();
				}
				else
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Kinematic");
					ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##phys_kin", &physics.kinematic);
					ImGui::SetItemTooltip("Body is moved only by code/animation: it pushes dynamic objects around\nbut ignores gravity and forces itself (moving platforms, doors).");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Density");
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
					ImGui::DragFloat("##phys_density", &physics.density, 0.5f, 0.0f, 10.0f, "%.0f");
					ImGui::SetItemTooltip("Mass per unit of volume - determines how heavy the body is\nand how hard it is to push.");
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
				ImGui::SetItemTooltip("Name of the parent entity this prefab member belongs to.\nLeave empty to make this entity the prefab root.");
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
			if (ImGui::InputText("##script_path", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				script.script = buf;
				script.script_data.clear();
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
				{
					script.script = chosen;
					script.script_data.clear();
					if (ScriptManager::Get()->compile(entity.getComponent<ScriptComponent>()) < 0)
						invalid_script = true;
					else
						invalid_script = false;
				}
			}

			ImGui::SameLine();
			ImGui::BeginDisabled(script.script.empty());
			if (ImGui::Button("Edit##script_edit_open"))
				EditorInterface::open_script_in_editor(script.script);
			ImGui::EndDisabled();

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

					// Unique per-slot ID: exposed globals can share a display name
					// with each other or with behavior properties in the same window
					ImGui::PushID(("script_gbl_" + std::to_string(i)).c_str());

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

					ImGui::PopID();
				}

				i++;
			}

			break;
		}
	case ENTITY_COMPONENT::BEHAVIOR:
		{
			if (!entity.hasComponent<BehaviorComponent>())
				return false;

			auto& bc = entity.getComponent<BehaviorComponent>();

			char typeBuf[128];
			memset(typeBuf, 0, sizeof(typeBuf));
			for (auto i = 0U; i < bc.behaviorType.size() && i < sizeof(typeBuf) - 1; i++)
				typeBuf[i] = bc.behaviorType[i];

			ImGui::Text("Behavior Type:");
			ImGui::SameLine();
			if (ImGui::InputText("##behaviortype", typeBuf, sizeof(typeBuf), ImGuiInputTextFlags_EnterReturnsTrue))
				bc.behaviorType = typeBuf;
			ImGui::SetItemTooltip("Name of the C++ behavior class to attach - must match a type\nregistered in the BehaviorFactory. Its properties appear below in game mode.");

			if (bc.behavior)
			{
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				int behaviorPropIdx = 0;
				for (auto prop : bc.behavior->getProperties())
				{
					// Unique per-property ID: names can collide with script globals
					// or component labels elsewhere in the window
					ImGui::PushID(("behavior_prop_" + std::to_string(behaviorPropIdx++)).c_str());

					switch (prop.type)
					{
					case BehaviorPropType::INT:
						if (ImGui::InputInt(prop.name.c_str(), static_cast<int*>(prop.ptr), 1, 10))
							bc.syncPropertyToMap(prop);
						break;
					case BehaviorPropType::FLOAT:
						if (ImGui::InputFloat(prop.name.c_str(), static_cast<float*>(prop.ptr), 0, 0, "%.2f"))
							bc.syncPropertyToMap(prop);
						break;
					case BehaviorPropType::BOOL:
						if (ImGui::Checkbox(prop.name.c_str(), static_cast<bool*>(prop.ptr)))
							bc.syncPropertyToMap(prop);
						break;
					case BehaviorPropType::STRING:
					{
						auto* str = static_cast<std::string*>(prop.ptr);
						char strbuf[256];
						memset(strbuf, 0, sizeof(strbuf));
						for (auto j = 0U; j < str->size() && j < sizeof(strbuf) - 1; j++) strbuf[j] = str->at(j);
						if (ImGui::InputText(prop.name.c_str(), strbuf, sizeof(strbuf), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							str->assign(strbuf);
							bc.syncPropertyToMap(prop);
						}
						break;
					}
					case BehaviorPropType::VECTOR3:
					{
						auto* v = static_cast<irr::core::vector3df*>(prop.ptr);
						float f3v[3];
						v->getAs3Values(f3v);
						if (ImGui::InputFloat3(prop.name.c_str(), f3v, "%.2f"))
						{
							v->X = f3v[0]; v->Y = f3v[1]; v->Z = f3v[2];
							bc.syncPropertyToMap(prop);
						}
						break;
					}
					default: break;
					}

					ImGui::PopID();
				}
			}
			else
			{
				ImGui::TextDisabled("(no behavior loaded — enter game mode)");
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
						ImGui::SetItemTooltip("Distance within which the sound plays at full volume;\nit attenuates with distance beyond this.");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("3D");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##snd_3d", &s.is3D);
						ImGui::SetItemTooltip("Positional audio: volume and panning follow the listener's distance and direction.\nUncheck for music and UI sounds.");

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Loop");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##snd_loop", &s.loop);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Start Paused");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##snd_paused", &s.startPaused);
						ImGui::SetItemTooltip("Load the sound silent; playback must be started later\nby a script or trigger.");

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
				ImGui::SetItemTooltip("Attach another entity as a transform child using its numeric entity ID\n(shown in that entity's Descriptor component).");
				ImGui::SameLine();
				if (ImGui::Button("Add##transform_child"))
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
			const auto zoneHostId = entity.getComponent<DescriptorComponent>().id;

			char zone_ent_buf[512] = {};
			for (auto i = 0U; i < zone.entity.size() && i < 511; i++) zone_ent_buf[i] = zone.entity[i];
			char zone_trig_buf[512] = {};
			for (auto i = 0U; i < zone.triggered_entity.size() && i < 511; i++) zone_trig_buf[i] = zone.triggered_entity[i];

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
				if (ImGui::InputText("##zone_ent", zone_ent_buf, 512, ImGuiInputTextFlags_EnterReturnsTrue))
					zone.entity = zone_ent_buf;
				ImGui::SetItemTooltip("Name of the entity whose position is tested against this zone.\nOnly used when Mask is 2 (ENTITY_NAME).");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				s_drawLinkPickButton(zoneHostId, LinkPickField::ZONE_DETECT_ENTITY,
					"Pick the entity to detect - replaces the current value");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Trigger Entity");
				ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##zone_trig", zone_trig_buf, 512, ImGuiInputTextFlags_EnterReturnsTrue))
					zone.triggered_entity = zone_trig_buf;
				ImGui::SetItemTooltip("Comma-separated names of entities activated when the zone fires\n(their Logic events fire, lights toggle, sounds play...).");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				s_drawLinkPickButton(zoneHostId, LinkPickField::ZONE_TRIGGERED_ENTITY,
					"Pick an entity to add as trigger target (Shift+Click in viewport or click in hierarchy)");
				ImGui::TableSetColumnIndex(1);
				s_drawNameListChips(zone.triggered_entity, "##zone_trig_chips");

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
				ImGui::SetItemTooltip("Who can fire the zone:\n0 NONE (disabled), 1 PLAYER_ONLY, 2 ENTITY_NAME (only the Detect Entity above)");
				ImGui::PopID();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Single Use");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##zone_single", &zone.single_use);
				ImGui::SetItemTooltip("Fire once and never reset.\nUncheck to let the zone fire again after being left.");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Toggle");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##zone_toggle", &zone.toggle);
				ImGui::SetItemTooltip("Each firing toggles the target's state (e.g. light on/off)\ninstead of setting it one way.");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Invert Output");
				ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##zone_invert", &zone.invert);
				ImGui::SetItemTooltip("Deactivate targets instead of activating them\n(e.g. turn a light off rather than on).");

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
				ImGui::PushID(("dialog_input_" + std::to_string(iter)).c_str());
				if (ImGui::InputText("##text", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
					dstr = buf;
				ImGui::PopID();

				iter++;
			}

			if (ImGui::Button("Add##dialog_line"))
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
			ImGui::SetItemTooltip("Which mesh buffer of the model is the moving part\n(e.g. the button cap, the switch lever). Set in the .ent file.");
			ImGui::SameLine();
			ImGui::TextDisabled("(requires reload to change)");
			ImGui::Spacing();

			float start_pos[3];
			tw.startPosition.getAs3Values(start_pos);
			if (ImGui::InputFloat3("Start Pos", start_pos, "%.3f"))
				tw.startPosition = irr::core::vector3df(start_pos[0], start_pos[1], start_pos[2]);
			ImGui::SetItemTooltip("Local position of the moving part when the tween is inactive (resting pose).");

			float start_rot[3];
			tw.startRotation.getAs3Values(start_rot);
			if (ImGui::InputFloat3("Start Rot", start_rot, "%.3f"))
				tw.startRotation = irr::core::vector3df(start_rot[0], start_rot[1], start_rot[2]);

			ImGui::Spacing();

			float target_pos[3];
			tw.targetPosition.getAs3Values(target_pos);
			if (ImGui::InputFloat3("Target Pos", target_pos, "%.3f"))
				tw.targetPosition = irr::core::vector3df(target_pos[0], target_pos[1], target_pos[2]);
			ImGui::SetItemTooltip("Local position when the tween is active (e.g. a button's pushed-in pose).");

			float target_rot[3];
			tw.targetRotation.getAs3Values(target_rot);
			if (ImGui::InputFloat3("Target Rot", target_rot, "%.3f"))
				tw.targetRotation = irr::core::vector3df(target_rot[0], target_rot[1], target_rot[2]);

			ImGui::Spacing();
			ImGui::SliderFloat("Speed", &tw.speed, 0.01f, 1.0f, "%.2f");
			ImGui::SetItemTooltip("Interpolation speed between start and target - higher = snappier.");
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
			ImGui::SetItemTooltip("Movement speed along the navmesh path, world units per second.");
			ImGui::InputFloat("Arrival Radius",  &agent.arrivalRadius,  0.05f, 0.25f, "%.2f");
			ImGui::SetItemTooltip("Distance at which a path corner counts as reached and the agent\nmoves on to the next one. Too small can cause circling on the spot.");

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
			if (ImColorPicker("##water_shallow_col", &shallowCol))
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
			if (ImColorPicker("##water_deep_col", &deepCol))
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
				ImGui::SetItemTooltip("Name the effect is precached/spawned under.\nLeave empty to use the .psys file name.");

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
	case ENTITY_COMPONENT::BEHAVIOR:            entity.addComponent<BehaviorComponent>();          break;
	case ENTITY_COMPONENT::SKYBOX:              entity.addComponent<SkyboxComponent>();            break;
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
	case ENTITY_COMPONENT::BEHAVIOR:            return entity.hasComponent<BehaviorComponent>();
	case ENTITY_COMPONENT::SKYBOX:              return entity.hasComponent<SkyboxComponent>();
	default:                                    return false;
	}
}
