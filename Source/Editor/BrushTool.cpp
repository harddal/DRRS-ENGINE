#include "Editor/BrushTool.h"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

#include "Editor/SceneInteractionManager.h"
#include "Engine/Brush/BrushGeometry.h"
#include "Engine/Brush/BrushManager.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Interface/ImTransformControl.h"
#include "Engine/Renderer/RenderManager.h"

using namespace irr;
using namespace core;
using namespace video;

namespace
{
    const SColor kColPrimary   (255, 255, 230,  60);
    const SColor kColSecondary (255, 255, 140,   0);
    const SColor kColFace      (255,  80, 255, 120);
    const SColor kColHandle    (255, 200, 200, 200);
    const SColor kColHandleHot (255, 255, 230,  60);
    const SColor kColValid     (255,  80, 255, 120);
    const SColor kColInvalid   (255, 255,  60,  60);
    const SColor kColClipPoint (255,  60, 220, 255);
    const SColor kColClipFront (255,  90, 160, 255);
    const SColor kColClipBack  (255, 255, 110, 110);

    void drawCross(const vector3df& p, float size, SColor color)
    {
        auto* rm = RenderManager::Get();
        rm->renderLine3DOverlay(Line3D(line3df(p - vector3df(size, 0, 0), p + vector3df(size, 0, 0)), color));
        rm->renderLine3DOverlay(Line3D(line3df(p - vector3df(0, size, 0), p + vector3df(0, size, 0)), color));
        rm->renderLine3DOverlay(Line3D(line3df(p - vector3df(0, 0, size), p + vector3df(0, 0, size)), color));
    }
}

void BrushTool::init(SceneInteractionManager* owner)
{
    m_owner = owner;
    m_mode = BrushToolMode::OFF;
    m_dragActive = false;
    m_createDragging = false;
    m_faceDragActive = false;
    m_vertDragActive = false;
    m_selectedFaces.clear();
    m_clipPoints.clear();
}

void BrushTool::setMode(BrushToolMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    // Never leave heavy rebuilds deferred across a mode change
    if (BrushManager::Get())
        BrushManager::Get()->setDeferHeavyRebuilds(false);
    m_dragActive = false;
    m_createDragging = false;
    m_faceDragActive = false;
    m_vertDragActive = false;
    m_selectedFaces.clear();
    m_clipPoints.clear();
}

bool BrushTool::handleEscape()
{
    if (m_mode == BrushToolMode::CLIP && !m_clipPoints.empty())
    {
        m_clipPoints.clear();
        return true;
    }
    if (m_mode != BrushToolMode::OFF)
    {
        setMode(BrushToolMode::OFF);
        return true;
    }
    return false;
}

void BrushTool::update(float dt)
{
    (void)dt;

    if (m_mode == BrushToolMode::OFF || !BrushManager::Get())
        return;
    if (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive())
        return;

    switch (m_mode)
    {
    case BrushToolMode::CREATE: updateCreateMode(); break;
    case BrushToolMode::FACE:   updateFaceMode();   break;
    case BrushToolMode::VERTEX: updateVertexMode(); break;
    case BrushToolMode::CLIP:   updateClipMode();   break;
    default: break;
    }
}

