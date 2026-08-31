#include "FractureGeometry.h"

#include <algorithm>
#include <cmath>

#include "Engine/Engine.h"

namespace
{
	using namespace irr;

	// Point-on-plane tolerance. Deliberately generous relative to typical prop
	// scale: the cost of treating a near-plane vertex as ON the plane is a
	// degenerate sliver triangle nobody sees, while the cost of treating it as
	// across the plane is a crack between neighbouring shards.
	constexpr float _on_epsilon = 1.0e-4f;

	// Interior UV repeat, in tiles per world unit of cut surface. Scaling by
	// world size rather than fitting 0..1 to each face is what makes grain read
	// as running THROUGH the solid: two shards of different size show the same
	// texel density, the way real broken material does.
	constexpr float _interior_uv_scale = 1.6f;

	// Full vertex payload carried through the clip so fragments keep the source
	// model's texture mapping.
	struct ClipVertex
	{
		core::vector3df pos;
		core::vector3df normal;
		core::vector2df uv;
	};

	ClipVertex lerpVertex(const ClipVertex& a, const ClipVertex& b, float t)
	{
		ClipVertex r;
		r.pos    = a.pos    + (b.pos    - a.pos)    * t;
		r.normal = a.normal + (b.normal - a.normal) * t;
		r.uv     = a.uv     + (b.uv     - a.uv)     * t;

		if (r.normal.getLengthSQ() > 1.0e-8f)
			r.normal.normalize();

		return r;
	}

	// Sutherland-Hodgman against one half-space, keeping the inside (dist <= 0).
	void clipPolygon(const std::vector<ClipVertex>& in,
	                 const FractureGeometry::Plane& plane,
	                 std::vector<ClipVertex>& out)
	{
		out.clear();

		const size_t n = in.size();
		if (n < 3)
			return;

		for (size_t i = 0; i < n; ++i)
		{
			const ClipVertex& a = in[i];
			const ClipVertex& b = in[(i + 1) % n];

			const float da = plane.distance(a.pos);
			const float db = plane.distance(b.pos);

			if (da <= _on_epsilon)
				out.push_back(a);

			if ((da < -_on_epsilon && db > _on_epsilon) ||
			    (da > _on_epsilon && db < -_on_epsilon))
			{
				out.push_back(lerpVertex(a, b, da / (da - db)));
			}
		}
	}

	// Tangent basis for a plane normal, well-conditioned for any orientation.
	void planeBasis(const core::vector3df& n, core::vector3df& t1, core::vector3df& t2)
	{
		core::vector3df up(0.0f, 1.0f, 0.0f);

		if (std::fabs(n.Y) > 0.9f)
			up.set(1.0f, 0.0f, 0.0f);

		t1 = n.crossProduct(up);
		t1.normalize();

		t2 = n.crossProduct(t1);
		t2.normalize();
	}

	// The cell's own face on plane 'index': a quad lying in that plane, clipped
	// by every OTHER plane of the cell.
	//
	// This is how a cut face MUST be built. The obvious alternative — harvest
	// edges from the clipped source triangles and chain them into a loop — can
	// never work, and the reason is structural: a cap is bounded partly by the
	// mesh surface and partly by the neighbouring CUT PLANES, and those second
	// edges lie INSIDE the solid. No surface triangle contains them, so they
	// cannot be recovered from surface geometry at any tolerance. Every cap came
	// out one edge short, never closed, and got discarded — worse the more cells
	// there were, because more cells means more cap-meets-cap adjacency.
	//
	// Clipping a convex polygon, by contrast, yields a closed ordered polygon by
	// construction: no welding, no chaining, no open loops, no slivers.
	void buildCellFace(const std::vector<FractureGeometry::Plane>& planes, size_t index,
	                   float reach,
	                   std::vector<ClipVertex>& poly,
	                   std::vector<ClipVertex>& scratch)
	{
		const FractureGeometry::Plane& p = planes[index];

		core::vector3df t1, t2;
		planeBasis(p.normal, t1, t2);

		// Any point on the plane; the plane equation gives the closest one to
		// the origin directly.
		const core::vector3df origin = p.normal * (-p.d);

		poly.clear();

		const core::vector3df corner[4] = {
			origin + t1 * reach + t2 * reach,
			origin - t1 * reach + t2 * reach,
			origin - t1 * reach - t2 * reach,
			origin + t1 * reach - t2 * reach
		};

		for (const auto& c : corner)
		{
			ClipVertex v;
			v.pos    = c;
			v.normal = p.normal;
			poly.push_back(v);
		}

		for (size_t q = 0; q < planes.size() && poly.size() >= 3; ++q)
		{
			if (q == index)
				continue;

			clipPolygon(poly, planes[q], scratch);
			poly.swap(scratch);
		}
	}
}

