#include "NavigationManager.h"

#include <cstring>
#include <cmath>

#include "Engine/Navigation/Recast/Include/RecastAlloc.h"

NavigationManager* NavigationManager::s_Instance = nullptr;

// ---------------------------------------------------------------------------
// rcContext implementation — routes Recast logs through spdlog
// ---------------------------------------------------------------------------

class NavContext : public rcContext
{
protected:
    void doLog(const rcLogCategory category, const char* msg, const int /*len*/) override
    {
        switch (category)
        {
        case RC_LOG_WARNING: spdlog::warn("Recast: {}", msg); break;
        case RC_LOG_ERROR:   spdlog::error("Recast: {}", msg); break;
        default:             spdlog::info("Recast: {}", msg);  break;
        }
    }
};

// ---------------------------------------------------------------------------

NavigationManager::NavigationManager()
    : m_navMesh(nullptr), m_navQuery(nullptr), m_navStale(false)
{
    if (s_Instance)
    {
        spdlog::error("NavigationManager: duplicate instance");
        return;
    }
    s_Instance = this;
}

NavigationManager::~NavigationManager()
{
    destroyNavMesh();
    s_Instance = nullptr;
}

// ---------------------------------------------------------------------------
// Geometry accumulation
// ---------------------------------------------------------------------------

void NavigationManager::addMeshGeometry(irr::scene::IMesh* mesh,
                                        const irr::core::matrix4& transform)
{
    if (!mesh)
        return;

    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b)
    {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
        if (!buf)
            continue;

        const irr::u32 vertBase = static_cast<irr::u32>(m_verts.size() / 3);
        const irr::u32 vertCount = buf->getVertexCount();
        const irr::u8* rawVerts = static_cast<const irr::u8*>(buf->getVertices());
        const irr::u32 stride = irr::video::getVertexPitchFromType(buf->getVertexType());

        m_verts.reserve(m_verts.size() + vertCount * 3);
        for (irr::u32 v = 0; v < vertCount; ++v)
        {
            // Position is always the first field in every Irrlicht vertex type
            irr::core::vector3df pos =
                reinterpret_cast<const irr::video::S3DVertex*>(rawVerts + v * stride)->Pos;

            transform.transformVect(pos);

            m_verts.push_back(pos.X);
            m_verts.push_back(pos.Y);
            m_verts.push_back(pos.Z);
        }

        const irr::u32 indexCount = buf->getIndexCount();
        m_tris.reserve(m_tris.size() + indexCount);

        if (buf->getIndexType() == irr::video::EIT_32BIT)
        {
            const irr::u32* idx = reinterpret_cast<const irr::u32*>(buf->getIndices());
            for (irr::u32 i = 0; i < indexCount; ++i)
                m_tris.push_back(static_cast<int>(vertBase + idx[i]));
        }
        else
        {
            const irr::u16* idx = reinterpret_cast<const irr::u16*>(buf->getIndices());
            for (irr::u32 i = 0; i < indexCount; ++i)
                m_tris.push_back(static_cast<int>(vertBase + idx[i]));
        }
    }
}

