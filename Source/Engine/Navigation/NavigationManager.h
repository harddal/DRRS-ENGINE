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

    NavMeshConfig() :
        cellSize(0.3f), cellHeight(0.2f),
        agentHeight(1.8f), agentRadius(0.35f), agentMaxClimb(0.4f), agentMaxSlope(45.0f),
        regionMinSize(8), regionMergeSize(20),
        edgeMaxLen(12.0f), edgeMaxError(1.3f),
        vertsPerPoly(6),
        detailSampleDist(6.0f), detailSampleMaxError(1.0f),
        maxPathNodes(2048) {}
};

class NavigationManager
{
public:
    NavigationManager();
    ~NavigationManager();

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

    static NavigationManager* Get() { return s_Instance; }

private:
    static NavigationManager* s_Instance;

    // Accumulated raw geometry (world-space, ready for Recast)
    std::vector<float> m_verts;  // Flat XYZ float array
    std::vector<int>   m_tris;   // Flat index triplets

    dtNavMesh*      m_navMesh;
    dtNavMeshQuery* m_navQuery;
    dtQueryFilter   m_filter;
};
