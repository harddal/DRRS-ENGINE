#pragma once

#include <vector>

#include "Engine/Brush/BrushData.h"

struct BrushChunk;

// ---------------------------------------------------------------------------
// BrushCompiler — turns a chunk's member brushes into renderable geometry.
//
// One CMeshBuffer<S3DVertex2TCoords> per material+shader bucket (split past
// the u16 index limit), UV1 from the face's Valve-220 axes, UV2 = UV1 until
// the lightmap baker overwrites it.  Chunk vertices are in WORLD space; the
// chunk node sits at the origin, so its recalculated AABB drives Irrlicht's
// per-node EAC_BOX frustum culling — chunk culling for free in drawAll(),
// the shadow pass and the depth pre-pass.
// ---------------------------------------------------------------------------

namespace BrushCompiler
{
    // What buildChunkMesh emits.  Tool brushes (contentFlags != 0) never
    // reach the runtime render mesh or the structural collision cook — their
    // clip bits get separate query-filtered actors via the CLIP filter.
    enum class MeshFilter
    {
        RENDER,     // structural brushes, FACE_NODRAW faces skipped
        COLLISION,  // structural brushes, FACE_NODRAW faces included
        CLIP,       // tool brushes whose clipMask() equals clipMask, all faces
        TOOL,       // all tool brushes, all faces — editor overlay mesh
                    // (vertices emitted with alpha for EMT_TRANSPARENT_VERTEX_ALPHA)
    };

    // Build a world-space SMesh from the given brushes per the filter.
    // Returns nullptr when there is nothing to emit; caller drops.
    irr::scene::SMesh* buildChunkMesh(const std::vector<const Brush*>& brushes,
                                      MeshFilter filter, irr::u32 clipMask = 0);

    // Create/refresh/destroy the chunk's scene node from its member brushes,
    // using the rebuild-in-place pattern (new SAnimatedMesh + setMesh, then
    // material flags re-applied).  Does NOT touch collision or selectors —
    // those are the "heavy" path in BrushManager, deferred during drags.
    void updateChunkNode(BrushChunk& chunk, const std::vector<const Brush*>& brushes);
}
