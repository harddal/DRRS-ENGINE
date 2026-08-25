#include "Editor/EditorViewport.h"

#include <SViewFrustum.h>
#include <matrix4.h>

#include "Engine/Interface/ImTransformControl.h"
#include "Engine/Renderer/RenderManager.h"

namespace
{
    EditorViewport::Pane s_live[EditorViewport::MAX_PANES];
    EditorViewport::Pane s_frozen[EditorViewport::MAX_PANES];

    // ImGui frame the snapshot in s_frozen was taken on. -1 forces a snapshot on the
    // first access. GetFrameCount() starts at 0 and only advances in NewFrame().
    int s_frozenFrame = -1;

    inline bool validIndex(int idx)
    {
        return idx >= 0 && idx < EditorViewport::MAX_PANES;
    }

    // Refresh the per-frame snapshot if this is the first call of the frame.
    void syncFreeze()
    {
        const int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : s_frozenFrame;
        if (frame == s_frozenFrame)
            return;

        for (int i = 0; i < EditorViewport::MAX_PANES; ++i)
            s_frozen[i] = s_live[i];

        s_frozenFrame = frame;
    }
}

namespace EditorViewport
{

void publish(const Pane& pane, int idx)
{
    if (!validIndex(idx))
        return;

    s_live[idx] = pane;
}

void initDefaults(const ImVec2& windowSize)
{
    Pane seed;
    seed.pos     = ImVec2(0.0f, 0.0f);
    seed.size    = windowSize;
    seed.valid   = false;
    seed.hovered = false;

    for (int i = 0; i < MAX_PANES; ++i)
    {
        s_live[i]   = seed;
        s_frozen[i] = seed;
    }

    s_frozenFrame = -1;
}

const Pane& frozen(int idx)
{
    syncFreeze();
    return s_frozen[validIndex(idx) ? idx : 0];
}

const Pane& live(int idx)
{
    return s_live[validIndex(idx) ? idx : 0];
}

float aspect(int idx)
{
    const Pane& p = frozen(idx);
    if (p.size.y <= 0.0f)
        return 1.0f;

    return p.size.x / p.size.y;
}

irr::core::position2di localMouse(int idx)
{
    const Pane& p = frozen(idx);

    // Both operands must come from ImGui. With multi-viewport enabled every ImGui
    // coordinate — GetMousePos() and the pane origin alike — is desktop-absolute,
    // whereas Irrlicht's getCursorControl() reports main-window client pixels.
    // Mixing them offsets picking by the whole window position.
    //
    // Using ImGui also drops a pre-existing inaccuracy: Irrlicht converts to client
    // space with a border width computed once from SM_CXSIZEFRAME, which is neither
    // DWM- nor per-monitor-DPI-correct, so this was already a few pixels off.
    const ImVec2 cursor = ImGui::GetMousePos();

    return irr::core::position2di(static_cast<irr::s32>(cursor.x - p.pos.x),
                                  static_cast<irr::s32>(cursor.y - p.pos.y));
}

irr::core::line3df rayFromMouse(irr::scene::ICameraSceneNode* camera, int idx)
{
    irr::core::line3df ln(0, 0, 0, 0, 0, 0);

    if (!camera)
        camera = RenderManager::Get()->sceneManager()->getActiveCamera();
    if (!camera)
        return ln;

    const Pane& p = frozen(idx);
    if (p.size.x <= 0.0f || p.size.y <= 0.0f)
        return ln;

    const irr::core::position2di pos = localMouse(idx);

    // Same construction as CSceneCollisionManager::getRayFromScreenCoordinates, but
    // normalised against the pane instead of Driver->getViewPort().
    const irr::scene::SViewFrustum* f = camera->getViewFrustum();

    const irr::core::vector3df farLeftUp   = f->getFarLeftUp();
    const irr::core::vector3df lefttoright = f->getFarRightUp()  - farLeftUp;
    const irr::core::vector3df uptodown    = f->getFarLeftDown() - farLeftUp;

    const irr::f32 dx = pos.X / p.size.x;
    const irr::f32 dy = pos.Y / p.size.y;

    if (camera->isOrthogonal())
        ln.start = f->cameraPosition + (lefttoright * (dx - 0.5f)) + (uptodown * (dy - 0.5f));
    else
        ln.start = f->cameraPosition;

    ln.end = farLeftUp + (lefttoright * dx) + (uptodown * dy);

    return ln;
}

irr::core::position2di worldToScreen(const irr::core::vector3df& world,
                                     irr::scene::ICameraSceneNode* camera,
                                     int idx)
{
    if (!camera)
        camera = RenderManager::Get()->sceneManager()->getActiveCamera();
    if (!camera)
        return irr::core::position2di(-10000, -10000);

    const Pane& p = frozen(idx);

    const irr::f32 halfW = p.size.x * 0.5f;
    const irr::f32 halfH = p.size.y * 0.5f;

    irr::core::matrix4 trans = camera->getProjectionMatrix();
    trans *= camera->getViewMatrix();

    irr::f32 transformedPos[4] = { world.X, world.Y, world.Z, 1.0f };
    trans.multiplyWith1x4Matrix(transformedPos);

    // Behind the camera — push far off-panel so distance tests reject it.
    if (transformedPos[3] < 0.0f)
        return irr::core::position2di(-10000, -10000);

    const irr::f32 zDiv = (transformedPos[3] == 0.0f)
        ? 1.0f
        : irr::core::reciprocal(transformedPos[3]);

    // Pane-relative, then offset by the pane origin so the result is directly
    // comparable with io.MousePos / ICursorControl::getPosition().
    return irr::core::position2di(
        static_cast<irr::s32>(p.pos.x) + irr::core::round32(halfW + halfW * (transformedPos[0] * zDiv)),
        static_cast<irr::s32>(p.pos.y) + irr::core::round32(halfH - halfH * (transformedPos[1] * zDiv)));
}

void setGizmoRect(int idx)
{
    const Pane& p = frozen(idx);
    ImTransformControl::SetRect(p.pos.x, p.pos.y, p.size.x, p.size.y);
}

bool acceptsSceneInput(int idx)
{
    const Pane& p = frozen(idx);
    if (!p.valid || !p.hovered)
        return false;

    // An active item means a widget is being dragged (slider, splitter, text field);
    // scene tools must not also act on that drag.
    return !ImGui::IsAnyItemActive();
}

}
