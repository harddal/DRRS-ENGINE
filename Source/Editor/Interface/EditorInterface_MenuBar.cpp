#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"

#include <tinyxml2.h>
#include <boost/filesystem.hpp>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>

using namespace boost;
using namespace filesystem;

// Scans every .ent file under g_entity_path and inserts any fields that are present
// in a default-constructed entity but missing from the file (i.e. added after the file was authored).
// Uses the serializer itself as the schema source — fully automatic, no manual field list needed.
static void function_upgrade_entity_files()
{
    namespace tx2 = tinyxml2;

    auto* wm = WorldManager::Get();

    // --- Step 1: Build schema by serializing a default entity with every component ---
    anax::Entity schemaEntity = wm->world()->createEntity();

    // Descriptor must exist and isSerializable must be true for serializeEntity to write anything.
    schemaEntity.addComponent<DescriptorComponent>();
    auto& desc = schemaEntity.getComponent<DescriptorComponent>();
    desc.name          = "__schema__";
    desc.isSerializable = true;

    // Add every component that the serializers know about (engine + game).
    // The entity is never activated so no systems will process it — safe to add all at once.
    schemaEntity.addComponent<TransformComponent>();
    schemaEntity.addComponent<MeshComponent>();
    schemaEntity.addComponent<PhysicsComponent>();
    schemaEntity.addComponent<RenderComponent>();
    schemaEntity.addComponent<LightComponent>();
    schemaEntity.addComponent<CameraComponent>();
    schemaEntity.addComponent<CCTComponent>();
    schemaEntity.addComponent<BillboardSpriteComponent>();
    schemaEntity.addComponent<DebugMeshComponent>();
    schemaEntity.addComponent<DebugSpriteComponent>();
    schemaEntity.addComponent<NavAgentComponent>();
    schemaEntity.addComponent<TweenComponent>();
    schemaEntity.addComponent<ScriptComponent>();
    schemaEntity.addComponent<SoundListenerComponent>();
    schemaEntity.addComponent<SoundComponent>();
    schemaEntity.addComponent<AutoKillComponent>();
    schemaEntity.addComponent<DamageReceiverComponent>();
    schemaEntity.addComponent<DataComponent>();
    schemaEntity.addComponent<DialogComponent>();
    schemaEntity.addComponent<InteractionComponent>();
    schemaEntity.addComponent<ItemComponent>();
    schemaEntity.addComponent<LogicComponent>();
    schemaEntity.addComponent<MarkerComponent>();
    schemaEntity.addComponent<NPCComponent>();
    schemaEntity.addComponent<TriggerZoneComponent>();
    schemaEntity.addComponent<WaterComponent>();

    const std::string tempPath = g_entity_path + "_upgrade_schema_tmp_.ent";
    wm->exportEntity(schemaEntity, tempPath, false);
    wm->world()->killEntity(schemaEntity);

    // --- Step 2: Load schema DOM and index component elements by name ---
    tx2::XMLDocument schemaDoc;
    if (schemaDoc.LoadFile(tempPath.c_str()) != tx2::XML_SUCCESS)
    {
        spdlog::error("Entity upgrade: failed to generate schema (temp file: {})", tempPath);
        return;
    }

    // Structure: <cereal><entity><mesh>...</mesh>...</entity></cereal>
    std::unordered_map<std::string, tx2::XMLElement*> schemaComponents;
    {
        auto* cerealNode = schemaDoc.RootElement();
        auto* entityNode = cerealNode ? cerealNode->FirstChildElement("entity") : nullptr;
        if (entityNode)
        {
            for (auto* c = entityNode->FirstChildElement(); c; c = c->NextSiblingElement())
                schemaComponents[c->Name()] = c;
        }
    }

    boost::filesystem::remove(tempPath);

    if (schemaComponents.empty())
    {
        spdlog::error("Entity upgrade: schema was empty — nothing to do");
        return;
    }

    spdlog::info("Entity upgrade: schema has {} component types", schemaComponents.size());

    // --- Step 3: Patch every .ent file ---
    int filesModified = 0, fieldsAdded = 0;

    for (auto& entry : boost::filesystem::recursive_directory_iterator(g_entity_path))
    {
        if (entry.path().extension() != ".ent") continue;

        const std::string filePath = entry.path().string();
        const std::string fileName = entry.path().filename().string();

        tx2::XMLDocument fileDoc;
        if (fileDoc.LoadFile(filePath.c_str()) != tx2::XML_SUCCESS)
        {
            spdlog::warn("Entity upgrade: skipping unreadable file {}", fileName);
            continue;
        }

        bool fileModified = false;

        auto* cerealNode = fileDoc.RootElement();
        for (auto* fEntity = cerealNode ? cerealNode->FirstChildElement("entity") : nullptr;
             fEntity;
             fEntity = fEntity->NextSiblingElement("entity"))
        {
            // --- Migration: script component <file> -> <script> with full path ---
            // Old format: <script><value0><file>pickup/weapon_minigun</file></value0></script>
            // New format: <script><value0><script>content/script/pickup/weapon_minigun.asc</script></value0></script>
            if (auto* fScriptComp = fEntity->FirstChildElement("script"))
            {
                if (auto* fValue0 = fScriptComp->FirstChildElement("value0"))
                {
                    if (auto* fFile = fValue0->FirstChildElement("file"))
                    {
                        const char* oldVal = fFile->GetText();
                        if (oldVal && strlen(oldVal) > 0)
                        {
                            std::string newPath = "content/script/" + std::string(oldVal) + ".asc";
                            tx2::XMLElement* newScript = fileDoc.NewElement("script");
                            newScript->SetText(newPath.c_str());
                            fValue0->InsertAfterChild(fFile, newScript);
                            fValue0->DeleteChild(fFile);
                            spdlog::info("Entity upgrade: {}: script file -> {}", fileName, newPath);
                            ++fieldsAdded;
                            fileModified = true;
                        }
                    }
                }
            }

            // --- Migration: renamed fields (old lowercase/format -> new name, with optional bool fix) ---
            // Add entries here whenever a field is renamed in a serialize() method.
            static const struct { const char* comp; const char* oldName; const char* newName; bool isBool; } k_renames[] =
            {
                { "descriptor", "isdebug", "isDebug", true },
            };
            for (auto& r : k_renames)
            {
                auto* fComp = fEntity->FirstChildElement(r.comp);
                if (!fComp) continue;
                auto* fValue0 = fComp->FirstChildElement("value0");
                if (!fValue0) continue;
                auto* fOld = fValue0->FirstChildElement(r.oldName);
                if (!fOld) continue;

                // If the new name already exists (inserted by schema pass), remove it first
                if (auto* fExisting = fValue0->FirstChildElement(r.newName))
                    fValue0->DeleteChild(fExisting);

                // Rename by replacing the element
                const char* oldText = fOld->GetText();
                tx2::XMLElement* fNew = fileDoc.NewElement(r.newName);
                if (oldText)
                {
                    if (r.isBool)
                        fNew->SetText((strcmp(oldText, "1") == 0 || strcmp(oldText, "true") == 0) ? "true" : "false");
                    else
                        fNew->SetText(oldText);
                }
                fValue0->InsertAfterChild(fOld, fNew);
                fValue0->DeleteChild(fOld);
                spdlog::info("Entity upgrade: {}: renamed {}/{} -> {}", fileName, r.comp, r.oldName, r.newName);
                ++fieldsAdded;
                fileModified = true;
            }

            for (auto* fComp = fEntity->FirstChildElement(); fComp; fComp = fComp->NextSiblingElement())
            {
                auto it = schemaComponents.find(fComp->Name());
                if (it == schemaComponents.end()) continue;

                // Cereal wraps component fields in a <value0> child — work at that level
                tx2::XMLElement* fFields = fComp->FirstChildElement("value0");
                tx2::XMLElement* sFields = it->second->FirstChildElement("value0");
                if (!fFields || !sFields) continue;

                for (auto* sField = sFields->FirstChildElement(); sField; sField = sField->NextSiblingElement())
                {
                    const char* schemaText = sField->GetText();
                    tx2::XMLElement* fField = fFields->FirstChildElement(sField->Name());

                    if (!fField)
                    {
                        // Field is missing entirely — insert with schema default
                        tx2::XMLElement* newField = fileDoc.NewElement(sField->Name());
                        if (schemaText)
                            newField->SetText(schemaText);
                        fFields->InsertEndChild(newField);

                        spdlog::info("Entity upgrade: {}: added {}/{}", fileName, fComp->Name(), sField->Name());
                        ++fieldsAdded;
                        fileModified = true;
                    }
                    else if (schemaText)
                    {
                        // Field exists — if schema says it's a bool (true/false) but file has 0/1, fix it
                        const bool schemaisBool = (strcmp(schemaText, "true") == 0 || strcmp(schemaText, "false") == 0);
                        const char* fileText = fField->GetText();
                        if (schemaisBool && fileText)
                        {
                            const bool fileIsNumeric = (strcmp(fileText, "0") == 0 || strcmp(fileText, "1") == 0);
                            if (fileIsNumeric)
                            {
                                const char* corrected = (strcmp(fileText, "1") == 0) ? "true" : "false";
                                fField->SetText(corrected);
                                spdlog::info("Entity upgrade: {}: fixed bool {}/{}: {} -> {}", fileName, fComp->Name(), sField->Name(), fileText, corrected);
                                ++fieldsAdded;
                                fileModified = true;
                            }
                        }
                    }
                }
            }

            // --- Dedup pass: remove empty elements that have a non-empty sibling with the same name ---
            // This cleans up stale empty tags left by earlier upgrade runs (e.g. <script/> alongside
            // <script>content/script/...</script> inserted by the migration pass).
            for (auto* fComp = fEntity->FirstChildElement(); fComp; fComp = fComp->NextSiblingElement())
            {
                tx2::XMLElement* fFields = fComp->FirstChildElement("value0");
                if (!fFields) continue;

                std::vector<tx2::XMLElement*> toRemove;
                for (auto* field = fFields->FirstChildElement(); field; field = field->NextSiblingElement())
                {
                    // Only consider empty elements
                    if (field->GetText() != nullptr) continue;

                    // Check if a non-empty sibling with the same name exists
                    for (auto* other = fFields->FirstChildElement(field->Name()); other; other = other->NextSiblingElement(field->Name()))
                    {
                        if (other != field && other->GetText() != nullptr)
                        {
                            toRemove.push_back(field);
                            spdlog::info("Entity upgrade: {}: removed redundant empty <{}/> in {}", fileName, field->Name(), fComp->Name());
                            ++fieldsAdded;
                            fileModified = true;
                            break;
                        }
                    }
                }

                for (auto* el : toRemove)
                    fFields->DeleteChild(el);
            }
        }

        if (fileModified)
        {
            fileDoc.SaveFile(filePath.c_str());
            ++filesModified;
        }
    }

    spdlog::info("Entity upgrade complete: {} file(s) modified, {} field(s) added", filesModified, fieldsAdded);
}

