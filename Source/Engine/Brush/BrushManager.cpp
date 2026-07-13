#include "Engine/Brush/BrushManager.h"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

#include "PxPhysicsAPI.h"

#include "Engine/Brush/BrushCompiler.h"
#include "Engine/Brush/BrushGeometry.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/Lightmapper/LightmapBaker.h"

using namespace irr;
using namespace core;

BrushManager* BrushManager::s_Instance = nullptr;

BrushManager::BrushManager()
{
    s_Instance = this;
}

BrushManager::~BrushManager()
{
    clearAll();
    s_Instance = nullptr;
}

// ---------------------------------------------------------------------------
// Chunk keying
// ---------------------------------------------------------------------------

irr::s64 BrushManager::chunkKeyFor(const vector3df& centroid) const
{
    const s64 x = static_cast<s64>(std::floor(centroid.X / m_cellSize));
    const s64 y = static_cast<s64>(std::floor(centroid.Y / m_cellSize));
    const s64 z = static_cast<s64>(std::floor(centroid.Z / m_cellSize));
    // 21 signed bits per axis — ±1M cells, far beyond any level extent
    return ((x & 0x1FFFFF) << 42) | ((y & 0x1FFFFF) << 21) | (z & 0x1FFFFF);
}

void BrushManager::assignChunk(Brush& brush)
{
    const s64 newKey = chunkKeyFor(brush.bounds.getCenter());
    if (newKey == brush.chunkKey)
    {
        auto it = m_chunks.find(newKey);
        if (it != m_chunks.end())
        {
            it->second.dirty = true;
            return;
        }
        // fall through: chunk record missing, recreate below
    }

    // Remove from the previous chunk
    if (brush.chunkKey != -1)
    {
        auto old = m_chunks.find(brush.chunkKey);
        if (old != m_chunks.end())
        {
            auto& ids = old->second.brushIds;
            ids.erase(std::remove(ids.begin(), ids.end(), brush.id), ids.end());
            old->second.dirty = true;
        }
    }

    BrushChunk& chunk = m_chunks[newKey];
    if (chunk.brushIds.empty() && !chunk.node)
    {
        chunk.cell = vector3di(
            static_cast<s32>(std::floor(brush.bounds.getCenter().X / m_cellSize)),
            static_cast<s32>(std::floor(brush.bounds.getCenter().Y / m_cellSize)),
            static_cast<s32>(std::floor(brush.bounds.getCenter().Z / m_cellSize)));
    }
    if (std::find(chunk.brushIds.begin(), chunk.brushIds.end(), brush.id) == chunk.brushIds.end())
        chunk.brushIds.push_back(brush.id);
    chunk.dirty = true;
    brush.chunkKey = newKey;
}

// ---------------------------------------------------------------------------
// Lifecycle / editing
// ---------------------------------------------------------------------------

uint32_t BrushManager::addBrush(Brush brush)
{
    if (!brush.geometryValid && !BrushGeometry::rebuild(brush))
    {
        spdlog::warn("BrushManager::addBrush: rejected invalid plane set");
        return 0;
    }

    brush.id = m_nextId++;
    if (brush.name.empty())
        brush.name = "Brush " + std::to_string(brush.id);
    brush.chunkKey = -1;

    m_brushes.push_back(std::move(brush));
    assignChunk(m_brushes.back());
    return m_brushes.back().id;
}

void BrushManager::removeBrush(uint32_t id)
{
    for (auto it = m_brushes.begin(); it != m_brushes.end(); ++it)
    {
        if (it->id != id)
            continue;

        if (it->chunkKey != -1)
        {
            auto chunk = m_chunks.find(it->chunkKey);
            if (chunk != m_chunks.end())
            {
                auto& ids = chunk->second.brushIds;
                ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
                chunk->second.dirty = true;
            }
        }
        m_brushes.erase(it);
        return;
    }
}

bool BrushManager::replaceBrush(uint32_t id, const Brush& brush)
{
    Brush* existing = getBrush(id);
    if (!existing)
        return false;

    Brush candidate = brush;
    candidate.id = id;
    if (!BrushGeometry::rebuild(candidate))
        return false;

    candidate.chunkKey = existing->chunkKey;
    *existing = std::move(candidate);
    assignChunk(*existing);
    return true;
}

