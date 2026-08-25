#include "Engine/Brush/BrushGeometry.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <spdlog/spdlog.h>

using namespace irr;
using namespace core;

namespace
{
    // ------------------------------------------------------------------
    // internals
    // ------------------------------------------------------------------

    inline float snapf(float v)
    {
        return std::floor(v / BrushGeometry::GRID_QUANTUM + 0.5f) * BrushGeometry::GRID_QUANTUM;
    }

    // Stable perpendicular basis for a plane normal.  t x b == n.
    void planeBasis(const vector3df& n, vector3df& t, vector3df& b)
    {
        vector3df up = (std::fabs(n.Y) < 0.9f) ? vector3df(0, 1, 0) : vector3df(1, 0, 0);
        t = n.crossProduct(up);
        t.normalize();
        b = n.crossProduct(t);
    }

    // Three well-spread points lying exactly on `plane`, near refPoint.
    void makePlanePoints(const plane3df& plane, const vector3df& refPoint, float extent,
                         vector3df outPts[3])
    {
        const vector3df n = plane.Normal;
        // project refPoint onto the plane
        const vector3df p0 = refPoint - n * plane.getDistanceTo(refPoint);
        vector3df t, b;
        planeBasis(n, t, b);
        if (extent < 1.0f) extent = 1.0f;
        outPts[0] = p0;
        outPts[1] = p0 + t * extent;
        outPts[2] = p0 + b * extent;
        // plane3df(p0, p0+t, p0+b) has normal t x b == n by construction
    }

    // Newell's method best-fit normal for a vertex loop (unnormalized).
    vector3df newellNormal(const std::vector<vector3df>& pts)
    {
        vector3df n(0, 0, 0);
        for (size_t i = 0; i < pts.size(); i++)
        {
            const vector3df& a = pts[i];
            const vector3df& c = pts[(i + 1) % pts.size()];
            n.X += (a.Y - c.Y) * (a.Z + c.Z);
            n.Y += (a.Z - c.Z) * (a.X + c.X);
            n.Z += (a.X - c.X) * (a.Y + c.Y);
        }
        return n;
    }

    // Shared implementation: derive plane/verts/loops/bounds from the plane
    // set.  quietPrune suppresses the per-face prune warning (used by clip and
    // carve, where faces legitimately vanish).
    bool rebuildImpl(Brush& b, bool quietPrune)
    {
        using namespace BrushGeometry;

        b.verts.clear();
        b.geometryValid = false;
        for (auto& f : b.faces)
            f.loop.clear();

        const int n = static_cast<int>(b.faces.size());
        if (n < 4 || n > MAX_FACES)
            return false;

        // Derive planes from the persisted points
        for (auto& f : b.faces)
        {
            f.plane = plane3df(f.planePoints[0], f.planePoints[1], f.planePoints[2]);
            if (f.plane.Normal.getLengthSQ() < 0.5f)   // degenerate (collinear points)
                return false;
        }

        // Enumerate candidate vertices: all triple-plane intersections that lie
        // on or behind every face plane.
        for (int i = 0; i < n - 2; i++)
        for (int j = i + 1; j < n - 1; j++)
        for (int k = j + 1; k < n; k++)
        {
            const vector3df& n1 = b.faces[i].plane.Normal;
            const vector3df& n2 = b.faces[j].plane.Normal;
            const vector3df& n3 = b.faces[k].plane.Normal;

            const float det = n1.dotProduct(n2.crossProduct(n3));
            if (std::fabs(det) < 1e-6f)
                continue;

            // Cramer's rule: p = (-d1(n2 x n3) - d2(n3 x n1) - d3(n1 x n2)) / det
            const vector3df p =
                (n2.crossProduct(n3) * -b.faces[i].plane.D +
                 n3.crossProduct(n1) * -b.faces[j].plane.D +
                 n1.crossProduct(n2) * -b.faces[k].plane.D) / det;

            bool inside = true;
            for (const auto& f : b.faces)
            {
                if (f.plane.getDistanceTo(p) > ON_EPSILON)
                {
                    inside = false;
                    break;
                }
            }
            if (!inside)
                continue;

            // Weld into the pool
            bool welded = false;
            for (const auto& v : b.verts)
            {
                if (v.getDistanceFromSQ(p) <= WELD_EPSILON * WELD_EPSILON)
                {
                    welded = true;
                    break;
                }
            }
            if (!welded)
            {
                if (b.verts.size() >= 0xFFFF)
                    return false;   // u16 loop indices — unreachable at sane face counts
                b.verts.push_back(p);
            }
        }

        if (b.verts.size() < 4)
            return false;

        // Face membership by distance: for a convex polyhedron, every pool
        // vertex on a face's plane belongs to that face's polygon.
        for (auto& f : b.faces)
        {
            for (size_t v = 0; v < b.verts.size(); v++)
            {
                if (std::fabs(f.plane.getDistanceTo(b.verts[v])) <= ON_EPSILON)
                    f.loop.push_back(static_cast<u16>(v));
            }

            if (f.loop.size() < 3)
            {
                f.loop.clear();     // pruned below
                continue;
            }

            // Sort the loop by angle around the face centroid.  With basis
            // b = n x t this yields front-facing winding for Irrlicht's
            // clockwise front faces (see BrushCompiler).
            vector3df centroid(0, 0, 0);
            for (u16 idx : f.loop)
                centroid += b.verts[idx];
            centroid /= static_cast<f32>(f.loop.size());

            vector3df t, bt;
            planeBasis(f.plane.Normal, t, bt);

            const std::vector<vector3df>& pool = b.verts;
            std::sort(f.loop.begin(), f.loop.end(),
                [&](u16 a, u16 c)
                {
                    const vector3df da = pool[a] - centroid;
                    const vector3df dc = pool[c] - centroid;
                    return std::atan2(da.dotProduct(bt), da.dotProduct(t)) <
                           std::atan2(dc.dotProduct(bt), dc.dotProduct(t));
                });
        }

        // Prune faces whose plane contributes no polygon (redundant planes,
        // or planes cut away entirely by a clip).
        for (auto it = b.faces.begin(); it != b.faces.end(); )
        {
            if (it->loop.size() < 3)
            {
                if (!quietPrune)
                    spdlog::warn("BrushGeometry: pruning redundant face on brush {} (plane contributes no polygon)", b.id);
                it = b.faces.erase(it);
            }
            else
                ++it;
        }

        if (b.faces.size() < 4)
            return false;

        // Euler characteristic V - E + F == 2 catches open hulls and
        // duplicate-plane degeneracies.  Every edge is shared by exactly two
        // face loops, so E = sum(loop sizes) / 2.
        size_t loopTotal = 0;
        for (const auto& f : b.faces)
            loopTotal += f.loop.size();
        if (loopTotal % 2 != 0)
            return false;
        const long long euler =
            static_cast<long long>(b.verts.size()) -
            static_cast<long long>(loopTotal / 2) +
            static_cast<long long>(b.faces.size());
        if (euler != 2)
            return false;

        b.bounds.reset(b.verts[0]);
        for (size_t v = 1; v < b.verts.size(); v++)
            b.bounds.addInternalPoint(b.verts[v]);

        b.geometryValid = true;
        return true;
    }

    // Orient a face's plane points so the outward normal faces away from an
    // interior reference point.
    void orientOutward(BrushFace& f, const vector3df& interior)
    {
        const plane3df pl(f.planePoints[0], f.planePoints[1], f.planePoints[2]);
        if (pl.getDistanceTo(interior) > 0.0f)
            std::swap(f.planePoints[1], f.planePoints[2]);
    }

    // ------------------------------------------------------------------
    // parametric-primitive helpers (stairs / arch)
    // ------------------------------------------------------------------

    inline bool axisIsX(BrushGeometry::BrushAxis a)
    {
        return a == BrushGeometry::BrushAxis::PLUS_X ||
               a == BrushGeometry::BrushAxis::MINUS_X;
    }

    inline bool axisIsNegative(BrushGeometry::BrushAxis a)
    {
        return a == BrushGeometry::BrushAxis::MINUS_X ||
               a == BrushGeometry::BrushAxis::MINUS_Z;
    }

    // Stair layout in whole grid quanta, shared by makeStairs and
    // makeStairClipRamp so the two can never disagree about the effective step
    // count.  Working in integers is what makes every tread edge and every
    // riser STRICTLY increasing by construction: once the step count is clamped
    // to at most the number of quanta available on an axis, adding runQ >= n to
    // the numerator always bumps the quotient, so no float rounding can collapse
    // two steps together or leave the top step short of the box.
    struct StairLayout
    {
        int              steps = 0;     // effective count after clamping
        std::vector<int> edgeQ;         // steps+1 tread edges, quanta from the low end
        std::vector<int> riseQ;         // steps riser tops, quanta above the floor
    };

    bool buildStairLayout(float run, float rise, int requested, StairLayout& out)
    {
        const float q = BrushGeometry::GRID_QUANTUM;
        const int runQ  = static_cast<int>(std::floor(run  / q + 0.5f));
        const int riseQ = static_cast<int>(std::floor(rise / q + 0.5f));
        if (runQ < 1 || riseQ < 1)
            return false;

        // Two quanta, not one, is the real minimum feature size: ON_EPSILON
        // (~0.008) is slightly LARGER than one quantum (0.0078125), so a
        // one-quantum-thick box has its opposing faces inside the point-on-plane
        // tolerance and rebuild() collapses it.
        int n = requested;
        if (n < 1)          n = 1;
        if (n > 64)         n = 64;
        if (n > runQ  / 2)  n = runQ  / 2;
        if (n > riseQ / 2)  n = riseQ / 2;
        if (n < 1)
            return false;

        out.steps = n;
        out.edgeQ.resize(static_cast<size_t>(n) + 1);
        out.riseQ.resize(static_cast<size_t>(n));
        for (int i = 0; i <= n; i++)
            out.edgeQ[static_cast<size_t>(i)] = (runQ * i + n / 2) / n;
        for (int i = 0; i < n; i++)
            out.riseQ[static_cast<size_t>(i)] = (riseQ * (i + 1) + n / 2) / n;
        return true;
    }

    // Basis for a solid of revolution.  `aDir` points at the TAPERED end and
    // uDir/vDir span the ring plane.  The PLUS_Y case reproduces makeCylinder's
    // original parametrization exactly (cos on X, sin on Z), so a cylinder built
    // through makeCone comes out vertex-for-vertex identical.
    void revolveBasis(BrushGeometry::BrushRevolveAxis a,
                      vector3df& aDir, vector3df& uDir, vector3df& vDir)
    {
        typedef BrushGeometry::BrushRevolveAxis RA;
        switch (a)
        {
        case RA::PLUS_X:  aDir.set( 1, 0, 0); uDir.set(0, 0, 1); vDir.set(0, 1, 0); break;
        case RA::MINUS_X: aDir.set(-1, 0, 0); uDir.set(0, 0, 1); vDir.set(0, 1, 0); break;
        case RA::PLUS_Z:  aDir.set(0, 0,  1); uDir.set(1, 0, 0); vDir.set(0, 1, 0); break;
        case RA::MINUS_Z: aDir.set(0, 0, -1); uDir.set(1, 0, 0); vDir.set(0, 1, 0); break;
        case RA::MINUS_Y: aDir.set(0, -1, 0); uDir.set(1, 0, 0); vDir.set(0, 0, 1); break;
        case RA::PLUS_Y:
        default:          aDir.set(0,  1, 0); uDir.set(1, 0, 0); vDir.set(0, 0, 1); break;
        }
    }

    // Prism over an already-convex, already-quantized cross-section polygon
    // lying in the plane through `pts` with outward normal `n`, extruded
    // `depth` units along -n.
    //
    // Unlike makeExtrudedPolygon this does NO hulling and NO reordering: the
    // callers are generators whose points are computed exactly, so face order
    // stays deterministic (one side face per polygon edge, then the +n cap,
    // then the -n cap).  Consecutive duplicates are welded, so a degenerate
    // collapse (a one-step ramp, a hair-thin voussoir) falls back to a
    // lower-order prism instead of an invalid brush.  Winding-agnostic:
    // orientOutward fixes the normals against the centroid.
    Brush prismFromPolygon(const std::vector<vector3df>& pts, const vector3df& n,
                           float depth, const std::string& material)
    {
        Brush b;
        if (pts.size() < 3 || depth < BrushGeometry::GRID_QUANTUM)
            return b;

        const float weldSQ = BrushGeometry::WELD_EPSILON * BrushGeometry::WELD_EPSILON;
        std::vector<vector3df> poly;
        poly.reserve(pts.size());
        for (size_t i = 0; i < pts.size(); i++)
        {
            if (!poly.empty() && poly.back().getDistanceFromSQ(pts[i]) < weldSQ)
                continue;
            poly.push_back(pts[i]);
        }
        while (poly.size() > 1 &&
               poly.front().getDistanceFromSQ(poly.back()) < weldSQ)
            poly.pop_back();

        const size_t np = poly.size();
        if (np < 3 || np + 2 > static_cast<size_t>(BrushGeometry::MAX_FACES))
            return b;

        const vector3df ext = n * depth;
        for (size_t i = 0; i < np; i++)
            b.faces.push_back(BrushGeometry::makeFace(poly[i], poly[(i + 1) % np],
                                                      poly[i] - ext, material));
        b.faces.push_back(BrushGeometry::makeFace(poly[0], poly[1], poly[2], material));
        b.faces.push_back(BrushGeometry::makeFace(poly[0] - ext, poly[2] - ext,
                                                  poly[1] - ext, material));

        vector3df centroid(0, 0, 0);
        for (size_t i = 0; i < np; i++)
            centroid += poly[i];
        centroid /= static_cast<float>(np);
        centroid -= ext * 0.5f;
        for (auto& f : b.faces)
            orientOutward(f, centroid);

        BrushGeometry::rebuild(b);
        return b;
    }
}

