#include "Engine/Brush/BrushManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

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
    // Mover source brushes are never chunk members — pull them out of any
    // chunk they were in (conversion path) and leave them unassigned.
    if (brush.isMoverBrush())
    {
        if (brush.chunkKey != -1)
        {
            auto old = m_chunks.find(brush.chunkKey);
            if (old != m_chunks.end())
            {
                auto& ids = old->second.brushIds;
                ids.erase(std::remove(ids.begin(), ids.end(), brush.id), ids.end());
                old->second.dirty = true;
            }
            brush.chunkKey = -1;
        }
        return;
    }

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

    // Mover groups compile outside the chunk system — refresh their cached
    // entity meshes whenever the world recompiles (scene load, cell resize).
    registerMoverMeshes();
}

void BrushManager::clearAll()
{
    // Drop cached mover meshes so a new scene can't inherit stale geometry
    // (entity nodes keep their own reference; cache removal is safe).
    if (RenderManager::Get())
    {
        auto* cache = RenderManager::Get()->sceneManager()->getMeshCache();
        for (auto& group : getMoverGroups())
        {
            if (auto* old = cache->getMeshByName(moverMeshName(group.first).c_str()))
                cache->removeMesh(old);
        }
    }

    for (auto& pair : m_chunks)
        destroyChunkResources(pair.second);
    m_chunks.clear();
    m_brushes.clear();
    m_nextId = 1;
    m_deferHeavy = false;
}

void BrushManager::setToolOverlayVisible(bool visible)
{
    m_toolOverlayVisible = visible;
    for (auto& pair : m_chunks)
        if (pair.second.toolNode)
            pair.second.toolNode->setVisible(visible);
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
    // (FACE_NODRAW faces still block movement — not hitscans, which trace the
    // render-mesh selector).  Tool brushes are excluded here; their clip bits
    // get separate query-filtered actors below.
    if (chunk.physicsActor)
    {
        chunk.physicsActor->release();
        chunk.physicsActor = nullptr;
    }
    for (auto* actor : chunk.clipActors)
        actor->release();
    chunk.clipActors.clear();

    if (!PhysicsManager::Get())
        return;

    std::vector<const Brush*> members;
    gatherChunkMembers(chunk, members);
    if (members.empty())
        return;

    // Chunk verts are world-space — cook at the identity transform.
    irr::scene::SMesh* collisionMesh =
        BrushCompiler::buildChunkMesh(members, BrushCompiler::MeshFilter::COLLISION);
    if (collisionMesh)
    {
        PhysicsManager::Get()->cookStaticTriangleMeshFromMemory(collisionMesh, chunk.physicsActor);
        collisionMesh->drop();
    }

    // Clip brushes: one actor per distinct clip-bit combination so each
    // shape's query filter word carries exactly what it blocks.  Trigger/sky
    // brushes without clip bits cook nothing.
    std::vector<irr::u32> signatures;
    for (const Brush* b : members)
    {
        const irr::u32 sig = b->clipMask();
        if (sig != 0 &&
            std::find(signatures.begin(), signatures.end(), sig) == signatures.end())
            signatures.push_back(sig);
    }

    for (irr::u32 sig : signatures)
    {
        irr::scene::SMesh* clipMesh =
            BrushCompiler::buildChunkMesh(members, BrushCompiler::MeshFilter::CLIP, sig);
        if (!clipMesh)
            continue;

        irr::u32 word0 = 0;
        if (sig & CONTENT_CLIP_PLAYER)  word0 |= RHG_CLIP_PLAYER;
        if (sig & CONTENT_CLIP_MONSTER) word0 |= RHG_CLIP_MONSTER;
        if (sig & CONTENT_CLIP_WEAPON)  word0 |= RHG_CLIP_WEAPON;

        physx::PxRigidStatic* actor = nullptr;
        PhysicsManager::Get()->cookStaticTriangleMeshFromMemory(
            clipMesh, actor,
            physx::PxVec3(0, 0, 0), physx::PxQuat(0, physx::PxVec3(0, 1, 0)),
            physx::PxVec3(1, 1, 1), word0);
        clipMesh->drop();
        if (actor)
            chunk.clipActors.push_back(actor);
    }
}

