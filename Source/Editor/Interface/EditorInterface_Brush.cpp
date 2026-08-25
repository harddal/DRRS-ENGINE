#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"

#include <IMGUI/imgui.h>

#include "Engine/Engine.h"
#include "Engine/Brush/BrushGeometry.h"
#include "Engine/Brush/BrushManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Editor/BrushTool.h"
#include "Utility/Utility.h"
#include "Game/Components.h"

#include <irrlicht/source/Irrlicht/COpenGLTexture.h>

#include <algorithm>
#include <string>

namespace
{
    // Mode button that highlights when active and enforces painter exclusivity.
    void modeButton(const char* label, BrushToolMode mode, BrushTool& tool, const char* tooltip = nullptr)
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
        if (tooltip)
            ImGui::SetItemTooltip("%s", tooltip);
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
        { "Player Clip",  CONTENT_CLIP_PLAYER,  TOOL_TEXTURE_PLAYER_CLIP               },
        { "Monster Clip", CONTENT_CLIP_MONSTER, "content/texture/tool/monsterclip.png" },
        { "Weapon Clip",  CONTENT_CLIP_WEAPON,  "content/texture/tool/weaponclip.png"  },
        { "Trigger",      CONTENT_TRIGGER,      "content/texture/tool/trigger.png"     },
        { "Sky",          CONTENT_SKY,          "content/texture/tool/sky.png"         },
        { "Ladder",       CONTENT_LADDER,       "content/texture/tool/ladder.png"      },
        { "Fog",          CONTENT_FOG,          "content/texture/tool/fog.png"         },
        { "Hurt",         CONTENT_HURT,         "content/texture/tool/damage.png"      },
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

    // Resolve a brush material name to an ImGui-drawable texture id, using
    // the same convention as BrushCompiler (full path first, then the bare
    // extensionless texture-browser fallback).  0 when unresolved.
    ImTextureID materialPreviewTexId(const std::string& materialName)
    {
        if (materialName.empty())
            return 0;

        auto* driver = RenderManager::Get()->driver();
        irr::video::ITexture* tex = driver->getTexture(materialName.c_str());
        if (!tex && materialName.find('/') == std::string::npos &&
                    materialName.find('\\') == std::string::npos)
        {
            const std::string resolved = "content/texture/" + materialName + ".png";
            tex = driver->getTexture(resolved.c_str());
        }
        if (!tex)
            return 0;

        const GLuint glTex =
            static_cast<irr::video::COpenGLTexture*>(tex)->getOpenGLTextureName();
        return (ImTextureID)(uintptr_t)glTex;
    }

    // Undo for face-level edits: dedupe the owning brushes of the selected
    // faces and snapshot them through the brush-undo path.
    void pushSelectedFaceUndo(const std::vector<BrushTool::FaceRef>& faces)
    {
        std::vector<uint32_t> ids;
        for (const auto& ref : faces)
            if (std::find(ids.begin(), ids.end(), ref.brushId) == ids.end())
                ids.push_back(ref.brushId);
        pushSelectedBrushUndo(ids);
    }

    // Apply fn to every valid selected face, then recompile each owning brush
    // once (a face selection can span multiple brushes).
    template <typename F>
    void forEachSelectedFace(const std::vector<BrushTool::FaceRef>& faces, F&& fn)
    {
        std::vector<uint32_t> dirty;
        for (const auto& ref : faces)
        {
            Brush* b = BrushManager::Get()->getBrush(ref.brushId);
            if (!b || ref.faceIndex < 0 ||
                ref.faceIndex >= static_cast<int>(b->faces.size()))
                continue;
            fn(b->faces[ref.faceIndex]);
            if (std::find(dirty.begin(), dirty.end(), ref.brushId) == dirty.end())
                dirty.push_back(ref.brushId);
        }
        for (uint32_t id : dirty)
            BrushManager::Get()->markBrushDirty(id);
    }
}

