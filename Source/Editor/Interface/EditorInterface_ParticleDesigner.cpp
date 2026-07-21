#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include <irrlicht/source/Irrlicht/COpenGLTexture.h>

#include "Engine/Renderer/Particle/ParticleSystemDef.h"
#include "Engine/Renderer/Particle/ParticleSystemLoader.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Renderer/RenderManager.h"

#include "cereal/archives/xml.hpp"

#include "SPK.h"
#include "SPK_IRR.h"

#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

using namespace SPK;
using namespace SPK::IRR;

// ---------------------------------------------------------------------------
// Particle effect editing state
// ---------------------------------------------------------------------------

static ParticleSystemDef s_def;
static std::string       s_filePath;
static char              s_pathBuf[512] = {};
static bool              s_dirty        = false;

static int s_selectedGroup   = -1;
static int s_selectedEmitter = -1;

enum class PdPending { None, New, Open, Close };
static PdPending   s_pendingAction   = PdPending::None;
static std::string s_pendingOpenPath;
static bool        s_closePending    = false;

// ---------------------------------------------------------------------------
// Preview viewport state (mirrors EntityBuilder pattern)
// ---------------------------------------------------------------------------

static irr::scene::ISceneManager*    s_pdPreviewSM     = nullptr;
static irr::video::ITexture*         s_pdPreviewRTT    = nullptr;
static GLuint                        s_pdPreviewGLTex  = 0;
static irr::scene::ICameraSceneNode* s_pdPreviewCamera = nullptr;
static float s_pdAzimuth   = 45.0f;
static float s_pdElevation = 20.0f;
static float s_pdDistance  = 2.0f;

static System* s_pdSparkSystem  = nullptr;
static float   s_pdUpdateRate   = 1.0f;
static bool    s_pdLoop         = true;
static bool    s_pdStale        = false;  // true when s_def changed since last preview


static char s_groupNameBuf[128] = {};
static char s_texPathBuf[512]   = {};

static const char* k_flagNames[9] = {
    "RED", "GREEN", "BLUE", "ALPHA", "SIZE",
    "MASS", "ANGLE", "TEXTURE_INDEX", "ROTATION_SPEED"
};

// ---------------------------------------------------------------------------
// Preview infrastructure
// ---------------------------------------------------------------------------

static void s_pdEnsureInfra()
{
    if (s_pdPreviewSM) return;

    auto* mainSM = RenderManager::Get()->sceneManager();
    auto* driver = RenderManager::Get()->driver();

    s_pdPreviewSM = mainSM->createNewSceneManager(false);

    s_pdPreviewCamera = s_pdPreviewSM->addCameraSceneNode();
    s_pdPreviewCamera->setNearValue(0.1f);
    s_pdPreviewCamera->setFarValue(1000.0f);
    s_pdPreviewCamera->setAspectRatio(480.0f / 320.0f);

    s_pdPreviewSM->setAmbientLight(irr::video::SColorf(1.0f, 1.0f, 1.0f, 1.0f));

    s_pdPreviewRTT = driver->addRenderTargetTexture(
        irr::core::dimension2d<irr::u32>(480, 320), "pd_preview_rtt");

    s_pdPreviewGLTex =
        static_cast<irr::video::COpenGLTexture*>(s_pdPreviewRTT)->getOpenGLTextureName();
}

static void s_pdUpdateCamera()
{
    if (!s_pdPreviewCamera) return;
    const float az = s_pdAzimuth   * (irr::core::PI / 180.0f);
    const float el = s_pdElevation * (irr::core::PI / 180.0f);
    const float r  = s_pdDistance;
    s_pdPreviewCamera->setPosition(irr::core::vector3df(
        r * cosf(el) * sinf(az),
        r * sinf(el),
        r * cosf(el) * cosf(az)));
    s_pdPreviewCamera->setTarget(irr::core::vector3df(0.0f, 0.0f, 0.0f));
}

static void s_pdStopParticlePreview()
{
    if (s_pdSparkSystem)
    {
        RenderManager::Get()->clearPreviewParticle();
        SPK_Destroy(s_pdSparkSystem);
        s_pdSparkSystem = nullptr;
    }
}