irr::scene::SMesh* BrushManager::buildNavGeometry()
{
    std::vector<const Brush*> all;
    all.reserve(m_brushes.size());
    for (const auto& b : m_brushes)
        if (b.geometryValid && !b.isMoverBrush())
            all.push_back(&b);
    if (all.empty())
        return nullptr;

    // Structural world geometry — same mesh the PhysX collision cook uses,
    // so the navmesh matches what NPC movement probes actually collide with.
    irr::scene::SMesh* mesh =
        BrushCompiler::buildChunkMesh(all, BrushCompiler::MeshFilter::COLLISION);

    // Monsterclip blocks NPC movement, so it must block pathfinding too —
    // rasterize those volumes as solid obstructions.
    std::vector<irr::u32> signatures;
    for (const Brush* b : all)
    {
        const irr::u32 sig = b->clipMask();
        if ((sig & CONTENT_CLIP_MONSTER) &&
            std::find(signatures.begin(), signatures.end(), sig) == signatures.end())
            signatures.push_back(sig);
    }

    for (irr::u32 sig : signatures)
    {
        irr::scene::SMesh* clip =
            BrushCompiler::buildChunkMesh(all, BrushCompiler::MeshFilter::CLIP, sig);
        if (!clip)
            continue;
        if (!mesh)
        {
            mesh = clip;
        }
        else
        {
            for (irr::u32 b = 0; b < clip->getMeshBufferCount(); b++)
                mesh->addMeshBuffer(clip->getMeshBuffer(b));   // grabs the buffer
            clip->drop();
        }
    }

    if (mesh)
        mesh->recalculateBoundingBox();
    return mesh;
}

// ---------------------------------------------------------------------------
// Mover brushes — SOLID_ENTITY groups compiled to pivot-local entity meshes,
// delivered through Irrlicht's mesh cache under moverMeshName(owner)
// ---------------------------------------------------------------------------

namespace
{
    irr::core::vector3df snapToGrid(const vector3df& v)
    {
        const float q = BrushGeometry::GRID_QUANTUM;
        return vector3df(std::floor(v.X / q + 0.5f) * q,
                         std::floor(v.Y / q + 0.5f) * q,
                         std::floor(v.Z / q + 0.5f) * q);
    }
}

irr::core::vector3df BrushManager::markAsMover(const std::vector<uint32_t>& ids, const std::string& owner)
{
    for (uint32_t id : ids)
    {
        Brush* b = getBrush(id);
        if (!b)
            continue;
        b->contentFlags = 0;        // tool semantics don't compose with movers
        b->receiver.clear();
        b->solidClass = SOLID_ENTITY;
        b->owner = owner;
        markBrushDirty(id);         // re-derive + pull out of its chunk
    }

    vector3df pivot;
    registerMoverMesh(owner, &pivot);
    return pivot;
}

void BrushManager::revertMover(const std::string& owner)
{
    if (RenderManager::Get())
    {
        auto* cache = RenderManager::Get()->sceneManager()->getMeshCache();
        if (auto* old = cache->getMeshByName(moverMeshName(owner).c_str()))
            cache->removeMesh(old);
    }

    for (auto& b : m_brushes)
    {
        if (b.owner != owner || !b.isMoverBrush())
            continue;
        b.solidClass = SOLID_WORLD;
        b.owner.clear();
        markBrushDirty(b.id);       // rejoin the world chunks
    }
}

bool BrushManager::registerMoverMesh(const std::string& owner, irr::core::vector3df* outPivot)
{
    std::vector<const Brush*> group;
    aabbox3df bounds;
    bool first = true;
    for (const auto& b : m_brushes)
    {
        if (!b.isMoverBrush() || b.owner != owner || !b.geometryValid)
            continue;
        group.push_back(&b);
        if (first) { bounds = b.bounds; first = false; }
        else       { bounds.addInternalBox(b.bounds); }
    }
    if (group.empty())
        return false;

    const vector3df pivot = snapToGrid(bounds.getCenter());
    if (outPivot)
        *outPivot = pivot;

    irr::scene::SMesh* mesh = BrushCompiler::buildMoverMesh(group, pivot);
    if (!mesh)
        return false;

    auto* cache = RenderManager::Get()->sceneManager()->getMeshCache();
    const irr::io::path name(moverMeshName(owner).c_str());
    if (auto* old = cache->getMeshByName(name))
        cache->removeMesh(old);

    auto* anim = new irr::scene::SAnimatedMesh(mesh);
    mesh->drop();
    anim->setHardwareMappingHint(irr::scene::EHM_STATIC);
    cache->addMesh(name, anim);
    anim->drop();
    return true;
}

void BrushManager::registerMoverMeshes()
{
    std::vector<std::string> owners;
    for (const auto& b : m_brushes)
    {
        if (b.isMoverBrush() && !b.owner.empty() &&
            std::find(owners.begin(), owners.end(), b.owner) == owners.end())
            owners.push_back(b.owner);
    }
    for (const auto& owner : owners)
        registerMoverMesh(owner);
}

