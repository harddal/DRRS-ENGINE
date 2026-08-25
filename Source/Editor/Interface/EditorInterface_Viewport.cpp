#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include <IMGUI/imgui.h>
#include <IMGUI/imgui_internal.h>

#include "Editor/EditorState.h"
#include "Editor/EditorViewport.h"
#include "Engine/Renderer/RenderManager.h"

// The 3D viewport panel.
//
// The scene is rendered by RenderManager at this panel's resolution and copied into
// m_viewportRTT; all this window does is display that texture, publish its rect so the
// rest of the editor can map coordinates into it, and host the transform gizmo's
// draw list.
void EditorInterface::draw_window_viewport()
{
    auto* rm = RenderManager::Get();

    if (!m_windowData.draw_window_viewport)
    {
        // Panel closed: hold the last known size so the RTT chain is not reallocated,
        // and report the pane invalid so scene tools stop accepting input.
        EditorViewport::Pane pane = EditorViewport::live();
        pane.valid   = false;
        pane.hovered = false;
        EditorViewport::publish(pane);
        return;
    }

    // Keep the 3D view inside the main window while tool panels are free to be torn
    // out onto other monitors.
    //
    // The reason is the render path, not preference: the scene is drawn into the main
    // window's BACKBUFFER and copied into m_viewportRTT, and renderSize() is clamped
    // to getScreenSize(). A torn-off Viewport larger than the main window would render
    // at reduced resolution and be upscaled.
    //
    // Two flags are needed. NoMove blocks dragging the tab or title bar out.
    // NoUndocking closes the remaining hole: it lives on the dock NODE, so if a tool
    // panel is ever tabbed into the central node and happens to be the selected tab,
    // its undock cannot drag the whole node — Viewport included — out with it.
    // It goes through DockNodeFlagsOverrideSet rather than a DockBuilder LocalFlags
    // write because NoUndocking is in neither the saved nor the transfer flag mask,
    // so a LocalFlags write would silently vanish on ini reload or a node split.
    ImGuiWindowClass viewportClass;                  // ClassId 0 — still docks anywhere
    viewportClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking;
    ImGui::SetNextWindowClass(&viewportClass);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin("Viewport", &m_windowData.draw_window_viewport,
                                   ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();

    EditorViewport::Pane pane = EditorViewport::live();

    if (!open)
    {
        // Collapsed or in an inactive dock tab. Same treatment as closed: keep the
        // last size (falling back to it avoids thrashing the RTT chain when tabbing
        // back and forth), drop validity.
        pane.valid   = false;
        pane.hovered = false;
        EditorViewport::publish(pane);
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    if (avail.x >= 1.0f && avail.y >= 1.0f)
    {
        pane.pos   = origin;
        pane.size  = avail;
        pane.valid = true;
    }
    else
    {
        pane.valid = false;
    }

    // Deliberately NOT an InvisibleButton. Registering an item here would make
    // ImGui::IsAnyItemHovered() true whenever the cursor is over the 3D view, which
    // is exactly the condition every scene tool (and the gizmo's CanActivate) uses to
    // decide it should stand down. Window-level hover carries the same information
    // without poisoning that signal.
    pane.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    const unsigned int tex = rm->viewportGLTexture();
    if (tex && pane.valid)
    {
        // Irrlicht render targets are stored bottom-up (the driver compensates in the
        // fixed-function texture matrix, which does not apply to an ImGui draw), so
        // the V axis is flipped here.
        //
        // The texture is renderSize() and the panel is pane.size; those agree at rest
        // but differ for a few frames while a resize is being debounced, in which case
        // the image is stretched to fit rather than cropped.
        ImGui::GetWindowDrawList()->AddImage(
            ImTextureRef((ImTextureID)(uintptr_t)tex),
            origin,
            ImVec2(origin.x + avail.x, origin.y + avail.y),
            ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }

    EditorViewport::publish(pane);

    // Tell the renderer what to render at next. draw() runs later this frame and reads
    // the frozen pane, so this and the image above stay one consistent pair.
    if (pane.valid)
    {
        rm->setViewportPanelSize(irr::core::dimension2du(
            static_cast<irr::u32>(pane.size.x),
            static_cast<irr::u32>(pane.size.y)));
    }

    // The gizmo must be submitted while this window is current: ImTransformControl's
    // SetDrawlist() captures ImGui::GetWindowDrawList(), so anywhere else it would draw
    // underneath the docked panels and outside this window's clip rect.
    g_sceneInteractor.draw();

    ImGui::End();
}

// Build the default dock arrangement. Called once when MainDockSpace has no nodes
// (fresh install or deleted imgui.ini) and on demand from View > Reset Layout.
void EditorInterface::reset_dock_layout()
{
    const ImGuiID dockspace = ImGui::GetID("MainDockSpace");

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);

    ImGuiID centre = dockspace;
    const ImGuiID left   = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left,  0.18f, nullptr, &centre);
    const ImGuiID right  = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.24f, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down,  0.26f, nullptr, &centre);

    ImGui::DockBuilderDockWindow("Viewport", centre);

    ImGui::DockBuilderDockWindow("Scene Hierarchy", left);

    ImGui::DockBuilderDockWindow("Entity Properties", right);
    ImGui::DockBuilderDockWindow("Scene Properties", right);
    ImGui::DockBuilderDockWindow("Brush Editor", right);

    ImGui::DockBuilderDockWindow("Log", bottom);
    ImGui::DockBuilderDockWindow("Entity Spawn Menu", bottom);
    ImGui::DockBuilderDockWindow("Prop Spawn Menu", bottom);
    ImGui::DockBuilderDockWindow("Scene Stats", bottom);

    ImGui::DockBuilderFinish(dockspace);

    m_windowData.draw_window_viewport = true;
}
