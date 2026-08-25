#pragma once

#include "Engine/Brush/BrushData.h"

// ---------------------------------------------------------------------------
// BrushGeometry — pure geometry kernel for CSG brushes.
//
// Everything here derives from the persisted plane set: rebuild() enumerates
// the convex polyhedron (verts + per-face loops) by triple-plane intersection,
// and doubles as the validator — a brush whose planes all yield non-degenerate
// loops with every vertex behind every plane is convex by construction.
//
// No engine dependencies beyond Irrlicht math; testable in isolation via
// runSelfTests().
// ---------------------------------------------------------------------------

namespace BrushGeometry
{
    // All committed geometry snaps to this grid; it is what bounds float drift
    // across repeated edits (planes are re-derived from quantized points on
    // every commit, so error can never accumulate).
    const float GRID_QUANTUM   = 1.0f / 128.0f;
    const float ON_EPSILON     = 0.008f;   // point-on-plane tolerance (~ quantum)
    const float WELD_EPSILON   = 0.004f;   // vertex weld radius
    const float NORMAL_EPSILON = 1e-5f;    // degenerate-plane determinant floor
    const int   MAX_FACES      = 64;

    // Snap a point / all of a brush's plane points to GRID_QUANTUM.
    irr::core::vector3df quantize(const irr::core::vector3df& v);
    void quantizeFaces(Brush& b);

    // Derive plane, verts, loops and bounds from the plane set.
    // Prunes redundant faces (planes contributing no loop) with a log warning.
    // Returns false — and sets geometryValid = false — if the plane set does
    // not describe a valid convex polyhedron (empty volume, open hull,
    // degenerate edges, too many faces).
    bool rebuild(Brush& b);

    // Refit each face's planePoints from its (possibly dragged) loop verts:
    // Newell-method best-fit plane, three most-spread loop verts projected
    // onto it and quantized.  Call before rebuild() when committing a
    // vertex-first edit.  Returns false if any face degenerates.
    bool refitFacePlanes(Brush& b);

    // Initialize a face's UV axes to the world-aligned paraxial basis for its
    // plane normal (coplanar faces of neighboring brushes tile seamlessly).
    // Requires face.plane to be valid.
    void initFaceUV(BrushFace& face);

    // ---- displacement helpers (Source-style, see DispInfo) ----
    // Extract the four corners of a QUAD face into out[0..3] ordered
    // [uv00, uv10, uv01, uv11] by projecting loop verts onto the face's
    // uAxis/vAxis.  Collinear loop verts (kept to avoid T-junctions) are
    // dropped first; returns false unless exactly four corners remain.
    // Requires face.loop / brush.verts to be up to date (post-rebuild).
    bool extractQuadCorners(const Brush& b, const BrushFace& face,
                            irr::core::vector3df out[4]);

    // Bilinear base position of grid vertex (i,j) across a quad's four corners
    // (as returned by extractQuadCorners), with `side` verts per edge.
    irr::core::vector3df dispBasePos(const irr::core::vector3df corners[4],
                                     int i, int j, int side);

    // Build a face from three points wound so the plane normal faces outward.
    BrushFace makeFace(const irr::core::vector3df& a,
                       const irr::core::vector3df& b,
                       const irr::core::vector3df& c,
                       const std::string& material = std::string());

    // Horizontal direction for the parametric primitives (stair ascent, arch
    // span).  There is deliberately no AUTO here: the kernel has no access to
    // the footprint drag, so the editor resolves AUTO before calling in.
    enum class BrushAxis : irr::u8 { PLUS_X, MINUS_X, PLUS_Z, MINUS_Z };

    struct StairParams
    {
        int       steps  = 8;                    // clamped 1..64, then to what the grid can express
        BrushAxis ascend = BrushAxis::PLUS_X;    // direction the staircase climbs
    };

    struct ArchParams
    {
        int       segments     = 8;              // clamped 3..32; voussoir count == brush count
        float     arcDegrees   = 180.0f;         // clamped 10..360; 180 = doorway, 360 = closed ring
        float     startDegrees = 0.0f;           // CCW from the +span axis, in the arch plane
        float     wallDepth    = 0.5f;           // radial band thickness (world units)
        BrushAxis span         = BrushAxis::PLUS_X;   // horizontal axis the arch spans
    };