void NavigationManager::clearGeometry()
{
    m_verts.clear();
    m_tris.clear();
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

bool NavigationManager::buildNavMesh(const NavMeshConfig& config)
{
    destroyNavMesh();

    if (m_verts.empty() || m_tris.empty())
    {
        spdlog::error("NavigationManager::buildNavMesh: no geometry added");
        return false;
    }

    const float* verts    = m_verts.data();
    const int    nverts   = static_cast<int>(m_verts.size() / 3);
    const int*   tris     = m_tris.data();
    const int    ntris    = static_cast<int>(m_tris.size() / 3);

    NavContext ctx;

    // ------------------------------------------------------------------
    // 1. Recast config
    // ------------------------------------------------------------------
    rcConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cs                 = config.cellSize;
    cfg.ch                 = config.cellHeight;
    cfg.walkableSlopeAngle = config.agentMaxSlope;
    cfg.walkableHeight     = static_cast<int>(ceilf(config.agentHeight   / cfg.ch));
    cfg.walkableClimb      = static_cast<int>(floorf(config.agentMaxClimb / cfg.ch));
    cfg.walkableRadius     = static_cast<int>(ceilf(config.agentRadius    / cfg.cs));
    cfg.maxEdgeLen         = static_cast<int>(config.edgeMaxLen / config.cellSize);
    cfg.maxSimplificationError = config.edgeMaxError;
    cfg.minRegionArea      = static_cast<int>(rcSqr(config.regionMinSize));
    cfg.mergeRegionArea    = static_cast<int>(rcSqr(config.regionMergeSize));
    cfg.maxVertsPerPoly    = config.vertsPerPoly;
    cfg.detailSampleDist   = config.detailSampleDist < 0.9f ? 0.0f
                             : config.cellSize * config.detailSampleDist;
    cfg.detailSampleMaxError = config.cellHeight * config.detailSampleMaxError;

    rcCalcBounds(verts, nverts, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    // ------------------------------------------------------------------
    // 2. Heightfield rasterization
    // ------------------------------------------------------------------
    rcHeightfield* hf = rcAllocHeightfield();
    if (!hf || !rcCreateHeightfield(&ctx, *hf, cfg.width, cfg.height,
                                    cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not create heightfield");
        rcFreeHeightField(hf);
        return false;
    }

    unsigned char* triareas = new unsigned char[ntris];
    memset(triareas, 0, ntris * sizeof(unsigned char));
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle,
                            verts, nverts, tris, ntris, triareas);
    if (!rcRasterizeTriangles(&ctx, verts, nverts, tris, triareas, ntris,
                              *hf, cfg.walkableClimb))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not rasterize triangles");
        delete[] triareas;
        rcFreeHeightField(hf);
        return false;
    }
    delete[] triareas;

    // ------------------------------------------------------------------
    // 3. Filtering
    // ------------------------------------------------------------------
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *hf);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *hf);

    // ------------------------------------------------------------------
    // 4. Compact heightfield
    // ------------------------------------------------------------------
    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight,
                                           cfg.walkableClimb, *hf, *chf))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not build compact heightfield");
        rcFreeCompactHeightfield(chf);
        rcFreeHeightField(hf);
        return false;
    }
    rcFreeHeightField(hf);

    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not erode walkable area");
        rcFreeCompactHeightfield(chf);
        return false;
    }

    // ------------------------------------------------------------------
    // 5. Regions
    // ------------------------------------------------------------------
    if (!rcBuildDistanceField(&ctx, *chf) ||
        !rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not build regions");
        rcFreeCompactHeightfield(chf);
        return false;
    }

    // ------------------------------------------------------------------
    // 6. Contours
    // ------------------------------------------------------------------
    rcContourSet* cset = rcAllocContourSet();
    if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError,
                                  cfg.maxEdgeLen, *cset))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not build contours");
        rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf);
        return false;
    }

    // ------------------------------------------------------------------
    // 7. Polygon mesh
    // ------------------------------------------------------------------
    rcPolyMesh* pmesh = rcAllocPolyMesh();
    if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not build poly mesh");
        rcFreePolyMesh(pmesh);
        rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf);
        return false;
    }

    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf,
                                         cfg.detailSampleDist,
                                         cfg.detailSampleMaxError, *dmesh))
    {
        spdlog::error("NavigationManager::buildNavMesh: could not build poly mesh detail");
        rcFreePolyMeshDetail(dmesh);
        rcFreePolyMesh(pmesh);
        rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf);
        return false;
    }

    rcFreeContourSet(cset);
    rcFreeCompactHeightfield(chf);

    // Mark all polygons walkable so Detour can traverse them
    for (int i = 0; i < pmesh->npolys; ++i)
        pmesh->flags[i] = 1;

    // ------------------------------------------------------------------
    // 8. Create Detour navmesh
    // ------------------------------------------------------------------
    dtNavMeshCreateParams params;
    memset(&params, 0, sizeof(params));
    params.verts            = pmesh->verts;
    params.vertCount        = pmesh->nverts;
    params.polys            = pmesh->polys;
    params.polyAreas        = pmesh->areas;
    params.polyFlags        = pmesh->flags;
    params.polyCount        = pmesh->npolys;
    params.nvp              = pmesh->nvp;
    params.detailMeshes     = dmesh->meshes;
    params.detailVerts      = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris       = dmesh->tris;
    params.detailTriCount   = dmesh->ntris;
    params.walkableHeight   = config.agentHeight;
    params.walkableRadius   = config.agentRadius;
    params.walkableClimb    = config.agentMaxClimb;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs          = cfg.cs;
    params.ch          = cfg.ch;
    params.buildBvTree = true;

    unsigned char* navData     = nullptr;
    int            navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
    {
        spdlog::error("NavigationManager::buildNavMesh: dtCreateNavMeshData failed");
        rcFreePolyMeshDetail(dmesh);
        rcFreePolyMesh(pmesh);
        return false;
    }

    rcFreePolyMeshDetail(dmesh);
    rcFreePolyMesh(pmesh);

    m_navMesh = dtAllocNavMesh();
    if (!m_navMesh)
    {
        spdlog::error("NavigationManager::buildNavMesh: dtAllocNavMesh failed");
        dtFree(navData);
        return false;
    }

    dtStatus status = m_navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(status))
    {
        spdlog::error("NavigationManager::buildNavMesh: dtNavMesh::init failed");
        dtFreeNavMesh(m_navMesh);
        m_navMesh = nullptr;
        return false;
    }

    m_navQuery = dtAllocNavMeshQuery();
    if (!m_navQuery)
    {
        spdlog::error("NavigationManager::buildNavMesh: dtAllocNavMeshQuery failed");
        dtFreeNavMesh(m_navMesh);
        m_navMesh = nullptr;
        return false;
    }

    status = m_navQuery->init(m_navMesh, config.maxPathNodes);
    if (dtStatusFailed(status))
    {
        spdlog::error("NavigationManager::buildNavMesh: dtNavMeshQuery::init failed");
        dtFreeNavMeshQuery(m_navQuery);
        dtFreeNavMesh(m_navMesh);
        m_navQuery = nullptr;
        m_navMesh  = nullptr;
        return false;
    }

    spdlog::info("NavigationManager: navmesh built ({} verts, {} tris)",
                 nverts, ntris);
    return true;
}