namespace FractureGeometry
{

void scatterSeeds(const irr::core::aabbox3df& box, int count,
                  std::vector<irr::core::vector3df>& out)
{
	using namespace irr;

	auto* rng = Engine::Get()->rng();

	const core::vector3df ext = box.getExtent();

	float smallest = ext.X;
	if (ext.Y < smallest) smallest = ext.Y;
	if (ext.Z < smallest) smallest = ext.Z;

	// Seeds closer than this fraction of the smallest box axis get rejected —
	// a near-coincident pair bisects into a wafer, which reads as a glitch.
	const float minSep   = smallest * 0.18f;
	const float minSepSq = minSep * minSep;

	out.clear();
	out.reserve(count);

	// Bounded retries: on a very flat prop the separation test can be
	// unsatisfiable, and a fixed budget degrades to "accept what we have"
	// rather than spinning.
	const int maxAttempts = count * 24;

	for (int attempt = 0; attempt < maxAttempts && static_cast<int>(out.size()) < count; ++attempt)
	{
		const core::vector3df p(
			rng->getFloat(box.MinEdge.X, box.MaxEdge.X),
			rng->getFloat(box.MinEdge.Y, box.MaxEdge.Y),
			rng->getFloat(box.MinEdge.Z, box.MaxEdge.Z));

		bool tooClose = false;

		for (const auto& s : out)
		{
			if ((s - p).getLengthSQ() < minSepSq)
			{
				tooClose = true;
				break;
			}
		}

		if (!tooClose)
			out.push_back(p);
	}

	// Never return an empty set — one seed means one shard, which is a valid
	// (if dull) break, whereas zero means the caller silently does nothing.
	if (out.empty())
		out.push_back(box.getCenter());
}

void buildCellPlanes(const std::vector<irr::core::vector3df>& seeds, size_t index,
                     const irr::core::aabbox3df& box,
                     std::vector<Plane>& out)
{
	using namespace irr;

	out.clear();
	out.reserve(seeds.size() + 5);

	// The six box faces, outward normals. These stop a cell running off to
	// infinity in the directions no other seed bounds.
	const core::vector3df axes[6] = {
		{ -1.0f,  0.0f,  0.0f }, {  1.0f,  0.0f,  0.0f },
		{  0.0f, -1.0f,  0.0f }, {  0.0f,  1.0f,  0.0f },
		{  0.0f,  0.0f, -1.0f }, {  0.0f,  0.0f,  1.0f }
	};

	const float extremes[6] = {
		-box.MinEdge.X,  box.MaxEdge.X,
		-box.MinEdge.Y,  box.MaxEdge.Y,
		-box.MinEdge.Z,  box.MaxEdge.Z
	};

	for (int i = 0; i < 6; ++i)
	{
		Plane p;
		p.normal   = axes[i];
		p.d        = -extremes[i];

		// Not cappable: the box BOUNDS the mesh, it never slices through it, so
		// no cut face can exist on it. (For a cube the AABB *is* the mesh, and
		// capping these would lay duplicate geometry over the real faces.)
		p.cappable = false;

		out.push_back(p);
	}

	// Perpendicular bisector against every other seed: the half-space of points
	// nearer to seeds[index] than to seeds[j]. That set of half-spaces IS the
	// voronoi cell — no explicit diagram construction needed.
	const core::vector3df& si = seeds[index];

	for (size_t j = 0; j < seeds.size(); ++j)
	{
		if (j == index)
			continue;

		core::vector3df n = seeds[j] - si;

		if (n.getLengthSQ() < 1.0e-8f)
			continue;

		n.normalize();

		const core::vector3df mid = (si + seeds[j]) * 0.5f;

		Plane p;
		p.normal = n;
		p.d      = -n.dotProduct(mid);
		out.push_back(p);
	}
}

}   // namespace FractureGeometry

