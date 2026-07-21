#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include <irrlicht/source/Irrlicht/COpenGLTexture.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"

#include <string>

// ---- Entity Builder state ----
static anax::Entity     s_builderEntity;
bool                    s_builderEntityCreated  = false;  // extern in Internal.h — used by detectKeyShortcuts
static char             s_builderName[256]       = "new_entity";
static int              s_builderType            = 1;     // ET_STATIC
static bool             s_builderCompEnabled[28] = {};
static char             s_builderExportPath[512] = {};

// ---- Entity Builder preview state ----
static irr::scene::ISceneManager*          s_previewSM           = nullptr;
static irr::video::ITexture*               s_previewRTT          = nullptr;
static GLuint                              s_previewGLTex        = 0;
static irr::scene::ICameraSceneNode*       s_previewCamera       = nullptr;
irr::scene::IAnimatedMeshSceneNode*        s_previewNode         = nullptr;  // extern in Internal.h — used by draw_component_properties MESH case
static std::string                         s_previewLastMeshPath;
static float                               s_previewAzimuth      = 45.0f;
static float                               s_previewElevation    = 20.0f;
static float                               s_previewDistance     = 8.0f;
static irr::core::vector3df                s_previewPanOffset    = { 0.0f, 0.0f, 0.0f };

// ---- Component table ----
struct BuilderComponentEntry { const char* label; ENTITY_COMPONENT comp; bool locked; };
static const BuilderComponentEntry k_builderComponents[] = {
    { "Auto Kill",             ENTITY_COMPONENT::AUTOKILL,            false },
    { "Billboard Sprite",      ENTITY_COMPONENT::BILLBOARDSPRITE,     false },
    { "Camera",                ENTITY_COMPONENT::CAMERA,              false },
    { "Character Controller",  ENTITY_COMPONENT::CHARACTERCONTROLLER, false },
    { "Damage Receiver",       ENTITY_COMPONENT::DAMAGERECEIVER,      false },
    { "Data",                  ENTITY_COMPONENT::DATA,                false },
    { "Debug Mesh",            ENTITY_COMPONENT::DEBUGMESH,           false },
    { "Debug Sprite",          ENTITY_COMPONENT::DEBUGSPRITE,         false },
    { "Descriptor",            ENTITY_COMPONENT::DESCRIPTOR,          true  },
    { "Interaction",           ENTITY_COMPONENT::INTERACTION,         false },
    { "Item",                  ENTITY_COMPONENT::ITEM,                false },
    { "Light",                 ENTITY_COMPONENT::LIGHT,               false },
    { "Logic",                 ENTITY_COMPONENT::LOGIC,               false },
    { "Marker",                ENTITY_COMPONENT::MARKER,              false },
    { "Mesh",                  ENTITY_COMPONENT::MESH,                false },
    { "NPC",                   ENTITY_COMPONENT::NPC,                 false },
    { "Physics",               ENTITY_COMPONENT::PHYSICS,             false },
    { "Prefab",                ENTITY_COMPONENT::PREFAB,              false },
    { "Render",                ENTITY_COMPONENT::RENDER,              false },
    { "Script",                ENTITY_COMPONENT::SCRIPT,              false },
    { "Sound",                 ENTITY_COMPONENT::SOUND,               false },
    { "Sound Listener",        ENTITY_COMPONENT::SOUNDLISTENER,       false },
    { "Transform",             ENTITY_COMPONENT::TRANSFORM,           true  },
    { "Trigger Zone",          ENTITY_COMPONENT::TRIGGERZONE,         false },
    { "Dialog",                ENTITY_COMPONENT::DIALOG,              false },
    { "Tween",                 ENTITY_COMPONENT::TWEEN,               false },
    { "Nav Agent",             ENTITY_COMPONENT::NAVAGENT,            false },
    { "Water",                 ENTITY_COMPONENT::WATER,               false },
    { "Skybox",                ENTITY_COMPONENT::SKYBOX,              false },
};
static constexpr int k_builderComponentCount = 29;