void NavigationManager::destroyNavMesh()
{
    if (m_navQuery) { dtFreeNavMeshQuery(m_navQuery); m_navQuery = nullptr; }
    if (m_navMesh)  { dtFreeNavMesh(m_navMesh);       m_navMesh  = nullptr; }

    // The single clear point for the stale flag: buildNavMesh() and
    // loadNavMesh() both call us first, so a fresh bake and a scene load
    // each come out clean without touching them.
    m_navStale = false;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

std::vector<uint8_t> NavigationManager::serializeNavMesh() const
{
    if (!m_navMesh)
        return {};

    // The navmesh built by buildNavMesh() is always a single tile (tile 0, layer 0).
    // Access via a const pointer so the compiler selects the public const overload.
    const dtNavMesh* constMesh = m_navMesh;
    const dtMeshTile* tile = constMesh->getTile(0);
    if (!tile || !tile->data || tile->dataSize <= 0)
        return {};

    const uint8_t* begin = reinterpret_cast<const uint8_t*>(tile->data);
    return std::vector<uint8_t>(begin, begin + tile->dataSize);
}

bool NavigationManager::loadNavMesh(const uint8_t* data, size_t size)
{
    destroyNavMesh();

    if (!data || size == 0)
    {
        spdlog::error("NavigationManager::loadNavMesh: empty data");
        return false;
    }

    // Detour will own and free this copy via DT_TILE_FREE_DATA
    uint8_t* copy = static_cast<uint8_t*>(dtAlloc(size, DT_ALLOC_PERM));
    if (!copy)
    {
        spdlog::error("NavigationManager::loadNavMesh: dtAlloc failed");
        return false;
    }
    memcpy(copy, data, size);

    m_navMesh = dtAllocNavMesh();
    if (!m_navMesh)
    {
        spdlog::error("NavigationManager::loadNavMesh: dtAllocNavMesh failed");
        dtFree(copy);
        return false;
    }

    dtStatus status = m_navMesh->init(copy, static_cast<int>(size), DT_TILE_FREE_DATA);
    if (dtStatusFailed(status))
    {
        spdlog::error("NavigationManager::loadNavMesh: dtNavMesh::init failed");
        dtFreeNavMesh(m_navMesh);
        m_navMesh = nullptr;
        return false;
    }

    m_navQuery = dtAllocNavMeshQuery();
    if (!m_navQuery)
    {
        spdlog::error("NavigationManager::loadNavMesh: dtAllocNavMeshQuery failed");
        dtFreeNavMesh(m_navMesh);
        m_navMesh = nullptr;
        return false;
    }

    status = m_navQuery->init(m_navMesh, 2048);
    if (dtStatusFailed(status))
    {
        spdlog::error("NavigationManager::loadNavMesh: dtNavMeshQuery::init failed");
        dtFreeNavMeshQuery(m_navQuery);
        dtFreeNavMesh(m_navMesh);
        m_navQuery = nullptr;
        m_navMesh  = nullptr;
        return false;
    }

    spdlog::info("NavigationManager::loadNavMesh: loaded {} bytes", size);
    return true;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<irr::core::vector3df> NavigationManager::findPath(
    const irr::core::vector3df& start,
    const irr::core::vector3df& end,
    int maxStraightPath) const
{
    if (!m_navMesh || !m_navQuery)
        return {};

    const float halfExtents[3] = { 2.0f, 4.0f, 2.0f };
    const float startPos[3]    = { start.X, start.Y, start.Z };
    const float endPos[3]      = { end.X,   end.Y,   end.Z   };

    dtPolyRef startRef = 0, endRef = 0;
    m_navQuery->findNearestPoly(startPos, halfExtents, &m_filter, &startRef, nullptr);
    m_navQuery->findNearestPoly(endPos,   halfExtents, &m_filter, &endRef,   nullptr);

    if (!startRef || !endRef)
        return {};

    static const int MAX_POLYS = 256;
    dtPolyRef path[MAX_POLYS];
    int       pathCount = 0;

    dtStatus status = m_navQuery->findPath(
        startRef, endRef, startPos, endPos,
        &m_filter, path, &pathCount, MAX_POLYS);

    if (dtStatusFailed(status) || pathCount == 0)
        return {};

    const int clampedMax = maxStraightPath > 0 ? maxStraightPath : MAX_POLYS;

    std::vector<float>         straightVerts(clampedMax * 3);
    std::vector<unsigned char> straightFlags(clampedMax);
    std::vector<dtPolyRef>     straightRefs(clampedMax);
    int straightCount = 0;

    m_navQuery->findStraightPath(
        startPos, endPos,
        path, pathCount,
        straightVerts.data(), straightFlags.data(), straightRefs.data(),
        &straightCount, clampedMax);

    std::vector<irr::core::vector3df> result;
    result.reserve(straightCount);
    for (int i = 0; i < straightCount; ++i)
    {
        result.emplace_back(
            straightVerts[i * 3 + 0],
            straightVerts[i * 3 + 1],
            straightVerts[i * 3 + 2]);
    }
    return result;
}

bool NavigationManager::findNearestPoint(const irr::core::vector3df& pos,
                                         irr::core::vector3df& nearest) const
{
    if (!m_navMesh || !m_navQuery)
        return false;

    const float halfExtents[3] = { 2.0f, 4.0f, 2.0f };
    const float inPos[3]       = { pos.X, pos.Y, pos.Z };

    dtPolyRef ref = 0;
    float     nearestPt[3];

    dtStatus status = m_navQuery->findNearestPoly(
        inPos, halfExtents, &m_filter, &ref, nearestPt);

    if (dtStatusFailed(status) || !ref)
        return false;

    nearest = { nearestPt[0], nearestPt[1], nearestPt[2] };
    return true;
}
