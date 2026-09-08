#pragma once

#include <cstdint>
#include <vector>

#include <irrlicht.h>
#include <spdlog/spdlog.h>

#include "Engine/Navigation/Recast/Include/Recast.h"
#include "Engine/Navigation/Detour/Include/DetourNavMesh.h"
#include "Engine/Navigation/Detour/Include/DetourNavMeshQuery.h"
#include "Engine/Navigation/Detour/Include/DetourNavMeshBuilder.h"

struct NavMeshConfig
{
    float cellSize;           // XZ voxel size (smaller = more detail, more cost)
    float cellHeight;         // Y voxel size
    float agentHeight;        // Minimum passable ceiling height
    float agentRadius;        // Agent capsule radius (erodes walkable edges)
    float agentMaxClimb;      // Maximum step height
    float agentMaxSlope;      // Maximum walkable slope in degrees
    int   regionMinSize;      // Minimum region island size (in cells)
    int   regionMergeSize;    // Region merge threshold
    float edgeMaxLen;         // Maximum contour edge length
    float edgeMaxError;       // Contour simplification error
    int   vertsPerPoly;       // Max vertices per nav poly (max 6)
    float detailSampleDist;   // Detail mesh sample distance
    float detailSampleMaxError;
    int   maxPathNodes;       // dtNavMeshQuery node pool size

    // These defaults ARE the bake: the editor's Bake button calls
    // buildNavMesh() with no argument (EditorInterface_Toolbar.cpp:308) and
    // nothing anywhere constructs a modified NavMeshConfig. Change a number
    // here and the next bake uses it -- there is no UI to check against.
    //
    // Three were retuned for route quality; the reasoning is per-field so a
    // future change knows what it is undoing.
    NavMeshConfig() :
        cellSize(0.3f), cellHeight(0.2f),
        agentHeight(1.8f), agentRadius(0.35f), agentMaxClimb(0.4f), agentMaxSlope(45.0f),

        // WAS 8. minRegionArea is rcSqr(regionMinSize), so 8 meant 64 cells --
        // small enough that scraps survive as disconnected islands, and an
        // agent whose findNearestPoly lands on one is stranded there with every
        // path request failing. 20 -> 400 cells (~36 sq units at cellSize 0.3),
        // which is still smaller than any room but too big for a scrap.
        regionMinSize(20), regionMergeSize(20),

        // WAS 1.3. In CELLS, so at cellSize 0.3 that was ~0.4u of contour slop:
        // corners get shaved off the mesh and corridors pinch, and agents cut
        // wide around doorways because the mesh genuinely is narrower there
        // than the geometry. 1.0 is ~0.3u.
        edgeMaxLen(12.0f), edgeMaxError(1.0f),

        vertsPerPoly(6),

        // WAS 6.0. The detail mesh is what the corridor reports Y from, and at
        // 6 units between samples that Y drifts well off the real floor on any
        // slope. 2.5 is the Recast demo's own default.
        detailSampleDist(2.5f), detailSampleMaxError(1.0f),

        maxPathNodes(2048) {}
};

class NavigationManager
{
public:
    NavigationManager();
    ~NavigationManager();

	static void destroy() {
		delete s_Instance;
		s_Instance = nullptr;
	}

    // --- Geometry accumulation ---
    // Add an Irrlicht mesh to the input geometry pool. Apply a world transform if the
    // mesh is not already in world space. Call buildNavMesh() once all geometry is added.
    void addMeshGeometry(irr::scene::IMesh* mesh,
                         const irr::core::matrix4& transform = irr::core::matrix4());

    void clearGeometry();

    // --- Build ---
    // Runs the full Recast pipeline over accumulated geometry and initialises the
    // Detour navmesh + query. Returns false on failure.
    bool buildNavMesh(const NavMeshConfig& config = NavMeshConfig());
    void destroyNavMesh();

    bool isNavMeshBuilt() const { return m_navMesh != nullptr; }

    // A built navmesh whose source geometry has changed since the bake.
    // Meaningless with no navmesh, so the getter folds in the built check.
    bool isNavMeshStale() const { return m_navMesh != nullptr && m_navStale; }
    void markNavMeshStale() { if (m_navMesh) m_navStale = true; }

    // --- Serialization ---
    // Returns the raw Detour tile bytes for the single tile built by buildNavMesh().
    // Returns an empty vector if no navmesh is built.
    std::vector<uint8_t> serializeNavMesh() const;

    // Skips the Recast pipeline entirely — initialises the navmesh directly from
    // bytes previously produced by serializeNavMesh(). Returns false on failure.
    bool loadNavMesh(const uint8_t* data, size_t size);

    // --- Queries ---
    // Returns the straight-line waypoint path from start to end. The first waypoint
    // is the clamped start position on the mesh and the last is the clamped end.
    // Returns an empty vector if no path exists or the navmesh is not built.
    std::vector<irr::core::vector3df> findPath(
        const irr::core::vector3df& start,
        const irr::core::vector3df& end,
        int maxStraightPath = 256) const;

    // Snaps pos to the nearest point on the navmesh. Returns false if the navmesh
    // is not built or no polygon is found within the search extents.
    bool findNearestPoint(const irr::core::vector3df& pos,
                          irr::core::vector3df& nearest) const;

    // A random reachable point on the navmesh within 'radius' of 'pos'.
    //
    // Reachable, not merely nearby: Detour walks the poly graph outward from
    // the poly under 'pos', so the result is never on the far side of a wall
    // the way a random offset plus findNearestPoint would be. That is the whole
    // reason to have this rather than roll it at the call site.
    //
    // Returns false with no navmesh, or when 'pos' is not on the mesh at all.
    // Callers must handle that -- an unbaked scene is a supported configuration
    // everywhere else in this class and this is no exception.
    bool randomPointNear(const irr::core::vector3df& pos, float radius,
                         irr::core::vector3df& outPoint) const;

    // --- Raw Detour access -------------------------------------------------
    // For CrowdManager (dtCrowd::init needs the mesh) and for anything that
    // wants a navmesh raycast rather than a physics one.
    //
    // BOTH POINTERS ARE INVALIDATED BY destroyNavMesh(), which runs at the top
    // of buildNavMesh() AND loadNavMesh() -- and importScene() calls loadNavMesh
    // on every scene import, so play-in-editor invalidates them twice per play
    // session. Never cache either of these across a frame. See CrowdManager.
    dtNavMesh*           navMesh()  const { return m_navMesh;  }
    dtNavMeshQuery*      navQuery() const { return m_navQuery; }
    const dtQueryFilter& filter()   const { return m_filter;   }

    // World-space AABB of the built navmesh, read off tile 0's header. Only
    // meaningful while isNavMeshBuilt(); zero otherwise.
    const irr::core::vector3df& boundsMin() const { return m_boundsMin; }
    const irr::core::vector3df& boundsMax() const { return m_boundsMax; }

    static NavigationManager* Get() { return s_Instance; }

private:
    // Reads tile 0's header bounds into m_boundsMin/Max. Shared by buildNavMesh
    // and loadNavMesh so a loaded mesh reports the same bounds as a baked one.
    void captureBounds();

    static NavigationManager* s_Instance;

    // Accumulated raw geometry (world-space, ready for Recast)
    std::vector<float> m_verts;  // Flat XYZ float array
    std::vector<int>   m_tris;   // Flat index triplets

    dtNavMesh*      m_navMesh;
    dtNavMeshQuery* m_navQuery;
    dtQueryFilter   m_filter;
    bool            m_navStale;  // geometry edited since the last bake/load

    irr::core::vector3df m_boundsMin;
    irr::core::vector3df m_boundsMax;
};