void EditorInterface::draw_window_brush_editor()
{
    if (!m_windowData.draw_window_brush_editor)
        return;
    if (!BrushManager::Get())
        return;

    BrushTool& tool = g_sceneInteractor.getBrushTool();

    //ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(280, 460));
    if (!ImGui::Begin("Brush Editor", &m_windowData.draw_window_brush_editor))
    {
        ImGui::End();
        return;
    }

    // ---- Tool modes ----
    ImGui::Text("Tool");
    modeButton("Create", BrushToolMode::CREATE, tool,
        "Draw new brush geometry: drag a footprint in the viewport,\nrelease to extrude it to the Height below."); ImGui::SameLine();
    modeButton("Face",   BrushToolMode::FACE,   tool,
        "Select individual faces and push/pull them along their normal.\nAlso edits per-face texture and UVs below."); ImGui::SameLine();
    modeButton("Vertex", BrushToolMode::VERTEX, tool,
        "Drag corner vertices to reshape a brush.\nEdits that would make an invalid shape revert."); ImGui::SameLine();
    modeButton("Clip",   BrushToolMode::CLIP,   tool,
        "Slice brushes along a plane defined by 2-3 clicked points.\nTab flips which side is kept, Enter commits."); ImGui::SameLine();
    modeButton("Texture", BrushToolMode::PAINT, tool,
        "Paint the Material below onto faces by clicking/dragging.\nCtrl+click samples a face's existing material."); ImGui::SameLine();
    modeButton("Cutout", BrushToolMode::STAMP, tool,
        "Outline a shape on a brush face by clicking points\n(auto convex hull), then Enter carves it through the brush."); ImGui::SameLine();
    modeButton("Sculpt", BrushToolMode::SCULPT, tool,
        "Turn a quad face into a displacement grid and sculpt it\ninto organic, low-poly cave/cliff/terrain surfaces.");

    switch (tool.mode())
    {
    case BrushToolMode::OFF:
        ImGui::TextDisabled("Select: Shift+click a brush; gizmo moves it");
        break;
    case BrushToolMode::CREATE:
        ImGui::TextDisabled("Drag a footprint; release builds the primitive upward");
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
    case BrushToolMode::STAMP:
        ImGui::TextDisabled("Click points on a brush face; Enter cuts the shape out");
        break;
    case BrushToolMode::SCULPT:
        ImGui::TextDisabled("Click a quad face to select; Make Displacement, then drag to sculpt");
        break;
    }

    ImGui::Separator();

    // ---- Create settings ----
    ImGui::Text("Primitive");
    // Must stay in BrushPrimitive order — the combo maps onto the enum by index.
    static const char* primNames[] = { "Box", "Wedge", "Cylinder", "Cone",
                                       "Stairs", "Arch", "Gothic Arch" };
    int prim = static_cast<int>(tool.primitive);
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("##brush_prim", &prim, primNames, IM_ARRAYSIZE(primNames)))
        tool.primitive = static_cast<BrushPrimitive>(prim);

    // Per-primitive parameters.  Each primitive owns its own block; clamps are
    // written out longhand rather than with std::min/max because this TU can
    // reach the Windows min/max macros through RenderManager.h.
    static const char* axisNames[]    = { "Auto", "+X", "-X", "+Z", "-Z" };
    static const char* revolveNames[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

    switch (tool.primitive)
    {
    case BrushPrimitive::CYLINDER:
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Sides", &tool.cylinderSides);
        if (tool.cylinderSides < 3)  tool.cylinderSides = 3;
        if (tool.cylinderSides > 32) tool.cylinderSides = 32;

        int rax = static_cast<int>(tool.cylinderAxis);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("Axis##cyl", &rax, revolveNames, IM_ARRAYSIZE(revolveNames)))
            tool.cylinderAxis = static_cast<BrushRevolveChoice>(rax);
        ImGui::SetItemTooltip("Axis of revolution.  Lay a tube on its side with +X or +Z,\n"
                              "then Hollow it to get a tunnel.");
        break;
    }

    case BrushPrimitive::CONE:
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Sides", &tool.cylinderSides);
        if (tool.cylinderSides < 3)  tool.cylinderSides = 3;
        if (tool.cylinderSides > 32) tool.cylinderSides = 32;
        ImGui::SetItemTooltip("4 sides gives a pyramid or a tapered box.");

        ImGui::SetNextItemWidth(90);
        ImGui::InputFloat("Top scale", &tool.coneTopScale, 0.0f, 0.0f, "%.3f");
        if (tool.coneTopScale < 0.0f) tool.coneTopScale = 0.0f;
        if (tool.coneTopScale > 1.0f) tool.coneTopScale = 1.0f;
        ImGui::SetItemTooltip("0 = a point (spires, pyramids)\n"
                              "between = a frustum (battered wall bases, plinths, capitals)\n"
                              "1 = a plain prism\n"
                              "A top ring too fine for the grid snaps to a true point.");

        int rax = static_cast<int>(tool.coneAxis);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("Axis##cone", &rax, revolveNames, IM_ARRAYSIZE(revolveNames)))
            tool.coneAxis = static_cast<BrushRevolveChoice>(rax);
        ImGui::SetItemTooltip("Axis of revolution.  The sign picks which end tapers,\n"
                              "so -Y flares upward from the same drag.");
        break;
    }

    case BrushPrimitive::STAIRS:
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Steps", &tool.stairSteps);
        if (tool.stairSteps < 2)  tool.stairSteps = 2;
        if (tool.stairSteps > 64) tool.stairSteps = 64;
        ImGui::SetItemTooltip("Number of step brushes.  Each step is one solid box\n"
                              "rising from the staircase floor to that step's top.");

        int ax = static_cast<int>(tool.stairAxis);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("Ascend", &ax, axisNames, IM_ARRAYSIZE(axisNames)))
            tool.stairAxis = static_cast<BrushAxisChoice>(ax);
        ImGui::SetItemTooltip("Direction the staircase climbs.\n"
                              "Auto follows the dominant drag direction.");

        ImGui::Checkbox("Player clip ramp", &tool.stairClipRamp);
        ImGui::SetItemTooltip("Also emit one PLAYER CLIP brush sloped just above the step\n"
                              "tops, so the player glides up instead of bumping each riser.\n"
                              "It does not render.");
        break;
    }

    case BrushPrimitive::ARCH:
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Segments", &tool.archSegments);
        if (tool.archSegments < 3)  tool.archSegments = 3;
        if (tool.archSegments > 32) tool.archSegments = 32;
        ImGui::SetItemTooltip("Voussoirs around the arc — one brush each.");

        ImGui::SetNextItemWidth(90);
        ImGui::InputFloat("Arc deg", &tool.archArcDeg, 0.0f, 0.0f, "%.0f");
        if (tool.archArcDeg < 10.0f)  tool.archArcDeg = 10.0f;
        if (tool.archArcDeg > 360.0f) tool.archArcDeg = 360.0f;
        ImGui::SetItemTooltip("180 = doorway (upper half of the footprint)\n"
                              "360 = closed ring / tunnel");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputFloat("Start deg", &tool.archStartDeg, 0.0f, 0.0f, "%.0f");
        ImGui::SetItemTooltip("Where the sweep begins, counter-clockwise from the span axis.");

        ImGui::SetNextItemWidth(90);
        ImGui::InputFloat("Wall", &tool.archWallDepth, 0.0f, 0.0f, "%.2f");
        if (tool.archWallDepth < 0.01f) tool.archWallDepth = 0.01f;
        ImGui::SetItemTooltip("Radial thickness of the arch band (world units).\n"
                              "Clamped at build time so the opening never closes.");

        int ax = static_cast<int>(tool.archAxis);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("Span", &ax, axisNames, IM_ARRAYSIZE(axisNames)))
            tool.archAxis = static_cast<BrushAxisChoice>(ax);
        ImGui::SetItemTooltip("Horizontal axis the arch spans; the other becomes its depth.\n"
                              "Auto uses the longer footprint side.  A negative axis mirrors\n"
                              "the sweep direction.");
        break;
    }

    case BrushPrimitive::GOTHIC_ARCH:
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Segments", &tool.gothicSegments);
        if (tool.gothicSegments < 2)  tool.gothicSegments = 2;
        if (tool.gothicSegments > 16) tool.gothicSegments = 16;
        ImGui::SetItemTooltip("Voussoirs PER SIDE — the arch is twice this many brushes.");

        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Pointiness", &tool.gothicPointiness, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("0 = a plain semicircular arch\n"
                              "1 = the equilateral gothic arch\n"
                              "The apex always lands on the top of the box, so this only\n"
                              "changes the shape, never the size.");

        ImGui::SetNextItemWidth(90);
        ImGui::InputFloat("Wall##gothic", &tool.gothicWallDepth, 0.0f, 0.0f, "%.2f");
        if (tool.gothicWallDepth < 0.01f) tool.gothicWallDepth = 0.01f;
        ImGui::SetItemTooltip("Radial thickness of the arch band (world units).\n"
                              "Clamped at build time so the opening never closes.");

        int ax = static_cast<int>(tool.gothicAxis);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("Span##gothic", &ax, axisNames, IM_ARRAYSIZE(axisNames)))
            tool.gothicAxis = static_cast<BrushAxisChoice>(ax);
        ImGui::SetItemTooltip("Horizontal axis the arch spans; the other becomes its depth.\n"
                              "Auto uses the longer footprint side.");
        break;
    }

    case BrushPrimitive::BOX:
    case BrushPrimitive::WEDGE:
    default:
        break;
    }

    ImGui::Text("Height:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##brush_height", &tool.defaultHeight, 0.0f, 0.0f, "%.2f");
    ImGui::SetItemTooltip("Extrusion height (world units) given to newly created brushes.");
    if (tool.defaultHeight < 0.1f) tool.defaultHeight = 0.1f;

    // ---- Material ----
    ImGui::Separator();
    ImGui::Text("Material");
    if (tool.defaultMaterial.empty())
        ImGui::TextDisabled("(none)");
    else
    {
        if (const ImTextureID texId = materialPreviewTexId(tool.defaultMaterial))
        {
            ImGui::Image(ImTextureRef(texId), DPI_SCALED_IMVEC2(96, 96));
            ImGui::SameLine();
        }
        ImGui::TextWrapped("%s", tool.defaultMaterial.c_str());
    }

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
    ImGui::SetItemTooltip("Edge length of the world cells brushes are batched into for rendering.\nSmaller cells = faster edits/finer rebuilds, larger cells = fewer draw calls.\nTakes effect on Apply (rebuilds all chunks).");
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

    // ---- Cutout controls ----
    if (tool.mode() == BrushToolMode::STAMP)
    {
        ImGui::Separator();
        ImGui::Text("Cutout points: %d", tool.stampPointCount());
        if (ImGui::Button("Commit Cutout (Enter)"))
            tool.commitStamp();
        ImGui::SetItemTooltip("Carve the outlined shape through the brush.\nThe red wireframe shows the removed volume; Esc clears the points.");
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

    // ---- Sculpt / displacement controls ----
    if (tool.mode() == BrushToolMode::SCULPT)
    {
        ImGui::Separator();

        const bool haveFace = !tool.selectedFaces().empty();
        const BrushTool::FaceRef selFace = haveFace ? tool.selectedFaces().front()
                                                     : BrushTool::FaceRef();
        const bool displaced = haveFace && tool.faceIsDisplaced(selFace);

        if (!haveFace)
        {
            ImGui::TextDisabled("Click a quad face in the viewport to select it.");
        }
        else if (!displaced)
        {
            // Grid resolution for the new displacement.
            static const char* powerNames[] = { "5 x 5", "9 x 9", "17 x 17" };
            int powIdx = std::max(0, std::min(2, tool.dispPower - 2));
            ImGui::Text("Grid");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("##disp_power", &powIdx, powerNames, 3))
                tool.dispPower = powIdx + 2;
            if (ImGui::Button("Make Displacement"))
                tool.makeDisplacement(selFace, tool.dispPower);
            ImGui::SetItemTooltip("Subdivide the selected quad face into a sculptable grid.\nOnly 4-sided faces can become displacements.");
        }
        else
        {
            // Sub-tool picker.
            ImGui::Text("Brush");
            struct SculptToolButton { const char* label; SculptTool value; };
            const SculptToolButton kTools[] = {
                { "Raise",   SculptTool::RAISE   },
                { "Lower",   SculptTool::LOWER   },
                { "Smooth",  SculptTool::SMOOTH  },
                { "Noise",   SculptTool::NOISE   },
                { "Flatten", SculptTool::FLATTEN },
            };
            for (int i = 0; i < 5; i++)
            {
                const bool on = (tool.sculptTool == kTools[i].value);
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                if (ImGui::Button(kTools[i].label))
                    tool.sculptTool = kTools[i].value;
                if (on) ImGui::PopStyleColor();
                if (i < 4) ImGui::SameLine();
            }

            ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("Radius",   &tool.sculptRadius,   0.25f, 20.0f, "%.2f");
            ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("Strength", &tool.sculptStrength, 0.1f,  20.0f, "%.2f");
            ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("Falloff",  &tool.sculptFalloff,  0.0f,  1.0f,  "%.2f");
            if (tool.sculptTool == SculptTool::NOISE)
            {
                ImGui::SetNextItemWidth(160);
                ImGui::SliderFloat("Amount", &tool.sculptNoise, 0.1f, 10.0f, "%.2f");
            }
            ImGui::TextDisabled("Hold and drag on the face to sculpt.");

            if (ImGui::Button("Remove Displacement"))
                tool.removeDisplacement(selFace);
            ImGui::SetItemTooltip("Flatten the face back to a plain, undisplaced quad.");
        }
    }

    // ---- Carve / Hollow ----
    if (!sel.empty() && tool.mode() == BrushToolMode::OFF)
    {
        ImGui::Separator();
        if (ImGui::Button("Carve With Selected"))
            tool.carveWithSelected();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Subtract the selected brush from every brush it overlaps.\nThe carver itself is kept — delete it afterwards if unwanted.");

        // Hollow.  No magnitude clamp beyond a sanity bound: the kernel owns the
        // two-quantum floor, and duplicating that rule here would let them drift.
        ImGui::SetNextItemWidth(80);
        ImGui::InputFloat("##hollow_thick", &tool.hollowThickness, 0.0f, 0.0f, "%.3f");
        if (tool.hollowThickness >  64.0f) tool.hollowThickness =  64.0f;
        if (tool.hollowThickness < -64.0f) tool.hollowThickness = -64.0f;
        ImGui::SameLine();
        if (ImGui::Button("Hollow"))
            tool.hollowSelected();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Replace each selected brush with a shell of wall brushes.\n"
                              "Positive thickness hollows inward (the brush becomes the room);\n"
                              "negative wraps a shell around it.  Works on any convex brush —\n"
                              "a cylinder becomes a tube.  Brushes too small to carry the\n"
                              "wall are left alone.");

        ImGui::SameLine();
        if (ImGui::Button("Convert to Mover"))
        {
            // Unique name across live entities AND existing mover groups
            std::string moverName;
            for (int n = 1;; n++)
            {
                moverName = "mover_" + std::to_string(n);
                bool taken = WorldManager::Get()->managerSystem()->doesEntityExist(moverName);
                if (!taken)
                {
                    for (auto& g : BrushManager::Get()->getMoverGroups())
                        if (g.first == moverName) { taken = true; break; }
                }
                if (!taken)
                    break;
            }

            const std::vector<uint32_t> ids = sel;  // copy — selection cleared below
            const irr::core::vector3df pivot = BrushManager::Get()->markAsMover(ids, moverName);

            // Same component set as the slidingdoor.ent prefab; the mesh comes
            // from the cache-registered brush compile instead of a file.
            auto entity = WorldManager::Get()->world()->createEntity();

            entity.addComponent<DescriptorComponent>();
            auto& desc = entity.getComponent<DescriptorComponent>();
            desc.id             = WorldManager::Get()->getNewID();
            desc.name           = moverName;
            desc.type           = ET_DYNAMIC;
            desc.isSerializable = true;
            desc.isDebug        = false;
            desc.isAlive        = true;

            entity.addComponent<TransformComponent>();
            entity.getComponent<TransformComponent>().setPosition(pivot);

            entity.addComponent<RenderComponent>();

            entity.addComponent<MeshComponent>();
            auto& mc = entity.getComponent<MeshComponent>();
            mc.mesh        = BrushManager::moverMeshName(moverName);
            mc.navCookable = false;

            entity.addComponent<PhysicsComponent>();
            auto& pc = entity.getComponent<PhysicsComponent>();
            pc.type      = PCT_BOX;
            pc.kinematic = true;
            pc.active    = true;

            entity.addComponent<LogicComponent>();

            entity.addComponent<ScriptComponent>();
            entity.getComponent<ScriptComponent>().script = "content/script/object/sliding_door.asc";

            entity.addComponent<SoundComponent>();
            auto& snd = entity.getComponent<SoundComponent>();
            snd.add(sSoundData("content/sound/effect/switch7.wav", "open",  true, false, 100.0f, 5.0f, 100.0f));
            snd.add(sSoundData("content/sound/effect/switch7.wav", "close", true, false, 100.0f, 5.0f, 100.0f));

            entity.activate();
            WorldManager::Get()->world()->refresh();

            g_sceneInteractor.clearSelectedBrushes();
            g_sceneInteractor.setSelectedEntity(desc.id);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turn the selected brushes into a door/elevator entity.\nDefaults to sliding_door.asc — swap the script for elevator.asc in the property panel.\nUse the Movers list below to revert.");
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
        auto presetButton = [&](const char* label, irr::u32 flags, const char* texture, const char* tooltip)
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
            if (tooltip)
                ImGui::SetItemTooltip("%s", tooltip);
        };

        presetButton("Solid", 0, nullptr,
            "Normal visible world geometry.");
        ImGui::SameLine(); presetButton(kToolPresets[0].label, kToolPresets[0].flags, kToolPresets[0].texture,
            "Invisible in-game; blocks only the player.\nSmooths stairs/edges, fences off out-of-bounds areas.");
        ImGui::SameLine(); presetButton(kToolPresets[1].label, kToolPresets[1].flags, kToolPresets[1].texture,
            "Invisible in-game; blocks only NPCs.\nKeeps AI out of places players can go.");
        presetButton(kToolPresets[2].label, kToolPresets[2].flags, kToolPresets[2].texture,
            "Invisible in-game; blocks only weapon fire and projectiles.");
        ImGui::SameLine(); presetButton(kToolPresets[3].label, kToolPresets[3].flags, kToolPresets[3].texture,
            "Invisible volume that signals its target entities when entered.\nSet targets in the Trigger section below.");
        ImGui::SameLine(); presetButton(kToolPresets[4].label, kToolPresets[4].flags, kToolPresets[4].texture,
            "Marks sky: stripped from the game's render mesh so the skybox shows through.");
        presetButton(kToolPresets[5].label, kToolPresets[5].flags, kToolPresets[5].texture,
            "Invisible climbable volume: W climbs up, S climbs down, jump detaches.\nPlace it flush against the wall/ladder mesh the player climbs.");
        ImGui::SameLine(); presetButton(kToolPresets[6].label, kToolPresets[6].flags, kToolPresets[6].texture,
            "Invisible volume that overrides the scene fog while the player is inside,\ncross-fading back to the scene defaults on exit.\nSet the fog values in the Fog Zone section below.");
        presetButton(kToolPresets[7].label, kToolPresets[7].flags, kToolPresets[7].texture,
            "Invisible volume that damages the player while they stand in it.\nSet the rate in the Hurt Volume section below.");

        if (primary->isToolBrush())
            ImGui::TextDisabled("Tool brush: stripped from the game's render mesh");

        // Individual bits for combinations the presets don't cover.
        // These touch flags only — textures stay as they are.
        if (ImGui::TreeNode("Flags##brush_content"))
        {
            ImGui::SetItemTooltip("Raw content flags for combinations the presets don't cover.\nUnlike the presets these leave face textures untouched.");
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
            flagCheckbox("Ladder",       CONTENT_LADDER);
            flagCheckbox("Fog",          CONTENT_FOG);
            flagCheckbox("Hurt",         CONTENT_HURT);
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

        // ---- Fog zone settings ----
        // Fog values are pure runtime data (GameplaySystem reads them each
        // frame), so unlike face/geometry edits they never markBrushDirty.
        if (primary->contentFlags & CONTENT_FOG)
        {
            ImGui::Separator();
            ImGui::Text("Fog Zone");
            if (sel.size() > 1)
                ImGui::TextDisabled("Values edit the primary brush only");

            ImGui::SetNextItemWidth(150);
            ImGui::DragFloat("Density##fog", &primary->fogDensity, 0.001f, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemActivated())
                pushSelectedBrushUndo({ primary->id });
            ImGui::SetItemTooltip("Fog thickness while inside the volume (0 = clear).\nSame scale as the scene's fog density.");

            ImGui::SetNextItemWidth(150);
            ImGui::DragFloat("Start##fog", &primary->fogStart, 0.5f, 0.0f, 10000.0f, "%.1f");
            if (ImGui::IsItemActivated())
                pushSelectedBrushUndo({ primary->id });
            ImGui::SetItemTooltip("View distance before fog begins, in world units.");

            ImGui::SetNextItemWidth(200);
            ImGui::ColorEdit3("Color##fog", &primary->fogColor.r);
            if (ImGui::IsItemActivated())
                pushSelectedBrushUndo({ primary->id });

            if (ImGui::SmallButton("Seed from scene##fog"))
            {
                pushSelectedBrushUndo({ primary->id });
                const SceneDescriptor sd = WorldManager::Get()->getCurrentSceneDescriptor();
                primary->fogDensity = sd.fogDensity;
                primary->fogStart   = sd.fogStart;
                primary->fogColor   = sd.fogColor;
            }
            ImGui::SetItemTooltip("Copy the scene's current fog values into this volume as a starting point.");
        }

        // ---- Hurt volume settings ----
        // Like the fog values this is runtime-only data, so no markBrushDirty.
        if (primary->contentFlags & CONTENT_HURT)
        {
            ImGui::Separator();
            ImGui::Text("Hurt Volume");
            if (sel.size() > 1)
                ImGui::TextDisabled("Values edit the primary brush only");

            ImGui::SetNextItemWidth(150);
            ImGui::DragFloat("Damage/sec##hurt", &primary->hurtDamagePerSecond, 0.5f, 0.0f, 10000.0f, "%.1f");
            if (ImGui::IsItemActivated())
                pushSelectedBrushUndo({ primary->id });
            ImGui::SetItemTooltip(
                "Health drained per second while the player overlaps this volume.\n"
                "Applied in whole points, so fractional rates tick irregularly.\n"
                "0 disables the volume; overlapping hurt volumes stack.");

            // Default player health is 100 (DamageReceiverComponent::threshold)
            if (primary->hurtDamagePerSecond > 0.0f)
                ImGui::TextDisabled("~%.1fs to drain 100 health",
                    100.0f / primary->hurtDamagePerSecond);
        }
    }

    // ---- Movers ----
    const auto moverGroups = BrushManager::Get()->getMoverGroups();
    if (!moverGroups.empty())
    {
        ImGui::Separator();
        ImGui::Text("Movers");
        for (const auto& group : moverGroups)
        {
            ImGui::PushID(group.first.c_str());
            ImGui::Text("%s (%d brush%s)", group.first.c_str(), group.second,
                        group.second == 1 ? "" : "es");
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert to Brushes"))
            {
                for (auto* e : WorldManager::Get()->managerSystem()->getEntitiesByName(group.first))
                {
                    if (e && e->isValid())
                        WorldManager::Get()->killEntityByID(
                            e->getComponent<DescriptorComponent>().id);
                }
                BrushManager::Get()->revertMover(group.first);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Delete the mover entity and return its brushes to the world");
            ImGui::PopID();
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

            // ---- Face UVs ----
            // The primary face's values are the live edit buffer; any change
            // is copied to every selected face (absolute values, matching
            // "Apply Texture..." semantics).  Undo snapshots on drag-grab.
            ImGui::Text("Face UVs");

            bool changed = false;
            ImGui::SetNextItemWidth(150);
            changed |= ImGui::DragFloat2("Offset##face_shift", &face.shiftU, 0.05f);
            if (ImGui::IsItemActivated())
                pushSelectedFaceUndo(faces);
            ImGui::SetItemTooltip("Slide the texture across the face (U/V, world units).\nApplies to every selected face.");

            ImGui::SetNextItemWidth(150);
            changed |= ImGui::DragFloat2("Tile size##face_scale", &face.scaleU, 0.05f, 0.05f, 64.0f);
            if (ImGui::IsItemActivated())
                pushSelectedFaceUndo(faces);
            ImGui::SetItemTooltip("World units per texture repeat - larger values stretch the texture.\nApplies to every selected face.");

            if (face.scaleU < 0.05f) face.scaleU = 0.05f;
            if (face.scaleV < 0.05f) face.scaleV = 0.05f;

            if (ImGui::SmallButton("Reset UVs##face"))
            {
                pushSelectedFaceUndo(faces);
                face.shiftU = 0.0f; face.shiftV = 0.0f;
                face.scaleU = 2.0f; face.scaleV = 2.0f;
                changed = true;
            }
            ImGui::SetItemTooltip("Back to the defaults (offset 0/0, tile size 2/2) on all selected faces.");

            if (changed)
            {
                const float shiftU = face.shiftU, shiftV = face.shiftV;
                const float scaleU = face.scaleU, scaleV = face.scaleV;
                forEachSelectedFace(faces, [&](BrushFace& f)
                {
                    f.shiftU = shiftU; f.shiftV = shiftV;
                    f.scaleU = scaleU; f.scaleV = scaleV;
                });
            }

            bool nodraw = (face.flags & FACE_NODRAW) != 0;
            if (ImGui::Checkbox("No draw (collision only)", &nodraw))
            {
                pushSelectedFaceUndo(faces);
                forEachSelectedFace(faces, [&](BrushFace& f)
                {
                    f.flags = static_cast<irr::u8>(nodraw ? (f.flags | FACE_NODRAW)
                                                          : (f.flags & ~FACE_NODRAW));
                });
            }
            ImGui::SetItemTooltip("Skip these faces in the render mesh; they still block movement.\nApplies to every selected face.");
        }
    }

    ImGui::End();
}