bool BrushManager::restoreBrush(const Brush& brush)
{
    Brush candidate = brush;
    if (!BrushGeometry::rebuild(candidate))
        return false;

    if (Brush* existing = getBrush(brush.id))
    {
        candidate.chunkKey = existing->chunkKey;
        *existing = std::move(candidate);
        assignChunk(*existing);
    }
    else
    {
        candidate.chunkKey = -1;
        m_brushes.push_back(std::move(candidate));
        assignChunk(m_brushes.back());
        if (m_nextId <= brush.id)
            m_nextId = brush.id + 1;
    }
    return true;
}

Brush* BrushManager::getBrush(uint32_t id)
{
    for (auto& b : m_brushes)
        if (b.id == id)
            return &b;
    return nullptr;
}

void BrushManager::markBrushDirty(uint32_t id)
{
    Brush* brush = getBrush(id);
    if (!brush)
        return;

    if (!BrushGeometry::rebuild(*brush))
    {
        // Invalid plane set mid-edit: keep it out of the compiled mesh but
        // keep the record so the editor can revert.
        spdlog::warn("BrushManager: brush {} has an invalid plane set", id);
    }
    assignChunk(*brush);
}

void BrushManager::rebuildDirtyChunks()
{
    for (auto it = m_chunks.begin(); it != m_chunks.end(); )
    {
        BrushChunk& chunk = it->second;

        if (chunk.brushIds.empty())
        {
            destroyChunkResources(chunk);
            it = m_chunks.erase(it);
            continue;
        }

        if (chunk.dirty)
        {
            // Geometry changed — any baked lightmap is stale
            clearChunkLightmap(chunk);

            std::vector<const Brush*> members;
            gatherChunkMembers(chunk, members);
            BrushCompiler::updateChunkNode(chunk, members);
            chunk.dirty = false;
            chunk.heavyPending = true;
            chunk.revision++;
        }

        if (chunk.heavyPending && !m_deferHeavy)
        {
            compileHeavy(chunk);
            chunk.heavyPending = false;
        }

        ++it;
    }
}

void BrushManager::compileAll()
{
    for (auto& pair : m_chunks)
        destroyChunkResources(pair.second);
    m_chunks.clear();

    for (auto& brush : m_brushes)
    {
        brush.chunkKey = -1;
        if (!BrushGeometry::rebuild(brush))
        {
            spdlog::warn("BrushManager::compileAll: brush {} ('{}') has an invalid plane set — skipped",
                         brush.id, brush.name);
            continue;
        }
        assignChunk(brush);
    }

    rebuildDirtyChunks();
}

void BrushManager::clearAll()
{
    for (auto& pair : m_chunks)
        destroyChunkResources(pair.second);
    m_chunks.clear();
    m_brushes.clear();
    m_nextId = 1;
    m_deferHeavy = false;
}

void BrushManager::setCellSize(float size)
{
    if (size < 1.0f)
        size = 1.0f;
    if (std::fabs(size - m_cellSize) < 1e-4f)
        return;
    m_cellSize = size;
    compileAll();
}

void BrushManager::gatherChunkMembers(const BrushChunk& chunk, std::vector<const Brush*>& out)
{
    out.clear();
    out.reserve(chunk.brushIds.size());
    for (uint32_t id : chunk.brushIds)
    {
        const Brush* b = getBrush(id);
        if (b && b->geometryValid)
            out.push_back(b);
    }
}

void BrushManager::compileHeavy(BrushChunk& chunk)
{
    auto* smgr = RenderManager::Get()->sceneManager();

    // Triangle selector for editor ray picking (mesh-exact, octree)
    if (chunk.node && chunk.node->getMesh())
    {
        auto* selector = smgr->createOctreeTriangleSelector(
            chunk.node->getMesh()->getMesh(0), chunk.node);
        if (selector)
        {
            chunk.node->setTriangleSelector(selector);
            selector->drop();
        }
    }

    // PhysX static collision, cooked from a collision-inclusive mesh
    // (FACE_NODRAW faces still block movement and bullets).
    if (chunk.physicsActor)
    {
        chunk.physicsActor->release();
        chunk.physicsActor = nullptr;
    }

    if (!PhysicsManager::Get())
        return;

    std::vector<const Brush*> members;
    gatherChunkMembers(chunk, members);
    if (members.empty())
        return;

    irr::scene::SMesh* collisionMesh = BrushCompiler::buildChunkMesh(members, /*includeNoDraw=*/true);
    if (!collisionMesh)
        return;

    // Chunk verts are world-space — cook at the identity transform.
    PhysicsManager::Get()->cookStaticTriangleMeshFromMemory(collisionMesh, chunk.physicsActor);
    collisionMesh->drop();
}