namespace FractureGeometry
{

bool clipMeshToCell(irr::scene::IMesh* src,
                    const std::vector<Plane>& planes,
                    Cell& out, bool capCuts)
{
	using namespace irr;

	if (!src || planes.empty())
		return false;

	// TWO buffers: buffer 0 is the outer surface clipped from the source
	// geometry and keeps the model's own texture; buffer 1 is the cut faces,
	// which take the profile's interior material. A single buffer could only
	// ever bind one texture.
	auto* skin     = new scene::SMeshBuffer();
	auto* interior = new scene::SMeshBuffer();

	std::vector<ClipVertex> poly, clipped;

	poly.reserve(16);
	clipped.reserve(16);

	// --- Outer skin: clip the source triangles -------------------------------
	for (u32 b = 0; b < src->getMeshBufferCount(); ++b)
	{
		scene::IMeshBuffer* mb = src->getMeshBuffer(b);
		if (!mb)
			continue;

		const u32 idxCount = mb->getIndexCount();
		const bool is32    = (mb->getIndexType() == video::EIT_32BIT);

		const void* rawIdx = mb->getIndices();
		if (!rawIdx)
			continue;

		for (u32 i = 0; i + 2 < idxCount; i += 3)
		{
			u32 tri[3];

			for (int c = 0; c < 3; ++c)
			{
				tri[c] = is32
					? static_cast<const u32*>(rawIdx)[i + c]
					: static_cast<const u16*>(rawIdx)[i + c];
			}

			poly.clear();

			for (int c = 0; c < 3; ++c)
			{
				ClipVertex v;
				v.pos    = mb->getPosition(tri[c]);
				v.normal = mb->getNormal(tri[c]);
				v.uv     = mb->getTCoords(tri[c]);
				poly.push_back(v);
			}

			// Sequential half-space clip. Sutherland-Hodgman preserves the input
			// winding, so the fragment keeps the source model's facing.
			for (size_t p = 0; p < planes.size() && poly.size() >= 3; ++p)
			{
				clipPolygon(poly, planes[p], clipped);
				poly.swap(clipped);
			}

			if (poly.size() < 3)
				continue;

			// u16 index buffer — a shard should never come close, but a dense
			// source mesh with a generous cell could, and silently wrapping
			// would corrupt the whole fragment.
			if (skin->Vertices.size() + poly.size() > 65000)
				break;

			const u16 base = static_cast<u16>(skin->Vertices.size());

			for (const auto& v : poly)
			{
				skin->Vertices.push_back(video::S3DVertex(
					v.pos, v.normal, video::SColor(255, 255, 255, 255), v.uv));
			}

			// Fan, original winding preserved (NOT reversed — this geometry came
			// in already wound correctly).
			for (size_t f = 1; f + 1 < poly.size(); ++f)
			{
				skin->Indices.push_back(base);
				skin->Indices.push_back(static_cast<u16>(base + f));
				skin->Indices.push_back(static_cast<u16>(base + f + 1));
			}
		}
	}

	// A cell with no surface in it holds no fragment: either a seed landed in
	// the hollow of a non-convex prop, or inside the AABB but outside the mesh.
	// Checked BEFORE building caps, so an empty cell cannot emit cut faces
	// floating in mid-air.
	if (skin->Vertices.size() < 3 || skin->Indices.size() < 3)
	{
		skin->drop();
		interior->drop();
		return false;
	}

	// --- Cut faces: built from the CELL, not harvested from the surface -------
	// See buildCellFace() for why harvesting cannot work.
	const float reach = src->getBoundingBox().getExtent().getLength() * 2.0f;

	for (size_t p = 0; capCuts && p < planes.size(); ++p)
	{
		if (!planes[p].cappable)
			continue;

		buildCellFace(planes, p, reach, poly, clipped);

		if (poly.size() < 3)
			continue;

		core::vector3df centre(0.0f, 0.0f, 0.0f);
		for (const auto& v : poly)
			centre += v.pos;
		centre /= static_cast<float>(poly.size());

		// Clipping preserves the seed quad's orientation, which has no reason to
		// match the plane normal. Measure rather than reason: the signed area
		// about the normal says which way this face actually turns.
		float area2 = 0.0f;
		for (size_t f = 0; f < poly.size(); ++f)
		{
			const core::vector3df d0 = poly[f].pos - centre;
			const core::vector3df d1 = poly[(f + 1) % poly.size()].pos - centre;

			area2 += d0.crossProduct(d1).dotProduct(planes[p].normal);
		}

		// Degenerate face — a plane that only grazes the cell.
		if (std::fabs(area2) < 1.0e-8f)
			continue;

		if (area2 < 0.0f)
			std::reverse(poly.begin(), poly.end());

		if (interior->Vertices.size() + poly.size() + 1 > 65000)
			break;

		core::vector3df t1, t2;
		planeBasis(planes[p].normal, t1, t2);

		const u16 centreIdx = static_cast<u16>(interior->Vertices.size());

		interior->Vertices.push_back(video::S3DVertex(
			centre, planes[p].normal, video::SColor(255, 255, 255, 255),
			core::vector2df(0.0f, 0.0f)));

		for (const auto& v : poly)
		{
			const core::vector3df d = v.pos - centre;

			interior->Vertices.push_back(video::S3DVertex(
				v.pos, planes[p].normal, video::SColor(255, 255, 255, 255),
				core::vector2df(d.dotProduct(t1) * _interior_uv_scale,
				                d.dotProduct(t2) * _interior_uv_scale)));
		}

		// A front face is the one whose RIGHT-HAND normal points outward
		// (derived from Irrlicht's createCubeMesh: its -Z quad a=(0,0,0)
		// b=(1,0,0) c=(1,1,0) is emitted (a,c,b), RH normal (0,0,-1) = the
		// outward normal). The face is now wound CCW about +normal and plane
		// normals point OUT of the cell, so this order is already correct.
		for (size_t f = 0; f < poly.size(); ++f)
		{
			const u16 a = static_cast<u16>(centreIdx + 1 + f);
			const u16 c = static_cast<u16>(centreIdx + 1 + ((f + 1) % poly.size()));

			interior->Indices.push_back(centreIdx);
			interior->Indices.push_back(a);
			interior->Indices.push_back(c);
		}
	}

	skin->recalculateBoundingBox();

	core::aabbox3df bounds = skin->getBoundingBox();

	const bool hasInterior = (interior->Vertices.size() >= 3 && interior->Indices.size() >= 3);

	if (hasInterior)
	{
		interior->recalculateBoundingBox();
		bounds.addInternalBox(interior->getBoundingBox());
	}

	// Re-centre on the fragment's own middle so it tumbles about itself rather
	// than pivoting around the prop's origin. Bounding-box centre rather than a
	// vertex average: the average drifts toward whichever face got tessellated
	// most finely, which makes a shard spin lopsidedly.
	const core::vector3df centroid = bounds.getCenter();

	for (u32 v = 0; v < skin->Vertices.size(); ++v)
		skin->Vertices[v].Pos -= centroid;

	skin->recalculateBoundingBox();

	auto* mesh = new scene::SMesh();
	mesh->addMeshBuffer(skin);
	skin->drop();

	if (hasInterior)
	{
		for (u32 v = 0; v < interior->Vertices.size(); ++v)
			interior->Vertices[v].Pos -= centroid;

		interior->recalculateBoundingBox();

		mesh->addMeshBuffer(interior);
	}

	interior->drop();

	mesh->recalculateBoundingBox();

	out.mesh        = mesh;
	out.centroid    = centroid;
	out.hasInterior = hasInterior;

	return true;
}

}   // namespace FractureGeometry