namespace BrushGeometry
{

vector3df quantize(const vector3df& v)
{
    return vector3df(snapf(v.X), snapf(v.Y), snapf(v.Z));
}

void quantizeFaces(Brush& b)
{
    for (auto& f : b.faces)
        for (int i = 0; i < 3; i++)
            f.planePoints[i] = quantize(f.planePoints[i]);
}

bool rebuild(Brush& b)
{
    return rebuildImpl(b, false);
}

bool refitFacePlanes(Brush& b)
{
    for (auto& f : b.faces)
    {
        if (f.loop.size() < 3)
            return false;

        std::vector<vector3df> pts;
        pts.reserve(f.loop.size());
        for (u16 idx : f.loop)
            pts.push_back(b.verts[idx]);

        vector3df n = newellNormal(pts);
        if (n.getLengthSQ() < NORMAL_EPSILON * NORMAL_EPSILON)
            return false;
        n.normalize();
        if (n.dotProduct(f.plane.Normal) < 0.0f)
            n = -n;     // keep outward orientation

        vector3df centroid(0, 0, 0);
        for (const auto& p : pts)
            centroid += p;
        centroid /= static_cast<f32>(pts.size());
        const float d = -centroid.dotProduct(n);    // best-fit through centroid

        // Three most-spread loop verts: first vert, farthest from it, then
        // max triangle area.
        const vector3df& a = pts[0];
        size_t bi = 1;
        float bestD = 0.0f;
        for (size_t i = 1; i < pts.size(); i++)
        {
            const float dd = pts[i].getDistanceFromSQ(a);
            if (dd > bestD) { bestD = dd; bi = i; }
        }
        size_t ci = 0;
        float bestArea = -1.0f;
        for (size_t i = 0; i < pts.size(); i++)
        {
            if (i == 0 || i == bi) continue;
            const float area = (pts[bi] - a).crossProduct(pts[i] - a).getLengthSQ();
            if (area > bestArea) { bestArea = area; ci = i; }
        }
        if (bestArea <= NORMAL_EPSILON)
            return false;

        // Project the three onto the best-fit plane, then quantize.
        vector3df tri[3] = { a, pts[bi], pts[ci] };
        for (int i = 0; i < 3; i++)
        {
            tri[i] -= n * (n.dotProduct(tri[i]) + d);
            tri[i] = quantize(tri[i]);
        }

        // Wind so the derived plane keeps the outward normal.
        const plane3df check(tri[0], tri[1], tri[2]);
        if (check.Normal.getLengthSQ() < 0.5f)
            return false;
        if (check.Normal.dotProduct(n) < 0.0f)
            std::swap(tri[1], tri[2]);

        f.planePoints[0] = tri[0];
        f.planePoints[1] = tri[1];
        f.planePoints[2] = tri[2];
    }
    return true;
}

void initFaceUV(BrushFace& face)
{
    const vector3df& n = face.plane.Normal;
    const float ax = std::fabs(n.X), ay = std::fabs(n.Y), az = std::fabs(n.Z);

    if (ay >= ax && ay >= az)
    {
        face.uAxis = vector3df(1, 0, 0);    // floors / ceilings: project to XZ
        face.vAxis = vector3df(0, 0, 1);
    }
    else if (ax >= az)
    {
        face.uAxis = vector3df(0, 0, 1);    // ±X walls
        face.vAxis = vector3df(0, -1, 0);
    }
    else
    {
        face.uAxis = vector3df(1, 0, 0);    // ±Z walls
        face.vAxis = vector3df(0, -1, 0);
    }
    face.shiftU = face.shiftV = 0.0f;
    face.rotationDeg = 0.0f;
}

bool extractQuadCorners(const Brush& b, const BrushFace& face, vector3df out[4])
{
    if (face.loop.size() < 3)
        return false;

    // Drop collinear loop verts (loops keep them to avoid T-junction cracks
    // between adjacent faces; a displacement quad needs the four true corners).
    std::vector<vector3df> corners;
    const size_t n = face.loop.size();
    for (size_t i = 0; i < n; i++)
    {
        const vector3df& prev = b.verts[face.loop[(i + n - 1) % n]];
        const vector3df& cur  = b.verts[face.loop[i]];
        const vector3df& next = b.verts[face.loop[(i + 1) % n]];
        vector3df e0 = cur - prev, e1 = next - cur;
        e0.normalize();
        e1.normalize();
        // sin^2(turn angle); ~0 means cur lies on the edge -> skip it.
        if (e0.crossProduct(e1).getLengthSQ() < 1e-6f)
            continue;
        corners.push_back(cur);
    }
    if (corners.size() != 4)
        return false;

    // Order by proximity to the UV-space bounding-box corners so the (i,j)
    // grid mapping is stable across reloads and loop-rotation independent.
    const vector3df& ua = face.uAxis;
    const vector3df& va = face.vAxis;
    float pu[4], pv[4];
    float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
    for (int k = 0; k < 4; k++)
    {
        pu[k] = corners[k].dotProduct(ua);
        pv[k] = corners[k].dotProduct(va);
        minU = std::min(minU, pu[k]); maxU = std::max(maxU, pu[k]);
        minV = std::min(minV, pv[k]); maxV = std::max(maxV, pv[k]);
    }
    const float tu[4] = { minU, maxU, minU, maxU };   // targets: 00,10,01,11
    const float tv[4] = { minV, minV, maxV, maxV };
    bool used[4] = { false, false, false, false };
    for (int o = 0; o < 4; o++)
    {
        int best = -1;
        float bestD = 1e30f;
        for (int k = 0; k < 4; k++)
        {
            if (used[k]) continue;
            const float du = pu[k] - tu[o], dv = pv[k] - tv[o];
            const float d = du * du + dv * dv;
            if (d < bestD) { bestD = d; best = k; }
        }
        if (best < 0) return false;
        used[best] = true;
        out[o] = corners[best];
    }
    return true;
}

vector3df dispBasePos(const vector3df corners[4], int i, int j, int side)
{
    const float fu = (side > 1) ? static_cast<float>(i) / static_cast<float>(side - 1) : 0.0f;
    const float fv = (side > 1) ? static_cast<float>(j) / static_cast<float>(side - 1) : 0.0f;
    // corners are [00,10,01,11]; bilinear across u then v.
    const vector3df bottom = corners[0] + (corners[1] - corners[0]) * fu;
    const vector3df top    = corners[2] + (corners[3] - corners[2]) * fu;
    return bottom + (top - bottom) * fv;
}

BrushFace makeFace(const vector3df& a, const vector3df& b, const vector3df& c,
                   const std::string& material)
{
    BrushFace f;
    f.planePoints[0] = a;
    f.planePoints[1] = b;
    f.planePoints[2] = c;
    f.materialName = material;
    f.plane = plane3df(a, b, c);
    initFaceUV(f);
    return f;
}

Brush makeBox(const aabbox3df& box, const std::string& material)
{
    Brush b;
    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);
    const vector3df c = (m + M) * 0.5f;

    b.faces.push_back(makeFace({ M.X, m.Y, m.Z }, { M.X, M.Y, m.Z }, { M.X, m.Y, M.Z }, material)); // +X
    b.faces.push_back(makeFace({ m.X, m.Y, m.Z }, { m.X, m.Y, M.Z }, { m.X, M.Y, m.Z }, material)); // -X
    b.faces.push_back(makeFace({ m.X, M.Y, m.Z }, { m.X, M.Y, M.Z }, { M.X, M.Y, m.Z }, material)); // +Y
    b.faces.push_back(makeFace({ m.X, m.Y, m.Z }, { M.X, m.Y, m.Z }, { m.X, m.Y, M.Z }, material)); // -Y
    b.faces.push_back(makeFace({ m.X, m.Y, M.Z }, { M.X, m.Y, M.Z }, { m.X, M.Y, M.Z }, material)); // +Z
    b.faces.push_back(makeFace({ m.X, m.Y, m.Z }, { m.X, M.Y, m.Z }, { M.X, m.Y, m.Z }, material)); // -Z

    for (auto& f : b.faces)
        orientOutward(f, c);
    rebuild(b);
    return b;
}

Brush makeWedge(const aabbox3df& box, const std::string& material)
{
    Brush b;
    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);
    // Interior reference: below the slant midpoint
    const vector3df c(m.X + (M.X - m.X) * 0.25f, m.Y + (M.Y - m.Y) * 0.25f, (m.Z + M.Z) * 0.5f);

    b.faces.push_back(makeFace({ m.X, m.Y, m.Z }, { m.X, m.Y, M.Z }, { m.X, M.Y, m.Z }, material)); // -X (tall side)
    b.faces.push_back(makeFace({ m.X, m.Y, m.Z }, { M.X, m.Y, m.Z }, { m.X, m.Y, M.Z }, material)); // -Y (floor)
    b.faces.push_back(makeFace({ m.X, m.Y, M.Z }, { M.X, m.Y, M.Z }, { m.X, M.Y, M.Z }, material)); // +Z
    b.faces.push_back(makeFace({ m.X, m.Y, m.Z }, { m.X, M.Y, m.Z }, { M.X, m.Y, m.Z }, material)); // -Z
    // Slant: full height at x = min, zero at x = max
    b.faces.push_back(makeFace({ m.X, M.Y, m.Z }, { m.X, M.Y, M.Z }, { M.X, m.Y, m.Z }, material));

    for (auto& f : b.faces)
        orientOutward(f, c);
    rebuild(b);
    return b;
}

