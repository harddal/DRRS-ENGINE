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
    modeButton("Clip",   BrushToolMode::CLIP,   tool);

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
    {
        // Windows open-file dialog, stored project-relative so the driver and
        // pak-mounted lookups both resolve it
        std::string chosen = Utility::RemoveAbsDir(
            Utility::OpenFileDialog(dialog_filter_image, "content\\texture"));
        if (!chosen.empty())
            tool.defaultMaterial = chosen;
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Selected Texture"))
    {
        // Texture-browser selections are bare extensionless names —
        // _asset_tex() turns them into "content/texture/<name>.png"
        if (g_currentSelectedTexture != "null" && !g_currentSelectedTexture.empty())
            tool.defaultMaterial = _asset_tex(g_currentSelectedTexture);
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
            {
                std::string chosen = Utility::RemoveAbsDir(
                    Utility::OpenFileDialog(dialog_filter_image, "content\\texture"));
                if (!chosen.empty())
                    tool.applyMaterialToSelectedFaces(chosen);
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply Selected"))
            {
                if (g_currentSelectedTexture != "null" && !g_currentSelectedTexture.empty())
                    tool.applyMaterialToSelectedFaces(_asset_tex(g_currentSelectedTexture));
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
