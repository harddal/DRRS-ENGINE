#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>

#include "Engine/Brush/BrushData.h"

#include "cereal/cereal.hpp"
#include "cereal/archives/xml.hpp"

namespace physx { class PxRigidStatic; }
struct MeshLightmap;

// ---------------------------------------------------------------------------
// BrushChunk
// Runtime-only render/collision cache for one world grid cell.  Never
// serialized — recompiled from brush plane data on load and on edit.
// ---------------------------------------------------------------------------
struct BrushChunk
{
    irr::core::vector3di                cell;               // floor(centroid / cellSize)
    std::vector<uint32_t>               brushIds;           // members, by brush-AABB centroid
    irr::scene::IAnimatedMeshSceneNode* node = nullptr;     // world-space batched mesh
    // Editor-only semi-transparent tool-brush overlay.  Sibling of `node`
    // (never a child — the lightmap baker extracts meshes from `node`),
    // registered as a RenderManager debug node, no triangle selector.
    irr::scene::IAnimatedMeshSceneNode* toolNode = nullptr;
    physx::PxRigidStatic*               physicsActor = nullptr;
    // One extra cooked actor per distinct clip-bit combination among the
    // chunk's tool brushes, query-filtered with RHG_CLIP_* words only (no
    // RHG_STATIC) so masked queries can skip them.
    std::vector<physx::PxRigidStatic*>  clipActors;
    MeshLightmap*                       lightmap = nullptr; // baked lightmap; invalidated on recompile
    uint32_t revision = 0;      // bumped on every recompile — stale async bakes are discarded
    bool dirty = true;          // visual mesh needs recompiling
    bool heavyPending = false;  // collision cook + triangle selector deferred (mid-drag)
};

// ---------------------------------------------------------------------------
// BrushManager
// Singleton that owns all CSG brushes in the current scene (PropManager
// pattern — no ECS involvement).  Brush plane sets are the source of truth;
// chunks are regenerable caches keyed by world grid cell.
// ---------------------------------------------------------------------------
class BrushManager
{
public:
    BrushManager();
    ~BrushManager();

    static BrushManager* Get() { return s_Instance; }

    // -----------------------------------------------------------------------
    // Lifecycle / editing
    // -----------------------------------------------------------------------

    // Takes ownership; rebuilds geometry, assigns id + chunk, marks dirty.
    // Returns the brush id, or 0 if the plane set is invalid.
    uint32_t addBrush(Brush brush);

    void removeBrush(uint32_t id);

    // Undo/restore path: replace the brush's faces wholesale and re-derive.
    // Returns false (leaving the old brush untouched) if the new plane set is
    // invalid.
    bool replaceBrush(uint32_t id, const Brush& brush);

    // Undo path: insert-or-replace keeping brush.id (re-creates deleted
    // brushes with their original identity).
    bool restoreBrush(const Brush& brush);

    Brush*       getBrush(uint32_t id);
    const std::vector<Brush>& getAllBrushes() const { return m_brushes; }

    // Mutable access for runtime-only per-brush state (trigger edge flags).
    // Plane/face edits must still go through replaceBrush/markBrushDirty.
    std::vector<Brush>& getAllBrushesMutable() { return m_brushes; }

    // Re-derive geometry + chunk assignment after any plane edit.
    void markBrushDirty(uint32_t id);

    // Per-frame (EditorState::update): recompile dirty chunks, drop empty
    // ones.  Collision + selector rebuilds are skipped while deferHeavy is on
    // (drag in progress) and caught up on the first call after it turns off.
    void rebuildDirtyChunks();

    void setDeferHeavyRebuilds(bool defer) { m_deferHeavy = defer; }

    // Full recompile from plane data (after deserialize / cell size change).
    void compileAll();

    // Destroy all brushes, chunk nodes and physics actors.
    void clearAll();

    float cellSize() const { return m_cellSize; }
    void  setCellSize(float size);      // triggers compileAll