void EditorInterface::draw_menubar_main()
{
	if (!m_windowData.draw_menubar_main) { return; }

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New"))
			{
				const int result = MessageBox(nullptr, "Destroy current scene?", "New Scene", MB_YESNO);
				switch (result)
				{
				case IDYES:
					WorldManager::Get()->killAllEntities();
					g_sceneInteractor.clearSelectedEntities();
					g_sceneInteractor.clearSelectedProp();
					break;
				default: break;
				}
			}
			if (ImGui::MenuItem("Open", "CTRL-O"))
			{
				function_open_scene();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save", "CTRL+S"))
			{
				funtion_save_scene();
			}
			if (ImGui::MenuItem("Save As..."))
			{
				function_save_scene_as();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Quit", "ALT+F4"))
			{
				Engine::Get()->exit();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "CTRL+Z", false, false))
			{
			}
			if (ImGui::MenuItem("Redo", "CTRL+Y", false, false))
			{
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Cut",    "CTRL+X")) { if (g_sceneInteractor.isPropSelected()) g_sceneInteractor.cutProp();   else g_sceneInteractor.cutEntity(); }
			if (ImGui::MenuItem("Copy",   "CTRL+C")) { if (g_sceneInteractor.isPropSelected()) g_sceneInteractor.copyProp();  else g_sceneInteractor.copyEntity(); }
			if (ImGui::MenuItem("Paste",  "CTRL+V")) { if (g_sceneInteractor.hasPropClipboard()) g_sceneInteractor.pasteProp(); else g_sceneInteractor.pasteEntity(); }
			if (ImGui::MenuItem("Delete", "DEL", false, !s_builderEntityCreated)) { g_sceneInteractor.deleteEntity(); }

			ImGui::Separator();

			if (ImGui::MenuItem("Select All", "CTRL+A", false, false))
			{
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Show/Hide Menubar", "CTRL+M"))
			{
				function_showhide_menubar();
			}


			ImGui::EndMenu();
		}


		if (ImGui::BeginMenu("Game"))
		{
			if (ImGui::MenuItem("Play", "CTRL+G"))
			{
				function_play_scene();
			}

			ImGui::EndMenu();
		}

		//if (ImGui::BeginMenu("Project")) { ImGui::EndMenu(); }

		if (ImGui::BeginMenu("Entity"))
		{
			if (ImGui::MenuItem("Entity Properties Menu", "CTRL+P")) { m_windowData.draw_window_prop_ent = true; }
			if (ImGui::MenuItem("Entity Spawn Menu", "CTRL+E")) { m_windowData.draw_window_spawn_entity = true; }
			if (ImGui::MenuItem("Entity Builder", "CTRL+B")) { m_windowData.draw_window_entity_builder = true; }
			if (ImGui::MenuItem("Prop Spawn Menu", "")) { m_windowData.draw_window_spawn_prop = true; }
			if (ImGui::MenuItem("Vegetation Painter", "")) { m_windowData.draw_window_vegetation_painter = true; }
			if (ImGui::MenuItem("Texture Painter",    "")) { m_windowData.draw_window_texture_painter    = true; }

			ImGui::Separator();

			if (ImGui::MenuItem("Link Entities", "CTRL+L"))
			{
				MessageBoxA(nullptr,
					"Feature not implemented!\n\nUse entity names in logic component.",
					"Link Entities",
					MB_OK | MB_ICONERROR);
			}

			if (ImGui::MenuItem("Format Entity Files"))
			{
				if (MessageBoxA(nullptr,
					"This will modify all .ent files in content/entity/ to add missing fields, fix bool values, and remove duplicate tags.\n\nA backup of content/entity/ will be created automatically before any changes are made.\n\nContinue?",
					"Format Entity Files",
					MB_YESNO | MB_ICONWARNING) == IDYES)
				{
					std::time_t t = std::time(nullptr);
					std::tm tm;
					localtime_s(&tm, &t);
					char timestamp[32];
					strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M", &tm);
					const boost::filesystem::path src  = g_entity_path;
					const boost::filesystem::path dest = "content/entity_backup_" + std::string(timestamp);
					try
					{
						boost::filesystem::copy(src, dest, boost::filesystem::copy_options::recursive);
						spdlog::info("Entity upgrade: backup created at {}", dest.string());
						function_upgrade_entity_files();
					}
					catch (const boost::filesystem::filesystem_error& e)
					{
						MessageBoxA(nullptr,
							("Backup failed — upgrade aborted.\n\n" + std::string(e.what())).c_str(),
							"Format Entity Files",
							MB_OK | MB_ICONERROR);
					}
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Scene"))
		{
			if (ImGui::MenuItem("Scene Properties", "CTRL+Q")) { m_windowData.draw_window_prop_scene = true; }
			if (ImGui::MenuItem("Scene Hierarchy", "CTRL+H")) { m_windowData.draw_window_hiearchy = true; }

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools"))
		{
			//if (ImGui::MenuItem("Prefab Spawn Menu", "CTRL+R")) { m_windowData.draw_window_spawn_prefab = true; }
			//if (ImGui::MenuItem("Mesh Spawn Menu", "")) { m_windowData.draw_window_spawn_mesh = true; }
			//if (ImGui::MenuItem("Texture Browser", "CTRL+T")) { draw_window_texture_browser(); }

			if (ImGui::MenuItem("Editor Settings")) { m_windowData.draw_window_editor_settings = true; }

			ImGui::Separator();

			if (ImGui::MenuItem("Console", "CTRL+TAB")) { m_windowData.draw_window_console = true; }
			if (ImGui::MenuItem("Log")) { m_windowData.draw_window_log = true; }
			if (ImGui::MenuItem("Scene Statistics", "")) { m_windowData.draw_window_scene_stats = true; }

			ImGui::Separator();

			if (ImGui::MenuItem("Script Editor", ""))
			{
				m_windowData.draw_window_script_editor = true;
			}

			if (ImGui::MenuItem("Particle Designer", ""))
				m_windowData.draw_window_particle_designer = true;

			if (ImGui::MenuItem("Export Script Functions"))
			{
				std::ofstream file(Utility::SaveFileDialog("Text Files\0*.txt\0Any File\0*.*\0"));

				for (auto i = 0U; i < ScriptManager::Get()->getEngine()->GetGlobalFunctionCount(); i++)
				{
					auto func = ScriptManager::Get()->getEngine()->GetGlobalFunctionByIndex(i);

					file << func->GetDeclaration(true, true, true) << ";\n";
				}

				file.close();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Widget"))
		{
			ImGui::Text("Mode: %s", g_sceneInteractor.getWidgetToolModeStr().c_str());
			if (ImGui::MenuItem("Translate", "CTRL+1"))
			{
				g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::TRANSLATE);
			}
			if (ImGui::MenuItem("Rotate", "CTRL+2"))
			{
				g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::ROTATE);
			}
			if (ImGui::MenuItem("Scale", "CTRL+3"))
			{
				g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::SCALE);
			}

			ImGui::Separator();

			ImGui::Text("Mode: %s", g_sceneInteractor.getWidgetCoordModeStr().c_str());
			if (ImGui::MenuItem("Local", "CTRL+4"))
			{
				g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::LOCAL);
			}
			if (ImGui::MenuItem("World", "CTRL+5"))
			{
				g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::WORLD);
			}

			ImGui::Separator();

			ImGui::Text(std::string(std::string("Snap: ") + (g_sceneInteractor.isSnap() ? "On" : "Off")).c_str());

			if (ImGui::MenuItem("Snap to Grid", "CTRL+6")) { g_sceneInteractor.useSnap(!g_sceneInteractor.isSnap()); }

			auto snap = g_sceneInteractor.getSnapUnit();
			if (ImGui::InputFloat("Unit", &snap)) { g_sceneInteractor.setSnap(snap); }

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window")) { ImGui::EndMenu(); }

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About", "")) { m_windowData.draw_window_help_about = true; }

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}
