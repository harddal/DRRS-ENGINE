#pragma once

// ---------------------------------------------------------------------------
// FractureGeometry — voronoi cell generation and mesh clipping.
//
// Split out of FractureManager so the geometry is testable on its own and the
// manager stays about pooling and physics.
//
// The approach, and why it is this one:
//
//   A voronoi cell is just a CONVEX POLYHEDRON — the AABB's six half-spaces
//   plus one perpendicular-bisector half-space against every other seed. So the
//   whole thing reduces to "clip geometry against a list of planes", which is
//   Sutherland-Hodgman with interpolated vertex attributes.
//
//   Phase 1 built boxes from scratch and invented planar UVs, which threw the
//   source model's real UV layout away. Here the SOURCE TRIANGLES are clipped
//   instead, so every fragment keeps the original texture mapping, interpolated
//   across the cut edges. That is the whole reason phase 2 exists.
//
//   The cut faces themselves have no source geometry to inherit from, so they
//   are capped: the vertices created on a given plane are welded, sorted around
//   their centroid in that plane, and fanned. Exact for a convex cross-section,
//   which is what a convex cell through a mostly-convex prop gives.
//
// NOT reused: BrushGeometry's clipBrush. It does the same operation but snaps
// every plane point to GRID_QUANTUM, which would destroy a sub-unit prop.
// ---------------------------------------------------------------------------

#include <vector>

#include "irrlicht.h"

namespace FractureGeometry
{
	// Half-space. A point is INSIDE the cell when normal.dot(p) + d <= 0, so
	// 'normal' points OUT of the cell — the same sense as a brush face plane.
	struct Plane
	{
		irr::core::vector3df normal;
		float d = 0.0f;

		// False for the six AABB planes. They BOUND the mesh, they never slice
		// through it, so no cut face can ever exist on them — and a prop's own
		// surface routinely lies exactly ON them (a cube's AABB is the cube).
		// Capping those would lay duplicate geometry over the real faces.
		bool cappable = true;

		float distance(const irr::core::vector3df& p) const
		{
			return normal.dotProduct(p) + d;
		}
	};

	// One cell's result: clipped geometry plus where it ended up.
	//
	// The mesh carries buffer 0 = OUTER SKIN (clipped source triangles, keeps the
	// model's own UVs) and, when 'hasInterior' is set, buffer 1 = CUT FACES, whose
	// UVs are a world-scale planar projection meant for an interior material.
	// Two buffers rather than one is what lets a shard bind two textures.
	struct Cell
	{
		irr::scene::SMesh*   mesh = nullptr;   // re-centred on 'centroid'
		irr::core::vector3df centroid;         // in the SOURCE mesh's local space
		bool                 hasInterior = false;
	};

	// Scatter 'count' seed points inside 'box'.
	//
	// Rejection-sampled against a minimum separation so two seeds cannot land on
	// top of each other — near-coincident seeds produce a bisector that slices a
	// wafer off the prop, which reads as a glitch rather than a shard.
	void scatterSeeds(const irr::core::aabbox3df& box, int count,
	                  std::vector<irr::core::vector3df>& out);

	// Half-spaces bounding seed 'index': the six box faces, plus the bisector
	// against every other seed.
	void buildCellPlanes(const std::vector<irr::core::vector3df>& seeds, size_t index,
	                     const irr::core::aabbox3df& box,
	                     std::vector<Plane>& out);

	// Clip every triangle of 'src' to the half-space set, cap the cut faces, and
	// return the fragment re-centred on its own centroid.
	//
	// Returns false when the cell contains no geometry — which is normal and
	// expected: seeds land in the hollow of a non-convex prop, or outside the
	// mesh but inside its AABB. The caller drops those cells.
	// 'capCuts' closes the cut faces so a fragment is a solid with an interior.
	// Pass FALSE for a mesh modelled as a thin shell (a barrel, a pipe): there is
	// no interior to expose, and capping stretches a membrane across the
	// fragment's boundary that renders as a stray flat plate.
	bool clipMeshToCell(irr::scene::IMesh* src,
	                    const std::vector<Plane>& planes,
	                    Cell& out, bool capCuts);
}