static void s_pdSpawnParticlePreview()
{
    s_pdEnsureInfra();
    s_pdStopParticlePreview();

    s_pdSparkSystem = ParticleSystemLoader::buildForPreview(s_def, s_pdPreviewSM);
    if (!s_pdSparkSystem) return;

    IRRSystem* irrSys = static_cast<IRRSystem*>(s_pdSparkSystem);
    // buildForPreview triggers onRegister() which auto-adds the system to the main
    // scene's LDR effect node list. Remove it from there and redirect it to the
    // preview RTT pass instead so it renders in the designer viewport only.
    RenderManager::Get()->unregisterLDREffectNode(irrSys);
    RenderManager::Get()->setPreviewParticle(irrSys);

    irrSys->setVisible(true);
    irrSys->setPosition(irr::core::vector3df(0.0f, 0.0f, 0.0f));
    irrSys->updateAbsolutePosition();
#undef max
    s_pdUpdateRate = std::max(0.001f, s_def.updateRate);
    s_pdStale      = false;

    RenderManager::Get()->setPreviewScene(s_pdPreviewSM, s_pdPreviewRTT);
}

// ---------------------------------------------------------------------------
// Flag list helpers
// ---------------------------------------------------------------------------

static bool flagInList(const std::vector<std::string>& list, const char* name)
{
    for (const auto& s : list)
        if (s == name) return true;
    return false;
}

static void addFlagToList(std::vector<std::string>& list, const char* name)
{
    if (!flagInList(list, name))
        list.emplace_back(name);
}

static void removeFlagFromList(std::vector<std::string>& list, const char* name)
{
    list.erase(std::remove(list.begin(), list.end(), std::string(name)), list.end());
}

// ---------------------------------------------------------------------------
// 2-column property table helpers
// ---------------------------------------------------------------------------

static bool BeginPropTable(const char* id)
{
    bool ok = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit);
    if (ok)
    {
        ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed,   80.0f);
        ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
    }
    return ok;
}

static void EndPropTable() { ImGui::EndTable(); }

static void PropRow(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
}