void BrushManager::destroyChunkResources(BrushChunk& chunk)
{
    clearChunkLightmap(chunk);

    if (chunk.physicsActor)
    {
        chunk.physicsActor->release();
        chunk.physicsActor = nullptr;
    }
    if (chunk.node)
    {
        chunk.node->remove();
        chunk.node = nullptr;
    }
}

void BrushManager::clearChunkLightmap(BrushChunk& chunk)
{
    if (!chunk.lightmap)
        return;

    if (chunk.lightmap->texture)
    {
        if (chunk.node)
        {
            for (irr::u32 m = 0; m < chunk.node->getMaterialCount(); m++)
            {
                if (chunk.node->getMaterial(m).TextureLayer[1].Texture == chunk.lightmap->texture)
                    chunk.node->getMaterial(m).setTexture(1, nullptr);
            }
        }
        RenderManager::Get()->driver()->removeTexture(chunk.lightmap->texture);
    }
    delete chunk.lightmap;
    chunk.lightmap = nullptr;
}

BrushChunk* BrushManager::getChunkByKey(irr::s64 key)
{
    auto it = m_chunks.find(key);
    return (it != m_chunks.end()) ? &it->second : nullptr;
}

std::vector<irr::scene::IAnimatedMeshSceneNode*> BrushManager::getOccluderNodes()
{
    std::vector<irr::scene::IAnimatedMeshSceneNode*> nodes;
    for (auto& pair : m_chunks)
        if (pair.second.node && !pair.second.dirty)
            nodes.push_back(pair.second.node);
    return nodes;
}

std::vector<BrushManager::BakeTarget> BrushManager::getBakeTargets()
{
    std::vector<BakeTarget> targets;
    for (auto& pair : m_chunks)
    {
        BrushChunk& chunk = pair.second;
        if (!chunk.node || chunk.dirty)
            continue;

        bool wantsLightmap = false;
        for (uint32_t id : chunk.brushIds)
        {
            const Brush* b = getBrush(id);
            if (b && b->geometryValid && b->receivesLightmap)
            {
                wantsLightmap = true;
                break;
            }
        }
        if (!wantsLightmap)
            continue;

        BakeTarget t;
        t.chunkKey = pair.first;
        t.revision = chunk.revision;
        t.node = chunk.node;
        targets.push_back(t);
    }
    return targets;
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

bool BrushManager::raycastBrushes(const vector3df& origin, const vector3df& dir,
                                  float maxDist, BrushRayHit& out)
{
    bool hit = false;
    float bestT = maxDist;

    for (const auto& brush : m_brushes)
    {
        if (!brush.geometryValid)
            continue;

        float t = 0.0f;
        int face = -1;
        if (BrushGeometry::raycast(brush, origin, dir, bestT, t, face) && t < bestT)
        {
            bestT = t;
            out.brushId = brush.id;
            out.faceIndex = face;
            out.t = t;
            out.point = origin + dir * t;
            hit = true;
        }
    }
    return hit;
}

BrushChunk* BrushManager::getChunkFromNode(irr::scene::ISceneNode* node)
{
    if (!node)
        return nullptr;
    for (auto& pair : m_chunks)
        if (pair.second.node == node)
            return &pair.second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

void BrushManager::serialize(cereal::XMLOutputArchive& ar)
{
    ar.setNextName("brushes");
    ar.startNode();
    ar(cereal::make_nvp("cellSize", m_cellSize));
    ar(cereal::make_nvp("nextId", m_nextId));
    ar(cereal::make_nvp("count", static_cast<uint32_t>(m_brushes.size())));
    for (auto& brush : m_brushes)
        ar(cereal::make_nvp("brush", brush));
    ar.finishNode();
}

void BrushManager::deserialize(cereal::XMLInputArchive& ar)
{
    clearAll();

    try
    {
        ar.startNode();     // <brushes>

        ar(cereal::make_nvp("cellSize", m_cellSize));
        ar(cereal::make_nvp("nextId", m_nextId));

        uint32_t count = 0;
        ar(cereal::make_nvp("count", count));
        m_brushes.reserve(count);

        uint32_t maxId = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            Brush brush;
            ar(cereal::make_nvp("brush", brush));
            if (brush.id > maxId)
                maxId = brush.id;
            m_brushes.push_back(std::move(brush));
        }
        if (m_nextId <= maxId)
            m_nextId = maxId + 1;

        ar.finishNode();    // </brushes>
    }
    catch (cereal::Exception& ex)
    {
        spdlog::warn("BrushManager::deserialize: failed to read brushes — {}", ex.what());
    }

    compileAll();
}