static void s_previewEnsureInfra()
{
    if (s_previewSM) return;

    auto* mainSM = RenderManager::Get()->sceneManager();
    auto* driver = RenderManager::Get()->driver();

    s_previewSM = mainSM->createNewSceneManager(false);

    s_previewCamera = s_previewSM->addCameraSceneNode();
    s_previewCamera->setNearValue(0.1f);
    s_previewCamera->setFarValue(1000.0f);
    s_previewCamera->setAspectRatio(640.0f / 480.0f);

    s_previewSM->setAmbientLight(irr::video::SColorf(0.8f, 0.8f, 0.8f, 1.0f));

    s_previewRTT = driver->addRenderTargetTexture(
        irr::core::dimension2d<irr::u32>(640, 480), "builder_preview_rtt");

    s_previewGLTex = static_cast<irr::video::COpenGLTexture*>(s_previewRTT)->getOpenGLTextureName();
}

static void s_previewCreate()
{
    s_previewEnsureInfra();

    if (s_previewNode) { s_previewNode->remove(); s_previewNode = nullptr; }

    s_previewLastMeshPath = "";
    s_previewPanOffset    = irr::core::vector3df(0.0f, 0.0f, 0.0f);
    s_previewAzimuth      = 45.0f;
    s_previewElevation    = 20.0f;
    s_previewDistance     = 8.0f;

    RenderManager::Get()->setPreviewScene(s_previewSM, s_previewRTT);
}

static void s_previewDestroy()
{
    RenderManager::Get()->clearPreviewScene();

    if (s_previewNode) { s_previewNode->remove(); s_previewNode = nullptr; }
    s_previewLastMeshPath = "";
}

static void s_previewSyncNode()
{
    if (!s_previewSM || !s_builderEntityCreated) return;

    if (!s_builderEntity.hasComponent<MeshComponent>())
    {
        if (s_previewNode) { s_previewNode->remove(); s_previewNode = nullptr; }
        s_previewLastMeshPath = "";
        return;
    }

    auto& mc = s_builderEntity.getComponent<MeshComponent>();

    if (mc.node)
        mc.isPreview = true;

    if (mc.mesh != s_previewLastMeshPath)
    {
        if (s_previewNode) { s_previewNode->remove(); s_previewNode = nullptr; }

        if (mc.trimesh)
        {
            s_previewNode = s_previewSM->addAnimatedMeshSceneNode(mc.trimesh);
            s_previewNode->setPosition(irr::core::vector3df(0, 0, 0));
        }

        s_previewLastMeshPath = mc.mesh;
    }

    if (s_previewNode)
    {
        for (irr::u32 i = 0; i < static_cast<irr::u32>(mc.textures.size()); i++)
        {
            auto* tex = RenderManager::Get()->driver()->getTexture(mc.textures[i].c_str());
            s_previewNode->setMaterialTexture(i, tex);
        }
    }
}

static void s_previewUpdateCamera()
{
    if (!s_previewCamera) return;

    const float az = s_previewAzimuth   * (irr::core::PI / 180.0f);
    const float el = s_previewElevation * (irr::core::PI / 180.0f);
    const float r  = s_previewDistance;

    s_previewCamera->setPosition(irr::core::vector3df(
        r * cosf(el) * sinf(az),
        r * sinf(el),
        r * cosf(el) * cosf(az)) + s_previewPanOffset);
    s_previewCamera->setTarget(s_previewPanOffset);
}

static void s_builderKillEntity()
{
    s_previewDestroy();

    if (s_builderEntity.isValid())
    {
        if (s_builderEntity.hasComponent<DescriptorComponent>())
        {
            auto id = s_builderEntity.getComponent<DescriptorComponent>().id;
            g_sceneInteractor.removeFromSelection(id);
            WorldManager::Get()->freeEntityID(id);
        }
        WorldManager::Get()->world()->killEntity(s_builderEntity);
        WorldManager::Get()->world()->refresh();
        s_builderEntity = anax::Entity();
    }
    s_builderEntityCreated = false;
}

