#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"

#include <IMGUI/imgui.h>

#include "Engine/Engine.h"
#include "Engine/Brush/BrushGeometry.h"
#include "Engine/Brush/BrushManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Editor/BrushTool.h"
#include "Utility/Utility.h"

#include <string>

namespace
{
    // Mode button that highlights when active and enforces painter exclusivity.
    void modeButton(const char* label, BrushToolMode mode, BrushTool& tool)
    {
        const bool active = (tool.mode() == mode);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.85f, 0.3f, 1.0f));
        }
        if (ImGui::Button(label))
        {
            tool.setMode(active ? BrushToolMode::OFF : mode);
            if (tool.isActive())
            {
                // Modal brush tools take the viewport — painters go dormant
                g_sceneInteractor.getPainter().m_active = false;
                g_sceneInteractor.getTexturePainter().active = false;
            }
        }
        if (active)
            ImGui::PopStyleColor(2);
    }

    // Tool-brush type presets: one click sets contentFlags AND applies the
    // matching label texture to every face.  The texture is only visual —
    // the flags are the behavior (the two stay independent otherwise).
    struct ToolPreset
    {
        const char* label;
        irr::u32    flags;
        const char* texture;
    };
    const ToolPreset kToolPresets[] = {
        { "Player Clip",  CONTENT_CLIP_PLAYER,  "content/texture/tool/playerclip.png"  },
        { "Monster Clip", CONTENT_CLIP_MONSTER, "content/texture/tool/monsterclip.png" },
        { "Weapon Clip",  CONTENT_CLIP_WEAPON,  "content/texture/tool/weaponclip.png"  },
        { "Trigger",      CONTENT_TRIGGER,      "content/texture/tool/trigger.png"     },
        { "Sky",          CONTENT_SKY,          "content/texture/tool/sky.png"         },
    };

    // One undo entry snapshotting every selected brush (whole-brush snapshots,
    // same pattern as BrushTool's edit paths).
    void pushSelectedBrushUndo(const std::vector<uint32_t>& sel)
    {
        UndoEntry entry;
        for (uint32_t id : sel)
        {
            if (Brush* b = BrushManager::Get()->getBrush(id))
            {
                BrushSnapshot snap;
                snap.id = id;
                snap.existed = true;
                snap.data = *b;
                entry.brushes.push_back(std::move(snap));
            }
        }
        if (!entry.empty())
            g_sceneInteractor.pushUndoEntry(std::move(entry));
    }

    void applyToolPreset(const std::vector<uint32_t>& sel, irr::u32 flags, const char* texture)
    {
        pushSelectedBrushUndo(sel);
        for (uint32_t id : sel)
        {
            Brush* b = BrushManager::Get()->getBrush(id);
            if (!b)
                continue;
            b->contentFlags = flags;
            if (texture)
            {
                for (auto& face : b->faces)
                    face.materialName = texture;
            }
            BrushManager::Get()->markBrushDirty(id);
        }
    }

    void setContentFlag(const std::vector<uint32_t>& sel, irr::u32 flag, bool on)
    {
        pushSelectedBrushUndo(sel);
        for (uint32_t id : sel)
        {
            Brush* b = BrushManager::Get()->getBrush(id);
            if (!b)
                continue;
            b->contentFlags = on ? (b->contentFlags | flag) : (b->contentFlags & ~flag);
            // TRIGGER_ONCE never changes what the compile emits — skip the
            // recompile (and the chunk lightmap invalidation it brings)
            if (flag != CONTENT_TRIGGER_ONCE)
                BrushManager::Get()->markBrushDirty(id);
        }
    }
}