std::vector<std::pair<std::string, int>> BrushManager::getMoverGroups() const
{
    std::vector<std::pair<std::string, int>> groups;
    for (const auto& b : m_brushes)
    {
        if (!b.isMoverBrush() || b.owner.empty())
            continue;
        auto it = std::find_if(groups.begin(), groups.end(),
            [&](const std::pair<std::string, int>& g) { return g.first == b.owner; });
        if (it == groups.end())
            groups.emplace_back(b.owner, 1);
        else
            it->second++;
    }
    return groups;
}

void BrushManager::destroyChunkResources(BrushChunk& chunk)
{
    clearChunkLightmap(chunk);

    if (chunk.physicsActor)
    {
        chunk.physicsActor->release();
        chunk.physicsActor = nullptr;
    }
    for (auto* actor : chunk.clipActors)
        actor->release();
    chunk.clipActors.clear();
    if (chunk.toolNode)
    {
        // Unregister before remove — RenderManager walks m_debugNodes every frame
        RenderManager::Get()->unregisterDebugNode(chunk.toolNode);
        chunk.toolNode->remove();
        chunk.toolNode = nullptr;
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
        if (!brush.geometryValid || brush.isMoverBrush())
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
// Chunk lightmap persistence — same PNG + LMSV uvmesh formats as the prop and
// entity paths (PropManager::collectPropLightmapFiles / loadPropLightmaps),
// keyed by packed chunk key.
// ---------------------------------------------------------------------------

std::vector<BrushManager::ChunkLightmapFiles> BrushManager::collectChunkLightmapFiles(
    irr::video::IVideoDriver* driver,
    const std::string&        tempDir)
{
    std::vector<ChunkLightmapFiles> result;

    for (auto& pair : m_chunks)
    {
        BrushChunk& chunk = pair.second;
        if (!chunk.lightmap || !chunk.lightmap->texture || !chunk.node)
            continue;

        ChunkLightmapFiles clf;
        clf.chunkKey = pair.first;

        // --- PNG: write to temp file, read back ---
        {
            irr::video::ITexture* tex = chunk.lightmap->texture;
            irr::video::IImage* img = driver->createImage(
                tex, position2di(0, 0), tex->getSize());

            if (img)
            {
                const std::string tmpPath =
                    tempDir + "/brush_lightmap_" + std::to_string(pair.first) + "_tmp.png";

                driver->writeImageToFile(img, tmpPath.c_str());
                img->drop();

                std::ifstream ifs(tmpPath, std::ios::binary);
                if (ifs)
                {
                    clf.pngBytes.assign(
                        std::istreambuf_iterator<char>(ifs),
                        std::istreambuf_iterator<char>());
                }
                std::remove(tmpPath.c_str());
            }
        }

        // --- UVMesh: extract from the EVT_2TCOORDS chunk mesh (LMSV binary) ---
        {
            irr::scene::IMesh* mesh =
                chunk.node->getMesh() ? chunk.node->getMesh()->getMesh(0) : nullptr;
            if (!mesh)
            {
                spdlog::warn("BrushManager::collectChunkLightmapFiles: no mesh on chunk {}", pair.first);
                continue;
            }

            const u32 bufCount = mesh->getMeshBufferCount();

            std::vector<uint8_t> buf;
            auto write32 = [&](uint32_t v) {
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
            };
            auto write16 = [&](uint16_t v) {
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 2);
            };
            auto writeF = [&](float v) {
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
            };

            write32(0x4C4D5356); // magic 'LMSV'
            write32(1);          // version
            write32(bufCount);

            for (u32 b = 0; b < bufCount; b++)
            {
                irr::scene::IMeshBuffer* mbuf = mesh->getMeshBuffer(b);
                const u32 vertCount  = mbuf->getVertexCount();
                const u32 indexCount = mbuf->getIndexCount();

                write32(vertCount);
                write32(indexCount);

                const irr::video::S3DVertex2TCoords* verts =
                    static_cast<const irr::video::S3DVertex2TCoords*>(mbuf->getVertices());

                for (u32 i = 0; i < vertCount; i++)
                {
                    writeF(verts[i].Pos.X);      writeF(verts[i].Pos.Y);      writeF(verts[i].Pos.Z);
                    writeF(verts[i].Normal.X);   writeF(verts[i].Normal.Y);   writeF(verts[i].Normal.Z);
                    writeF(verts[i].TCoords.X);  writeF(verts[i].TCoords.Y);
                    writeF(verts[i].TCoords2.X); writeF(verts[i].TCoords2.Y);
                }

                const u16* indices = static_cast<const u16*>(mbuf->getIndices());
                for (u32 i = 0; i < indexCount; i++)
                    write16(static_cast<uint16_t>(indices[i]));
            }

            clf.uvmeshBytes = std::move(buf);
        }

        if (!clf.pngBytes.empty() && !clf.uvmeshBytes.empty())
            result.push_back(std::move(clf));
        else
            spdlog::warn("BrushManager::collectChunkLightmapFiles: skipping chunk {} — data empty", pair.first);
    }

    return result;
}

void BrushManager::loadChunkLightmaps(irr::io::IFileSystem* fs, irr::video::IVideoDriver* driver)
{
    size_t applied = 0;

    for (auto& pair : m_chunks)
    {
        BrushChunk& chunk = pair.second;
        if (!chunk.node)
            continue;

        const std::string baseName = "brush_lightmap_" + std::to_string(pair.first);

        irr::io::IReadFile* uvFile = fs->createAndOpenFile((baseName + ".uvmesh").c_str());
        if (!uvFile)
            continue; // no lightmap baked for this chunk

        std::vector<uint8_t> uvData(static_cast<size_t>(uvFile->getSize()));
        uvFile->read(uvData.data(), static_cast<u32>(uvData.size()));
        uvFile->drop();

        const uint8_t* ptr = uvData.data();
        const uint8_t* end = ptr + uvData.size();

        auto readU32 = [&]() -> uint32_t { uint32_t v; memcpy(&v, ptr, 4); ptr += 4; return v; };
        auto readU16 = [&]() -> uint16_t { uint16_t v; memcpy(&v, ptr, 2); ptr += 2; return v; };
        auto readF32 = [&]() -> float    { float    v; memcpy(&v, ptr, 4); ptr += 4; return v; };

        if (readU32() != 0x4C4D5356)
        {
            spdlog::warn("BrushManager::loadChunkLightmaps: bad magic for chunk {}, skipping", pair.first);
            continue;
        }
        /*version =*/ readU32();
        const uint32_t bufCount = readU32();

        MeshLightmap* lm = new MeshLightmap();
        lm->buffers.resize(bufCount);

        for (uint32_t b = 0; b < bufCount && ptr < end; b++)
        {
            const uint32_t vertCount  = readU32();
            const uint32_t indexCount = readU32();

            lm->buffers[b].vertices.resize(vertCount);
            lm->buffers[b].indices.resize(indexCount);

            for (uint32_t vi = 0; vi < vertCount && ptr < end; vi++)
            {
                MeshLightmap::Vertex& v = lm->buffers[b].vertices[vi];
                v.objPos.X    = readF32(); v.objPos.Y    = readF32(); v.objPos.Z    = readF32();
                v.objNormal.X = readF32(); v.objNormal.Y = readF32(); v.objNormal.Z = readF32();
                v.uv1.X = readF32(); v.uv1.Y = readF32();
                v.uv2.X = readF32(); v.uv2.Y = readF32();
            }
            for (uint32_t ii = 0; ii < indexCount && ptr < end; ii++)
                lm->buffers[b].indices[ii] = readU16();
        }

        lm->texture = driver->getTexture((baseName + ".png").c_str());
        if (!lm->texture)
        {
            spdlog::warn("BrushManager::loadChunkLightmaps: texture '{}.png' not found", baseName);
            delete lm;
            continue;
        }
        lm->width  = lm->texture->getSize().Width;
        lm->height = lm->texture->getSize().Height;

        clearChunkLightmap(chunk);
        chunk.lightmap = lm;

        LightmapBaker::applyLightmapUVsToNode(*lm, chunk.node);

        // Free CPU vertex data — now on GPU
        { std::vector<MeshLightmap::BufferData>().swap(lm->buffers); }
        applied++;
    }

    if (applied > 0)
        spdlog::info("BrushManager::loadChunkLightmaps: applied {} chunk lightmap(s)", applied);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

void BrushManager::serialize(cereal::XMLOutputArchive& ar)
{
    ar.setNextName("brushes");
    ar.startNode();
    ar(cereal::make_nvp("version", BRUSHES_XML_VERSION));
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

        // Schema version: files older than BRUSHES_XML_VERSION 1 have no
        // <version> element — peek the first child's name instead of reading
        // blind (a missing NVP would throw and abort the whole load).
        uint32_t version = 0;
        const char* firstNode = ar.getNodeName();
        if (firstNode && std::strcmp(firstNode, "version") == 0)
            ar(cereal::make_nvp("version", version));

        ar(cereal::make_nvp("cellSize", m_cellSize));
        ar(cereal::make_nvp("nextId", m_nextId));

        uint32_t count = 0;
        ar(cereal::make_nvp("count", count));
        m_brushes.reserve(count);

        uint32_t maxId = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            Brush brush;
            ar.setNextName("brush");
            ar.startNode();
            brush.load(ar, version);
            ar.finishNode();
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
