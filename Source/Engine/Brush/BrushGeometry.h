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

    // Build a face from three points wound so the plane normal faces outward.
    BrushFace makeFace(const irr::core::vector3df& a,
                       const irr::core::vector3df& b,
                       const irr::core::vector3df& c,
                       const std::string& material = std::string());

    // ---- primitives (plane sets reproduce the snapped verts exactly) ----
    Brush makeBox(const irr::core::aabbox3df& box, const std::string& material = std::string());
    // Box with the +Y/+X top edge collapsed: a ramp along X, full extent in Z.
    Brush makeWedge(const irr::core::aabbox3df& box, const std::string& material = std::string());
    // N side planes + top/bottom caps, axis = +Y, inscribed in the box footprint.
    Brush makeCylinder(const irr::core::aabbox3df& box, int sides, const std::string& material = std::string());

    // ---- plane-first operations ----
    // Split by a plane: the piece in FRONT of the plane (distance > 0) and the
    // piece BEHIND it.  Either output may be skipped by passing nullptr, and
    // either may come back invalid (geometryValid == false) when the plane
    // misses the brush.  Face attributes are inherited; the cut face gets the
    // attributes of `cutFaceTemplate` if given, else defaults + initFaceUV.
    void clipBrush(const Brush& in, const irr::core::plane3df& plane,
                   Brush* outFront, Brush* outBack,
                   const BrushFace* cutFaceTemplate = nullptr);

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