void EditorInterface::draw_window_brush_editor()
{
    if (!m_windowData.draw_window_brush_editor)
        return;
    if (!BrushManager::Get())
        return;

    BrushTool& tool = g_sceneInteractor.getBrushTool();

    ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(280, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Brush Editor", &m_windowData.draw_window_brush_editor))
    {
        ImGui::End();
        return;
    }

    // ---- Tool modes ----
    ImGui::Text("Tool");
    modeButton("Create", BrushToolMode::CREATE, tool); ImGui::SameLine();
    modeButton("Face",   BrushToolMode::FACE,   tool); ImGui::SameLine();
    modeButton("Vertex", BrushToolMode::VERTEX, tool); ImGui::SameLine();
    modeButton("Clip",   BrushToolMode::CLIP,   tool); ImGui::SameLine();
    modeButton("Texture", BrushToolMode::PAINT, tool);

    switch (tool.mode())
    {
    case BrushToolMode::OFF:
        ImGui::TextDisabled("Select: Shift+click a brush; gizmo moves it");
        break;
    case BrushToolMode::CREATE:
        ImGui::TextDisabled("Drag a footprint; release extrudes upward");
        break;
    case BrushToolMode::FACE:
        ImGui::TextDisabled("Click a face; gizmo pushes/pulls along its normal");
        break;
    case BrushToolMode::VERTEX:
        ImGui::TextDisabled("Drag corner handles; invalid shapes revert");
        break;
    case BrushToolMode::CLIP:
        ImGui::TextDisabled("Click 2-3 points; Tab flips side; Enter commits");
        break;
    case BrushToolMode::PAINT:
        ImGui::TextDisabled("Click/drag faces to apply the Material below; Ctrl+click samples");
        break;
    }

    ImGui::Separator();

    // ---- Create settings ----
    ImGui::Text("Primitive");
    static const char* primNames[] = { "Box", "Wedge", "Cylinder" };
    int prim = static_cast<int>(tool.primitive);
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("##brush_prim", &prim, primNames, 3))
        tool.primitive = static_cast<BrushPrimitive>(prim);

    if (tool.primitive == BrushPrimitive::CYLINDER)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Sides", &tool.cylinderSides);
        if (tool.cylinderSides < 3)  tool.cylinderSides = 3;
        if (tool.cylinderSides > 32) tool.cylinderSides = 32;
    }

    ImGui::Text("Height:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##brush_height", &tool.defaultHeight, 0.0f, 0.0f, "%.2f");
    if (tool.defaultHeight < 0.1f) tool.defaultHeight = 0.1f;

    // ---- Material ----
    ImGui::Separator();
    ImGui::Text("Material");
    if (tool.defaultMaterial.empty())
        ImGui::TextDisabled("(none)");
    else
        ImGui::TextWrapped("%s", tool.defaultMaterial.c_str());

    if (ImGui::Button("Browse...##brush_mat"))
        show_window_texture_browser("brush_default_material");
    if (g_textureBrowserRequestID == "brush_default_material" && g_currentSelectedTexture != "null")
    {
        tool.defaultMaterial = g_currentSelectedTexture;
        g_currentSelectedTexture = "null";
        g_textureBrowserRequestID.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear##brush_mat"))
        tool.defaultMaterial.clear();

    // ---- Scene settings ----
    ImGui::Separator();
    ImGui::Text("Chunk cell size:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    static float s_cellSize = 0.0f;
    if (s_cellSize <= 0.0f)
        s_cellSize = BrushManager::Get()->cellSize();
    ImGui::InputFloat("##brush_cell", &s_cellSize, 0.0f, 0.0f, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Apply##brush_cell"))
    {
        if (s_cellSize < 1.0f) s_cellSize = 1.0f;
        BrushManager::Get()->setCellSize(s_cellSize);
    }

    ImGui::Text("Brushes: %d", static_cast<int>(BrushManager::Get()->getAllBrushes().size()));
    const auto& sel = g_sceneInteractor.getSelectedBrushes();
    if (!sel.empty())
    {
        ImGui::SameLine();
        ImGui::Text("| selected: %d", static_cast<int>(sel.size()));
    }

    bool showToolOverlay = BrushManager::Get()->isToolOverlayVisible();
    if (ImGui::Checkbox("Show tool brushes", &showToolOverlay))
        BrushManager::Get()->setToolOverlayVisible(showToolOverlay);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Semi-transparent overlay for clip/trigger/sky brushes (editor only)");

    // ---- Clip controls ----
    if (tool.mode() == BrushToolMode::CLIP)
    {
        ImGui::Separator();
        static const char* keepNames[] = { "Keep front", "Keep back", "Keep both" };
        ImGui::Text("Points: %d/3 | %s (Tab)", tool.clipPointCount(), keepNames[tool.clipKeepMode()]);
        if (ImGui::Button("Commit Clip (Enter)"))
            tool.commitClip();
    }

    // ---- Carve ----
    if (!sel.empty() && tool.mode() == BrushToolMode::OFF)
    {
        ImGui::Separator();
        if (ImGui::Button("Carve With Selected"))
            tool.carveWithSelected();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Subtract the selected brush from every brush it overlaps.\nThe carver itself is kept — delete it afterwards if unwanted.");
    }

    // ---- Brush type / content flags ----
    Brush* primary = sel.empty() ? nullptr : BrushManager::Get()->getBrush(sel[0]);
    if (primary && tool.mode() == BrushToolMode::OFF)
    {
        ImGui::Separator();
        ImGui::Text("Brush Type");

        // Preset buttons: flags + matching tool texture in one undoable click.
        // Active preset highlights (TRIGGER_ONCE modifier ignored for the match).
        const irr::u32 matchFlags = primary->contentFlags & ~CONTENT_TRIGGER_ONCE;
        auto presetButton = [&](const char* label, irr::u32 flags, const char* texture)
        {
            const bool active = (matchFlags == flags);
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.85f, 0.3f, 1.0f));
            }
            if (ImGui::SmallButton(label) && !active)
                applyToolPreset(sel, flags, texture);
            if (active)
                ImGui::PopStyleColor(2);
        };

        presetButton("Solid", 0, nullptr);
        ImGui::SameLine(); presetButton(kToolPresets[0].label, kToolPresets[0].flags, kToolPresets[0].texture);
        ImGui::SameLine(); presetButton(kToolPresets[1].label, kToolPresets[1].flags, kToolPresets[1].texture);
        presetButton(kToolPresets[2].label, kToolPresets[2].flags, kToolPresets[2].texture);
        ImGui::SameLine(); presetButton(kToolPresets[3].label, kToolPresets[3].flags, kToolPresets[3].texture);
        ImGui::SameLine(); presetButton(kToolPresets[4].label, kToolPresets[4].flags, kToolPresets[4].texture);

        if (primary->isToolBrush())
            ImGui::TextDisabled("Tool brush: stripped from the game's render mesh");

        // Individual bits for combinations the presets don't cover.
        // These touch flags only — textures stay as they are.
        if (ImGui::TreeNode("Flags##brush_content"))
        {
            auto flagCheckbox = [&](const char* label, irr::u32 flag)
            {
                bool on = (primary->contentFlags & flag) != 0;
                if (ImGui::Checkbox(label, &on))
                    setContentFlag(sel, flag, on);
            };
            flagCheckbox("Player clip",  CONTENT_CLIP_PLAYER);
            flagCheckbox("Monster clip", CONTENT_CLIP_MONSTER);
            flagCheckbox("Weapon clip",  CONTENT_CLIP_WEAPON);
            flagCheckbox("Trigger",      CONTENT_TRIGGER);
            flagCheckbox("Sky",          CONTENT_SKY);
            ImGui::TreePop();
        }

        // ---- Trigger settings ----
        if (primary->contentFlags & CONTENT_TRIGGER)
        {
            ImGui::Separator();
            ImGui::Text("Trigger");

            bool once = (primary->contentFlags & CONTENT_TRIGGER_ONCE) != 0;
            if (ImGui::Checkbox("Fire once", &once))
                setContentFlag(sel, CONTENT_TRIGGER_ONCE, once);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fire a single time per play session instead of re-arming on exit");

            if (sel.size() > 1)
                ImGui::TextDisabled("Targets edit the primary brush only");

            ImGui::Text("On enter, signal:");
            ImGui::SameLine();

            const bool pickingThis = g_sceneInteractor.isLinkPicking()
                && g_sceneInteractor.linkPickField() == LinkPickField::BRUSH_RECEIVER
                && g_sceneInteractor.linkPickBrushId() == primary->id;
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
                if (ImGui::SmallButton("Link...##brush_receiver"))
                    g_sceneInteractor.beginBrushLinkPick(primary->id);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Shift+Click an entity in the viewport (or click it in the hierarchy)\nto add it to this trigger's targets");
            }

            // Chip removal is undoable: snapshot the pre-edit state on change
            const std::string receiverBefore = primary->receiver;
            s_drawNameListChips(primary->receiver, "brush_receiver_chips");
            if (primary->receiver != receiverBefore)
            {
                UndoEntry entry;
                BrushSnapshot snap;
                snap.id = primary->id;
                snap.existed = true;
                snap.data = *primary;
                snap.data.receiver = receiverBefore;
                entry.brushes.push_back(std::move(snap));
                g_sceneInteractor.pushUndoEntry(std::move(entry));
            }
        }
    }

    // ---- Face properties ----
    const auto& faces = tool.selectedFaces();
    if (tool.mode() == BrushToolMode::FACE && !faces.empty())
    {
        ImGui::Separator();
        ImGui::Text("Faces selected: %d", static_cast<int>(faces.size()));

        Brush* brush = BrushManager::Get()->getBrush(faces[0].brushId);
        if (brush && faces[0].faceIndex >= 0 &&
            faces[0].faceIndex < static_cast<int>(brush->faces.size()))
        {
            BrushFace& face = brush->faces[faces[0].faceIndex];

            if (face.materialName.empty())
                ImGui::TextDisabled("(no material)");
            else
                ImGui::TextWrapped("%s", face.materialName.c_str());

            if (ImGui::Button("Apply Texture...##face"))
                show_window_texture_browser("brush_face_material");
            if (g_textureBrowserRequestID == "brush_face_material" && g_currentSelectedTexture != "null")
            {
                tool.applyMaterialToSelectedFaces(g_currentSelectedTexture);
                g_currentSelectedTexture = "null";
                g_textureBrowserRequestID.clear();
            }

            // UV fields edit the primary face; recompile on change.
            // Offset is in world units; tile size = world units per repeat.
            bool changed = false;
            ImGui::SetNextItemWidth(150);
            changed |= ImGui::DragFloat2("Offset##face_shift", &face.shiftU, 0.05f);
            ImGui::SetNextItemWidth(150);
            changed |= ImGui::DragFloat2("Tile size##face_scale", &face.scaleU, 0.05f, 0.05f, 64.0f);
            if (face.scaleU < 0.05f) face.scaleU = 0.05f;
            if (face.scaleV < 0.05f) face.scaleV = 0.05f;

            bool nodraw = (face.flags & FACE_NODRAW) != 0;
            if (ImGui::Checkbox("No draw (collision only)", &nodraw))
            {
                face.flags = static_cast<irr::u8>(nodraw ? (face.flags | FACE_NODRAW)
                                                         : (face.flags & ~FACE_NODRAW));
                changed = true;
            }

            if (changed)
                BrushManager::Get()->markBrushDirty(faces[0].brushId);
        }
    }

    ImGui::End();
}