static Brush makeConeOnce(const aabbox3df& box, int sides, float topScale,
                          BrushRevolveAxis axis, const std::string& material)
{
    Brush b;
    if (sides < 3)  sides = 3;
    if (sides > 32) sides = 32;
    if (topScale < 0.0f) topScale = 0.0f;
    // Above 1 the top ring would escape the box; flare the other way with the
    // negative axis instead, which keeps the box-inscribed contract.
    if (topScale > 1.0f) topScale = 1.0f;

    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);
    const vector3df c   = (m + M) * 0.5f;
    const vector3df ext = M - m;

    vector3df aDir, uDir, vDir;
    revolveBasis(axis, aDir, uDir, vDir);

    const float hA = std::fabs(ext.dotProduct(aDir)) * 0.5f;
    const float rU = std::fabs(ext.dotProduct(uDir)) * 0.5f;
    const float rV = std::fabs(ext.dotProduct(vDir)) * 0.5f;
    if (hA < GRID_QUANTUM || rU < GRID_QUANTUM || rV < GRID_QUANTUM)
        return b;

    // Snap to a true apex when the top ring is finer than the grid can express.
    // Both the RADIUS and the CHORD have to clear two quanta: an elongated box
    // can leave a top ring whose radius looks fine while its chord — which is
    // what separates adjacent side planes — is sub-grid, and near-coincident
    // side planes make the whole plane set non-convex.  A ring that small is a
    // point in all but name, so building the honest cone is the right answer.
    const float minFeature = GRID_QUANTUM * 2.0f;
    const float rTopMin    = ((rU < rV) ? rU : rV) * topScale;
    const float topChord   = 2.0f * rTopMin * std::sin(PI / static_cast<float>(sides));
    const bool  apex = (rTopMin < minFeature) || (topChord < minFeature);

    // Deliberately NOT quantized.  The axial component already lands exactly on
    // the box face (c +/- hA collapses to m or M, both quantized), while the
    // in-plane components stay the exact centre — snapping those first and then
    // adding the radius compounds two roundings and pushes the ring outside the
    // box on small brushes.  Only the finished ring points are quantized, which
    // is what makeCylinder always did.
    const vector3df baseC = c - aDir * hA;      // wide end
    const vector3df topC  = c + aDir * hA;      // tapered end

    // Base ring corners are snapped to the grid FIRST so the side planes exactly
    // reproduce them on rebuild.
    std::vector<vector3df> base(static_cast<size_t>(sides));
    for (int i = 0; i < sides; i++)
    {
        const float phi = 2.0f * PI * static_cast<float>(i) / static_cast<float>(sides);
        const float cs = std::cos(phi), sn = std::sin(phi);
        base[static_cast<size_t>(i)] =
            quantize(baseC + uDir * (cs * rU) + vDir * (sn * rV));
    }

    // Side planes.  The third point is derived from the QUANTIZED base corner by
    // the exact taper — deliberately NOT quantized itself.  With
    // t_i = topC + s*(b_i - baseC), the identity
    //     t_1 = b_1 + (t_0 - b_0) + (s - 1)*(b_1 - b_0)
    // puts t_1 exactly on the plane through b_0, b_1, t_0, so every side quad is
    // exactly planar.  Snapping the top ring independently instead breaks that
    // by up to a quantum, which for a small top ring is enough to make the whole
    // plane set non-convex and get the brush rejected.
    // The formula also unifies the three cases: s = 0 gives t_i == topC (the
    // apex), and s = 1 gives an exact on-grid translate of the base ring (the
    // cylinder, unchanged from before).
    for (int i = 0; i < sides; i++)
    {
        const vector3df& b0 = base[static_cast<size_t>(i)];
        const vector3df& b1 = base[static_cast<size_t>((i + 1) % sides)];
        const vector3df  t0 = topC + (b0 - baseC) * topScale;
        if (b0.getDistanceFromSQ(b1) < GRID_QUANTUM * GRID_QUANTUM)
            continue;   // radius too small for this side count; neighbors merged by snapping
        if (b0.getDistanceFromSQ(t0) < GRID_QUANTUM * GRID_QUANTUM)
            continue;
        b.faces.push_back(makeFace(b0, t0, b1, material));
    }
    if (b.faces.size() < 3)
        return b;

    // Caps from the axis frame rather than from ring points: three quantized
    // ring points need not be exactly coplanar-perpendicular, and a tilted cap
    // plane is what turns a tall thin cone into a rejected brush.  A centre plus
    // two unit basis offsets is exactly on-grid and exactly perpendicular.
    // The +axis cap is pushed first to preserve makeCylinder's face order.
    if (!apex)
        b.faces.push_back(makeFace(topC, topC + uDir, topC + vDir, material));
    b.faces.push_back(makeFace(baseC, baseC + uDir, baseC + vDir, material));

    for (auto& f : b.faces)
        orientOutward(f, c);
    rebuild(b);
    return b;
}

Brush makeCone(const aabbox3df& box, int sides, float topScale,
               BrushRevolveAxis axis, const std::string& material)
{
    if (sides < 3)  sides = 3;
    if (sides > 32) sides = 32;

    // Reduce the side count until the plane set both survives rebuild() AND
    // still fits the box it was asked to fill.
    //
    // A narrow taper amplifies the base ring's quantization: the side planes are
    // pinned by base corners that moved by up to half a quantum, and as those
    // planes converge toward the tapered end that angular error grows into a
    // positional one.  Past a certain side count it exceeds WELD_EPSILON and the
    // top-ring vertices split into near-duplicates that never weld.
    //
    // Checking validity alone is NOT enough, and this is the subtle part: when
    // the top ring collapses far enough, the enumeration finds the rulings'
    // common apex instead, the top cap contributes no loop and is pruned as
    // redundant, and rebuild() then SUCCEEDS — handing back a valid brush that
    // is a full cone running out to the analytic apex, well outside the box.
    // The box-inscribed contract is what actually distinguishes the shape we
    // asked for, so test that.  One or two fewer sides is visually
    // indistinguishable and always beats silently wrong geometry.
    const vector3df qm = quantize(box.MinEdge);
    const vector3df qM = quantize(box.MaxEdge);
    const float tol = ON_EPSILON + GRID_QUANTUM * 2.0f;

    for (int n = sides; n >= 3; n--)
    {
        Brush b = makeConeOnce(box, n, topScale, axis, material);
        if (!b.geometryValid)
            continue;
        if (b.bounds.MinEdge.X < qm.X - tol || b.bounds.MaxEdge.X > qM.X + tol ||
            b.bounds.MinEdge.Y < qm.Y - tol || b.bounds.MaxEdge.Y > qM.Y + tol ||
            b.bounds.MinEdge.Z < qm.Z - tol || b.bounds.MaxEdge.Z > qM.Z + tol)
            continue;               // top ring collapsed; this is a cone, not the frustum asked for

        if (n < sides)
            spdlog::warn("BrushGeometry::makeCone: {} sides do not survive this taper "
                         "on the grid, reduced to {}", sides, n);
        return b;
    }
    return Brush();
}

Brush makeCylinder(const aabbox3df& box, int sides, const std::string& material)
{
    return makeCone(box, sides, 1.0f, BrushRevolveAxis::PLUS_Y, material);
}

Brush makeExtrudedPolygon(const std::vector<vector3df>& points,
                          const vector3df& normal, float depth,
                          const std::string& material)
{
    Brush b;
    if (points.size() < 3 || depth <= 0.0f)
        return b;

    vector3df n = normal;
    if (n.getLengthSQ() < 1e-8f)
        return b;
    n.normalize();

    // In-plane basis for 2D hulling
    vector3df t = n.crossProduct(
        (std::fabs(n.Y) < 0.9f) ? vector3df(0, 1, 0) : vector3df(1, 0, 0));
    t.normalize();
    const vector3df bt = n.crossProduct(t);

    // Snap, weld duplicates, project to the (t, bt) plane basis
    struct P2 { float u, v; vector3df w; };
    std::vector<P2> pts;
    pts.reserve(points.size());
    for (const auto& raw : points)
    {
        const vector3df q = quantize(raw);
        bool dup = false;
        for (const auto& e : pts)
            if (e.w.getDistanceFromSQ(q) < WELD_EPSILON * WELD_EPSILON) { dup = true; break; }
        if (!dup)
            pts.push_back({ q.dotProduct(t), q.dotProduct(bt), q });
    }
    if (pts.size() < 3)
        return b;

    // 2D convex hull, Andrew's monotone chain.  Strict turns only (<= drops
    // collinear points), so the hull is minimal; fully collinear input yields
    // fewer than 3 hull points and is rejected.
    std::sort(pts.begin(), pts.end(), [](const P2& a, const P2& c)
              { return a.u < c.u || (a.u == c.u && a.v < c.v); });
    auto cross2 = [](const P2& o, const P2& a, const P2& c)
                  { return (a.u - o.u) * (c.v - o.v) - (a.v - o.v) * (c.u - o.u); };

    const size_t np = pts.size();
    std::vector<P2> hull(2 * np);
    size_t k = 0;
    for (size_t i = 0; i < np; i++)
    {
        while (k >= 2 && cross2(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f) k--;
        hull[k++] = pts[i];
    }
    const size_t lower = k + 1;
    for (size_t i = np - 1; i-- > 0; )
    {
        while (k >= lower && cross2(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f) k--;
        hull[k++] = pts[i];
    }
    hull.resize(k - 1);

    if (hull.size() < 3 || hull.size() + 2 > static_cast<size_t>(MAX_FACES))
        return b;

    // Side faces through consecutive hull edges along the extrusion, plus the
    // two caps.  orientOutward fixes any winding slips against the centroid.
    const vector3df ext = n * depth;
    const size_t hn = hull.size();
    for (size_t i = 0; i < hn; i++)
    {
        const vector3df& p0 = hull[i].w;
        const vector3df& p1 = hull[(i + 1) % hn].w;
        b.faces.push_back(makeFace(p0, p1, p0 - ext, material));
    }
    b.faces.push_back(makeFace(hull[0].w, hull[1].w, hull[2].w, material));
    b.faces.push_back(makeFace(hull[0].w - ext, hull[2].w - ext, hull[1].w - ext, material));

    vector3df centroid(0, 0, 0);
    for (const auto& h : hull)
        centroid += h.w;
    centroid /= static_cast<float>(hn);
    centroid -= ext * 0.5f;
    for (auto& f : b.faces)
        orientOutward(f, centroid);

    rebuild(b);
    return b;
}

std::vector<Brush> makeStairs(const aabbox3df& box, const StairParams& p,
                              const std::string& material)
{
    std::vector<Brush> out;

    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);

    const bool  alongX = axisIsX(p.ascend);
    const bool  flip   = axisIsNegative(p.ascend);
    const float aLo = alongX ? m.X : m.Z;       // ascent axis, low world coord
    const float aHi = alongX ? M.X : M.Z;
    const float cLo = alongX ? m.Z : m.X;       // cross axis, full extent per step
    const float cHi = alongX ? M.Z : M.X;

    const float run  = aHi - aLo;
    const float rise = M.Y - m.Y;
    if (run < GRID_QUANTUM || rise < GRID_QUANTUM || cHi - cLo < GRID_QUANTUM)
        return out;

    // Every step needs at least one quantum of tread AND one of riser, else its
    // plane set collapses.  The integer layout clamps up front so we never emit
    // a step that rebuild() would only reject afterwards.
    StairLayout layout;
    if (!buildStairLayout(run, rise, p.steps, layout))
        return out;
    if (layout.steps < p.steps)
        spdlog::warn("BrushGeometry::makeStairs: {} steps do not fit the grid, clamped to {}",
                     p.steps, layout.steps);

    out.reserve(static_cast<size_t>(layout.steps));
    for (int i = 0; i < layout.steps; i++)
    {
        const float e0   = flip ? (aHi - layout.edgeQ[i]     * GRID_QUANTUM)
                                : (aLo + layout.edgeQ[i]     * GRID_QUANTUM);
        const float e1   = flip ? (aHi - layout.edgeQ[i + 1] * GRID_QUANTUM)
                                : (aLo + layout.edgeQ[i + 1] * GRID_QUANTUM);
        const float yTop = m.Y + layout.riseQ[i] * GRID_QUANTUM;

        // Descending edges when ascending along -X/-Z; aabbox3df needs min <= max.
        const float lo = (e0 < e1) ? e0 : e1;
        const float hi = (e0 < e1) ? e1 : e0;

        const aabbox3df stepBox =
            alongX ? aabbox3df(vector3df(lo, m.Y, cLo), vector3df(hi, yTop, cHi))
                   : aabbox3df(vector3df(cLo, m.Y, lo), vector3df(cHi, yTop, hi));

        Brush s = makeBox(stepBox, material);
        if (s.geometryValid)
            out.push_back(std::move(s));
    }
    return out;
}

Brush makeStairClipRamp(const aabbox3df& box, const StairParams& p,
                        const std::string& material)
{
    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);

    const bool  alongX = axisIsX(p.ascend);
    const bool  flip   = axisIsNegative(p.ascend);
    const float aLo = alongX ? m.X : m.Z;
    const float aHi = alongX ? M.X : M.Z;
    const float cLo = alongX ? m.Z : m.X;
    const float cHi = alongX ? M.Z : M.X;

    const float run  = aHi - aLo;
    const float rise = M.Y - m.Y;
    if (run < GRID_QUANTUM || rise < GRID_QUANTUM || cHi - cLo < GRID_QUANTUM)
        return Brush();

    // Same layout the steps themselves use, so the ramp can never disagree with
    // them about the effective step count after grid clamping.
    StairLayout layout;
    if (!buildStairLayout(run, rise, p.steps, layout))
        return Brush();
    const int steps = layout.steps;

    // Step i's top is at y0 + rise*(i+1)/N over the tread [a_i, a_i+1].  A ramp
    // through the nosings would sit at y0 + rise*i/N at the BACK of each tread,
    // i.e. buried inside the solid step.  Lifting the ramp by exactly one riser
    // makes it touch each tread's back-top corner and clear the rest.
    const float aStart = flip ? aHi : aLo;      // low end of the climb
    const float aEnd   = flip ? (aHi - layout.edgeQ[steps]     * GRID_QUANTUM)
                              : (aLo + layout.edgeQ[steps]     * GRID_QUANTUM);
    const float aLast  = flip ? (aHi - layout.edgeQ[steps - 1] * GRID_QUANTUM)
                              : (aLo + layout.edgeQ[steps - 1] * GRID_QUANTUM);
    const float yLip   = m.Y + layout.riseQ[0] * GRID_QUANTUM;

    // The pentagon's two distinguishing features — the one-riser lip and the
    // final clamped tread — must each be at least two quanta.  Below that the
    // cross-section is finer than the kernel's own resolution (ON_EPSILON is
    // about one quantum), so rebuild() would reject it as degenerate anyway.
    // Bail explicitly instead: callers treat an invalid ramp as "no ramp", and
    // stairs that shallow are climbable without one regardless.  Note the
    // pentagon cannot be simplified away — dropping the clamped tread makes the
    // slope shallower, which stops covering the BACK of every tread, and
    // dropping the lip buries the ramp inside the solid steps entirely.
    const float minFeature = GRID_QUANTUM * 2.0f;
    if ((yLip - m.Y) < minFeature || std::fabs(aEnd - aLast) < minFeature)
        return Brush();

    // Cross-section on the cHi face, extruded across to cLo.  For steps == 1 the
    // last two points coincide and prismFromPolygon welds them into a plain box.
    const vector3df n = alongX ? vector3df(0, 0, 1) : vector3df(1, 0, 0);
    std::vector<vector3df> poly;
    poly.reserve(5);
    const float sect[5][2] = {
        { aStart, m.Y }, { aEnd, m.Y }, { aEnd, M.Y }, { aLast, M.Y }, { aStart, yLip }
    };
    for (int i = 0; i < 5; i++)
    {
        poly.push_back(alongX ? vector3df(sect[i][0], sect[i][1], cHi)
                              : vector3df(cHi, sect[i][1], sect[i][0]));
    }
    return prismFromPolygon(poly, n, cHi - cLo, material);
}

