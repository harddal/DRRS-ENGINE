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
    CLIP,       // place up to 3 points, preview halves, Enter commits
    PAINT,      // click/drag faces to apply defaultMaterial; Ctrl+click samples
    STAMP,      // outline a shape on a brush face, Enter carves it through
    SCULPT      // subdivide a quad face into a displacement and sculpt it
};

// NOTE: the brush panel's primNames[] combo maps straight onto this enum by
// index, so the two must be edited together.  Nothing serializes BrushPrimitive,
// so reordering is otherwise free.
enum class BrushPrimitive
{
    BOX,
    WEDGE,
    CYLINDER,
    CONE,        // tapered prism: apex, frustum or plain prism via topScale
    STAIRS,      // N solid step brushes, optionally plus a player-clip ramp
    ARCH,        // N voussoir brushes swept around an arc
    GOTHIC_ARCH  // two arcs meeting at a point, 2*N voussoirs
};

// Axis of revolution for CYLINDER / CONE.  No AUTO: a footprint drag says
// nothing about which way a column should lie, so the pick stays explicit.
enum class BrushRevolveChoice
{
    PLUS_X, MINUS_X, PLUS_Y, MINUS_Y, PLUS_Z, MINUS_Z
};

// Horizontal axis pick for the parametric primitives.  AUTO is resolved from
// the footprint drag at commit time, so it never reaches BrushGeometry.
enum class BrushAxisChoice
{
    AUTO,
    PLUS_X,
    MINUS_X,
    PLUS_Z,
    MINUS_Z
};

// Displacement sculpt sub-tools (SCULPT mode).
enum class SculptTool
{
    RAISE,      // push grid verts out along the face normal
    LOWER,      // pull grid verts in along the face normal
    SMOOTH,     // relax each vert toward its neighbours' average offset
    NOISE,      // random jitter along the normal (roughening)
    FLATTEN     // ease offsets back toward the flat base plane
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

    // ---- stairs (STAIRS primitive) ----
    int             stairSteps    = 8;                       // 2..64
    BrushAxisChoice stairAxis     = BrushAxisChoice::AUTO;   // ascent direction
    bool            stairClipRamp = false;                   // also emit a player-clip ramp

    // ---- arch (ARCH primitive) ----
    int             archSegments  = 8;                       // 3..32
    float           archArcDeg    = 180.0f;                  // 10..360
    float           archStartDeg  = 0.0f;
    float           archWallDepth = 0.5f;                    // radial band, world units
    BrushAxisChoice archAxis      = BrushAxisChoice::AUTO;   // span axis

    // ---- gothic arch (GOTHIC_ARCH primitive) ----
    int             gothicSegments   = 6;                    // 2..16 PER SIDE
    float           gothicPointiness = 1.0f;                 // 0 = round, 1 = equilateral
    float           gothicWallDepth  = 0.5f;
    BrushAxisChoice gothicAxis       = BrushAxisChoice::AUTO;

    // ---- cone (CONE primitive) ----
    // cylinderSides is shared with CYLINDER: it is the same knob, and only one
    // of the two is ever on screen at a time.
    float              coneTopScale = 0.0f;                          // 0 = apex, 1 = prism
    BrushRevolveChoice coneAxis     = BrushRevolveChoice::PLUS_Y;
    BrushRevolveChoice cylinderAxis = BrushRevolveChoice::PLUS_Y;

    // ---- hollow (an operation on the selection, not a primitive) ----
    float hollowThickness = 0.5f;   // world units; negative wraps a shell outward

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

    // ---- displacement / sculpt (SCULPT mode) ----
    // Sculpt tuning, driven by the brush-editor panel.
    SculptTool sculptTool     = SculptTool::RAISE;
    int        dispPower      = 3;        // 2/3/4 -> 5x5 / 9x9 / 17x17 grid
    float      sculptRadius   = 2.0f;     // world units
    float      sculptStrength = 2.0f;     // world units/sec at full weight
    float      sculptFalloff  = 0.5f;     // 0 = soft (linear-ish), 1 = tight
    float      sculptNoise    = 1.0f;     // NOISE amplitude scale

    // Turn a selected quad face into a (flat) displacement / clear it back to a
    // plain face.  Both are undo-recorded; return false if the face is not a
    // usable quad (make) or not displaced (remove).
    bool makeDisplacement(const FaceRef& ref, int power);
    bool removeDisplacement(const FaceRef& ref);
    // Convenience: does the face have an active displacement?
    bool faceIsDisplaced(const FaceRef& ref) const;

    // ---- clip (CLIP mode) ----
    int  clipPointCount() const { return static_cast<int>(m_clipPoints.size()); }
    int  clipKeepMode() const { return m_clipKeep; }   // 0 front, 1 back, 2 both
    void commitClip();

    // ---- stamp/cutout (STAMP mode) ----
    int  stampPointCount() const { return static_cast<int>(m_stampPoints.size()); }
    void commitStamp();

    // Subtract the primary selected brush from every brush it overlaps
    // (carver itself is kept; delete it separately if unwanted).
    void carveWithSelected();

    // Replace every selected brush with a shell of wall brushes hollowThickness
    // thick (Hammer's Ctrl+H).  Any convex brush works: a box becomes a room, a
    // cylinder a tube.  Brushes too small to carry the wall are left untouched.
    // Undo-recorded as one entry; the resulting shell is left selected.
    void hollowSelected();

    // Transform src's plane points AND its Valve-220 texture frame by the
    // affine map A (texture lock: axes rotate/scale with the geometry, shift
    // refit so a reference point keeps its texel coordinates).
    static BrushFace transformFace(const BrushFace& src, const irr::core::matrix4& A);

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

    // ---- stamp/cutout mode state ----
    // Outline points live exactly on the locked target face's plane; commit
    // convex-hulls them, extrudes through the brush and carves.
    std::vector<irr::core::vector3df> m_stampPoints;
    uint32_t m_stampBrushId   = 0;    // target brush, locked by the first click
    int      m_stampFaceIndex = -1;   // face whose plane hosts the outline
    bool     m_stampMouseDown = false;
    bool     m_stampEnterDown = false;

    void updateStampMode();
    void drawStampMode();
    // Build the extruded carver prism from the current outline (through the
    // whole target with margin).  False when the outline/target is unusable.
    bool buildStampCarver(Brush& out);

    // ---- paint mode state ----
    bool    m_paintMouseDown = false;         // getMousePressOnce edge state (eyedropper)
    FaceRef m_paintLast;                      // last face painted this stroke
    std::vector<Brush> m_paintStroke;         // pre-stroke copies, one per touched brush

    void updatePaintMode();
    void drawPaintMode();
    void commitPaintStroke();                 // one undo entry for the whole stroke

    // ---- sculpt mode state ----
    // A press selects the face under the cursor (m_selectedFaces, single); if
    // that face is displaced and a sub-tool is set, holding+moving sculpts it.
    // A whole held stroke coalesces into one undo entry (like PAINT).
    bool m_sculptMouseDown = false;           // getMousePressOnce edge state
    std::vector<Brush> m_sculptStroke;        // pre-stroke copies, one per touched brush

    void updateSculptMode(float dt);
    void drawSculptMode();
    void commitSculptStroke();
    // Apply the current sub-tool to `face` around world point `center`.
    void applySculptToFace(Brush& brush, BrushFace& face,
                           const irr::core::vector3df& center, float dt);

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
};
