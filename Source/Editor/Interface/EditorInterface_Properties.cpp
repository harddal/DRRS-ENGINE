#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"

#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Renderer/ClusteredLightManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Renderer/Lightmapper/LightmapBaker.h"
#include "Engine/Prop/PropManager.h"

#include <string>
#include <sstream>

void EditorInterface::draw_window_prop_ent(bool display_override)
{
	if (display_override) { m_windowData.draw_window_prop_ent = true; }

	if (!m_windowData.draw_window_prop_ent) { return; }

	std::string label = "Entity Properties";

	/*if (g_sceneInteractor.isPropSelected())
	{
		label = "Prop Properties";
	}
	else
	{
		switch (static_cast<SELECTED_OBJECT_TYPE>(g_currentSelectedObjectType))
		{
		case SELECTED_OBJECT_TYPE::NONE:
			label = "Properties";
			break;
		case SELECTED_OBJECT_TYPE::ENTITY:
			label = "Entity Properties";
			break;
		case SELECTED_OBJECT_TYPE::MESH:
			label = "Mesh Properties";
			break;
		case SELECTED_OBJECT_TYPE::PREFAB:
			label = "Prefab Properties";
			break;
		default:
			label = "Properties";
			break;
		}
	}*/

	if (ImGui::Begin(label.c_str(), &m_windowData.draw_window_prop_ent))
	{
		// PROP
		if (g_sceneInteractor.isPropSelected() && PropManager::Get())
		{
			const uint32_t selectedId = g_sceneInteractor.getSelectedProp();
			StaticProp* prop = PropManager::Get()->getProp(selectedId);

			if (prop)
			{
				ImGui::TextDisabled("ID %u", prop->id);
				ImGui::Spacing();

				// Shared axis-color helper (X=red, Y=green, Z=blue)
				const ImVec4 ax[3][3] = {
					{ {0.40f,0.08f,0.08f,1.0f}, {0.55f,0.12f,0.12f,1.0f}, {0.65f,0.15f,0.15f,1.0f} },
					{ {0.08f,0.35f,0.08f,1.0f}, {0.12f,0.48f,0.12f,1.0f}, {0.15f,0.58f,0.15f,1.0f} },
					{ {0.08f,0.15f,0.45f,1.0f}, {0.12f,0.22f,0.60f,1.0f}, {0.15f,0.28f,0.72f,1.0f} },
				};
				auto xyzDrag = [&](const char** ids, float v[], float speed) -> bool {
					float fw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
					bool ch = false;
					for (int i = 0; i < 3; ++i) {
						if (i > 0) ImGui::SameLine();
						ImGui::PushStyleColor(ImGuiCol_FrameBg,        ax[i][0]);
						ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ax[i][1]);
						ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ax[i][2]);
						ImGui::SetNextItemWidth(fw);
						ch |= ImGui::DragFloat(ids[i], &v[i], speed, 0.0f, 0.0f, "%.3f");
						ImGui::PopStyleColor(3);
					}
					return ch;
				};

				// --- Mesh ---
				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("Mesh"))
				{
					static char meshBuf[256];
					strncpy_s(meshBuf, prop->mesh.c_str(), sizeof(meshBuf) - 1);
					if (ImGui::BeginTable("##pp_mesh", 2, ImGuiTableFlags_SizingFixedFit))
					{
						ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
						ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Path");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::InputText("##propmesh", meshBuf, sizeof(meshBuf), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							StaticProp copy = *prop;
							copy.mesh = meshBuf;
							PropManager::Get()->removeProp(selectedId);
							uint32_t newId = PropManager::Get()->addProp(copy);
							g_sceneInteractor.setSelectedProp(newId);
						}
						ImGui::EndTable();
					}
				}

				// --- Transform ---
				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("Transform"))
				{
					float pos[3] = { prop->position.X, prop->position.Y, prop->position.Z };
					float rot[3] = { prop->rotation.X, prop->rotation.Y, prop->rotation.Z };
					float scl[3] = { prop->scale.X,    prop->scale.Y,    prop->scale.Z    };
					bool changed = false;

					const char* pos_ids[] = { "##ppX", "##ppY", "##ppZ" };
					const char* rot_ids[] = { "##prX", "##prY", "##prZ" };
					const char* scl_ids[] = { "##psX", "##psY", "##psZ" };

					if (ImGui::BeginTable("##pp_xform", 2, ImGuiTableFlags_SizingFixedFit))
					{
						ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
						ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Position");
						ImGui::TableSetColumnIndex(1); if (xyzDrag(pos_ids, pos, 0.1f))  changed = true;

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Rotation");
						ImGui::TableSetColumnIndex(1); if (xyzDrag(rot_ids, rot, 0.5f))  changed = true;

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Scale");
						ImGui::TableSetColumnIndex(1); if (xyzDrag(scl_ids, scl, 0.01f)) changed = true;

						ImGui::EndTable();
					}

					if (changed)
					{
						prop->position = { pos[0], pos[1], pos[2] };
						prop->rotation = { rot[0], rot[1], rot[2] };
						prop->scale    = { scl[0], scl[1], scl[2] };
						PropManager::Get()->applyTransform(selectedId);
					}
				}

				// --- Rendering ---
				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("Rendering"))
				{
					static const char* shaderNames[] = { "phong_perpixel", "foliage", "grass", "phong_perpixel_transparent" };
					static const int   shaderCount   = 4;
					int currentShader = 0;
					for (int s = 0; s < shaderCount; s++)
						if (prop->defaultShader == shaderNames[s]) { currentShader = s; break; }

					if (ImGui::BeginTable("##pp_render", 2, ImGuiTableFlags_SizingFixedFit))
					{
						ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
						ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Shader");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::Combo("##propShader", &currentShader, shaderNames, shaderCount))
						{
							prop->defaultShader = shaderNames[currentShader];
							PropManager::Get()->applyShaders(selectedId);
						}

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Cull Dist.");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::InputFloat("##propCull", &prop->cullDistance, 0.0f, 0.0f, "%.1f"))
							if (prop->cullDistance < 0.0f) prop->cullDistance = 0.0f;

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Vegetation");
						ImGui::TableSetColumnIndex(1);
						if (ImGui::Checkbox("##propVeg", &prop->isVegetation))
							PropManager::Get()->applyShaders(selectedId);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Rcv Lightmap");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##propRlm", &prop->receivesLightmap);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Cast Shadows");
						ImGui::TableSetColumnIndex(1);
						ImGui::Checkbox("##propCs", &prop->castShadows);

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Collision");
						ImGui::TableSetColumnIndex(1);
						bool hc = prop->hasCollision;
						if (ImGui::Checkbox("##propHc", &hc))
						{
							prop->hasCollision = hc;
							PropManager::Get()->applyCollision(selectedId);
						}
						if (prop->hasCollision)
						{
							ImGui::SameLine();
							bool ucc = prop->useConvexCollision;
							if (ImGui::Checkbox("Convex##propUcc", &ucc))
							{
								prop->useConvexCollision = ucc;
								PropManager::Get()->applyCollision(selectedId);
							}
						}

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Roughness");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::DragFloat("##propRoughness", &prop->roughness, 1.0f, 0.0f, 128.0f))
							for (auto i = 0; i < prop->node->getMaterialCount(); i++)
								prop->node->getMaterial(i).Shininess = prop->roughness;

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Emissive");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::DragFloat("##propEmissive", &prop->emissive, 0.05f, 0.0f, 1.0f))
							for (auto i = 0; i < prop->node->getMaterialCount(); i++)
								prop->node->getMaterial(i).SpecularColor.setAlpha(
									static_cast<irr::u32>(prop->emissive * 255.0f));

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("UV Scroll");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						float uvScroll[2] = { prop->materialTypeParams[4], prop->materialTypeParams[5] };
						if (ImGui::DragFloat2("##propUVScroll", uvScroll, 0.01f, -5.0f, 5.0f, "%.2f"))
						{
							prop->materialTypeParams[4] = uvScroll[0];
							prop->materialTypeParams[5] = uvScroll[1];
							for (auto i = 0; i < prop->node->getMaterialCount(); i++) {
								prop->node->getMaterial(i).MaterialTypeParams[4] = uvScroll[0];
								prop->node->getMaterial(i).MaterialTypeParams[5] = uvScroll[1];
							}
						}

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Warp Amp");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::DragFloat("##propUVWarpAmp", &prop->materialTypeParams[6], 0.005f, 0.0f, 0.5f, "%.3f"))
							for (auto i = 0; i < prop->node->getMaterialCount(); i++)
								prop->node->getMaterial(i).MaterialTypeParams[6] = prop->materialTypeParams[6];

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Warp Speed");
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
						if (ImGui::DragFloat("##propUVWarpSpd", &prop->materialTypeParams[7], 0.1f, 0.0f, 20.0f, "%.1f"))
							for (auto i = 0; i < prop->node->getMaterialCount(); i++)
								prop->node->getMaterial(i).MaterialTypeParams[7] = prop->materialTypeParams[7];

						ImGui::EndTable();
					}
				}

				// --- Textures ---
				if (ImGui::CollapsingHeader("Textures"))
				{
					while (prop->textures.size() < 2)
						prop->textures.emplace_back("");

					auto* driver = RenderManager::Get()->driver();
					static char texBuf0[256], texBuf1[256];
					strncpy_s(texBuf0, prop->textures[0].c_str(), sizeof(texBuf0) - 1);
					strncpy_s(texBuf1, prop->textures[1].c_str(), sizeof(texBuf1) - 1);

					if (ImGui::BeginTable("##pp_tex", 2, ImGuiTableFlags_SizingFixedFit))
					{
						ImGui::TableSetupColumn("##tlbl", ImGuiTableColumnFlags_WidthFixed, 80.0f);
						ImGui::TableSetupColumn("##tval", ImGuiTableColumnFlags_WidthStretch);

						float btnW = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Diffuse");
						ImGui::TableSetColumnIndex(1);
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
						if (ImGui::InputText("##propTex0", texBuf0, sizeof(texBuf0), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							prop->textures[0] = texBuf0;
							prop->node->setMaterialTexture(0, driver->getTexture(texBuf0));
						}
						ImGui::SameLine();
						if (ImGui::Button("...##propTex0btn"))
						{
							std::string chosen = Utility::RemoveAbsDir(Utility::OpenFileDialog(dialog_filter_image, "content\\texture"));
							if (!chosen.empty())
							{
								prop->textures[0] = chosen;
								strncpy_s(texBuf0, chosen.c_str(), sizeof(texBuf0) - 1);
								prop->node->setMaterialTexture(0, driver->getTexture(chosen.c_str()));
							}
						}

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Normal Map");
						ImGui::TableSetColumnIndex(1);
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
						if (ImGui::InputText("##propTex1", texBuf1, sizeof(texBuf1), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							prop->textures[1] = texBuf1;
							prop->node->setMaterialTexture(1, driver->getTexture(texBuf1));
						}
						ImGui::SameLine();
						if (ImGui::Button("...##propTex1btn"))
						{
							std::string chosen = Utility::RemoveAbsDir(Utility::OpenFileDialog(dialog_filter_image, "content\\texture"));
							if (!chosen.empty())
							{
								prop->textures[1] = chosen;
								strncpy_s(texBuf1, chosen.c_str(), sizeof(texBuf1) - 1);
								prop->node->setMaterialTexture(1, driver->getTexture(chosen.c_str()));
							}
						}

						ImGui::EndTable();
					}
				}

				// --- Fresnel / Glass ---
				if (prop->defaultShader == "phong_perpixel_transparent")
				{
					ImGui::SetNextItemOpen(true, ImGuiCond_Once);
					if (ImGui::CollapsingHeader("Fresnel / Glass"))
					{
						if (ImGui::BeginTable("##pp_glass", 2, ImGuiTableFlags_SizingFixedFit))
						{
							ImGui::TableSetupColumn("##glbl", ImGuiTableColumnFlags_WidthFixed, 100.0f);
							ImGui::TableSetupColumn("##gval", ImGuiTableColumnFlags_WidthStretch);

							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Transparency");
							ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
							if (ImGui::DragFloat("##propGlassTrans", &prop->crystalTransparency, 0.01f, 0.0f, 1.0f, "%.2f"))
								for (auto i = 0; i < prop->node->getMaterialCount(); i++)
									prop->node->getMaterial(i).DiffuseColor.setAlpha(
										static_cast<irr::u32>(prop->crystalTransparency * 255.0f));

							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Fresnel Glow");
							ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
							if (ImGui::DragFloat("##propGlassGlow", &prop->crystalGlow, 0.01f, 0.0f, 5.0f, "%.2f"))
								for (auto i = 0; i < prop->node->getMaterialCount(); i++)
									prop->node->getMaterial(i).MaterialTypeParams[2] = prop->crystalGlow;

							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Fresnel Power");
							ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
							if (ImGui::DragFloat("##propGlassPower", &prop->crystalShimmer, 0.1f, 0.5f, 20.0f, "%.1f"))
								for (auto i = 0; i < prop->node->getMaterialCount(); i++)
									prop->node->getMaterial(i).MaterialTypeParams[3] = prop->crystalShimmer;

							ImGui::EndTable();
						}
					}
				}

				// --- Buffer Overrides ---
				if (ImGui::CollapsingHeader("Buffer Overrides"))
				{
					for (int i = 0; i < static_cast<int>(prop->bufferShaderOverrides.size()); i++)
					{
						auto& ovr = prop->bufferShaderOverrides[i];
						ImGui::PushID(i);

						int bufIdx = static_cast<int>(ovr.bufferIndex);
						static char ovrBuf[64];
						strncpy_s(ovrBuf, ovr.shaderName.c_str(), sizeof(ovrBuf) - 1);

						ImGui::PushItemWidth(50);
						if (ImGui::InputInt("##bufIdx", &bufIdx)) ovr.bufferIndex = static_cast<uint32_t>(bufIdx);
						ImGui::PopItemWidth();
						ImGui::SameLine();
						ImGui::PushItemWidth(120);
						if (ImGui::InputText("##ovrShader", ovrBuf, sizeof(ovrBuf), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							ovr.shaderName = ovrBuf;
							PropManager::Get()->applyShaders(selectedId);
						}
						ImGui::PopItemWidth();
						ImGui::SameLine();
						if (ImGui::SmallButton("X"))
						{
							prop->bufferShaderOverrides.erase(prop->bufferShaderOverrides.begin() + i);
							PropManager::Get()->applyShaders(selectedId);
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}
					if (ImGui::SmallButton("+ Add Override"))
						prop->bufferShaderOverrides.push_back({ 0, "phong_perpixel" });
				}

				// --- Delete ---
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.10f, 0.10f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.15f, 0.15f, 1.0f));
				if (ImGui::Button("Delete Prop", ImVec2(-1.0f, 0.0f)))
				{
					ImGui::PopStyleColor(2);
					PropManager::Get()->removeProp(selectedId);
					g_sceneInteractor.clearSelectedProp();
				}
				else ImGui::PopStyleColor(2);
			}
			else
			{
				ImGui::Text("No valid prop selected.");
			}
		}
		else
		{
		// MESH
		/*if (g_currentSelectedObjectType == static_cast<unsigned int>(SELECTED_OBJECT_TYPE::MESH) && g_currentMesh < _entity_null_value) {
		    auto mesh = WorldManager::Get()->getStaticMeshNode(g_currentMesh);

		    float fl3_pos[3];
		    mesh->getPosition().getAs3Values(fl3_pos);
		    if (ImGui::InputFloat3("Positon", fl3_pos, "%.2f")) {
		        mesh->setPosition(irr::core::vector3df(fl3_pos[0], fl3_pos[1], fl3_pos[2]));
		        WorldManager::Get()->renderSystem()->forceTransformUpdate();
		    }
		    float fl3_rot[3];
		    mesh->getRotation().getAs3Values(fl3_rot);
		    if (ImGui::InputFloat3("Rotation", fl3_rot, "%.2f")) {
		        mesh->setRotation(irr::core::vector3df(fl3_rot[0], fl3_rot[1], fl3_rot[2]));
		        WorldManager::Get()->renderSystem()->forceTransformUpdate();
		    }
		    float fl3_scl[3];
		    mesh->getScale().getAs3Values(fl3_scl);
		    if (ImGui::InputFloat3("Scale", fl3_scl, "%.2f")) {
		        mesh->setScale(irr::core::vector3df(fl3_scl[0], fl3_scl[1], fl3_scl[2]));
		        WorldManager::Get()->renderSystem()->forceTransformUpdate();
		    }
		}*/

		// ENTITY
		if (g_currentSelectedObjectType == static_cast<unsigned int>(SELECTED_OBJECT_TYPE::ENTITY) && g_currentEntity <
			_entity_null_value)
		{
			auto& entity = WorldManager::Get()->managerSystem()->getEntityByID(g_currentEntity);

			if (entity.isValid())
			{
				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<DescriptorComponent>() && ImGui::CollapsingHeader("Descriptor"))
				{
					draw_component_properties(ENTITY_COMPONENT::DESCRIPTOR, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<TransformComponent>() && ImGui::CollapsingHeader("Transform"))
				{
					draw_component_properties(ENTITY_COMPONENT::TRANSFORM, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<DataComponent>() && ImGui::CollapsingHeader("Data"))
				{
					draw_component_properties(ENTITY_COMPONENT::DATA, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<RenderComponent>() && ImGui::CollapsingHeader("Render"))
				{
					draw_component_properties(ENTITY_COMPONENT::RENDER, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<MeshComponent>() && ImGui::CollapsingHeader("Mesh"))
				{
					draw_component_properties(ENTITY_COMPONENT::MESH, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<CameraComponent>() && ImGui::CollapsingHeader("Camera"))
				{
					draw_component_properties(ENTITY_COMPONENT::CAMERA, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<LightComponent>() && ImGui::CollapsingHeader("Light"))
				{
					draw_component_properties(ENTITY_COMPONENT::LIGHT, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<PhysicsComponent>() && ImGui::CollapsingHeader("Physics"))
				{
					draw_component_properties(ENTITY_COMPONENT::PHYSICS, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<CCTComponent>() && ImGui::CollapsingHeader("CCT"))
				{
					draw_component_properties(ENTITY_COMPONENT::CHARACTERCONTROLLER, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<BillboardSpriteComponent>() && ImGui::CollapsingHeader("Billboard Sprite"))
				{
					draw_component_properties(ENTITY_COMPONENT::BILLBOARDSPRITE, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<DebugSpriteComponent>() && ImGui::CollapsingHeader("DebugSprite"))
				{
					draw_component_properties(ENTITY_COMPONENT::DEBUGSPRITE, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<DebugMeshComponent>() && ImGui::CollapsingHeader("DebugMesh"))
				{
					draw_component_properties(ENTITY_COMPONENT::DEBUGMESH, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<ScriptComponent>() && ImGui::CollapsingHeader("Script"))
				{
					draw_component_properties(ENTITY_COMPONENT::SCRIPT, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<LogicComponent>() && ImGui::CollapsingHeader("Logic Event"))
				{
					draw_component_properties(ENTITY_COMPONENT::LOGIC, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<SoundComponent>() && ImGui::CollapsingHeader("Sound"))
				{
					draw_component_properties(ENTITY_COMPONENT::SOUND, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<MarkerComponent>() && ImGui::CollapsingHeader("Marker"))
				{
					draw_component_properties(ENTITY_COMPONENT::MARKER, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<TriggerZoneComponent>() && ImGui::CollapsingHeader("Trigger Zone"))
				{
					draw_component_properties(ENTITY_COMPONENT::TRIGGERZONE, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<PrefabComponent>() && ImGui::CollapsingHeader("Prefab"))
				{
					draw_component_properties(ENTITY_COMPONENT::PREFAB, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<NPCComponent>() && ImGui::CollapsingHeader("NPC"))
				{
					draw_component_properties(ENTITY_COMPONENT::NPC, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<AutoKillComponent>() && ImGui::CollapsingHeader("Auto Kill"))
				{
					draw_component_properties(ENTITY_COMPONENT::AUTOKILL, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<DamageReceiverComponent>() && ImGui::CollapsingHeader("Damage Receiver"))
				{
					draw_component_properties(ENTITY_COMPONENT::DAMAGERECEIVER, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<ItemComponent>() && ImGui::CollapsingHeader("Item"))
				{
					draw_component_properties(ENTITY_COMPONENT::ITEM, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<SoundListenerComponent>() && ImGui::CollapsingHeader("Sound Listener"))
				{
					draw_component_properties(ENTITY_COMPONENT::SOUNDLISTENER, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<DialogComponent>() && ImGui::CollapsingHeader("Dialog"))
				{
					draw_component_properties(ENTITY_COMPONENT::DIALOG, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<TweenComponent>() && ImGui::CollapsingHeader("Tween"))
				{
					draw_component_properties(ENTITY_COMPONENT::TWEEN, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<NavAgentComponent>() && ImGui::CollapsingHeader("Nav Agent"))
				{
					draw_component_properties(ENTITY_COMPONENT::NAVAGENT, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<WaterComponent>() && ImGui::CollapsingHeader("Water"))
				{
					draw_component_properties(ENTITY_COMPONENT::WATER, entity);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (entity.hasComponent<BehaviorComponent>() && ImGui::CollapsingHeader("Behavior"))
				{
					draw_component_properties(ENTITY_COMPONENT::BEHAVIOR, entity);
				}

				ImGui::Separator();

				if (ImGui::Button("Add Component..."))
				{
					m_windowData.draw_window_add_component = true;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Opens tool to add a component to the selected entity.\n\nCAREFUL! Still contains bugs that may crash the editor.");
			}
			else
			{
				ImGui::Text("No valid entity selected");
			}
		}
		else
		{
			ImGui::Text("No Valid Entity Selected!");
		}
		} // end else (not prop)
	}
	ImGui::End();
}

void EditorInterface::draw_window_prop_scene()
{
	if (!m_windowData.draw_window_prop_scene) { return; }

	if (ImGui::Begin("Scene Properties", nullptr))
	{
		static auto scenedesc = WorldManager::Get()->getCurrentSceneDescriptor();

		char buf_name[256];
		memset(buf_name, 0, 256);
		for (auto i = 0U; i < scenedesc.name.size() && i < 256; i++)
		{
			buf_name[i] = scenedesc.name[i];
		}
		ImGui::PushID("SceneNameInput");
		if (ImGui::InputText("", buf_name, 256, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			scenedesc.name = buf_name;
		}
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Text("Name");

		char buf_creator[256];
		memset(buf_creator, 0, 256);
		for (auto i = 0U; i < scenedesc.creator.size() && i < 256; i++)
		{
			buf_creator[i] = scenedesc.creator[i];
		}
		ImGui::PushID("SceneCreatorInput");
		if (ImGui::InputText("", buf_creator, 256, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			scenedesc.creator = buf_creator;
		}
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Text("Creator");

		char buf_notes[256];
		memset(buf_notes, 0, 256);
		for (auto i = 0U; i < scenedesc.notes.size() && i < 256; i++)
		{
			buf_notes[i] = scenedesc.notes[i];
		}
		ImGui::PushID("SceneNotesInput");
		if (ImGui::InputText("", buf_notes, 256, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			scenedesc.notes = buf_notes;
		}
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Text("Notes");

		if (ImGui::Button("Set Skydome Texture"))
		{
			auto skydome = Utility::OpenFileDialog(dialog_filter_image, "content\\texture");

			//if (PathFileExistsA(skydome.c_str()))
			//{
			std::size_t found = skydome.find("content\\");

			if (found != std::string::npos)
			{
				auto skydome_path = skydome.substr(found);

				scenedesc.skydome_texture = skydome_path;
			}
			else
			{
				Utility::Warning("Assets must exist in local path!");
			}
			//}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		float ambArr[3] = { scenedesc.ambient_light.r, scenedesc.ambient_light.g, scenedesc.ambient_light.b };
		ImGui::ColorEdit3("Ambient Color", ambArr);
		scenedesc.ambient_light = irr::video::SColorf(ambArr[0], ambArr[1], ambArr[2], 1.0f);
		RenderManager::Get()->sceneManager()->setAmbientLight(scenedesc.ambient_light);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::Text("Bloom:");
		ImGui::SliderFloat("Threshold", &RenderManager::Get()->bloomBrightCallback()->threshold, 0.0f, 1.0f, "%.2f");
		ImGui::PushID("BLOOM_STR");
		ImGui::SliderFloat("Strength", &RenderManager::Get()->bloomCompositeCallback()->strength, 0.0f, 2.0f, "%.2f");
		ImGui::PopID();
		ImGui::Text("Tonemapping:");
		ImGui::SliderFloat("Exposure", &RenderManager::Get()->tonemapCallback()->exposure, 0.0f, 15.0f, "%.2f");
		ImGui::SliderFloat("White Point", &RenderManager::Get()->tonemapCallback()->whitePoint, 0.0f, 20.0f, "%.2f");
		ImGui::Text("Sharpening:");
		ImGui::PushID("SHARPEN_STR");
		ImGui::SliderFloat("Strength", &RenderManager::Get()->sharpenCallback()->strength, 0.0f, 2.0f, "%.2f");
		ImGui::PopID();
		ImGui::Text("Pixelation:");
		ImGui::Checkbox("##usePixelate", &scenedesc.usePixelate);
		RenderManager::Get()->setPixelateEnabled(scenedesc.usePixelate);
		ImGui::SameLine();
		ImGui::PushID("PIXELATE_SIZE");
		ImGui::SliderFloat("Size", &RenderManager::Get()->pixelateCallback()->pixelSize, 1.0f, 32.0f, "%.1f");
		ImGui::PopID();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// ---- Fog ----
		ImGui::Text("Fog:");
		{
			auto* cb = RenderManager::Get()->mainShaderCallback();
			float fogArr[3] = { cb->fogColor[0], cb->fogColor[1], cb->fogColor[2] };
			ImGui::ColorEdit3("Fog Color", fogArr);
			cb->fogColor[0] = fogArr[0]; cb->fogColor[1] = fogArr[1]; cb->fogColor[2] = fogArr[2];
			ImGui::PushID("FOG_DENSITY");
			ImGui::SliderFloat("Density",    &cb->fogDensity, 0.0f, 0.1f, "%.4f");
			ImGui::PopID();
			ImGui::PushID("FOG_START");
			ImGui::SliderFloat("Start Dist", &cb->fogStart,   0.0f, 200.0f, "%.1f");
			ImGui::PopID();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// ---- Color Grading ----
		ImGui::Text("Color Grading:");
		ImGui::Checkbox("##useColorGrade", &scenedesc.useColorGrade);
		RenderManager::Get()->setColorGradeEnabled(scenedesc.useColorGrade);
		ImGui::SameLine();
		ImGui::PushID("CG_SAT");
		ImGui::SliderFloat("Saturation", &RenderManager::Get()->colorGradeCallback()->saturation, 0.0f, 4.0f, "%.2f");
		ImGui::PopID();
		ImGui::PushID("CG_BRIGHT");
		ImGui::SliderFloat("Brightness", &RenderManager::Get()->colorGradeCallback()->brightness, -0.5f, 0.5f, "%.3f");
		ImGui::PopID();
		ImGui::ColorEdit3("Tint", RenderManager::Get()->colorGradeCallback()->colorTint);

		// ---- Posterize ----
		ImGui::Text("Posterize:");
		ImGui::Checkbox("##usePosterize", &scenedesc.usePosterize);
		RenderManager::Get()->setPosterizeEnabled(scenedesc.usePosterize);
		ImGui::SameLine();
		ImGui::PushID("PZ_LEVELS");
		ImGui::SliderFloat("Levels",   &RenderManager::Get()->posterizeCallback()->levels,   4.0f, 64.0f, "%.0f");
		ImGui::PopID();
		ImGui::PushID("PZ_STR");
		ImGui::SliderFloat("Strength", &RenderManager::Get()->posterizeCallback()->strength, 0.0f, 1.0f,  "%.2f");
		ImGui::PopID();

		// ---- Film Grain ----
		ImGui::Text("Film Grain:");
		ImGui::Checkbox("##useFilmGrain", &scenedesc.useFilmGrain);
		RenderManager::Get()->setFilmGrainEnabled(scenedesc.useFilmGrain);
		ImGui::SameLine();
		ImGui::PushID("FG_STR");
		ImGui::SliderFloat("Strength", &RenderManager::Get()->filmGrainCallback()->strength, 0.0f, 0.2f, "%.3f");
		ImGui::PopID();

		// ---- Lighting path (A/B toggle: clustered froxel textures vs. legacy 8-light gather) ----
		if (auto* clm = RenderManager::Get()->clusteredLights())
		{
			bool clustered = clm->isEnabled();
			if (ImGui::Checkbox("Clustered Lighting", &clustered))
				clm->setEnabled(clustered);
			if (clustered)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(%d lights)", clm->lightCount());
			}
		}

		// ---- SSAO (prepass + half-res AO applied before bloom/tonemap) ----
		{
			bool ssaoOn = RenderManager::Get()->isSSAOEnabled();
			if (ImGui::Checkbox("SSAO", &ssaoOn))
				RenderManager::Get()->setSSAOEnabled(ssaoOn);
			if (ssaoOn)
			{
				auto* cb = RenderManager::Get()->ssaoGenCallback();
				ImGui::PushID("SSAO_RADIUS");
				ImGui::SliderFloat("Radius", &cb->radius, 0.1f, 5.0f, "%.2f");
				ImGui::PopID();
				ImGui::PushID("SSAO_INTENSITY");
				ImGui::SliderFloat("Intensity", &cb->intensity, 0.0f, 2.0f, "%.2f");
				ImGui::PopID();
			}
		}

		// ---- Soft particles (checkbox = shader on/off; slider = fade distance) ----
		if (auto* spc = RenderManager::Get()->softParticleCallback())
		{
			static bool s_softParticles = RenderManager::Get()->softParticleShaderEnabled();
			if (ImGui::Checkbox("Soft Particles", &s_softParticles))
				ParticleManager::Get()->setSoftParticleShader(s_softParticles);
			if (s_softParticles)
			{
				ImGui::PushID("SOFT_PART");
				ImGui::SliderFloat("Fade Dist", &spc->softDistance, 0.0f, 2.0f, "%.2f");
				ImGui::PopID();
			}
		}

		//ImGui::Text("Lightmaps:");

		//static LightmapBaker::BakeSettings s_bakeSettings;
		//static int s_bakeResolution = static_cast<int>(s_bakeSettings.resolution);

		//ImGui::PushID("BakeResolution");
		//if (ImGui::InputInt("Resolution", &s_bakeResolution, 64, 256))
		//{
		//	s_bakeResolution = irr::core::clamp(s_bakeResolution, 64, 2048);
		//	s_bakeSettings.resolution = static_cast<uint32_t>(s_bakeResolution);
		//}
		//ImGui::PopID();

		//ImGui::PushID("BakeShadowBias");
		//ImGui::InputFloat("Shadow Bias", &s_bakeSettings.shadowBias, 0.001f, 0.01f, "%.4f");
		//ImGui::PopID();

		//// Flush any completed CPU bakes to the GPU every frame
		//if (LightmapBaker::isBaking() || LightmapBaker::hasPendingUploads())
		//	LightmapBaker::flushPendingUploads(RenderManager::Get()->driver());

		//const bool bakeRunning = LightmapBaker::isBaking() || LightmapBaker::hasPendingUploads();
		//if (bakeRunning)
		//{
		//	const float progress = LightmapBaker::getBakeProgress();
		//	char label[32];
		//	snprintf(label, sizeof(label), "%.0f%%", progress * 100.0f);
		//	ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), label);

		//	ImGui::BeginDisabled();
		//	ImGui::Button("Bake Lightmaps");
		//	ImGui::EndDisabled();
		//}
		//else
		//{
		//	if (ImGui::Button("Bake Lightmaps"))
		//	{
		//		spdlog::info("EditorInterface: starting lightmap bake...");
		//		LightmapBaker::bakeSceneAsync(s_bakeSettings);
		//	}
		//}

		//ImGui::Spacing();
		//ImGui::Text("NavMesh: ");
		//ImGui::SameLine();
		//if (NavigationManager::Get() && NavigationManager::Get()->isNavMeshBuilt())
		//	ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Baked");
		//else
		//	ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Not baked");

		//if (ImGui::Button("Bake NavMesh"))
		//{
		//	if (NavigationManager::Get())
		//	{
		//		NavigationManager::Get()->clearGeometry();

		//		int cookCount = 0;
		//		for (auto& entity : WorldManager::Get()->world()->getEntities())
		//		{
		//			if (!entity.hasComponent<MeshComponent>())
		//				continue;

		//			auto& mesh = entity.getComponent<MeshComponent>();
		//			if (!mesh.navCookable || !mesh.trimesh)
		//				continue;

		//			irr::core::matrix4 worldTransform;
		//			if (entity.hasComponent<TransformComponent>() && entity.getComponent<TransformComponent>().node)
		//				worldTransform = entity.getComponent<TransformComponent>().node->getAbsoluteTransformation();

		//			NavigationManager::Get()->addMeshGeometry(mesh.trimesh, worldTransform);
		//			++cookCount;
		//		}

		//		if (cookCount > 0)
		//		{
		//			spdlog::info("EditorInterface: baking navmesh from {} mesh(es)...", cookCount);
		//			NavigationManager::Get()->buildNavMesh();
		//		}
		//		else
		//		{
		//			spdlog::warn("EditorInterface: no meshes flagged as navCookable");
		//		}
		//	}
		//}

		//ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Save"))
		{
			RenderManager::Get()->swapSkyDomeTexture(scenedesc.skydome_texture);

			scenedesc.bloomThreshold    = RenderManager::Get()->bloomBrightCallback()->threshold;
			scenedesc.bloomStrength     = RenderManager::Get()->bloomCompositeCallback()->strength;
			scenedesc.tonemapExposure   = RenderManager::Get()->tonemapCallback()->exposure;
			scenedesc.tonemapwhitePoint = RenderManager::Get()->tonemapCallback()->whitePoint;
			scenedesc.sharpenStrength   = RenderManager::Get()->sharpenCallback()->strength;
			scenedesc.pixelateSize      = RenderManager::Get()->pixelateCallback()->pixelSize;

			{
				auto* cb = RenderManager::Get()->mainShaderCallback();
				scenedesc.fogDensity  = cb->fogDensity;
				scenedesc.fogStart    = cb->fogStart;
				scenedesc.fogColor    = irr::video::SColorf(cb->fogColor[0], cb->fogColor[1], cb->fogColor[2], 1.0f);
			}
			scenedesc.cgSaturation  = RenderManager::Get()->colorGradeCallback()->saturation;
			scenedesc.cgBrightness  = RenderManager::Get()->colorGradeCallback()->brightness;
			scenedesc.cgTintR       = RenderManager::Get()->colorGradeCallback()->colorTint[0];
			scenedesc.cgTintG       = RenderManager::Get()->colorGradeCallback()->colorTint[1];
			scenedesc.cgTintB       = RenderManager::Get()->colorGradeCallback()->colorTint[2];
			scenedesc.posterizeLevels   = RenderManager::Get()->posterizeCallback()->levels;
			scenedesc.posterizeStrength = RenderManager::Get()->posterizeCallback()->strength;
			scenedesc.filmGrainStrength = RenderManager::Get()->filmGrainCallback()->strength;

			WorldManager::Get()->setCurrentSceneDescriptor(scenedesc);
			scenedesc = WorldManager::Get()->getCurrentSceneDescriptor();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset"))
		{
			auto desc = WorldManager::Get()->getCurrentSceneDescriptor();
			WorldManager::Get()->setCurrentSceneDescriptor(desc);

			RenderManager::Get()->sceneManager()->setAmbientLight(desc.ambient_light);
			RenderManager::Get()->setPostProcessPassEnabled("fxaa", true);
			RenderManager::Get()->setBloomEnabled(true);
			RenderManager::Get()->bloomBrightCallback()->threshold = 1.0f;
			RenderManager::Get()->bloomCompositeCallback()->strength = 2.0f;
			RenderManager::Get()->setTonemapEnabled(true);
			RenderManager::Get()->tonemapCallback()->exposure = 10.0f;
			RenderManager::Get()->tonemapCallback()->whitePoint = 11.2f;
			RenderManager::Get()->setSharpenEnabled(true);
			RenderManager::Get()->sharpenCallback()->strength = 0.6f;
			RenderManager::Get()->setAutoExposure(false);

			{
				auto* cb = RenderManager::Get()->mainShaderCallback();
				cb->fogDensity  = desc.fogDensity;
				cb->fogStart    = desc.fogStart;
				cb->fogColor[0] = desc.fogColor.r;
				cb->fogColor[1] = desc.fogColor.g;
				cb->fogColor[2] = desc.fogColor.b;
			}
			RenderManager::Get()->setColorGradeEnabled(desc.useColorGrade);
			RenderManager::Get()->colorGradeCallback()->saturation    = desc.cgSaturation;
			RenderManager::Get()->colorGradeCallback()->brightness    = desc.cgBrightness;
			RenderManager::Get()->colorGradeCallback()->colorTint[0]  = desc.cgTintR;
			RenderManager::Get()->colorGradeCallback()->colorTint[1]  = desc.cgTintG;
			RenderManager::Get()->colorGradeCallback()->colorTint[2]  = desc.cgTintB;
			RenderManager::Get()->setPosterizeEnabled(desc.usePosterize);
			RenderManager::Get()->posterizeCallback()->levels   = desc.posterizeLevels;
			RenderManager::Get()->posterizeCallback()->strength = desc.posterizeStrength;
			RenderManager::Get()->setFilmGrainEnabled(desc.useFilmGrain);
			RenderManager::Get()->filmGrainCallback()->strength = desc.filmGrainStrength;

			scenedesc = WorldManager::Get()->getCurrentSceneDescriptor();
		}
	}
	ImGui::End();
}

void EditorInterface::draw_window_help_about()
{
	if (!m_windowData.draw_window_help_about) { return; }

	if (ImGui::Begin("About", &m_windowData.draw_window_help_about, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking))
	{
		ImGui::Text("Engine v%s", "0.3a"/*ENGINE_BUILD_VERSION""*/);
		ImGui::Text("Created by Dallas Hardwicke");
		ImGui::Spacing();
		// TODO: Get special message for IJG copyright
		ImGui::Text(
			"Special thanks to:\n\t- Irrlicht\n\t- NVIDIA\n\t- Anax\n\t- AngelScript\n\t- DearImGui\n\t- Boost\n\t- Cereal\n\t- AssImp\n\t- SoLoud\n\t- tinyXML\n\t- jpeglib\n\t- IJG\n\t- zlib\n\t- libPng");
		ImGui::Spacing();
		ImGui::Text(
			"ALL CODE CONTAINED HEREIN (THIS PROGRAM AND SUBSEQUENT LIBRARIES)\n"
			"IS PROPERTY OF ITS RIGHTFUL OWNER AND USED WITH PERMISSION WHERE NECCESARY\n"
			"USE OF OPEN SOURCE LIBRARIES IN THIS PROGRAM REQUIRE THAT THIS PROGRAMS SOURCE CODE BE MADE PUBLICLY AVAILABLE\n"
			"SOURCE CODE CAN BE ACCESSED AT: https://github.com/harddal/DRRS-ENGINE");
		ImGui::Spacing();
	}
	ImGui::End();
}