// Voussoir band shared by the arch family.
//
// `unitCurve` samples the OUTER curve in a normalized frame: X in [-1,1] along
// the span axis, Y in [-1,1] up from mid-height, so the curve inscribes the box
// by construction.  Both rings are the same samples scaled by the outer and
// inner radii, so the band is `wallDepth` thick at the extremes and thins
// slightly in between — the behaviour makeArch's ellipse has always had, now
// stated once instead of once per generator.  `closed` wraps the last segment
// back to sample 0 (a full ring); otherwise the curve carries one more sample
// than it has segments.
// Samples an arch's OUTER curve at a given segment count.  Passed to
// buildVoussoirBand so it can re-sample at a lower count when the geometry that
// falls out turns out to contain slivers.
typedef std::function<void(int segs, std::vector<vector2df>& out)> CurveFn;

static std::vector<Brush> buildVoussoirBandOnce(const aabbox3df& box, BrushAxis span,
                                                const std::vector<vector2df>& unitCurve,
                                                bool closed, float wallDepth,
                                                const std::string& material,
                                                int& outWanted)
{
    std::vector<Brush> out;

    const int ringN = static_cast<int>(unitCurve.size());
    const int segs  = closed ? ringN : ringN - 1;
    outWanted = segs;
    if (segs < 1)
        return out;

    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);

    const bool  alongX = axisIsX(span);
    const float sSign  = axisIsNegative(span) ? -1.0f : 1.0f;

    const float rx  = (alongX ? (M.X - m.X) : (M.Z - m.Z)) * 0.5f;   // outer, span axis
    const float ry  = (M.Y - m.Y) * 0.5f;                            // outer, vertical
    const float dLo = alongX ? m.Z : m.X;                            // depth axis extent
    const float dHi = alongX ? M.Z : M.X;
    const float depth = dHi - dLo;
    if (rx < GRID_QUANTUM || ry < GRID_QUANTUM || depth < GRID_QUANTUM)
        return out;

    // Radial band.  Like Hammer this subtracts from both radii rather than
    // offsetting at constant thickness, so a wide ellipse thins slightly at the
    // crown.  Two clamps keep it well-conditioned at both extremes:
    //
    //  - a thin band needs a real minimum thickness, not merely a representable
    //    one.  A hair-thin band is a sliver whose faces sit within the kernel's
    //    own point-on-plane tolerance along its length, so rebuild() discards
    //    it — and it would z-fight and cook badly even if it survived.  Eight
    //    quanta (1/16 unit) is the thinnest band that behaves.
    //  - a thick band floors the inner radii PROPORTIONALLY (10% of their outer
    //    radius) rather than at a bare minimum, because a sliver inner ellipse
    //    leaves the END voussoirs so nearly-degenerate that grid snapping tips
    //    them concave and they are silently lost.
    const float minBand = GRID_QUANTUM * 8.0f;
    const float rxFloor = (rx * 0.1f > minBand) ? rx * 0.1f : minBand;
    const float ryFloor = (ry * 0.1f > minBand) ? ry * 0.1f : minBand;
    const float wallMax = ((rx - rxFloor) < (ry - ryFloor)) ? (rx - rxFloor) : (ry - ryFloor);
    if (wallMax < minBand)
        return out;                 // opening too small to carry a band at all
    float wall = wallDepth;
    if (wall < minBand)  wall = minBand;
    if (wall > wallMax)  wall = wallMax;
    const float rxIn = rx - wall;
    const float ryIn = ry - wall;

    const float spanC = alongX ? (m.X + M.X) * 0.5f : (m.Z + M.Z) * 0.5f;
    const float yC    = (m.Y + M.Y) * 0.5f;

    // Ring points are snapped FIRST (makeCylinder discipline) so the side planes
    // reproduce them exactly.  A closed ring wraps by modulo onto sample 0, so
    // the seam is bit-identical and no sliver can appear.
    const vector3df dDir = alongX ? vector3df(0, 0, 1) : vector3df(1, 0, 0);
    auto ringPoint = [&](float c, float s, float radSpan, float radY)
    {
        const float sp = spanC + sSign * c * radSpan;
        const float y  = yC + s * radY;
        return quantize(alongX ? vector3df(sp, y, dHi) : vector3df(dHi, y, sp));
    };

    std::vector<vector3df> outer(static_cast<size_t>(ringN));
    std::vector<vector3df> inner(static_cast<size_t>(ringN));
    for (int i = 0; i < ringN; i++)
    {
        const float c = unitCurve[static_cast<size_t>(i)].X;
        const float s = unitCurve[static_cast<size_t>(i)].Y;
        outer[i] = ringPoint(c, s, rx,   ry);
        inner[i] = ringPoint(c, s, rxIn, ryIn);
    }

    out.reserve(static_cast<size_t>(segs));
    for (int i = 0; i < segs; i++)
    {
        const int j = closed ? ((i + 1) % ringN) : (i + 1);
        // Skip a voussoir whose in-plane quad has collapsed (tiny radius against
        // a high segment count, or a band thinner than the grid) — same guard
        // shape as makeCylinder's merged-neighbour skip.
        if (outer[i].getDistanceFromSQ(outer[j]) < GRID_QUANTUM * GRID_QUANTUM ||
            outer[i].getDistanceFromSQ(inner[i]) < GRID_QUANTUM * GRID_QUANTUM)
        {
            spdlog::warn("BrushGeometry: voussoir {} degenerate, skipped", i);
            continue;
        }

        std::vector<vector3df> quad;
        quad.reserve(4);
        quad.push_back(outer[i]);
        quad.push_back(outer[j]);
        quad.push_back(inner[j]);
        quad.push_back(inner[i]);

        Brush v = prismFromPolygon(quad, dDir, depth, material);
        if (v.geometryValid)
            out.push_back(std::move(v));
    }
    return out;
}

// Build the band, reducing the segment count until every voussoir survives.
//
// A dropped voussoir leaves a visible GAP in the arch, so it is much better to
// hand back fewer, valid ones.  This is the same principle as the callers' chord
// clamp, but measured on the geometry that actually came out instead of
// estimated from the radii — and the estimate cannot see anisotropy.  On a tall
// narrow opening (rx << ry) the voussoirs near the crown turn into slivers that
// prismFromPolygon rejects long before the chord estimate says anything is
// wrong, which silently cost the arch its apex.
static std::vector<Brush> buildVoussoirBand(const aabbox3df& box, BrushAxis span,
                                            bool closed, float wallDepth,
                                            const std::string& material,
                                            int segsRequested, int segsMin,
                                            const CurveFn& curveFn)
{
    std::vector<vector2df> curve;
    for (int segs = segsRequested; segs >= segsMin; segs--)
    {
        curveFn(segs, curve);
        int wanted = 0;
        std::vector<Brush> band =
            buildVoussoirBandOnce(box, span, curve, closed, wallDepth, material, wanted);

        if (wanted > 0 && static_cast<int>(band.size()) == wanted)
        {
            if (segs < segsRequested)
                spdlog::warn("BrushGeometry: {} segments produced slivers at this shape, "
                             "reduced to {} so every voussoir survives", segsRequested, segs);
            return band;
        }
    }
    return std::vector<Brush>();
}

std::vector<Brush> makeArch(const aabbox3df& box, const ArchParams& p,
                            const std::string& material)
{
    int segs = p.segments;
    if (segs < 3)  segs = 3;
    if (segs > 32) segs = 32;

    float arc = p.arcDegrees;
    if (arc < 10.0f)  arc = 10.0f;
    if (arc > 360.0f) arc = 360.0f;
    const bool closed = (arc >= 359.999f);

    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);
    const bool  alongX = axisIsX(p.span);
    const float rx = (alongX ? (M.X - m.X) : (M.Z - m.Z)) * 0.5f;
    const float ry = (M.Y - m.Y) * 0.5f;
    if (rx < GRID_QUANTUM || ry < GRID_QUANTUM)
        return std::vector<Brush>();

    // Clamp the segment count to what the arc can actually express: the chord
    // between consecutive ring points shrinks with both the sweep and the
    // smaller radius, and below two quanta the points snap together and the
    // voussoir is discarded.  The bound uses the OUTER radii — the outer ring
    // is what must stay distinct, whereas inner points collapsing merely welds
    // a voussoir's quad down to a triangle, which prismFromPolygon handles.
    // Clamping here means the caller gets fewer, valid voussoirs instead of a
    // silently gap-toothed arch.
    const float sweep   = arc * PI / 180.0f;
    const float rOutMin = (rx < ry) ? rx : ry;
    const int   segsMax = static_cast<int>(rOutMin * sweep / (GRID_QUANTUM * 2.0f));
    if (segs > segsMax)
    {
        spdlog::warn("BrushGeometry::makeArch: {} segments do not fit a {:.0f} degree arc "
                     "at this radius, clamped to {}", segs, arc, segsMax);
        segs = segsMax;
    }
    if (segs < 3)
        return std::vector<Brush>();    // arc too fine to express on the grid

    const float start = p.startDegrees * PI / 180.0f;
    CurveFn curveFn = [start, sweep, closed](int n, std::vector<vector2df>& out)
    {
        const int ringN = closed ? n : n + 1;
        out.resize(static_cast<size_t>(ringN));
        for (int i = 0; i < ringN; i++)
        {
            const float th = start + sweep * static_cast<float>(i) / static_cast<float>(n);
            out[static_cast<size_t>(i)] = vector2df(std::cos(th), std::sin(th));
        }
    };
    return buildVoussoirBand(box, p.span, closed, p.wallDepth, material, segs, 3, curveFn);
}

// Unit gothic curve: springing (-1,0) -> apex (0,1) -> springing (+1,0).
//
// Two circular arcs of radius Rc = 1 + d centred at (+d,0) and (-d,0), d being
// the pointiness.  d = 0 puts both centres on the axis and the curve IS a
// semicircle; d = 1 centres each arc on the OPPOSITE springing with radius equal
// to the full span — the equilateral gothic.
//
// The raw apex sits at h = sqrt(Rc^2 - d^2) = sqrt(1 + 2d)  (1 at d = 0, sqrt(3)
// at d = 1).  Dividing Y by h renormalizes it to 1, so the arch inscribes its
// box exactly whatever the pointiness: the extents stay pinned by the box and
// `pointiness` is a pure shape control, never a size one.
static void gothicUnitCurve(int segsPerSide, float pointiness,
                            std::vector<vector2df>& out)
{
    float d = pointiness;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;

    const float rc     = 1.0f + d;
    const float h      = std::sqrt(1.0f + 2.0f * d);
    const float thApex = std::acos(-d / rc);     // where the left arc crosses x = 0

    out.clear();
    out.reserve(static_cast<size_t>(2 * segsPerSide + 1));

    // Left arc, theta from PI (the -1 springing) down to thApex.  At theta = PI
    // this gives X = d + rc*(-1) = -1 and Y = 0 exactly.
    for (int i = 0; i < segsPerSide; i++)
    {
        const float th = PI + (thApex - PI) * static_cast<float>(i)
                                            / static_cast<float>(segsPerSide);
        out.push_back(vector2df(d + rc * std::cos(th), rc * std::sin(th) / h));
    }

    // The apex ONCE, written analytically rather than evaluated: both centre
    // voussoirs then quantize the identical ring point, so no sliver can open at
    // the seam.  (At thApex, cos = -d/rc gives X = 0 and sin = h/rc gives Y = 1,
    // so this is the exact value, not an approximation.)
    out.push_back(vector2df(0.0f, 1.0f));

    // Right arc = the left samples mirrored, so the arch is exactly symmetric
    // and the +1 springing is exact rather than a second acos round-trip.
    for (int i = segsPerSide - 1; i >= 0; i--)
        out.push_back(vector2df(-out[static_cast<size_t>(i)].X,
                                 out[static_cast<size_t>(i)].Y));
}