static float ThirdWidth()
{
    return (ImGui::GetContentRegionAvail().x
            - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
}

// ---------------------------------------------------------------------------
// Unsaved-changes helpers
// ---------------------------------------------------------------------------

static void s_doNew()
{
    s_pdStopParticlePreview();
    RenderManager::Get()->clearPreviewScene();
    s_def           = ParticleSystemDef{};
    s_filePath      = "";
    s_pathBuf[0]    = '\0';
    s_selectedGroup = s_selectedEmitter = -1;
    s_dirty         = false;
}

static void s_doOpen(const std::string& rel)
{
    std::ifstream is(rel);
    if (!is.good()) return;
    try
    {
        cereal::XMLInputArchive ar(is);
        ar(cereal::make_nvp("ParticleSystem", s_def));
        s_filePath = rel;
        strncpy_s(s_pathBuf, rel.c_str(), sizeof(s_pathBuf) - 1);
        s_selectedGroup   = s_def.groups.empty() ? -1 : 0;
        s_selectedEmitter = -1;
        s_dirty           = true;
    }
    catch (...) {}
}

static void s_executePending()
{
    switch (s_pendingAction)
    {
    case PdPending::New:   s_doNew();                   break;
    case PdPending::Open:  s_doOpen(s_pendingOpenPath); break;
    case PdPending::Close: s_closePending = true;       break;
    default: break;
    }
    s_pendingAction = PdPending::None;
}

static bool s_trySave()
{
    if (!s_filePath.empty())
    {
        ParticleSystemLoader::save(s_def, s_filePath);
        s_dirty = false;
        return true;
    }
    std::string abs = Utility::SaveFileDialog(
        "Particle System\0*.psys\0Any File\0*.*\0", "content\\particle");
    if (abs.empty()) return false;
    s_filePath = Utility::RemoveAbsDir(abs);
    strncpy_s(s_pathBuf, s_filePath.c_str(), sizeof(s_pathBuf) - 1);
    ParticleSystemLoader::save(s_def, s_filePath);
    s_dirty = false;
    return true;
}

static void s_requestAction(PdPending action)
{
    s_pendingAction = action;
    if (s_dirty)
        ImGui::OpenPopup("Unsaved Changes##pd");
    else
        s_executePending();
}

static void drawUnsavedChangesModal()
{
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved Changes##pd", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("You have unsaved changes.");
    ImGui::Text("Save before continuing?");
    ImGui::Spacing();

    if (ImGui::Button("Save", ImVec2(80, 0)))
    {
        if (s_trySave())
        {
            s_executePending();
            ImGui::CloseCurrentPopup();
        }
        // If SaveAs dialog was cancelled, stay in the modal
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(80, 0)))
    {
        // Let execute functions own dirty state — do NOT clear s_dirty here
        s_executePending();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0)))
    {
        s_pendingAction = PdPending::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// File management
// ---------------------------------------------------------------------------

static void drawFileManagement()
{
    // Row 1: path + Open + New
    const float btnW1 = ImGui::CalcTextSize("Open...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float btnW2 = ImGui::CalcTextSize("New").x     + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW1 - btnW2
                            - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::InputText("##pd_path", s_pathBuf, sizeof(s_pathBuf), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();

    if (ImGui::Button("Open..."))
    {
        std::string abs = Utility::OpenFileDialog(
            "Particle System\0*.psys\0Any File\0*.*\0", "content\\particle");
        if (!abs.empty())
        {
            s_pendingOpenPath = Utility::RemoveAbsDir(abs);
            s_requestAction(PdPending::Open);
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("New"))
        s_requestAction(PdPending::New);

    // Row 2: Save + Save As...
    if (ImGui::Button("Save"))
        s_trySave();
    ImGui::SameLine();

    if (ImGui::Button("Save As..."))
    {
        std::string abs = Utility::SaveFileDialog(
            "Particle System\0*.psys\0Any File\0*.*\0", "content\\particle");
        if (!abs.empty())
        {
            s_filePath = Utility::RemoveAbsDir(abs);
            strncpy_s(s_pathBuf, s_filePath.c_str(), sizeof(s_pathBuf) - 1);
            ParticleSystemLoader::save(s_def, s_filePath);
            s_dirty = false;
        }
    }

    ImGui::Spacing();

    if (BeginPropTable("##pd_sys"))
    {
        PropRow("Update Rate");
        if (ImGui::DragFloat("##pd_updaterate", &s_def.updateRate,
                             0.025f, 0.1f, 10.0f, "%.2f"))
        {
            s_dirty   = true;
            s_pdStale = true;
        }
        ImGui::SetItemTooltip("Simulation speed multiplier for the whole effect (1 = normal).");
        EndPropTable();
    }
}

// ---------------------------------------------------------------------------
// Group list
// ---------------------------------------------------------------------------

static void drawGroupList()
{
    ImGui::Text("Groups");
    ImGui::SameLine();

    if (ImGui::SmallButton("+##grpadd"))
    {
        ParticleGroupDef g;
        g.name = "group_" + std::to_string(s_def.groups.size());
        s_def.groups.push_back(std::move(g));
        s_selectedGroup   = static_cast<int>(s_def.groups.size()) - 1;
        s_selectedEmitter = -1;
    }

    if (s_selectedGroup >= 0 && s_selectedGroup < static_cast<int>(s_def.groups.size()))
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("-##grprem"))
        {
            s_def.groups.erase(s_def.groups.begin() + s_selectedGroup);
            s_selectedGroup = std::min(s_selectedGroup,
                                       static_cast<int>(s_def.groups.size()) - 1);
            s_selectedEmitter = -1;
        }
    }

    ImGui::BeginChild("##pd_grouplist", ImVec2(0, 110), true);
    for (int i = 0; i < static_cast<int>(s_def.groups.size()); ++i)
    {
        ImGui::PushID(i);
        if (ImGui::Selectable(s_def.groups[i].name.c_str(), i == s_selectedGroup))
        {
            s_selectedGroup   = i;
            s_selectedEmitter = -1;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Group settings
// ---------------------------------------------------------------------------

static void drawGroupSettings(ParticleGroupDef& grp)
{
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Group Settings")) return;

    if (!BeginPropTable("##pd_grp")) return;

    strncpy_s(s_groupNameBuf, grp.name.c_str(), sizeof(s_groupNameBuf) - 1);
    PropRow("Name");
    if (ImGui::InputText("##pd_grpname", s_groupNameBuf, sizeof(s_groupNameBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue))
        grp.name = s_groupNameBuf;

    PropRow("Capacity");
    if (ImGui::InputInt("##pd_cap", &grp.capacity) && grp.capacity < 1)
        grp.capacity = 1;
    ImGui::SetItemTooltip("Maximum particles alive in this group at once -\nemission stalls until older particles die.");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Gravity");
    ImGui::TableSetColumnIndex(1);
    {
        float fw = ThirdWidth();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##gx", &grp.gravityX, 0.01f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##gy", &grp.gravityY, 0.01f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##gz", &grp.gravityZ, 0.01f);
    }

    PropRow("Friction");
    ImGui::DragFloat("##pd_fric", &grp.friction, 0.001f, 0.0f, 100.0f, "%.3f");
    ImGui::SetItemTooltip("Air drag - how quickly particles lose velocity. 0 = none.");

    PropRow("Rotator");
    ImGui::Checkbox("##pd_rot", &grp.useRotator);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drives ROTATION_SPEED -> ANGLE each frame.\nRequires ANGLE (random) + ROTATION_SPEED enabled in the model.");

    EndPropTable();
}

// ---------------------------------------------------------------------------
// Emitters
// ---------------------------------------------------------------------------

static void drawEmitters(ParticleGroupDef& grp)
{
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Emitters")) return;

    ImGui::SameLine();
    if (ImGui::SmallButton("+##emitadd"))
    {
        grp.emitters.push_back(ParticleEmitterDef{});
        s_selectedEmitter = static_cast<int>(grp.emitters.size()) - 1;
    }
    if (s_selectedEmitter >= 0 && s_selectedEmitter < static_cast<int>(grp.emitters.size()))
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("-##emitrem"))
        {
            grp.emitters.erase(grp.emitters.begin() + s_selectedEmitter);
            s_selectedEmitter = std::min(s_selectedEmitter,
                                         static_cast<int>(grp.emitters.size()) - 1);
        }
    }

    ImGui::BeginChild("##pd_emitlist", ImVec2(0, 60), true);
    for (int i = 0; i < static_cast<int>(grp.emitters.size()); ++i)
    {
        ImGui::PushID(i);
        char label[48];
        snprintf(label, sizeof(label), "Emitter %d (%s)", i, grp.emitters[i].type.c_str());
        if (ImGui::Selectable(label, i == s_selectedEmitter))
            s_selectedEmitter = i;
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (s_selectedEmitter < 0 ||
        s_selectedEmitter >= static_cast<int>(grp.emitters.size()))
        return;

    ParticleEmitterDef& em = grp.emitters[s_selectedEmitter];

    // Indent + left accent bar to show these properties belong to the selected emitter
    ImGui::Spacing();
    float emLineX   = ImGui::GetCursorScreenPos().x + 3.0f;
    float emLineTop = ImGui::GetCursorScreenPos().y;
    ImGui::Indent(14.0f);

    if (!BeginPropTable("##pd_emit")) { ImGui::Unindent(14.0f); return; }

    static const char* k_emitTypes[] = { "random", "normal", "static", "straight", "spheric" };
    int emIdx = 0;
    for (int i = 0; i < 5; ++i) if (em.type == k_emitTypes[i]) { emIdx = i; break; }
    PropRow("Type");
    if (ImGui::Combo("##pd_etype", &emIdx, k_emitTypes, 5))
        em.type = k_emitTypes[emIdx];
    ImGui::SetItemTooltip("Initial velocity pattern:\nrandom - any direction\nnormal - along the zone's surface normal\nstatic - no initial velocity\nstraight - fixed Direction\nspheric - cone around Direction (Angle Min/Max)");

    static const char* k_zoneTypes[] = { "sphere", "point", "aabox" };
    int zIdx = 0;
    for (int i = 0; i < 3; ++i) if (em.zone.type == k_zoneTypes[i]) { zIdx = i; break; }
    PropRow("Zone");
    if (ImGui::Combo("##pd_ztype", &zIdx, k_zoneTypes, 3))
        em.zone.type = k_zoneTypes[zIdx];
    ImGui::SetItemTooltip("Shape particles spawn from: a sphere, a single point,\nor an axis-aligned box.");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Center");
    ImGui::TableSetColumnIndex(1);
    {
        float fw = ThirdWidth();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##zcx", &em.zone.cx, 0.01f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##zcy", &em.zone.cy, 0.01f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##zcz", &em.zone.cz, 0.01f);
    }

    if (em.zone.type == "sphere")
    {
        PropRow("Radius");
        ImGui::DragFloat("##pd_zrad", &em.zone.radius, 0.01f, 0.0f, 1000.0f);
    }

    if (em.zone.type == "aabox")
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Half-Ext");
        ImGui::TableSetColumnIndex(1);
        float fw = ThirdWidth();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##zhx", &em.zone.hx, 0.01f, 0.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##zhy", &em.zone.hy, 0.01f, 0.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##zhz", &em.zone.hz, 0.01f, 0.0f);
    }

    PropRow("Full Zone");
    ImGui::Checkbox("##pd_fullz", &em.fullZone);
    ImGui::SetItemTooltip("Spawn anywhere inside the zone's volume;\nunchecked spawns only on its surface.");

    PropRow("Tank");
    if (ImGui::InputInt("##pd_tank", &em.tank) && em.tank < 0) em.tank = 0;
    ImGui::SetItemTooltip("Total particle budget for this emitter - it stops when the tank is spent.\nEmitted all at once when Flow = -1.");

    PropRow("Flow");
    ImGui::DragFloat("##pd_flow", &em.flow, 0.1f, -1.0f, 100000.0f, "%.1f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("flow = -1 means one-shot burst using Tank count");

    PropRow("Force Min");
    ImGui::DragFloat("##pd_fmin", &em.forceMin, 0.01f, 0.0f, 1000.0f);
    ImGui::SetItemTooltip("Each particle launches with a random initial speed\nbetween Force Min and Force Max.");
    PropRow("Force Max");
    ImGui::DragFloat("##pd_fmax", &em.forceMax, 0.01f, 0.0f, 1000.0f);

    if (em.type == "straight" || em.type == "spheric")
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Direction");
        ImGui::TableSetColumnIndex(1);
        {
            float fw = ThirdWidth();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##dix", &em.dirX, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##diy", &em.dirY, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##diz", &em.dirZ, 0.01f);
        }
    }
    if (em.type == "spheric")
    {
        PropRow("Angle Min");
        ImGui::DragFloat("##pd_amin", &em.angleMin, 0.001f, 0.0f, 6.2831853f, "%.4f rad");
        PropRow("Angle Max");
        ImGui::DragFloat("##pd_amax", &em.angleMax, 0.001f, 0.0f, 6.2831853f, "%.4f rad");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0,0 = straight beam  0,PI/2 = 90deg cone  0,PI = hemisphere  0,2PI = full sphere");
    }

    EndPropTable();

    ImGui::Unindent(14.0f);
    float emLineBot = ImGui::GetCursorScreenPos().y;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(emLineX, emLineTop), ImVec2(emLineX, emLineBot),
        ImGui::GetColorU32(ImGuiCol_SeparatorActive, 0.4f), 2.0f);
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

static int requiredValueCount(const ParticleModelDef& mdl, const char* fn)
{
    bool isMut  = flagInList(mdl.mutableFlags, fn);
    bool isRand = flagInList(mdl.randomFlags,  fn);
    if (isMut && isRand) return 4;
    if (isMut || isRand) return 2;
    return 1;
}

static void resizeParamValues(ParticleParamDef& p, int count)
{
    while (static_cast<int>(p.values.size()) < count)  p.values.push_back(0.0f);
    while (static_cast<int>(p.values.size()) > count)  p.values.pop_back();
}

static void drawModel(ParticleModelDef& mdl)
{
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Model")) return;

    if (BeginPropTable("##pd_lt"))
    {
        PropRow("Life Min");
        ImGui::DragFloat("##pd_ltmin", &mdl.lifetimeMin, 0.01f, 0.0f, 600.0f);
        PropRow("Life Max");
        ImGui::DragFloat("##pd_ltmax", &mdl.lifetimeMax, 0.01f, 0.0f, 600.0f);
        EndPropTable();
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("##pd_flags", 5,
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Flag",    ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed,  56.0f);
        ImGui::TableSetupColumn("Mutable", ImGuiTableColumnFlags_WidthFixed,  56.0f);
        ImGui::TableSetupColumn("Random",  ImGuiTableColumnFlags_WidthFixed,  56.0f);
        ImGui::TableSetupColumn("Interp.", ImGuiTableColumnFlags_WidthFixed,  52.0f);
        ImGui::TableHeadersRow();

        for (int fi = 0; fi < 9; ++fi)
        {
            const char* fn = k_flagNames[fi];
            bool ena    = flagInList(mdl.enabledFlags,      fn);
            bool mut_   = flagInList(mdl.mutableFlags,      fn);
            bool rnd    = flagInList(mdl.randomFlags,       fn);
            bool interp = flagInList(mdl.interpolatedFlags, fn);

            ImGui::PushID(fi);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(fn);

            // Helper: center a checkbox in the current table cell
            auto CenteredCheckbox = [](const char* id, bool* v) -> bool {
                float cbW  = ImGui::GetFrameHeight();
                float avail = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - cbW) * 0.5f);
                return ImGui::Checkbox(id, v);
            };

            ImGui::TableSetColumnIndex(1);
            if (CenteredCheckbox("##ena", &ena))
            {
                if (ena) addFlagToList(mdl.enabledFlags, fn);
                else
                {
                    removeFlagFromList(mdl.enabledFlags,      fn);
                    removeFlagFromList(mdl.mutableFlags,      fn);
                    removeFlagFromList(mdl.randomFlags,       fn);
                    removeFlagFromList(mdl.interpolatedFlags, fn);
                }
            }

            if (!ena) ImGui::BeginDisabled();

            ImGui::TableSetColumnIndex(2);
            if (CenteredCheckbox("##mut", &mut_))
            { if (mut_) addFlagToList(mdl.mutableFlags, fn); else removeFlagFromList(mdl.mutableFlags, fn); }

            ImGui::TableSetColumnIndex(3);
            if (CenteredCheckbox("##rnd", &rnd))
            { if (rnd) addFlagToList(mdl.randomFlags, fn); else removeFlagFromList(mdl.randomFlags, fn); }

            ImGui::TableSetColumnIndex(4);
            if (CenteredCheckbox("##interp", &interp))
            { if (interp) addFlagToList(mdl.interpolatedFlags, fn); else removeFlagFromList(mdl.interpolatedFlags, fn); }

            if (!ena) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Param Values"))
    {
        for (int fi = 0; fi < 9; ++fi)
        {
            const char* fn = k_flagNames[fi];
            if (!flagInList(mdl.enabledFlags, fn)) continue;

            ParticleParamDef* param = nullptr;
            for (auto& p : mdl.params) if (p.name == fn) { param = &p; break; }
            if (!param) { mdl.params.push_back({ fn, { 0.0f } }); param = &mdl.params.back(); }

            int needed = requiredValueCount(mdl, fn);
            resizeParamValues(*param, needed);

            ImGui::PushID(fi);
            ImGui::Text("  %s", fn);
            ImGui::SameLine(120.0f);
#undef max
            float fw = std::max(40.0f,
                (ImGui::GetContentRegionAvail().x
                 - ImGui::GetStyle().ItemSpacing.x * (needed - 1)) / (float)needed);

            static const char* lbl1[1]  = { "const" };
            static const char* lbl2m[2] = { "start", "end" };
            static const char* lbl2r[2] = { "min",   "max" };
            static const char* lbl4[4]  = { "bMin",  "bMax", "eMin", "eMax" };

            const char** lbls = (needed == 4) ? lbl4
                              : (needed == 2 && flagInList(mdl.mutableFlags, fn)) ? lbl2m
                              : (needed == 2) ? lbl2r
                              : lbl1;

            for (int vi = 0; vi < needed; ++vi)
            {
                if (vi > 0) ImGui::SameLine();
                char vid[16]; snprintf(vid, sizeof(vid), "##pv%d_%d", fi, vi);
                ImGui::SetNextItemWidth(fw);
                ImGui::DragFloat(vid, &param->values[vi], 0.01f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lbls[vi]);
            }
            ImGui::PopID();
        }

        mdl.params.erase(
            std::remove_if(mdl.params.begin(), mdl.params.end(),
                [&](const ParticleParamDef& p)
                { return !flagInList(mdl.enabledFlags, p.name.c_str()); }),
            mdl.params.end());
    }

    if (ImGui::CollapsingHeader("Interpolators"))
    {
        for (int fi = 0; fi < 9; ++fi)
        {
            const char* fn = k_flagNames[fi];
            if (!flagInList(mdl.interpolatedFlags, fn)) continue;

            ParticleInterpolatorDef* interp = nullptr;
            for (auto& ip : mdl.interpolators) if (ip.param == fn) { interp = &ip; break; }
            if (!interp) { mdl.interpolators.push_back({ fn, {} }); interp = &mdl.interpolators.back(); }

            ImGui::PushID(fi);
            ImGui::Text("  %s", fn);
            ImGui::SameLine();
            if (ImGui::SmallButton("+##ikf")) interp->entries.push_back({ 0.0f, 0.0f, 0.0f });
            if (!interp->entries.empty()) { ImGui::SameLine(); if (ImGui::SmallButton("-##ikf")) interp->entries.pop_back(); }

            if (ImGui::BeginTable("##itbl", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed, 22.0f);
                ImGui::TableSetupColumn("lifeRatio", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("min",       ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("max",       ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (int ei = 0; ei < static_cast<int>(interp->entries.size()); ++ei)
                {
                    auto& e = interp->entries[ei];
                    ImGui::TableNextRow();
                    ImGui::PushID(ei);
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", ei);
                    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##lr", &e.lifeRatio, 0.01f, 0.0f, 1.0f, "%.2f");
                    ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##mn", &e.min, 0.01f, -1000.f, 1000.f, "%.3f");
                    ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##mx", &e.max, 0.01f, -1000.f, 1000.f, "%.3f");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }

        mdl.interpolators.erase(
            std::remove_if(mdl.interpolators.begin(), mdl.interpolators.end(),
                [&](const ParticleInterpolatorDef& ip)
                { return !flagInList(mdl.interpolatedFlags, ip.param.c_str()); }),
            mdl.interpolators.end());
    }
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

static void drawRenderer(ParticleRendererDef& rnd)
{
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Renderer")) return;

    if (!BeginPropTable("##pd_rnd")) return;

    strncpy_s(s_texPathBuf, rnd.texturePath.c_str(), sizeof(s_texPathBuf) - 1);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Texture");
    ImGui::TableSetColumnIndex(1);
    {
        float btnW = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputText("##pd_tex", s_texPathBuf, sizeof(s_texPathBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            rnd.texturePath = s_texPathBuf;
        ImGui::SameLine();
        if (ImGui::Button("...##pd_texbr"))
            EditorInterface::show_window_texture_browser("particle_renderer_texture");
        if (g_textureBrowserRequestID == "particle_renderer_texture" && g_currentSelectedTexture != "null")
        {
            rnd.texturePath = g_currentSelectedTexture;
            strncpy_s(s_texPathBuf, g_currentSelectedTexture.c_str(), sizeof(s_texPathBuf) - 1);
            g_currentSelectedTexture = "null";
            g_textureBrowserRequestID.clear();
        }
    }

    static const char* k_blend[] = { "alpha", "add", "none" };
    int blendIdx = 0;
    for (int i = 0; i < 3; ++i) if (rnd.blending == k_blend[i]) { blendIdx = i; break; }
    PropRow("Blending");
    if (ImGui::Combo("##pd_blend", &blendIdx, k_blend, 3))
        rnd.blending = k_blend[blendIdx];
    ImGui::SetItemTooltip("alpha - normal transparency (smoke, dust)\nadd - brightens what's behind (fire, sparks, glows)\nnone - opaque quads");

    static const char* k_orient[] = { "camera_plane", "direction_aligned", "fixed" };
    int orientIdx = 0;
    for (int i = 0; i < 3; ++i) if (rnd.orientation == k_orient[i]) { orientIdx = i; break; }
    PropRow("Orientation");
    if (ImGui::Combo("##pd_orient", &orientIdx, k_orient, 3))
        rnd.orientation = k_orient[orientIdx];
    ImGui::SetItemTooltip("camera_plane - billboard facing the camera\ndirection_aligned - stretched along the particle's velocity (streaks, rain)\nfixed - constant Look/Up orientation");

    if (rnd.orientation == "fixed")
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Look");
        ImGui::TableSetColumnIndex(1);
        {
            float fw = ThirdWidth();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##lx", &rnd.lookX, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##ly", &rnd.lookY, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##lz", &rnd.lookZ, 0.01f);
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Up");
        ImGui::TableSetColumnIndex(1);
        {
            float fw = ThirdWidth();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##ux", &rnd.upX, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##uy", &rnd.upY, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fw); ImGui::DragFloat("##uz", &rnd.upZ, 0.01f);
        }
    }

    PropRow("Atlas W");
    if (ImGui::InputInt("##pd_aw", &rnd.atlasW) && rnd.atlasW < 1) rnd.atlasW = 1;
    ImGui::SetItemTooltip("Columns x rows of sub-images in the texture atlas (1x1 = whole texture).\nUsed with the TEXTURE_INDEX parameter to pick/animate frames.");
    PropRow("Atlas H");
    if (ImGui::InputInt("##pd_ah", &rnd.atlasH) && rnd.atlasH < 1) rnd.atlasH = 1;

    PropRow("Scale X");
    ImGui::DragFloat("##pd_sx", &rnd.scaleX, 0.001f, 0.0001f, 100.0f, "%.4f");
    PropRow("Scale Y");
    ImGui::DragFloat("##pd_sy", &rnd.scaleY, 0.001f, 0.0001f, 100.0f, "%.4f");

    PropRow("Depth Write");
    ImGui::Checkbox("##pd_dw", &rnd.depthWrite);
    ImGui::SetItemTooltip("Write particles to the depth buffer. Usually OFF for blended particles -\nON causes hard edges where they overlap.");

    PropRow("Alpha Test");
    ImGui::Checkbox("##pd_at", &rnd.alphaTest);
    ImGui::SetItemTooltip("Discard pixels below the alpha threshold - hard cutout edges\ninstead of soft blending (leaves, debris).");
    if (rnd.alphaTest)
    {
        PropRow("Alpha Thresh");
        ImGui::DragFloat("##pd_ath", &rnd.alphaTestThreshold, 0.001f, 0.0f, 1.0f, "%.3f");
        ImGui::SetItemTooltip("Alpha value below which pixels are discarded.");
    }

    EndPropTable();
}

// ---------------------------------------------------------------------------
// Preview viewport
// ---------------------------------------------------------------------------

static void drawPreview()
{
    // Viewport — always visible. Ensure infra exists and re-bind the RTT each frame
    // so the preview renders even before the user clicks Preview.
    s_pdEnsureInfra();
    RenderManager::Get()->setPreviewScene(s_pdPreviewSM, s_pdPreviewRTT);
    s_pdUpdateCamera();

    constexpr float vpW = 640.0f, vpH = 480.0f;
    ImVec2 vpPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##pd_viewport", ImVec2(vpW, vpH));
    ImGui::GetWindowDrawList()->AddImage(
        ImTextureRef((ImTextureID)(uintptr_t)s_pdPreviewGLTex),
        vpPos, ImVec2(vpPos.x + vpW, vpPos.y + vpH),
        ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

    if (ImGui::IsItemActive())
    {
        ImGuiIO& io = ImGui::GetIO();
        s_pdAzimuth   -= io.MouseDelta.x * 0.5f;
        s_pdElevation += io.MouseDelta.y * 0.5f;
        s_pdElevation  = irr::core::clamp(s_pdElevation, -89.0f, 89.0f);
    }

    ImGui::TextDisabled("LMB drag: orbit");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    float prevDist = s_pdDistance;
    ImGui::InputFloat("Zoom", &s_pdDistance, 0.5f, 2.0f, "%.1f");
    s_pdDistance = prevDist - (s_pdDistance - prevDist);  // invert: + zooms in, - zooms out
    s_pdDistance = irr::core::clamp(s_pdDistance, 0.1f, 100.0f);
    ImGui::Spacing();

    // Controls
    if (s_pdStale)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 1.0f));
    if (ImGui::Button(s_pdStale ? "Rebuild Preview *" : "Rebuild Preview"))
        s_pdSpawnParticlePreview();
    if (s_pdStale)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Builds from current in-memory settings.\nUnsaved changes are included.\n* = settings changed since last preview.");
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        s_pdStopParticlePreview();
        RenderManager::Get()->clearPreviewScene();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &s_pdLoop);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When enabled, the preview restarts automatically\nand picks up any setting changes each cycle.");
    ImGui::SameLine();

    if (s_pdSparkSystem)
    {
        if (s_pdLoop)
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Playing (auto-reload)");
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Playing");
    }
    else
        ImGui::TextDisabled("Stopped");
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void EditorInterface::draw_window_particle_designer()
{
    if (!m_windowData.draw_window_particle_designer)
    {
        if (s_pdSparkSystem)
            s_pdStopParticlePreview();
        RenderManager::Get()->clearPreviewScene();
        return;
    }

    // Tick simulation every frame (before any ImGui calls so the RTT is fresh)
    if (s_pdSparkSystem)
    {
        float dt = ImGui::GetIO().DeltaTime;
        bool alive = s_pdSparkSystem->update(dt * s_pdUpdateRate);
        if (!alive)
        {
            if (s_pdLoop)
                s_pdSpawnParticlePreview();
            else
            {
                s_pdStopParticlePreview();
                RenderManager::Get()->clearPreviewScene();
            }
        }
    }

    // Handle confirmed close from modal (deferred one frame so modal can clean up)
    if (s_closePending)
    {
        s_closePending = false;
        s_doNew();
        m_windowData.draw_window_particle_designer = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 900.0f), ImGuiCond_FirstUseEver);
    bool windowOpen = true;
    if (!ImGui::Begin("Particle Designer", &windowOpen))
    {
        ImGui::End();
        if (!windowOpen) s_requestAction(PdPending::Close);
        return;
    }

    if (!windowOpen)
        s_requestAction(PdPending::Close);

    drawFileManagement();
    ImGui::Separator();
    drawPreview();
    ImGui::Separator();
    drawGroupList();

    if (s_selectedGroup >= 0 && s_selectedGroup < static_cast<int>(s_def.groups.size()))
    {
        ParticleGroupDef& grp = s_def.groups[s_selectedGroup];

        // Indent + left accent bar to show these sections are children of the selected group
        ImGui::Spacing();
        float lineX   = ImGui::GetCursorScreenPos().x + 3.0f;
        float lineTop = ImGui::GetCursorScreenPos().y;
        ImGui::Indent(14.0f);

        drawGroupSettings(grp);
        ImGui::Spacing();
        drawEmitters(grp);
        ImGui::Spacing();
        drawModel(grp.model);
        ImGui::Spacing();
        drawRenderer(grp.renderer);
        ImGui::Spacing();

        if (ImGui::IsAnyItemActive())
            s_pdStale = true;

        ImGui::Unindent(14.0f);
        float lineBot = ImGui::GetCursorScreenPos().y;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(lineX, lineTop), ImVec2(lineX, lineBot),
            ImGui::GetColorU32(ImGuiCol_SeparatorActive, 0.6f), 2.0f);
    }
    else
    {
        ImGui::Spacing();
        ImGui::TextDisabled("No group selected. Add or select a group above.");
    }

    drawUnsavedChangesModal();

    ImGui::End();
}
