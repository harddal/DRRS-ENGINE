#include "Engine/Brush/BrushGeometry.h"

#include <algorithm>
#include <cmath>

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

Brush makeCylinder(const aabbox3df& box, int sides, const std::string& material)
{
    Brush b;
    if (sides < 3) sides = 3;
    if (sides > 32) sides = 32;

    const vector3df m = quantize(box.MinEdge);
    const vector3df M = quantize(box.MaxEdge);
    const float cx = (m.X + M.X) * 0.5f;
    const float cz = (m.Z + M.Z) * 0.5f;
    const float rx = (M.X - m.X) * 0.5f;
    const float rz = (M.Z - m.Z) * 0.5f;
    const vector3df c(cx, (m.Y + M.Y) * 0.5f, cz);

    // Polygon corners are snapped to the grid FIRST so the side planes exactly
    // reproduce them on rebuild.
    std::vector<vector3df> bottom(sides);
    for (int i = 0; i < sides; i++)
    {
        const float phi = 2.0f * PI * static_cast<float>(i) / static_cast<float>(sides);
        bottom[i] = quantize(vector3df(cx + std::cos(phi) * rx, m.Y, cz + std::sin(phi) * rz));
    }

    for (int i = 0; i < sides; i++)
    {
        const vector3df& b0 = bottom[i];
        const vector3df& b1 = bottom[(i + 1) % sides];
        if (b0.getDistanceFromSQ(b1) < GRID_QUANTUM * GRID_QUANTUM)
            continue;   // radius too small for this side count; neighbors merged by snapping
        const vector3df t0(b0.X, M.Y, b0.Z);
        b.faces.push_back(makeFace(b0, t0, b1, material));
    }

    // Caps
    b.faces.push_back(makeFace({ m.X, M.Y, m.Z }, { m.X, M.Y, M.Z }, { M.X, M.Y, m.Z }, material)); // +Y
    b.faces.push_back(makeFace({ m.X, m.Y, m.Z }, { M.X, m.Y, m.Z }, { m.X, m.Y, M.Z }, material)); // -Y

    for (auto& f : b.faces)
        orientOutward(f, c);
    rebuild(b);
    return b;
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

std::vector<Brush> carve(const Brush& target, const Brush& carver)
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
        // Reuse the carver face's exact quantized points for the cut face
        BrushFace proto = cutProto;
        proto.planePoints[0] = cf.planePoints[0];
        proto.planePoints[1] = cf.planePoints[1];
        proto.planePoints[2] = cf.planePoints[2];
        proto.plane = cf.plane;
        initFaceUV(proto);

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

    if (failures == 0)
        spdlog::info("BrushGeometry self-tests: all passed");
    else
        spdlog::error("BrushGeometry self-tests: {} failure(s)", failures);
    return failures;
}

} // namespace BrushGeometry