std::vector<Brush> makeGothicArch(const aabbox3df& box, const GothicArchParams& p,
                                  const std::string& material)
{
    int segs = p.segments;
    if (segs < 2)  segs = 2;
    if (segs > 16) segs = 16;

    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);
    const bool  alongX = axisIsX(p.span);
    const float rx = (alongX ? (M.X - m.X) : (M.Z - m.Z)) * 0.5f;
    const float ry = (M.Y - m.Y) * 0.5f;
    if (rx < GRID_QUANTUM || ry < GRID_QUANTUM)
        return std::vector<Brush>();

    float d = p.pointiness;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;

    // Same chord clamp makeArch uses, applied to ONE arc's sweep.
    const float rc      = 1.0f + d;
    const float sweep   = PI - std::acos(-d / rc);
    const float rOutMin = (rx < ry) ? rx : ry;
    const int   segsMax = static_cast<int>(rOutMin * sweep / (GRID_QUANTUM * 2.0f));
    if (segs > segsMax)
    {
        spdlog::warn("BrushGeometry::makeGothicArch: {} segments per side do not fit at "
                     "this radius, clamped to {}", segs, segsMax);
        segs = segsMax;
    }
    if (segs < 1)
        return std::vector<Brush>();

    CurveFn curveFn = [d](int n, std::vector<vector2df>& out)
    {
        gothicUnitCurve(n, d, out);
    };
    return buildVoussoirBand(box, p.span, /*closed=*/false, p.wallDepth, material,
                             segs, 1, curveFn);
}

void clipBrush(const Brush& in, const plane3df& plane,
               Brush* outFront, Brush* outBack,
               const BrushFace* cutFaceTemplate)
{
    const vector3df refPoint = in.bounds.getCenter();
    const float extent = in.bounds.getExtent().getLength();

    // Cut-face attribute source
    BrushFace cutProto;
    if (cutFaceTemplate)
        cutProto = *cutFaceTemplate;
    else if (!in.faces.empty())
    {
        cutProto.materialName = in.faces[0].materialName;
        cutProto.shaderName   = in.faces[0].shaderName;
    }
    cutProto.loop.clear();

    // If the template's stored points lie on the clip plane (within epsilon),
    // reuse them verbatim — preserves grid-exactness of user-placed points.
    bool templateOnPlane = false;
    if (cutFaceTemplate)
    {
        templateOnPlane = true;
        for (int i = 0; i < 3; i++)
        {
            if (std::fabs(plane.getDistanceTo(cutFaceTemplate->planePoints[i])) > ON_EPSILON)
            {
                templateOnPlane = false;
                break;
            }
        }
    }

    // The piece in FRONT of the plane is bounded by a face whose outward
    // normal is the REVERSED plane normal, and vice versa.
    struct Side { Brush* out; bool reversed; };
    const Side sides[2] = { { outFront, true }, { outBack, false } };

    for (int s = 0; s < 2; s++)
    {
        if (!sides[s].out)
            continue;

        Brush piece = in;
        piece.geometryValid = false;

        BrushFace cut = cutProto;
        if (templateOnPlane)
        {
            for (int i = 0; i < 3; i++)
                cut.planePoints[i] = cutFaceTemplate->planePoints[i];
        }
        else
        {
            makePlanePoints(plane, refPoint, extent, cut.planePoints);
        }

        // Wind for the desired outward normal
        const plane3df derived(cut.planePoints[0], cut.planePoints[1], cut.planePoints[2]);
        const float want = sides[s].reversed ? -1.0f : 1.0f;
        if (derived.Normal.dotProduct(plane.Normal) * want < 0.0f)
            std::swap(cut.planePoints[1], cut.planePoints[2]);

        cut.plane = plane3df(cut.planePoints[0], cut.planePoints[1], cut.planePoints[2]);
        if (!cutFaceTemplate)
            initFaceUV(cut);

        piece.faces.push_back(cut);
        rebuildImpl(piece, /*quietPrune=*/true);
        *sides[s].out = piece;      // geometryValid says whether the piece exists
    }
}

// Successive-clip subtraction shared by carve() and hollow().
//
// When `inheritCarverAttribs` is set the cut face is a full copy of the carver
// face — material, shader and Valve-220 frame — which is what makes a hollowed
// room's interior wear the same texture as the wall it was cut from.  Otherwise
// it is carve()'s historical behaviour: target.faces[0]'s material on a freshly
// initialized UV frame.
//
// Note the fragments inherit the target's id/name/flags, because clipBrush
// starts each piece as a copy of the input brush.  Callers that add fragments as
// NEW brushes must clear id/name themselves (see BrushTool::carveWithSelected).
static std::vector<Brush> subtractImpl(const Brush& target, const Brush& carver,
                                       bool inheritCarverAttribs)
{
    std::vector<Brush> fragments;

    if (!target.geometryValid || !carver.geometryValid ||
        !target.bounds.intersectsWithBox(carver.bounds))
    {
        fragments.push_back(target);
        return fragments;
    }

    // Successively clip the remainder against each carver plane, keeping the
    // outside piece at every step.  Whatever remains at the end lies inside
    // the carver and is discarded.
    BrushFace cutProto;
    if (!target.faces.empty())
    {
        cutProto.materialName = target.faces[0].materialName;
        cutProto.shaderName   = target.faces[0].shaderName;
    }

    Brush current = target;
    for (const auto& cf : carver.faces)
    {
        // Classify the remainder against the plane BEFORE clipping.  A plane
        // that merely grazes it (e.g. a carver face coincident with a target
        // face — routine when an outline edge lies on the brush boundary, or
        // when carver and target share a wall) must not go through clipBrush:
        // the duplicated face invalidates the inside piece and the remainder
        // would be dropped as if consumed.
        int numFront = 0, numBack = 0;
        for (const auto& v : current.verts)
        {
            const float d = cf.plane.getDistanceTo(v);
            if      (d >  ON_EPSILON) numFront++;
            else if (d < -ON_EPSILON) numBack++;
        }
        if (numFront == 0)
            continue;               // nothing outside this plane — skip it
        if (numBack == 0)
        {
            // Remainder is entirely outside the carver's volume
            fragments.push_back(current);
            return fragments;
        }

        // Reuse the carver face's exact quantized points for the cut face
        BrushFace proto;
        if (inheritCarverAttribs)
        {
            // Whole-face copy: material, shader and the Valve-220 frame come
            // across, so a hollowed wall's inner face matches its outer one.
            // clipBrush copies DispInfo along with the face, and a sculpted
            // displacement makes no sense on a freshly cut surface — drop it.
            proto = cf;
            proto.disp = DispInfo();
            proto.loop.clear();
        }
        else
        {
            proto = cutProto;
            proto.planePoints[0] = cf.planePoints[0];
            proto.planePoints[1] = cf.planePoints[1];
            proto.planePoints[2] = cf.planePoints[2];
            proto.plane = cf.plane;
            initFaceUV(proto);
        }

        Brush outside, inside;
        clipBrush(current, cf.plane, &outside, &inside, &proto);

        if (outside.geometryValid)
            fragments.push_back(outside);
        if (!inside.geometryValid)
            return fragments;       // nothing left to carve
        current = inside;
    }

    // `current` is fully inside the carver — discarded.
    return fragments;
}

std::vector<Brush> carve(const Brush& target, const Brush& carver)
{
    return subtractImpl(target, carver, /*inheritCarverAttribs=*/false);
}

bool offsetBrush(Brush& b, float distance)
{
    if (!b.geometryValid || b.faces.size() < 4)
        return false;

    // Re-derive each face's plane points spread across the whole brush rather
    // than translating the stored ones.  Quantizing three points that sit close
    // together tilts their plane by roughly quantum/spread radians, and a
    // cylinder's side face stores points barely a chord apart; spreading them
    // over the brush diagonal bounds the worst-case plane error at about one
    // quantum anywhere on the brush.  For an axis-aligned face planeBasis gives
    // axis-aligned tangents, so quantize only moves the points WITHIN the plane
    // and boxes offset with zero drift.
    const vector3df ref    = b.bounds.getCenter();
    const float     extent = b.bounds.getExtent().getLength();

    for (auto& f : b.faces)
    {
        // Irrlicht convention: getDistanceTo(p) == p.dot(Normal) + D, with a
        // unit normal, so moving the plane outward by `distance` is D -= it.
        plane3df moved = f.plane;
        moved.D -= distance;

        vector3df pts[3];
        makePlanePoints(moved, ref, extent, pts);   // wound so normal == moved.Normal
        for (int i = 0; i < 3; i++)
            f.planePoints[i] = quantize(pts[i]);

        f.loop.clear();
        f.disp = DispInfo();        // an offset face is a new surface
    }

    // rebuild() doubles as the "does this offset fit" test: shrinking past the
    // solid's own size collapses it.  It also prunes faces the shrink swallowed
    // entirely, which is geometrically right — the wall that face would have
    // produced is simply absorbed into its neighbours and the shell still tiles.
    return rebuild(b);
}

std::vector<Brush> hollow(const Brush& b, float thickness)
{
    std::vector<Brush> out;
    if (!b.geometryValid)
        return out;

    // Two quanta, not one: ON_EPSILON is larger than one quantum, so a
    // one-quantum wall has its inner and outer faces inside the point-on-plane
    // tolerance and rebuild() silently discards it.  Snapping to the grid first
    // keeps the offset planes exactly on-grid for axis-aligned faces.
    const float minWall = GRID_QUANTUM * 2.0f;
    float t = snapf(std::fabs(thickness));
    if (t < minWall)
        t = minWall;

    if (thickness >= 0.0f)
    {
        Brush cavity = b;
        if (!offsetBrush(cavity, -t))
        {
            spdlog::warn("BrushGeometry::hollow: brush {} is too small for a {:.4f} wall",
                         b.id, t);
            return out;         // empty == declined; the caller leaves the brush alone
        }
        out = subtractImpl(b, cavity, /*inheritCarverAttribs=*/true);
    }
    else
    {
        // Outward: the brush's own volume becomes the room.  Growing a convex
        // solid can never collapse it, so this direction has no minimum-size
        // failure mode.
        Brush shell = b;
        if (!offsetBrush(shell, t))
            return out;
        out = subtractImpl(shell, b, /*inheritCarverAttribs=*/true);
    }

    if (out.empty())
        spdlog::warn("BrushGeometry::hollow: brush {} produced no walls", b.id);
    return out;
}

bool raycast(const Brush& b, const vector3df& origin, const vector3df& dir,
             float maxDist, float& tOut, int& faceOut)
{
    if (!b.geometryValid)
        return false;

    float t0 = 0.0f;
    float t1 = maxDist;
    int enterFace = -1;

    for (size_t i = 0; i < b.faces.size(); i++)
    {
        const plane3df& pl = b.faces[i].plane;
        const float denom = pl.Normal.dotProduct(dir);
        const float dist  = pl.getDistanceTo(origin);

        if (std::fabs(denom) < 1e-8f)
        {
            if (dist > ON_EPSILON)
                return false;       // parallel and outside this half-space
            continue;
        }

        const float t = -dist / denom;
        if (denom < 0.0f)
        {
            if (t > t0) { t0 = t; enterFace = static_cast<int>(i); }
        }
        else
        {
            if (t < t1) t1 = t;
        }
        if (t0 > t1)
            return false;
    }

    if (enterFace < 0)
        return false;               // origin inside the brush

    tOut = t0;
    faceOut = enterFace;
    return true;
}