    struct GothicArchParams
    {
        int       segments   = 6;     // clamped 2..16 PER SIDE; brush count is 2*segments
        float     pointiness = 1.0f;  // clamped 0..1; 0 = semicircular, 1 = equilateral gothic
        float     wallDepth  = 0.5f;  // radial band thickness (world units)
        BrushAxis span       = BrushAxis::PLUS_X;
    };

    // Axis of revolution for the solids of revolution (cylinder, cone).  Kept
    // SEPARATE from BrushAxis rather than extending it: stairs and arches are
    // meaningless about a vertical axis, and axisIsX()/axisIsNegative() would
    // silently reinterpret a PLUS_Y stair as a +Z one instead of failing to
    // compile.  The sign chooses which END tapers, so MINUS_Y off the same drag
    // gives the flared plinth that PLUS_Y gives upside down.
    enum class BrushRevolveAxis : irr::u8
    {
        PLUS_X, MINUS_X, PLUS_Y, MINUS_Y, PLUS_Z, MINUS_Z
    };

    // ---- primitives (plane sets reproduce the snapped verts exactly) ----
    Brush makeBox(const irr::core::aabbox3df& box, const std::string& material = std::string());
    // Box with the +Y/+X top edge collapsed: a ramp along X, full extent in Z.
    Brush makeWedge(const irr::core::aabbox3df& box, const std::string& material = std::string());
    // N side planes + top/bottom caps, axis = +Y, inscribed in the box footprint.
    // Thin forward to makeCone with topScale = 1 about PLUS_Y.
    Brush makeCylinder(const irr::core::aabbox3df& box, int sides, const std::string& material = std::string());
    // N side planes + caps, inscribed in `box`, revolved about `axis` (the sign
    // picks which END tapers).  topScale scales the far ring: 1 = prism/cylinder,
    // 0 < t < 1 = frustum (battered wall bases, plinths, capitals), 0 = a true
    // apex (4 sides = a pyramid).  A top ring finer than the grid can express
    // snaps to the apex and the top cap is OMITTED — a zero-area cap is a
    // degenerate plane and rebuild() would reject the whole brush on it.
    // sides is clamped 3..32.
    Brush makeCone(const irr::core::aabbox3df& box, int sides, float topScale,
                   BrushRevolveAxis axis, const std::string& material = std::string());
    // Convex prism from points lying (approximately) on a common plane with the
    // given outward normal, extruded `depth` units along -normal.  The points
    // are convex-hulled in the plane, so sloppy/interior clicks are fine.
    // Returns an invalid brush (geometryValid == false) for degenerate input.
    Brush makeExtrudedPolygon(const std::vector<irr::core::vector3df>& points,
                              const irr::core::vector3df& normal, float depth,
                              const std::string& material = std::string());

    // Blocky (Quake/Hammer) staircase inscribed in `box`: one box brush per
    // step, each spanning one tread depth and rising from the box floor to that
    // step's top, so steps butt face-to-face and never overlap.  Risers are
    // quantized independently (never accumulated), so the top step lands exactly
    // on box.MaxEdge.Y.  Returns an empty vector when the box cannot express
    // even one step on the grid; `steps` is clamped down to what fits.
    std::vector<Brush> makeStairs(const irr::core::aabbox3df& box, const StairParams& p,
                                  const std::string& material = std::string());

    // Single convex prism covering every tread top of makeStairs(box, p), for
    // use as a CONTENT_CLIP_PLAYER volume so the player glides up instead of
    // bumping each riser.  Because blocky steps are solid to the floor, a plain
    // wedge along the nosings would sit entirely inside them and do nothing —
    // this ramp starts one riser above the floor and is clamped at the box top,
    // giving a 5-sided cross-section (7 faces).  Does NOT set contentFlags;
    // that is the editor's policy to apply.
    // Returns an invalid brush (geometryValid == false) when the riser or the
    // final tread would be finer than the grid can express — callers should
    // treat that as "no ramp"; stairs that shallow need no ramp anyway.
    Brush makeStairClipRamp(const irr::core::aabbox3df& box, const StairParams& p,
                            const std::string& material = std::string());

