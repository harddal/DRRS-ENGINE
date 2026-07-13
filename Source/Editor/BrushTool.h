#pragma once

#include <cstdint>
#include <vector>

#include <irrlicht.h>

#include "Engine/Brush/BrushData.h"

class SceneInteractionManager;

// Modal sub-tools.  OFF = plain selection: brushes are picked with the normal
// Shift+LMB path and moved with the shared transform gizmo.
enum class BrushToolMode
{
    OFF,
    CREATE,     // drag out a primitive footprint, then set height
    FACE,       // select faces; gizmo push/pull along the face normal
    VERTEX,     // drag vertex handles through the provisional-commit pipeline
    CLIP        // place up to 3 points, preview halves, Enter commits
};

enum class BrushPrimitive
{
    BOX,
    WEDGE,
    CYLINDER
};

// ---------------------------------------------------------------------------
// BrushTool — all interactive CSG brush editing.
//
// Owned by SceneInteractionManager exactly like the painters: update(dt) is
// called every editor frame (early-outs when nothing brush-related is
// happening), draw() renders gizmos/overlays and runs the whole-brush
// transform.  While a drag is in progress BrushManager's heavy rebuilds
// (PhysX cook + selectors) are deferred; commit happens on release through
// quantize -> rebuild -> validate, reverting to the drag-start snapshot when
// the result is invalid.
// ---------------------------------------------------------------------------
class BrushTool
{
public:
    void init(SceneInteractionManager* owner);

    void update(float dt);      // modal interactions (create/clip/vertex)
    void draw();                // overlays + whole-brush gizmo

    bool isActive() const { return m_mode != BrushToolMode::OFF; }
    BrushToolMode mode() const { return m_mode; }
    void setMode(BrushToolMode mode);

    // Esc pressed: returns true if the tool consumed it (cleared clip points
    // or exited a sub-mode) so selection survives.
    bool handleEscape();

    // UI state (brush panel)
    BrushPrimitive primitive = BrushPrimitive::BOX;
    int   cylinderSides = 8;
    float defaultHeight = 2.0f;
    std::string defaultMaterial;

    // ---- face selection (FACE mode) ----
    struct FaceRef
    {
        uint32_t brushId = 0;
        int      faceIndex = -1;
    };
    const std::vector<FaceRef>& selectedFaces() const { return m_selectedFaces; }
    void clearSelectedFaces() { m_selectedFaces.clear(); }

    // Apply material/shader to all selected faces (undo-recorded).
    void applyMaterialToSelectedFaces(const std::string& material);

    // ---- clip (CLIP mode) ----
    int  clipPointCount() const { return static_cast<int>(m_clipPoints.size()); }
    int  clipKeepMode() const { return m_clipKeep; }   // 0 front, 1 back, 2 both
    void commitClip();

    // Subtract the primary selected brush from every brush it overlaps
    // (carver itself is kept; delete it separately if unwanted).
    void carveWithSelected();

private:
    SceneInteractionManager* m_owner = nullptr;
    BrushToolMode m_mode = BrushToolMode::OFF;

    // ---- whole-brush gizmo drag ----
    bool m_dragActive = false;
    irr::core::matrix4   m_dragGizmoMatrix;   // fed back to ImGuizmo while dragging
    irr::core::vector3df m_dragCenter;        // pivot (primary brush AABB center at grab)
    std::vector<Brush>   m_dragStart;         // selected brushes at grab, same order as selection

    // ---- create-drag state ----
    bool m_createDragging = false;
    bool m_createMouseDown = false;           // getMousePressOnce edge state
    bool m_createMouseUp = false;             // getMouseRelease edge state
    irr::core::vector3df m_createAnchor;      // snapped start corner on the base plane
    irr::core::vector3df m_createCurrent;     // snapped opposite corner

    // ---- face mode state ----
    std::vector<FaceRef> m_selectedFaces;
    bool  m_faceMouseDown = false;
    bool  m_faceDragActive = false;
    float m_faceDragOffset = 0.0f;            // accumulated push/pull along the normal
    irr::core::vector3df m_faceDragNormal;
    irr::core::vector3df m_faceDragCentroid;  // gizmo anchor at grab
    irr::core::matrix4   m_faceGizmoMatrix;
    std::vector<Brush>   m_faceDragStart;     // brushes owning selected faces, deduped

    void updateFaceMode();
    void drawFaceMode();
    void beginFaceDrag();
    void applyFaceDrag();
    void commitFaceDrag();

    // ---- vertex mode state ----
    bool m_vertMouseDown = false;
    bool m_vertDragActive = false;
    int  m_vertDragIndex = -1;                // index into the provisional's verts
    uint32_t m_vertBrushId = 0;
    Brush m_vertProvisional;                  // copy being edited; live brush untouched
    irr::core::vector3df m_vertDragPlanePoint;
    bool m_vertProvisionalValid = false;

    void updateVertexMode();
    void drawVertexMode();

    // ---- clip mode state ----
    std::vector<irr::core::vector3df> m_clipPoints;
    int  m_clipKeep = 2;                      // 0 front, 1 back, 2 both
    bool m_clipMouseDown = false;
    bool m_clipTabDown = false;
    bool m_clipEnterDown = false;

    void updateClipMode();
    void drawClipMode();
    bool clipPlaneFromPoints(irr::core::plane3df& outPlane,
                             irr::core::vector3df outPts[3]) const;

    void updateCreateMode();
    void commitCreateDrag();
    // Cursor ray vs scene (surface hit) or vs the horizontal plane at planeY.
    bool cursorWorldPoint(irr::core::vector3df& out, bool lockToPlaneY, float planeY);
    // Cursor ray vs the brush set; false when nothing is hit.
    bool cursorBrushHit(uint32_t& brushId, int& faceIndex, irr::core::vector3df& point);
    irr::core::vector3df snapPoint(const irr::core::vector3df& p) const;

    void drawSelectionOverlays();
    void drawWholeBrushGizmo();
    void drawBrushWireframe(const Brush& brush, irr::video::SColor color);
    void beginGizmoDrag(const std::vector<uint32_t>& selection,
                        const irr::core::vector3df& center);
    void applyGizmoDrag(const std::vector<uint32_t>& selection);
    void commitGizmoDrag(const std::vector<uint32_t>& selection);

    // Transform src's plane points AND its Valve-220 texture frame by the
    // affine map A (texture lock: axes rotate/scale with the geometry, shift
    // refit so a reference point keeps its texel coordinates).
    static BrushFace transformFace(const BrushFace& src, const irr::core::matrix4& A);
};