static void s_builderResetAll()
{
    s_builderKillEntity();
    memset(s_builderName, 0, sizeof(s_builderName));
    strncpy(s_builderName, "new_entity", sizeof(s_builderName) - 1);
    s_builderType = 1;
    memset(s_builderCompEnabled, 0, sizeof(s_builderCompEnabled));
    memset(s_builderExportPath, 0, sizeof(s_builderExportPath));
    const std::string defaultPath = g_entity_path + std::string("new_entity") + ".ent";
    strncpy(s_builderExportPath, defaultPath.c_str(), sizeof(s_builderExportPath) - 1);
}

void EditorInterface::draw_window_entity_builder()
{
    if (!m_windowData.draw_window_entity_builder) return;

    ImGui::Begin("Entity Builder", &m_windowData.draw_window_entity_builder);

    if (!s_builderEntityCreated)
    {
        // -------------------------------------------------------
        // PHASE 1: SETUP — no entity exists yet
        // -------------------------------------------------------
        ImGui::Text("Identity");
        ImGui::Separator();

        ImGui::PushID("BuilderName");
        if (ImGui::InputText("Name", s_builderName, sizeof(s_builderName), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const std::string p = g_entity_path + std::string(s_builderName) + ".ent";
            memset(s_builderExportPath, 0, sizeof(s_builderExportPath));
            strncpy(s_builderExportPath, p.c_str(), sizeof(s_builderExportPath) - 1);
        }
        ImGui::PopID();

        const char* typeList = "NULL\0STATIC\0DYNAMIC\0PLAYER\0MARKER\0DEBUG\0\0";
        ImGui::PushID("BuilderType");
        ImGui::Combo("Type", &s_builderType, typeList, 6);
        ImGui::SetItemTooltip("STATIC = immovable scenery (included in lightmap baking).\nDYNAMIC = can move at runtime.\nPLAYER/MARKER/DEBUG are for engine-managed special entities.");
        ImGui::PopID();

        ImGui::Spacing();
        ImGui::Text("Components");
        ImGui::Separator();
        ImGui::TextDisabled("Descriptor and Transform are always included");
        ImGui::Spacing();

        for (int i = 0; i < k_builderComponentCount; i++)
        {
            const auto& entry = k_builderComponents[i];
            ImGui::PushID(entry.label);
            if (entry.locked)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                bool dummy = true;
                ImGui::Checkbox(entry.label, &dummy);
                ImGui::PopStyleVar();
            }
            else
            {
                ImGui::Checkbox(entry.label, &s_builderCompEnabled[i]);
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool createClicked = ImGui::Button("Create Entity");
        ImGui::SetItemTooltip("Create a working copy with the checked components and open\nthe property editors + 3D preview. Nothing is saved until Export.");
        if (createClicked)
        {
            if (s_builderExportPath[0] == '\0')
            {
                const std::string p = g_entity_path + std::string(s_builderName) + ".ent";
                strncpy(s_builderExportPath, p.c_str(), sizeof(s_builderExportPath) - 1);
            }

            s_builderEntity = WorldManager::Get()->world()->createEntity();

            s_builderEntity.addComponent<DescriptorComponent>();
            auto& desc = s_builderEntity.getComponent<DescriptorComponent>();
            desc.id             = WorldManager::Get()->getNewID();
            desc.name           = std::string(s_builderName);
            desc.type           = static_cast<ENTITY_TYPE>(s_builderType);
            desc.isSerializable = false;
            desc.isDebug        = false;
            desc.isAlive        = true;

            s_builderEntity.addComponent<TransformComponent>();

            for (int i = 0; i < k_builderComponentCount; i++)
            {
                if (!k_builderComponents[i].locked && s_builderCompEnabled[i])
                    add_component(k_builderComponents[i].comp, s_builderEntity);
            }

            s_builderEntity.activate();
            WorldManager::Get()->world()->refresh();
            s_builderEntityCreated = true;
            s_previewCreate();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            s_builderResetAll();
    }
    else
    {
        // -------------------------------------------------------
        // PHASE 2: EDITING — entity exists, show property editors
        // -------------------------------------------------------

        if (s_previewGLTex && s_previewRTT)
        {
            s_previewSyncNode();
            s_previewUpdateCamera();

            ImVec2 previewSize(640, 480);
            ImVec2 previewPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##preview_orbit", previewSize);
            ImGui::GetWindowDrawList()->AddImage(
                ImTextureRef((ImTextureID)(uintptr_t)s_previewGLTex), previewPos,
                ImVec2(previewPos.x + previewSize.x, previewPos.y + previewSize.y));

            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsItemActive())
            {
                s_previewAzimuth   -= io.MouseDelta.x * 0.5f;
                s_previewElevation += io.MouseDelta.y * 0.5f;
                s_previewElevation  = irr::core::clamp(s_previewElevation, -89.0f, 89.0f);
            }
            if (ImGui::IsItemHovered())
            {
                if (io.MouseWheel != 0.0f)
                {
                    s_previewDistance -= io.MouseWheel * 0.5f;
                    s_previewDistance  = irr::core::clamp(s_previewDistance, 0.5f, 100.0f);
                }
                if (io.MouseDown[2] && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
                {
                    const float az = s_previewAzimuth   * (irr::core::PI / 180.0f);
                    const float el = s_previewElevation * (irr::core::PI / 180.0f);

                    const irr::core::vector3df right( cosf(az), 0.0f, -sinf(az));
                    const irr::core::vector3df up(-sinf(el) * sinf(az), cosf(el), -sinf(el) * cosf(az));

                    const float panScale = s_previewDistance * 0.002f;
                    s_previewPanOffset += right * (-io.MouseDelta.x * panScale)
                                       +  up    * ( io.MouseDelta.y * panScale);
                }
            }

            ImGui::TextDisabled("LMB drag: orbit  |  MMB drag: pan  |  Scroll: zoom");
            ImGui::Separator();
            ImGui::Spacing();
        }

        auto& desc = s_builderEntity.getComponent<DescriptorComponent>();
        ImGui::Text("Entity: %s", desc.name.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Properties");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Descriptor##builder"))
            draw_component_properties(ENTITY_COMPONENT::DESCRIPTOR, s_builderEntity);
        if (ImGui::CollapsingHeader("Transform##builder"))
            draw_component_properties(ENTITY_COMPONENT::TRANSFORM, s_builderEntity);

        for (int i = 0; i < k_builderComponentCount; i++)
        {
            const auto& entry = k_builderComponents[i];
            if (entry.locked) continue;
            if (!has_component(entry.comp, s_builderEntity)) continue;
            std::string header = std::string(entry.label) + "##builder";
            if (ImGui::CollapsingHeader(header.c_str()))
                draw_component_properties(entry.comp, s_builderEntity);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Export");
        ImGui::Separator();

        ImGui::PushID("BuilderExportPath");
        ImGui::InputText("Path", s_builderExportPath, sizeof(s_builderExportPath));
        ImGui::PopID();
        ImGui::SameLine();
        if (ImGui::Button("..."))
        {
            std::string chosen = Utility::SaveFileDialog(dialog_filter_entity, "content\\entity");
            if (!chosen.empty())
            {
                memset(s_builderExportPath, 0, sizeof(s_builderExportPath));
                strncpy(s_builderExportPath, chosen.c_str(), sizeof(s_builderExportPath) - 1);
            }
        }

        bool exportClicked = ImGui::Button("Export");
        ImGui::SetItemTooltip("Write the entity to the .ent file at the path above,\nso it can be spawned from the entity list.");
        if (exportClicked)
        {
            const std::string path(s_builderExportPath);
            if (!path.empty())
            {
                auto& mc = s_builderEntity.getComponent<MeshComponent>();
                if (mc.node)
                    mc.isVisible = true;

                desc.isSerializable = true;
                WorldManager::Get()->exportEntity(s_builderEntity, path, false);
                desc.isSerializable = false;

                if (mc.node)
                    mc.isVisible = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            s_builderResetAll();
        ImGui::SetItemTooltip("Discard the working entity and start over. Unexported changes are lost.");
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            s_builderKillEntity();
            m_windowData.draw_window_entity_builder = false;
        }
        ImGui::SetItemTooltip("Close the builder and discard the working entity. Unexported changes are lost.");
    }

    ImGui::End();

    if (!m_windowData.draw_window_entity_builder && s_builderEntityCreated)
        s_builderKillEntity();
}
