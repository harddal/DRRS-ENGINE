#pragma once

#include <IMGUI/imgui.h>

#include <ICameraSceneNode.h>
#include <line3d.h>
#include <position2d.h>
#include <vector3d.h>

// Coordinate-space authority for the editor's docked 3D viewport panel(s).
//
// Before the viewport became a panel, every editor coordinate space was "the whole
// window": gizmo rects were SetRect(0, 0, DisplaySize), picking fed raw cursor
// positions to Irrlicht's ray helpers, and the camera aspect came from the backbuffer.
// None of that holds once the 3D view lives inside a resizable dock node, so all of
// it routes through here instead.
//
// Indexed by pane so a future multi-pane (quad) view needs no second refactor; v1
// only ever uses pane 0.
namespace EditorViewport
{
    static const int MAX_PANES = 4;

    struct Pane
    {
        // Content-region top-left in ImGui screen pixels. ImGui multi-viewport is off,
        // so this is also Irrlicht client-window pixels — the same space
        // ICursorControl::getPosition() reports in.
        ImVec2 pos  = ImVec2(0.0f, 0.0f);
        ImVec2 size = ImVec2(1280.0f, 720.0f);

        bool valid   = false;   // panel open, visible and non-degenerate this frame
        bool hovered = false;
    };

    // Called once per frame by draw_window_viewport(). Writes the "live" pane; the
    // value consumed elsewhere this frame is frozen() (see below).
    void publish(const Pane& pane, int idx = 0);

    // Seed pane 0 with the window size so frame 1 has a sane rect.
    void initDefaults(const ImVec2& windowSize);

    // The rect every consumer must use.
    //
    // Lazily snapshots the live pane the first time it is called within a given
    // ImGui::GetFrameCount(), so aspect, RTT size, GL viewport, ray math and the gizmo
    // rect all agree within a frame. This matters because EditorState::update() runs
    // 0..N times per rendered frame inside Engine's fixed-timestep loop — a frame-count
    // key is self-synchronising where an explicit begin-frame hook is not.
    //
    // The consequence is that a frame consumes the rect published on the previous
    // frame. Only the final blit trails during a resize: one stretched frame, which
    // corrects itself immediately.
    const Pane& frozen(int idx = 0);

    // Live (this-frame) pane. Only the panel itself should need this.
    const Pane& live(int idx = 0);

    float aspect(int idx = 0);

    // Cursor position relative to the pane's top-left, in pane pixels.
    irr::core::position2di localMouse(int idx = 0);

    // Ray through the cursor. Deliberately reimplements Irrlicht's frustum math against
    // the pane size rather than calling ISceneCollisionManager: its version divides by
    // Driver->getViewPort() and never subtracts the viewport origin, and the driver's
    // viewport state is stale at ImGui-submission time anyway.
    irr::core::line3df rayFromMouse(irr::scene::ICameraSceneNode* camera, int idx = 0);

    // World -> ImGui screen pixels (pane-relative projection plus the pane origin), so
    // results are directly comparable with io.MousePos. Replaces
    // getScreenCoordinatesFrom3DPosition, which divides by the current render target size.
    irr::core::position2di worldToScreen(const irr::core::vector3df& world,
                                         irr::scene::ICameraSceneNode* camera,
                                         int idx = 0);

    // Gate for scene-space tools (brush, painters, camera). Replaces the
    // IsAnyItemHovered() || IsAnyItemActive() idiom, which was only ever a workaround
    // for WantCaptureMouse being permanently true under the fullscreen host window.
    bool acceptsSceneInput(int idx = 0);

    // Point ImTransformControl at the pane. Call between SetDrawlist() and Manipulate().
    // The gizmo is already rect-aware internally; it just needs to be told where the
    // 3D view actually is instead of assuming the whole window.
    void setGizmoRect(int idx = 0);
}