void BrushTool::draw()
{
    if (!m_owner || !BrushManager::Get())
        return;

    drawSelectionOverlays();

    switch (m_mode)
    {
    case BrushToolMode::OFF:
        // The whole-brush gizmo only runs in plain selection mode — modal
        // sub-tools own the mouse
        drawWholeBrushGizmo();
        break;

    case BrushToolMode::CREATE:
        if (m_createDragging)
        {
            aabbox3df box;
            box.reset(m_createAnchor);
            box.addInternalPoint(m_createCurrent);
            box.addInternalPoint(m_createAnchor + vector3df(0, defaultHeight, 0));
            RenderManager::Get()->renderBox3DOverlay(box, kColValid);
        }
        break;

    case BrushToolMode::FACE:   drawFaceMode();   break;
    case BrushToolMode::VERTEX: drawVertexMode(); break;
    case BrushToolMode::CLIP:   drawClipMode();   break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

vector3df BrushTool::snapPoint(const vector3df& p) const
{
    const auto cfg = m_owner->getConfiguration();
    if (!cfg.useSnap)
        return BrushGeometry::quantize(p);

    auto snapAxis = [](float v, float s)
    {
        return (s > 1e-5f) ? std::floor(v / s + 0.5f) * s : v;
    };
    return vector3df(
        snapAxis(p.X, cfg.snapX),
        snapAxis(p.Y, cfg.snapY),
        snapAxis(p.Z, cfg.snapZ));
}

bool BrushTool::cursorWorldPoint(vector3df& out, bool lockToPlaneY, float planeY)
{
    auto* rm   = RenderManager::Get();
    auto* smgr = rm->sceneManager();
    auto* cam  = smgr->getActiveCamera();
    if (!cam)
        return false;

    const line3df ray = smgr->getSceneCollisionManager()->getRayFromScreenCoordinates(
        rm->device()->getCursorControl()->getPosition(), cam);

    if (lockToPlaneY)
    {
        const plane3df plane(vector3df(0, planeY, 0), vector3df(0, 1, 0));
        return plane.getIntersectionWithLimitedLine(ray.start, ray.end, out);
    }

    // Prefer a real surface hit; fall back to the y=0 ground plane
    auto hit = rm->raycastWorldPosition(ray.start, ray.end, /*excludeDebugNodes=*/true);
    if (hit.hit)
    {
        out = hit.point;
        return true;
    }
    const plane3df ground(vector3df(0, 0, 0), vector3df(0, 1, 0));
    return ground.getIntersectionWithLimitedLine(ray.start, ray.end, out);
}

bool BrushTool::cursorBrushHit(uint32_t& brushId, int& faceIndex, vector3df& point)
{
    auto* rm   = RenderManager::Get();
    auto* smgr = rm->sceneManager();
    auto* cam  = smgr->getActiveCamera();
    if (!cam)
        return false;

    const line3df ray = smgr->getSceneCollisionManager()->getRayFromScreenCoordinates(
        rm->device()->getCursorControl()->getPosition(), cam);

    vector3df dir = ray.end - ray.start;
    const float len = dir.getLength();
    if (len < 0.001f)
        return false;
    dir /= len;

    BrushManager::BrushRayHit hit;
    if (!BrushManager::Get()->raycastBrushes(ray.start, dir, len, hit))
        return false;

    brushId = hit.brushId;
    faceIndex = hit.faceIndex;
    point = hit.point;
    return true;
}

void BrushTool::drawBrushWireframe(const Brush& brush, SColor color)
{
    auto* rm = RenderManager::Get();
    for (const auto& face : brush.faces)
    {
        for (size_t v = 0; v < face.loop.size(); v++)
        {
            const vector3df& a = brush.verts[face.loop[v]];
            const vector3df& b = brush.verts[face.loop[(v + 1) % face.loop.size()]];
            rm->renderLine3DOverlay(Line3D(line3df(a, b), color));
        }
    }
}

void BrushTool::drawSelectionOverlays()
{
    const auto& selection = m_owner->getSelectedBrushes();
    for (size_t i = 0; i < selection.size(); i++)
    {
        const Brush* brush = BrushManager::Get()->getBrush(selection[i]);
        if (!brush || !brush->geometryValid)
            continue;
        drawBrushWireframe(*brush, (i == 0) ? kColPrimary : kColSecondary);
    }
}

// ---------------------------------------------------------------------------
// Whole-brush transform gizmo (selection mode)
// ---------------------------------------------------------------------------

void BrushTool::drawWholeBrushGizmo()
{
    const auto& selection = m_owner->getSelectedBrushes();
    if (selection.empty())
        return;

    Brush* primary = BrushManager::Get()->getBrush(selection[0]);
    if (!primary || !primary->geometryValid)
        return;

    const auto cfg = m_owner->getConfiguration();
    float snap[3] = { cfg.snapX, cfg.snapY, cfg.snapZ };

    matrix4 transform;
    if (m_dragActive)
        transform = m_dragGizmoMatrix;
    else
        transform.setTranslation(primary->bounds.getCenter());

    const vector3df preDragCenter = primary->bounds.getCenter();

    ImGuiIO& io = ImGui::GetIO();
    ImTransformControl::SetDrawlist();
    ImTransformControl::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImTransformControl::Manipulate(
        RenderManager::Get()->sceneManager()->getActiveCamera()->getViewMatrix().pointer(),
        RenderManager::Get()->sceneManager()->getActiveCamera()->getProjectionMatrix().pointer(),
        m_owner->getWidgetToolMode(), m_owner->getWidgetCoordMode(),
        transform.pointer(), nullptr, cfg.useSnap ? &snap[0] : nullptr);

    if (ImTransformControl::IsUsing())
    {
        if (!m_dragActive)
            beginGizmoDrag(selection, preDragCenter);

        m_dragGizmoMatrix = transform;
        applyGizmoDrag(selection);
    }
    else if (m_dragActive)
    {
        commitGizmoDrag(selection);
    }
}

void BrushTool::beginGizmoDrag(const std::vector<uint32_t>& selection,
                               const vector3df& center)
{
    m_dragActive = true;
    m_dragCenter = center;

    m_dragStart.clear();
    UndoEntry entry;
    for (uint32_t id : selection)
    {
        Brush* brush = BrushManager::Get()->getBrush(id);
        m_dragStart.push_back(brush ? *brush : Brush());

        BrushSnapshot snap;
        snap.id = id;
        snap.existed = (brush != nullptr);
        if (brush)
            snap.data = *brush;
        entry.brushes.push_back(std::move(snap));
    }
    m_owner->pushUndoEntry(std::move(entry));

    BrushManager::Get()->setDeferHeavyRebuilds(true);
}

void BrushTool::applyGizmoDrag(const std::vector<uint32_t>& selection)
{
    // Affine map from drag-start space: pivot to origin, then the accumulated
    // gizmo matrix (row-vector convention: left operand applies first).
    matrix4 toPivot;
    toPivot.setTranslation(-m_dragCenter);
    const matrix4 A = toPivot * m_dragGizmoMatrix;

    for (size_t i = 0; i < selection.size() && i < m_dragStart.size(); i++)
    {
        Brush* live = BrushManager::Get()->getBrush(selection[i]);
        const Brush& src = m_dragStart[i];
        if (!live || src.faces.empty())
            continue;

        live->faces.clear();
        live->faces.reserve(src.faces.size());
        for (const auto& face : src.faces)
            live->faces.push_back(transformFace(face, A));

        BrushManager::Get()->markBrushDirty(selection[i]);
    }
}

void BrushTool::commitGizmoDrag(const std::vector<uint32_t>& selection)
{
    for (size_t i = 0; i < selection.size() && i < m_dragStart.size(); i++)
    {
        Brush* live = BrushManager::Get()->getBrush(selection[i]);
        if (!live)
            continue;

        Brush candidate = *live;
        BrushGeometry::quantizeFaces(candidate);

        if (!BrushManager::Get()->replaceBrush(selection[i], candidate))
        {
            // Quantization degenerated the plane set — revert the whole drag
            spdlog::warn("BrushTool: transform produced an invalid brush {}, reverting", selection[i]);
            BrushManager::Get()->replaceBrush(selection[i], m_dragStart[i]);
        }
    }

    BrushManager::Get()->setDeferHeavyRebuilds(false);
    m_dragActive = false;
    m_dragStart.clear();
}

BrushFace BrushTool::transformFace(const BrushFace& src, const matrix4& A)
{
    BrushFace dst = src;
    dst.loop.clear();

    for (int i = 0; i < 3; i++)
    {
        vector3df p = src.planePoints[i];
        A.transformVect(p);
        dst.planePoints[i] = p;
    }

    // Texture lock: transform the UV axes by the linear part, fold their
    // length change into the scale, and refit the shifts so a reference point
    // keeps its texel coordinates.
    vector3df u = src.uAxis;
    vector3df v = src.vAxis;
    A.rotateVect(u);
    A.rotateVect(v);

    const float lu = u.getLength();
    const float lv = v.getLength();
    const float su = (std::fabs(src.scaleU) > 1e-6f) ? src.scaleU : 1.0f;
    const float sv = (std::fabs(src.scaleV) > 1e-6f) ? src.scaleV : 1.0f;

    if (lu > 1e-6f && lv > 1e-6f)
    {
        const vector3df r = src.planePoints[0];
        vector3df rp = r;
        A.transformVect(rp);

        // Tile coordinates of the reference point: u = (p.axis + shift)/scale
        const float texU = (r.dotProduct(src.uAxis) + src.shiftU) / su;
        const float texV = (r.dotProduct(src.vAxis) + src.shiftV) / sv;

        dst.uAxis  = u / lu;
        dst.vAxis  = v / lv;
        dst.scaleU = su * lu;
        dst.scaleV = sv * lv;
        dst.shiftU = texU * dst.scaleU - rp.dotProduct(dst.uAxis);
        dst.shiftV = texV * dst.scaleV - rp.dotProduct(dst.vAxis);
    }

    return dst;
}

// ---------------------------------------------------------------------------
// CREATE mode — drag out a footprint, release extrudes by defaultHeight
// ---------------------------------------------------------------------------

void BrushTool::updateCreateMode()
{
    auto* input = InputManager::Get();

    if (!m_createDragging)
    {
        if (input->getMousePressOnce(0, &m_createMouseDown))
        {
            vector3df p;
            if (cursorWorldPoint(p, /*lockToPlaneY=*/false, 0.0f))
            {
                m_createAnchor = snapPoint(p);
                m_createCurrent = m_createAnchor;
                m_createDragging = true;
            }
        }
        return;
    }

    // Dragging: track the opposite corner on the anchor's base plane
    vector3df p;
    if (cursorWorldPoint(p, /*lockToPlaneY=*/true, m_createAnchor.Y))
        m_createCurrent = snapPoint(p);

    if (input->getMouseRelease(0, &m_createMouseUp))
        commitCreateDrag();
}

void BrushTool::commitCreateDrag()
{
    m_createDragging = false;

    const float minExtent = BrushGeometry::GRID_QUANTUM * 4.0f;
    aabbox3df box;
    box.reset(m_createAnchor);
    box.addInternalPoint(m_createCurrent);
    box.addInternalPoint(m_createAnchor + vector3df(0, defaultHeight, 0));

    const vector3df extent = box.getExtent();
    if (extent.X < minExtent || extent.Z < minExtent || extent.Y < minExtent)
        return;     // degenerate footprint — ignore the click

    Brush brush;
    switch (primitive)
    {
    case BrushPrimitive::WEDGE:
        brush = BrushGeometry::makeWedge(box, defaultMaterial);
        break;
    case BrushPrimitive::CYLINDER:
        brush = BrushGeometry::makeCylinder(box, cylinderSides, defaultMaterial);
        break;
    case BrushPrimitive::BOX:
    default:
        brush = BrushGeometry::makeBox(box, defaultMaterial);
        break;
    }

    const uint32_t id = BrushManager::Get()->addBrush(brush);
    if (id == 0)
    {
        spdlog::warn("BrushTool: create produced an invalid brush, discarded");
        return;
    }

    // Undo: the brush did not exist before this operation
    UndoEntry entry;
    BrushSnapshot snap;
    snap.id = id;
    snap.existed = false;
    entry.brushes.push_back(std::move(snap));
    m_owner->pushUndoEntry(std::move(entry));

    m_owner->setSelectedBrush(id);
}

// ---------------------------------------------------------------------------
// FACE mode — click faces, push/pull along their normals
// ---------------------------------------------------------------------------

void BrushTool::updateFaceMode()
{
    if (m_faceDragActive)
        return;     // the gizmo (in draw) owns the mouse

    // Don't treat gizmo grabs as selection clicks
    if (ImTransformControl::IsUsing() || ImTransformControl::IsOver())
        return;

    auto* input = InputManager::Get();
    if (!input->getMousePressOnce(0, &m_faceMouseDown))
        return;

    uint32_t brushId;
    int faceIndex;
    vector3df point;
    const bool ctrl = input->isKeyPressed(KEYBOARD_KEY::KEY_LCONTROL);

    if (cursorBrushHit(brushId, faceIndex, point))
    {
        if (ctrl)
        {
            for (auto it = m_selectedFaces.begin(); it != m_selectedFaces.end(); ++it)
            {
                if (it->brushId == brushId && it->faceIndex == faceIndex)
                {
                    m_selectedFaces.erase(it);
                    return;
                }
            }
            m_selectedFaces.push_back({ brushId, faceIndex });
        }
        else
        {
            m_selectedFaces.clear();
            m_selectedFaces.push_back({ brushId, faceIndex });
        }
    }
    else if (!ctrl)
    {
        m_selectedFaces.clear();
    }
}

void BrushTool::drawFaceMode()
{
    // Drop refs whose brush/face vanished (deleted, pruned by a rebuild)
    for (auto it = m_selectedFaces.begin(); it != m_selectedFaces.end(); )
    {
        Brush* b = BrushManager::Get()->getBrush(it->brushId);
        if (!b || it->faceIndex < 0 || it->faceIndex >= static_cast<int>(b->faces.size()))
            it = m_selectedFaces.erase(it);
        else
            ++it;
    }
    if (m_selectedFaces.empty())
        return;

    auto* rm = RenderManager::Get();

    // Highlight selected face loops + centroid of the primary face
    vector3df primaryCentroid(0, 0, 0);
    vector3df primaryNormal(0, 1, 0);
    bool first = true;

    for (const auto& ref : m_selectedFaces)
    {
        Brush* brush = BrushManager::Get()->getBrush(ref.brushId);
        const BrushFace& face = brush->faces[ref.faceIndex];
        if (face.loop.size() < 3)
            continue;

        vector3df centroid(0, 0, 0);
        for (size_t v = 0; v < face.loop.size(); v++)
        {
            const vector3df& a = brush->verts[face.loop[v]];
            const vector3df& b = brush->verts[face.loop[(v + 1) % face.loop.size()]];
            rm->renderLine3DOverlay(Line3D(line3df(a, b), kColFace));
            centroid += a;
        }
        centroid /= static_cast<f32>(face.loop.size());
        rm->renderLine3DOverlay(Line3D(line3df(centroid, centroid + face.plane.Normal * 0.5f), kColFace));

        if (first)
        {
            primaryCentroid = centroid;
            primaryNormal = face.plane.Normal;
            first = false;
        }
    }

    // ---- push/pull gizmo (translate, constrained to the face normal) ----
    const auto cfg = m_owner->getConfiguration();
    float snap[3] = { cfg.snapX, cfg.snapY, cfg.snapZ };

    matrix4 transform;
    if (m_faceDragActive)
        transform = m_faceGizmoMatrix;
    else
        transform.setTranslation(primaryCentroid);

    ImGuiIO& io = ImGui::GetIO();
    ImTransformControl::SetDrawlist();
    ImTransformControl::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImTransformControl::Manipulate(
        RenderManager::Get()->sceneManager()->getActiveCamera()->getViewMatrix().pointer(),
        RenderManager::Get()->sceneManager()->getActiveCamera()->getProjectionMatrix().pointer(),
        ImTransformControl::TRANSLATE, ImTransformControl::WORLD,
        transform.pointer(), nullptr, cfg.useSnap ? &snap[0] : nullptr);

    if (ImTransformControl::IsUsing())
    {
        if (!m_faceDragActive)
        {
            m_faceDragCentroid = primaryCentroid;
            m_faceDragNormal = primaryNormal;
            beginFaceDrag();
        }

        // Constrain the gizmo translation to the primary face normal
        const vector3df delta = transform.getTranslation() - m_faceDragCentroid;
        m_faceDragOffset = delta.dotProduct(m_faceDragNormal);

        matrix4 constrained;
        constrained.setTranslation(m_faceDragCentroid + m_faceDragNormal * m_faceDragOffset);
        m_faceGizmoMatrix = constrained;

        applyFaceDrag();
    }
    else if (m_faceDragActive)
    {
        commitFaceDrag();
    }
}

void BrushTool::beginFaceDrag()
{
    m_faceDragActive = true;
    m_faceDragOffset = 0.0f;

    // Snapshot each owning brush once (a brush may own several selected faces)
    m_faceDragStart.clear();
    UndoEntry entry;
    for (const auto& ref : m_selectedFaces)
    {
        bool known = false;
        for (const auto& b : m_faceDragStart)
            if (b.id == ref.brushId) { known = true; break; }
        if (known)
            continue;

        Brush* brush = BrushManager::Get()->getBrush(ref.brushId);
        if (!brush)
            continue;
        m_faceDragStart.push_back(*brush);

        BrushSnapshot snap;
        snap.id = ref.brushId;
        snap.existed = true;
        snap.data = *brush;
        entry.brushes.push_back(std::move(snap));
    }
    m_owner->pushUndoEntry(std::move(entry));

    BrushManager::Get()->setDeferHeavyRebuilds(true);
}

void BrushTool::applyFaceDrag()
{
    // Rebuild every touched brush from its drag-start copy, offsetting each
    // selected face along ITS OWN normal by the shared drag distance.
    for (const auto& start : m_faceDragStart)
    {
        Brush* live = BrushManager::Get()->getBrush(start.id);
        if (!live)
            continue;

        live->faces = start.faces;
        for (const auto& ref : m_selectedFaces)
        {
            if (ref.brushId != start.id ||
                ref.faceIndex < 0 || ref.faceIndex >= static_cast<int>(start.faces.size()))
                continue;

            const BrushFace& src = start.faces[ref.faceIndex];
            BrushFace& dst = live->faces[ref.faceIndex];
            const vector3df offset = src.plane.Normal * m_faceDragOffset;
            for (int i = 0; i < 3; i++)
                dst.planePoints[i] = src.planePoints[i] + offset;
            // Plane offset is along the normal, so the Valve-220 UV frame is
            // invariant — no shift refit needed.
        }
        BrushManager::Get()->markBrushDirty(start.id);
    }
}

void BrushTool::commitFaceDrag()
{
    for (const auto& start : m_faceDragStart)
    {
        Brush* live = BrushManager::Get()->getBrush(start.id);
        if (!live)
            continue;

        Brush candidate = *live;
        BrushGeometry::quantizeFaces(candidate);

        if (!BrushManager::Get()->replaceBrush(start.id, candidate))
        {
            spdlog::warn("BrushTool: face push/pull invalidated brush {}, reverting", start.id);
            BrushManager::Get()->replaceBrush(start.id, start);
        }
    }

    // Face indices may have shifted if the rebuild pruned faces — revalidate
    for (auto it = m_selectedFaces.begin(); it != m_selectedFaces.end(); )
    {
        Brush* b = BrushManager::Get()->getBrush(it->brushId);
        if (!b || it->faceIndex >= static_cast<int>(b->faces.size()))
            it = m_selectedFaces.erase(it);
        else
            ++it;
    }

    BrushManager::Get()->setDeferHeavyRebuilds(false);
    m_faceDragActive = false;
    m_faceDragStart.clear();
}

void BrushTool::applyMaterialToSelectedFaces(const std::string& material)
{
    if (m_selectedFaces.empty() || !BrushManager::Get())
        return;

    // Undo: one snapshot per touched brush
    UndoEntry entry;
    for (const auto& ref : m_selectedFaces)
    {
        bool known = false;
        for (const auto& s : entry.brushes)
            if (s.id == ref.brushId) { known = true; break; }
        if (known)
            continue;
        Brush* brush = BrushManager::Get()->getBrush(ref.brushId);
        if (!brush)
            continue;
        BrushSnapshot snap;
        snap.id = ref.brushId;
        snap.existed = true;
        snap.data = *brush;
        entry.brushes.push_back(std::move(snap));
    }
    m_owner->pushUndoEntry(std::move(entry));

    for (const auto& ref : m_selectedFaces)
    {
        Brush* brush = BrushManager::Get()->getBrush(ref.brushId);
        if (!brush || ref.faceIndex < 0 || ref.faceIndex >= static_cast<int>(brush->faces.size()))
            continue;
        brush->faces[ref.faceIndex].materialName = material;
        BrushManager::Get()->markBrushDirty(ref.brushId);
    }
}

// ---------------------------------------------------------------------------
// VERTEX mode — drag corner handles through the provisional-commit pipeline.
// The live brush is never touched during the drag; the provisional copy is
// previewed as a wireframe and committed (refit -> quantize -> rebuild) or
// discarded on release.
// ---------------------------------------------------------------------------

void BrushTool::updateVertexMode()
{
    auto* input = InputManager::Get();
    auto* rm    = RenderManager::Get();
    auto* smgr  = rm->sceneManager();
    auto* cam   = smgr->getActiveCamera();
    if (!cam)
        return;

    const auto& selection = m_owner->getSelectedBrushes();

    if (m_vertDragActive)
    {
        // Drag the vertex in the camera-facing plane through its grab position
        const line3df ray = smgr->getSceneCollisionManager()->getRayFromScreenCoordinates(
            rm->device()->getCursorControl()->getPosition(), cam);

        vector3df camDir = ray.end - ray.start;
        camDir.normalize();
        const plane3df dragPlane(m_vertDragPlanePoint, -camDir);

        vector3df p;
        if (dragPlane.getIntersectionWithLimitedLine(ray.start, ray.end, p) &&
            m_vertDragIndex >= 0 &&
            m_vertDragIndex < static_cast<int>(m_vertProvisional.verts.size()))
        {
            m_vertProvisional.verts[m_vertDragIndex] = snapPoint(p);

            // Tentative validity check on a scratch copy (colors the preview)
            Brush scratch = m_vertProvisional;
            m_vertProvisionalValid =
                BrushGeometry::refitFacePlanes(scratch) && BrushGeometry::rebuild(scratch);
        }

        if (input->getMouseRelease(0, &m_vertMouseDown))
        {
            m_vertDragActive = false;

            Brush candidate = m_vertProvisional;
            if (BrushGeometry::refitFacePlanes(candidate))
            {
                Brush* live = BrushManager::Get()->getBrush(m_vertBrushId);
                if (live)
                {
                    // Undo: the live brush still holds the pre-drag state
                    UndoEntry entry;
                    BrushSnapshot snap;
                    snap.id = m_vertBrushId;
                    snap.existed = true;
                    snap.data = *live;
                    entry.brushes.push_back(std::move(snap));

                    if (BrushManager::Get()->replaceBrush(m_vertBrushId, candidate))
                        m_owner->pushUndoEntry(std::move(entry));
                    else
                        spdlog::info("BrushTool: vertex edit rejected (non-convex result)");
                }
            }
        }
        return;
    }

    // Not dragging: LMB near a handle starts a drag
    if (selection.empty())
    {
        // Allow picking a brush to edit
        if (input->getMousePressOnce(0, &m_vertMouseDown))
        {
            uint32_t brushId;
            int faceIndex;
            vector3df point;
            if (cursorBrushHit(brushId, faceIndex, point))
                m_owner->setSelectedBrush(brushId);
        }
        return;
    }

    Brush* brush = BrushManager::Get()->getBrush(selection[0]);
    if (!brush || !brush->geometryValid)
        return;

    if (input->getMousePressOnce(0, &m_vertMouseDown))
    {
        const position2di cursor = rm->device()->getCursorControl()->getPosition();
        auto* coll = smgr->getSceneCollisionManager();

        int   best = -1;
        float bestDistSq = 12.0f * 12.0f;   // pixel radius
        for (size_t v = 0; v < brush->verts.size(); v++)
        {
            const position2di s = coll->getScreenCoordinatesFrom3DPosition(brush->verts[v], cam);
            const float dx = static_cast<float>(s.X - cursor.X);
            const float dy = static_cast<float>(s.Y - cursor.Y);
            const float d = dx * dx + dy * dy;
            if (d < bestDistSq)
            {
                bestDistSq = d;
                best = static_cast<int>(v);
            }
        }

        if (best >= 0)
        {
            m_vertDragActive = true;
            m_vertDragIndex = best;
            m_vertBrushId = selection[0];
            m_vertProvisional = *brush;
            m_vertDragPlanePoint = brush->verts[best];
            m_vertProvisionalValid = true;
        }
        else
        {
            // Clicking elsewhere re-targets another brush
            uint32_t brushId;
            int faceIndex;
            vector3df point;
            if (cursorBrushHit(brushId, faceIndex, point))
                m_owner->setSelectedBrush(brushId);
        }
    }
}

void BrushTool::drawVertexMode()
{
    const auto& selection = m_owner->getSelectedBrushes();
    if (selection.empty())
        return;

    Brush* brush = BrushManager::Get()->getBrush(selection[0]);
    if (!brush || !brush->geometryValid)
        return;

    const float handleSize = 0.08f;

    if (m_vertDragActive)
    {
        // Preview the provisional shape; green = will commit, red = rejected
        drawBrushWireframe(m_vertProvisional, m_vertProvisionalValid ? kColValid : kColInvalid);
        for (size_t v = 0; v < m_vertProvisional.verts.size(); v++)
        {
            drawCross(m_vertProvisional.verts[v], handleSize,
                (static_cast<int>(v) == m_vertDragIndex) ? kColHandleHot : kColHandle);
        }
        return;
    }

    for (const auto& vert : brush->verts)
        drawCross(vert, handleSize, kColHandle);
}

// ---------------------------------------------------------------------------
// CLIP mode — place 2-3 points, Tab flips the kept side, Enter commits
// ---------------------------------------------------------------------------

void BrushTool::updateClipMode()
{
    auto* input = InputManager::Get();

    if (input->getMousePressOnce(0, &m_clipMouseDown))
    {
        vector3df p;
        if (cursorWorldPoint(p, /*lockToPlaneY=*/false, 0.0f))
        {
            p = snapPoint(p);
            if (m_clipPoints.size() < 3)
                m_clipPoints.push_back(p);
            else
                m_clipPoints.back() = p;
        }
    }

    if (input->getKeyPressOnce(KEYBOARD_KEY::KEY_TAB, &m_clipTabDown))
        m_clipKeep = (m_clipKeep + 1) % 3;

    if (input->getKeyPressOnce(KEYBOARD_KEY::KEY_RETURN, &m_clipEnterDown))
        commitClip();
}

bool BrushTool::clipPlaneFromPoints(plane3df& outPlane, vector3df outPts[3]) const
{
    if (m_clipPoints.size() < 2)
        return false;

    outPts[0] = m_clipPoints[0];
    outPts[1] = m_clipPoints[1];
    if (m_clipPoints.size() >= 3)
        outPts[2] = m_clipPoints[2];
    else
        outPts[2] = m_clipPoints[0] + vector3df(0, 1, 0);   // vertical plane

    const plane3df plane(outPts[0], outPts[1], outPts[2]);
    if (plane.Normal.getLengthSQ() < 0.5f)
        return false;   // collinear points

    outPlane = plane;
    return true;
}

void BrushTool::commitClip()
{
    plane3df plane;
    vector3df pts[3];
    if (!clipPlaneFromPoints(plane, pts))
        return;

    const auto selection = m_owner->getSelectedBrushes();   // copy — we mutate
    if (selection.empty())
        return;

    UndoEntry entry;
    std::vector<uint32_t> createdIds;

    for (uint32_t id : selection)
    {
        Brush* live = BrushManager::Get()->getBrush(id);
        if (!live || !live->geometryValid)
            continue;

        BrushSnapshot snap;
        snap.id = id;
        snap.existed = true;
        snap.data = *live;
        entry.brushes.push_back(std::move(snap));

        // The cut face reuses the exact user-placed (snapped) points and
        // inherits the brush's first face material
        BrushFace tmpl;
        tmpl.planePoints[0] = pts[0];
        tmpl.planePoints[1] = pts[1];
        tmpl.planePoints[2] = pts[2];
        tmpl.plane = plane;
        if (!live->faces.empty())
        {
            tmpl.materialName = live->faces[0].materialName;
            tmpl.shaderName   = live->faces[0].shaderName;
        }
        BrushGeometry::initFaceUV(tmpl);

        Brush front, back;
        BrushGeometry::clipBrush(*live, plane, &front, &back, &tmpl);

        const bool keepFront = (m_clipKeep == 0 || m_clipKeep == 2) && front.geometryValid;
        const bool keepBack  = (m_clipKeep == 1 || m_clipKeep == 2) && back.geometryValid;

        if (keepFront && keepBack)
        {
            BrushManager::Get()->replaceBrush(id, front);
            back.id = 0;
            back.name.clear();
            const uint32_t newId = BrushManager::Get()->addBrush(back);
            if (newId != 0)
                createdIds.push_back(newId);
        }
        else if (keepFront)
        {
            BrushManager::Get()->replaceBrush(id, front);
        }
        else if (keepBack)
        {
            BrushManager::Get()->replaceBrush(id, back);
        }
        // Neither side kept (plane missed, or kept side empty): leave unchanged
    }

    for (uint32_t newId : createdIds)
    {
        BrushSnapshot snap;
        snap.id = newId;
        snap.existed = false;
        entry.brushes.push_back(std::move(snap));
    }
    m_owner->pushUndoEntry(std::move(entry));

    m_clipPoints.clear();
}

void BrushTool::drawClipMode()
{
    for (const auto& p : m_clipPoints)
        drawCross(p, 0.12f, kColClipPoint);

    plane3df plane;
    vector3df pts[3];
    if (!clipPlaneFromPoints(plane, pts))
        return;

    auto* rm = RenderManager::Get();

    // Plane rectangle outline around the points' centroid
    {
        const vector3df c = (pts[0] + pts[1] + pts[2]) / 3.0f;
        vector3df t = plane.Normal.crossProduct(
            (std::fabs(plane.Normal.Y) < 0.9f) ? vector3df(0, 1, 0) : vector3df(1, 0, 0));
        t.normalize();
        const vector3df b = plane.Normal.crossProduct(t);

        float half = 3.0f;
        if (!m_owner->getSelectedBrushes().empty())
        {
            const Brush* primary = BrushManager::Get()->getBrush(m_owner->getSelectedBrushes()[0]);
            if (primary && primary->geometryValid)
                half = primary->bounds.getExtent().getLength() * 0.75f;
        }

        const vector3df c0 = c + t * half + b * half;
        const vector3df c1 = c - t * half + b * half;
        const vector3df c2 = c - t * half - b * half;
        const vector3df c3 = c + t * half - b * half;
        rm->renderLine3DOverlay(Line3D(line3df(c0, c1), kColClipPoint));
        rm->renderLine3DOverlay(Line3D(line3df(c1, c2), kColClipPoint));
        rm->renderLine3DOverlay(Line3D(line3df(c2, c3), kColClipPoint));
        rm->renderLine3DOverlay(Line3D(line3df(c3, c0), kColClipPoint));
    }

    // Preview both halves of each selected brush: blue = front, red = back;
    // kept side(s) rendered, dropped side dimmed out entirely
    for (uint32_t id : m_owner->getSelectedBrushes())
    {
        const Brush* live = BrushManager::Get()->getBrush(id);
        if (!live || !live->geometryValid)
            continue;

        Brush front, back;
        BrushGeometry::clipBrush(*live, plane, &front, &back);

        if (front.geometryValid && (m_clipKeep == 0 || m_clipKeep == 2))
            drawBrushWireframe(front, kColClipFront);
        if (back.geometryValid && (m_clipKeep == 1 || m_clipKeep == 2))
            drawBrushWireframe(back, kColClipBack);
    }
}

// ---------------------------------------------------------------------------
// Carve — subtract the primary selected brush from everything it overlaps
// ---------------------------------------------------------------------------

void BrushTool::carveWithSelected()
{
    const auto& selection = m_owner->getSelectedBrushes();
    if (selection.empty() || !BrushManager::Get())
        return;

    Brush* carver = BrushManager::Get()->getBrush(selection[0]);
    if (!carver || !carver->geometryValid)
        return;

    // Collect targets first — carving mutates the brush list
    std::vector<uint32_t> targets;
    for (const auto& b : BrushManager::Get()->getAllBrushes())
    {
        if (b.id == carver->id || !b.geometryValid)
            continue;
        if (b.bounds.intersectsWithBox(carver->bounds))
            targets.push_back(b.id);
    }
    if (targets.empty())
        return;

    const Brush carverCopy = *carver;   // carver pointer may dangle after edits

    UndoEntry entry;
    std::vector<uint32_t> createdIds;

    for (uint32_t id : targets)
    {
        Brush* live = BrushManager::Get()->getBrush(id);
        if (!live)
            continue;

        BrushSnapshot snap;
        snap.id = id;
        snap.existed = true;
        snap.data = *live;
        entry.brushes.push_back(std::move(snap));

        std::vector<Brush> fragments = BrushGeometry::carve(*live, carverCopy);

        if (fragments.empty())
        {
            // Entirely inside the carver
            BrushManager::Get()->removeBrush(id);
            continue;
        }

        BrushManager::Get()->replaceBrush(id, fragments[0]);
        for (size_t f = 1; f < fragments.size(); f++)
        {
            fragments[f].id = 0;
            fragments[f].name.clear();
            const uint32_t newId = BrushManager::Get()->addBrush(fragments[f]);
            if (newId != 0)
                createdIds.push_back(newId);
        }
    }

    for (uint32_t newId : createdIds)
    {
        BrushSnapshot snap;
        snap.id = newId;
        snap.existed = false;
        entry.brushes.push_back(std::move(snap));
    }
    m_owner->pushUndoEntry(std::move(entry));
}