    // Editor overlay for tool brushes (chunk toolNode visibility)
    bool isToolOverlayVisible() const { return m_toolOverlayVisible; }
    void setToolOverlayVisible(bool visible);

    // -----------------------------------------------------------------------
    // Picking
    // -----------------------------------------------------------------------
    struct BrushRayHit
    {
        uint32_t brushId = 0;
        int      faceIndex = -1;
        irr::core::vector3df point;
        float    t = 0.0f;
    };

    // Exact ray vs plane sets over all brushes; nearest hit wins.
    bool raycastBrushes(const irr::core::vector3df& origin,
                        const irr::core::vector3df& dir,
                        float maxDist, BrushRayHit& out);

    // Chunk lookup for the editor's node-based pick path (checked alongside
    // PropManager::getPropFromNode, before entity-ID resolution).
    BrushChunk* getChunkFromNode(irr::scene::ISceneNode* node);

    BrushChunk* getChunkByKey(irr::s64 key);

    // -----------------------------------------------------------------------
    // Lightmap integration (LightmapBaker)
    // -----------------------------------------------------------------------
    struct BakeTarget
    {
        irr::s64 chunkKey = 0;
        uint32_t revision = 0;
        irr::scene::IAnimatedMeshSceneNode* node = nullptr;
    };

    // Chunks with a compiled node whose members include at least one
    // receivesLightmap brush.  Chunk verts are world-space (identity
    // transform for the baker).
    std::vector<BakeTarget> getBakeTargets();

    // Remove the chunk's lightmap texture + CPU data.  Called before every
    // recompile — an edited chunk's bake is stale by definition.
    void clearChunkLightmap(BrushChunk& chunk);

    // All compiled chunk nodes — shadow occluders for the baker regardless of
    // whether the chunk itself receives a lightmap.
    std::vector<irr::scene::IAnimatedMeshSceneNode*> getOccluderNodes();

    // Baked chunk lightmaps encoded for the scene zip (PNG + LMSV uvmesh),
    // keyed by packed chunk key — deterministic across save/load for the same
    // brush set and cell size (mirrors PropManager::PropLightmapFiles).
    struct ChunkLightmapFiles
    {
        irr::s64             chunkKey = 0;
        std::vector<uint8_t> pngBytes;
        std::vector<uint8_t> uvmeshBytes;
    };

    // tempDir must be writable; temporary PNG files are deleted on return.
    std::vector<ChunkLightmapFiles> collectChunkLightmapFiles(
        irr::video::IVideoDriver* driver, const std::string& tempDir);

    // Reattach lightmaps from the mounted scene zip.  Must run after
    // deserialize (chunks recompiled and clean) and before the zip unmounts.
    void loadChunkLightmaps(irr::io::IFileSystem* fs, irr::video::IVideoDriver* driver);

    // -----------------------------------------------------------------------
    // Serialization (called by WorldManager during import/export)
    // -----------------------------------------------------------------------
    void serialize(cereal::XMLOutputArchive& ar);
    void deserialize(cereal::XMLInputArchive& ar);    // ends with compileAll()

private:
    static BrushManager* s_Instance;

    std::vector<Brush> m_brushes;
    std::unordered_map<irr::s64, BrushChunk> m_chunks;
    uint32_t m_nextId = 1;              // serialized; ids never reused
    float    m_cellSize = 16.0f;        // serialized world-cell edge length
    bool     m_deferHeavy = false;
    bool     m_toolOverlayVisible = true;   // editor UI toggle, not serialized

    irr::s64 chunkKeyFor(const irr::core::vector3df& centroid) const;

    // Move the brush into the chunk its centroid now falls in, dirtying both
    // the old and new chunk.
    void assignChunk(Brush& brush);

    void gatherChunkMembers(const BrushChunk& chunk, std::vector<const Brush*>& out);
    void compileHeavy(BrushChunk& chunk);   // triangle selector + PhysX cook
    void destroyChunkResources(BrushChunk& chunk);
};