    // Round arch standing in the vertical plane containing `p.span` and +Y, its
    // outer ellipse inscribed in `box` (makeCylinder's footprint convention) and
    // extruded the full box extent along the remaining horizontal axis.  One
    // trapezoidal prism (voussoir) per segment, so a 180-degree arc gives a
    // doorway springing from mid-height.  A 360-degree arc reuses the seam
    // points exactly, so the ring closes with no sliver.
    std::vector<Brush> makeArch(const irr::core::aabbox3df& box, const ArchParams& p,
                                const std::string& material = std::string());

    // Pointed (gothic) arch standing in the vertical plane containing `p.span`
    // and +Y: two circular arcs springing from the box sides at mid-height and
    // meeting at a point exactly on the box top, `p.segments` voussoirs each.
    // The apex ring point is shared by both centre voussoirs, so the seam has no
    // sliver.  pointiness 0 reproduces a semicircular arch; 1 is the equilateral
    // gothic.  The apex is renormalized to the box top whatever the pointiness,
    // so it stays a pure shape control and never a size one.  Unlike makeArch
    // there is no sweep to choose — a gothic arch is always springing-to-apex.
    std::vector<Brush> makeGothicArch(const irr::core::aabbox3df& box,
                                      const GothicArchParams& p,
                                      const std::string& material = std::string());

    // ---- plane-first operations ----
    // Split by a plane: the piece in FRONT of the plane (distance > 0) and the
    // piece BEHIND it.  Either output may be skipped by passing nullptr, and
    // either may come back invalid (geometryValid == false) when the plane
    // misses the brush.  Face attributes are inherited; the cut face gets the
    // attributes of `cutFaceTemplate` if given, else defaults + initFaceUV.
    void clipBrush(const Brush& in, const irr::core::plane3df& plane,
                   Brush* outFront, Brush* outBack,
                   const BrushFace* cutFaceTemplate = nullptr);

    // Offset every face plane along its own outward normal: positive `distance`
    // grows the brush, negative shrinks it.  Per-face attributes (material,
    // Valve-220 axes, flags) are preserved, so the offset copy stays a faithful
    // stand-in for the original; displacements are cleared, since an offset face
    // is a new surface.  Returns false — leaving `b` invalid — when shrinking
    // collapses the solid, which is exactly the definition of "that wall
    // thickness does not fit this brush".
    bool offsetBrush(Brush& b, float distance);

    // Hammer's Ctrl+H.  Replaces a convex brush with the shell of wall brushes
    // that tile it: `thickness` > 0 hollows INWARD (the brush's own volume
    // becomes the room), < 0 wraps a shell around it.  Works on any convex
    // brush — a box gives a room, a cylinder a tube.  Returns one convex,
    // non-overlapping wall per surviving face; an EMPTY vector means the brush
    // is too small to carry a wall that thick and the caller should leave it
    // alone.  Fragments keep the source brush's id/name/flags, exactly like
    // carve(), and each wall's cavity-facing face inherits the material and
    // Valve-220 frame of the exterior face it was cut from.
    std::vector<Brush> hollow(const Brush& b, float thickness);

    // Subtract `carver` from `target`: returns the convex fragments of target
    // outside carver (≤ one per carver face).  Empty result means target is
    // entirely inside carver.  If the two do not intersect at all, returns a
    // single fragment equal to target.
    std::vector<Brush> carve(const Brush& target, const Brush& carver);

    // ---- queries ----
    // Exact ray vs convex plane set.  Returns true on hit with entry distance
    // (world units along the normalized ray direction) and the entered face.
    bool raycast(const Brush& b, const irr::core::vector3df& origin,
                 const irr::core::vector3df& dir, float maxDist,
                 float& tOut, int& faceOut);

    bool containsPoint(const Brush& b, const irr::core::vector3df& p);

    // ---- diagnostics ----
    // Runs the geometry self-test suite, logging via spdlog.
    // Returns the number of failed checks (0 = all passed).
    int runSelfTests();
}