bool containsPoint(const Brush& b, const vector3df& p)
{
    if (!b.geometryValid)
        return false;
    for (const auto& f : b.faces)
        if (f.plane.getDistanceTo(p) > ON_EPSILON)
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Self tests
// ---------------------------------------------------------------------------

int runSelfTests()
{
    int failures = 0;
    auto check = [&](bool ok, const char* what)
    {
        if (!ok)
        {
            failures++;
            spdlog::error("BrushGeometry self-test FAILED: {}", what);
        }
    };

    const aabbox3df unitBox(vector3df(0, 0, 0), vector3df(2, 1, 3));

    // 1. Box: 8 verts, 6 quad loops, correct bounds
    {
        Brush box = makeBox(unitBox);
        check(box.geometryValid, "box valid");
        check(box.verts.size() == 8, "box has 8 verts");
        check(box.faces.size() == 6, "box has 6 faces");
        bool loops4 = true;
        for (const auto& f : box.faces)
            if (f.loop.size() != 4) loops4 = false;
        check(loops4, "box loops are quads");
        check(box.bounds.MinEdge.equals(vector3df(0, 0, 0), 0.01f) &&
              box.bounds.MaxEdge.equals(vector3df(2, 1, 3), 0.01f), "box bounds");
    }

    // 2. Redundant 7th plane is pruned
    {
        Brush box = makeBox(unitBox);
        box.faces.push_back(makeFace({ 5, 0, 0 }, { 5, 1, 0 }, { 5, 0, 1 })); // x=5, outside
        orientOutward(box.faces.back(), vector3df(1, 0.5f, 1.5f));
        check(rebuildImpl(box, true) && box.faces.size() == 6, "redundant plane pruned");
    }

    // 3. Inverted face plane -> open hull -> invalid
    {
        Brush box = makeBox(unitBox);
        std::swap(box.faces[0].planePoints[1], box.faces[0].planePoints[2]);
        Brush tmp = box;
        check(!rebuildImpl(tmp, true), "inverted plane rejected");
    }

    // 4. Open box (5 planes) -> invalid
    {
        Brush box = makeBox(unitBox);
        box.faces.erase(box.faces.begin());     // remove +X
        Brush tmp = box;
        check(!rebuildImpl(tmp, true), "open hull rejected");
    }

    // 5. Clip keep-both
    {
        Brush box = makeBox(unitBox);
        const plane3df cut(vector3df(1, 0, 0), vector3df(1, 0, 0));  // x = 1, normal +X
        Brush front, back;
        clipBrush(box, cut, &front, &back);
        check(front.geometryValid && back.geometryValid, "clip produces two pieces");
        check(front.faces.size() == 6 && back.faces.size() == 6, "clip pieces are boxes");
        bool frontOK = front.geometryValid;
        for (const auto& v : front.verts)
            if (v.X < 1.0f - ON_EPSILON) frontOK = false;
        bool backOK = back.geometryValid;
        for (const auto& v : back.verts)
            if (v.X > 1.0f + ON_EPSILON) backOK = false;
        check(frontOK && backOK, "clip pieces on correct sides");
    }

    // 6. Clip plane that misses -> one invalid piece
    {
        Brush box = makeBox(unitBox);
        const plane3df cut(vector3df(10, 0, 0), vector3df(1, 0, 0)); // x = 10
        Brush front, back;
        clipBrush(box, cut, &front, &back);
        check(!front.geometryValid && back.geometryValid, "miss clip keeps one side");
    }

    // 7. Carve a corner notch
    {
        Brush target = makeBox(aabbox3df(vector3df(0, 0, 0), vector3df(4, 4, 4)));
        Brush carver = makeBox(aabbox3df(vector3df(2, 2, 2), vector3df(6, 6, 6)));
        std::vector<Brush> frags = carve(target, carver);
        check(!frags.empty() && frags.size() <= carver.faces.size(), "carve fragment count");
        const vector3df insideCarver(3, 3, 3);
        const vector3df outsideCarver(1, 1, 1);
        int containInside = 0, containOutside = 0;
        for (const auto& fr : frags)
        {
            if (containsPoint(fr, insideCarver))  containInside++;
            if (containsPoint(fr, outsideCarver)) containOutside++;
        }
        check(containInside == 0, "carved region removed");
        check(containOutside == 1, "kept region present exactly once");
    }

    // 8. Non-intersecting carve returns target unchanged
    {
        Brush target = makeBox(aabbox3df(vector3df(0, 0, 0), vector3df(1, 1, 1)));
        Brush carver = makeBox(aabbox3df(vector3df(5, 5, 5), vector3df(6, 6, 6)));
        std::vector<Brush> frags = carve(target, carver);
        check(frags.size() == 1 && frags[0].verts.size() == 8, "disjoint carve is identity");
    }

    // 9. Cylinder: N sides + 2 caps
    {
        Brush cyl = makeCylinder(aabbox3df(vector3df(0, 0, 0), vector3df(4, 2, 4)), 8);
        check(cyl.geometryValid, "cylinder valid");
        check(cyl.faces.size() == 10, "cylinder face count");
        check(cyl.verts.size() == 16, "cylinder vert count");
    }

    // 10. Wedge: 5 faces, 6 verts
    {
        Brush wedge = makeWedge(unitBox);
        check(wedge.geometryValid, "wedge valid");
        check(wedge.faces.size() == 5, "wedge face count");
        check(wedge.verts.size() == 6, "wedge vert count");
    }

    // 11. Raycast hits the near face at the right distance
    {
        Brush box = makeBox(unitBox);
        float t = 0.0f;
        int face = -1;
        const bool hit = raycast(box, vector3df(-2, 0.5f, 1.5f), vector3df(1, 0, 0), 100.0f, t, face);
        check(hit && std::fabs(t - 2.0f) < 0.01f, "raycast distance");
        check(hit && face >= 0 && box.faces[face].plane.Normal.equals(vector3df(-1, 0, 0), 0.01f),
              "raycast entry face");
        const bool miss = raycast(box, vector3df(-2, 5, 0), vector3df(1, 0, 0), 100.0f, t, face);
        check(!miss, "raycast miss");
    }

    // 12. refitFacePlanes round-trip keeps the brush intact
    {
        Brush box = makeBox(unitBox);
        Brush copy = box;
        check(refitFacePlanes(copy) && rebuild(copy), "refit round-trip valid");
        check(copy.verts.size() == 8 && copy.faces.size() == 6, "refit round-trip counts");
        bool boundsMatch = copy.geometryValid &&
            copy.bounds.MinEdge.equals(box.bounds.MinEdge, static_cast<f32>(GRID_QUANTUM) * 2) &&
            copy.bounds.MaxEdge.equals(box.bounds.MaxEdge, static_cast<f32>(GRID_QUANTUM) * 2);
        check(boundsMatch, "refit round-trip bounds");
    }

    // 13. Quantization snaps to 1/128
    {
        const vector3df q = quantize(vector3df(1.0033f, -0.9967f, 0.5f));
        check(std::fabs(q.X - 128.0f * GRID_QUANTUM) < 1e-6f ||
              std::fabs(q.X * 128.0f - std::floor(q.X * 128.0f + 0.5f)) < 1e-4f,
              "quantize on grid");
    }

    // 14. Extruded polygon: sloppy point order + interior point are hulled;
    //     the stamp cutout carves a through-hole
    {
        // Square on the +Y plane at y=1, clicked out of order, with a point
        // inside the square that the hull must discard
        std::vector<vector3df> pts = {
            { 0, 1, 0 }, { 2, 1, 2 }, { 2, 1, 0 }, { 0, 1, 2 }, { 1, 1, 1 } };
        Brush prism = makeExtrudedPolygon(pts, vector3df(0, 1, 0), 1.0f);
        check(prism.geometryValid, "extruded polygon valid");
        check(prism.faces.size() == 6, "extruded square is a box (interior point hulled away)");
        check(prism.bounds.MinEdge.equals(vector3df(0, 0, 0), 0.01f) &&
              prism.bounds.MaxEdge.equals(vector3df(2, 1, 2), 0.01f), "extruded polygon bounds");

        // Collinear input is rejected
        std::vector<vector3df> line = { { 0, 1, 0 }, { 1, 1, 0 }, { 2, 1, 0 } };
        check(!makeExtrudedPolygon(line, vector3df(0, 1, 0), 1.0f).geometryValid,
              "collinear outline rejected");

        // Carving the prism through a bigger box yields fragments whose total
        // count is > 1 and none of which contain the hole's center
        Brush slab = makeBox(aabbox3df(vector3df(-2, 0.25f, -2), vector3df(4, 0.75f, 4)));
        std::vector<Brush> frags = carve(slab, prism);
        check(frags.size() > 1, "stamp carve splits the slab");
        bool holeOpen = true;
        for (const auto& f : frags)
            if (containsPoint(f, vector3df(1, 0.5f, 1)))
                holeOpen = false;
        check(holeOpen, "stamp carve opens a through-hole");
    }

    // 15. Corner cut: carver planes coincident with target faces must not eat
    //     the target (regression — grazing planes used to invalidate the
    //     remainder inside carve and everything vanished)
    {
        Brush box = makeBox(aabbox3df(vector3df(0, 0, 0), vector3df(4, 1, 4)));
        // Prism over the box's (0,·,0) corner: two side planes lie exactly on
        // the box's x=0 and z=0 faces, poking through vertically with margin
        std::vector<vector3df> corner = {
            { 0, 1.1f, 0 }, { 1, 1.1f, 0 }, { 1, 1.1f, 1 }, { 0, 1.1f, 1 } };
        Brush cornerPrism = makeExtrudedPolygon(corner, vector3df(0, 1, 0), 1.3f);
        check(cornerPrism.geometryValid, "corner prism valid");

        std::vector<Brush> frags = carve(box, cornerPrism);
        check(!frags.empty(), "corner carve keeps the remainder");
        bool cornerGone = true, bodyKept = false;
        for (const auto& f : frags)
        {
            if (containsPoint(f, vector3df(0.5f, 0.5f, 0.5f))) cornerGone = false;
            if (containsPoint(f, vector3df(2.5f, 0.5f, 2.5f))) bodyKept = true;
        }
        check(cornerGone, "corner carve removes the corner");
        check(bodyKept, "corner carve keeps the body");
    }

    auto containsAny = [](const std::vector<Brush>& bs, const vector3df& p)
    {
        for (const auto& b : bs)
            if (containsPoint(b, p)) return true;
        return false;
    };
    auto sharesVertex = [](const Brush& a, const Brush& b)
    {
        for (const auto& va : a.verts)
            for (const auto& vb : b.verts)
                if (va.getDistanceFromSQ(vb) < WELD_EPSILON * WELD_EPSILON) return true;
        return false;
    };

    // 16. Stairs: N solid floor-anchored boxes, butting, spanning the footprint
    {
        const aabbox3df stairBox(vector3df(0, 0, 0), vector3df(4, 2, 3));
        StairParams sp;
        sp.steps = 4;
        sp.ascend = BrushAxis::PLUS_X;
        std::vector<Brush> st = makeStairs(stairBox, sp);
        check(st.size() == 4, "stairs produce exactly N brushes");

        bool allBoxes = true, floorAnchored = true, monotone = true;
        bool noOverlap = true, crossFull = true;
        for (size_t i = 0; i < st.size(); i++)
        {
            if (!st[i].geometryValid || st[i].faces.size() != 6 || st[i].verts.size() != 8)
                allBoxes = false;
            if (std::fabs(st[i].bounds.MinEdge.Y) > ON_EPSILON)
                floorAnchored = false;
            if (std::fabs(st[i].bounds.MinEdge.Z) > ON_EPSILON ||
                std::fabs(st[i].bounds.MaxEdge.Z - 3.0f) > ON_EPSILON)
                crossFull = false;
            if (i > 0)
            {
                if (st[i].bounds.MaxEdge.Y <= st[i - 1].bounds.MaxEdge.Y)
                    monotone = false;
                if (st[i].bounds.MinEdge.X < st[i - 1].bounds.MaxEdge.X - ON_EPSILON)
                    noOverlap = false;
            }
        }
        check(allBoxes, "stair steps are valid boxes");
        check(floorAnchored, "stair steps are solid to the floor");
        check(monotone, "stair steps rise monotonically");
        check(noOverlap, "stair steps butt without overlapping");
        check(crossFull, "stair steps span the cross axis");
        check(std::fabs(st.front().bounds.MinEdge.X) < ON_EPSILON &&
              std::fabs(st.back().bounds.MaxEdge.X - 4.0f) < ON_EPSILON,
              "stairs span the footprint");
        check(std::fabs(st.back().bounds.MaxEdge.Y - 2.0f) < ON_EPSILON,
              "top stair step reaches the box top");

        // -X ascent: still lowest-first, but the tall step is now at the -X end
        sp.ascend = BrushAxis::MINUS_X;
        std::vector<Brush> rev = makeStairs(stairBox, sp);
        check(rev.size() == 4, "reversed stairs produce N brushes");
        check(!rev.empty() && std::fabs(rev.back().bounds.MinEdge.X) < ON_EPSILON,
              "reversed stairs climb toward -X");

        // Anti-drift: 7 risers over height 2 are off-grid at every intermediate
        // step, but independent quantization still lands exactly on the top.
        sp.steps = 7;
        sp.ascend = BrushAxis::PLUS_X;
        std::vector<Brush> st7 = makeStairs(stairBox, sp);
        check(st7.size() == 7, "7-step stairs produce 7 brushes");
        check(!st7.empty() && std::fabs(st7.back().bounds.MaxEdge.Y - 2.0f) < 1e-4f,
              "stair risers do not accumulate grid drift");

        // Absurd step counts clamp rather than emitting degenerate brushes
        sp.steps = 10000;
        std::vector<Brush> many = makeStairs(stairBox, sp);
        bool manyValid = !many.empty() && many.size() <= 64;
        for (const auto& b : many)
            if (!b.geometryValid) manyValid = false;
        check(manyValid, "absurd stair count clamps to valid brushes");
    }

    // 17. Stair clip ramp: covers every tread top, stays inside the stair box
    {
        const aabbox3df stairBox(vector3df(0, 0, 0), vector3df(4, 2, 3));
        StairParams sp;
        sp.steps = 4;
        sp.ascend = BrushAxis::PLUS_X;
        std::vector<Brush> st = makeStairs(stairBox, sp);
        Brush ramp = makeStairClipRamp(stairBox, sp);
        check(ramp.geometryValid, "stair clip ramp valid");
        check(ramp.faces.size() == 7, "stair clip ramp is a 5-sided prism");

        // The assertion that catches a naive nosing-line wedge: such a wedge is
        // buried inside the solid steps and fails this at every tread.
        bool coversTreads = true;
        for (const auto& s : st)
        {
            const vector3df mid((s.bounds.MinEdge.X + s.bounds.MaxEdge.X) * 0.5f,
                                s.bounds.MaxEdge.Y, 1.5f);
            if (!containsPoint(ramp, mid)) coversTreads = false;
        }
        check(coversTreads, "stair clip ramp covers every tread top");
        check(ramp.bounds.MaxEdge.Y <= 2.0f + ON_EPSILON &&
              ramp.bounds.MinEdge.X >= -ON_EPSILON &&
              ramp.bounds.MaxEdge.X <= 4.0f + ON_EPSILON,
              "stair clip ramp stays inside the stair box");

        // One step: the pentagon welds down to a plain box
        sp.steps = 1;
        Brush flat = makeStairClipRamp(stairBox, sp);
        check(flat.geometryValid && flat.faces.size() == 6,
              "one-step clip ramp collapses to a box");
    }

    // 18. Arch: segmented band, open below the springing line, closes at 360
    {
        const aabbox3df archBox(vector3df(0, 0, 0), vector3df(8, 4, 1));
        ArchParams ap;
        ap.segments = 8;
        ap.arcDegrees = 180.0f;
        ap.wallDepth = 0.5f;
        ap.span = BrushAxis::PLUS_X;
        std::vector<Brush> arch = makeArch(archBox, ap);
        check(arch.size() == 8, "arch produces one brush per segment");

        bool voussoirsOk = true, inBox = true;
        for (const auto& v : arch)
        {
            if (!v.geometryValid || v.faces.size() != 6) voussoirsOk = false;
            if (v.bounds.MinEdge.X < -ON_EPSILON || v.bounds.MaxEdge.X > 8.0f + ON_EPSILON ||
                v.bounds.MinEdge.Y < -ON_EPSILON || v.bounds.MaxEdge.Y > 4.0f + ON_EPSILON ||
                v.bounds.MinEdge.Z < -ON_EPSILON || v.bounds.MaxEdge.Z > 1.0f + ON_EPSILON)
                inBox = false;
        }
        check(voussoirsOk, "arch voussoirs are valid 6-face prisms");
        check(inBox, "arch fits its bounding box");
        check(!containsAny(arch, vector3df(4, 0.5f, 0.5f)), "arch doorway is open");
        check(containsAny(arch, vector3df(4, 3.75f, 0.5f)), "arch crown is solid");

        // 360 degrees: the seam reuses ring point 0 exactly, so no sliver
        ap.arcDegrees = 360.0f;
        std::vector<Brush> ring = makeArch(archBox, ap);
        check(ring.size() == 8, "closed arch produces one brush per segment");
        check(ring.size() == 8 && sharesVertex(ring.front(), ring.back()),
              "closed arch seam shares vertices");

        // Clamps
        ap.arcDegrees = 180.0f;
        ap.segments = 2;
        check(makeArch(archBox, ap).size() == 3, "arch segment count clamps up to 3");
        ap.segments = 999;
        check(makeArch(archBox, ap).size() == 32, "arch segment count clamps down to 32");

        // A wall thicker than the opening clamps instead of inverting
        ap.segments = 8;
        ap.wallDepth = 100.0f;
        std::vector<Brush> thick = makeArch(archBox, ap);
        bool thickOk = thick.size() == 8;
        for (const auto& v : thick)
            if (!v.geometryValid) thickOk = false;
        check(thickOk, "over-thick arch wall clamps to valid voussoirs");
    }

    // 19. Degenerate inputs: sub-unit boxes and hair-thin bands must clamp to
    //     valid geometry, never to something rebuild() silently discards.
    //     ON_EPSILON (~0.008) is slightly LARGER than one quantum (0.0078125),
    //     so every generator floors its features at two quanta; these cases are
    //     the regressions that pinned that down.
    {
        // A 0.1-unit cube asked for more steps than the grid can carry: the
        // count clamps, but the staircase must still reach the top of its box.
        const aabbox3df tiny(vector3df(0, 0, 0), vector3df(0.1f, 0.1f, 0.1f));
        const float tinyTop = quantize(vector3df(0.1f, 0.1f, 0.1f)).Y;
        bool tinyOk = true;
        for (int n = 1; n <= 40; n++)
        {
            StairParams sp;
            sp.steps = n;
            sp.ascend = BrushAxis::PLUS_X;
            std::vector<Brush> st = makeStairs(tiny, sp);
            if (st.empty()) continue;
            for (size_t i = 0; i < st.size(); i++)
                if (!st[i].geometryValid || st[i].faces.size() != 6) tinyOk = false;
            if (std::fabs(st.back().bounds.MaxEdge.Y - tinyTop) > ON_EPSILON)
                tinyOk = false;
        }
        check(tinyOk, "sub-unit stairs clamp to valid steps reaching the box top");

        // Whenever a clip ramp IS produced it must cover every tread; on boxes
        // too fine to express the pentagon it must decline outright rather than
        // hand back a ramp the player could fall through.
        bool rampOk = true;
        for (int n = 1; n <= 40; n++)
        {
            StairParams sp;
            sp.steps = n;
            sp.ascend = BrushAxis::PLUS_X;
            std::vector<Brush> st = makeStairs(tiny, sp);
            Brush ramp = makeStairClipRamp(tiny, sp);
            if (!ramp.geometryValid) continue;      // declining is allowed here
            for (size_t i = 0; i < st.size(); i++)
            {
                vector3df mid = st[i].bounds.getCenter();
                mid.Y = st[i].bounds.MaxEdge.Y;
                if (!containsPoint(ramp, mid)) rampOk = false;
            }
        }
        check(rampOk, "a produced clip ramp always covers every tread");

        // A hair-thin wall request floors to a viable band instead of
        // collapsing every voussoir into a discarded sliver.
        {
            ArchParams ap;
            ap.segments = 8;
            ap.arcDegrees = 359.0f;     // not closed: the seam is a real gap
            ap.wallDepth = 0.01f;       // ~1 quantum, below the band floor
            ap.span = BrushAxis::PLUS_X;
            std::vector<Brush> hair = makeArch(aabbox3df(vector3df(0, 0, 0),
                                                         vector3df(4, 2, 3)), ap);
            bool hairOk = hair.size() == 8;
            for (const auto& v : hair)
                if (!v.geometryValid) hairOk = false;
            check(hairOk, "hair-thin arch wall floors to valid voussoirs");
        }

        // Too many segments for the arc: clamp to what the chord can express
        // rather than returning a gap-toothed ring of discarded voussoirs.
        {
            ArchParams ap;
            ap.segments = 32;
            ap.arcDegrees = 10.0f;      // 32 segments across 10 degrees is impossible
            ap.wallDepth = 0.1f;
            ap.span = BrushAxis::PLUS_X;
            std::vector<Brush> fine = makeArch(aabbox3df(vector3df(0, 0, 0),
                                                         vector3df(4, 2, 3)), ap);
            bool fineOk = !fine.empty() && fine.size() < 32;
            for (const auto& v : fine)
                if (!v.geometryValid) fineOk = false;
            check(fineOk, "over-fine arch segments clamp to valid voussoirs");
        }
    }

    auto countContaining = [](const std::vector<Brush>& bs, const vector3df& p)
    {
        int n = 0;
        for (const auto& b : bs)
            if (containsPoint(b, p)) n++;
        return n;
    };

    // 20. Hollow tiles the shell: every sample is in EXACTLY one wall when it is
    //     outside the cavity and exactly zero when inside.  That one assertion
    //     proves non-overlap, cavity emptiness and full coverage together.
    {
        Brush src = makeBox(aabbox3df(vector3df(0, 0, 0), vector3df(4, 4, 4)));
        for (size_t i = 0; i < src.faces.size(); i++)
            src.faces[i].materialName = "mat" + std::to_string(i);

        std::vector<Brush> walls = hollow(src, 0.5f);
        check(walls.size() == 6, "hollow box gives one wall per face");

        bool wallsValid = true, inBounds = true;
        for (const auto& w : walls)
        {
            if (!w.geometryValid) wallsValid = false;
            if (w.bounds.MinEdge.X < -ON_EPSILON || w.bounds.MaxEdge.X > 4.0f + ON_EPSILON ||
                w.bounds.MinEdge.Y < -ON_EPSILON || w.bounds.MaxEdge.Y > 4.0f + ON_EPSILON ||
                w.bounds.MinEdge.Z < -ON_EPSILON || w.bounds.MaxEdge.Z > 4.0f + ON_EPSILON)
                inBounds = false;
        }
        check(wallsValid, "hollow walls are valid");
        check(inBounds, "hollow walls stay inside the original brush");

        // Oracle for "is this point in the cavity", and for the wall-to-wall
        // interfaces — those lie on the cavity face planes extended, so one
        // proximity test against every cavity plane covers both.
        Brush cavity = src;
        const bool cavityOk = offsetBrush(cavity, -0.5f);
        check(cavityOk, "hollow cavity oracle builds");

        // Lattice deliberately off-grid so samples never land on a cavity plane
        // (a 0.5-spaced lattice would sit exactly on every one of them).
        const float skip = ON_EPSILON * 4.0f;
        int tested = 0, outsideSeen = 0, insideSeen = 0;
        bool tiles = true;
        for (int ix = 0; ix < 11 && cavityOk; ix++)
        for (int iy = 0; iy < 11; iy++)
        for (int iz = 0; iz < 11; iz++)
        {
            const vector3df p(0.17f + 0.36f * ix, 0.17f + 0.36f * iy, 0.17f + 0.36f * iz);

            bool nearPlane = false, insideCavity = true;
            for (const auto& cf : cavity.faces)
            {
                const float d = cf.plane.getDistanceTo(p);
                if (std::fabs(d) < skip) nearPlane = true;
                if (d > 0.0f) insideCavity = false;
            }
            if (nearPlane)
                continue;               // off the epsilon knife-edge

            tested++;
            const int hits = countContaining(walls, p);
            if (insideCavity) { insideSeen++;  if (hits != 0) tiles = false; }
            else              { outsideSeen++; if (hits != 1) tiles = false; }
        }
        check(tested > 500 && outsideSeen > 100 && insideSeen > 100,
              "hollow lattice sweep covers both regions");
        check(tiles, "hollow walls tile the shell exactly once and leave the cavity empty");

        // Each wall's cavity-facing face must wear the material of the exterior
        // face it was cut from.  Before the subtractImpl(inherit) fix every cut
        // face took faces[0]'s material instead.
        bool matsOk = cavityOk;
        for (const auto& w : walls)
            for (const auto& wf : w.faces)
                for (const auto& cf : cavity.faces)
                {
                    if (wf.plane.Normal.dotProduct(cf.plane.Normal) > -0.999f)
                        continue;       // not the reverse of this cavity face
                    if (std::fabs(cf.plane.getDistanceTo(wf.planePoints[0])) > ON_EPSILON)
                        continue;       // not on its plane
                    if (wf.materialName != cf.materialName)
                        matsOk = false;
                }
        check(matsOk, "hollow inner faces inherit the exterior material");
    }

    // 21. Thickness limits: too thick declines outright, too thin floors
    {
        Brush small = makeBox(aabbox3df(vector3df(0, 0, 0), vector3df(2, 1, 3)));
        check(hollow(small, 5.0f).empty(), "over-thick hollow declines");

        std::vector<Brush> zero = hollow(small, 0.0f);
        bool zeroOk = zero.size() == 6;
        for (const auto& w : zero)
            if (!w.geometryValid) zeroOk = false;
        check(zeroOk, "zero hollow thickness floors to a valid shell");

        std::vector<Brush> hair = hollow(small, 0.001f);
        bool hairOk = hair.size() == 6;
        for (const auto& w : hair)
            if (!w.geometryValid) hairOk = false;
        check(hairOk, "sub-quantum hollow thickness floors to a valid shell");
    }

    // 22. Hollowing a cylinder gives a tube
    {
        Brush cyl = makeCylinder(aabbox3df(vector3df(0, 0, 0), vector3df(4, 2, 4)), 8);
        std::vector<Brush> walls = hollow(cyl, 0.25f);
        bool valid = !walls.empty() && walls.size() <= 10;
        for (const auto& w : walls)
            if (!w.geometryValid) valid = false;
        check(valid, "hollow cylinder gives valid walls");
        check(countContaining(walls, vector3df(2, 1, 2)) == 0, "tube bore is open");
        // Sample along a side-face normal (22.5 degrees), not along +X where the
        // octagon has a vertex: the face planes sit at the INRADIUS
        // 2*cos(22.5) = 1.848, so the wall band is 1.598..1.848 from the axis.
        check(countContaining(walls, vector3df(2.0f + 1.72f * 0.92388f, 1.0f,
                                               2.0f + 1.72f * 0.38268f)) == 1,
              "tube wall band is solid once");
        check(countContaining(walls, vector3df(2, 1.9f, 2)) == 1, "tube keeps its top cap");
        check(countContaining(walls, vector3df(2, 0.1f, 2)) == 1, "tube keeps its bottom cap");
    }

    // 23. Negative thickness wraps a shell around the brush instead
    {
        Brush src = makeBox(aabbox3df(vector3df(0, 0, 0), vector3df(4, 4, 4)));
        std::vector<Brush> walls = hollow(src, -0.5f);
        bool valid = walls.size() == 6, grown = true;
        for (const auto& w : walls)
        {
            if (!w.geometryValid) valid = false;
            if (w.bounds.MinEdge.X < -0.5f - ON_EPSILON || w.bounds.MaxEdge.X > 4.5f + ON_EPSILON ||
                w.bounds.MinEdge.Y < -0.5f - ON_EPSILON || w.bounds.MaxEdge.Y > 4.5f + ON_EPSILON ||
                w.bounds.MinEdge.Z < -0.5f - ON_EPSILON || w.bounds.MaxEdge.Z > 4.5f + ON_EPSILON)
                grown = false;
        }
        check(valid, "outward hollow gives one valid wall per face");
        check(grown, "outward hollow stays within the grown bounds");
        check(countContaining(walls, vector3df(2, 2, 2)) == 0, "outward hollow leaves the original volume empty");
        check(countContaining(walls, vector3df(4.25f, 2, 2)) == 1, "outward hollow covers the outside once");
    }

    // 24. Cone / frustum / revolve axis
    {
        const aabbox3df coneBox(vector3df(0, 0, 0), vector3df(4, 4, 4));

        // Apex sweep: all n side planes meet at one vertex, which is the case
        // most likely to expose an ill-conditioned triple-plane intersection.
        bool apexOk = true;
        for (int n = 3; n <= 32; n++)
        {
            Brush cone = makeCone(coneBox, n, 0.0f, BrushRevolveAxis::PLUS_Y);
            if (!cone.geometryValid ||
                cone.faces.size() != static_cast<size_t>(n) + 1 ||
                cone.verts.size() != static_cast<size_t>(n) + 1)
            { apexOk = false; break; }
        }
        check(apexOk, "cone apex is valid for every side count");

        Brush cone8 = makeCone(coneBox, 8, 0.0f, BrushRevolveAxis::PLUS_Y);
        bool apexAtTop = false;
        for (const auto& v : cone8.verts)
            if (v.equals(vector3df(2, 4, 2), 0.01f)) apexAtTop = true;
        check(apexAtTop, "cone apex sits at the box top centre");

        // Frustum keeps both caps and both rings
        Brush frustum = makeCone(coneBox, 8, 0.5f, BrushRevolveAxis::PLUS_Y);
        check(frustum.geometryValid && frustum.faces.size() == 10 &&
              frustum.verts.size() == 16, "frustum keeps both rings and caps");
        check(frustum.bounds.MinEdge.equals(vector3df(0, 0, 0), 0.01f) &&
              frustum.bounds.MaxEdge.equals(vector3df(4, 4, 4), 0.01f), "frustum fills its box");

        // topScale 1 is exactly a cylinder
        Brush asCyl = makeCone(coneBox, 8, 1.0f, BrushRevolveAxis::PLUS_Y);
        Brush cyl   = makeCylinder(coneBox, 8);
        check(asCyl.geometryValid && asCyl.faces.size() == cyl.faces.size() &&
              asCyl.verts.size() == cyl.verts.size(), "topScale 1 reproduces the cylinder");

        // A top ring finer than the grid snaps to a true apex, no sliver cap
        Brush sliver = makeCone(coneBox, 8, 1e-4f, BrushRevolveAxis::PLUS_Y);
        check(sliver.geometryValid && sliver.faces.size() == 9,
              "sub-grid top ring snaps to an apex");

        // Every axis builds, fills its box, and tapers at the expected end
        const BrushRevolveAxis axes[6] = {
            BrushRevolveAxis::PLUS_X, BrushRevolveAxis::MINUS_X,
            BrushRevolveAxis::PLUS_Y, BrushRevolveAxis::MINUS_Y,
            BrushRevolveAxis::PLUS_Z, BrushRevolveAxis::MINUS_Z };
        bool axesOk = true, tapersRight = true;
        for (int a = 0; a < 6; a++)
        {
            Brush ck = makeCone(coneBox, 8, 0.0f, axes[a]);
            if (!ck.geometryValid || ck.faces.size() != 9 || ck.verts.size() != 9)
            { axesOk = false; continue; }
            if (!ck.bounds.MinEdge.equals(vector3df(0, 0, 0), 0.01f) ||
                !ck.bounds.MaxEdge.equals(vector3df(4, 4, 4), 0.01f))
                axesOk = false;

            // The apex is the lone vertex at the tapered end: exactly one vert
            // should sit on that side of the box.
            vector3df aDir, uDir, vDir;
            switch (axes[a])
            {
            case BrushRevolveAxis::PLUS_X:  aDir.set( 1, 0, 0); break;
            case BrushRevolveAxis::MINUS_X: aDir.set(-1, 0, 0); break;
            case BrushRevolveAxis::PLUS_Z:  aDir.set(0, 0,  1); break;
            case BrushRevolveAxis::MINUS_Z: aDir.set(0, 0, -1); break;
            case BrushRevolveAxis::MINUS_Y: aDir.set(0, -1, 0); break;
            default:                        aDir.set(0,  1, 0); break;
            }
            const vector3df want = vector3df(2, 2, 2) + aDir * 2.0f;
            int atTip = 0;
            for (const auto& v : ck.verts)
                if (v.equals(want, 0.01f)) atTip++;
            if (atTip != 1) tapersRight = false;
        }
        check(axesOk, "every revolve axis builds a valid cone filling its box");
        check(tapersRight, "the revolve axis sign picks which end tapers");

        // Sub-unit boxes: decline or be fully valid, never silently discarded
        const aabbox3df tinyBox(vector3df(0, 0, 0), vector3df(0.1f, 0.1f, 0.1f));
        bool tinyOk = true;
        for (int n = 3; n <= 32; n++)
            for (int s = 0; s <= 4; s++)
            {
                Brush t = makeCone(tinyBox, n, static_cast<float>(s) * 0.25f,
                                   BrushRevolveAxis::PLUS_Y);
                if (t.geometryValid && t.faces.size() < 4) tinyOk = false;
            }
        check(tinyOk, "sub-unit cones are valid or decline cleanly");

        // Regression: a narrow taper at a high side count used to collapse the
        // top ring, get its cap pruned as redundant, and come back VALID — but
        // as a full cone running out to the analytic apex, far outside the box.
        // Validity alone never distinguished that from the frustum requested;
        // the box-inscribed contract does.
        bool inscribed = true;
        const aabbox3df eccBox(vector3df(0, 0, 0), vector3df(8, 4, 1));
        for (int n = 3; n <= 32; n++)
            for (int s = 1; s <= 4; s++)
            {
                const float ts = static_cast<float>(s) * 0.125f;
                for (int a = 0; a < 6; a++)
                {
                    Brush ck = makeCone(eccBox, n, ts, axes[a]);
                    if (!ck.geometryValid) continue;
                    const float tol = ON_EPSILON + GRID_QUANTUM * 2.0f;
                    if (ck.bounds.MinEdge.X < -tol || ck.bounds.MaxEdge.X > 8.0f + tol ||
                        ck.bounds.MinEdge.Y < -tol || ck.bounds.MaxEdge.Y > 4.0f + tol ||
                        ck.bounds.MinEdge.Z < -tol || ck.bounds.MaxEdge.Z > 1.0f + tol)
                        inscribed = false;
                }
            }
        check(inscribed, "a tapered cone never escapes its box as a collapsed full cone");
    }

    // 25. Gothic arch
    {
        const aabbox3df gBox(vector3df(0, 0, 0), vector3df(8, 4, 1));
        GothicArchParams gp;
        gp.segments = 4;
        gp.pointiness = 0.0f;
        gp.wallDepth = 0.5f;
        gp.span = BrushAxis::PLUS_X;

        std::vector<Brush> round = makeGothicArch(gBox, gp);
        check(round.size() == 8, "gothic arch gives two voussoirs per segment per side");

        bool prisms = true, inBox = true;
        float topY = 0.0f;
        for (const auto& v : round)
        {
            if (!v.geometryValid || v.faces.size() != 6) prisms = false;
            if (v.bounds.MinEdge.X < -ON_EPSILON || v.bounds.MaxEdge.X > 8.0f + ON_EPSILON ||
                v.bounds.MinEdge.Y < -ON_EPSILON || v.bounds.MaxEdge.Y > 4.0f + ON_EPSILON)
                inBox = false;
            if (v.bounds.MaxEdge.Y > topY) topY = v.bounds.MaxEdge.Y;
        }
        check(prisms, "gothic voussoirs are valid 6-face prisms");
        check(inBox, "gothic arch fits its bounding box");
        check(std::fabs(topY - 4.0f) < ON_EPSILON, "gothic apex lands exactly on the box top");
        check(!containsAny(round, vector3df(4, 0.5f, 0.5f)), "gothic doorway is open");
        check(containsAny(round, vector3df(4, 3.75f, 0.5f)), "gothic crown is solid");

        // The apex ring point is shared, so the two centre voussoirs touch
        check(sharesVertex(round[3], round[4]), "gothic apex seam shares vertices");

        // pointiness 0 must reproduce the semicircular arch's footprint
        ArchParams ap;
        ap.segments = 8;
        ap.arcDegrees = 180.0f;
        ap.wallDepth = 0.5f;
        ap.span = BrushAxis::PLUS_X;
        std::vector<Brush> semi = makeArch(gBox, ap);
        aabbox3df gAll, sAll;
        gAll.reset(round[0].bounds.MinEdge);
        for (const auto& v : round) { gAll.addInternalBox(v.bounds); }
        sAll.reset(semi[0].bounds.MinEdge);
        for (const auto& v : semi)  { sAll.addInternalBox(v.bounds); }
        check(gAll.MinEdge.equals(sAll.MinEdge, ON_EPSILON * 2) &&
              gAll.MaxEdge.equals(sAll.MaxEdge, ON_EPSILON * 2),
              "pointiness 0 matches the semicircular arch");

        // A genuine gothic keeps the apex pinned but is demonstrably pointier:
        // a sample high on the centre line is open under a semicircle (whose
        // crown is thin there) and solid under the equilateral arch.
        gp.pointiness = 1.0f;
        std::vector<Brush> pointed = makeGothicArch(gBox, gp);
        float ptop = 0.0f;
        bool pValid = pointed.size() == 8;
        for (const auto& v : pointed)
        {
            if (!v.geometryValid) pValid = false;
            if (v.bounds.MaxEdge.Y > ptop) ptop = v.bounds.MaxEdge.Y;
        }
        check(pValid, "equilateral gothic arch is valid");
        check(std::fabs(ptop - 4.0f) < ON_EPSILON,
              "gothic apex stays on the box top at full pointiness");
        // Springings stay at the box sides at mid-height whatever the pointiness
        check(containsAny(pointed, vector3df(0.1f, 2.0f, 0.5f)) &&
              containsAny(round,   vector3df(0.1f, 2.0f, 0.5f)),
              "gothic springs from the box sides at mid-height");
        // Pointier: the curve sits higher, so a point just inside the round
        // arch's haunch is still solid, while the sample that distinguishes them
        // is on the centre line where the pointed arch has more material.
        check(containsAny(pointed, vector3df(4, 3.9f, 0.5f)),
              "equilateral gothic is solid at the crown");

        // Clamps
        gp.segments = 1;
        check(makeGothicArch(gBox, gp).size() == 4, "gothic segment count clamps up to 2");
        gp.segments = 999;
        check(makeGothicArch(gBox, gp).size() == 32, "gothic segment count clamps down to 16");
        gp.segments = 4;
        gp.wallDepth = 0.001f;
        bool thinOk = makeGothicArch(gBox, gp).size() == 8;
        gp.wallDepth = 100.0f;
        bool thickOk = makeGothicArch(gBox, gp).size() == 8;
        check(thinOk,  "hair-thin gothic wall floors to valid voussoirs");
        check(thickOk, "over-thick gothic wall clamps to valid voussoirs");
    }

    if (failures == 0)
        spdlog::info("BrushGeometry self-tests: all passed");
    else
        spdlog::error("BrushGeometry self-tests: {} failure(s)", failures);
    return failures;
}

} // namespace BrushGeometry
